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

void CopyMode::loadSnapshot(std::vector<ContentView::Line> lines, int initialRow,
                             std::optional<int> initialTopIndex) {
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
  islands_.clear();  // 直接のユーザー報告⑭自身のコメント参照――新しいCOPYセッションは空から始まる
  view_.setCursorHighlightEnabled(false);
  // loadSnapshot()自身の`initialTopIndex`のコメント参照。この時点では
  // まだview_のlines_が古い(または空の)ままなので、ここで直接
  // view_.setTopIndex()を呼んでも、その古いlines_基準のmaxTopで
  // 誤ってクランプされてしまう――render()自身が、新しいlines_を
  // view_.setLines()で反映させた「直後」に適用できるよう、
  // pendingTopIndexOverride_へ一旦預けておく。
  pendingTopIndexOverride_ = initialTopIndex;
  render();
}

void CopyMode::moveUp() {
  if (lines_.empty() || cursorRow_ == 0) return;
  int col = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
  cursorRow_--;
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, col);
  // 2026-09-07、直接のユーザー報告⑥("コピーモード発動後の^N^Pで
  // ページ送りが起きた場合、通常の動作と違うので同じにして"): 何も
  // しなければ、この後のrender()自身のview_.setCursorIndex()呼び出し
  // (内部でclampViewport()を呼ぶ)が、境界を超えた瞬間にtopIndex_を
  // 「cursorRow_-height+1」へ一気にジャンプさせてしまう――ページ全体
  // が瞬時に切り替わる見た目になり、ContentView::moveUp()自身の
  // 「境界を超えたらtopIndex_をちょうど1行だけ動かす」というスムーズ
  // なスクロールとは異なっていた。ここで先に1行分だけ動かしておく
  // ことで、後続のclampViewport()の不変条件が既に満たされ、no-opに
  // なる。
  if (cursorRow_ < view_.topIndex()) view_.setTopIndex(view_.topIndex() - 1);
  render();
}

void CopyMode::moveDown() {
  if (lines_.empty() || cursorRow_ + 1 >= static_cast<int>(lines_.size())) return;
  int col = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
  cursorRow_++;
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, col);
  // moveUp()自身のコメント参照。
  if (cursorRow_ >= view_.topIndex() + view_.height()) view_.setTopIndex(view_.topIndex() + 1);
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

void CopyMode::beginOrExtendFromMouse(int row, int column) {
  if (lines_.empty()) return;
  // 判定は必ず「動かす前」の状態で行う――bankCurrentMarkIfExtended()
  // 自身が(バンクした場合)アンカーをその場でカーソル位置へ置き直して
  // しまうため、後で調べると「実質的な範囲が無かった」ように見えて
  // しまう。
  bool wasActiveBefore = markActive_;
  bool hadExtentBefore = markActive_ && (markAnchorRow_ != cursorRow_ || markAnchorByte_ != cursorByte_);
  bankCurrentMarkIfExtended();
  cursorRow_ = std::clamp(row, 0, static_cast<int>(lines_.size()) - 1);
  cursorByte_ = byteOffsetForDisplayColumn(lines_[cursorRow_].text, column);
  if (!wasActiveBefore || hadExtentBefore) {
    // マークが全く無かった、または実質的な範囲を持っていて直前の
    // bankCurrentMarkIfExtended()で確定済み――どちらの場合も、この
    // 新しい押下位置を新しいアンカーにして(常に通常マークとして)
    // 開始する。
    markActive_ = true;
    rectangleMark_ = false;
    markAnchorRow_ = cursorRow_;
    markAnchorByte_ = cursorByte_;
  }
  // else: マークはアクティブだが実質的な範囲をまだ持っていなかった
  // (例えば^X SPC直後)――アンカーはそのまま、カーソルだけをこの位置へ
  // 動かしたことで、そのマークの自由端を定義する(直接のユーザー報告⑪)。
  render();
}

void CopyMode::startMark() {
  // 2026-09-07三訂、直接のユーザー要望(⑨のGUI限定ハイライト不具合の
  // 切り分けのため: "離れ小島は^@で作れる。だが^X␣では作れない。
  // そこで作れるようにしてみたらどうかな^@で。一旦ハイライトが出たら
  // あとは出たりして"): 直前の訂正(このメソッド自身の1つ前のコメント
  // 参照)でbankCurrentMarkIfExtended()呼び出しを撤去していたが、ここで
  // ^@に限り再度呼ぶようにする――beginOrExtendFromMouse()と同じく、
  // 既に実質的な範囲を持つマーク中にもう一度^@を押すと、その範囲を
  // 1つの島としてislands_へ確定・画面に残したまま、現在位置から新しい
  // アンカーを始める。startOrUpgradeRectangleMark()(^X SPC)側は据え
  // 置き(呼ばない)――ユーザー自身の言葉通り「^Xスペースでは作れない」
  // 状態を保ったまま、^@側だけを切り替えて比較できるようにするため。
  if (lines_.empty()) return;
  bankCurrentMarkIfExtended();
  markActive_ = true;
  rectangleMark_ = false;
  markAnchorRow_ = cursorRow_;
  markAnchorByte_ = cursorByte_;
  render();
}

void CopyMode::startOrUpgradeRectangleMark() {
  // startMark()自身の同日の訂正コメント参照――^X SPCも同じ理由で
  // bankCurrentMarkIfExtended()を呼ばない、単純な形に戻した。
  if (lines_.empty()) return;
  if (!markActive_) {
    markActive_ = true;
    markAnchorRow_ = cursorRow_;
    markAnchorByte_ = cursorByte_;
  }
  rectangleMark_ = true;
  render();
}

void CopyMode::bankCurrentMarkIfExtended() {
  if (!markActive_) return;
  if (markAnchorRow_ != cursorRow_ || markAnchorByte_ != cursorByte_) {
    islands_.push_back(currentMarkAsIsland());
    markAnchorRow_ = cursorRow_;
    markAnchorByte_ = cursorByte_;
  }
}

CopyMode::Island CopyMode::currentMarkAsIsland() const {
  bool anchorFirst =
      markAnchorRow_ < cursorRow_ || (markAnchorRow_ == cursorRow_ && markAnchorByte_ <= cursorByte_);
  Island island;
  island.rectangle = rectangleMark_;
  island.loRow = anchorFirst ? markAnchorRow_ : cursorRow_;
  island.hiRow = anchorFirst ? cursorRow_ : markAnchorRow_;
  if (rectangleMark_) {
    int anchorCol = displayColumnFor(lines_[markAnchorRow_].text, markAnchorByte_);
    int cursorCol = displayColumnFor(lines_[cursorRow_].text, cursorByte_);
    island.loCol = std::min(anchorCol, cursorCol);
    island.hiCol = std::max(anchorCol, cursorCol);
  } else {
    island.loByte = anchorFirst ? markAnchorByte_ : cursorByte_;
    island.hiByte = anchorFirst ? cursorByte_ : markAnchorByte_;
  }
  return island;
}

std::vector<std::string> CopyMode::extractIslandParts(const Island &island) const {
  std::vector<std::string> parts;
  for (int row = island.loRow; row <= island.hiRow; row++) {
    const std::string &text = lines_[row].text;
    int rowLo, rowHi;
    if (island.rectangle) {
      rowLo = byteOffsetForDisplayColumn(text, island.loCol);
      rowHi = byteOffsetForDisplayColumn(text, island.hiCol);
    } else {
      rowLo = (row == island.loRow) ? island.loByte : 0;
      rowHi = (row == island.hiRow) ? island.hiByte : static_cast<int>(text.size());
    }
    parts.push_back(text.substr(rowLo, std::max(0, rowHi - rowLo)));
  }
  return parts;
}

void CopyMode::highlightIsland(std::vector<ContentView::Line> &rendered, const Island &island) {
  int loRow = std::clamp(island.loRow, 0, static_cast<int>(rendered.size()) - 1);
  int hiRow = std::clamp(island.hiRow, 0, static_cast<int>(rendered.size()) - 1);
  for (int row = loRow; row <= hiRow; row++) {
    const std::string &text = rendered[row].text;
    int rowLo, rowHi;
    if (island.rectangle) {
      rowLo = byteOffsetForDisplayColumn(text, island.loCol);
      rowHi = byteOffsetForDisplayColumn(text, island.hiCol);
    } else {
      rowLo = (row == island.loRow) ? island.loByte : 0;
      rowHi = (row == island.hiRow) ? island.hiByte : static_cast<int>(text.size());
    }
    if (rowHi > rowLo) rendered[row].attributes.push_back({rowLo, rowHi, Display::Attribute::Standout});
  }
}

void CopyMode::confirmMark() {
  if (lines_.empty() || !markActive_) return;
  std::vector<std::string> allParts;
  for (const Island &island : islands_) {
    auto parts = extractIslandParts(island);
    for (auto &part : parts) allParts.push_back(std::move(part));
  }
  {
    auto parts = extractIslandParts(currentMarkAsIsland());
    for (auto &part : parts) allParts.push_back(std::move(part));
  }
  bool anyNonEmpty = false;
  for (const auto &part : allParts)
    if (!part.empty()) anyNonEmpty = true;
  if (anyNonEmpty) {
    // 空の断片(例えば、まだ実質的な範囲を持たないままconfirmMark()に
    // 至った現在進行中のマーク――他の島が既にある状態でのクリック
    // 直後等)は連結から完全に除く――「\n」だけの空行が末尾に残るのを
    // 防ぐ。
    std::string joined;
    for (const auto &part : allParts) {
      if (part.empty()) continue;
      if (!joined.empty()) joined += "\n";
      joined += part;
    }
    clipboardText_ = joined;
  }
  islands_.clear();
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
  for (const Island &island : islands_) highlightIsland(rendered, island);
  if (markActive_ && !rendered.empty()) highlightIsland(rendered, currentMarkAsIsland());
  // ContentView::setLines()は常にtopIndex_を0へリセットする(その
  // 宣言コメント参照、「内容が丸ごと置き換わったので古いスクロール
  // 位置は無意味」という一般的な前提)――しかしCopyModeにとっては、
  // 「内容」は毎回のrender()呼び出しのたびに実質的に「同じ」スナップ
  // ショット(ハイライトの範囲が変わっただけ)であり、setLines()を
  // 呼ぶたびにスクロール位置が問答無用でリセットされてしまうと、
  // moveUp()/moveDown()自身の「1行だけスムーズにスクロールする」
  // という調整(2026-09-07追加、直接のユーザー報告⑥参照)が、この
  // 直後のsetLines()によって毎回台無しになってしまう――保存して
  // 直後に復元することで、setLines()自身のリセットを実質的に無害化
  // する。復元後にsetCursorIndex()を呼ぶことで、その内部の
  // clampViewport()は「本当に必要な時だけ」(カーソルが実際に
  // ビューポートの外に出た時だけ)働く、本来の意図通りの動きになる。
  // pendingTopIndexOverride_自身の宣言コメント参照(loadSnapshot()直後
  // の1回だけ、起点のtopIndex()を使う――それ以外は通常通りview_自身の
  // 現在のtopIndex()をそのまま保存/復元する)。
  int preservedTopIndex = pendingTopIndexOverride_.value_or(view_.topIndex());
  pendingTopIndexOverride_.reset();
  view_.setLines(std::move(rendered));
  view_.setTopIndex(preservedTopIndex);
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
