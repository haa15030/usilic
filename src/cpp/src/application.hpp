#pragma once

#include <optional>
#include <string>

#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "cut_mode.hpp"
#include "directory_mode.hpp"
#include "display.hpp"
#include "file_mode.hpp"
#include "grep_mode.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "line_editor.hpp"
#include "man_mode.hpp"
#include "mode_stack.hpp"

namespace fm {

// Application-level command, already resolved from a raw keypress --
// what get_m()'s giant switch(m) case label meant, without any of the
// byte/escape-sequence recognition that produced it (KeyInput's job)
// or any of the ~50 case labels this port hasn't reached yet.
enum class Command {
  None,
  MoveUp,
  MoveDown,
  PageUp,
  PageDown,
  Enter,
  Back,
  StartFgrep,
  StartEgrep,
  StartCutFixed,
  StartCutRegex,
  StartMan
};

// Which *Mode is currently on screen. Ported from legacy's np->M (a
// T_* mode constant); scoped down to exactly the modes this slice
// knows about. Cut and CutContent are two distinct screens for one
// legacy mode (M_cut): Cut shows the list of segment labels,
// CutContent shows one selected segment's actual lines -- see the
// class comment on why CutMode itself doesn't merge them.
enum class ApplicationMode { Directory, File, Grep, Cut, CutContent, Man };

// The top-level controller: owns the ModeStack(s) and every *Mode
// instance, maps keypresses to Command, and dispatches each Command
// to whichever *Mode is current. Ported from menu.c's get_m() (the
// keystroke-to-command switch) + m_cr() (the "Enter was pressed, do
// the right thing" routing) + m_bl() (^G) + m_row()/m_page() (cursor
// movement) + m_grep()/m_cut() (^S/^J^S/^Jc/^J^C entering search/cut)
// + menu()/init_menu() (the outer run loop and startup) -- together
// the ~3300-line mode.c/menu.c "Mode" layer this whole project has
// been building toward one class at a time. This is an early slice of
// it (Directory + File navigation, plus FGREP/EGREP search and CUT),
// see "Deliberately excluded" below for what's intentionally not here
// yet.
//
// This is NOT a literal port. Legacy's get_m()/m_cr()/m_bl()/m_grep()/
// m_cut() are one intertwined state machine over a dozen shared
// mutable structs (HEAP/ARRAY/FLAG/NUM/LINE/RV/PN/CHILD, see
// CLAUDE.md and every already-ported *Mode class's own header for why
// this project keeps choosing not to carry that forward) -- here the
// equivalent state is just currentMode_ (an enum), a couple of small
// prompt/leader-key bookkeeping fields, and each owned ContentView's
// own already-encapsulated cursor state. In particular:
//   - Legacy's single shared PP buffer (the 12-slot hp->base[] pool
//     DirectoryMode's own header flagged as an unsolved gap) is
//     resolved here by giving Directory, File, Grep, Cut, and
//     CutContent each their own ContentView, all wrapping the same
//     Display. Switching modes is just "stop calling redraw() on one
//     view, start calling it on another" -- a view not currently
//     shown keeps its lines/cursor state exactly as it was, in
//     memory, with nothing to save or restore. This is why returning
//     from File/Grep/Cut/CutContent to whatever was active before
//     doesn't need a ModeStack push at all (see handleCommand()'s
//     Back case): the prior mode's own state never left.
//   - TWO separate ModeStack instances are owned here, not one:
//     modeStack_ (DirectoryMode's own directory-to-directory descent,
//     ported in its first slice) and cutModeStack_ (CutMode's own
//     nested-resplit bookkeeping). They are NOT shared, despite both
//     being "just a ModeStack" -- confirmed during this class's design
//     that sharing one instance between them would be a real bug:
//     CutMode::canGoBack()/goBack() only check "is the stack
//     non-empty" / "does the popped frame have a cutBuffer", so if a
//     directory-navigation frame happened to be sitting on top of a
//     SHARED stack when the user was mid-CUT, goBack() would silently
//     pop and discard that directory frame while returning false
//     (frame->cutBuffer null) -- corrupting DirectoryMode's own
//     history for a completely unrelated excursion. Giving each class
//     its own ModeStack instance, despite the duplication, avoids
//     this cross-contamination entirely at the wiring layer without
//     touching either already-shipped class.
//   - Legacy's `^J` is a genuine two-byte leader key (^Jw, ^Jc, ^Jl,
//     ^Jm, ^J^S, ^J^C, ...), not a synonym for Enter/^M -- a subtlety
//     this class's very first slice got wrong (an earlier revision
//     folded 0x0a into the Enter case for convenience; fixed once
//     EGREP's ^J^S binding made the conflict concrete). resolveCommand()
//     stays a pure single-byte function; the stateful leader-byte
//     accumulation across two feedByte() calls lives in feedByte()
//     itself, the one piece of this class that has to remember
//     anything between keystrokes besides which mode is active.
//   - Legacy's search-pattern text entry (read_m()'s klen/prompt
//     machinery once np->M == M_fgrep/M_egrep/M_cut/M_cut2) is
//     represented here by a small explicit prompt state (promptKind_)
//     plus an owned LineEditor -- feedByte() routes raw bytes to the
//     editor instead of resolveCommand() while a prompt is active,
//     submitting on ^M and cancelling on ^G, mirroring get_m()'s own
//     read_m()-vs-dispatch split without needing read_m()'s hand-
//     rolled klen/cname table (LineEditor already owns text editing;
//     Application only decides when input goes there instead of
//     through Command dispatch). GREP and CUT share one promptKind_/
//     LineEditor pair, distinguished by which PromptKind is pending
//     and (for CUT) whether currentMode_ was File (fresh split) or
//     CutContent (resplitting a segment) when the prompt started --
//     see startCutPrompt()/executeCut(). MAN's name prompt uses the
//     same mechanism but needs no such per-mode branching: unlike
//     GREP/CUT it doesn't read the current view's content at all
//     (m_man() clears the prompt to empty rather than pre-filling it
//     from the current selection, confirmed by tracing *lp->arg =
//     K_z in m_man() -- the name is always freshly typed), so it can
//     be started from -- and returns to -- whichever mode was active,
//     tracked the same way grepReturnMode_/cutReturnMode_ are.
//
// Deliberately excluded (future work, not this slice):
//   - Every mode besides Directory/File/Grep/Cut/Man: PROGRAM,
//     LOCATE, archive browsing, SHELL entry, DU, CP/MV/RM execution,
//     history/list browsers, help/error/log screens -- each already
//     has its own *Mode class built; wiring each one in is the same
//     shape of change (extend Command/ApplicationMode, extend
//     feedByte()'s leader table, extend handleCommand()) repeated per
//     mode, deferred to keep each slice reviewable.
//   - GREP's own further actions: opening/jumping to a matched line's
//     original context, nested search (searching within results),
//     highlighting the matched substring, and recording the pattern
//     into History's hfgrep/hegrep rings -- all explicitly out of
//     GrepMode's own scope too (see its class comment); Enter on a
//     grep result is a no-op in this slice.
//   - CUT's own ^Jb/^J^B "split without first opening the file view"
//     shortcut from a directory listing, and LOG mode's built-in
//     prompt-marker split: both explicitly out of CutMode's own scope
//     too (see its class comment) -- CUT can only be started from
//     File mode (a fresh split) or CutContent (a nested resplit) in
//     this slice. Recording cut patterns into History is likewise out
//     of scope, same boundary GrepMode already draws.
//   - Exiting the program (M_exit/T_exit): m_exit()'s superuser-safety
//     confirmation dialog and screen-stack unwind isn't ported yet;
//     there is intentionally no Command::Quit in this slice. A caller
//     driving Application in a real run loop currently has to stop
//     the loop by its own means (e.g. an external signal), same as
//     this class provides no run()/main loop of its own yet either --
//     see below.
//   - A real terminal run loop (legacy's menu()): reading bytes from
//     a live KeyInput, translating escape sequences, and looping
//     until exit needs the not-yet-ported M_exit flow to have
//     somewhere to break out to, so it's deliberately not built here
//     either. feedByte()/handleCommand() are the tested, terminal-
//     independent primitives such a loop would call.
//   - Selection-mark-driven CP/MV/RM/ARC dispatch (M_cp/M_mv/M_rm/
//     M_arc, m_arc()/m_dest()/m_yn()): DirectoryMode's marking and
//     CpMvRmMode both already exist and are usable directly by a
//     caller today; wiring them into Command/handleCommand() is
//     future-slice work, not this one.
//   - Selection marking's own keys (^Space etc) and file-jump (M_jump):
//     same reason, not part of the commands this slice maps.
class Application {
 public:
  Application(Display &display, History &history, const Configuration &configuration,
              Charset::Encoding encoding, int width);

  // Lists `directory` (DirectoryMode::enterDirectory()) and makes it
  // the active mode. Ported from init_menu()'s initial directory
  // load. False if the initial `ls` fails.
  bool start(const std::string &directory);

  // Resolves a single, non-leader-prefixed keypress to a Command.
  // Ported from the movement/Enter/^G/^S slice of get_m()'s
  // switch(m) -- see the table in the .cpp for exact byte/SpecialKey
  // mappings and their legacy provenance. `special` is whatever
  // KeyInput::readEscapeSequence() returned after an ESC byte, or
  // nullopt for an ordinary byte. Does NOT recognize ^J-leader
  // sequences (e.g. ^J^S for EGREP, ^Jc/^J^C for CUT) -- those need
  // state across two calls, which this pure function deliberately
  // can't hold; use feedByte() for real key-by-key input, which
  // handles both.
  static Command resolveCommand(int byte, std::optional<KeyInput::SpecialKey> special);

  // Executes command against whichever *Mode is current. Returns
  // false when the command was a no-op given the current state (e.g.
  // Back with nothing to go back to, Enter on an empty listing) --
  // mirrors get_m()'s handlers, which likewise silently no-op rather
  // than erroring on such cases.
  bool handleCommand(Command command);

  // The real entry point for driving this class byte-by-byte (what a
  // KeyInput-backed run loop would call): handles ^J-leader
  // accumulation and prompt text entry, falling through to
  // resolveCommand()+handleCommand() for everything else. Returns
  // false only when the resulting action (if any) was itself a
  // no-op; a byte consumed into leader/prompt state with the action
  // still pending returns true.
  bool feedByte(int byte, std::optional<KeyInput::SpecialKey> special);

  bool promptActive() const;
  const std::string &promptText() const;

  ApplicationMode currentMode() const;
  DirectoryMode &directoryMode();
  FileMode &fileMode();
  GrepMode &grepMode();
  CutMode &cutMode();
  ManMode &manMode();

 private:
  bool openSelectedEntry();
  bool openSelectedSegment();
  bool returnToDirectory();
  bool returnFromGrep();
  bool returnFromCutContent();
  bool returnFromCut();
  bool returnFromMan();
  ContentView &activeView();

  bool startGrepPrompt(GrepSyntax syntax);
  bool startCutPrompt(GrepSyntax syntax);
  bool startManPrompt();
  bool feedPromptByte(int byte);
  bool submitPrompt();
  void cancelPrompt();
  bool executeGrep(const std::string &pattern);
  bool executeCut(const std::string &pattern);
  bool executeMan(const std::string &name);

  ModeStack modeStack_;
  ModeStack cutModeStack_;
  ContentView directoryView_;
  ContentView fileView_;
  ContentView grepView_;
  ContentView cutView_;
  ContentView cutContentView_;
  ContentView manView_;
  LineEditor promptEditor_;
  DirectoryMode directoryMode_;
  FileMode fileMode_;
  GrepMode grepMode_;
  CutMode cutMode_;
  ManMode manMode_;

  ApplicationMode currentMode_ = ApplicationMode::Directory;
  ApplicationMode grepReturnMode_ = ApplicationMode::Directory;
  ApplicationMode cutReturnMode_ = ApplicationMode::Directory;
  ApplicationMode manReturnMode_ = ApplicationMode::Directory;
  GrepSyntax grepSyntax_ = GrepSyntax::Fixed;
  GrepSyntax cutSyntax_ = GrepSyntax::Fixed;

  bool leaderJPending_ = false;  // saw ^J, waiting for the byte that completes a two-byte command

  enum class PromptKind { None, GrepPattern, CutPattern, ManName };
  PromptKind promptKind_ = PromptKind::None;
};

}  // namespace fm
