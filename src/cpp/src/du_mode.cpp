#include "du_mode.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "child_process.hpp"

namespace fm {

DuMode::DuMode(ContentView &view, History &history, const Configuration &configuration)
    : view_(view), history_(history), configuration_(configuration) {}

std::string DuMode::formatEntryLine(const DuEntry &entry) {
  return std::to_string(entry.sizeKb) + "K\t" + entry.path;
}

bool DuMode::loadUsage(const std::string &path) {
  ProgramCommand du = configuration_.commandFor(ConfiguredRole::Du);
  if (du.argv.empty()) return false;

  std::vector<std::string> argv = du.argv;
  argv.push_back(path);
  ChildProcess::Result result = ChildProcess::run(argv);
  if (!result.ok) return false;

  std::vector<DuEntry> entries;
  std::vector<ContentView::Line> lines;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    std::size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;

    DuEntry entry;
    entry.sizeKb = std::atol(line.substr(0, tab).c_str());
    entry.path = line.substr(tab + 1);
    std::error_code ec;
    entry.isDirectory = std::filesystem::is_directory(entry.path, ec) && !ec;
    entries.push_back(entry);
    lines.push_back({formatEntryLine(entry), std::nullopt, std::nullopt, {}});
  }

  currentPath_ = path;
  entries_ = std::move(entries);
  view_.setLines(std::move(lines));

  if (auto pos = history_.positionFor(PositionCategory::Screen, currentPath_)) {
    int idx = pos->row;
    int last = static_cast<int>(entries_.size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  return true;
}

const std::string &DuMode::currentPath() const { return currentPath_; }
const std::vector<DuEntry> &DuMode::entries() const { return entries_; }

const DuEntry *DuMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

void DuMode::saveCursorPosition() {
  if (currentPath_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(entries_.size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::Screen, currentPath_, pos);
}

}  // namespace fm
