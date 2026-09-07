#include "copy_mode.hpp"

#include <algorithm>

#include "utf8.hpp"

namespace fm {

namespace {

int nextCharBoundary(const std::string &text, int byteOffset) {
  if (byteOffset >= static_cast<int>(text.size())) return byteOffset;
  int len = Utf8::sequence_length(static_cast<unsigned char>(text[byteOffset]));
  return std::min(byteOffset + len, static_cast<int>(text.size()));
}

int prevCharBoundary(const std::string &text, int byteOffset) {
  // 0から歩いてbyteOffsetの直前の境界を記憶しておく――前方スキャン
  // 無しにUTF-8を逆方向に歩くことはできないため。
  int i = 0, prev = 0;
  while (i < byteOffset && i < static_cast<int>(text.size())) {
    prev = i;
    int len = Utf8::sequence_length(static_cast<unsigned char>(text[i]));
    i += std::min(len, static_cast<int>(text.size()) - i);
  }
  return prev;
}

int displayColumnFor(const std::string &text, int byteOffset) {
  int col = 0, i = 0;
  while (i < byteOffset && i < static_cast<int>(text.size())) {
    int len = Utf8::sequence_length(static_cast<unsigned char>(text[i]));
    len = std::min(len, static_cast<int>(text.size()) - i);
    col += Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + i, len);
    i += len;
  }
  return col;
}

// displayColumnFor()の逆: 表示カラムが`targetColumn`を超えない範囲で
// 最も近いバイトオフセット。上下移動をまたいで水平位置を保持する
// ために使う。
int byteOffsetForDisplayColumn(const std::string &text, int targetColumn) {
  int col = 0, i = 0;
  while (i < static_cast<int>(text.size())) {
    int len = Utf8::sequence_length(static_cast<unsigned char>(text[i]));
    len = std::min(len, static_cast<int>(text.size()) - i);
    int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + i, len);
    if (col + width > targetColumn) return i;
    col += width;
    i += len;
  }
  return static_cast<int>(text.size());
}

}  // namespace

CopyMode::CopyMode(ContentView &view) : view_(view) {}

void CopyMode::loadSnapshot(std::vector<ContentView::Line> lines, int initialRow) {
  lines_.clear();
  lines_.reserve(lines.size());
  for (auto &line : lines) {
    ContentView::Line plain;
    plain.text = std::move(line.text);
    plain.attributes = std::move(line.attributes);
    lines_.push_back(std::move(plain));
  }
  cursorRow_ = (initialRow >= 0 && initialRow < static_cast<int>(lines_.size())) ? initialRow : 0;
  cursorByte_ = 0;
  markActive_ = false;
  markAnchorByte_ = 0;
  view_.setCursorHighlightEnabled(false);
  render();
}

void CopyMode::moveUp() {
  if (lines_.empty() || markActive_ || cursorRow_ == 0) return;
  int col = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
  cursorRow_--;
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, col);
  render();
}

void CopyMode::moveDown() {
  if (lines_.empty() || markActive_ || cursorRow_ + 1 >= static_cast<int>(lines_.size())) return;
  int col = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
  cursorRow_++;
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, col);
  render();
}

void CopyMode::moveLeft() {
  if (lines_.empty() || cursorByte_ == 0) return;
  cursorByte_ = prevCharBoundary(lines_[cursorRow_].text, cursorByte_);
  render();
}

void CopyMode::moveRight() {
  if (lines_.empty()) return;
  const std::string &text = lines_[cursorRow_].text;
  if (cursorByte_ >= static_cast<int>(text.size())) return;
  cursorByte_ = nextCharBoundary(text, cursorByte_);
  render();
}

void CopyMode::moveToLineStart() {
  if (lines_.empty()) return;
  cursorByte_ = 0;
  render();
}

void CopyMode::moveToLineEnd() {
  if (lines_.empty()) return;
  cursorByte_ = static_cast<int>(lines_[cursorRow_].text.size());
  render();
}

void CopyMode::placeCursorAt(int row, int column) {
  if (lines_.empty()) return;
  if (!markActive_) cursorRow_ = std::clamp(row, 0, static_cast<int>(lines_.size()) - 1);
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, column);
  render();
}

void CopyMode::startMark() {
  if (lines_.empty()) return;
  markActive_ = true;
  markAnchorByte_ = cursorByte_;
  render();
}

void CopyMode::confirmMark() {
  if (lines_.empty() || !markActive_) return;
  int lo = std::min(markAnchorByte_, cursorByte_);
  int hi = std::max(markAnchorByte_, cursorByte_);
  if (hi > lo) clipboardText_ = lines_[cursorRow_].text.substr(lo, hi - lo);
  cancelMark();
}

bool CopyMode::markActive() const { return markActive_; }

const std::string &CopyMode::clipboardText() const { return clipboardText_; }

void CopyMode::cancelMark() {
  markActive_ = false;
  markAnchorByte_ = 0;
  render();
}

void CopyMode::render() {
  std::vector<ContentView::Line> rendered = lines_;
  if (markActive_ && cursorRow_ >= 0 && cursorRow_ < static_cast<int>(rendered.size())) {
    int lo = std::min(markAnchorByte_, cursorByte_);
    int hi = std::max(markAnchorByte_, cursorByte_);
    if (hi > lo) rendered[cursorRow_].attributes.push_back({lo, hi, Display::Attribute::Standout});
  }
  view_.setLines(std::move(rendered));
  if (!lines_.empty()) view_.setCursorIndex(cursorRow_);
  view_.redraw();
  int column = lines_.empty() ? 0 : displayColumnFor(lines_[cursorRow_].text, cursorByte_);
  view_.setContentCursorColumn(column);
}

}  // namespace fm
