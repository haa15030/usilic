#include "file_mode.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "pipe_buffer.hpp"

namespace fm {

namespace {

// Groups `value`'s decimal digits into 3s from the right, space-
// separated (e.g. 1108009 -> "1 108 009") -- added 2026-08-10, a real
// request ("モードラインのファイルサイズ表示...区切りの空白入れて
// みて") after a large file's byte count (CLAUDE.md, over 1MB) showed
// up as an unbroken run of 7 digits, hard to read at a glance. Legacy
// tcb never grouped this (plain %ld in disp_window()), so this is a
// genuinely new convenience, not a ported behavior. A half-width space
// (not a comma) per the request's own wording.
std::string groupDigits(std::uintmax_t value) {
  std::string digits = std::to_string(value);
  std::string grouped;
  int fromEnd = static_cast<int>(digits.size());
  for (size_t i = 0; i < digits.size(); i++) {
    if (i > 0 && (fromEnd - static_cast<int>(i)) % 3 == 0) grouped.push_back(' ');
    grouped.push_back(digits[i]);
  }
  return grouped;
}

}  // namespace

FileMode::FileMode(ContentView &view, History &history, Charset::Encoding encoding, int width)
    : view_(view), history_(history), encoding_(encoding), width_(width) {
  // Ported from put_str()'s own f_mark logic (see
  // ContentView::setCursorHighlightEnabled()'s comment): plain file
  // content never gets a whole-line reverse-video cursor highlight in
  // the real CUI, unlike Directory mode's listing (which this port
  // had already been highlighting, correctly, well before this flag
  // existed) -- confirmed by a real CUI/GUI side-by-side screenshot.
  view_.setCursorHighlightEnabled(false);

  // Mode-line fields, ported from disp_window()'s own field layout for
  // the pager case (name0 not starting with '/' takes the "copy as-
  // is" branch, see its comment): the bare filename on the left (NOT
  // the full path -- that's what the CUI actually shows), and the
  // file's byte size plus its containing directory on the right.
  // Registered once here rather than re-set on every loadFile() call
  // since it's a live callback into currentFile_, not a snapshot.
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (currentFile_.empty()) return {};
    std::filesystem::path path(currentFile_);
    std::string directory = path.parent_path().string();
    if (!directory.empty() && directory.back() != '/') directory += '/';

    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    std::string trailing = ec ? directory : groupDigits(size) + " " + directory;
    // File mode's trailing field is legacy's padded-to-edge style, see
    // ModeLineFields::fillTrailingToEdge's own comment. No
    // rightWatermark here -- see its own comment on why File mode
    // never shows it.
    return {path.filename().string(), trailing, /*fillTrailingToEdge=*/true, /*rightWatermark=*/""};
  });
}

std::vector<ContentView::Line> FileMode::readAndWrap(const std::string &path) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!in.good() && !in.eof()) return {};
  std::string content = buffer.str();

  PipeBuffer pipe(encoding_, width_, 64L * 1024 * 1024);
  pipe.feed(content);
  pipe.flush();

  std::vector<ContentView::Line> lines;
  lines.reserve(pipe.lines().size());
  // styled.attributes (2026-08-05): PipeBuffer's own overstrike
  // detection (nroff-style bold/underline) already produces this --
  // previously discarded here, the one and only place FileMode turns
  // PipeBuffer's StyledLines into what actually reaches the screen.
  for (const auto &styled : pipe.lines()) lines.push_back({styled.text, std::nullopt, std::nullopt, styled.attributes});
  return lines;
}

bool FileMode::loadFile(const std::string &path) {
  std::ifstream probe(path, std::ios::binary);
  if (!probe) return false;
  probe.close();

  view_.setLines(readAndWrap(path));
  currentFile_ = path;
  // K-46 (^X^F, "FILE: Select file name")'s own data source -- see
  // DirectoryMode::enterDirectory()'s identical pushRecent() call for
  // K-48's own RecentDirectory counterpart.
  history_.pushRecent(RingCategory::RecentFile, currentFile_);
  if (auto pos = history_.positionFor(PositionCategory::File, currentFile_)) {
    int idx = pos->row;
    int last = static_cast<int>(view_.lines().size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  // A real cursor at the current line, since plain file content has
  // no whole-line highlight to indicate it otherwise (see
  // ContentView::syncVisibleCursor()'s own comment) -- a 2026-08-04
  // user request: the current line still needs to be visibly and
  // programmatically knowable in both -c/-g (launching an editor at
  // the current line, and a future COPY mode's row+column selection,
  // both rely on a real cursor position).
  view_.syncVisibleCursor();
  return true;
}

bool FileMode::reflow(int newWidth) {
  if (currentFile_.empty()) return false;
  width_ = newWidth;
  int oldCursor = view_.cursorIndex();

  view_.setLines(readAndWrap(currentFile_));
  int last = static_cast<int>(view_.lines().size()) - 1;
  if (oldCursor < 0) oldCursor = 0;
  if (oldCursor > last) oldCursor = (last < 0) ? 0 : last;
  view_.setCursorIndex(oldCursor);
  view_.redraw();
  view_.syncVisibleCursor();
  return true;
}

const std::string &FileMode::currentFile() const { return currentFile_; }

void FileMode::setEncoding(Charset::Encoding encoding) { encoding_ = encoding; }

void FileMode::saveCursorPosition() {
  if (currentFile_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(view_.lines().size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::File, currentFile_, pos);
}

}  // namespace fm
