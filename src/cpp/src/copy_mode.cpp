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
// ためと、矩形マークの列範囲を行ごとのバイトオフセットへ変換する
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
  rectangleMark_ = false;
  markAnchorRow_ = 0;
  markAnchorByte_ = 0;
  view_.setCursorHighlightEnabled(false);
  render();
}

void CopyMode::moveUp() {
  if (lines_.empty() || cursorRow_ == 0) return;
  int col = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
  cursorRow_--;
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, col);
  render();
}

void CopyMode::moveDown() {
  if (lines_.empty() || cursorRow_ + 1 >= static_cast<int>(lines_.size())) return;
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
  cursorRow_ = std::clamp(row, 0, static_cast<int>(lines_.size()) - 1);
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, column);
  render();
}

void CopyMode::startMark() {
  if (lines_.empty()) return;
  markActive_ = true;
  rectangleMark_ = false;
  markAnchorRow_ = cursorRow_;
  markAnchorByte_ = cursorByte_;
  render();
}

void CopyMode::startOrUpgradeRectangleMark() {
  if (lines_.empty()) return;
  if (!markActive_) {
    markActive_ = true;
    markAnchorRow_ = cursorRow_;
    markAnchorByte_ = cursorByte_;
  }
  rectangleMark_ = true;
  render();
}

void CopyMode::confirmMark() {
  if (lines_.empty() || !markActive_) return;
  // (loRow,loByte)-(hiRow,hiByte)を読み順に正規化する――
  // confirmMark()自身の宣言コメント(copy_mode.hpp)参照。
  bool anchorFirst =
      markAnchorRow_ < cursorRow_ || (markAnchorRow_ == cursorRow_ && markAnchorByte_ <= cursorByte_);
  int loRow = anchorFirst ? markAnchorRow_ : cursorRow_;
  int hiRow = anchorFirst ? cursorRow_ : markAnchorRow_;

  std::vector<std::string> parts;
  bool anyNonEmpty = false;
  if (rectangleMark_) {
    // confirmMark()自身の宣言コメント参照(矩形マーク): 列範囲は
    // アンカー/カーソルそれぞれの「表示カラム」のmin/maxで決め、
    // 各行ごとにbyteOffsetForDisplayColumn()でバイトオフセットへ
    // 変換し直す――行によって文字幅(全角/半角混在等)が異なり得る
    // ため、1つの行で求めたバイトオフセットを他の行にそのまま使い
    // 回すことはできない。
    int anchorCol = displayColumnFor(lines_[markAnchorRow_].text, markAnchorByte_);
    int cursorCol = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
    int loCol = std::min(anchorCol, cursorCol);
    int hiCol = std::max(anchorCol, cursorCol);
    for (int row = loRow; row <= hiRow; row++) {
      const std::string &text = lines_[row].text;
      int rowLo = byteOffsetForDisplayColumn(text, loCol);
      int rowHi = byteOffsetForDisplayColumn(text, hiCol);
      if (rowHi > rowLo) anyNonEmpty = true;
      parts.push_back(text.substr(rowLo, std::max(0, rowHi - rowLo)));
    }
  } else {
    int loByte = anchorFirst ? markAnchorByte_ : cursorByte_;
    int hiByte = anchorFirst ? cursorByte_ : markAnchorByte_;
    for (int row = loRow; row <= hiRow; row++) {
      const std::string &text = lines_[row].text;
      int rowLo = (row == loRow) ? loByte : 0;
      int rowHi = (row == hiRow) ? hiByte : static_cast<int>(text.size());
      if (rowHi > rowLo) anyNonEmpty = true;
      parts.push_back(text.substr(rowLo, std::max(0, rowHi - rowLo)));
    }
  }
  if (anyNonEmpty) {
    std::string joined;
    for (const auto &part : parts) {
      if (!joined.empty()) joined += "\n";
      joined += part;
    }
    clipboardText_ = joined;
  }
  cancelMark();
}

bool CopyMode::markActive() const { return markActive_; }

bool CopyMode::rectangleMarkActive() const { return markActive_ && rectangleMark_; }

const std::string &CopyMode::clipboardText() const { return clipboardText_; }

void CopyMode::cancelMark() {
  markActive_ = false;
  rectangleMark_ = false;
  markAnchorRow_ = 0;
  markAnchorByte_ = 0;
  render();
}

void CopyMode::render() {
  std::vector<ContentView::Line> rendered = lines_;
  if (markActive_ && !rendered.empty()) {
    bool anchorFirst =
        markAnchorRow_ < cursorRow_ || (markAnchorRow_ == cursorRow_ && markAnchorByte_ <= cursorByte_);
    int loRow = std::clamp(anchorFirst ? markAnchorRow_ : cursorRow_, 0, static_cast<int>(rendered.size()) - 1);
    int hiRow = std::clamp(anchorFirst ? cursorRow_ : markAnchorRow_, 0, static_cast<int>(rendered.size()) - 1);
    if (rectangleMark_) {
      // confirmMark()自身の矩形マークのコメント参照――ライブハイライト
      // も同じ列範囲(表示カラムのmin/max)を、行ごとにバイトオフセット
      // へ変換し直しながら描く。
      int anchorCol = displayColumnFor(lines_[markAnchorRow_].text, markAnchorByte_);
      int cursorCol = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
      int loCol = std::min(anchorCol, cursorCol);
      int hiCol = std::max(anchorCol, cursorCol);
      for (int row = loRow; row <= hiRow; row++) {
        int rowLo = byteOffsetForDisplayColumn(rendered[row].text, loCol);
        int rowHi = byteOffsetForDisplayColumn(rendered[row].text, hiCol);
        if (rowHi > rowLo) rendered[row].attributes.push_back({rowLo, rowHi, Display::Attribute::Standout});
      }
    } else {
      int loByte = anchorFirst ? markAnchorByte_ : cursorByte_;
      int hiByte = anchorFirst ? cursorByte_ : markAnchorByte_;
      for (int row = loRow; row <= hiRow; row++) {
        int rowLo = (row == loRow) ? loByte : 0;
        int rowHi = (row == hiRow) ? hiByte : static_cast<int>(rendered[row].text.size());
        if (rowHi > rowLo) rendered[row].attributes.push_back({rowLo, rowHi, Display::Attribute::Standout});
      }
    }
  }
  view_.setLines(std::move(rendered));
  if (!lines_.empty()) view_.setCursorIndex(cursorRow_);
  // render()自身の宣言コメント参照(copy_mode.hpp): 矩形マーク中は
  // モードラインの位置フィールドに"Rectangle-Mark"を表示し、それ
  // 以外は常にstartCopy()自身が設定する"---"のプレースホルダーへ
  // 戻す(2026-09-07追加、直接のユーザー要望)。
  view_.setPositionTextOverride(rectangleMark_ ? "Rectangle-Mark" : "---");
  view_.redraw();
  int column = lines_.empty() ? 0 : displayColumnFor(lines_[cursorRow_].text, cursorByte_);
  view_.setContentCursorColumn(column);
}

}  // namespace fm
