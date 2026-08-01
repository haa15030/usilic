#include "application.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

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
// Ported from menu.c's init_cut2() call for M_help/T_help -- the
// fixed section-header pattern HELP's own CUT split always uses (see
// class comment).
const char *const kHelpPattern = "^  [1-6][1-6]+\\.";
}  // namespace

Application::Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
                          Charset::Encoding encoding, int width, ShellOptions shellOptions,
                          std::string homeDirectory)
    : display_(display),
      pathResolver_(std::move(homeDirectory)),
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
      cpMvRmMode_(configuration, keyInput),
      duMode_(duView_, history, configuration),
      listBrowserMode_(listBrowserView_),
      editMode_(keyInput, configuration),
      execMode_(keyInput) {
  layoutViews();
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
    case kControlS:
      return Command::StartFgrep;
    case 0x80:  // ^Space (M_mark) -- KeyInput reports a literal NUL/Ctrl-Space as 0x80, see its class comment
      return Command::ToggleMark;
    case 0x12:  // ^R (M_hist)
      return Command::StartDirectoryHistory;
    case 0x0c:  // ^L (M_clear)
      return Command::Redraw;
    case 'q':  // M_quit -- usilicrc scopes this to pager contexts; see handleCommand()'s own gate
      return Command::QuitPager;
    default:
      return Command::None;
  }
}

bool Application::dispatchByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (promptKind_ != PromptKind::None) return feedPromptByte(byte);
  if (currentMode_ == ApplicationMode::Shell) return feedShellByte(byte, special);

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
      case kControlC:  // ^X ^C : exit
        return handleCommand(Command::StartExit);
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
      default:
        return false;  // unrecognized ^X-sequence in this slice (^X^Ka/^Xa/^X^Kd are still future work)
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
      default:
        return false;  // ^X^Ka (ARC) and ^X^Kd (kanji switch) are still future work
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

  return handleCommand(resolveCommand(byte, special));
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
    case Command::ScrollLineUp:
      activeView().scrollLineUp();
      return true;
    case Command::ScrollLineDown:
      activeView().scrollLineDown();
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
      return true;
    case Command::GoToTop:
      activeView().goToTop();
      return true;
    case Command::GoToBottom:
      activeView().goToBottom();
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
    case Command::StartDirectoryInput:
      return startPathInputPrompt();
    case Command::ToggleLsMode: {
      if (currentMode_ != ApplicationMode::Directory) return false;
      bool enabled = !directoryMode_.filenameOnlyMode();
      directoryMode_.setFilenameOnlyMode(enabled);
      history_.setLsMode(enabled ? 1 : 0);  // persisted across runs, see History's own field comment
      return true;
    }
    case Command::MarkAll:
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.markAll();
      return true;
    case Command::ClearAllMarks:
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
bool Application::startCopyPrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  promptKind_ = PromptKind::CopyDestination;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

bool Application::startMovePrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  promptKind_ = PromptKind::MoveDestination;
  promptEditor_.setText("");
  promptEditor_.redraw();
  return true;
}

// Ported from M_rm2 (^Xr) entering a confirmation prompt -- a
// deliberately simplified stand-in for m_dest()'s T_rm branch (which
// checks a single typed 'y' the same way), see class comment.
bool Application::startRemovePrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
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

// K-45/K-47 (^Xf/^Xd, "Input file/directory name") -- unlike
// startDuPrompt() and friends, deliberately NOT gated to Directory
// mode: legacy's own M_lfile2/M_ldir2 are general "type any path,
// land wherever it goes" navigation, usable from File/Man/Grep/etc
// just as much as from a directory listing. Both keys share this one
// prompt/execute pair since PathResolver already decides file-vs-
// directory from the path itself, not from which key was pressed.
bool Application::startPathInputPrompt() {
  promptKind_ = PromptKind::PathInput;
  promptEditor_.setText("");
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
  ResolvedPath resolved = pathResolver_.resolve(path);
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
