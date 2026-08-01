#pragma once

#include <functional>
#include <string>
#include <vector>

#include "charset.hpp"
#include "content_view.hpp"
#include "display.hpp"
#include "line_editor.hpp"
#include "pipe_buffer.hpp"

namespace fm {

// Options for a Shell session. Namespace-scope (not nested inside
// Shell) for the same reason ChildProcessOptions is: a nested
// struct's default member initializers aren't visible yet when the
// enclosing class parses a member function's default argument that
// references them -- see child_process.hpp's comment on
// ChildProcessOptions for the full story (a real bug, not
// theoretical, was caused by getting this wrong once already in this
// project).
struct ShellOptions {
  // Program + argv to exec onto the pty (e.g. {"bash"} or
  // {"/bin/tcsh"}). Deliberately not resolved here from $SHELL or a
  // tcbrc CHILD/SHELL directive -- which program to run is
  // Configuration's job (see child.c's save_child()/get_child()),
  // matching how ChildProcess never decides its own argv either.
  std::vector<std::string> argv = {"/bin/sh"};
  // Unique-enough sentinel woven into PS1 so Shell can recognize its
  // own prompt lines in the byte stream (legacy: s_S(S_mark)). Two
  // Shell instances (or a Shell and something else printing to the
  // same pty) must not share one process's mark, since PS1 itself
  // embeds it twice -- see buildSetupCommand()'s comment.
  std::string promptMark = "<usilic>";
  // Woven into PS1 between the two promptMark occurrences (legacy:
  // getenv(ENV_PROG), i.e. this program's own name) -- purely
  // decorative/diagnostic text in the prompt, not used for matching.
  std::string programName = "usilic";
  Charset::Encoding encoding = Charset::Encoding::Ascii;
  int rows = 24;
  int columns = 80;
  // Total scrollback content budget in bytes, forwarded to the
  // internal PipeBuffer (legacy: no equivalent -- the linked-list
  // scrollback just grew unbounded). PipeBuffer::overBudget() lets a
  // caller notice and e.g. warn/trim, matching the same knob
  // PipeBuffer already exposes for file/pipe content.
  long scrollbackBudget = 1 << 20;
};

// A SHELL-mode session: a pty-backed interactive child shell (bash/
// csh/...), its scrollback of completed prompt+output lines, and the
// in-progress command line being edited. Ported from shell.c's
// make_shell()/start_shell()/output_shell()/input_shell()/
// read_line()/write_shell(), and sub.c's open_pty() (kept
// near-verbatim -- see openPty() in shell.cpp).
//
// This is NOT a literal port. Legacy SHELL mode is three cooperating
// processes -- the main tcb process, a second "display" process
// forked purely to keep draining the pty while the main process
// blocks on a keypress, and the shell itself -- talking over eight
// hand-rolled pipes with SIGALRM abused as a message-bus doorbell
// (write_comm()/read_comm()/wait_comm() for synchronous requests,
// a second async push channel for "a new prompt appeared"). None of
// that exists here: with poll()/select() available, one process can
// multiplex the pty fd and the keyboard fd directly, so the entire
// IPC protocol -- and the duplicated PP/RV scrollback struct it was
// shuttling field-by-field across processes -- simply isn't needed.
// What's left, once that dissolves, is comparatively small: pty
// process lifecycle, recognizing the shell's own prompt in its
// output, tracking its reported cwd, and routing each incoming byte
// to either the scrollback or the live command line.
//
// Deliberately excluded (belongs to other already-ported or future
// classes, not Shell):
//   - Generic line-wrap/tab/overstrike-bold/underline parsing of pty
//     output: PipeBuffer's job. Shell owns one internally and feeds
//     it non-prompt bytes rather than reimplementing read_line()'s
//     ff.bs/ff.md/ff.us state machine.
//   - Scrollback storage and scroll mechanics (single-line/page
//     up/down, hardware-scroll-vs-redraw): ContentView's job. Shell
//     owns one internally instead of shell.c's own PP/RV linked list
//     plus next_line()/shell_row()/shell_page().
//   - Single-line editing mechanics (insert/delete/cursor movement/
//     kill-yank/UTF-8 column tracking): LineEditor's job. Shell owns
//     one internally for the in-progress command line; callers
//     forward ordinary keys straight to commandLine() rather than
//     Shell re-exposing insert/backspace/etc. itself.
//   - Raw keyboard byte reading, escape-sequence decoding, SIGWINCH
//     detection: already KeyInput's job. Shell only reacts to a
//     resize once the caller has decided one happened (onResize()),
//     and only receives ordinary command-line bytes/Enter/Tab once
//     the caller has decided a raw byte isn't some other special key.
//   - TAB-completion candidate generation (make_list()): explicitly
//     deferred to a future PathCompleter class, same as LineEditor's
//     own exclusion. Shell only knows how to ask a caller-supplied
//     provider and splice the answer back in.
//   - Command/directory history storage (save_history()/save_str()):
//     a future History class's job; Shell calls a caller-supplied
//     recorder callback rather than owning the storage.
//   - Actually running an external interactive PROGRAM (do_prog()'s
//     suspend-raw-mode/run-in-foreground/"Hit any key" dance):
//     Mode/Application's job, same split ChildProcess's own header
//     already draws for the non-interactive case. Shell only decides
//     *that* the typed command's trailing pipeline stage matches a
//     configured PROGRAM (via a caller-supplied predicate) and hands
//     off -- see submitLine(). The caller must remember the f_wait
//     lesson (CLAUDE.md history: an unconditional "Hit any key" pause
//     after running the program silently eats the user's very next
//     keystroke) when it implements that handoff; Shell itself has
//     nothing further to do once it's handed off.
//   - Cross-process single-instance socket/file handoff (from_main()):
//     an Application/session-singleton concern, not Shell's.
//   - Screen rendering primitives: Display's job, reached only
//     through the owned ContentView/LineEditor, same as everywhere
//     else in this codebase.
class Shell {
 public:
  Shell(Display &display, const ShellOptions &options);
  ~Shell();

  Shell(const Shell &) = delete;
  Shell &operator=(const Shell &) = delete;

  // -- pty/child process lifecycle --
  // Opens a pty, forks, execs options.argv onto the slave side, and
  // sends the PS1/prompt-marker setup command. False on any failure
  // (pty allocation, fork, or the shell exiting before the initial
  // handshake completes); Shell is left inert and start() may be
  // retried.
  bool start();
  bool alive() const;
  // The current child's pid, or -1 if not started -- exposed purely
  // so a caller (e.g. K-60/RestartShell's own tests) can confirm a
  // restart produced a genuinely new process, not the same one reused.
  int childPid() const;
  // SIGHUP + wait for the child to die, close the pty. Idempotent;
  // also called from the destructor. Ported from start_shell()'s
  // no-`mess` (shutdown) branch and output_shell()'s shell_hup/case
  // -1 teardown, collapsed into one path since there's no longer a
  // separate display process to tear down independently.
  void terminate();

  // The pty master fd, for the owner's poll()/select() loop. -1 if
  // not started.
  int ptyFd() const;

  // -- pty output --
  // Call when ptyFd() is readable. Does one non-blocking read and
  // processes whatever arrived; returns false on EOF/error (the
  // shell child exited or the pty is gone). Thin glue over
  // processOutputBytes() -- kept separate so tests can exercise the
  // parsing logic with synthetic byte sequences, without a real pty.
  bool onPtyReadable();
  void processOutputBytes(const std::string &bytes);

  // -- command line --
  LineEditor &commandLine();
  ContentView &scrollback();

  // Enter: decides whether the current command line's trailing
  // pipeline stage matches a configured interactive PROGRAM (via
  // programLauncher_, if set) and hands off, or otherwise writes the
  // line straight to the pty and records it via historyRecorder_ (if
  // set). Clears the command line either way. Ported from
  // input_shell()'s '\r' case (the pipe-splitting/find_prog() lookup
  // logic itself; do_prog() and the PROGRAM table lookup are not
  // ported here, see the class comment).
  void submitLine();

  // TAB: asks completionProvider_ (if set) for candidates matching
  // the partial word ending at the cursor, and splices a single
  // unambiguous match back into the command line. Multiple/zero
  // candidates are left for a future PathCompleter to resolve (this
  // deliberately does not reimplement make_list()'s cycle-through-
  // candidates UI) -- see the class comment.
  void onTab();
  void setCompletionProvider(
      std::function<std::vector<std::string>(const std::string &partialWord)> provider);

  // Writes a raw byte straight to the pty, bypassing the command-line
  // editor entirely -- for forwarding e.g. Ctrl-C to interrupt
  // whatever the shell is currently running in the foreground. The
  // caller decides which keys route here vs. to commandLine().
  void sendControlByte(unsigned char byte);

  // -- directory tracking --
  // Fires whenever a newly-detected prompt's directory differs from
  // the previous one. Ported from write_shell()'s prompt-string
  // directory extraction, exposed as a callback instead of a direct
  // chdir() of the whole process -- other code that cares (a file
  // browser mode, say) should react to this, not have Shell silently
  // mutate global process state.
  void setDirectoryChangedCallback(std::function<void(const std::string &directory)> callback);
  const std::string &currentDirectory() const;

  // -- PROGRAM handoff / history --
  // Returns true if it ran (and is now responsible for) the given
  // trailing-pipeline-stage program token; false to have Shell write
  // the line to the pty as plain text instead.
  void setProgramLauncher(
      std::function<bool(const std::string &programToken, const std::string &fullLine)> launcher);
  void setHistoryRecorder(std::function<void(const std::string &line)> recorder);
  // Whether a handed-off interactive PROGRAM is still running --
  // caller-driven (Shell has no way to know on its own once it hands
  // off), used to let other modes refuse actions while true. Ported
  // from is_inter().
  void setBusy(bool busy);
  bool busy() const;

  // -- resize --
  // Propagates a new terminal size to the pty (TIOCSWINSZ) and to the
  // owned ContentView's viewport/LineEditor's field width. Ported
  // from the vt_mode('W') handling in input_shell()'s init case.
  void onResize(int rows, int columns);

  // K-27 (^Oa): updates the encoding used by displayWidth() (input-
  // echo/prompt-redraw width calculations, see add_buff()'s own
  // history in CLAUDE.md) and commandLine_'s own LineEditor encoding.
  // Does NOT retroactively re-decode scrollback() lines already
  // received from the pty under the OLD encoding -- Shell keeps no
  // raw-byte history to re-run that decoding from, only the
  // already-decoded ContentView::Line text (a real, disclosed
  // limitation: switching charset mid-SHELL-session only affects
  // output from that point forward, matching the practical scope of
  // what legacy's own vt_charset() toggle affects too).
  void setEncoding(Charset::Encoding encoding);

 private:
  void handlePromptDetected(const std::string &tailBeforeMark);
  void appendBodyByte(unsigned char c);
  void flushPendingLine();  // see appendBodyByte()'s own comment on pendingLine_
  // Real-terminal ANSI/VT100 escape sequences (SGR color codes,
  // bracketed-paste-mode enable/disable, etc -- anything starting
  // with a raw ESC byte) from the child's OWN output, consumed and
  // discarded here BEFORE they ever reach appendBodyByte()/
  // PipeBuffer. Found necessary via actual hands-on use with a real
  // interactive shell (2026-08-01): PipeBuffer's job is charset/tab/
  // overstrike-bold processing for PLAIN FILE content, with zero
  // concept of "this byte sequence is invisible terminal control
  // data, not text" -- every real shell emits at least SOME of these
  // even with a plain, uncolored synthetic prompt (bracketed paste
  // mode alone is a zsh 5.x+ default), and feeding them through
  // uninterpreted corrupts both the visible text (raw escape bytes
  // shown as literal characters) and PipeBuffer's own width-wrapping
  // math (which counts every byte as a visible column). Returns true
  // if `c` was consumed as part of (starting, continuing, or
  // completing) an escape sequence -- the caller should NOT also pass
  // it to appendBodyByte() in that case. This is a strip, not a
  // render: color/attribute information itself is discarded, not
  // reproduced -- a deliberate, disclosed scope choice (building a
  // real ANSI-to-Display::Attribute translator is future work, not
  // needed just to stop the corruption).
  bool consumeEscapeByte(unsigned char c);
  void flushNewScrollbackLines();
  void writeToPty(const std::string &bytes);
  void updateCommandLineField();
  int displayWidth(const std::string &text) const;
  std::string buildSetupCommand() const;
  bool waitForHandshake();

  Display &display_;
  ShellOptions options_;

  int masterFd_ = -1;
  int childPid_ = -1;
  bool isBash_ = true;
  bool isZsh_ = false;  // see isZshProgram()'s own comment on why this needs its own branch, not just "not bash"

  PipeBuffer pipeBuffer_;
  std::string pendingLine_;  // appendBodyByte()'s own line-assembly buffer, see its comment
  bool sawBareCr_ = false;   // appendBodyByte()'s own CR-vs-CRLF lookahead flag
  ContentView scrollback_;
  LineEditor commandLine_;
  std::size_t linesConsumed_ = 0;  // how many of pipeBuffer_.lines() are already in scrollback_

  std::string markerPrefix_;  // promptMark + programName + ":" -- see buildSetupCommand()
  std::size_t markerMatchPos_ = 0;
  std::string markerMatchBuffer_;  // bytes tentatively matched against markerPrefix_ so far
  bool inPromptTail_ = false;
  std::string promptTailBuffer_;

  // consumeEscapeByte()'s own state -- which KIND of escape sequence
  // (if any) is currently being scanned past.
  enum class EscapeKind { None, JustEsc, Csi, Osc, OscEscPending };
  EscapeKind escapeState_ = EscapeKind::None;

  int viewportRows_ = 0, columns_ = 0;
  std::string promptPlain_;  // current prompt text, no embedded escape codes (for width calc)

  std::string currentDirectory_;
  bool busy_ = false;

  std::function<void(const std::string &)> directoryChangedCallback_;
  std::function<bool(const std::string &, const std::string &)> programLauncher_;
  std::function<void(const std::string &)> historyRecorder_;
  std::function<std::vector<std::string>(const std::string &)> completionProvider_;
};

}  // namespace fm
