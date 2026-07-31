#include "directory_mode.hpp"

#include <sstream>

#include "child_process.hpp"

namespace fm {

DirectoryMode::DirectoryMode(ContentView &view, ModeStack &modeStack, History &history,
                              const Configuration &configuration)
    : view_(view), modeStack_(modeStack), history_(history), configuration_(configuration) {
  // Mode-line fields, ported from disp_window()'s own field layout for
  // the T_dir/file-selector case (name0[i]=='/' && *prev0==K_z keeps
  // the whole absolute path as-is, see its comment): the current
  // directory's full path on the left, and the listing command (e.g.
  // "(ls)") on the right -- NOT the selected entry's name, which was
  // this port's own invented design before this was corrected to
  // match the real CUI. Registered once here rather than re-set on
  // every enterDirectory() call since it's a live callback into
  // currentDirectory_/configuration_, not a snapshot.
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    const auto &ls = configuration_.commandFor(ConfiguredRole::Ls);
    std::string program = ls.argv.empty() ? "" : ls.argv.front();
    return {currentDirectory_, program.empty() ? "" : "(" + program + ")"};
  });
}

bool DirectoryMode::enterDirectory(const std::string &path) {
  ProgramCommand ls = configuration_.commandFor(ConfiguredRole::Ls);
  if (ls.argv.empty()) return false;

  std::vector<std::string> argv = ls.argv;
  argv.push_back(path);

  ChildProcess::Options options;
  // Ported from fork_bin()'s LC_ALL=C forcing for exactly this
  // reason: parseLsLine()'s month-name anchor only recognizes English
  // abbreviations (see its own comment, ported from scroll.c's
  // get_col()). Without this, a non-English locale's `ls` falls back
  // to the last-space heuristic and mis-splits any filename
  // containing a space.
  options.forceCLocale = true;
  ChildProcess::Result result = ChildProcess::run(argv, options);
  if (!result.ok) return false;

  std::vector<DirectoryEntry> entries;
  std::vector<ContentView::Line> lines;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    if (line.rfind("total ", 0) == 0) continue;  // `ls -l`'s block-count summary line, not an entry
    entries.push_back(parseLsLine(line));
    lines.push_back({line});
  }

  // Trailing-slash normalization only (NOT canonicalization -- no
  // symlink resolution, no relative-to-absolute conversion): matches
  // the CUI mode-line's own directory display convention (see
  // CLAUDE.md's real-screenshot comparison), and is idempotent/safe
  // for callers that already pass a trailing slash (every existing
  // caller does).
  currentDirectory_ = path;
  if (!currentDirectory_.empty() && currentDirectory_.back() != '/') currentDirectory_ += '/';
  entries_ = std::move(entries);
  markedIndices_.clear();  // marks are scoped to the listing they were made in; a fresh `ls` invalidates them
  view_.setLines(std::move(lines));

  if (auto pos = history_.positionFor(PositionCategory::Directory, currentDirectory_)) {
    int idx = pos->row;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(entries_.size())) idx = static_cast<int>(entries_.size()) - 1;
    view_.setCursorIndex(idx < 0 ? 0 : idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  return true;
}

const std::string &DirectoryMode::currentDirectory() const { return currentDirectory_; }
const std::vector<DirectoryEntry> &DirectoryMode::entries() const { return entries_; }

const DirectoryEntry *DirectoryMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

std::string DirectoryMode::parentDirectory(const std::string &dir) {
  if (dir.empty() || dir == "/") return "/";
  std::string trimmed = dir;
  if (trimmed.back() == '/') trimmed.pop_back();
  std::size_t slash = trimmed.find_last_of('/');
  if (slash == std::string::npos) return "/";
  return trimmed.substr(0, slash + 1);
}

bool DirectoryMode::openSelected() {
  const DirectoryEntry *entry = selectedEntry();
  if (!entry || !entry->isDirectory) return false;
  if (entry->name == "./") return false;

  std::string target;
  if (entry->name == "../") {
    target = parentDirectory(currentDirectory_);
  } else {
    target = currentDirectory_;
    if (!target.empty() && target.back() != '/') target += '/';
    target += entry->name;
  }

  saveCurrentCursorPosition();
  ModeStackFrame frame;
  frame.directory = currentDirectory_;
  frame.name = currentDirectory_;
  modeStack_.push(std::move(frame));

  if (!enterDirectory(target)) {
    // Couldn't descend (permission denied, race with deletion, ...):
    // undo the push and stay put, matching legacy's own
    // access(p, R_OK)-fails-stay-in-T_dir behavior.
    modeStack_.pop();
    return false;
  }
  return true;
}

bool DirectoryMode::goBack() {
  if (modeStack_.empty()) return false;
  saveCurrentCursorPosition();
  auto frame = modeStack_.pop();
  if (!frame) return false;
  return enterDirectory(frame->directory);
}

bool DirectoryMode::canGoBack() const { return !modeStack_.empty(); }

void DirectoryMode::saveCurrentCursorPosition() {
  if (currentDirectory_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(entries_.size());
  pos.top = 0;  // ContentView recomputes a visible viewport from cursorIndex on restore; not load-bearing here
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::Directory, currentDirectory_, pos);
}

// Ported from scroll.c's get_col(): `ls -alF` output's date field
// ("Mon DD HH:MM " for recent files, "Mon DD  YYYY " for older ones)
// is always exactly 13 bytes from the month abbreviation to the start
// of the filename, and the month abbreviation is a reliable,
// locale-independent anchor (given forceCLocale above) -- unlike
// naively taking the last space in the line, which breaks the moment
// the filename itself contains a space (e.g. a Finder/QuickTime
// export like "2024-09-14 12.13.08.mp4"). Falls back to the last
// space if no recognizable date field is found (matching get_col()'s
// own fallback, e.g. for non-`ls -alF`-shaped lines).
DirectoryEntry DirectoryMode::parseLsLine(const std::string &line) const {
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  std::string searchable = line;
  std::size_t arrow = searchable.find(" -> ");  // symlink target -- not part of the name
  if (arrow != std::string::npos) searchable.erase(arrow);

  std::size_t nameStart = std::string::npos;
  for (const char *month : kMonths) {
    std::size_t pos = 0;
    while ((pos = searchable.find(month, pos)) != std::string::npos) {
      bool leftBoundaryOk = (pos == 0 || searchable[pos - 1] == ' ');
      bool rightBoundaryOk = (pos + 3 < searchable.size() && searchable[pos + 3] == ' ');
      if (leftBoundaryOk && rightBoundaryOk && searchable.size() > pos + 13) {
        std::size_t candidate = pos + 13;
        if (nameStart == std::string::npos || candidate < nameStart) nameStart = candidate;
      }
      pos += 1;
    }
  }
  if (nameStart == std::string::npos) {
    std::size_t lastSpace = searchable.find_last_of(' ');
    nameStart = (lastSpace == std::string::npos) ? 0 : lastSpace + 1;
  }

  DirectoryEntry entry;
  entry.lsLine = line;
  if (nameStart < line.size()) entry.name = line.substr(nameStart);
  entry.isDirectory = !line.empty() && line[0] == 'd';
  return entry;
}

void DirectoryMode::toggleMark(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  auto it = markedIndices_.find(index);
  if (it != markedIndices_.end())
    markedIndices_.erase(it);
  else
    markedIndices_.insert(index);
}

bool DirectoryMode::isMarked(int index) const { return markedIndices_.count(index) > 0; }

std::size_t DirectoryMode::markedCount() const { return markedIndices_.size(); }

void DirectoryMode::clearMarks() { markedIndices_.clear(); }

std::vector<DirectoryEntry> DirectoryMode::targetEntries() const {
  std::vector<DirectoryEntry> result;
  if (!markedIndices_.empty()) {
    for (int idx : markedIndices_) result.push_back(entries_[idx]);  // std::set iterates in ascending order
    return result;
  }
  if (const DirectoryEntry *entry = selectedEntry()) result.push_back(*entry);
  return result;
}

}  // namespace fm
