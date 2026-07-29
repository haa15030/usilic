#include "application.hpp"

#include <algorithm>

namespace fm {

Application::Application(Display &display, History &history, const Configuration &configuration,
                          Charset::Encoding encoding, int width)
    : directoryView_(display),
      fileView_(display),
      directoryMode_(directoryView_, modeStack_, history, configuration),
      fileMode_(fileView_, history, encoding, width) {
  int height = std::max(1, display.rows());
  directoryView_.setViewport(0, height);
  fileView_.setViewport(0, height);
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
// to m_page(); M_cr (^M) to m_cr(); M_bl (^G) to m_bl().
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
    case 0x0d:  // ^M / Enter
    case 0x0a:
      return Command::Enter;
    case 0x07:  // ^G
      return Command::Back;
    default:
      return Command::None;
  }
}

bool Application::handleCommand(Command command) {
  ContentView &view = (currentMode_ == ApplicationMode::Directory) ? directoryView_ : fileView_;
  switch (command) {
    case Command::MoveUp:
      view.moveUp();
      return true;
    case Command::MoveDown:
      view.moveDown();
      return true;
    case Command::PageUp:
      view.pageUp();
      return true;
    case Command::PageDown:
      view.pageDown();
      return true;
    case Command::Enter:
      if (currentMode_ != ApplicationMode::Directory) return false;
      return openSelectedEntry();
    case Command::Back:
      if (currentMode_ == ApplicationMode::File) return returnToDirectory();
      return directoryMode_.goBack();
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

bool Application::returnToDirectory() {
  fileMode_.saveCursorPosition();
  currentMode_ = ApplicationMode::Directory;
  directoryView_.redraw();
  return true;
}

ApplicationMode Application::currentMode() const { return currentMode_; }
DirectoryMode &Application::directoryMode() { return directoryMode_; }
FileMode &Application::fileMode() { return fileMode_; }

}  // namespace fm
