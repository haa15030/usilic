#include "file_mode.hpp"

#include <fstream>
#include <sstream>

#include "pipe_buffer.hpp"

namespace fm {

FileMode::FileMode(ContentView &view, History &history, Charset::Encoding encoding, int width)
    : view_(view), history_(history), encoding_(encoding), width_(width) {}

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
  for (const auto &styled : pipe.lines()) lines.push_back({styled.text});
  return lines;
}

bool FileMode::loadFile(const std::string &path) {
  std::ifstream probe(path, std::ios::binary);
  if (!probe) return false;
  probe.close();

  view_.setLines(readAndWrap(path));
  currentFile_ = path;
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
  return true;
}

const std::string &FileMode::currentFile() const { return currentFile_; }

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
