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
#include "du_mode.hpp"
#include "display.hpp"
#include "edit_mode.hpp"
#include "exec_mode.hpp"
#include "file_mode.hpp"
#include "grep_mode.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "line_editor.hpp"
#include "list_browser_mode.hpp"
#include "locate_mode.hpp"
#include "man_mode.hpp"
#include "mode_stack.hpp"
#include "path_resolver.hpp"
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
  // "Move window one line up/down" (usilicrc's own K-8/K-9 description,
  // bound to 'j'/'k' -- see ContentView::scrollLineDown()'s own
  // comment for exactly how this differs from MoveUp/MoveDown, which
  // usilicrc separately describes as "move CURSOR one line" and binds
  // to ^N/^P instead).
  ScrollLineDown,
  ScrollLineUp,
  PageUp,
  PageDown,
  Enter,
  Back,
  // ';'/':' -- confirmed directly by the user (2026-08-03, after an
  // initial wrong guess that this was some kind of "related file"
  // jump): these are literally shorthand for Back+MoveDown+Enter and
  // Back+MoveUp+Enter respectively, not a distinct feature of their
  // own. Kept as their own Command values (rather than expanding
  // dispatchByte() inline) so handleCommand()'s existing single place
  // for "what does this Command actually do" stays the only place
  // that needs to know the 3-step composition -- matches this port's
  // existing pattern for other composite actions (e.g.
  // StartFileInput/StartDirectoryInput sharing startPathInputPrompt()).
  JumpNext,      // ';' : Back, MoveDown, Enter
  JumpPrevious,  // ':' : Back, MoveUp, Enter
  StartFgrep,
  StartEgrep,
  StartCutFixed,
  StartCutRegex,
  // K-35/K-36 (^Jb / ^J^B, M_fcut2/M_fcut -- CUT's "regular file"
  // variant): same idea as StartCutFixed/StartCutRegex, but reachable
  // from Directory mode on the SELECTED entry directly, without first
  // opening it in File mode (CutMode's own class comment already
  // flagged this exact shortcut as unwired future work). See
  // startCutFilePrompt()'s own comment.
  StartCutFileFixed,
  StartCutFileRegex,
  StartMan,
  StartProgram,
  StartLocate,
  LocateUpdateDb,
  StartShell,
  ToggleMark,
  StartCopy,
  StartMove,
  StartRemove,
  StartExit,
  StartDu,
  StartDirectoryHistory,
  StartCommandHistory,
  // K-49 (plain ^R, M_hist, " HISTORY"): NOT a way to jump to a
  // DIFFERENT category from outside a list browser (an earlier
  // version of this port mistook it for exactly that, aliasing it to
  // StartDirectoryHistory -- wrong, per a direct 2026-08-04 user
  // clarification and a re-read of menu.c's m_list(), which is purely
  // context-sensitive: `is_name(ap->name, M_lfile) -> T_hfile`, etc).
  // Toggles the CURRENTLY-OPEN ListBrowser (whichever RingCategory --
  // FILE/DIR/MAN/LOCATE/FGREP/EGREP/COMMAND) between its two display
  // modes: normal (sorted alphabetically, de-duplicated -- de-dup
  // already came for free from History::pushRecent()'s own
  // first-occurrence-wins policy, but sorting did not) and
  // "HISTORY" (raw chronological access order, oldest at the top,
  // NEWEST AT THE BOTTOM ROW -- confirmed directly by the user: "FILE
  // （DIR）は今までアクセスしたファイル（ディレクトリ）をソートした
  // のち重複を排除した一覧、HISTORYはアクセスした順（最下行が最
  // 新）"). A no-op (returns false) outside ListBrowser mode, matching
  // m_list()'s own `echo_mess(S_errf)` when ^R doesn't apply to the
  // current context -- see Application::toggleListBrowserHistory().
  ToggleHistoryView,
  StartEdit,
  StartHelp,
  StartMacroRecord,
  StopMacroRecord,
  PlayMacro,
  ToggleGuiMode,
  // K-3 (^L, M_clear): force a full redraw of the active view.
  Redraw,
  // K-12/K-13 (ESC< / ESC>, M_head/M_tail): jump to the first/last
  // line -- ContentView::goToTop()/goToBottom() already existed but
  // were never wired to a key (a real report: usilicrc's own KEY-mode
  // help screen, compared line-by-line against this port's key table,
  // caught several such already-implemented-but-unwired/never-ported
  // bindings at once -- see CLAUDE.md).
  GoToTop,
  GoToBottom,
  // K-70 ('q', M_quit): exits immediately (no y/n confirmation,
  // unlike StartExit/^X^C) -- usilicrc documents this as pager-only
  // ("Exit tcb (pager)"), so handleCommand() only honors it outside
  // Directory mode, matching that scoping (a bare 'q' in Directory
  // mode remains a no-op, same as before this was added).
  QuitPager,
  // K-25 (^Oh, M_hs): toggles Display::smoothScrollEnabled() --
  // ported from usilicrc's own SCROLL section ("When the flag is set,
  // tcb window will scroll within the range above bottom line" --
  // exactly Display's hardware scroll-region path, which already
  // excludes the mode-line row, see moveUp()/moveDown()'s own
  // comments). Display::setSmoothScrollEnabled()/History's own
  // smoothScrollEnabled_ field already existed (parsed from/
  // serialized to the history file's TCB trailer line) but neither
  // was ever wired to a key OR applied to the Display at startup --
  // both gaps fixed together here, since a toggle key is meaningless
  // if the persisted value it toggles never actually reaches the
  // Display in the first place.
  ToggleScrollMode,
  // K-45/K-47 (^Xf / ^Xd, M_lfile2/M_ldir2 -- "Input file/directory
  // name"): prompts for a path and opens it, landing in File or
  // Directory mode depending on what PathResolver says it actually
  // is. Two distinct keys/commands (matching usilicrc's own two K-
  // numbers, and two likely-different prompt labels down the line)
  // but one shared execution path, since resolving "what kind of
  // thing is this path" doesn't depend on which key started the
  // prompt -- see startPathInputPrompt()'s own comment. The "Select"
  // counterparts (K-46/K-48, ^X^F/^X^D, M_lfile/M_ldir) are a LIST-
  // based completion picker, not a plain text prompt, and are NOT
  // covered here -- future work, see CLAUDE.md.
  StartFileInput,
  StartDirectoryInput,
  // K-26 (^Ol, M_ls): toggles DirectoryMode::filenameOnlyMode() --
  // ported from usilicrc's own LS section ("When the flag is set,
  // file-selector will display only filename"). Gated to Directory
  // mode only, matching K-26's own "file-selector" scoping (no
  // meaning in File/Man/etc content views). Persisted the same way as
  // ToggleScrollMode/K-25.
  ToggleLsMode,
  // K-72/K-73 (^X^Y / ^X^N, "Mark all"/"Clear all" for CP/MV/RM/ARC):
  // bulk variants of ToggleMark -- mark every entry in the current
  // listing, or drop every mark, in one keystroke instead of pressing
  // ^Space on each one. Same Directory-mode-only scoping as ToggleMark
  // itself.
  MarkAll,
  ClearAllMarks,
  // K-51/K-53/K-55 (^X^Kc/^X^Km/^X^Kr, "with SHELL window"): same CP/
  // MV/RM operation as StartCopy/StartMove/StartRemove, but the real
  // command's own output is shown live on the real terminal (via
  // ChildProcess::runInteractive(), same shape as ExecMode/
  // ProgramMode) instead of being silently captured -- see
  // CpMvRmMode::copyToInteractive()'s own comment. Reuses the SAME
  // prompt kinds/text-entry flow as the non-interactive commands;
  // only the final execution step differs (executeCopy()/etc. check
  // cpMvRmInteractive_).
  StartCopyInteractive,
  StartMoveInteractive,
  StartRemoveInteractive,
  // K-17 (^X^W, M_w4): writes the active view's full content to
  // wFilePath_ (see the constructor's own comment) -- ported from
  // menu.c's own `send_pp(hp->bp->pp, "a")` (append mode).
  SaveBufferToFile,
  // K-61/K-62 (^J. / ^J,, M_chdir2/M_chdir): switches to Shell mode
  // (starting it if not already running) and immediately submits a
  // generated command -- ported from menu.c's own m_w() (M_chdir2/
  // M_chdir cases): "cd '<directory>'" (the CURRENT DirectoryMode
  // directory, matching legacy's own lp->dir) for K-61, the literal
  // "ls -l" usilicrc's own K-62 description names for K-62. Both
  // auto-submit (legacy's own generated strings end in \r) --
  // K-63/K-64 below deliberately don't.
  ShellChangeDirectory,
  ShellRunLsL,
  // K-63/K-64 (^Jw / ^J^W, M_w/M_w2): switches to Shell mode and
  // PRE-FILLS (does NOT submit) the command line -- ported from
  // menu.c's own m_w() (M_w/M_w2 cases). K-63 inserts "echo <name>"
  // (the currently selected/open item's name -- legacy writes it to
  // a temp file first only to sidestep 1990s shell-quoting concerns
  // that don't apply to this port's in-memory LineEditor, see
  // shellInsertName()'s own comment). K-64 inserts "cat <wFilePath_>"
  // after actually writing the active view's full content there
  // (same target file as K-17/SaveBufferToFile) -- unlike K-63,
  // there's no way around a real file here: `cat` needs one to read.
  ShellInsertName,
  ShellInsertBufferContent,
  // K-19 (^O^K, M_m): jumps to the parent of the current directory.
  // See DirectoryMode::goToParent()'s own comment.
  GoToParentDirectory,
  // K-60 (^Xs, M_hup): kills the current SHELL child (if any) and
  // starts a fresh one. See Application::shellRestart()'s own
  // comment.
  RestartShell,
  // K-58/K-57 (^Xa / ^X^Ka, ARC "Expand archive" without/with SHELL
  // window). See Application::startArchivePrompt()'s own comment.
  StartArchiveExtract,
  StartArchiveExtractInteractive,
  // K-67 (^X^Ke, M_error, "ERROR"): opens errorLogPath_ in File mode.
  // K-68 (^X^Kl, M_log, "LOG: SHELL output"): snapshots Shell::
  // scrollback() to logFilePath_, then opens THAT in File mode. See
  // Application::executeShowErrorLog()/executeShowShellLog()'s own
  // comments.
  ShowErrorLog,
  ShowShellLog,
  // K-0 (^T, M_tcb, "TCB"): shows a status/about screen. See
  // Application::executeShowTcbBanner()'s own comment.
  ShowTcbBanner,
  // K-27 (^Oa, M_ascii, "Switch ascii mode"): cycles the active
  // charset Ascii -> Euc -> Utf8 -> Ascii. See
  // Application::cycleCharset()'s own comment.
  CycleCharset,
  // K-46 (^X^F, "FILE: Select file name"): opens a ListBrowser over
  // RingCategory::RecentFile. K-48 (^X^D, "DIR: Select directory
  // name") reuses the ALREADY-EXISTING StartDirectoryHistory, so
  // needs no command of its own. (Correction, 2026-08-04: an earlier
  // version of this comment additionally claimed StartDirectoryHistory
  // was "^R's own command" -- wrong, see ToggleHistoryView's own
  // comment for why plain ^R is unrelated to which RingCategory is
  // being browsed.)
  StartFileHistory,
  // K-2 (^H, M_key, "KEY"): shows a browsable table of this port's
  // own key bindings (mirroring dispatchByte()'s/resolveCommand()'s
  // actual switch statements -- NOT parsed from usilicrc's own KEY
  // section, see startKey()'s own comment on why). Enter on a
  // highlighted entry returns to whichever mode KEY was invoked from
  // and then executes that entry's own key sequence, matching
  // HELP.jp 2.2's documented behavior. See Application::startKey()'s
  // own comment.
  StartKey
};

// Which *Mode is currently on screen. Ported from legacy's np->M (a
// T_* mode constant); scoped down to exactly the modes this slice
// knows about. Cut and CutContent are two distinct screens for one
// legacy mode (M_cut): Cut shows the list of segment labels,
// CutContent shows one selected segment's actual lines -- see the
// class comment on why CutMode itself doesn't merge them.
enum class ApplicationMode {
  Directory,
  File,
  Grep,
  Cut,
  CutContent,
  Man,
  Locate,
  Archive,
  ArchiveContent,
  Shell,
  Du,
  ListBrowser,
  Key
};

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
//   - Archive browsing (T_tar/T_lha/T_unzip/T_rpm/T_deb) and EXEC
//     (T_exec, a suffix-mapped external viewer -- e.g. a configured
//     PDF reader) are NEITHER prompt-driven, unlike every action
//     above -- both are auto-detected the same way
//     DirectoryMode::openSelected() already auto-detects "is the
//     selection a directory": openSelectedEntry() checks
//     Configuration::isExec() on the selected path FIRST (tracing
//     child.c's is_child() shows a suffix's own `f.exec` flag is
//     checked before the archive-suffix table, giving EXEC priority
//     -- though in practice Configuration's suffixEntries_ already
//     makes this a non-issue: it's ONE unified longest-suffix-wins
//     table where each entry is either archive-flagged or exec-
//     flagged, never both, so ArchiveMode::loadArchive() naturally
//     can't succeed on an exec-mapped suffix regardless of check
//     order -- checking isExec() first just avoids a wasted
//     loadArchive() attempt), running ExecMode::run() and returning
//     to Directory mode unchanged if so (do_exec()'s own behavior:
//     hand the whole file off to an external program, no new screen,
//     matching PROGRAM/EDIT's shape more than Archive's). Only if NOT
//     exec-mapped does openSelectedEntry() try
//     ArchiveMode::loadArchive() next, falling back to
//     FileMode::loadFile() only if that too fails (loadArchive()
//     itself reports "not a configured archive suffix" and "listing
//     command failed" identically, both simply false -- a real
//     fidelity gap flagged rather than glossed over: legacy would
//     presumably surface a specific error for a real archive suffix
//     whose listing command failed, whereas this falls through to
//     plain-text viewing instead). Archive results in a second screen
//     the same shape as Cut's: Archive (the entry listing, ArchiveMode's
//     own ContentView) and ArchiveContent (one extracted entry's
//     bytes, shown as plain text lines -- no PipeBuffer charset/
//     overstrike processing the way FileMode gets, since
//     ArchiveMode::extractSelected() only ever hands back raw bytes).
//     Like CutContent, opening ArchiveContent needs no ModeStack push
//     (ArchiveMode owns no stack of its own either); Back from
//     ArchiveContent returns to Archive, Back from Archive returns to
//     Directory, both plain toggles. EXEC is wired ONLY into
//     openSelectedEntry() (Directory mode's Enter key) in this slice,
//     the same boundary Archive detection already drew -- DU/LOCATE
//     result entries open directly via FileMode::loadFile() with no
//     suffix dispatch of their own kind, unchanged by this addition
//     (see their own bullets above).
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
//     ^[s isn't one of Display's 6 recognized escape sequences, so
//     KeyInput::readEscapeSequence() -- which exists specifically to
//     consume and match ESC-prefixed terminal capability sequences --
//     originally had no defined way to hand the trailing 's' byte
//     back to a caller when nothing matched (a real, previously-
//     documented gap). This class's own ESC-leader handling (mirroring
//     the ^J-leader mechanism) always worked correctly when fed the
//     raw bytes directly (0x1b then 's'), which is how it's tested
//     here; the gap was on KeyInput's side, closed by adding
//     KeyInput::lastEscapeBytes() (see its own class comment) -- the
//     real run loop (app/main.cpp) now feeds ESC + whatever
//     lastEscapeBytes() captured through feedByte() exactly the way a
//     live keypress would arrive, so ^[s works over a real terminal
//     too.
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
//   - Exiting (M_exit, ^X^C) reuses the SAME prompt mechanism as
//     RM's confirmation: tracing m_cr()'s M_exit case shows the
//     confirm-and-quit logic is genuinely this simple --
//     `if (*rvp->str == 'y') return -T_exit;` -- everything else in
//     m_exit() itself (the function, confusingly, not the same thing
//     as the M_exit case above) is stack-unwind bookkeeping for
//     legacy's shared-buffer-pool architecture that this port's
//     per-mode ContentViews never needed in the first place (same
//     reasoning as every other "NOT a literal port" note above).
//     Unlike RM, StartExit is reachable from any mode (no
//     targetEntries()-empty guard) and doesn't touch the filesystem
//     or reload anything on success -- it just sets a flag
//     (wantsExit()) for a future real run loop to notice and stop.
//     Application still has no run loop of its own (see below); this
//     slice only gives one somewhere to break out TO.
//   - DU (M_du/M_du2, ^Jd/^J^D) is prompt-driven like GREP/CUT/MAN/
//     LOCATE, but PRE-FILLS the prompt with the current directory
//     (tracing m_cr()'s `case T_du: strcpy(rvp->str, ap->du);` --
//     unlike every prompt above, which starts empty) rather than
//     requiring it typed fresh; the user can just press ^M to accept
//     the default or edit first. Directory-only entry (m_du() itself
//     doesn't gate on the current mode, but this slice narrows scope
//     the same way CP/MV/RM did). Unlike every other result screen,
//     Enter on a DU entry DOES drill in -- to DirectoryMode if the
//     entry is a directory, FileMode if it's a file -- since
//     DuMode's own class comment explicitly leaves that hand-off to
//     Mode/Application ("the decision of whether opening a DU entry
//     lands in DirectoryMode or FileMode"). Both ^Jd/^J^D are treated
//     identically here (always a fresh `du`), deferring M_du2's own
//     "reuse the existing DU buffer without re-running `du`" shortcut
//     the same way LOCATE's analogous ^Jl shortcut was deferred.
//   - The history/list browser (m_list(), ^R=M_hist/^J^R=M_cmd/
//     ^I=M_list) turned out to be the most heavily simplified action
//     in this whole port: tracing m_list() shows it picks from NINE
//     different History/Configuration-backed categories depending on
//     which prompt context was active when the key was pressed
//     (T_hfile/T_hdir/T_hman/T_hlocate/T_hfgrep/T_hegrep/T_list/
//     T_cmd/T_hist) -- context-tracking this whole port's Application-
//     level design otherwise avoids needing. This slice wires only
//     the two least ambiguous, most directly useful cases, both
//     Directory-mode-only: ^R browses History::RingCategory::
//     RecentDirectory (newest first), ^J^R browses ::ShellCommand.
//     ListBrowserMode (already a thin, generic "show strings, track a
//     selection" class, see its own header) needed no changes at all
//     -- Application just needed a `History &history_` reference of
//     its own (new; every other *Mode needing History gets it via its
//     own constructor instead) to call recentEntries() directly, since
//     populating this list isn't any single *Mode's job. Enter on a
//     RecentDirectory entry drills into DirectoryMode (the same shape
//     as DU's directory entries); Enter on a ShellCommand entry is a
//     no-op in this slice (pre-filling Shell's command line with it,
//     the obvious next step, needs Shell to already be started, which
//     adds a real state-management question -- what happens if it
//     isn't -- deliberately left open rather than guessed at).
//   - EDIT (M_edit, ^Je -- confirmed against src/usilicrc's own KEY
//     table: "F-1 e ... EDIT ... K-66", the same F-1-means-^J-leader
//     notation ^Jd/^Jl/^Jp/^Jm etc. already use) is, like PROGRAM,
//     NOT prompt-driven and doesn't switch ApplicationMode at all --
//     tracing menu.c's M_edit case shows it runs immediately against
//     whatever's already on screen (m_edit()'s do_edit() call), no
//     text entry involved. Narrowed to exactly two contexts in this
//     slice: Directory (edits the highlighted entry directly, using
//     the same currentDirectory()+"/"+entry->name path-building
//     openSelectedEntry() already uses, refusing if it's a directory
//     -- an explicit improvement over do_edit(), which doesn't check
//     this at all and would just hand a directory path to the
//     editor) and File (edits the currently displayed file at its
//     cursor line). Both call EditMode::edit() with the SAME
//     KeyInput& PROGRAM already required, for the same reason
//     (leaving/re-entering real raw termio around the child editor).
//     do_edit()'s own line-number argument (`pnp->line`) turns out,
//     on closer reading, to be the file's own per-line line-number
//     field (`rvp->pp->f.line`) when invoked from FILE mode, but
//     merely the cursor's ROW WITHIN THE DIRECTORY LISTING when
//     invoked from directory-listing context -- i.e. legacy passes a
//     directory-listing row number as if it were a line number inside
//     an entirely different file being opened, which doesn't mean
//     anything. This slice deliberately does NOT carry that over:
//     Directory-mode edits always open at line 1; only File mode's
//     ContentView::cursorIndex()+1 is used as a real line number.
//     EditMode::edit()'s own bool return (did the file's content
//     actually change) is used to decide whether to reload FileMode's
//     displayed content afterward (do_edit()'s own st_ctime check,
//     ported using last_write_time() instead -- see EditMode's own
//     class comment for why); a Directory-mode edit never triggers a
//     re-`ls` (do_edit() itself doesn't touch the listing buffer
//     either, only the "next" viewing slot).
//   - HELP (M_help/T_help, ^J^H -- confirmed against src/usilicrc's
//     own KEY table: "F-1 C-h ... HELP ... K-1") turned out, on
//     tracing menu.c's init_bp() default-case handling of M_help/
//     T_help, to not be its own display mechanism at all: legacy
//     hands the help text's PATH straight to init_cut2() with a fixed
//     section-header regex ("^  [1-6][1-6]+\\.") and sets np->M =
//     T_cut -- i.e. HELP genuinely IS a CUT split (a table of numbered
//     sections to pick from), just with a fixed source and pattern
//     instead of a typed one. Confirmed directly with the user this
//     is intentional, not an implementation shortcut to route around:
//     HELP doubling as a live, hands-on demonstration of CUT itself
//     is part of the point. So HELP reuses the SAME CutMode instance
//     real user CUT sessions use, via a new CutMode::pushSplit()
//     (factored out of splitSelected()'s own push-then-split shape,
//     see its own comment) -- if a real CUT session happens to already
//     be active (currentMode_ is Cut/CutContent with a genuine prior
//     split()), pushSplit() preserves it on CutMode's own ModeStack
//     exactly the way a nested splitSelected() resplit would, so ^G
//     out of HELP naturally resumes it via the SAME canGoBack()/
//     goBack() path Back already checks first for Cut. If NO real CUT
//     was active, startHelp() calls split() directly instead (nothing
//     worth preserving) and sets cutReturnMode_ to wherever the user
//     was, the same "leave Cut back to..." anchor a real top-level CUT
//     already uses -- HELP is reachable from any mode, matching
//     legacy's own init_bp() default case having no mode-context
//     guard for M_help/T_help. The distinction between "this is a real
//     CUT" and "this is HELP" is tracked with one new field
//     (cutOrigin_, File or Help) updated only at the two plain split()
//     call sites (startCutPrompt(), startHelp()'s non-preserving
//     branch) -- pushSplit() call sites don't touch it, since a pushed
//     level is by construction nested and already excluded from
//     resize reflow below regardless of origin.
//   - HELP's own text is loaded via loadHelpText(text) (mirroring
//     Configuration::loadRcText()/History::loadHistoryText()'s
//     "caller does the file I/O, this class just takes the text" rule
//     -- Application still does no hidden file reads of its own) and
//     re-wrapped through the exact same Charset::Encoding/PipeBuffer
//     pipeline FileMode/ManMode's own readAndWrap()/runAndWrap() use
//     (wrapHelpText(), a new private helper) -- which is also why this
//     class now stores `encoding_` directly (no other member needed
//     it before this). src/doc/HELP vs HELP.jp (English vs a genuine
//     Japanese translation, not just a re-encoding -- confirmed by
//     reading both: HELP.jp's raw bytes are actual EUC-JP Japanese
//     text) is main.cpp's choice to make, the same "caller resolves
//     the actual path" boundary usilicrc/history already draw --
//     Application only ever sees whichever text it was handed.
//   - Recorded macros (p_km, ^X(/^X)/^Xe -- see src/usilicrc's own
//     "Begin/End/Execute macro definition" KEY entries) are ONE of
//     TWO genuinely different legacy macro mechanisms; the other
//     (p_bm, usilicrc's preset M-0..M-19 macros, defined via an
//     entire user-configurable key-remapping directive format --
//     F-?/K-?/M-? -- letting any physical key be rebound to any
//     internal command) is a MUCH larger, separate feature this port
//     has no supporting infrastructure for at all (Application's
//     Command dispatch is still a fixed, hardcoded table) and is
//     deliberately NOT attempted here -- confirmed explicitly with the
//     user rather than silently dropped, see CLAUDE.md. Tracing
//     key.c's set_km() shows p_km records whatever read_m() itself
//     RETURNS each call (a resolved M_*/T_* command number during
//     normal dispatch, or a literal typed character while a text
//     prompt is active -- read_m() unifies both into one return
//     channel depending on its own current mode) -- NOT the raw
//     keyboard bytes read before interpretation. This port's closest
//     analogous unit is "whatever byte(s) were fed to feedByte()",
//     one layer earlier than read_m()'s own return value, but
//     equivalent in effect: replaying the same byte(s) through
//     feedByte() again deterministically reproduces the same leader-
//     accumulation/prompt-routing/dispatch sequence, so recording at
//     the byte layer needs no separate handling for "was this byte a
//     resolved command or prompt text" the way legacy's own single-
//     int-array a_km[] implicitly relies on read_m()'s dual return
//     meaning to paper over. The one piece of care this needs: the
//     ^X( / ^X) / ^Xe sequences that start/stop/play a recording must
//     NOT themselves end up IN the recording (legacy's own set_km()
//     doesn't append M_ms/M_me to a_km[] either) -- feedByte() holds
//     the leading ^X byte of a leaderX sequence back in
//     pendingLeaderXByte_ until the second byte resolves, THEN
//     commits both bytes together only if the resolution wasn't
//     itself StartMacroRecord/StopMacroRecord/PlayMacro (^J/ESC
//     leader sequences need no such holdback: macro control lives
//     exclusively under ^X, so every ^J/ESC byte can be committed
//     immediately without knowing how its sequence will resolve).
//     playRecordedMacro() replays each recorded byte through a private
//     dispatchByte() (feedByte()'s own former body, now renamed and
//     wrapped) rather than feedByte() itself, so a macro triggered
//     from inside another recording session's own bytes is never
//     accidentally self-recorded -- deliberately NOT replicating
//     set_km()'s own M_mm "endless loop" self-appending quirk (a
//     macro-calls-itself auto-repeat feature the legacy comment itself
//     flags as unusual), which this port doesn't support.
//   - GUI mode (docs/ideas/IDEAS.md's "cat's-paw" hidden mode, not
//     part of legacy tcb at all -- an entirely new feature request)
//     has exactly two pieces of surface here, both DELIBERATELY kept
//     ignorant of SDL2/GraphicalDisplay specifics -- that whole
//     concrete-backend question belongs to a real run loop (app/
//     main.cpp), the same boundary this class already draws around
//     TerminalDisplay:
//       1. ^Og (a NEW leader, ^O -- F-3 in legacy's own KEY table,
//          unused by this port until now) toggles GUI mode.
//          ToggleGuiMode doesn't touch currentMode_/any *Mode at all;
//          it just raises a flag (guiModeToggleRequested_) a run loop
//          polls via consumeGuiModeToggleRequest() (same "flag, not a
//          direct callback" shape as wantsExit()) and decides what
//          "toggle" actually means (which concrete Display backend is
//          active, whether a GraphicalDisplay/window already exists,
//          swapping the run loop's own input source between real tty
//          bytes and translated SDL events) entirely on its own.
//          Tentative binding, confirmed with the user as a placeholder
//          to revise once actually tried (IDEAS.md itself didn't
//          remember the real key).
//       2. handleMouseClick(row, column) -- IDEAS.md's own description
//          ("click the already-highlighted file to view it") read
//          together with ordinary file-manager convention: a click on
//          a DIFFERENT row just selects it (moves the cursor there,
//          matching keyboard Up/Down); a click on the ALREADY-selected
//          row opens it (same as Enter). Directory-mode-only in this
//          slice, matching IDEAS.md's own example scenario (the ls
//          file-selector screen) -- File/Man/other content views
//          could reasonably want click-to-scroll-there too, deferred.
//          Row/column are DISPLAY coordinates (ContentView::
//          lineIndexAtRow()'s own coordinate space, the same one
//          moveCursor() uses) -- translating an actual pixel click
//          into a row/column cell is GraphicalDisplay's job, not
//          Application's. Right-click (a PROGRAM-list context menu,
//          per IDEAS.md) and the inertial "cat's paw" drag physics
//          itself are BOTH explicitly out of scope for this first
//          slice (confirmed with the user) -- this wires plain click-
//          to-select/open only, so the rendering/interaction FEEL can
//          be evaluated before building the fancier parts on top.
//   - handleResize() (terminal resize, T_winch in legacy) is the one
//     piece of state Application reaches for that isn't reached via
//     KeyInput/Command dispatch at all -- tracing menu.c's T_winch
//     case shows legacy handles a resize completely differently
//     depending on WHAT'S on screen: directory/list-buffer-backed
//     modes (T_dir, is_list()/is_str()) just free the buffer to force
//     a full re-list/re-fetch on next redraw (their content isn't
//     width-wrapped paragraph text in the first place -- it's one
//     line per ls/tar/grep/locate/du/history entry, so there's
//     nothing to re-wrap), archive listings similarly regenerate via
//     alloc_head(), and everything else (the free-text-viewing
//     default case) goes through alloc_winch() -- reconstruct the
//     raw bytes from the currently-loaded PP list, discard the old
//     (wrong-width) line-wrapped buffer, and re-run the exact same
//     wrap function used at initial load against the NEW width. This
//     slice ports that same three-way split, using Display&
//     (display_, a new member -- no other *Mode class needed direct
//     Display access before this) to read the post-resize rows()/
//     columns() back rather than trusting separately-passed
//     parameters: EVERY owned ContentView's viewport is resized
//     (matching the T_dir/is_list/archive "no content reflow needed,
//     just refresh geometry" treatment -- correct for Directory/Grep/
//     Locate/Archive/Du/ListBrowser, since none of their content is
//     width-wrapped from a paragraph source), while File and Man mode
//     (the two modes actually built on PipeBuffer's width-wrapping,
//     matching legacy's alloc_winch() default case) get
//     FileMode::reflow()/ManMode::reflow() called INSTEAD of -- not in
//     addition to -- the plain viewport resize, since reflow() already
//     replaces their ContentView's lines outright. See their own class
//     comments for why neither is a literal alloc_winch() port
//     (re-reading the file / re-running `man` rather than reflowing
//     from an in-memory raw-byte cache that doesn't exist in this
//     port, and preserving the CURRENT session's cursor via a clamp
//     rather than legacy's row-index-based set_rv() recompute).
//     Cut/CutContent get the SAME reflow-instead-of-plain-resize
//     treatment, but ONLY when canGoBack()==false (a genuine top-level
//     split, not a nested resplit -- see the "Deliberately excluded"
//     bullet below for the nested case): the fresh source lines to
//     re-split come from fileMode_.reflow()+fileView_.lines() when
//     cutOrigin_==File, or wrapHelpText() when cutOrigin_==Help, then
//     cutMode_.split() is re-run with its own already-remembered
//     pattern()/cutSyntax_ -- the selected segment's INDEX is
//     preserved (clamped), and for CutContent specifically,
//     openSelectedSegment() is called again afterward to re-populate
//     cutContentView_ with that (hopefully still the same) segment's
//     freshly-rewrapped content.
//     Shell's pty is resized unconditionally regardless of
//     currentMode_ (Shell::onResize(), already fully built -- this
//     slice's only Shell-related change is actually calling it): a
//     real terminal resize is a property of the one physical terminal
//     every mode shares, not just whichever mode happens to be
//     visible, matching how a real terminal multiplexer resizes
//     background panes too.
//
// Deliberately excluded (future work, not this slice):
//   - The other 7 of m_list()'s 9 categories (T_hfile/T_hdir/T_hman/
//     T_hlocate/T_hfgrep/T_hegrep/T_list -- the Configuration-seed-
//     backed one, GREP/PROGRAM patterns rather than a History ring)
//     and M_list's own plain ^I/Tab binding (context-dependent in
//     legacy, and Tab already means something different in Shell mode
//     here -- see feedShellByte()): each is the same shape of
//     addition as ^R/^J^R above (pick a category, show it, decide
//     what Enter does), deferred to keep this slice reviewable.
//     Recording entries into these rings at all (e.g. every
//     DirectoryMode::enterDirectory() call feeding
//     RingCategory::RecentDirectory via History::pushRecent()) isn't
//     wired yet either -- History itself is fully capable of it
//     (pushRecent(), already exercised by History's own tests), this
//     slice just doesn't call it from Application, so recentEntries()
//     will be empty until something else populates it (this session's
//     own tests call pushRecent() directly to exercise the browsing
//     side independently).
//   - error/log screens (M_error/M_log): unlike help (see below), these
//     have no dedicated *Mode class of their own yet at all; wiring
//     them in is the same shape of change (extend Command/
//     ApplicationMode, extend feedByte()'s leader table, extend
//     handleCommand()) repeated per mode, deferred to keep each slice
//     reviewable.
//   - DuMode's own chdir()-free design means the legacy behavior of
//     actually changing the process's working directory while
//     measuring isn't replicated (see DuMode's own class comment);
//     Enter on a directory DU entry uses DirectoryMode::enterDirectory()
//     with an absolute path instead, consistent with this port's
//     standing "no hidden global state" rule.
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
//   - m_exit()'s superuser-safety check (`is_inter(NULL, -1, 0)`,
//     guarding against exiting mid-su or similar): not replicated,
//     see the class comment's own note on this. StartExit is
//     unconditionally reachable.
//   - A real terminal run loop (legacy's menu()): reading bytes from
//     a live KeyInput, translating escape sequences, and looping
//     until exit -- now has somewhere to break out to (wantsExit()),
//     but still isn't built here. feedByte()/handleCommand() are the
//     tested, terminal-independent primitives such a loop would call.
//   - ARC-creation (^X^Ka/^Xa, M_arc/M_arc2, m_arc()): a DIFFERENT,
//     unrelated "ARC" from the archive-browsing one above -- menu.c's
//     m_arc() here is the archive-CREATION action grouped alongside
//     CP/MV/RM, not T_tar/etc's archive-browsing/viewing. Unlike
//     CP/MV/RM, this has no ported class yet at all, so it isn't
//     wired regardless of leader-key scope.
//   - File-jump (M_jump, ^X^X -- swap between the cursor position and
//     the last ^Space-marked position): not part of the commands this
//     slice maps.
//   - p_bm (usilicrc's preset M-0..M-19 macros, and the entire F-?/
//     K-?/M-? user-configurable key-remapping directive format they
//     depend on): a genuinely separate, much larger feature from p_km
//     (see its own section above) -- confirmed explicitly with the
//     user to defer to a future slice rather than attempt alongside
//     p_km, given this port's Command dispatch has no configurable-
//     keybinding infrastructure at all yet to build it on.
//   - Nested Cut/CutContent resplits are NOT reflowed on resize
//     (revised from an earlier, broader version of this bullet that
//     excluded ALL of Cut/CutContent -- see CLAUDE.md history for
//     why): TOP-LEVEL Cut/CutContent (canGoBack()==false) DOES now
//     reflow on resize, both for a real File-mode-originated CUT and
//     for HELP -- only a NESTED resplit
//     (splitSelected(), canGoBack()==true) still doesn't, since
//     correctly reflowing a nested level would mean walking back up
//     the WHOLE ModeStack chain to the true root and re-deriving
//     every intermediate split from scratch, not just re-running the
//     current level's own split() -- CutMode's own class comment
//     already treats "what the parent split looked like at
//     splitSelected() time" as a one-shot snapshot at each nesting
//     level, and this port doesn't yet walk that chain to refresh it.
//     A nested resplit's ContentView viewport still resizes; its line
//     content stays wrapped at whatever width was active when its own
//     split()/splitSelected() last ran.
//   - EDIT from any mode besides Directory/File (Grep/Cut/CutContent/
//     Man/Locate/Archive/ArchiveContent/Du/ListBrowser/Shell): legacy's
//     own refuse-list only blocks archive/cut/split viewing contexts
//     outright (T_tar/T_lha/T_unzip/T_rpm/T_deb/T_buff/T_cut/T_split)
//     plus a content-based guard for synthetic non-file entries (a
//     GREP-result "arg" containing '(', an empty/space-led name) --
//     this slice narrows further, to exactly Directory/File, deferring
//     the finer-grained legacy guard logic (and whether e.g. editing
//     the source file behind a GREP match or a LOCATE result even
//     makes sense without more path-resolution machinery than either
//     mode currently exposes) to a future slice.
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
  // homeDirectory: forwarded to the owned PathResolver (K-45/K-47's
  // ^Xf/^Xd path prompts, see StartFileInput's own comment) -- no
  // hidden getenv("HOME") call here, matching PathResolver's own "no
  // hidden global state" rule (already established for Configuration/
  // Shell/etc); the caller (main()) reads the real environment. Also
  // used to derive wFilePath_ (K-17/K-64's own "write buffer to a
  // well-known file" target, `<homeDirectory>/.usilic/w.usilic` --
  // mirrors legacy's own `~/.tcb/w.tcb`, s_S(S_w)). Defaulted to ""
  // so this doesn't break existing callers that don't care about it
  // (~ expansion in a typed path just won't do anything useful, and
  // K-17/K-64 write to a relative "./.usilic/w.usilic" instead).
  Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
              Charset::Encoding encoding, int width, ShellOptions shellOptions = ShellOptions{},
              std::string homeDirectory = "");

  // Lists `directory` (DirectoryMode::enterDirectory()) and makes it
  // the active mode. Ported from init_menu()'s initial directory
  // load. False if the initial `ls` fails.
  //
  // consultHistory forwards straight to enterDirectory()'s own
  // parameter (see its comment): main() passes false whenever it was
  // launched with an explicit path argument, true only for an
  // argument-less launch (legacy's own "resume last session" case).
  bool start(const std::string &directory, bool consultHistory = true);

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
  // still pending returns true. While a macro recording is active
  // (see class comment's own p_km section), also appends this byte
  // (or holds it in pendingLeaderXByte_) to recordedMacro_.
  bool feedByte(int byte, std::optional<KeyInput::SpecialKey> special);

  bool isRecordingMacro() const;
  std::size_t recordedMacroLength() const;

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

  // True once an exit has been confirmed (StartExit's prompt was
  // submitted with a leading 'y') -- a future real run loop's signal
  // to stop calling feedByte() and unwind. Application itself never
  // reads this; nothing else changes when it flips.
  bool wantsExit() const;

  // Sets wantsExit() true directly, bypassing StartExit's y/n
  // confirmation prompt entirely. Added for the GUI-launched session's
  // own simpler exit gesture (ESC ESC Enter, see app/main.cpp's
  // runGuiPhase()) -- requested in place of the confirmation prompt
  // for that context, not a replacement for it elsewhere (the
  // terminal phase's ^X^C+y flow is untouched).
  void requestImmediateExit();

  // True if ^Og was pressed since the last call (consumes the flag,
  // same shape as KeyInput::consumeResizeSignal()) -- a run loop's
  // signal to toggle GUI mode. See class comment's own GUI-mode
  // section for why Application knows nothing about what "toggle"
  // actually does.
  bool consumeGuiModeToggleRequest();

  // A mouse click at DISPLAY row/column (see class comment's own GUI-
  // mode section for this coordinate space and current Directory-
  // mode-only scope). Selects the entry at that row, or -- if it was
  // already the selected one -- opens it (same as Enter). False if
  // outside Directory mode or the click landed outside any real row.
  bool handleMouseClick(int row, int column);

  // A terminal resize was detected (a real run loop's signal, after
  // KeyInput::consumeResizeSignal() has already updated Display's own
  // geometry -- this reads rows()/columns() back off `display`, not
  // off separate parameters, so it can never see a stale size).
  // Ported from menu.c's T_winch handling: resizes every owned
  // ContentView's viewport and the prompt field to the new geometry,
  // re-wraps File/Man mode's content at the new width if either is
  // currently active (FileMode::reflow()/ManMode::reflow()), and does
  // the same for a TOP-LEVEL Cut/CutContent split (real CUT or HELP --
  // see the class comment's own HELP section) -- Directory/Grep/
  // Locate/Archive/Du/ListBrowser content isn't width-wrapped
  // paragraph text the way File/Man/top-level-Cut's is, matching
  // legacy's own T_dir/is_list/is_str non-reflow treatment, so their
  // lines are left as-is beyond the viewport resize; a NESTED Cut
  // resplit is left as-is too (see class comment). Propagates the new
  // size to Shell's pty UNCONDITIONALLY (even if Shell isn't the
  // visible mode right now -- a real terminal resize affects the one
  // physical terminal every mode shares, matching how a real terminal
  // multiplexer resizes background panes too), and redraws whatever's
  // currently on screen.
  void handleResize();

  // Called by the main loop whenever Shell::onPtyReadable() reports
  // EOF (the child process, typically via `exit`, has ended on its
  // own). See the definition's own comment.
  void handleShellChildExited();

  // Supplies HELP's (^J^H) text -- mirroring Configuration::
  // loadRcText()/History::loadHistoryText()'s "caller does the file
  // I/O" rule, see the class comment's own HELP section. Must be
  // called before ^J^H can do anything; false (and HELP stays
  // unavailable, StartHelp simply fails) if text is empty.
  bool loadHelpText(const std::string &text);

  ApplicationMode currentMode() const;
  // The currently active charset, live-updated by cycleCharset()
  // (K-27, ^Oa). main.cpp reads this at shutdown for History::
  // setSavedCharset() instead of its own startup-time-only local, so
  // a mid-session charset switch actually persists across runs --
  // see main.cpp's own call site comment.
  Charset::Encoding encoding() const;

  // True whenever the NEXT byte fed to feedByte() would be consumed
  // as part of a pending multi-key sequence (ESC-prefix, ^J-leader)
  // or as literal text into an active prompt/entry widget, rather
  // than interpreted as a plain top-level navigation command. Added
  // for runGuiPhase()'s w/s inertial-scroll-trigger interception (see
  // CLAUDE.md's 2026-07-31 note): 'w'/'s' must fall through to
  // feedByte() untouched in any of these states -- typing "ESC s" for
  // StartShell, or typing a literal "w"/"s" into an FGREP/path prompt
  // -- rather than being hijacked for scrolling. Combined with a
  // currentMode() != Shell check at the call site (typing must always
  // reach the shell's own command line).
  bool awaitingSequenceOrPromptByte() const;
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
  DuMode &duMode();
  ListBrowserMode &listBrowserMode();
  EditMode &editMode();
  ExecMode &execMode();

  // Demo-support (2026-07-30 drag-scroll feasibility sample, see
  // GraphicalDisplay's own class comment) -- exposes whichever
  // ContentView is currently on screen so a prototype run loop can
  // render/scroll its full content directly, bypassing Command
  // dispatch entirely. NOT meant as a permanent API: a real
  // integration would fold validated scroll behavior into ContentView
  // itself (or Application's own Command handling) once this sample
  // is evaluated, not keep reaching in from outside like this.
  ContentView &activeContentView();

 private:
  // One recorded byte, for p_km (see class comment) -- feedByte()'s
  // own former body, renamed dispatchByte(), does the actual work;
  // feedByte() itself just wraps it with recording bookkeeping.
  struct RecordedStep {
    int byte;
    std::optional<KeyInput::SpecialKey> special;
  };
  bool dispatchByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // GrepPattern/CutPattern's own "Select string" ListBrowser (see
  // startRegexPromptList()'s own comment): a curated set of bytes
  // stays list-navigation-only, everything else transitions into an
  // editable prompt pre-filled with the highlighted candidate's own
  // text -- see its own definition comment for the full design.
  bool feedRegexListByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // The ESC/^J/^X/^X^K/^O leader-key state machine, factored out of
  // dispatchByte() so feedShellByte() can also reach it -- see its own
  // comment (2026-08-02 user report: legacy lets ^J/^X/^O leaders work
  // directly from within SHELL, e.g. ^X^F/^X^C, which this port
  // silently swallowed as raw shell input before this existed).
  bool dispatchLeaderByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // ^P/^N's own plain-text-cursor replacement for SHELL scrollback_'s
  // whole-line highlight -- see its own definition comment.
  void moveDisplayCursorToShellHistoryLine();
  bool startMacroRecording();
  bool stopMacroRecording();
  bool playRecordedMacro();

  bool openSelectedEntry();
  bool openSelectedSegment();
  bool openSelectedArchiveEntry();
  // Nested-archive support (docs/ideas/IDEAS.md's own original
  // motivating example for the whole C++ port: "アーカイブの中に
  // さらにアーカイブファイルがあっても、それを選んで再度ARCモードに
  // 入る(どこまで潜っても同様に動く)ことができない" -- legacy itself
  // never supported this either, see menu.c's T_tar/etc handling,
  // which only ever extracts to a destination or shows raw bytes;
  // this is new functionality this port adds, not a port of existing
  // behavior). ArchiveMode itself stays a single-archive-at-a-time
  // class by design (see its own class comment) -- coordinating the
  // "currently open chain of archives" is Application's job, the same
  // boundary already drawn for every other mode hand-off.
  //
  // ArchiveMode::loadArchive() needs a real file path (it shells out
  // to tar/unzip/etc, which don't read from a memory buffer), so
  // entering a nested archive means materializing its extracted bytes
  // (already available via ArchiveMode::extractSelected(), no new
  // extraction logic needed) to a real temporary file first, then
  // loading THAT path exactly the same way the top-level archive was
  // loaded. archiveParentStack_ remembers each level's path (real
  // file for the outermost archive, one of these temp files for every
  // level below it) so returnFromArchive() can climb back out one
  // level at a time instead of always returning straight to
  // Directory.
  bool descendToNestedArchive(const ArchiveEntry &entry);
  // Writes `bytes` to a freshly created temp file (a new mkdtemp()'d
  // directory per call, avoiding basename collisions between sibling
  // entries at different nesting levels that happen to share a name)
  // named after entry.name's own basename (preserving its suffix,
  // since programForSuffix() dispatches on that). Left for the OS to
  // clean up on reboot/tmp-cleaner sweep, same as this project's
  // already-documented, already-accepted tolerance for /tmp/.tcb's
  // own unswept scratch files (CLAUDE.md) -- not deleted eagerly
  // because a parent frame still on archiveParentStack_ may need to
  // reload it later in the same session. Empty string on failure
  // (mkdtemp()/ofstream failure).
  std::string materializeArchiveTempFile(const std::string &entryName, const std::vector<unsigned char> &bytes);
  bool openSelectedDuEntry();
  bool openSelectedListEntry();
  bool toggleMark();
  bool returnToDirectory();
  bool openSelectedGrepMatch();
  bool returnFromGrep();
  bool returnToGrepResults();
  bool returnFromCutContent();
  bool returnFromCut();
  bool returnToCutResults();
  bool returnFromMan();
  bool returnFromLocate();
  bool returnFromArchiveContent();
  bool returnFromArchive();
  bool returnFromShell();
  bool returnFromDu();
  bool returnFromListBrowser();
  bool returnFromKey();
  ContentView &activeView();
  void layoutViews();
  std::vector<std::string> collectTargetPaths() const;
  bool startListBrowser(RingCategory category);
  // See ToggleHistoryView's own comment.
  bool toggleListBrowserHistory();
  std::vector<std::string> sortedUniqueRecentEntries(RingCategory category) const;
  // K-2 (^H, M_key, "KEY"): see startKey()'s own definition comment.
  bool startKey();
  // Enter while in Key mode: returns to keyReturnMode_ FIRST (matching
  // HELP.jp 2.2's own documented order -- "KEYを呼び出したモードに
  // 戻ったのち，対応するキー定義を実行します"), THEN feeds the
  // selected entry's own byte sequence through feedByte(), one byte
  // at a time, so it's dispatched exactly as if the user had typed it
  // live in that mode (naturally re-entering leader-key states for
  // multi-byte sequences, same as a real keypress would).
  bool executeSelectedKeyBinding();

  bool startGrepPrompt(GrepSyntax syntax);
  bool startCutPrompt(GrepSyntax syntax);
  bool startCutFilePrompt(GrepSyntax syntax);
  bool startManPrompt();
  bool startProgramPrompt();
  bool startLocatePrompt();
  bool runLocateUpdateDb();
  bool startShell();
  // Shared by startShell() and K-61/K-62/K-63/K-64: gets Shell mode
  // on screen (starting the shell process if needed) WITHOUT
  // startShell()'s own "already in Shell mode" refusal -- unlike a
  // plain ESC-s/^Xs entry, these four keys are meant to work whether
  // or not Shell was already active (legacy's own m_w() call site has
  // no such mode-guard either, only the separate is_inter() busy
  // check).
  bool ensureShellActive();
  // Snapshot of whichever *Mode's own "current name" accessor matches
  // `mode`, for shellReturnName_ (see its own comment). Only the
  // modes that already have such an accessor AND are plausible entry
  // points into Shell are covered (Directory/File/Man/Archive/
  // ArchiveContent/Du); everything else (Grep/Cut/CutContent/Locate/
  // ListBrowser -- none of which currently expose a single "name"
  // string of their own) falls back to "", which simply omits the
  // trailing field rather than guessing.
  std::string previousModeName(ApplicationMode mode) const;
  // The label shown for each RingCategory in the LIST-based history
  // pickers' own mode-line trailing field -- see
  // listBrowserView_.setModeLineFieldsProvider()'s own comment
  // (constructor).
  std::string ringCategoryLabel(RingCategory category) const;
  // K-60 (^Xs, M_hup): kills the current SHELL child (if any, via
  // Shell::terminate()'s SIGHUP+wait) and starts a fresh one, entering
  // Shell mode. Ported from menu.c's own M_hup case: legacy's
  // is_inter(F12,...) SIGHUPs both the shell and its IPC relay process
  // and blocks until both die, which Shell::terminate() already does
  // the equivalent of in this port's simpler single-process pty
  // design (see Shell's own class comment on why there's no relay
  // process here). Refused, like legacy's own is_inter(NULL,...)
  // guard, while a handed-off interactive PROGRAM is still running
  // (Shell::busy()) -- restarting the pty out from under something
  // like `less` currently controlling it would strand that child.
  bool shellRestart();
  bool saveBufferToFile();
  bool shellChangeDirectory();
  bool shellRunLsL();
  bool shellInsertName();
  bool shellInsertBufferContent();
  // K-67 (^X^Ke): opens errorLogPath_ in File mode. Not prompt-driven
  // -- an immediate action, like Directory mode's own archive
  // auto-detection. False if the file doesn't exist yet (nothing
  // logged there this session) or can't be opened.
  bool executeShowErrorLog();
  // K-68 (^X^Kl): writes Shell::scrollback()'s CURRENT lines to
  // logFilePath_ (truncating any previous snapshot), then opens that
  // file in File mode. This is a snapshot on demand, not a
  // continuous tee of Shell's pty output the way legacy's own
  // write(sfd,...) is (see shell.c's read_line()) -- building that
  // would mean plumbing a log-file fd through Shell's byte-by-byte
  // output processing for a payoff (seeing output that's since
  // scrolled off-screen or been cleared) this simpler snapshot
  // already delivers for the common case of "what did the shell just
  // print." False if Shell was never started (nothing to snapshot)
  // or the file can't be written/opened.
  bool executeShowShellLog();
  // K-0 (^T): builds a short status/about screen -- version, the
  // usilicrc/history file paths (str.c's print_tcb() shows the SAME
  // two fields, "Startup file"/"History file"), user@host, and the
  // ERROR/LOG paths (K-67/K-68) -- and opens it via File mode.
  // Deliberately narrower than legacy's own print_tcb(): "Startup
  // mode" (file manager vs pager) and "Command line" (the invoking
  // argv) are both omitted -- this port has no pager-only launch path
  // yet (main.cpp always calls Application::start() into Directory
  // mode regardless of argument), and Application doesn't currently
  // receive its own argv at all, so there's nothing genuine to report
  // for either field rather than a guessed/hardcoded placeholder.
  // Written to a throwaway mkdtemp() temp file (materializeArchiveTempFile()'s
  // own pattern) since FileMode::loadFile() needs a real path and
  // this text has no other natural on-disk home the way ERROR/LOG do.
  bool executeShowTcbBanner();
  // K-27 (^Oa): ported from key.c's own key_getch() comment ("was a
  // straight 2-way ASCII<->EUC toggle; cycle through all three now
  // instead of leaving UTF-8 users stuck toggling between ASCII and
  // EUC with no way back to UTF-8" -- that fix already happened on
  // the legacy C side, this just ports the resulting 3-way cycle).
  // Updates encoding_ and propagates it to every already-ported class
  // that owns its own copy (FileMode/ManMode/Shell/promptEditor_ --
  // see each one's own setEncoding() comment on what it does and
  // doesn't retroactively affect), then reflows whatever's currently
  // on screen (File/Man mode) so the effect is immediately visible
  // instead of only applying to the NEXT file/page opened. Always
  // succeeds (no mode restriction -- legacy's own M_ascii has none
  // either, unlike most other K-? commands in this port).
  bool cycleCharset();
  bool startCopyPrompt(bool interactive = false);
  bool startMovePrompt(bool interactive = false);
  bool startRemovePrompt(bool interactive = false);
  bool startExitPrompt();
  bool startDuPrompt();
  // K-57/K-58 (^X^Ka/^Xa, M_arc/M_arc2, "ARC: Expand archive"):
  // starts the "Destination ?" prompt (HELP.jp 2.11). Two entry
  // points, both valid per HELP.jp's own text:
  //   - From Directory mode, the selected entry must itself be a
  //     recognized archive suffix (same programForSuffix() check
  //     openSelectedEntry() already uses at the top level) -- the
  //     WHOLE archive gets extracted.
  //   - From Archive mode (already browsing an archive's TOC, whether
  //     top-level or nested), targetEntries() must be non-empty (the
  //     marked entries, or the current selection if nothing's marked
  //     -- see ArchiveMode::toggleMark()'s own comment) -- only those
  //     specific entries get extracted.
  // False (refused) from every other mode, or if neither condition
  // above holds (e.g. Directory mode with a non-archive file
  // selected).
  bool startArchivePrompt(bool interactive = false);
  // Builds the extraction argv for `kind` (Tar/Zip only in this first
  // slice -- Lha's own extraction syntax differs from the `lha p`
  // extract-to-stdout command extractSelected() uses, and Rpm/Deb
  // already have no locateContent() support at all, see ArchiveMode's
  // own class comment; extending this to those formats is future
  // work, not guessed at here). `members` empty means "the whole
  // archive"; non-empty means "just these entries" (tar/unzip both
  // accept trailing member-name arguments for exactly this). Empty
  // result for an unsupported kind.
  std::vector<std::string> buildArchiveExtractArgv(const std::string &archivePath, ArchiveKind kind,
                                                    const std::vector<std::string> &members,
                                                    const std::string &destination) const;
  bool startPathInputPrompt(bool isDirectory);
  // K-65 (^I, M_list, "LIST: Complete file name"): lists filesystem
  // entries under the directory implied by `partial` (resolved
  // against `baseDirectory` when `partial` isn't itself absolute or
  // ~-rooted) whose name starts with the remaining prefix. Each
  // returned candidate is the FULL completion (partial's own
  // directory portion + matched name), not just the matched suffix --
  // matching Shell::completionProvider_'s own contract (see its
  // declaration comment): the caller only acts when there's exactly
  // one candidate, splicing in the part past what was already typed.
  // Directories get a trailing '/' (this port's established ls -F
  // convention, e.g. DirectoryEntry::name) so a single unambiguous
  // directory match can be Tab-completed again immediately for the
  // next path segment. Shared by Shell's own Tab-completion (wired in
  // the constructor) and the FILE/DIR PathInput prompt's own Tab
  // handling in feedPromptByte() -- deliberately NOT wired to every
  // other prompt kind (MAN/PROGRAM/LOCATE/GREP names aren't
  // filesystem paths at all) in this first slice; see class comment's
  // own scope notes for the general pattern.
  std::vector<std::string> completeFilesystemPath(const std::string &partial,
                                                    const std::string &baseDirectory) const;
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
  bool executeExit(const std::string &confirmation);
  bool executeDu(const std::string &path);
  bool executeArchiveExtract(const std::string &destination);
  bool executePathInput(const std::string &path);
  bool executeEdit();
  bool startHelp();
  std::vector<StyledLine> wrapHelpText(int width) const;

  Display &display_;  // handleResize()'s own rows()/columns() lookup -- no other member currently needs this
  PathResolver pathResolver_;  // ^Xf/^Xd path prompts, see the constructor's own homeDirectory comment
  std::string wFilePath_;  // K-17/K-64's write-buffer-to-a-file target, see the constructor's own comment
  // K-67 (^X^Ke, M_error, "ERROR"): where CpMvRmMode routes non-
  // interactive child processes' stderr (see the constructor's own
  // comment and CpMvRmMode's own errorLogPath_ parameter). Ported
  // from str.c's S_error path (`~/.tcb/error`, this port's own
  // `~/.usilic/error`).
  std::string errorLogPath_;
  // K-68 (^X^Kl, M_log, "LOG: SHELL output"): NOT a continuous tee of
  // Shell's pty output the way legacy's own write(sfd,...) is (see
  // shellLog()'s own comment on this simplification) -- a snapshot of
  // Shell::scrollback()'s current lines, written out on demand when
  // K-68 is actually pressed. Ported from str.c's S_log path
  // (`~/.tcb/shell`, this port's own `~/.usilic/shell`).
  std::string logFilePath_;
  // K-0's own banner text (see executeShowTcbBanner()'s own comment)
  // reports these two paths verbatim -- str.c's print_tcb() shows the
  // SAME two fields ("Startup file"/"History file").
  std::string usilicrcPath_;
  std::string historyFilePath_;
  Charset::Encoding encoding_;  // wrapHelpText()'s own PipeBuffer construction -- no other member currently needs this
  const Configuration &configuration_;  // executeProgram()'s own PROGRAM-list lookup, see find_prog() note above
  History &history_;  // startListBrowser()'s own recentEntries() lookup -- no other member currently needs this
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
  ContentView duView_;
  ContentView listBrowserView_;
  // K-2 (^H): backing view for startKey()'s static binding table --
  // no dedicated *Mode class, since the table itself is static (no
  // runtime parsing/state beyond which line is currently selected,
  // which ContentView already tracks). See startKey()'s own comment.
  ContentView keyView_;
  LineEditor promptEditor_;
  DirectoryMode directoryMode_;
  FileMode fileMode_;
  GrepMode grepMode_;
  CutMode cutMode_;
  ManMode manMode_;
  ProgramMode programMode_;
  LocateMode locateMode_;
  ArchiveMode archiveMode_;
  // Nested-archive descent -- see descendToNestedArchive()'s own
  // comment. archiveParentStack_[i] is the path to reload (real file
  // for the outermost archive, one of materializeArchiveTempFile()'s
  // own freshly mkdtemp()'d temp files below it) to climb back up to
  // nesting level i.
  std::vector<std::string> archiveParentStack_;
  Shell shell_;
  CpMvRmMode cpMvRmMode_;
  DuMode duMode_;
  ListBrowserMode listBrowserMode_;
  EditMode editMode_;
  ExecMode execMode_;

  ApplicationMode currentMode_ = ApplicationMode::Directory;
  ApplicationMode grepReturnMode_ = ApplicationMode::Directory;
  ApplicationMode cutReturnMode_ = ApplicationMode::Directory;
  ApplicationMode manReturnMode_ = ApplicationMode::Directory;
  ApplicationMode locateReturnMode_ = ApplicationMode::Directory;
  ApplicationMode shellReturnMode_ = ApplicationMode::Directory;
  // The previous mode's own "name" field (e.g. DirectoryMode::
  // currentDirectory(), FileMode::currentFile()), snapshotted at the
  // moment Shell mode is entered -- see previousModeName()'s own
  // comment. Ported from legacy's do_shell(prev, ...): menu.c's own
  // M_shell case (ESC-s) copies `ap->name` (the SAME string every
  // mode's own mode-line[1] "name" field is built from, see
  // disp_window()) into a local `name` buffer and passes it straight
  // through to do_shell() as `prev`, which shell.c carries all the
  // way to disp_window(..., T_shell, name="SHELL", prev, ...) as the
  // mode-line[6] "previous-mode" trailing field. A real user report
  // (2026-08-01) confirms this reads as "whatever name identified the
  // screen you came from" -- current directory when entering from
  // Directory mode, but the filename when entering from File mode,
  // NOT always "current directory" as this port first assumed.
  std::string shellReturnName_;
  ApplicationMode listBrowserReturnMode_ = ApplicationMode::Directory;
  RingCategory listBrowserCategory_ = RingCategory::RecentDirectory;
  // See ToggleHistoryView's own comment. Reset to false every fresh
  // startListBrowser() entry; flipped by toggleListBrowserHistory().
  bool listBrowserChronological_ = false;
  ApplicationMode keyReturnMode_ = ApplicationMode::Directory;
  GrepSyntax grepSyntax_ = GrepSyntax::Fixed;
  GrepSyntax cutSyntax_ = GrepSyntax::Fixed;
  // Non-empty between startCutFilePrompt() and the CutPattern prompt's
  // submit (K-35/K-36's "regular file" variant): the path to load and
  // split instead of the currently-displayed buffer. Cleared by
  // executeCut() as soon as it's consumed, so the very next CUT (of
  // either variant) doesn't accidentally reuse a stale path.
  std::string cutFileSourcePath_;
  // Set by startCopyPrompt(true)/startMovePrompt(true)/
  // startRemovePrompt(true) (K-51/K-53/K-55's "with SHELL window"
  // entry points), consumed once by executeCopy()/executeMove()/
  // executeRemove() to pick the interactive CpMvRmMode method instead
  // of the plain captured one.
  bool cpMvRmInteractive_ = false;
  // Set by startArchivePrompt(true) (K-57's "with SHELL window" entry
  // point), consumed once by executeArchiveExtract() the same way
  // cpMvRmInteractive_ is.
  bool archiveExtractInteractive_ = false;
  bool shellStarted_ = false;
  std::string helpText_;
  // Which top-level Cut/CutContent split is currently active -- only
  // meaningful (and only consulted by handleResize()) when
  // !cutMode_.canGoBack(); a pushSplit() (nested) level doesn't touch
  // this, see the class comment's own HELP section.
  enum class CutOrigin { File, Help, Directory };
  CutOrigin cutOrigin_ = CutOrigin::File;

  // Which search set DirectoryMode's current search annotation (see
  // its own setSearchAnnotation() comment) -- the annotation text
  // alone doesn't distinguish "FGREP:/EGREP:" from "CUT:" structurally
  // enough to know which results screen Command::Back should route
  // to, so this is set alongside every setSearchAnnotation() call.
  enum class DirectorySearchKind { Grep, Cut };
  DirectorySearchKind lastDirectorySearchKind_ = DirectorySearchKind::Grep;

  bool recordingMacro_ = false;
  std::vector<RecordedStep> recordedMacro_;
  // Holds a leaderX sequence's leading ^X byte back until the second
  // byte resolves, so a StartMacroRecord/StopMacroRecord/PlayMacro
  // resolution can be excluded from recordedMacro_ entirely (both
  // bytes at once) -- see class comment.
  std::optional<RecordedStep> pendingLeaderXByte_;

  bool leaderJPending_ = false;  // saw ^J, waiting for the byte that completes a two-byte command
  bool escPending_ = false;      // saw ESC (0x1b), waiting for the byte that completes ^[s -- see class comment
  bool leaderXPending_ = false;  // saw ^X, waiting for the byte that completes ^Xc/^Xm/^Xr/^X^C (exit shares ^X too)
  bool leaderXKPending_ = false;  // saw ^X^K (usilicrc's own F-2), waiting for the byte completing ^X^Kc/m/r
  bool leaderOPending_ = false;  // saw ^O, waiting for the byte that completes ^Og
  bool wantsExit_ = false;
  bool guiModeToggleRequested_ = false;

  enum class PromptKind {
    None,
    GrepPattern,
    CutPattern,
    ManName,
    ProgramCommand,
    LocateQuery,
    CopyDestination,
    MoveDestination,
    RemoveConfirm,
    ExitConfirm,
    DuPath,
    PathInput,  // K-45/K-47's shared prompt kind, see Command::StartFileInput's own comment
    ArchiveDestination  // K-57/K-58's own Destination prompt, see startArchivePrompt()'s own comment
  };
  // Shared by submitPrompt() (text typed live) and
  // openSelectedListEntry()'s own prompt-recall branch (text picked
  // from ^R's history list instead) -- see startPromptHistoryRecall()'s
  // own comment. Factored out so both paths dispatch on PromptKind
  // identically rather than duplicating the switch.
  bool executePromptSubmission(PromptKind kind, const std::string &text);
  // K-49 (^R) pressed WHILE a supported prompt (MAN/LOCATE/FGREP/
  // EGREP name/query/pattern) is active: see its own definition
  // comment for the full design. False (byte silently swallowed, not
  // inserted as literal text) for prompt kinds with no history of
  // their own yet -- matching m_list()'s own echo_mess(S_errf) for a
  // context ^R doesn't apply to, same spirit as ToggleHistoryView's
  // own outside-ListBrowser no-op.
  bool startPromptHistoryRecall(bool directEntry = false);
  // K-32/K-34 (^J^S EGREP, ^J^C CUT's regex variant): opens DIRECTLY
  // into the candidate-pattern list, skipping the blank prompt step
  // entirely -- see startGrepPrompt()/startCutPrompt()'s own comment
  // for why (confirmed against real legacy-tcb behavior, not just a
  // usilicrc wording nuance) and startRegexPromptList()'s own
  // definition comment for how it reuses startPromptHistoryRecall()
  // wholesale.
  bool startRegexPromptList(PromptKind kind);
  // K-49 (^R) pressed at the live SHELL prompt (integration point 3 of
  // the ^R design -- see CLAUDE.md item 162/163): opens a sorted
  // RecentEntries(ShellCommand) ListBrowser, same underlying data
  // ^J^R's own "COMMAND" picker already uses. Deliberately a SEPARATE
  // flag (shellHistoryRecallActive_) rather than reusing
  // promptHistoryRecallKind_ (SHELL isn't a PromptKind -- it's its own
  // ApplicationMode with its own feedShellByte() input path) or
  // folding into ^J^R's existing openSelectedListEntry() handling for
  // RingCategory::ShellCommand (that stays a deliberate no-op on Enter,
  // per its own "in this slice" test -- this is a DIFFERENT feature
  // that happens to share the same ring). Confirmed directly with the
  // user (2026-08-05): selecting an entry INSERTS it into the command
  // line for review/editing, it does NOT auto-run -- unlike MAN/
  // LOCATE/FGREP's own recall (side-effect-free actions), a picked
  // SHELL command could be destructive (rm, mv, ...), so silently
  // re-running an old one without a chance to look at it first would
  // be a real safety regression, not just a UX quirk.
  bool startShellHistoryRecall();
  // Shared trailer for every startXxxPrompt(): sets promptKind_, draws
  // that kind's own label onto the mode-line row (see
  // promptLabelText()'s own comment), and starts promptEditor_ with
  // initialText. A real user report (2026-08-02) found NO prompt
  // anywhere showed any visible indication of what it was asking for
  // (FGREP? EXIT y/n? DU path?) -- every startXxxPrompt() previously
  // did its own promptKind_=/setText()/redraw() trio with no label at
  // all, matching legacy's OWN mode-line[1] "name" field being
  // silently left unported this whole project.
  // positionText (2026-08-12 fix, default ""/not shown): a direct
  // user correction -- "[bs]→「1/10 TOP」が消滅" (backspacing an
  // existing candidate straight into editing dropped its own "which
  // of N candidates" position, which had been visible the whole time
  // the candidate list was just being browsed). feedRegexListByte()'s
  // own edit-transition passes ModeLine::format(cursorIndex+1, count)
  // through here so drawPromptLabel() can keep showing it; every other
  // activatePrompt() call site (plain FGREP/MAN/LOCATE/.../ArchiveDestination
  // prompts, none of which originate from a numbered list) leaves it
  // at the default.
  void activatePrompt(PromptKind kind, const std::string &initialText, const std::string &positionText = "");
  // The label text shown for each PromptKind -- ported from legacy's
  // own s_P(P_mode)[...] mode-name strings (disp_window()'s mode-
  // line[1] "name" field, e.g. " EXIT", " FGREP") for the ordinary
  // cases, PLUS M_exit's own disp.c special case
  // (sprintf(line, "%s (y/n) ?  ", s_S(S_errg))) folded into ONE
  // combined string here rather than replicating legacy's separate
  // name+position-field split -- this port has no equivalent second
  // field to put "(y/n) ?" into once promptEditor_ already owns the
  // rest of the row, and a single combined label communicates the
  // same thing with far less mechanism.
  std::string promptLabelText(PromptKind kind) const;
  // Draws promptLabelText(kind) in reverse video at the mode-line
  // row's own left edge, then narrows promptEditor_'s own field to
  // start right after it (see activatePrompt()'s own comment). Split
  // out from activatePrompt() only so a future resize-while-prompting
  // fix (not attempted here, see this port's own known-limitations
  // notes) has an obvious single place to re-call.
  void drawPromptLabel(PromptKind kind, const std::string &positionText = "");
  PromptKind promptKind_ = PromptKind::None;
  // See activatePrompt()'s own comment.
  bool promptCandidateListActive_ = false;
  // Set only while a ^R-triggered prompt-history ListBrowser is open
  // (see startPromptHistoryRecall()'s own comment): which prompt to
  // resume, what had already been typed there before ^R was pressed
  // (restored verbatim on Back/^G -- ^R shouldn't silently discard
  // partial input), and which ApplicationMode was active underneath
  // the prompt (prompts overlay the CURRENT mode rather than owning
  // one of their own, so this has to be saved/restored across the
  // ListBrowser interlude the same way listBrowserReturnMode_ is for
  // the plain ^X^F/^X^D case). nullopt outside a recall session.
  std::optional<PromptKind> promptHistoryRecallKind_;
  std::string promptHistoryRecallText_;
  ApplicationMode promptHistoryRecallReturnMode_ = ApplicationMode::Directory;
  // True only when this ListBrowser interlude was reached via
  // startRegexPromptList() (^J^S/^J^C's own "opens directly into the
  // candidate list" entry, see its own comment) rather than a real ^R
  // recall of PAST typed patterns -- legacy's mode.c distinguishes
  // these as genuinely different screens (T_hfgrep/T_hegrep for ^R vs
  // plain M_fgrep/M_locate/M_cut2 for direct entry): a real ^R recall
  // always shows the fixed literal "HISTORY" name (see
  // startPromptHistoryRecall()'s own 2026-08-05 comment on why that's
  // correct there) and starts on the NEWEST entry (bottom); direct
  // entry shows the actual mode name (e.g. "CUT") plus the destination
  // directory highlighted on the right, and starts on the TOP entry
  // (a real report + legacy source read, 2026-08-11: T_hfgrep/T_hegrep
  // explicitly set `row = nrow - 1`, but M_fgrep/M_locate/M_cut2 just
  // set `i = -2`, defaulting to the top). Reset alongside
  // promptHistoryRecallKind_ at every consumption site.
  bool promptHistoryRecallIsDirectEntry_ = false;
  // 2026-08-11: a real report + direct user clarification -- "検索への
  // 入口がCUTなら出るときもCUTから出るはず" (if the search's own
  // entrance was the candidate list, Back should exit through it too),
  // i.e. a proper LIFO stack: Directory -> candidate list -> results,
  // and ^G pops ONE level at a time, not straight back to Directory.
  // promptHistoryRecallIsDirectEntry_ itself gets cleared the instant
  // feedRegexListByte()/openSelectedListEntry() consume it (well
  // before the search actually runs), so its value is captured here
  // FIRST and carried through to executeGrep()/executeCut() on
  // success, which transfer it into grepReturnsToCandidateList_/
  // cutReturnsToCandidateList_ -- consulted (and cleared, one-shot)
  // by returnFromGrep()/returnFromCut() to decide whether Back
  // reopens the candidate list (via startRegexPromptList() again)
  // instead of returning straight to grepReturnMode_/cutReturnMode_.
  // A SECOND round-trip (re-edit the pattern from that reopened list,
  // submit again) sets the flag fresh again, so this naturally chains
  // for as many search/edit cycles as the user does -- the only way
  // to reach Directory is explicitly cancelling FROM the candidate
  // list itself (returnFromListBrowser()'s own unrelated, pre-existing
  // ^G-cancel path).
  bool pendingSearchFromDirectEntryList_ = false;
  bool grepReturnsToCandidateList_ = false;
  bool cutReturnsToCandidateList_ = false;
  // See startShellHistoryRecall()'s own comment.
  bool shellHistoryRecallActive_ = false;
  std::string shellHistoryRecallText_;
  // Which of the two keys (K-45 ^Xf vs K-47 ^Xd) started the current
  // PathInput prompt -- see startPathInputPrompt()'s own comment for
  // why EXECUTION stays unified (PathResolver decides file-vs-
  // directory from the typed path itself) while the mode-line LABEL
  // still needs to match legacy's own separate " FILE"/" DIR" text for
  // whichever key was actually pressed (a real side-by-side screenshot
  // comparison against legacy, 2026-08-03, found this port always
  // showing "FILE/DIR" regardless of which key started the prompt).
  bool pathInputIsDirectory_ = false;
};

}  // namespace fm
