#pragma once

#include <optional>
#include <string>
#include <vector>

#include "archive_mode.hpp"
#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "cp_mv_rm_mode.hpp"
#include "cut_mode.hpp"
#include "directory_mode.hpp"
#include "display.hpp"
#include "file_mode.hpp"
#include "grep_mode.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "line_editor.hpp"
#include "locate_mode.hpp"
#include "man_mode.hpp"
#include "mode_stack.hpp"
#include "program_mode.hpp"
#include "shell.hpp"

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
  StartMan,
  StartProgram,
  StartLocate,
  StartShell,
  ToggleMark,
  StartCopy,
  StartMove,
  StartRemove
};

// Which *Mode is currently on screen. Ported from legacy's np->M (a
// T_* mode constant); scoped down to exactly the modes this slice
// knows about. Cut and CutContent are two distinct screens for one
// legacy mode (M_cut): Cut shows the list of segment labels,
// CutContent shows one selected segment's actual lines -- see the
// class comment on why CutMode itself doesn't merge them.
enum class ApplicationMode { Directory, File, Grep, Cut, CutContent, Man, Locate, Archive, ArchiveContent, Shell };

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
//   - PROGRAM (M_prog/M_prog2, ^J^P/^Jp) doesn't switch
//     ApplicationMode at ALL, unlike every other prompt-driven action
//     above: tracing mode.c's M_prog case (the m_cr()-equivalent that
//     runs once the typed command line is submitted) shows it just
//     runs the command in the foreground and returns to whatever
//     screen was already showing -- there's no new persistent screen
//     the way GREP/CUT/MAN each create one. This also means
//     ProgramMode needs a live KeyInput (to actually leave/re-enter
//     raw termio around the child process, unlike every other *Mode
//     this class owns) -- so, uniquely, Application takes a
//     KeyInput& constructor parameter instead of only ever receiving
//     already-resolved bytes; see ProgramMode's own class comment for
//     why that reference has to be the SAME KeyInput instance
//     actually controlling the real tty, not a throwaway one
//     Application could construct for itself. Legacy's own program-
//     name validation (find_prog(): the typed line's first word must
//     be a strncmp-prefix match of some PROGRAM directive's own first
//     word) is replicated in executeProgram() against
//     Configuration::programSeedCommands().
//   - LOCATE (M_locate/M_locate2, ^J^L/^Jl) is the simplest of the
//     prompt-driven actions: like MAN it needs nothing from the
//     current view and always starts an empty prompt, but like GREP
//     it DOES refuse re-entry from within its own results screen
//     (m_locate()'s own is_name(ap->name, M_locate) guard) -- so it
//     tracks a return mode the same way GREP/CUT/MAN do, but is the
//     only one of the four with both properties at once. See
//     "Deliberately excluded" for the one piece of m_locate() this
//     slice does NOT carry over.
//   - Archive browsing (T_tar/T_lha/T_unzip/T_rpm/T_deb) is NOT
//     prompt-driven at all, unlike every action above -- it's auto-
//     detected the same way DirectoryMode::openSelected() already
//     auto-detects "is the selection a directory": openSelectedEntry()
//     tries ArchiveMode::loadArchive() on the selected path FIRST,
//     falling back to FileMode::loadFile() only if that fails
//     (loadArchive() itself reports "not a configured archive suffix"
//     and "listing command failed" identically, both simply false --
//     a real fidelity gap flagged rather than glossed over: legacy
//     would presumably surface a specific error for a real archive
//     suffix whose listing command failed, whereas this falls through
//     to plain-text viewing instead). Archive results in a second
//     screen the same shape as Cut's: Archive (the entry listing,
//     ArchiveMode's own ContentView) and ArchiveContent (one
//     extracted entry's bytes, shown as plain text lines -- no
//     PipeBuffer charset/overstrike processing the way FileMode gets,
//     since ArchiveMode::extractSelected() only ever hands back raw
//     bytes). Like CutContent, opening ArchiveContent needs no
//     ModeStack push (ArchiveMode owns no stack of its own either);
//     Back from ArchiveContent returns to Archive, Back from Archive
//     returns to Directory, both plain toggles.
//   - SHELL (M_shell, ^[s -- ESC then 's') is structurally the odd one
//     out among everything above: unlike GREP/CUT/MAN/PROGRAM/LOCATE,
//     which are all "type into promptEditor_, submit, get a Command-
//     driven result screen", Shell already owns its OWN LineEditor
//     (commandLine()) and ContentView (scrollback()) and drives them
//     directly against a real pty -- Application's job here is purely
//     routing bytes to Shell's own API while ApplicationMode::Shell is
//     active (feedShellByte()), not reimplementing any editing/
//     display logic of its own (see Shell's own class comment for
//     why: this project already dissolved the legacy three-process/
//     eight-pipe IPC design when Shell itself was ported). Once
//     started, the pty-backed child process is intentionally left
//     running in the background across Back/^G -- switching away from
//     Shell only changes which ContentView Application is redrawing
//     (shell_.scrollback() is returned directly from activeView()'s
//     Shell case, not copied into an Application-owned ContentView
//     the way every other mode's screen is), matching how a real
//     terminal multiplexer pane keeps running while unfocused. There
//     is no way to actually terminate the shell in this slice (no
//     M_exit yet, see below) beyond Shell's own destructor when
//     Application itself is destroyed.
//     A known, unresolved integration gap: ^[s isn't one of Display's
//     6 recognized escape sequences, so KeyInput::readEscapeSequence()
//     -- which exists specifically to consume and match ESC-prefixed
//     terminal capability sequences -- has no defined way to hand the
//     trailing 's' byte back to a caller when nothing matched. This
//     class's own ESC-leader handling (mirroring the ^J-leader
//     mechanism) works correctly when fed the raw bytes directly
//     (0x1b then 's'), which is how it's tested here, but wiring it to
//     a REAL terminal through the existing KeyInput boundary needs
//     further design (KeyInput exposing an escape-sequence-didn't-
//     match fallback, or the not-yet-built real run loop bypassing
//     readEscapeSequence() for bytes it already knows are app-level
//     leaders) -- flagged rather than silently assumed solved, same
//     as every other gap this class's own comment documents.
//   - CP/MV/RM (^Xc/^Xm/^Xr -- a THIRD leader key, ^X this time,
//     alongside ^J and ESC) reuses DirectoryMode's marking
//     (toggleMark()/targetEntries(), see its own header) and the
//     already-shipped CpMvRmMode as-is, so this slice's only real new
//     code is the prompt plumbing: CP/MV ask for a destination path
//     (PromptKind::CopyDestination/MoveDestination), RM asks for a
//     literal "y" confirmation (PromptKind::RemoveConfirm) -- a
//     deliberately simplified stand-in for m_dest()/m_yn()'s much
//     more elaborate per-entry validation (tilde/double-slash
//     cleanup, MustBeDirectory/MissingIsWritable destination checks
//     folded per-entry, ARC members mixed into the same dispatch).
//     ^Space (M_mark) toggles the mark at the cursor -- KeyInput's own
//     documented convention reports a literal NUL/Ctrl-Space as byte
//     0x80 (see its class comment), which is what resolveCommand()
//     matches. On success, the directory listing is reloaded
//     (DirectoryMode::enterDirectory() on the current directory) so
//     the screen reflects what cp/mv/rm actually did; marks are
//     cleared as a side effect of that reload, matching
//     DirectoryMode's own documented behavior, so there's no separate
//     clearMarks() call needed here.
//
// Deliberately excluded (future work, not this slice):
//   - Every mode besides Directory/File/Grep/Cut/Man/Locate/Archive/
//     Shell: DU, history/list browsers, help/error/log screens --
//     each already has its own *Mode class built; wiring each one in
//     is the same shape of change (extend Command/ApplicationMode,
//     extend feedByte()'s leader table, extend handleCommand())
//     repeated per mode, deferred to keep each slice reviewable.
//   - The ^X^Kc/^X^Km/^X^Kr synonyms for CP/MV/RM (a second, longer
//     leader sequence get_m() accepts for the same three actions,
//     setting an extra `fp->comm` flag whose effect wasn't traced) and
//     ^X^Ka/^Xa (M_arc/M_arc2, the archive-CREATION action -- see
//     below, a different "ARC" from archive-browsing): only ^Xc/^Xm/
//     ^Xr are wired in this slice.
//   - CpMvRmMode's own already-documented exclusions (interactive `-i`
//     confirmation at the OS level, entries living inside an archive
//     or a `du`-measured subdirectory): unchanged by this slice,
//     still out of scope for the same reasons its own header gives.
//   - mode.c's M_prog case falls back to launching SHELL mode with
//     the typed line pre-filled when the first word ISN'T a
//     recognized PROGRAM-list entry (`m = -M_shell`) -- executeProgram()
//     still just reports failure instead of that handoff, even though
//     SHELL itself is wired in now; connecting the two is its own
//     small future change (SHELL's own command line would need to be
//     pre-filled with the typed text), not bundled into either slice.
//   - Shell's own further hookups this slice leaves unset:
//     setHistoryRecorder()/setDirectoryChangedCallback()/
//     setProgramLauncher()/setCompletionProvider() -- all optional,
//     all deferred (History integration, cwd-follows-shell behavior,
//     PROGRAM handoff from within SHELL, and TAB completion
//     candidates are each their own follow-up, not required for a
//     working SHELL mode). Scrollback/command-line paging via arrow
//     Up/Down or PageUp/PageDown while in Shell mode is likewise not
//     wired (feedShellByte() only handles Left/Right cursor movement
//     among SpecialKeys).
//   - do_prog()'s post-run staleness check (re-stat the file being
//     viewed; if it changed while the program ran, force a reload):
//     not replicated here either -- a real gap, flagged rather than
//     silently glossed over, deferred until this slice's minimal
//     "just run it" behavior needs to grow that far.
//   - LOCATE's own ^Jl shortcut (m_locate()'s M_locate2 case): when a
//     previous locate result buffer already exists, ^Jl re-displays
//     it directly (skipping the prompt entirely) instead of asking
//     for a fresh query -- ^J^L always prompts fresh either way. This
//     slice treats both bindings identically (always a fresh prompt),
//     deferring the "remember and redisplay the last result set
//     without re-running the search" shortcut. Deciding what to do
//     with a selected result (handing off to DirectoryMode or
//     FileMode) is likewise out of scope, matching LocateMode's own
//     class comment; Enter on a locate result is a no-op in this
//     slice, same as GREP.
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
//   - ARC-creation (^X^Ka/^Xa, M_arc/M_arc2, m_arc()): a DIFFERENT,
//     unrelated "ARC" from the archive-browsing one above -- menu.c's
//     m_arc() here is the archive-CREATION action grouped alongside
//     CP/MV/RM, not T_tar/etc's archive-browsing/viewing. Unlike
//     CP/MV/RM, this has no ported class yet at all, so it isn't
//     wired regardless of leader-key scope.
//   - File-jump (M_jump, ^X^X -- swap between the cursor position and
//     the last ^Space-marked position): not part of the commands this
//     slice maps.
class Application {
 public:
  // `keyInput` must be the same instance actually controlling the
  // real tty's raw/cooked mode (see class comment) -- Application
  // only ever forwards it to ProgramMode, never reads from it itself.
  // `shellOptions` seeds Shell's argv/promptMark/programName/
  // scrollbackBudget; rows/columns/encoding are overwritten from
  // `display`/`encoding` regardless of what's passed in, so the
  // Shell session always matches this Application's own geometry and
  // charset instead of needing them specified twice.
  Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
              Charset::Encoding encoding, int width, ShellOptions shellOptions = ShellOptions{});

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

  // Routes a byte while ApplicationMode::Shell is current: ^G leaves
  // (see class comment on why the pty session itself isn't torn
  // down), ^M submits the command line, Tab asks Shell's completion
  // hook (unset in this slice, so effectively a no-op), ^C forwards
  // straight to the pty, DEL/^H backspaces, Left/Right move the
  // command-line cursor, everything else is inserted as ordinary
  // text. Exposed publicly (rather than folded silently into
  // feedByte()) so a caller -- or a test -- can drive Shell input
  // without needing to first get Application into Shell mode via a
  // real ^[s sequence.
  bool feedShellByte(int byte, std::optional<KeyInput::SpecialKey> special);

  bool promptActive() const;
  const std::string &promptText() const;

  ApplicationMode currentMode() const;
  DirectoryMode &directoryMode();
  FileMode &fileMode();
  GrepMode &grepMode();
  CutMode &cutMode();
  ManMode &manMode();
  ProgramMode &programMode();
  LocateMode &locateMode();
  ArchiveMode &archiveMode();
  Shell &shell();
  CpMvRmMode &cpMvRmMode();

 private:
  bool openSelectedEntry();
  bool openSelectedSegment();
  bool openSelectedArchiveEntry();
  bool toggleMark();
  bool returnToDirectory();
  bool returnFromGrep();
  bool returnFromCutContent();
  bool returnFromCut();
  bool returnFromMan();
  bool returnFromLocate();
  bool returnFromArchiveContent();
  bool returnFromArchive();
  bool returnFromShell();
  ContentView &activeView();
  std::vector<std::string> collectTargetPaths() const;

  bool startGrepPrompt(GrepSyntax syntax);
  bool startCutPrompt(GrepSyntax syntax);
  bool startManPrompt();
  bool startProgramPrompt();
  bool startLocatePrompt();
  bool startShell();
  bool startCopyPrompt();
  bool startMovePrompt();
  bool startRemovePrompt();
  bool feedPromptByte(int byte);
  bool submitPrompt();
  void cancelPrompt();
  bool executeGrep(const std::string &pattern);
  bool executeCut(const std::string &pattern);
  bool executeMan(const std::string &name);
  bool executeProgram(const std::string &command);
  bool executeLocate(const std::string &query);
  bool executeCopy(const std::string &destination);
  bool executeMove(const std::string &destination);
  bool executeRemove(const std::string &confirmation);

  const Configuration &configuration_;  // executeProgram()'s own PROGRAM-list lookup, see find_prog() note above
  ModeStack modeStack_;
  ModeStack cutModeStack_;
  ContentView directoryView_;
  ContentView fileView_;
  ContentView grepView_;
  ContentView cutView_;
  ContentView cutContentView_;
  ContentView manView_;
  ContentView locateView_;
  ContentView archiveView_;
  ContentView archiveContentView_;
  LineEditor promptEditor_;
  DirectoryMode directoryMode_;
  FileMode fileMode_;
  GrepMode grepMode_;
  CutMode cutMode_;
  ManMode manMode_;
  ProgramMode programMode_;
  LocateMode locateMode_;
  ArchiveMode archiveMode_;
  Shell shell_;
  CpMvRmMode cpMvRmMode_;

  ApplicationMode currentMode_ = ApplicationMode::Directory;
  ApplicationMode grepReturnMode_ = ApplicationMode::Directory;
  ApplicationMode cutReturnMode_ = ApplicationMode::Directory;
  ApplicationMode manReturnMode_ = ApplicationMode::Directory;
  ApplicationMode locateReturnMode_ = ApplicationMode::Directory;
  ApplicationMode shellReturnMode_ = ApplicationMode::Directory;
  GrepSyntax grepSyntax_ = GrepSyntax::Fixed;
  GrepSyntax cutSyntax_ = GrepSyntax::Fixed;
  bool shellStarted_ = false;

  bool leaderJPending_ = false;  // saw ^J, waiting for the byte that completes a two-byte command
  bool escPending_ = false;      // saw ESC (0x1b), waiting for the byte that completes ^[s -- see class comment
  bool leaderXPending_ = false;  // saw ^X, waiting for the byte that completes ^Xc/^Xm/^Xr

  enum class PromptKind {
    None,
    GrepPattern,
    CutPattern,
    ManName,
    ProgramCommand,
    LocateQuery,
    CopyDestination,
    MoveDestination,
    RemoveConfirm
  };
  PromptKind promptKind_ = PromptKind::None;
};

}  // namespace fm
