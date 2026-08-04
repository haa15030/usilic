#include "application.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "pipe_buffer.hpp"

namespace fm {

namespace {
constexpr int kLeaderJ = 0x0a;         // ^J
constexpr int kLeaderX = 0x18;         // ^X
constexpr int kLeaderO = 0x0f;         // ^O
constexpr int kEscape = 0x1b;          // ESC
constexpr int kControlS = 0x13;        // ^S
constexpr int kControlC = 0x03;        // ^C
constexpr int kControlG = 0x07;        // ^G
constexpr int kCarriageReturn = 0x0d;  // ^M
// K-2 (^H, M_key, "KEY"): startKey()'s own static binding table.
// Unlike HELP (which loads real prose text at runtime via
// loadHelpText()), this table is NOT parsed from usilicrc's own KEY
// section -- Configuration deliberately excludes that (see its own
// class comment: "its bindings target mode/command numbers that
// don't exist yet in this C++ port... deferred until Mode exists").
// That gap hasn't closed by rewriting Configuration's KEY parser
// (a separate, still-open project, since usilicrc's KEY syntax
// supports arbitrary user rebinding this table doesn't reproduce);
// instead, this table is a hand-maintained MIRROR of dispatchByte()'s
// /resolveCommand()'s own switch statements -- the single source of
// truth for which key sequences this port ACTUALLY recognizes right
// now. Each entry's bytes are fed through Application::feedByte(),
// one byte at a time, by executeSelectedKeyBinding() -- exactly
// as if a real user had typed that sequence live, so multi-byte
// leader sequences (^J c, ^X ^K l, etc.) naturally re-enter the same
// leaderXPending_/leaderJPending_/etc. states a live keypress would.
// M-0..M-19 (numbered keyboard-macro slots) are NOT included: this
// port has only ONE macro slot (StartMacroRecord/PlayMacro), not
// legacy's 20 independently-bound ones, so there's nothing per-slot
// to list. Arrow/PageUp/PageDown special keys are omitted too --
// every one of them already has a plain-byte alias in this table
// (j/k, ^N/^P, Space/^F/^V, b/^B), so nothing is unreachable by
// leaving KeyInput::SpecialKey-only entries out.
struct KeyTableEntry {
  const char *keyLabel;
  const char *description;
  std::vector<int> bytes;
};

const KeyTableEntry kKeyTable[] = {
    // Cursor / paging / misc (no leader)
    {"j", "Move window down", {'j'}},
    {"k", "Move window up", {'k'}},
    {"^N", "Move cursor down", {0x0e}},
    {"^P", "Move cursor up", {0x10}},
    {"Space", "Page down", {' '}},
    {"^F", "Page down", {0x06}},
    {"^V", "Page down", {0x16}},
    {"b", "Page up", {'b'}},
    {"^B", "Page up", {0x02}},
    {"Enter", "Open selected entry", {kCarriageReturn}},
    {"^G", "Back / cancel", {kControlG}},
    {"^S", "FGREP", {kControlS}},
    {"^Space", "Toggle mark (CP/MV/RM/ARC)", {0x80}},
    {"^R", "HISTORY (directory)", {0x12}},
    {"^L", "Redraw screen", {0x0c}},
    {"^T", "TCB banner", {0x14}},
    {"q", "Exit tcb (pager)", {'q'}},
    // ESC-leader
    {"ESC s", "SHELL", {kEscape, 's'}},
    {"ESC <", "Go to top", {kEscape, '<'}},
    {"ESC >", "Go to bottom", {kEscape, '>'}},
    {"ESC ^M", "Read as is", {kEscape, kCarriageReturn}},
    // ^J-leader
    {"^J ^S", "EGREP", {kLeaderJ, kControlS}},
    {"^J c", "CUT (fixed string, buffer)", {kLeaderJ, 'c'}},
    {"^J ^C", "CUT (regex, buffer)", {kLeaderJ, kControlC}},
    {"^J b", "CUT (fixed string, file)", {kLeaderJ, 'b'}},
    {"^J ^B", "CUT (regex, file)", {kLeaderJ, 0x02}},
    {"^J m", "MAN", {kLeaderJ, 'm'}},
    {"^J p", "PROGRAM", {kLeaderJ, 'p'}},
    {"^J l", "LOCATE", {kLeaderJ, 'l'}},
    {"^J d", "DU", {kLeaderJ, 'd'}},
    {"^J ^R", "HISTORY (SHELL commands)", {kLeaderJ, 0x12}},
    {"^J e", "EDIT", {kLeaderJ, 'e'}},
    {"^J ^H", "HELP", {kLeaderJ, 0x08}},
    {"^J .", "SHELL: cd <directory>", {kLeaderJ, '.'}},
    {"^J ,", "SHELL: ls -l", {kLeaderJ, ','}},
    {"^J w", "SHELL: insert access-name", {kLeaderJ, 'w'}},
    {"^J ^W", "SHELL: insert buffer content", {kLeaderJ, 0x17}},
    // ^X-leader
    {"^X c", "CP", {kLeaderX, 'c'}},
    {"^X m", "MV", {kLeaderX, 'm'}},
    {"^X r", "RM", {kLeaderX, 'r'}},
    {"^X f", "FILE: input file name", {kLeaderX, 'f'}},
    {"^X d", "DIR: input directory name", {kLeaderX, 'd'}},
    {"^X ^F", "FILE: select file name", {kLeaderX, 0x06}},
    {"^X ^D", "DIR: select directory name", {kLeaderX, 0x04}},
    {"^X ^C", "EXIT", {kLeaderX, kControlC}},
    {"^X ^W", "Save buffer to file", {kLeaderX, 0x17}},
    {"^X ^Y", "Mark all (CP/MV/RM/ARC)", {kLeaderX, 0x19}},
    {"^X ^N", "Clear all marks (CP/MV/RM/ARC)", {kLeaderX, 0x0e}},
    {"^X (", "Begin macro recording", {kLeaderX, '('}},
    {"^X )", "End macro recording", {kLeaderX, ')'}},
    {"^X e", "Execute recorded macro", {kLeaderX, 'e'}},
    {"^X s", "Restart SHELL", {kLeaderX, 's'}},
    {"^X a", "ARC: expand archive", {kLeaderX, 'a'}},
    // ^X^K-leader
    {"^X ^K c", "CP with SHELL window", {kLeaderX, 0x0b, 'c'}},
    {"^X ^K m", "MV with SHELL window", {kLeaderX, 0x0b, 'm'}},
    {"^X ^K r", "RM with SHELL window", {kLeaderX, 0x0b, 'r'}},
    {"^X ^K a", "ARC: expand with SHELL window", {kLeaderX, 0x0b, 'a'}},
    {"^X ^K e", "ERROR", {kLeaderX, 0x0b, 'e'}},
    {"^X ^K l", "LOG: SHELL output", {kLeaderX, 0x0b, 'l'}},
    // ^O-leader
    {"^O h", "Switch scroll mode", {kLeaderO, 'h'}},
    {"^O l", "Switch ls output format", {kLeaderO, 'l'}},
    {"^O a", "Switch charset (ASCII/EUC/UTF-8)", {kLeaderO, 'a'}},
    {"^O ^K", "Go to parent directory", {kLeaderO, 0x0b}},
    {"^O ^M", "Read per line", {kLeaderO, kCarriageReturn}},
    {"^O g", "Toggle GUI mode (this port's own addition, not a usilicrc K-? binding)", {kLeaderO, 'g'}},
};

// Ported from menu.c's init_cut2() call for M_help/T_help -- the
// fixed section-header pattern HELP's own CUT split always uses (see
// class comment). Legacy's OWN pattern (HELP.jp 2.4's own worked
// example: "^  [0-9]^H[0-9]+\.") is written for RAW, unprocessed
// bytes -- HELP.jp/HELP's section numbers are nroff-style overstrike-
// bold (each character doubled as "X\bX", see PipeBuffer's own
// A_md/A_md2 handling), so legacy's pattern really just anchors on
// "two leading spaces, one bold-doubled digit, then a period" and
// relies on the SAME literal backspace still being present in the
// buffer it matches against. This port's own wrapHelpText() already
// runs the raw text through PipeBuffer BEFORE cutMode_.split() ever
// sees it, which strips every overstrike pair down to a single
// (still just plain, non-doubled) character -- so matching against
// literal "\bH" or double-digit-in-a-narrow-range bytes can never
// hit anything in THIS port's own already-clean helpLines. Found via
// a real user report (2026-08-01: "HELPのCUTでマクロ機能を試そうと
// したが...効かない") after a PTY test showed HELP loading as a
// single 1-line, unsplit CUT segment ("1/1 END") instead of a proper
// section-by-section table of contents. The previous
// "^  [1-6][1-6]+\." here (two ADJACENT digits, both artificially
// restricted to 1-6) never matched anything either, on raw OR
// stripped text -- confirmed by generating a stripped-text dump of
// the real doc/HELP.jp and checking headers span "1." (bare chapter
// titles) through "2.18." (two-digit subsections) and "5.3.": no
// header is two adjacent digits, and several use digits outside
// 1-6 (e.g. "2.18.", "2.9."). "^  [0-9]+\." (one-or-more digits then
// a period, against the CLEAN de-overstruck text) matches every one
// of them -- it doesn't need to consume a header's OWN trailing
// ".M." suffix (e.g. matching just "2." out of "2.4.") since CUT
// split only needs the match to anchor where each new section
// starts, not span the whole header.
const char *const kHelpPattern = "^  [0-9]+\\.";
}  // namespace

Application::Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
                          Charset::Encoding encoding, int width, ShellOptions shellOptions,
                          std::string homeDirectory)
    : display_(display),
      pathResolver_(homeDirectory),
      wFilePath_(homeDirectory + "/.usilic/w.usilic"),
      errorLogPath_(homeDirectory + "/.usilic/error"),
      logFilePath_(homeDirectory + "/.usilic/shell"),
      usilicrcPath_(homeDirectory + "/.usilic/usilicrc"),
      historyFilePath_(homeDirectory + "/.usilic/history"),
      encoding_(encoding),
      configuration_(configuration),
      history_(history),
      directoryView_(display),
      fileView_(display),
      grepView_(display),
      cutView_(display),
      cutContentView_(display),
      manView_(display),
      locateView_(display),
      archiveView_(display),
      archiveContentView_(display),
      duView_(display),
      listBrowserView_(display),
      keyView_(display),
      promptEditor_(display, encoding),
      directoryMode_(directoryView_, modeStack_, history, configuration),
      fileMode_(fileView_, history, encoding, width),
      grepMode_(grepView_, configuration),
      cutMode_(cutView_, cutModeStack_, configuration),
      manMode_(manView_, history, configuration, encoding, width),
      programMode_(keyInput, history),
      locateMode_(locateView_, configuration),
      archiveMode_(archiveView_, history, configuration),
      shell_(display, [&] {
        shellOptions.rows = display.rows();
        shellOptions.columns = display.columns();
        shellOptions.encoding = encoding;
        return shellOptions;
      }()),
      cpMvRmMode_(configuration, keyInput, &errorLogPath_),
      duMode_(duView_, history, configuration),
      listBrowserMode_(listBrowserView_),
      editMode_(keyInput, configuration),
      execMode_(keyInput) {
  layoutViews();
  // K-65 (^I): real filesystem-backed completion for Shell's own
  // command line, resolved relative to whatever directory Shell last
  // reported itself in (its own prompt-parsing tracks this already --
  // see Shell::currentDirectory()'s own comment). Previously left
  // unset entirely, meaning Tab silently did nothing in Shell mode
  // despite Shell::onTab() otherwise being fully implemented and
  // tested against a synthetic provider (see test_shell.cpp).
  shell_.setCompletionProvider(
      [this](const std::string &partial) { return completeFilesystemPath(partial, shell_.currentDirectory()); });
  // K-49 (^R)/^J^R's own RingCategory::ShellCommand history, plus this
  // port's new ^P/^N command recall (see feedShellByte()'s own
  // comment). Shell::submitLine() has always called historyRecorder_
  // (if set) after successfully running a typed command -- but
  // nothing here ever SET it, discovered while building ^P/^N
  // (2026-08-02): this category was silently empty forever, meaning
  // ^J^R's own picker never had anything to show either despite being
  // fully wired otherwise -- the exact "wired but never connected"
  // pattern found repeatedly throughout this project (see
  // setCompletionProvider()'s own history just above).
  shell_.setHistoryRecorder(
      [this](const std::string &line) { history_.pushRecent(RingCategory::ShellCommand, line); });
  // Mode-line fields, ported from disp_shell()'s own
  // disp_window(..., T_shell, name="SHELL", prev, arg, ...) call
  // (shell.c): "SHELL" fixed on the left; on the right, the trailing
  // field is "(program) shellReturnName_" -- "(bash)"/"(zsh)" (see
  // Shell::programName()'s own comment, ported from disp_shell()'s
  // own sprintf("%c(%s)", K_z, chp->argv[0])) followed by
  // shellReturnName_ (see its own comment -- the previous mode's own
  // name, snapshotted by ensureShellActive()/shellRestart()). A real
  // user report (2026-08-02, comparing side by side against the
  // legacy build) found the "(program)" piece missing entirely from
  // this port's own SHELL mode-line. A live callback (like
  // DirectoryMode's own provider) since both pieces can change across
  // separate Shell entries/restarts within the same session.
  shell_.scrollback().setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    std::string trailing = "(" + shell_.programName() + ")";
    if (!shellReturnName_.empty()) trailing += " " + shellReturnName_;
    return {"SHELL", trailing, /*fillTrailingToEdge=*/false, /*rightWatermark=*/""};
  });
  // Mode-line fields for KEY (^H): "KEY" fixed on the left, and
  // whichever mode KEY was invoked from's own name (previousModeName(),
  // the SAME helper Shell's own trailing field above uses) as the
  // trailing field on the right -- matching a real legacy screenshot
  // (2026-08-02 user report) that showed e.g. "KEY .../HELP.rej" after
  // invoking KEY while viewing that file. A live callback since
  // keyReturnMode_ changes across separate KEY entries.
  keyView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    return {"KEY", previousModeName(keyReturnMode_), /*fillTrailingToEdge=*/false, /*rightWatermark=*/""};
  });
  // Mode-line fields for the LIST-based history pickers (K-45..K-49,
  // ^X^F/^X^D/^R/etc): the category itself ("FILE"/"DIR"/"HISTORY"/
  // etc, matching legacy's own " FILE_HISTORY"-style naming,
  // RingCategory's own comments) as the LEFT-aligned name field, and
  // the highlighted item's own string as the trailing field -- a real
  // user report (2026-08-02: "^X^F（FILE）が置き換わってない") found no
  // indication anywhere of WHICH history category was being browsed,
  // which this still fixes, but the initial fix (2026-08-02) put the
  // two fields backwards AND wrapped the category in parentheses (the
  // parens are reserved for the trailing PROGRAM-name field elsewhere,
  // e.g. DirectoryMode's own "(ls)" -- a real side-by-side screenshot
  // comparison against legacy, 2026-08-03, showed the category name
  // belongs unparenthesized at the LEFT edge, e.g. "FILE 1/5 END
  // /path/to/file", not "(FILE)" trailing after the path). A live
  // callback since both cursorIndex and listBrowserCategory_ change
  // across separate entries.
  listBrowserView_.setModeLineFieldsProvider([this](int cursorIndex) -> ContentView::ModeLineFields {
    const auto &items = listBrowserMode_.items();
    if (cursorIndex < 0 || cursorIndex >= static_cast<int>(items.size())) return {};
    return {ringCategoryLabel(listBrowserCategory_), items[cursorIndex], /*fillTrailingToEdge=*/false,
            /*rightWatermark=*/""};
  });
}

std::string Application::ringCategoryLabel(RingCategory category) const {
  switch (category) {
    case RingCategory::ShellCommand:
      return "HISTORY";
    case RingCategory::RecentFile:
      return "FILE";
    case RingCategory::RecentDirectory:
      return "DIR";
    case RingCategory::RecentManPage:
      return "MAN";
    case RingCategory::RecentLocateQuery:
      return "LOCATE";
    case RingCategory::RecentFgrepPattern:
      return "FGREP";
    case RingCategory::RecentEgrepPattern:
      return "EGREP";
  }
  return "";
}

// Shared by the constructor and handleResize(): sizes every owned
// ContentView's viewport and the prompt field off display_'s CURRENT
// geometry. The bottom row is reserved for prompt text entry (search/
// cut/man/locate/du patterns) -- legacy's equivalent is the mode-line
// row disp_window() left alone for exactly this (see ContentView's
// own class comment on why that row's rendering isn't ContentView's
// job either).
void Application::layoutViews() {
  int contentHeight = std::max(1, display_.rows() - 1);
  directoryView_.setViewport(0, contentHeight);
  fileView_.setViewport(0, contentHeight);
  grepView_.setViewport(0, contentHeight);
  cutView_.setViewport(0, contentHeight);
  cutContentView_.setViewport(0, contentHeight);
  manView_.setViewport(0, contentHeight);
  locateView_.setViewport(0, contentHeight);
  archiveView_.setViewport(0, contentHeight);
  archiveContentView_.setViewport(0, contentHeight);
  duView_.setViewport(0, contentHeight);
  listBrowserView_.setViewport(0, contentHeight);
  keyView_.setViewport(0, contentHeight);
  promptEditor_.setField(display_.rows() - 1, 0, std::max(1, display_.columns()));
}

bool Application::start(const std::string &directory, bool consultHistory) {
  currentMode_ = ApplicationMode::Directory;
  return directoryMode_.enterDirectory(directory, consultHistory);
}

// Ported from get_m()'s switch(m), cross-checked directly against
// usilicrc's own K-? default bindings (2026-07-31 correction -- an
// earlier version of this comment claimed 'j'/'k' aliased ^N/^P into
// the same M_dn/M_up cursor movement scroll.c's move_row() handles;
// usilicrc actually documents them as two DELIBERATELY DIFFERENT
// commands: K-8/K-9 "Move window one line down/up" for 'j'/'k' vs.
// K-10/K-11 "Move cursor one line down/up" for ^N/^P -- confirmed by
// grepping the legacy C sources: 'j'/'k' don't appear as literal key
// bindings anywhere in str.c/key.c/mode.c at all, only through
// usilicrc's configurable key table, and under a name ("move window")
// that doesn't match move_row()'s cursor-relative behavior). Left/
// Right-arrow==Up/Down is still a real legacy quirk (reads oddly for
// a single-column list, but is what move_row()'s switch does) and IS
// correctly ported below, unrelated to the j/k correction. M_forw/
// M_back/M_kforw/M_kback (space/b/^V and their arrow-adjacent
// bindings) map to m_page(); M_cr (^M only -- see class comment on
// why ^J is NOT folded in here) to m_cr(); M_bl (^G) to m_bl();
// M_fgrep (^S) starts a fgrep search prompt. ^J-leader sequences (e.g.
// ^J^S for EGREP, ^Jc/^J^C for CUT) aren't resolvable from one byte
// and are handled by feedByte() instead.
Command Application::resolveCommand(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (special) {
    switch (*special) {
      case KeyInput::SpecialKey::Up:
      case KeyInput::SpecialKey::Left:
        return Command::MoveUp;
      case KeyInput::SpecialKey::Down:
      case KeyInput::SpecialKey::Right:
        return Command::MoveDown;
      case KeyInput::SpecialKey::PageUp:
        return Command::PageUp;
      case KeyInput::SpecialKey::PageDown:
        return Command::PageDown;
    }
    return Command::None;
  }

  switch (byte) {
    case 'j':
      return Command::ScrollLineDown;
    case 'k':
      return Command::ScrollLineUp;
    case 0x0e:  // ^N
      return Command::MoveDown;
    case 0x10:  // ^P
      return Command::MoveUp;
    case ' ':
    case 0x06:  // ^F (M_forw2, usilicrc's own alias for Space/M_forw)
    case 'b':
    case 0x02:  // ^B (M_back2, alias for b/M_back)
    case 0x16:  // ^V
      return (byte == 'b' || byte == 0x02) ? Command::PageUp : Command::PageDown;
    case kCarriageReturn:
      return Command::Enter;
    case kControlG:
      return Command::Back;
    case ';':
      return Command::JumpNext;
    case ':':
      return Command::JumpPrevious;
    case kControlS:
      return Command::StartFgrep;
    case 0x08:  // ^H (M_key, K-2, "KEY") -- see startKey()'s own comment
      return Command::StartKey;
    case 0x80:  // ^Space (M_mark) -- KeyInput reports a literal NUL/Ctrl-Space as 0x80, see its class comment
      return Command::ToggleMark;
    case 0x12:  // ^R (M_hist)
      return Command::StartDirectoryHistory;
    case 0x0c:  // ^L (M_clear)
      return Command::Redraw;
    case 0x14:  // ^T (M_tcb, K-0, "TCB")
      return Command::ShowTcbBanner;
    case 'q':  // M_quit -- usilicrc scopes this to pager contexts; see handleCommand()'s own gate
      return Command::QuitPager;
    default:
      return Command::None;
  }
}

bool Application::dispatchByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (promptKind_ != PromptKind::None) return feedPromptByte(byte);
  if (currentMode_ == ApplicationMode::Shell) return feedShellByte(byte, special);

  if (escPending_ || leaderJPending_ || leaderXPending_ || leaderXKPending_ || leaderOPending_ ||
      (!special && (byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape))) {
    return dispatchLeaderByte(byte, special);
  }

  return handleCommand(resolveCommand(byte, special));
}

// See this function's own declaration comment (application.hpp).
// Unchanged from dispatchByte()'s own former body -- only the callers
// changed (dispatchByte() itself, plus feedShellByte() now).
bool Application::dispatchLeaderByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (escPending_) {
    escPending_ = false;
    if (special) return false;
    switch (byte) {
      case 's':  // ESC s : SHELL
        return handleCommand(Command::StartShell);
      case '<':  // ESC < : go to beginning of buffer (M_head)
        return handleCommand(Command::GoToTop);
      case '>':  // ESC > : go to end of buffer (M_tail)
        return handleCommand(Command::GoToBottom);
      case kCarriageReturn:  // ESC ^M : "Read as is" (K-24, M_code)
        return handleCommand(Command::Enter);
      default:
        return false;  // unrecognized ESC-sequence in this slice
    }
  }

  if (leaderJPending_) {
    leaderJPending_ = false;
    if (special) return false;
    switch (byte) {
      case kControlS:  // ^J ^S : EGREP
        return handleCommand(Command::StartEgrep);
      case 'c':  // ^J c : CUT (fixed string, buffer)
        return handleCommand(Command::StartCutFixed);
      case kControlC:  // ^J ^C : CUT (regex, buffer)
        return handleCommand(Command::StartCutRegex);
      case 'b':  // ^J b : CUT (fixed string, regular file)
        return handleCommand(Command::StartCutFileFixed);
      case 0x02:  // ^J ^B : CUT (regex, regular file)
        return handleCommand(Command::StartCutFileRegex);
      case 'm':              // ^J m : MAN
      case kCarriageReturn:  // ^J ^M : MAN (the second byte here is ^M, unrelated to a bare Enter)
        return handleCommand(Command::StartMan);
      case 'p':      // ^J p : PROGRAM
      case 0x10:     // ^J ^P : PROGRAM (the second byte here is ^P, unrelated to plain ^P/MoveUp)
        return handleCommand(Command::StartProgram);
      case 'l':       // ^J l : LOCATE
      case 0x0c:      // ^J ^L : LOCATE
        return handleCommand(Command::StartLocate);
      case 'd':       // ^J d : DU
      case 0x04:      // ^J ^D : DU
        return handleCommand(Command::StartDu);
      case 0x12:      // ^J ^R : command history (M_cmd)
        return handleCommand(Command::StartCommandHistory);
      case 'e':  // ^J e : EDIT
        return handleCommand(Command::StartEdit);
      case 0x08:  // ^J ^H : HELP
        return handleCommand(Command::StartHelp);
      case '.':  // ^J . : SHELL, change current directory
        return handleCommand(Command::ShellChangeDirectory);
      case ',':  // ^J , : SHELL, execute `ls -l`
        return handleCommand(Command::ShellRunLsL);
      case 'w':  // ^J w : SHELL, insert access-name
        return handleCommand(Command::ShellInsertName);
      case 0x17:  // ^J ^W : SHELL, insert buffer content
        return handleCommand(Command::ShellInsertBufferContent);
      default:
        return false;  // unrecognized ^J-sequence in this slice
    }
  }

  if (leaderXPending_) {
    leaderXPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'c':  // ^X c : CP
        return handleCommand(Command::StartCopy);
      case 'm':  // ^X m : MV
        return handleCommand(Command::StartMove);
      case 'r':  // ^X r : RM
        return handleCommand(Command::StartRemove);
      case 'f':  // ^X f : FILE (input file name)
        return handleCommand(Command::StartFileInput);
      case 'd':  // ^X d : DIR (input directory name)
        return handleCommand(Command::StartDirectoryInput);
      case 0x06:  // ^X ^F : FILE (select file name, K-46)
        return handleCommand(Command::StartFileHistory);
      case 0x04:  // ^X ^D : DIR (select directory name, K-48) -- reuses ^R's own StartDirectoryHistory
        return handleCommand(Command::StartDirectoryHistory);
      case kControlC:  // ^X ^C : exit
        return handleCommand(Command::StartExit);
      case 0x17:  // ^X ^W : save buffer to wFilePath_
        return handleCommand(Command::SaveBufferToFile);
      case 0x19:  // ^X ^Y : mark all (CP, MV, RM, ARC)
        return handleCommand(Command::MarkAll);
      case 0x0e:  // ^X ^N : clear all (CP, MV, RM, ARC)
        return handleCommand(Command::ClearAllMarks);
      case '(':  // ^X ( : begin macro recording
        return handleCommand(Command::StartMacroRecord);
      case ')':  // ^X ) : end macro recording
        return handleCommand(Command::StopMacroRecord);
      case 'e':  // ^X e : execute recorded macro
        return handleCommand(Command::PlayMacro);
      case 0x0b:  // ^X ^K : leader for CP/MV/RM's "with SHELL window" variants (F-2 in usilicrc's own terms)
        leaderXKPending_ = true;
        return true;  // sequence in progress, not a no-op
      case 's':  // ^X s : restart SHELL (K-60)
        return handleCommand(Command::RestartShell);
      case 'a':  // ^X a : ARC expand, without SHELL window (K-58)
        return handleCommand(Command::StartArchiveExtract);
      default:
        return false;  // unrecognized ^X-sequence in this slice (^X^Kd is still future work)
    }
  }

  if (leaderXKPending_) {
    leaderXKPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'c':  // ^X ^K c : CP with SHELL window (K-51)
        return handleCommand(Command::StartCopyInteractive);
      case 'm':  // ^X ^K m : MV with SHELL window (K-53)
        return handleCommand(Command::StartMoveInteractive);
      case 'r':  // ^X ^K r : RM with SHELL window (K-55)
        return handleCommand(Command::StartRemoveInteractive);
      case 'a':  // ^X ^K a : ARC expand, with SHELL window (K-57)
        return handleCommand(Command::StartArchiveExtractInteractive);
      case 'e':  // ^X ^K e : ERROR (K-67)
        return handleCommand(Command::ShowErrorLog);
      case 'l':  // ^X ^K l : LOG (K-68)
        return handleCommand(Command::ShowShellLog);
      default:
        return false;  // ^X^Kd (kanji switch) is still future work
    }
  }

  if (leaderOPending_) {
    leaderOPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'g':  // ^O g : toggle GUI mode (this port's own addition, not a usilicrc K-? binding)
        return handleCommand(Command::ToggleGuiMode);
      case 'h':  // ^O h : switch scroll mode (K-25)
        return handleCommand(Command::ToggleScrollMode);
      case 'l':  // ^O l : switch format of `ls' output (K-26)
        return handleCommand(Command::ToggleLsMode);
      case 'a':  // ^O a : switch ascii mode (K-27)
        return handleCommand(Command::CycleCharset);
      case 0x0b:  // ^O ^K : go to parent directory (K-19)
        return handleCommand(Command::GoToParentDirectory);
      case kCarriageReturn:  // ^O ^M : "Read per line" (K-23, M_fp)
        return handleCommand(Command::Enter);
      default:
        return false;  // unrecognized ^O-sequence in this slice
    }
  }

  if (!special && byte == kLeaderJ) {
    leaderJPending_ = true;
    return true;  // sequence in progress, not a no-op
  }
  if (!special && byte == kLeaderX) {
    leaderXPending_ = true;
    return true;  // sequence in progress, not a no-op
  }
  if (!special && byte == kLeaderO) {
    leaderOPending_ = true;
    return true;  // sequence in progress, not a no-op
  }
  if (!special && byte == kEscape) {
    escPending_ = true;
    return true;  // sequence in progress, not a no-op
  }

  return false;  // unreachable given dispatchByte()'s/feedShellByte()'s own guard, kept total for safety
}

// Wraps dispatchByte() (the real logic, see its own body) with p_km
// recording bookkeeping -- see class comment's own p_km section for
// why this holds a leaderX sequence's leading byte back in
// pendingLeaderXByte_ rather than recording it immediately.
bool Application::feedByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  bool wasLeaderXPending = leaderXPending_;
  bool wasRecording = recordingMacro_;

  bool result = dispatchByte(byte, special);

  if (wasRecording) {
    if (wasLeaderXPending) {
      if (recordingMacro_) {
        // still recording -- this byte did NOT resolve to
        // StartMacroRecord/StopMacroRecord/PlayMacro, so commit both
        // the held-back leading ^X byte and this one.
        if (pendingLeaderXByte_) recordedMacro_.push_back(*pendingLeaderXByte_);
        recordedMacro_.push_back({byte, special});
      }
      // else: recording just turned off (StopMacroRecord resolved) --
      // discard both bytes, they're the control sequence itself.
      pendingLeaderXByte_.reset();
    } else if (!special && byte == kLeaderX) {
      pendingLeaderXByte_ = RecordedStep{byte, special};  // hold back until the second byte resolves
    } else {
      recordedMacro_.push_back({byte, special});
    }
  }

  return result;
}

bool Application::startMacroRecording() {
  recordingMacro_ = true;
  recordedMacro_.clear();
  pendingLeaderXByte_.reset();
  return true;
}

bool Application::stopMacroRecording() {
  if (!recordingMacro_) return false;
  recordingMacro_ = false;
  return true;
}

// Replays each recorded byte through dispatchByte() directly (NOT
// feedByte()), so a macro played back from inside an active recording
// session is never accidentally self-recorded -- see class comment.
bool Application::playRecordedMacro() {
  if (recordedMacro_.empty()) return false;
  for (const auto &step : recordedMacro_) dispatchByte(step.byte, step.special);
  return true;
}

bool Application::isRecordingMacro() const { return recordingMacro_; }
std::size_t Application::recordedMacroLength() const { return recordedMacro_.size(); }

// See class comment for the scope of this routing (and the KeyInput/
// escape-sequence gap in wiring ^[s to a real terminal). Left/Right
// move the command-line cursor; Up/Down/PageUp/PageDown (scrollback
// paging / command history) are deliberately not handled here yet.
bool Application::feedShellByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  // Global leader-key access from within Shell mode: ^J/^X/^X^K/^O
  // (NOT bare ESC, unlike dispatchByte()'s own top-level check -- a
  // real interactive shell (bash/zsh) needs raw ESC constantly for
  // its own line editor, Meta-key bindings, vi-mode, etc, so
  // intercepting it here would break real shell usage far more than
  // ^J/^X/^O -- which essentially never come up as literal typed
  // shell input -- ever would). A real user report (2026-08-02,
  // comparing against the mature legacy build side by side) found
  // ^X^F/^X^C etc. work directly from within SHELL there, which
  // silently did nothing here before this existed: currentMode_==
  // Shell routed EVERY byte straight to this function with no
  // leader-key access at all.
  bool shellLeaderPending = leaderJPending_ || leaderXPending_ || leaderXKPending_ || leaderOPending_;
  if (!special && (shellLeaderPending || byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO)) {
    if (shellLeaderPending) {
      // Completing a sequence -- switch back to whichever mode SHELL
      // was entered from BEFORE dispatching, exactly as if the user
      // had pressed ^G first (matching legacy, and matching KEY
      // mode's own executeSelectedKeyBinding() precedent): several
      // downstream commands (startListBrowser() for ^X^F, etc.) have
      // their own currentMode_==Directory-style guards that would
      // otherwise reject them outright while still Shell. An
      // unrecognized sequence (a genuine typo) still leaves Shell as
      // a side effect -- accepted as a rare, minor edge case rather
      // than adding rollback machinery for it.
      currentMode_ = shellReturnMode_;
      activeView().redraw();
    }
    return dispatchLeaderByte(byte, special);
  }

  if (special) {
    switch (*special) {
      case KeyInput::SpecialKey::Left:
        shell_.commandLine().moveLeft();
        shell_.commandLine().redraw();
        return true;
      case KeyInput::SpecialKey::Right:
        shell_.commandLine().moveRight();
        shell_.commandLine().redraw();
        return true;
      default:
        return false;
    }
  }

  if (byte == kControlG) return returnFromShell();
  // ^P/^N and Enter-while-browsing below are ported from legacy's
  // shell.c M_kup/M_kdn: a cursor moves directly through scrollback_'s
  // OWN lines_ (the same unified prompt+output history real content
  // lives in, not a separate array) -- see Shell::freezePromptLine()'s
  // own comment for the data-model half of this. A real user report
  // (2026-08-02) pointed out that an earlier version of this (a
  // synthetic RingCategory::ShellCommand-backed index, entirely
  // disconnected from what's actually on screen) was "vibe coding":
  // reaching for whatever was easiest to bolt on on top of the
  // existing split scrollback_/commandLine_ design instead of reading
  // legacy's actual mechanism first. ContentView::moveUp()/moveDown()
  // already implement the exact cursor-move-with-boundary-bell
  // semantics needed here (used elsewhere for plain list navigation),
  // so ^P/^N end up needing no bespoke state of their own at all.
  if (byte == 0x10) {  // ^P
    shell_.scrollback().moveUp();
    shell_.redrawPromptRow();  // moveUp() can blank commandLineRow() -- see its own comment
    moveDisplayCursorToShellHistoryLine();
    return true;
  }
  if (byte == 0x0e) {  // ^N
    shell_.scrollback().moveDown();
    shell_.redrawPromptRow();
    moveDisplayCursorToShellHistoryLine();
    return true;
  }
  // !empty() guard: with no lines at all yet, lines().size()-1
  // underflows to -1, which cursorIndex()'s default of 0 never equals
  // -- falsely reading as "browsing" and indexing lines()[0] on an
  // empty vector below.
  bool browsingHistory = !shell_.scrollback().lines().empty() &&
                          shell_.scrollback().cursorIndex() != static_cast<int>(shell_.scrollback().lines().size()) - 1;
  if (byte == kCarriageReturn) {
    if (browsingHistory) {
      // Ported from shell.c's own '\r' case: re-run the highlighted
      // line's command if it's a real, previously-submitted prompt
      // line (Line::shellCommand set by freezePromptLine()); anything
      // else (plain output text) is legacy's own "not a prompt line"
      // fallthrough -- simplified here to just snap back rather than
      // also replicating legacy's own attempt to submit raw output
      // text as a command (an edge case legacy itself handles
      // inconsistently, not worth reproducing exactly).
      const auto &line = shell_.scrollback().lines()[shell_.scrollback().cursorIndex()];
      if (line.shellCommand) {
        shell_.commandLine().setText(*line.shellCommand);
        shell_.submitLine();
      } else {
        shell_.scrollback().goToBottom();
        shell_.redrawPromptRow();  // see the browsingHistory guard below's own comment
      }
      return true;
    }
    shell_.submitLine();
    return true;
  }
  // Any content-modifying key while browsing snaps the cursor back to
  // the live prompt FIRST, matching shell.c's own default case
  // ("if (pos != M_tail) { write_comm(pv[1], M_tail, ...); ... }"
  // before falling through to handle the actual keystroke). ^C is the
  // only byte past this point that does NOT touch commandLine_ (it
  // just forwards the interrupt to the child), matching legacy's own
  // K_c case, which has no such guard either. redrawPromptRow() (not
  // just commandLine_.redraw(), which Tab/backspace/insertByte below
  // already call on their own) is needed here specifically because
  // goToBottom() itself just ran a full scrollback_ redraw that can
  // blank the prompt PREFIX text -- see redrawPromptRow()'s own
  // comment for the full story (a real user report, 2026-08-02).
  if (browsingHistory && byte != kControlC) {
    shell_.scrollback().goToBottom();
    shell_.redrawPromptRow();
  }
  if (byte == 0x09) {  // Tab
    shell_.onTab();
    return true;
  }
  if (byte == 0x7f || byte == 0x08) {  // DEL / ^H
    shell_.commandLine().backspace();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == kControlC) {
    shell_.sendControlByte(static_cast<unsigned char>(byte));
    return true;
  }
  shell_.commandLine().insertByte(static_cast<unsigned char>(byte));
  shell_.commandLine().redraw();
  return true;
}

// ^P/^N's own plain-text-cursor replacement for the whole-line
// highlight scrollback_.setCursorHighlightEnabled(false) turns off
// (see Shell's own constructor comment for why: legacy's stand_str()
// never draws that highlight for SHELL mode at all). A no-op while on
// the live prompt line itself (cursorDisplayRow() there coincides with
// redrawPromptRow()'s own commandLine_.redraw() cursor position, which
// already ran immediately before this is called at both ^P/^N call
// sites, so there's nothing to override) -- only moves the display's
// visible cursor when actually browsing scrollback_ history, so the
// live-prompt case keeps editing normally. -1 (scrolled out of view,
// shouldn't be reachable given moveUp()/moveDown() always keep
// cursorIndex_ within the viewport, but checked defensively) is simply
// skipped rather than showing the cursor somewhere meaningless.
//
// setVisibleCursor(), not moveCursor() (2026-08-04 revision -- see
// Display::setVisibleCursor()'s own comment): an earlier version used
// plain moveCursor(), which worked for -c (nothing else touches the
// screen between this call and the terminal going idle again) but not
// -g, where runGuiPhase()'s own steady ~60Hz polling could re-run a
// full, unrelated ContentView::redraw() within the SAME frame this ran
// in, silently dragging the visible cursor back to wherever that
// redraw()'s OWN incidental moveCursor() calls last landed (a real
// user report: "-gのSHELLでは...一瞬出て消えるパターン").
void Application::moveDisplayCursorToShellHistoryLine() {
  const auto &lines = shell_.scrollback().lines();
  bool browsingHistory =
      !lines.empty() && shell_.scrollback().cursorIndex() != static_cast<int>(lines.size()) - 1;
  if (!browsingHistory) return;
  int row = shell_.scrollback().cursorDisplayRow();
  if (row >= 0) display_.setVisibleCursor(row, 0);
}

ContentView &Application::activeView() {
  switch (currentMode_) {
    case ApplicationMode::File:
      return fileView_;
    case ApplicationMode::Grep:
      return grepView_;
    case ApplicationMode::Cut:
      return cutView_;
    case ApplicationMode::CutContent:
      return cutContentView_;
    case ApplicationMode::Man:
      return manView_;
    case ApplicationMode::Locate:
      return locateView_;
    case ApplicationMode::Archive:
      return archiveView_;
    case ApplicationMode::ArchiveContent:
      return archiveContentView_;
    case ApplicationMode::Shell:
      return shell_.scrollback();
    case ApplicationMode::Du:
      return duView_;
    case ApplicationMode::ListBrowser:
      return listBrowserView_;
    case ApplicationMode::Key:
      return keyView_;
    case ApplicationMode::Directory:
    default:
      return directoryView_;
  }
}

bool Application::handleCommand(Command command) {
  switch (command) {
    case Command::MoveUp:
      activeView().moveUp();
      activeView().syncVisibleCursor();
      return true;
    case Command::MoveDown:
      activeView().moveDown();
      activeView().syncVisibleCursor();
      return true;
    case Command::ScrollLineUp:
      activeView().scrollLineUp();
      activeView().syncVisibleCursor();
      return true;
    case Command::ScrollLineDown:
      activeView().scrollLineDown();
      activeView().syncVisibleCursor();
      return true;
    case Command::PageUp:
      activeView().pageUp();
      activeView().syncVisibleCursor();
      return true;
    case Command::PageDown:
      activeView().pageDown();
      activeView().syncVisibleCursor();
      return true;
    case Command::Enter:
      if (currentMode_ == ApplicationMode::Directory) return openSelectedEntry();
      if (currentMode_ == ApplicationMode::Cut) return openSelectedSegment();
      if (currentMode_ == ApplicationMode::Archive) return openSelectedArchiveEntry();
      if (currentMode_ == ApplicationMode::Du) return openSelectedDuEntry();
      if (currentMode_ == ApplicationMode::ListBrowser) return openSelectedListEntry();
      if (currentMode_ == ApplicationMode::Key) return executeSelectedKeyBinding();
      return false;
    case Command::Back:
      if (currentMode_ == ApplicationMode::Key) return returnFromKey();
      if (currentMode_ == ApplicationMode::ListBrowser) return returnFromListBrowser();
      if (currentMode_ == ApplicationMode::Du) return returnFromDu();
      if (currentMode_ == ApplicationMode::Shell) return returnFromShell();
      if (currentMode_ == ApplicationMode::ArchiveContent) return returnFromArchiveContent();
      if (currentMode_ == ApplicationMode::Archive) return returnFromArchive();
      if (currentMode_ == ApplicationMode::Locate) return returnFromLocate();
      if (currentMode_ == ApplicationMode::Man) return returnFromMan();
      if (currentMode_ == ApplicationMode::CutContent) return returnFromCutContent();
      if (currentMode_ == ApplicationMode::Cut) {
        if (cutMode_.canGoBack()) return cutMode_.goBack();
        return returnFromCut();
      }
      if (currentMode_ == ApplicationMode::Grep) return returnFromGrep();
      if (currentMode_ == ApplicationMode::File) return returnToDirectory();
      return directoryMode_.goBack();
    // ';'/':' -- literally Back+MoveDown+Enter / Back+MoveUp+Enter,
    // confirmed directly by the user (2026-08-03) after an initial
    // wrong guess that this was a "related file" jump feature of its
    // own; see Command::JumpNext's own declaration comment. Each step
    // re-enters handleCommand() so it sees currentMode_ as it stands
    // AFTER the previous step (e.g. Back may switch File->Directory
    // before MoveDown/Enter act on the now-current Directory view) --
    // the same as if the three underlying keys had actually been typed
    // one at a time. Intermediate steps' own success/failure is
    // intentionally not checked (matching a real 3-keystroke replay,
    // which doesn't stop partway through just because one keystroke
    // was a no-op); only the final step's result is returned.
    case Command::JumpNext:
      handleCommand(Command::Back);
      handleCommand(Command::MoveDown);
      return handleCommand(Command::Enter);
    case Command::JumpPrevious:
      handleCommand(Command::Back);
      handleCommand(Command::MoveUp);
      return handleCommand(Command::Enter);
    case Command::StartFgrep:
      return startGrepPrompt(GrepSyntax::Fixed);
    case Command::StartEgrep:
      return startGrepPrompt(GrepSyntax::Regex);
    case Command::StartCutFixed:
      return startCutPrompt(GrepSyntax::Fixed);
    case Command::StartCutRegex:
      return startCutPrompt(GrepSyntax::Regex);
    case Command::StartCutFileFixed:
      return startCutFilePrompt(GrepSyntax::Fixed);
    case Command::StartCutFileRegex:
      return startCutFilePrompt(GrepSyntax::Regex);
    case Command::StartMan:
      return startManPrompt();
    case Command::StartProgram:
      return startProgramPrompt();
    case Command::StartLocate:
      return startLocatePrompt();
    case Command::StartShell:
      return startShell();
    case Command::ToggleMark:
      return toggleMark();
    case Command::StartCopy:
      return startCopyPrompt();
    case Command::StartMove:
      return startMovePrompt();
    case Command::StartRemove:
      return startRemovePrompt();
    case Command::StartCopyInteractive:
      return startCopyPrompt(/*interactive=*/true);
    case Command::StartMoveInteractive:
      return startMovePrompt(/*interactive=*/true);
    case Command::StartRemoveInteractive:
      return startRemovePrompt(/*interactive=*/true);
    case Command::SaveBufferToFile:
      return saveBufferToFile();
    case Command::ShellChangeDirectory:
      return shellChangeDirectory();
    case Command::ShellRunLsL:
      return shellRunLsL();
    case Command::ShellInsertName:
      return shellInsertName();
    case Command::ShellInsertBufferContent:
      return shellInsertBufferContent();
    case Command::GoToParentDirectory:
      if (currentMode_ != ApplicationMode::Directory) return false;
      return directoryMode_.goToParent();
    case Command::RestartShell:
      return shellRestart();
    case Command::StartArchiveExtract:
      return startArchivePrompt(/*interactive=*/false);
    case Command::StartArchiveExtractInteractive:
      return startArchivePrompt(/*interactive=*/true);
    case Command::ShowErrorLog:
      return executeShowErrorLog();
    case Command::ShowShellLog:
      return executeShowShellLog();
    case Command::ShowTcbBanner:
      return executeShowTcbBanner();
    case Command::CycleCharset:
      return cycleCharset();
    case Command::StartFileHistory:
      return startListBrowser(RingCategory::RecentFile);
    case Command::StartKey:
      return startKey();
    case Command::StartExit:
      return startExitPrompt();
    case Command::StartDu:
      return startDuPrompt();
    case Command::StartDirectoryHistory:
      return startListBrowser(RingCategory::RecentDirectory);
    case Command::StartCommandHistory:
      return startListBrowser(RingCategory::ShellCommand);
    case Command::StartEdit:
      return executeEdit();
    case Command::StartHelp:
      return startHelp();
    case Command::StartMacroRecord:
      return startMacroRecording();
    case Command::StopMacroRecord:
      return stopMacroRecording();
    case Command::PlayMacro:
      return playRecordedMacro();
    case Command::ToggleGuiMode:
      guiModeToggleRequested_ = true;
      return true;
    case Command::Redraw:
      activeView().redraw();
      activeView().syncVisibleCursor();
      return true;
    case Command::GoToTop:
      activeView().goToTop();
      activeView().syncVisibleCursor();
      return true;
    case Command::GoToBottom:
      activeView().goToBottom();
      activeView().syncVisibleCursor();
      return true;
    case Command::QuitPager:
      // usilicrc documents this as pager-only ("Exit tcb (pager)");
      // Directory mode has its own confirmed-exit path (StartExit,
      // ^X^C) and doesn't want a bare 'q' to bypass it.
      if (currentMode_ == ApplicationMode::Directory) return false;
      requestImmediateExit();
      return true;
    case Command::ToggleScrollMode: {
      bool enabled = !display_.smoothScrollEnabled();
      display_.setSmoothScrollEnabled(enabled);
      history_.setSmoothScrollEnabled(enabled);  // persisted across runs, see History's own field comment
      return true;
    }
    case Command::StartFileInput:
      return startPathInputPrompt(/*isDirectory=*/false);
    case Command::StartDirectoryInput:
      return startPathInputPrompt(/*isDirectory=*/true);
    case Command::ToggleLsMode: {
      if (currentMode_ != ApplicationMode::Directory) return false;
      bool enabled = !directoryMode_.filenameOnlyMode();
      directoryMode_.setFilenameOnlyMode(enabled);
      history_.setLsMode(enabled ? 1 : 0);  // persisted across runs, see History's own field comment
      return true;
    }
    case Command::MarkAll:
      if (currentMode_ == ApplicationMode::Archive) {
        archiveMode_.markAll();
        return true;
      }
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.markAll();
      return true;
    case Command::ClearAllMarks:
      if (currentMode_ == ApplicationMode::Archive) {
        archiveMode_.clearMarks();
        return true;
      }
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.clearMarks();
      return true;
    case Command::None:
      return false;
  }
  return false;
}

// Ported from mode.c's T_dir case reaching T_tar/T_lha/T_unzip/T_rpm/
// T_deb vs T_file: legacy dispatches on the selection's suffix before
// ever trying to view it as plain text. Here that's just "try
// ArchiveMode first, fall back to FileMode" -- see class comment for
// the one fidelity gap this simplification introduces (a real archive
// suffix whose listing command fails falls through to plain-text
// viewing instead of a specific error).
bool Application::openSelectedEntry() {
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry) return false;
  if (entry->isDirectory) return directoryMode_.openSelected();

  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;

  // EXEC (a suffix-mapped external viewer) takes priority over
  // archive detection -- see class comment on is_child()'s own
  // f.exec-checked-first precedence (though Configuration's unified
  // suffix table already makes the check order mostly moot in
  // practice). Hands off entirely and returns to Directory mode
  // unchanged, same shape as PROGRAM/EDIT.
  if (configuration_.isExec(path)) {
    if (auto command = configuration_.programForSuffix(path)) execMode_.run(*command, path);
    activeView().redraw();
    return true;
  }

  if (archiveMode_.loadArchive(path)) {
    archiveParentStack_.clear();  // a brand new top-level archive -- any earlier nested-descent chain no longer applies
    currentMode_ = ApplicationMode::Archive;
    return true;
  }
  if (!fileMode_.loadFile(path)) return false;
  currentMode_ = ApplicationMode::File;
  return true;
}

// Ported from mode.c's T_tar/etc Enter handling, PLUS new nested-
// archive support (not a port of anything -- legacy never had this,
// see descendToNestedArchive()'s own comment): if the selected entry
// is itself a recognized archive suffix, recurse into it instead of
// showing its raw bytes, the same suffix-dispatch openSelectedEntry()
// already does at the TOP level. Otherwise: extracts the selected
// entry's raw bytes and displays them as plain text lines (no
// PipeBuffer charset/overstrike processing -- ArchiveMode hands back
// raw bytes only, see class comment). False for a directory entry, a
// format that doesn't support extraction (RPM/.deb), or a failed
// extraction -- ArchiveMode::extractSelected() already distinguishes
// none of these from the others, so neither does this.
bool Application::openSelectedArchiveEntry() {
  if (const ArchiveEntry *entry = archiveMode_.selectedEntry(); entry && !entry->isDirectory) {
    ArchiveKind nestedKind = ArchiveKind::None;
    if (configuration_.programForSuffix(entry->name, &nestedKind) && nestedKind != ArchiveKind::None)
      return descendToNestedArchive(*entry);
  }

  auto content = archiveMode_.extractSelected();
  if (!content) return false;

  std::string text(content->begin(), content->end());
  std::istringstream stream(text);
  std::vector<ContentView::Line> lines;
  std::string line;
  while (std::getline(stream, line)) lines.push_back({line, std::nullopt, std::nullopt});
  archiveContentView_.setLines(std::move(lines));
  archiveContentView_.setCursorIndex(0);
  archiveContentView_.redraw();
  currentMode_ = ApplicationMode::ArchiveContent;
  return true;
}

// See the class's own declaration comment on this feature.
bool Application::descendToNestedArchive(const ArchiveEntry &entry) {
  auto content = archiveMode_.extractSelected();
  if (!content) return false;

  std::string tempPath = materializeArchiveTempFile(entry.name, *content);
  if (tempPath.empty()) return false;

  std::string parentPath = archiveMode_.currentArchive();
  archiveMode_.saveCursorPosition();
  if (!archiveMode_.loadArchive(tempPath)) return false;

  archiveParentStack_.push_back(parentPath);
  currentMode_ = ApplicationMode::Archive;
  return true;
}

std::string Application::materializeArchiveTempFile(const std::string &entryName,
                                                      const std::vector<unsigned char> &bytes) {
  char dirTemplate[] = "/tmp/usilic-arcXXXXXX";
  if (!::mkdtemp(dirTemplate)) return "";

  // entry.name may itself contain '/' (a path within the archive) --
  // only the basename matters for programForSuffix()'s own suffix
  // dispatch, and keeping the temp file directly in the fresh
  // mkdtemp()'d directory (rather than trying to recreate the
  // archive's internal directory structure) sidesteps needing to
  // mkdir -p anything here.
  std::string baseName = entryName;
  if (std::size_t slash = baseName.find_last_of('/'); slash != std::string::npos)
    baseName = baseName.substr(slash + 1);
  if (baseName.empty()) baseName = "entry";

  std::string path = std::string(dirTemplate) + "/" + baseName;
  std::ofstream out(path, std::ios::binary);
  if (!out) return "";
  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!out) return "";
  return path;
}

// Ported from mode.c's T_du Enter handling (see class comment on why
// this, unlike every other prompt-driven result screen, drills
// straight into DirectoryMode/FileMode instead of stopping at a
// no-op like GREP/LOCATE do): DuMode::loadUsage() already invokes
// `du` with an absolute path, so `du`'s own output entries are
// already absolute -- no path-joining needed here, unlike Cut/Archive
// segments/entries.
bool Application::openSelectedDuEntry() {
  const DuEntry *entry = duMode_.selectedEntry();
  if (!entry) return false;
  if (entry->isDirectory) {
    if (!directoryMode_.enterDirectory(entry->path)) return false;
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  if (!fileMode_.loadFile(entry->path)) return false;
  currentMode_ = ApplicationMode::File;
  return true;
}

// Ported from m_list()'s dispatch (see class comment for the 5 other
// categories not wired here): RecentDirectory (K-48, ^X^D/^R) drills
// into DirectoryMode, RecentFile (K-46, ^X^F) into FileMode; every
// other category (just ShellCommand in this slice) is a no-op, since
// deciding what "open" means for a shell command (pre-fill Shell's
// command line? require Shell already started?) is deliberately left
// open rather than guessed at.
bool Application::openSelectedListEntry() {
  const std::string *item = listBrowserMode_.selectedItem();
  if (!item) return false;
  if (listBrowserCategory_ == RingCategory::RecentDirectory) {
    if (!directoryMode_.enterDirectory(*item)) return false;
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  if (listBrowserCategory_ == RingCategory::RecentFile) {
    if (!fileMode_.loadFile(*item)) return false;
    currentMode_ = ApplicationMode::File;
    return true;
  }
  return false;
}

// Ported from M_mark (^Space): toggles the mark at the cursor.
// Directory-only, matching every other CP/MV/RM-related action added
// in this slice.
// K-57/K-58's own "mark specific entries to extract" (HELP.jp 2.11)
// reuses this same ^Space binding within Archive mode -- matching
// this port's established simplification of legacy's real per-mode
// marking mechanism into one shared ^Space/ToggleMark command (see
// DirectoryMode's own class comment on that choice).
bool Application::toggleMark() {
  if (currentMode_ == ApplicationMode::Archive) {
    if (!archiveMode_.selectedEntry()) return false;
    archiveMode_.toggleMark(archiveView_.cursorIndex());
    return true;
  }
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (!directoryMode_.selectedEntry()) return false;
  directoryMode_.toggleMark(directoryView_.cursorIndex());
  return true;
}

std::vector<std::string> Application::collectTargetPaths() const {
  std::vector<std::string> paths;
  std::string dir = directoryMode_.currentDirectory();
  for (const auto &entry : directoryMode_.targetEntries()) {
    std::string path = dir;
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry.name;
    paths.push_back(std::move(path));
  }
  return paths;
}

bool Application::openSelectedSegment() {
  const CutSegment *segment = cutMode_.selectedSegment();
  if (!segment) return false;

  std::vector<std::string> content = cutMode_.segmentContent(*segment);
  std::vector<ContentView::Line> lines;
  lines.reserve(content.size());
  for (auto &l : content) lines.push_back({l, std::nullopt, std::nullopt});
  cutContentView_.setLines(std::move(lines));
  cutContentView_.setCursorIndex(0);
  cutContentView_.redraw();
  currentMode_ = ApplicationMode::CutContent;
  return true;
}

bool Application::returnToDirectory() {
  fileMode_.saveCursorPosition();
  currentMode_ = ApplicationMode::Directory;
  // File mode's own view now sets a real visible cursor (see
  // ContentView::syncVisibleCursor()'s own comment) -- without this,
  // it would keep floating over the directory listing that replaces
  // it, since directoryView_.redraw() deliberately never touches
  // Display's visible-cursor state itself (same reasoning as
  // Application::returnFromShell()'s own hideVisibleCursor() call).
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

bool Application::returnFromGrep() {
  currentMode_ = grepReturnMode_;
  activeView().redraw();
  return true;
}

bool Application::returnFromCutContent() {
  currentMode_ = ApplicationMode::Cut;
  cutView_.redraw();
  return true;
}

bool Application::returnFromCut() {
  currentMode_ = cutReturnMode_;
  activeView().redraw();
  return true;
}

bool Application::returnFromMan() {
  manMode_.saveCursorPosition();
  currentMode_ = manReturnMode_;
  activeView().redraw();
  return true;
}

bool Application::returnFromLocate() {
  currentMode_ = locateReturnMode_;
  activeView().redraw();
  return true;
}

bool Application::returnFromArchiveContent() {
  currentMode_ = ApplicationMode::Archive;
  archiveView_.redraw();
  return true;
}

// Climbs back out of a nested-archive descent one level at a time
// (see descendToNestedArchive()'s own comment) before finally
// returning to Directory once archiveParentStack_ is empty again --
// the original, still-unchanged behavior for the non-nested case.
bool Application::returnFromArchive() {
  archiveMode_.saveCursorPosition();
  if (!archiveParentStack_.empty()) {
    std::string parentPath = archiveParentStack_.back();
    archiveParentStack_.pop_back();
    if (archiveMode_.loadArchive(parentPath)) return true;
    // The remembered path should always still be valid (a real file
    // or our own still-on-disk temp file) -- but if something removed
    // it out from under us, fail safe by falling through to Directory
    // below rather than leaving the screen showing stale content.
  }
  currentMode_ = ApplicationMode::Directory;
  directoryView_.redraw();
  return true;
}

bool Application::returnFromDu() {
  duMode_.saveCursorPosition();
  currentMode_ = ApplicationMode::Directory;
  directoryView_.redraw();
  return true;
}

bool Application::returnFromShell() {
  currentMode_ = shellReturnMode_;
  // The mode being returned to (typically a plain directory listing)
  // relies on ContentView's own row-highlight, not a visible cursor,
  // and never calls setVisibleCursor() itself -- without this, -g
  // would keep showing SHELL's last cursor position (a history
  // browsing line, or the live prompt caret) floating over the new
  // screen's content, since ContentView::redraw() deliberately never
  // touches it (see Display::setVisibleCursor()'s own comment on why
  // it must persist across incidental redraws to begin with).
  display_.hideVisibleCursor();
  activeView().redraw();
  return true;
}

// Ported from do_shell()'s own "if (kill(spid, 0)) { pv = NULL; m =
// -M_bl; break; }" -- legacy's input_shell() checks whether the
// relay/shell process is still alive on EVERY loop iteration, and the
// moment it isn't (typically because the user just typed `exit`),
// automatically returns -M_bl (^G's own code) and unwinds straight
// back to whichever mode SHELL was entered from -- no separate ^G
// needed. Called whenever Shell::onPtyReadable() reports EOF (see its
// own comment). shellStarted_ resets to false so the NEXT entry into
// Shell mode spawns a genuinely fresh child via ensureShellActive()'s
// own shellStarted_ guard, rather than trying to reuse a dead one --
// Shell::start()'s own resetSessionState() clears scrollback_/
// pipeBuffer_/etc at that point, matching legacy's fresh PP chain on
// restart. A real user request (2026-08-02) confirmed this -- see
// resetSessionState()'s own comment for the full exchange.
void Application::handleShellChildExited() {
  shell_.terminate();
  shellStarted_ = false;
  if (currentMode_ == ApplicationMode::Shell) returnFromShell();
}

bool Application::returnFromListBrowser() {
  currentMode_ = listBrowserReturnMode_;
  activeView().redraw();
  return true;
}

// Ported from the M_shell (^[s) entry point: starts the pty-backed
// child on first use only (shellStarted_), and just switches the
// active screen on every subsequent entry -- the child keeps running
// in the background across Back/^G in between (see class comment).
// Refuses re-entry from within Shell mode itself (nothing to do).
bool Application::startShell() {
  if (currentMode_ == ApplicationMode::Shell) return false;
  return ensureShellActive();
}

bool Application::ensureShellActive() {
  if (currentMode_ == ApplicationMode::Shell) return true;
  if (!shellStarted_) {
    if (!shell_.start()) return false;
    shellStarted_ = true;
  }
  shellReturnName_ = previousModeName(currentMode_);
  shellReturnMode_ = currentMode_;
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.redrawPromptRow();
  return true;
}

// K-60 (^Xs, M_hup): see the class's own declaration comment. Unlike
// ensureShellActive(), does NOT short-circuit when already in Shell
// mode -- that's the whole point of "restart" -- so it manages
// shellStarted_/currentMode_/shellReturnMode_ directly rather than
// delegating to ensureShellActive().
bool Application::shellRestart() {
  if (shell_.busy()) return false;
  shell_.terminate();
  shellStarted_ = false;
  if (!shell_.start()) return false;
  shellStarted_ = true;
  if (currentMode_ != ApplicationMode::Shell) {
    shellReturnName_ = previousModeName(currentMode_);
    shellReturnMode_ = currentMode_;
  }
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.redrawPromptRow();
  return true;
}

std::string Application::previousModeName(ApplicationMode mode) const {
  switch (mode) {
    case ApplicationMode::Directory:
      return directoryMode_.currentDirectory();
    case ApplicationMode::File:
      return fileMode_.currentFile();
    case ApplicationMode::Man:
      return manMode_.currentPage();
    case ApplicationMode::Archive:
    case ApplicationMode::ArchiveContent:
      return archiveMode_.currentArchive();
    case ApplicationMode::Du:
      return duMode_.currentPath();
    default:
      return "";
  }
}

// K-17 (^X^W, M_w4): ported from menu.c's own `send_pp(hp->bp->pp,
// "a")` (append mode) -- writes activeView()'s current lines to
// wFilePath_. Available from any mode (legacy's own M_w4 case has no
// mode guard either). False if wFilePath_ couldn't be opened for
// writing (e.g. Application was constructed with an empty
// homeDirectory, see the constructor's own comment).
bool Application::saveBufferToFile() {
  std::ofstream out(wFilePath_, std::ios::app);
  if (!out) return false;
  for (const auto &line : activeView().lines()) out << line.text << "\n";
  return true;
}

// K-61 (^J., M_chdir2): ported from menu.c's own m_w() M_chdir2 case
// ("cd '<dir>'\r", auto-submitted). Uses DirectoryMode's own current
// directory as the closest match to legacy's lp->dir (a generic
// "current directory" field this port doesn't have an equivalent of
// outside DirectoryMode).
bool Application::shellChangeDirectory() {
  if (shell_.busy()) return false;
  std::string dir = directoryMode_.currentDirectory();
  if (dir.empty()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("cd '" + dir + "'");
  shell_.submitLine();
  return true;
}

// K-62 (^J,, M_chdir): ported from menu.c's own m_w() M_chdir case,
// which forwards a caller-supplied command string -- usilicrc's own
// K-62 description ("Execute `ls -l \cM`") names the literal command
// directly, so that's what's sent here rather than re-deriving it
// from wherever legacy's own ap->comm was populated.
bool Application::shellRunLsL() {
  if (shell_.busy()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("ls -l");
  shell_.submitLine();
  return true;
}

// K-63 (^Jw, M_w): ported from menu.c's own m_w() M_w case in
// SPIRIT, not literally -- legacy writes `name` to a temp file then
// builds "echo `cat <file>`" purely to sidestep 1990s shell-quoting
// concerns around embedding the name directly in a generated command
// string; this port's LineEditor::setText() just takes the string
// directly (no shell parsing happens until the user actually submits
// it), so inserting "echo <name>" straight into the command line has
// the same visible effect without the temp-file indirection. Uses
// the selected DirectoryMode entry's name, or FileMode's own current
// file's basename if already viewing one -- other modes aren't
// covered yet (a real "current access name" tracked generically
// across every mode, matching legacy's own ap->name, doesn't exist
// in this port).
bool Application::shellInsertName() {
  if (shell_.busy()) return false;
  std::string name;
  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (entry) name = entry->name;
  } else if (currentMode_ == ApplicationMode::File || shellReturnMode_ == ApplicationMode::File) {
    std::filesystem::path path(fileMode_.currentFile());
    name = path.filename().string();
  }
  if (name.empty()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("echo " + name);
  shell_.commandLine().redraw();
  return true;
}

// K-64 (^J^W, M_w2): ported from menu.c's own m_w() M_w2 case --
// unlike K-63, this genuinely needs a real file (`cat` has no way to
// display in-memory content otherwise), so it reuses the SAME target
// saveBufferToFile() writes to. Writes activeView()'s CURRENT lines
// (the view active BEFORE switching to Shell mode, so this must run
// before ensureShellActive() changes currentMode_).
bool Application::shellInsertBufferContent() {
  if (shell_.busy()) return false;
  ContentView &source = activeView();
  std::ofstream out(wFilePath_, std::ios::trunc);
  if (!out) return false;
  for (const auto &line : source.lines()) out << line.text << "\n";
  out.close();
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("cat " + wFilePath_);
  shell_.commandLine().redraw();
  return true;
}

// K-67: see the class's own declaration comment.
bool Application::executeShowErrorLog() {
  if (!std::filesystem::exists(errorLogPath_)) return false;
  if (!fileMode_.loadFile(errorLogPath_)) return false;
  currentMode_ = ApplicationMode::File;
  return true;
}

// K-68: see the class's own declaration comment.
bool Application::executeShowShellLog() {
  if (!shellStarted_) return false;
  std::ofstream out(logFilePath_, std::ios::trunc);
  if (!out) return false;
  for (const auto &line : shell_.scrollback().lines()) out << line.text << "\n";
  out.close();
  if (!fileMode_.loadFile(logFilePath_)) return false;
  currentMode_ = ApplicationMode::File;
  return true;
}

// K-0: see the class's own declaration comment.
bool Application::executeShowTcbBanner() {
  std::ostringstream banner;
  banner << "usilic v1.5b\n";
  banner << "Startup file : " << usilicrcPath_ << "\n";
  banner << "History file : " << historyFilePath_ << "\n";
  banner << "User name    : ";
  if (const char *user = std::getenv("USER")) {
    banner << user;
    char hostname[256] = {0};
    if (::gethostname(hostname, sizeof(hostname) - 1) == 0 && hostname[0]) banner << "@" << hostname;
  } else {
    banner << "---";
  }
  banner << "\n";
  banner << "ERROR log    : " << errorLogPath_ << "\n";
  banner << "SHELL log    : " << logFilePath_ << "\n";

  std::string text = banner.str();
  std::vector<unsigned char> bytes(text.begin(), text.end());
  std::string tempPath = materializeArchiveTempFile("tcb", bytes);
  if (tempPath.empty() || !fileMode_.loadFile(tempPath)) return false;
  currentMode_ = ApplicationMode::File;
  return true;
}

// K-27: see the class's own declaration comment.
bool Application::cycleCharset() {
  encoding_ = (encoding_ == Charset::Encoding::Ascii)  ? Charset::Encoding::Euc
              : (encoding_ == Charset::Encoding::Euc) ? Charset::Encoding::Utf8
                                                       : Charset::Encoding::Ascii;

  fileMode_.setEncoding(encoding_);
  manMode_.setEncoding(encoding_);
  shell_.setEncoding(encoding_);
  promptEditor_.setEncoding(encoding_);

  if (currentMode_ == ApplicationMode::File) fileMode_.reflow(display_.columns());
  else if (currentMode_ == ApplicationMode::Man) manMode_.reflow(display_.columns());

  return true;
}

// Ported from M_cp2/M_mv2 (^Xc/^Xm) entering a destination-path
// prompt: Directory-only, and only when there's something to act on
// (marked entries, or the current selection -- see
// DirectoryMode::targetEntries()).
bool Application::startCopyPrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  activatePrompt(PromptKind::CopyDestination, "");
  return true;
}

bool Application::startMovePrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  activatePrompt(PromptKind::MoveDestination, "");
  return true;
}

// Ported from M_rm2 (^Xr) entering a confirmation prompt -- a
// deliberately simplified stand-in for m_dest()'s T_rm branch (which
// checks a single typed 'y' the same way), see class comment.
bool Application::startRemovePrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  activatePrompt(PromptKind::RemoveConfirm, "");
  return true;
}

// Ported from M_exit (^X^C) entering a confirmation prompt -- see
// class comment for why this is reachable from any mode and doesn't
// need a targetEntries()-style guard the way CP/MV/RM do.
bool Application::startExitPrompt() {
  activatePrompt(PromptKind::ExitConfirm, "");
  return true;
}

// Ported from m_du()/m_cr()'s T_du case: unlike every other prompt
// above, the field is PRE-FILLED with the current directory
// (`strcpy(rvp->str, ap->du)`) rather than starting empty -- ^M alone
// accepts the default, or the user can edit first. Directory-only,
// see class comment.
bool Application::startDuPrompt() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  activatePrompt(PromptKind::DuPath, directoryMode_.currentDirectory());
  return true;
}

// K-57/K-58: see the class's own declaration comment.
bool Application::startArchivePrompt(bool interactive) {
  if (currentMode_ == ApplicationMode::Archive) {
    if (archiveMode_.targetEntries().empty()) return false;
  } else if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry || entry->isDirectory) return false;
    std::string path = directoryMode_.currentDirectory();
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry->name;
    ArchiveKind kind = ArchiveKind::None;
    if (!configuration_.programForSuffix(path, &kind) || kind == ArchiveKind::None) return false;
  } else {
    return false;
  }
  archiveExtractInteractive_ = interactive;
  activatePrompt(PromptKind::ArchiveDestination, "");
  return true;
}

std::vector<std::string> Application::buildArchiveExtractArgv(const std::string &archivePath, ArchiveKind kind,
                                                                const std::vector<std::string> &members,
                                                                const std::string &destination) const {
  if (kind == ArchiveKind::Tar) {
    std::vector<std::string> argv = {"tar", "xf", archivePath, "-C", destination};
    for (const auto &m : members) argv.push_back(m);
    return argv;
  }
  if (kind == ArchiveKind::Zip) {
    std::vector<std::string> argv = {"unzip", "-o", archivePath};
    for (const auto &m : members) argv.push_back(m);
    argv.push_back("-d");
    argv.push_back(destination);
    return argv;
  }
  return {};  // Lha/Rpm/Deb/Bzip2 -- not supported by this first slice, see the class's own declaration comment
}

// K-45/K-47 (^Xf/^Xd, "Input file/directory name") -- unlike
// startDuPrompt() and friends, deliberately NOT gated to Directory
// mode: legacy's own M_lfile2/M_ldir2 are general "type any path,
// land wherever it goes" navigation, usable from File/Man/Grep/etc
// just as much as from a directory listing. Both keys share this one
// prompt/execute pair since PathResolver already decides file-vs-
// directory from the path itself, not from which key was pressed --
// `isDirectory` is stashed in pathInputIsDirectory_ purely for
// promptLabelText()'s benefit (legacy shows a distinct " FILE"/" DIR"
// mode-line label per key even though the two share this one
// execution path; a real side-by-side screenshot comparison,
// 2026-08-03, found this port always showing "FILE/DIR" regardless of
// which key was pressed).
bool Application::startPathInputPrompt(bool isDirectory) {
  pathInputIsDirectory_ = isDirectory;
  activatePrompt(PromptKind::PathInput, "");
  return true;
}

// Ported from m_list()'s dispatch, narrowed to the two least
// ambiguous categories (see class comment). Not prompt-driven -- this
// is an immediate action, like archive-browsing's auto-detection,
// not a text-entry flow. Directory-only entry, matching every other
// history-adjacent action in this slice. History::recentEntries()
// returns oldest-first; shown newest-first here, the more useful
// order for a "recent items" picker.
bool Application::startListBrowser(RingCategory category) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  const std::deque<std::string> &recent = history_.recentEntries(category);
  std::vector<std::string> items(recent.rbegin(), recent.rend());
  listBrowserCategory_ = category;
  listBrowserReturnMode_ = currentMode_;
  listBrowserMode_.show(items);
  currentMode_ = ApplicationMode::ListBrowser;
  return true;
}

// K-2 (^H, M_key, "KEY"): see kKeyTable's own comment for why this is
// a hand-maintained mirror of dispatchByte()'s switch statements
// rather than a real usilicrc KEY-section parse. Reachable from any
// mode (matching HELP's own no-mode-guard precedent, and legacy's own
// init_bp() default case) -- re-entry from within Key mode itself is
// refused, same shape as every other Start*() in this class.
bool Application::startKey() {
  if (currentMode_ == ApplicationMode::Key) return false;
  std::vector<ContentView::Line> lines;
  lines.reserve(std::size(kKeyTable));
  for (const auto &entry : kKeyTable) {
    std::string label = entry.keyLabel;
    if (label.size() < 10) label.append(10 - label.size(), ' ');
    lines.push_back({label + "  " + entry.description, std::nullopt, std::nullopt});
  }
  keyView_.setLines(std::move(lines));
  keyView_.setCursorIndex(0);
  keyReturnMode_ = currentMode_;
  currentMode_ = ApplicationMode::Key;
  keyView_.redraw();
  return true;
}

bool Application::returnFromKey() {
  currentMode_ = keyReturnMode_;
  activeView().redraw();
  return true;
}

// See this class's own declaration comment (application.hpp) for the
// "return first, THEN execute" order HELP.jp 2.2 documents.
bool Application::executeSelectedKeyBinding() {
  int idx = keyView_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(std::size(kKeyTable))) return false;
  std::vector<int> bytes = kKeyTable[idx].bytes;  // copy: returnFromKey() below may redraw/mutate state first

  currentMode_ = keyReturnMode_;
  activeView().redraw();
  bool result = false;
  for (int byte : bytes) result = feedByte(byte, std::nullopt);
  return result;
}

// Ported from m_grep(): entering FGREP/EGREP just remembers where to
// come back to (legacy: save_stack()) and switches to a prompt state
// so the next bytes are the search pattern, not commands. Refuses to
// start a nested search from within grep results, matching m_grep()'s
// own is_name(ap->name, M_fgrep/M_egrep) guard.
bool Application::startGrepPrompt(GrepSyntax syntax) {
  if (currentMode_ == ApplicationMode::Grep) return false;
  grepSyntax_ = syntax;
  grepReturnMode_ = currentMode_;
  activatePrompt(PromptKind::GrepPattern, "");
  return true;
}

// Ported from m_cut(): only reachable from File mode (a fresh split
// of the whole file) or CutContent (resplitting the segment currently
// being viewed, CutMode::splitSelected()'s "one level deeper") --
// matching the user-confirmed semantics recorded on CutMode's own
// class comment. Starting from the Cut label-list itself isn't
// meaningful (nothing selected yet to resplit); starting from
// Directory/Grep isn't part of this slice (see class comment's
// "Deliberately excluded").
bool Application::startCutPrompt(GrepSyntax syntax) {
  if (currentMode_ != ApplicationMode::File && currentMode_ != ApplicationMode::CutContent) return false;
  cutSyntax_ = syntax;
  if (currentMode_ == ApplicationMode::File) cutReturnMode_ = ApplicationMode::File;
  activatePrompt(PromptKind::CutPattern, "");
  return true;
}

// K-35/K-36's "regular file" CUT variant -- CutMode's own class
// comment already flagged this exact shortcut (split a file's content
// straight from a directory listing, without a separate "open it
// first" step) as unwired future work; wiring it here is just:
// resolve the selected entry's path, stash it, and reuse the SAME
// CutPattern prompt/executeCut() the buffer variant already has --
// executeCut() loads cutFileSourcePath_ via FileMode before splitting
// when it's non-empty, instead of reading the already-displayed
// buffer. cutReturnMode_ is deliberately left untouched here: its
// class default is already ApplicationMode::Directory, exactly where
// this variant is entered from and should return to.
bool Application::startCutFilePrompt(GrepSyntax syntax) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry || entry->isDirectory) return false;
  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;
  cutFileSourcePath_ = path;
  cutSyntax_ = syntax;
  activatePrompt(PromptKind::CutPattern, "");
  return true;
}

// Ported from m_man(): unlike GREP/CUT, m_man() has no is_name()
// nested-entry guard and doesn't read anything from the currently
// active view (see class comment) -- so this is reachable from any
// mode, including Man itself (viewing a different page without
// leaving Man mode first). manReturnMode_ is only updated the FIRST
// time (i.e. when not already in Man mode), so ^G after browsing
// several pages in a row still returns to wherever the excursion
// originally started, not to the page viewed just before it.
bool Application::startManPrompt() {
  if (currentMode_ != ApplicationMode::Man) manReturnMode_ = currentMode_;
  activatePrompt(PromptKind::ManName, "");
  return true;
}

// Ported from m_prog()'s M_prog/M_prog2 entry (see class comment for
// why, unlike GREP/CUT/MAN, this never switches ApplicationMode at
// all -- it runs a foreground command and returns to whatever was
// already showing). Like MAN, always reachable and always starts
// with an empty prompt.
bool Application::startProgramPrompt() {
  activatePrompt(PromptKind::ProgramCommand, "");
  return true;
}

// Ported from m_locate()'s M_locate (^J^L) path: unlike MAN/PROGRAM,
// entering LOCATE from within its own results screen is refused
// (m_locate()'s own is_name(ap->name, M_locate) guard) -- the one
// property LOCATE shares with GREP rather than MAN/PROGRAM. The ^Jl
// "reuse the previous result set without re-prompting" shortcut
// (M_locate2 when a prior result exists) is deliberately not
// replicated here; see class comment.
bool Application::startLocatePrompt() {
  if (currentMode_ == ApplicationMode::Locate) return false;
  locateReturnMode_ = currentMode_;
  activatePrompt(PromptKind::LocateQuery, "");
  return true;
}

bool Application::feedPromptByte(int byte) {
  if (byte == kCarriageReturn) return submitPrompt();
  if (byte == kControlG) {
    cancelPrompt();
    return true;
  }
  // K-65 (^I): only for the FILE/DIR PathInput prompt in this first
  // slice -- MAN/PROGRAM/LOCATE/GREP/etc's own prompt text isn't a
  // filesystem path, so there's nothing meaningful to complete there
  // yet (see completeFilesystemPath()'s own declaration comment).
  if (byte == 0x09 && promptKind_ == PromptKind::PathInput) {
    auto candidates = completeFilesystemPath(promptEditor_.text(), directoryMode_.currentDirectory());
    if (candidates.size() == 1 && candidates[0].size() > promptEditor_.text().size()) {
      promptEditor_.paste(candidates[0].substr(promptEditor_.text().size()));
    }
    return true;
  }
  promptEditor_.insertByte(static_cast<unsigned char>(byte));
  promptEditor_.redraw();
  return true;
}

bool Application::submitPrompt() {
  PromptKind kind = promptKind_;
  std::string text = promptEditor_.text();
  promptKind_ = PromptKind::None;

  switch (kind) {
    case PromptKind::GrepPattern:
      return executeGrep(text);
    case PromptKind::CutPattern:
      return executeCut(text);
    case PromptKind::ManName:
      return executeMan(text);
    case PromptKind::ProgramCommand:
      return executeProgram(text);
    case PromptKind::LocateQuery:
      return executeLocate(text);
    case PromptKind::CopyDestination:
      return executeCopy(text);
    case PromptKind::MoveDestination:
      return executeMove(text);
    case PromptKind::RemoveConfirm:
      return executeRemove(text);
    case PromptKind::ExitConfirm:
      return executeExit(text);
    case PromptKind::DuPath:
      return executeDu(text);
    case PromptKind::ArchiveDestination:
      return executeArchiveExtract(text);
    case PromptKind::PathInput:
      return executePathInput(text);
    case PromptKind::None:
      return false;
  }
  return false;
}

void Application::cancelPrompt() {
  promptKind_ = PromptKind::None;
  activeView().redraw();
}

std::string Application::promptLabelText(PromptKind kind) const {
  switch (kind) {
    case PromptKind::GrepPattern:
      return grepSyntax_ == GrepSyntax::Fixed ? "FGREP" : "EGREP";
    case PromptKind::CutPattern:
      return "CUT";
    case PromptKind::ManName:
      return "MAN";
    case PromptKind::ProgramCommand:
      return "PROGRAM";
    case PromptKind::LocateQuery:
      return "LOCATE";
    case PromptKind::CopyDestination:
      return "CP";
    case PromptKind::MoveDestination:
      return "MV";
    case PromptKind::RemoveConfirm:
      return "RM";
    case PromptKind::ExitConfirm:
      return "EXIT (y/n)?";
    case PromptKind::DuPath:
      return "DU";
    case PromptKind::PathInput:
      return pathInputIsDirectory_ ? "DIR" : "FILE";
    case PromptKind::ArchiveDestination:
      return "ARC";
    case PromptKind::None:
      return "";
  }
  return "";
}

void Application::drawPromptLabel(PromptKind kind) {
  std::string label = promptLabelText(kind);
  int row = std::max(0, display_.rows() - 1);
  display_.moveCursor(row, 0);
  display_.clearToEndOfLine();
  display_.setAttribute(Display::Attribute::Standout);
  for (unsigned char c : label) display_.putChar(c);
  display_.setAttribute(Display::Attribute::StandoutOff);
  int labelWidth = label.empty() ? 0 : static_cast<int>(label.size()) + 1;  // +1: one-column gap before the field
  promptEditor_.setField(row, labelWidth, std::max(1, display_.columns() - labelWidth));
}

void Application::activatePrompt(PromptKind kind, const std::string &initialText) {
  promptKind_ = kind;
  drawPromptLabel(kind);
  promptEditor_.setText(initialText);
  promptEditor_.redraw();
}

bool Application::executeGrep(const std::string &pattern) {
  ContentView &source = (grepReturnMode_ == ApplicationMode::File) ? fileView_ : directoryView_;
  std::vector<std::string> sourceLines;
  sourceLines.reserve(source.lines().size());
  for (const auto &line : source.lines()) sourceLines.push_back(line.text);

  if (!grepMode_.search(sourceLines, pattern, grepSyntax_)) {
    source.redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Grep;
  return true;
}

bool Application::executeCut(const std::string &pattern) {
  bool ok;
  std::string fileSourcePath = std::move(cutFileSourcePath_);
  cutFileSourcePath_.clear();  // consumed exactly once, regardless of which branch below runs or its outcome

  if (currentMode_ == ApplicationMode::CutContent) {
    ok = cutMode_.splitSelected(pattern, cutSyntax_);  // nested -- cutOrigin_ untouched, see class comment
  } else if (!fileSourcePath.empty()) {
    // K-35/K-36's "regular file" variant: load the file first (same
    // as opening it normally would), THEN split its now-current
    // buffer -- see startCutFilePrompt()'s own comment.
    if (!fileMode_.loadFile(fileSourcePath)) {
      activeView().redraw();
      return false;
    }
    std::vector<std::string> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back(line.text);
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
    if (ok) cutOrigin_ = CutOrigin::File;
  } else {
    std::vector<std::string> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back(line.text);
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
    if (ok) cutOrigin_ = CutOrigin::File;  // a fresh top-level split -- eligible for reflow on resize
  }

  if (!ok) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Cut;
  return true;
}

bool Application::executeMan(const std::string &name) {
  if (!manMode_.loadPage(name)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Man;
  return true;
}

// Ported from mode.c's M_prog case: the typed command line's first
// word must be a strncmp-prefix match of some PROGRAM directive
// entry's own first word (find_prog(), sub.c) -- otherwise legacy
// falls back to SHELL mode with the typed line pre-filled, which
// isn't wired here yet (see class comment). Runs the whole typed line
// via ProgramMode::run() when recognized; ApplicationMode never
// changes either way (see class comment).
bool Application::executeProgram(const std::string &command) {
  std::size_t start = command.find_first_not_of(" \t");
  if (start == std::string::npos) {
    activeView().redraw();
    return false;
  }
  std::size_t end = command.find_first_of(" \t", start);
  std::string firstWord = command.substr(start, end == std::string::npos ? std::string::npos : end - start);

  bool recognized = false;
  for (const auto &seed : configuration_.programSeedCommands()) {
    std::size_t seedEnd = seed.find_first_of(" \t");
    std::string seedFirstWord = seed.substr(0, seedEnd);
    if (!seedFirstWord.empty() && firstWord.compare(0, seedFirstWord.size(), seedFirstWord) == 0) {
      recognized = true;
      break;
    }
  }

  if (!recognized) {
    activeView().redraw();
    return false;
  }

  programMode_.run(command);
  activeView().redraw();
  return true;
}

bool Application::executeLocate(const std::string &query) {
  if (!locateMode_.search(query)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Locate;
  return true;
}

// Ported from m_dest()'s T_cp/T_mv branches (destination validation
// simplified away -- CpMvRmMode's own cp/mv invocation surfaces
// failures via its ChildProcess::Result instead, see class comment).
// Reloads the directory listing afterward either way, so the screen
// reflects what actually happened on disk (this also clears marks,
// via DirectoryMode::enterDirectory()'s own documented behavior).
bool Application::executeCopy(const std::string &destination) {
  bool interactive = cpMvRmInteractive_;
  cpMvRmInteractive_ = false;  // consumed exactly once, regardless of outcome below

  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    directoryView_.redraw();
    return false;
  }
  // K-51 ("with SHELL window"): runs interactively, with the real
  // command's own output visible live on the real terminal -- see
  // CpMvRmMode::copyToInteractive()'s own comment. K-52 (the default):
  // captured silently, matching the pre-existing behavior.
  bool ok = interactive ? cpMvRmMode_.copyToInteractive(targets, destination) == 0
                        : cpMvRmMode_.copyTo(targets, destination).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

bool Application::executeMove(const std::string &destination) {
  bool interactive = cpMvRmInteractive_;
  cpMvRmInteractive_ = false;

  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    directoryView_.redraw();
    return false;
  }
  bool ok = interactive ? cpMvRmMode_.moveToInteractive(targets, destination) == 0
                        : cpMvRmMode_.moveTo(targets, destination).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

// Ported from m_dest()'s T_rm branch: `confirmation` must be exactly
// "y" (matching legacy's `*ap->mess != 'y'` check) or nothing is
// removed.
bool Application::executeRemove(const std::string &confirmation) {
  bool interactive = cpMvRmInteractive_;
  cpMvRmInteractive_ = false;

  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || confirmation != "y") {
    directoryView_.redraw();
    return false;
  }
  bool ok = interactive ? cpMvRmMode_.removeInteractive(targets) == 0 : cpMvRmMode_.remove(targets).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

// Ported from m_cr()'s M_exit case: `if (*rvp->str == 'y') return
// -T_exit;` -- confirmation must be exactly "y" (same simplification
// as executeRemove()'s first-character-only legacy check). Sets
// wantsExit_ rather than doing anything itself; nothing about
// currentMode_/the screen changes either way.
bool Application::executeExit(const std::string &confirmation) {
  if (confirmation != "y") {
    activeView().redraw();
    return false;
  }
  wantsExit_ = true;
  return true;
}

// Ported from mode.c's T_du case: runs `du` on `path`. False for an
// empty path or a failed `du` invocation.
bool Application::executeDu(const std::string &path) {
  if (path.empty() || !duMode_.loadUsage(path)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Du;
  return true;
}

// K-57/K-58: see startArchivePrompt()'s own comment for the two entry
// points this re-derives at submit time (currentMode_ can't have
// changed while the prompt was up).
bool Application::executeArchiveExtract(const std::string &destination) {
  bool interactive = archiveExtractInteractive_;
  archiveExtractInteractive_ = false;  // consumed exactly once, regardless of outcome below

  std::string archivePath;
  ArchiveKind kind = ArchiveKind::None;
  std::vector<std::string> members;

  if (currentMode_ == ApplicationMode::Archive) {
    archivePath = archiveMode_.currentArchive();
    kind = archiveMode_.archiveKind();
    for (const auto &entry : archiveMode_.targetEntries()) members.push_back(entry.name);
  } else if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry) {
      activeView().redraw();
      return false;
    }
    archivePath = directoryMode_.currentDirectory();
    if (!archivePath.empty() && archivePath.back() != '/') archivePath += '/';
    archivePath += entry->name;
    configuration_.programForSuffix(archivePath, &kind);
    // members left empty -- the whole archive, per HELP.jp 2.11's own
    // File-selector example.
  }

  std::vector<std::string> argv = buildArchiveExtractArgv(archivePath, kind, members, destination);
  if (argv.empty() || destination.empty()) {
    activeView().redraw();
    return false;
  }

  // K-57 ("with SHELL window"): runs interactively, with the real
  // tar/unzip output visible live on the real terminal -- same
  // ChildProcess::runInteractive() shape CP/MV/RM's own ^X^Kc/^X^Km/
  // ^X^Kr already use (see CpMvRmMode::runInteractive()'s own
  // comment on why this class, despite its name, is what actually
  // owns the KeyInput reference this needs). K-58 (the default):
  // captured silently.
  bool ok = interactive ? cpMvRmMode_.runInteractive(argv) == 0 : cpMvRmMode_.run(argv).ok;
  activeView().redraw();
  return ok;
}

// Ported from mode.c's M_lfile/M_ldir handling: resolves the typed
// path (~ expansion, ./ and ../ cleanup, see PathResolver's own class
// comment) and lands in Directory or File mode depending on what it
// actually is. Deliberately simpler than openSelectedEntry(): no
// EXEC-suffix or archive-format auto-detection here (legacy's own
// M_lfile2 doesn't do that either -- typing a path is "load this as
// plain text/a listing," not "open it the smart way an existing
// listing ENTRY would be," which is Enter's own, separate job). False
// for an empty, nonexistent, or unreadable path.
bool Application::executePathInput(const std::string &path) {
  if (path.empty()) {
    activeView().redraw();
    return false;
  }
  // Resolved relative to directoryMode_'s own currentDirectory() (the
  // directory last BROWSED, which stays put even while looking at a
  // File/Man/etc screen opened from it -- see PathResolver::resolve()'s
  // own baseDirectory comment for why this can't be the process's own
  // cwd: usilic never chdir()s). Matches Shell's own Tab-completion,
  // which already resolves against shell_.currentDirectory() for the
  // same reason.
  ResolvedPath resolved = pathResolver_.resolve(path, directoryMode_.currentDirectory());
  if (!resolved.exists) {
    activeView().redraw();
    return false;
  }
  if (resolved.isDirectory) {
    if (!directoryMode_.enterDirectory(resolved.path)) {
      activeView().redraw();
      return false;
    }
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  if (!fileMode_.loadFile(resolved.path)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::File;
  return true;
}

// K-65 (^I, M_list): see the class's own declaration comment.
std::vector<std::string> Application::completeFilesystemPath(const std::string &partial,
                                                               const std::string &baseDirectory) const {
  std::string dirPart, prefix;
  std::size_t slash = partial.find_last_of('/');
  if (slash == std::string::npos) {
    prefix = partial;
  } else {
    dirPart = partial.substr(0, slash + 1);
    prefix = partial.substr(slash + 1);
  }

  std::string searchDir;
  if (!dirPart.empty() && (dirPart.front() == '/' || dirPart.front() == '~')) {
    searchDir = pathResolver_.resolve(dirPart).path;
  } else {
    searchDir = baseDirectory;
    if (!searchDir.empty() && searchDir.back() != '/') searchDir += '/';
    searchDir += dirPart;
  }

  std::vector<std::string> results;
  std::error_code ec;
  std::filesystem::directory_iterator it(searchDir, ec);
  if (ec) return results;
  for (const auto &entry : it) {
    std::string name = entry.path().filename().string();
    if (name.compare(0, prefix.size(), prefix) != 0) continue;
    bool isDir = entry.is_directory(ec);
    results.push_back(dirPart + name + (isDir ? "/" : ""));
  }
  return results;
}

// Ported from menu.c's M_edit case (^Je) -- see class comment for why
// this is neither prompt-driven nor mode-switching, and for the
// deliberate simplification on line-number: Directory-mode edits
// always open at line 1 (do_edit()'s own `pnp->line` argument is the
// cursor's row within the DIRECTORY LISTING in that context, not a
// meaningful line number inside the file about to be opened -- not
// carried over here), File-mode edits use the real cursor line.
bool Application::executeEdit() {
  std::string path;
  int lineNumber = 1;

  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry || entry->isDirectory) return false;
    path = directoryMode_.currentDirectory();
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry->name;
  } else if (currentMode_ == ApplicationMode::File) {
    path = fileMode_.currentFile();
    lineNumber = fileView_.cursorIndex() + 1;
  } else {
    return false;  // out of scope for this slice, see class comment
  }

  bool changed = editMode_.edit(path, lineNumber);
  if (changed && currentMode_ == ApplicationMode::File) fileMode_.loadFile(path);
  activeView().redraw();
  return true;
}

std::vector<std::string> Application::wrapHelpText(int width) const {
  PipeBuffer pipe(encoding_, width, 16L * 1024 * 1024);
  pipe.feed(helpText_);
  pipe.flush();
  std::vector<std::string> lines;
  lines.reserve(pipe.lines().size());
  for (const auto &styled : pipe.lines()) lines.push_back(styled.text);
  return lines;
}

bool Application::loadHelpText(const std::string &text) {
  helpText_ = text;
  return !helpText_.empty();
}

// Ported from menu.c's M_help/T_help case -- see class comment for
// why this is a CutMode split (real CUT and HELP share one instance)
// rather than its own display mechanism. Reachable from any mode
// (legacy's init_bp() default case has no mode-context guard for
// M_help/T_help either).
bool Application::startHelp() {
  if (helpText_.empty()) return false;
  std::vector<std::string> helpLines = wrapHelpText(display_.columns());

  bool preserveExistingCut = (currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent);
  bool ok;
  if (preserveExistingCut) {
    // A real CUT session is already showing -- push it (the SAME
    // mechanism a nested splitSelected() resplit uses) so ^G out of
    // HELP resumes it via Back's existing canGoBack()/goBack() check.
    // Deliberately does NOT touch cutSyntax_/cutOrigin_ here: both are
    // single shared fields (not saved/restored per ModeStack level),
    // so leaving them alone preserves whatever the real cut being
    // preserved already had -- setting them for HELP's own benefit
    // would corrupt that real cut's own state once ^G restores it
    // (e.g. a Fixed-string real cut resuming with cutSyntax_ left at
    // Regex from HELP would re-split wrong on a later resize).
    ok = cutMode_.pushSplit(helpLines, kHelpPattern, GrepSyntax::Regex);
  } else {
    cutReturnMode_ = currentMode_;  // nothing to preserve -- same "leave Cut back to..." anchor a real CUT uses
    ok = cutMode_.split(helpLines, kHelpPattern, GrepSyntax::Regex);
    if (ok) {
      cutOrigin_ = CutOrigin::Help;  // a fresh top-level split -- eligible for reflow on resize
      cutSyntax_ = GrepSyntax::Regex;  // safe to set here: nothing real depends on the old value in this branch
    }
  }
  if (!ok) return false;

  currentMode_ = ApplicationMode::Cut;
  return true;
}

// Ported from menu.c's T_winch handling -- see class comment for the
// three-way split this preserves (plain viewport resize for Directory/
// Grep/Locate/Archive/Du/ListBrowser, reflow() for File/Man and a
// TOP-LEVEL Cut/CutContent split, Shell's pty resized unconditionally)
// and why File/Man's own reflow() isn't a literal alloc_winch() port.
void Application::handleResize() {
  layoutViews();
  if (currentMode_ == ApplicationMode::File) fileMode_.reflow(display_.columns());
  if (currentMode_ == ApplicationMode::Man) manMode_.reflow(display_.columns());
  if ((currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent) &&
      !cutMode_.canGoBack()) {
    std::vector<std::string> freshSource;
    if (cutOrigin_ == CutOrigin::Help) {
      freshSource = wrapHelpText(display_.columns());
    } else {
      fileMode_.reflow(display_.columns());
      freshSource.reserve(fileView_.lines().size());
      for (const auto &line : fileView_.lines()) freshSource.push_back(line.text);
    }
    int savedIndex = cutView_.cursorIndex();
    bool wasContent = (currentMode_ == ApplicationMode::CutContent);
    if (cutMode_.split(freshSource, cutMode_.pattern(), cutSyntax_)) {
      int last = static_cast<int>(cutMode_.segments().size()) - 1;
      if (savedIndex < 0) savedIndex = 0;
      if (savedIndex > last) savedIndex = (last < 0) ? 0 : last;
      cutView_.setCursorIndex(savedIndex);
      if (wasContent) openSelectedSegment();  // re-populate cutContentView_ with the (hopefully same) segment
    }
  }
  shell_.onResize(display_.rows(), display_.columns());
  activeView().redraw();
  if (promptActive()) promptEditor_.redraw();
}

bool Application::wantsExit() const { return wantsExit_; }
void Application::requestImmediateExit() { wantsExit_ = true; }

bool Application::consumeGuiModeToggleRequest() {
  bool requested = guiModeToggleRequested_;
  guiModeToggleRequested_ = false;
  return requested;
}

// Ported from IDEAS.md's own GUI-mode click description (not legacy) --
// see class comment for the click-to-select/click-again-to-open
// convention and current Directory-only scope.
bool Application::handleMouseClick(int row, int column) {
  (void)column;  // unused in this slice -- see class comment
  if (currentMode_ != ApplicationMode::Directory) return false;
  int idx = directoryView_.lineIndexAtRow(row);
  if (idx < 0) return false;
  if (idx == directoryView_.cursorIndex()) return openSelectedEntry();
  directoryView_.setCursorIndex(idx);
  directoryView_.redraw();
  return true;
}

bool Application::promptActive() const { return promptKind_ != PromptKind::None; }
const std::string &Application::promptText() const { return promptEditor_.text(); }

ApplicationMode Application::currentMode() const { return currentMode_; }
Charset::Encoding Application::encoding() const { return encoding_; }

bool Application::awaitingSequenceOrPromptByte() const {
  return escPending_ || leaderJPending_ || promptKind_ != PromptKind::None;
}
DirectoryMode &Application::directoryMode() { return directoryMode_; }
FileMode &Application::fileMode() { return fileMode_; }
GrepMode &Application::grepMode() { return grepMode_; }
CutMode &Application::cutMode() { return cutMode_; }
ManMode &Application::manMode() { return manMode_; }
ProgramMode &Application::programMode() { return programMode_; }
LocateMode &Application::locateMode() { return locateMode_; }
ArchiveMode &Application::archiveMode() { return archiveMode_; }
DuMode &Application::duMode() { return duMode_; }
ListBrowserMode &Application::listBrowserMode() { return listBrowserMode_; }
Shell &Application::shell() { return shell_; }
CpMvRmMode &Application::cpMvRmMode() { return cpMvRmMode_; }
EditMode &Application::editMode() { return editMode_; }
ExecMode &Application::execMode() { return execMode_; }
ContentView &Application::activeContentView() { return activeView(); }

}  // namespace fm
