#include "application.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

namespace fm {

namespace {
constexpr int kLeaderJ = 0x0a;         // ^J
constexpr int kLeaderX = 0x18;         // ^X
constexpr int kEscape = 0x1b;          // ESC
constexpr int kControlS = 0x13;        // ^S
constexpr int kControlC = 0x03;        // ^C
constexpr int kControlG = 0x07;        // ^G
constexpr int kCarriageReturn = 0x0d;  // ^M
}  // namespace

Application::Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
                          Charset::Encoding encoding, int width, ShellOptions shellOptions)
    : configuration_(configuration),
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
      cpMvRmMode_(configuration),
      duMode_(duView_, history, configuration),
      listBrowserMode_(listBrowserView_),
      editMode_(keyInput, configuration) {
  // The bottom row is reserved for prompt text entry (search/cut/man/
  // locate/du patterns) -- legacy's equivalent is the mode-line row
  // disp_window() left alone for exactly this (see ContentView's own
  // class comment on why that row's rendering isn't ContentView's job
  // either).
  int contentHeight = std::max(1, display.rows() - 1);
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
  promptEditor_.setField(display.rows() - 1, 0, std::max(1, display.columns()));
}

bool Application::start(const std::string &directory) {
  currentMode_ = ApplicationMode::Directory;
  return directoryMode_.enterDirectory(directory);
}

// Ported from get_m()'s switch(m): case M_prev/M_up/M_kprev/M_kup ("k",
// ^P, left-arrow, up-arrow) and M_next/M_dn/M_knext/M_kdn ("j", ^N,
// right-arrow, down-arrow) both funnel into m_row() as plain up/down
// movement -- move_row() (scroll.c) treats all four bindings for a
// direction identically, including the pair of arrow keys that read
// oddly for a single-column list (left==up, right==down) but is
// exactly what tracing move_row()'s switch confirmed. M_forw/M_back/
// M_kforw/M_kback (space/b/^V and their arrow-adjacent bindings) map
// to m_page(); M_cr (^M only -- see class comment on why ^J is NOT
// folded in here) to m_cr(); M_bl (^G) to m_bl(); M_fgrep (^S) starts
// a fgrep search prompt. ^J-leader sequences (e.g. ^J^S for EGREP,
// ^Jc/^J^C for CUT) aren't resolvable from one byte and are handled
// by feedByte() instead.
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
    case 0x0e:  // ^N
      return Command::MoveDown;
    case 'k':
    case 0x10:  // ^P
      return Command::MoveUp;
    case ' ':
    case 'b':
    case 0x16:  // ^V
      return byte == 'b' ? Command::PageUp : Command::PageDown;
    case kCarriageReturn:
      return Command::Enter;
    case kControlG:
      return Command::Back;
    case kControlS:
      return Command::StartFgrep;
    case 0x80:  // ^Space (M_mark) -- KeyInput reports a literal NUL/Ctrl-Space as 0x80, see its class comment
      return Command::ToggleMark;
    case 0x12:  // ^R (M_hist)
      return Command::StartDirectoryHistory;
    default:
      return Command::None;
  }
}

bool Application::feedByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (promptKind_ != PromptKind::None) return feedPromptByte(byte);
  if (currentMode_ == ApplicationMode::Shell) return feedShellByte(byte, special);

  if (escPending_) {
    escPending_ = false;
    if (!special && byte == 's') return handleCommand(Command::StartShell);  // ESC s : SHELL
    return false;  // unrecognized ESC-sequence in this slice
  }

  if (leaderJPending_) {
    leaderJPending_ = false;
    if (special) return false;
    switch (byte) {
      case kControlS:  // ^J ^S : EGREP
        return handleCommand(Command::StartEgrep);
      case 'c':  // ^J c : CUT (fixed string)
        return handleCommand(Command::StartCutFixed);
      case kControlC:  // ^J ^C : CUT (regex)
        return handleCommand(Command::StartCutRegex);
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
      case kControlC:  // ^X ^C : exit
        return handleCommand(Command::StartExit);
      default:
        return false;  // unrecognized ^X-sequence in this slice (^X^Kc/^X^Km/^X^Kr/^X^Ka/^Xa are future work)
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
  if (!special && byte == kEscape) {
    escPending_ = true;
    return true;  // sequence in progress, not a no-op
  }

  return handleCommand(resolveCommand(byte, special));
}

// See class comment for the scope of this routing (and the KeyInput/
// escape-sequence gap in wiring ^[s to a real terminal). Left/Right
// move the command-line cursor; Up/Down/PageUp/PageDown (scrollback
// paging / command history) are deliberately not handled here yet.
bool Application::feedShellByte(int byte, std::optional<KeyInput::SpecialKey> special) {
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
  if (byte == kCarriageReturn) {
    shell_.submitLine();
    return true;
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
    case ApplicationMode::Directory:
    default:
      return directoryView_;
  }
}

bool Application::handleCommand(Command command) {
  switch (command) {
    case Command::MoveUp:
      activeView().moveUp();
      return true;
    case Command::MoveDown:
      activeView().moveDown();
      return true;
    case Command::PageUp:
      activeView().pageUp();
      return true;
    case Command::PageDown:
      activeView().pageDown();
      return true;
    case Command::Enter:
      if (currentMode_ == ApplicationMode::Directory) return openSelectedEntry();
      if (currentMode_ == ApplicationMode::Cut) return openSelectedSegment();
      if (currentMode_ == ApplicationMode::Archive) return openSelectedArchiveEntry();
      if (currentMode_ == ApplicationMode::Du) return openSelectedDuEntry();
      if (currentMode_ == ApplicationMode::ListBrowser) return openSelectedListEntry();
      return false;
    case Command::Back:
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
    case Command::StartFgrep:
      return startGrepPrompt(GrepSyntax::Fixed);
    case Command::StartEgrep:
      return startGrepPrompt(GrepSyntax::Regex);
    case Command::StartCutFixed:
      return startCutPrompt(GrepSyntax::Fixed);
    case Command::StartCutRegex:
      return startCutPrompt(GrepSyntax::Regex);
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

  if (archiveMode_.loadArchive(path)) {
    currentMode_ = ApplicationMode::Archive;
    return true;
  }
  if (!fileMode_.loadFile(path)) return false;
  currentMode_ = ApplicationMode::File;
  return true;
}

// Ported from mode.c's T_tar/etc Enter handling: extracts the
// selected entry's raw bytes and displays them as plain text lines
// (no PipeBuffer charset/overstrike processing -- ArchiveMode hands
// back raw bytes only, see class comment). False for a directory
// entry, a format that doesn't support extraction (RPM/.deb), or a
// failed extraction -- ArchiveMode::extractSelected() already
// distinguishes none of these from the others, so neither does this.
bool Application::openSelectedArchiveEntry() {
  auto content = archiveMode_.extractSelected();
  if (!content) return false;

  std::string text(content->begin(), content->end());
  std::istringstream stream(text);
  std::vector<ContentView::Line> lines;
  std::string line;
  while (std::getline(stream, line)) lines.push_back({line});
  archiveContentView_.setLines(std::move(lines));
  archiveContentView_.setCursorIndex(0);
  archiveContentView_.redraw();
  currentMode_ = ApplicationMode::ArchiveContent;
  return true;
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

// Ported from m_list()'s dispatch (see class comment for the 7 other
// categories not wired here): RecentDirectory drills into
// DirectoryMode; every other category (just ShellCommand in this
// slice) is a no-op, since deciding what "open" means for a shell
// command (pre-fill Shell's command line? require Shell already
// started?) is deliberately left open rather than guessed at.
bool Application::openSelectedListEntry() {
  const std::string *item = listBrowserMode_.selectedItem();
  if (!item) return false;
  if (listBrowserCategory_ == RingCategory::RecentDirectory) {
    if (!directoryMode_.enterDirectory(*item)) return false;
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  return false;
}

// Ported from M_mark (^Space): toggles the mark at the cursor.
// Directory-only, matching every other CP/MV/RM-related action added
// in this slice.
bool Application::toggleMark() {
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
  for (auto &l : content) lines.push_back({l});
  cutContentView_.setLines(std::move(lines));
  cutContentView_.setCursorIndex(0);
  cutContentView_.redraw();
  currentMode_ = ApplicationMode::CutContent;
  return true;
}

bool Application::returnToDirectory() {
  fileMode_.saveCursorPosition();
  currentMode_ = ApplicationMode::Directory;
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

bool Application::returnFromArchive() {
  archiveMode_.saveCursorPosition();
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
  activeView().redraw();
  return true;
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
  if (!shellStarted_) {
    if (!shell_.start()) return false;
    shellStarted_ = true;
  }
  shellReturnMode_ = currentMode_;
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.commandLine().redraw();
  return true;
}

// Ported from M_cp2/M_mv2 (^Xc/^Xm) entering a destination-path
// prompt: Directory-only, and only when there's something to act on
// (marked entries, or the current selection -- see
// DirectoryMode::targetEntries()).
bool Application::startCopyPrompt() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  promptKind_ = PromptKind::CopyDestination;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

bool Application::startMovePrompt() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  promptKind_ = PromptKind::MoveDestination;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

// Ported from M_rm2 (^Xr) entering a confirmation prompt -- a
// deliberately simplified stand-in for m_dest()'s T_rm branch (which
// checks a single typed 'y' the same way), see class comment.
bool Application::startRemovePrompt() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  promptKind_ = PromptKind::RemoveConfirm;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

// Ported from M_exit (^X^C) entering a confirmation prompt -- see
// class comment for why this is reachable from any mode and doesn't
// need a targetEntries()-style guard the way CP/MV/RM do.
bool Application::startExitPrompt() {
  promptKind_ = PromptKind::ExitConfirm;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

// Ported from m_du()/m_cr()'s T_du case: unlike every other prompt
// above, the field is PRE-FILLED with the current directory
// (`strcpy(rvp->str, ap->du)`) rather than starting empty -- ^M alone
// accepts the default, or the user can edit first. Directory-only,
// see class comment.
bool Application::startDuPrompt() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  promptKind_ = PromptKind::DuPath;
  promptEditor_.setText(directoryMode_.currentDirectory());
  promptEditor_.redraw();
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

// Ported from m_grep(): entering FGREP/EGREP just remembers where to
// come back to (legacy: save_stack()) and switches to a prompt state
// so the next bytes are the search pattern, not commands. Refuses to
// start a nested search from within grep results, matching m_grep()'s
// own is_name(ap->name, M_fgrep/M_egrep) guard.
bool Application::startGrepPrompt(GrepSyntax syntax) {
  if (currentMode_ == ApplicationMode::Grep) return false;
  grepSyntax_ = syntax;
  grepReturnMode_ = currentMode_;
  promptKind_ = PromptKind::GrepPattern;
  promptEditor_.setText("");
  promptEditor_.redraw();
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
  promptKind_ = PromptKind::CutPattern;
  promptEditor_.setText("");
  promptEditor_.redraw();
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
  promptKind_ = PromptKind::ManName;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

// Ported from m_prog()'s M_prog/M_prog2 entry (see class comment for
// why, unlike GREP/CUT/MAN, this never switches ApplicationMode at
// all -- it runs a foreground command and returns to whatever was
// already showing). Like MAN, always reachable and always starts
// with an empty prompt.
bool Application::startProgramPrompt() {
  promptKind_ = PromptKind::ProgramCommand;
  promptEditor_.setText("");
  promptEditor_.redraw();
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
  promptKind_ = PromptKind::LocateQuery;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

bool Application::feedPromptByte(int byte) {
  if (byte == kCarriageReturn) return submitPrompt();
  if (byte == kControlG) {
    cancelPrompt();
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
    case PromptKind::None:
      return false;
  }
  return false;
}

void Application::cancelPrompt() {
  promptKind_ = PromptKind::None;
  activeView().redraw();
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
  if (currentMode_ == ApplicationMode::CutContent) {
    ok = cutMode_.splitSelected(pattern, cutSyntax_);
  } else {
    std::vector<std::string> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back(line.text);
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
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
  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    directoryView_.redraw();
    return false;
  }
  bool ok = cpMvRmMode_.copyTo(targets, destination).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

bool Application::executeMove(const std::string &destination) {
  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    directoryView_.redraw();
    return false;
  }
  bool ok = cpMvRmMode_.moveTo(targets, destination).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

// Ported from m_dest()'s T_rm branch: `confirmation` must be exactly
// "y" (matching legacy's `*ap->mess != 'y'` check) or nothing is
// removed.
bool Application::executeRemove(const std::string &confirmation) {
  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || confirmation != "y") {
    directoryView_.redraw();
    return false;
  }
  bool ok = cpMvRmMode_.remove(targets).ok;
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

bool Application::wantsExit() const { return wantsExit_; }

bool Application::promptActive() const { return promptKind_ != PromptKind::None; }
const std::string &Application::promptText() const { return promptEditor_.text(); }

ApplicationMode Application::currentMode() const { return currentMode_; }
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

}  // namespace fm
