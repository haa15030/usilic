#include "directory_mode.hpp"

#include <optional>
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
  // match the real CUI. The right-edge "usilic v1.5b" watermark
  // (rightWatermark, see its own comment on ModeLineFields) is the
  // OTHER thing legacy shows on this screen specifically -- confirmed
  // by a real CUI screenshot, and by prev0 genuinely being the "no
  // previous-mode text" sentinel for T_dir in the C source (unlike
  // File mode, which populates that slot with real content instead).
  // "1.5b" mirrors tcb/src/version.h's own version tag -- this is the
  // same project, same version epoch, just a different implementation
  // language, so there's no reason to invent a separate number here.
  // Registered once here rather than re-set on every enterDirectory()
  // call since it's a live callback into currentDirectory_/
  // configuration_, not a snapshot.
  // 2026-08-15 addition: `trailing` changed from the "(ls)" program
  // indicator to the listing's own total byte count, matching the
  // SAME convention every other mode's own trailing field already
  // uses (contentByteCount()/ContentView::totalByteCount(), see its
  // own comment) -- a direct user request ("全モードのモードラインに
  // 「画面TOPからENDまでのバイト数」を追加してほしい") specifically
  // named Directory as one of the modes still missing this. All THREE
  // of trailing/trailing2/rightWatermark were already spoken for here
  // (byte count makes a fourth concept with nowhere obvious to go), so
  // "(ls)" now lives in trailing2 instead, as a fallback shown only
  // when searchAnnotation_ isn't set -- a search annotation (which
  // mode a LOCATE/GREP/CUT search actually came from) is more
  // situationally useful than the program name, which is almost always
  // just "(ls)" anyway. A judgment call, not legacy-source-verified
  // (this port has no legacy binary to compare a byte-count-showing
  // Directory screen against); flagged for correction if this doesn't
  // match what was actually meant.
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // See reportEditError()'s/clearEditError()'s own comments -- NOT
    // cleared here on read. A real user report (2026-08-17, GUI mode
    // only: "-cはOKだが-gだといつもの一瞬出て消える症状") found that
    // clearing it here (an earlier version of this code) made it
    // vanish almost instantly under -g specifically: -g's run loop
    // calls redraw() far more often than a keypress alone warrants
    // (mouse-hover tracking, the ~60Hz idle loop, ...), so a
    // read-once-and-clear flag got consumed by one of THOSE incidental
    // reads before the message was ever actually visible on screen --
    // -c never has this problem since nothing there calls redraw()
    // except in direct response to a keypress. Cleared instead by
    // clearEditError(), called once at the top of Application::
    // handleCommand() for every dispatched command -- so this message
    // now persists through any number of incidental redraws exactly the
    // way legacy's own echo_mess() does, and only actually goes away
    // once the user does something else.
    if (showEditError_) {
      ContentView::ModeLineFields fields;
      fields.name = "Invalid mode";
      fields.nameUnderlined = true;
      return fields;
    }
    const auto &ls = configuration_.commandFor(ConfiguredRole::Ls);
    std::string program = ls.argv.empty() ? "" : ls.argv.front();
    std::string programBadge = program.empty() ? "" : "(" + program + ")";
    std::string trailing2 = searchAnnotation_.empty() ? programBadge : searchAnnotation_;
    // fillTrailingToEdge stays keyed on searchAnnotation_ specifically
    // (NOT trailing2's own emptiness, which is now ALWAYS non-empty
    // once "(ls)" is filling in as its fallback) -- a real regression
    // found "(ls)" itself getting padded all the way to the row's
    // right edge, which left renderModeLine()'s own drawnLen tracking
    // already AT the column count by the time it got to rightWatermark,
    // silently dropping "usilic v1.5b" via that function's own
    // `drawnLen + watermark.size() <= cols` check -- exactly the
    // "usilic v1.5b" half of the =bd/=be report this round was
    // supposed to fix, not preserve.
    return {currentDirectory_, std::to_string(view_.totalByteCount()), /*fillTrailingToEdge=*/!searchAnnotation_.empty(),
            "usilic v1.5b", trailing2};
  });
}

bool DirectoryMode::enterDirectory(const std::string &path, bool consultHistory) {
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
    DirectoryEntry entry = parseLsLine(line);
    ContentView::Line contentLine;
    contentLine.text = filenameOnlyMode_ ? entry.name : line;
    if (!filenameOnlyMode_) contentLine.highlightStartColumn = entry.nameStartColumn;
    lines.push_back(std::move(contentLine));
    entries.push_back(std::move(entry));
  }

  // Trailing-slash normalization only (NOT canonicalization -- no
  // symlink resolution, no relative-to-absolute conversion): matches
  // the CUI mode-line's own directory display convention (see
  // CLAUDE.md's real-screenshot comparison), and is idempotent/safe
  // for callers that already pass a trailing slash (every existing
  // caller does).
  currentDirectory_ = path;
  if (!currentDirectory_.empty() && currentDirectory_.back() != '/') currentDirectory_ += '/';
  searchAnnotation_.clear();  // a fresh `ls` invalidates the old search -- see setSearchAnnotation()'s own comment
  entries_ = std::move(entries);
  markedIndices_.clear();  // marks are scoped to the listing they were made in; a fresh `ls` invalidates them
  view_.setLines(std::move(lines));

  // K-48 (^X^D, "DIR: Select directory name")'s own data source --
  // every successfully-listed directory counts as "accessed" (HELP.jp
  // 2.9's own broad wording), matching History::pushRecent()'s
  // existing first-occurrence-wins/capped-size behavior (see its own
  // comment) -- no extra bookkeeping needed here.
  history_.pushRecent(RingCategory::RecentDirectory, currentDirectory_);

  auto pos = consultHistory ? history_.positionFor(PositionCategory::Directory, currentDirectory_) : std::nullopt;
  if (pos) {
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

void DirectoryMode::setSearchAnnotation(const std::string &text) { searchAnnotation_ = text; }

void DirectoryMode::reportEditError() { showEditError_ = true; }

void DirectoryMode::clearEditError() { showEditError_ = false; }

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

bool DirectoryMode::descendTo(const std::string &target) {
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
  return descendTo(target);
}

bool DirectoryMode::goToParent() {
  std::string target = parentDirectory(currentDirectory_);
  if (target == currentDirectory_) return false;  // already at root ("/"), matching legacy's own no-op there
  return descendTo(target);
}

bool DirectoryMode::goBack() {
  if (modeStack_.empty()) return false;
  saveCurrentCursorPosition();
  auto frame = modeStack_.pop();
  if (!frame) return false;
  if (modeStack_.isFloor(*frame)) {
    if (frame->previousDirectory.empty()) return false;
    std::string target = frame->previousDirectory;
    if (!enterDirectory(target)) return false;
    // Swap directory/previousDirectory and re-push -- see this
    // function's own declaration comment on why (a genuine toggle, not
    // a one-shot pop).
    modeStack_.pushFloor(target, frame->directory, nullptr, 0);
    return true;
  }
  return enterDirectory(frame->directory);
}

bool DirectoryMode::canGoBack() const { return !modeStack_.empty(); }

void DirectoryMode::resetNavigationToFloor(const std::string &previousDirectory) {
  modeStack_.pushFloor(currentDirectory_, previousDirectory, nullptr, 0);
}

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
  // searchable (arrow-stripped), not the raw `line` -- a symlink's " ->
  // target" portion belongs in lsLine (the raw display row) but NOT in
  // `name` (used to build real filesystem paths, e.g. openSelectedEntry()'s
  // own `path += entry->name`), which the original `line.substr()` here
  // never accounted for.
  if (nameStart < searchable.size()) entry.name = searchable.substr(nameStart);
  entry.isDirectory = !line.empty() && line[0] == 'd';
  // 2026-08-17 fix: `ls -alF`'s own trailing type-indicator character
  // ('*' executable, '@' symlink, '=' socket, '%' whiteout, '|' FIFO --
  // '/' for directories is handled separately below) used to stay
  // embedded in `name` verbatim, silently breaking every path built
  // from a non-directory entry with one of these types. Harmless for a
  // DIRECTORY's own trailing '/' (opendir()/stat() treat "dir" and
  // "dir/" identically, and openSelected() actually DEPENDS on it
  // staying present -- see its own `entry->name == "./"` checks -- so
  // directories are deliberately left untouched here). A real
  // reproduction (^J^L search -> open a LOCATE result -> ^J^K, landing
  // in a directory full of executables -- exactly how a real
  // .app/Contents bundle looks, which no synthetic test fixture in
  // this port ever chmod +x's) found Enter on ANY such entry silently
  // doing nothing: the constructed path (".../LicensingSettings.xml*")
  // doesn't exist on disk, so fileMode_.loadFile() failed and
  // openSelectedEntry() returned false with no visible change --
  // exactly the "^J^Kを使うと以後反応しなくなる" symptom reported,
  // previously misdiagnosed as broader state corruption.
  if (!entry.isDirectory && !entry.name.empty()) {
    char last = entry.name.back();
    if (last == '*' || last == '@' || last == '=' || last == '%' || last == '|') entry.name.pop_back();
  }
  entry.nameStartColumn = static_cast<int>(nameStart);
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

void DirectoryMode::markAll() {
  markedIndices_.clear();
  for (int i = 0; i < static_cast<int>(entries_.size()); i++) markedIndices_.insert(i);
}

std::vector<DirectoryEntry> DirectoryMode::targetEntries() const {
  std::vector<DirectoryEntry> result;
  if (!markedIndices_.empty()) {
    for (int idx : markedIndices_) result.push_back(entries_[idx]);  // std::set iterates in ascending order
    return result;
  }
  if (const DirectoryEntry *entry = selectedEntry()) result.push_back(*entry);
  return result;
}

void DirectoryMode::setFilenameOnlyMode(bool enabled) {
  filenameOnlyMode_ = enabled;
  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  for (const auto &entry : entries_) {
    ContentView::Line line;
    line.text = enabled ? entry.name : entry.lsLine;
    if (!enabled) line.highlightStartColumn = entry.nameStartColumn;
    lines.push_back(std::move(line));
  }
  view_.setLines(std::move(lines));
  view_.redraw();
}

bool DirectoryMode::filenameOnlyMode() const { return filenameOnlyMode_; }

}  // namespace fm
