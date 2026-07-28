#pragma once

#include <functional>
#include <optional>
#include <string>

#include "display.hpp"

namespace fm {

// Raw keyboard input: reading bytes from the controlling tty, raw/
// cooked termios mode switching, escape-sequence recognition for
// special keys (arrows/page up/down), terminal-resize (SIGWINCH) and
// job-control (SIGTTOU/SIGCONT) signal handling, and raw-byte macro
// playback. Ported from key.c's read_m()/read_esc() and vt.c's
// vt_getch()/vt_init() (the raw-termios half of vt_init(); the
// alternate-screen/attribute half belongs to Display, see
// display.hpp).
//
// Deliberately excluded (belongs to future classes, not KeyInput):
//   - SIGSEGV (crash recovery) / SIGTERM / SIGHUP (session cleanup):
//     Application lifecycle, not keyboard input. Legacy's
//     save_signal() installed all of these plus KeyInput's signals in
//     one function; that responsibility is split here.
//   - Mapping a raw key or resolved SpecialKey to a mode command
//     number (the M_*/T_* dispatch inside legacy's read_m()): Mode/
//     Application's job. KeyInput only reports what was pressed.
//   - edit_line()/make_list() (the command-line text-editing widget):
//     LineEditor's job.
//   - stand_str() (file-name highlight rendering): ContentView's/
//     Display's job.
//   - Recorded-command keyboard macros (legacy's `p_km`, recorded via
//     M_ms/M_mm/M_me): tracing the legacy code shows these record
//     and replay *resolved M_*/T_* command numbers*, not raw key
//     bytes -- playback bypasses re-dispatch entirely and hands the
//     resolved value straight back to read_m()'s caller. That's a
//     Mode/Application-level concern (recording what the dispatcher
//     decided), not something KeyInput -- which only ever sees bytes
//     -- can meaningfully own. What *is* ported here is the other,
//     genuinely different mechanism: usilicrc's preset M-0..M-19
//     macros (legacy's `p_bm`), which replay as raw bytes fed back
//     through the same low-level read path a live keypress would use
//     -- see playRawMacro().
class KeyInput {
 public:
  enum class SpecialKey { Up, Down, Right, Left, PageUp, PageDown };

  explicit KeyInput(Display &display);
  virtual ~KeyInput();

  // -- raw byte reading --
  // Reads one raw byte, from a queued playRawMacro() sequence if one
  // is active, otherwise from the controlling tty (blocking up to the
  // VTIME timeout set by open()/setPolling()). Returns 0 on timeout/
  // EOF/error. A literal NUL byte read from the tty (Ctrl-Space on
  // many terminals) is reported as 0x80, matching legacy vt_getch()'s
  // K_at sentinel -- callers already have to special-case this
  // exactly as legacy callers did; it's not a new ambiguity.
  //
  // Checks a playRawMacro() queue before the real tty -- which is
  // also how tests exercise readEscapeSequence()'s real matching
  // logic end to end without a real tty (queue bytes via
  // playRawMacro(), no fake tty or subclass needed). Also virtual, so
  // a caller can substitute an entirely different byte source if it
  // ever needs to. (Legacy solved the "swap the byte source"
  // problem -- for its own, unrelated keyboard-macro-playback
  // feature -- by casting a 2-argument function to a 0-argument
  // function-pointer type and relying on whatever garbage was in the
  // unused argument registers; that was undefined behavior and
  // caused a real, previously-fixed segfault, see CLAUDE.md history
  // item 9. Neither playRawMacro() nor this being virtual reproduces
  // that hazard.)
  virtual int readByte();

  // -- raw/cooked mode, tty lifecycle --
  // Opens ttyPath and switches it to raw mode (no ICANON/ECHO/ISIG,
  // VMIN=0, VTIME=1 decisecond). Ported from vt_init(1)'s termios half.
  void open(const std::string &ttyPath);
  // Restores the tty to the mode it was in before open() and closes
  // it. Ported from vt_init(-1)'s termios half.
  void close();
  // Restores the tty to the mode it was in before open(), but leaves
  // it open -- for temporarily dropping out of raw mode (e.g. to run
  // an external program) and returning later via reenterRawMode().
  // Ported from vt_init(-2)'s termios half.
  void leaveRawMode();
  // Re-applies raw mode after a SIGCONT (job-control resume) or after
  // leaveRawMode(). Ported from vt_init(-9)/(3).
  void reenterRawMode();
  // Toggles the read timeout between polling (VTIME=1, the default)
  // and blocking (VTIME=0), used while replaying a macro at full
  // speed. Ported from vt_init(3)/(-3).
  void setPolling(bool polling);

  // -- escape-sequence recognition --
  // Call immediately after readByte() has returned ESC (0x1b). Reads
  // and consumes whatever further bytes are needed to identify one of
  // Display's 6 key sequences, returning which one matched, or
  // nullopt if the ESC wasn't followed by a recognized sequence
  // (including a bare ESC with nothing following). Ported from
  // read_esc(), with one deliberate correction: legacy's is_esc()
  // returned the plain int 0 both when nothing matched AND when Up
  // (index 0) matched, and its caller could not tell those apart --
  // an unrecognized escape sequence was silently treated as Up on any
  // terminal that defines a key_up capability (virtually all of
  // them). std::optional makes "no match" and "matched Up" distinct
  // by construction.
  std::optional<SpecialKey> readEscapeSequence();

  // -- signals --
  // Installs SIGWINCH/SIGTTOU/SIGCONT handlers for this instance (the
  // single active KeyInput -- usilic has only ever run as one
  // foreground input handler at a time, same assumption
  // TerminalDisplay makes for its own signal-adjacent global, see its
  // outc() comment). Ported from the subset of save_signal() that's
  // KeyInput's concern (see class comment for what's deliberately
  // left out).
  void installSignalHandlers();

  // True if a SIGWINCH arrived since the last call (consumes the
  // flag). Also re-queries the controlling terminal's size and calls
  // display_.setSize() with it. Ported from read_m()'s `Key.f.winch`
  // check plus vt_winsize().
  bool consumeResizeSignal();

  // Records which child pid should be SIGCONT'd if a SIGTTOU suspend
  // happens while it's the SHELL's foreground process. Ported from
  // tc_cursor()/key_winch()'s spid bookkeeping (see display.hpp for
  // why that logic doesn't belong on Display despite the misleading
  // legacy name vt_cursor()).
  void setForegroundChildPid(int pid);

  // Whether an error message is currently pending display -- affects
  // whether a SIGTTOU suspends just this process or the whole process
  // group. Ported from key_ttou()'s `s_S(S_error)` check; exposed as
  // a plain setter instead of a hidden Configuration/session-state
  // lookup so KeyInput doesn't need to know about that class.
  void setErrorPending(bool pending);

  // Called (if set) whenever SIGTTOU or SIGCONT fires, with this
  // process's pid -- ported from key_ttou()/key_cont()'s
  // `if (s_S(S_pid)) print_pid(getpid())` reporting hook, exposed as
  // a callback instead of a direct sub.c dependency.
  void setPidReporter(std::function<void(int pid)> reporter);

  // -- raw-byte macro playback --
  // Queues `bytes` to be returned one at a time by subsequent
  // readByte() calls in place of real tty input, until exhausted.
  // Ported from read_m()'s `p_bm` playback (usilicrc's preset M-0..
  // M-19 macros) -- see the class comment for why the *other* legacy
  // macro mechanism (`p_km`, recorded command playback) isn't ported
  // here.
  void playRawMacro(const std::string &bytes);
  bool isPlayingRawMacro() const;

  // Called by the process-wide SIGTTOU/SIGCONT trampolines installed
  // by installSignalHandlers() (which, being real signal handlers,
  // can only reach this instance through the single-active-KeyInput
  // pointer, not as ordinary private member calls) -- not intended
  // for other callers.
  void reportPid();
  bool errorPending() const;

 private:
  Display &display_;
  int fd_ = -1;
  struct RawState;
  RawState *raw_;  // pimpl: keeps <termios.h> out of this header

  std::string macroQueue_;
  std::size_t macroPos_ = 0;

  bool errorPending_ = false;
  std::function<void(int)> pidReporter_;
};

}  // namespace fm
