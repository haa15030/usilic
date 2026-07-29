#include "application.hpp"

#include <algorithm>

namespace fm {

namespace {
constexpr int kLeaderJ = 0x0a;         // ^J
constexpr int kControlS = 0x13;        // ^S
constexpr int kControlC = 0x03;        // ^C
constexpr int kControlG = 0x07;        // ^G
constexpr int kCarriageReturn = 0x0d;  // ^M
}  // namespace

Application::Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
                          Charset::Encoding encoding, int width)
    : configuration_(configuration),
      directoryView_(display),
      fileView_(display),
      grepView_(display),
      cutView_(display),
      cutContentView_(display),
      manView_(display),
      promptEditor_(display, encoding),
      directoryMode_(directoryView_, modeStack_, history, configuration),
      fileMode_(fileView_, history, encoding, width),
      grepMode_(grepView_, configuration),
      cutMode_(cutView_, cutModeStack_, configuration),
      manMode_(manView_, history, configuration, encoding, width),
      programMode_(keyInput, history) {
  // The bottom row is reserved for prompt text entry (search/cut/man
  // patterns, and a future slice's LOCATE query prompt) -- legacy's
  // equivalent is the mode-line row disp_window() left alone for
  // exactly this (see ContentView's own class comment on why that
  // row's rendering isn't ContentView's job either).
  int contentHeight = std::max(1, display.rows() - 1);
  directoryView_.setViewport(0, contentHeight);
  fileView_.setViewport(0, contentHeight);
  grepView_.setViewport(0, contentHeight);
  cutView_.setViewport(0, contentHeight);
  cutContentView_.setViewport(0, contentHeight);
  manView_.setViewport(0, contentHeight);
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
    default:
      return Command::None;
  }
}

bool Application::feedByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (promptKind_ != PromptKind::None) return feedPromptByte(byte);

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
      default:
        return false;  // unrecognized ^J-sequence in this slice (LOCATE/... is future work)
    }
  }

  if (!special && byte == kLeaderJ) {
    leaderJPending_ = true;
    return true;  // sequence in progress, not a no-op
  }

  return handleCommand(resolveCommand(byte, special));
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
      return false;
    case Command::Back:
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
    case Command::None:
      return false;
  }
  return false;
}

bool Application::openSelectedEntry() {
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry) return false;
  if (entry->isDirectory) return directoryMode_.openSelected();

  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;
  if (!fileMode_.loadFile(path)) return false;
  currentMode_ = ApplicationMode::File;
  return true;
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

bool Application::promptActive() const { return promptKind_ != PromptKind::None; }
const std::string &Application::promptText() const { return promptEditor_.text(); }

ApplicationMode Application::currentMode() const { return currentMode_; }
DirectoryMode &Application::directoryMode() { return directoryMode_; }
FileMode &Application::fileMode() { return fileMode_; }
GrepMode &Application::grepMode() { return grepMode_; }
CutMode &Application::cutMode() { return cutMode_; }
ManMode &Application::manMode() { return manMode_; }
ProgramMode &Application::programMode() { return programMode_; }

}  // namespace fm
