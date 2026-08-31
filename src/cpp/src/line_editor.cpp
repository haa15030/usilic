#include "line_editor.hpp"

#include <algorithm>

namespace fm {

namespace {
const unsigned char *bytes(const std::string &s) {
  return reinterpret_cast<const unsigned char *>(s.data());
}
}  // namespace

LineEditor::LineEditor(Display &display, Charset::Encoding encoding)
    : display_(display), encoding_(encoding) {}

void LineEditor::setEncoding(Charset::Encoding encoding) {
  encoding_ = encoding;
  // 入力途中での文字コード変更は、legacyもやはり扱っていなかった
  // 端のケースである(部分的なUTF-8シーケンスがバッファされている間に
  // キーで文字コードを変える方法は無かった)――新しいエンコーディング
  // の下でそれを誤って解釈するリスクを負うより、部分シーケンスは
  // 放棄する。
  pendingUtf8_.clear();
}

void LineEditor::setField(int row, int col0, int width) {
  row_ = row;
  col0_ = col0;
  width_ = width;
}

void LineEditor::setText(const std::string &text) {
  buffer_ = text;
  if (static_cast<int>(buffer_.size()) > kMaxLength - 1)
    buffer_.resize(kMaxLength - 1);
  cursorPos_ = static_cast<int>(buffer_.size());
  shift_ = 0;
  pendingUtf8_.clear();
  redraw();
}

const std::string &LineEditor::text() const { return buffer_; }

int LineEditor::cursorBytePos() const { return cursorPos_; }

// -- UTF-8 / EUCのバイト<->カラムヘルパー --
// key.cのchar_bytes_before()/b2col()/utf8_total_width()/
// utf8_right_shift()からの移植(legacyのように呼び出し箇所ごとに
// 一度ずつではなく、ここで一度だけ書いてある理由についてはクラス
// コメント参照)。

int LineEditor::charBytesBefore(int pos) const {
  if (encoding_ != Charset::Encoding::Utf8)
    return Charset::splits_character(bytes(buffer_), pos, encoding_) ? 2 : 1;
  if (pos <= 0) return 0;
  int end = pos - 1, start = end;
  while (start > 0 && (static_cast<unsigned char>(buffer_[start]) & 0xc0) == 0x80) start--;
  return end - start + 1;
}

int LineEditor::charBytesAt(int pos) const {
  if (pos >= static_cast<int>(buffer_.size())) return 1;
  if (encoding_ == Charset::Encoding::Utf8)
    return Charset::sequence_length(static_cast<unsigned char>(buffer_[pos]));
  return Charset::splits_character(bytes(buffer_), pos + 1, encoding_) ? 2 : 1;
}

int LineEditor::byteToColumn(int shift, int bytePos) const {
  if (encoding_ != Charset::Encoding::Utf8) return bytePos - shift;
  int col = 0;
  int n = static_cast<int>(buffer_.size());
  for (int pos = shift; pos < bytePos && pos < n; ) {
    int sl = Charset::sequence_length(static_cast<unsigned char>(buffer_[pos]));
    col += Charset::display_width(bytes(buffer_) + pos, sl);
    pos += sl;
  }
  return col;
}

int LineEditor::totalWidth(const std::string &text) const {
  if (encoding_ != Charset::Encoding::Utf8) return static_cast<int>(text.size());
  int total = 0;
  int n = static_cast<int>(text.size());
  for (int pos = 0; pos < n; ) {
    int sl = Charset::sequence_length(static_cast<unsigned char>(text[pos]));
    total += Charset::display_width(bytes(text) + pos, sl);
    pos += sl;
  }
  return total;
}

int LineEditor::rightShift(const std::string &text, int fieldWidth) const {
  int n = static_cast<int>(text.size());
  if (encoding_ != Charset::Encoding::Utf8) {
    int excess = n - fieldWidth;
    return (excess > 0) ? excess : 0;
  }
  int col = totalWidth(text) - fieldWidth;
  int pos = 0;
  while (pos < n && col > 0) {
    int sl = Charset::sequence_length(static_cast<unsigned char>(text[pos]));
    col -= Charset::display_width(bytes(text) + pos, sl);
    pos += sl;
  }
  return pos;
}

// カーソルがビューポートからはみ出さない限りビューポートを安定
// させ、はみ出す場合は、カーソルを再び見える状態に戻すのに必要な
// 最小量(一度に1文字ずつ)だけスクロールする。編集用の各メソッドは
// 全て、redraw()経由でこの単一のルールへ収束する――legacyでは
// 代わりに、^E/^U/^Y/paste/insertは(need_scroll()経由で)常に新しく
// 右揃えのビューへ飛ぶ一方、^B/^Fは段階的にスクロールしていた。
// この2つの異なる挙動は、たまたまそれぞれ9箇所の呼び出し箇所ごとに
// 少しずつ違う形で再実装されてもいた。ここで統一することで、
// Home/End/kill/yank/paste/入力の全てが、特別扱いを一切必要とせず
// 同じ、単一の、正しい答えへ収束する: 例えばカーソルを0へ動かすと、
// 自然にshift_も0へ引き下げられ(下の最初の分岐)、カーソルをバッファ
// の末尾へ動かすと、末尾が収まるまで自然にshift_が増える(ループ)
// ――これは、Home/End専用のものではなく一般ルール経由で導出される
// だけで、直接rightShift()を計算するのと同じshiftへ着地する。
void LineEditor::adjustViewport() {
  if (totalWidth(buffer_) <= width_) {
    shift_ = 0;
    return;
  }
  if (cursorPos_ < shift_) shift_ = cursorPos_;
  while (byteToColumn(shift_, cursorPos_) >= width_) shift_ += charBytesAt(shift_);
}

void LineEditor::renderVisible(const std::string &visible, int startBytePos) {
  bool splitStart = (encoding_ != Charset::Encoding::Utf8) &&
                     Charset::splits_character(bytes(buffer_), startBytePos, encoding_);
  int len = static_cast<int>(visible.size());
  int checkStart = splitStart ? 1 : 0;
  bool splitEnd = (encoding_ != Charset::Encoding::Utf8) &&
                   Charset::splits_character(bytes(visible) + checkStart, len - checkStart, encoding_);
  if (splitEnd) len--;

  int i = 0;
  if (splitStart) {
    display_.putChar(' ');
    i = 1;
  }
  for (; i < len; i++) {
    unsigned char c = static_cast<unsigned char>(visible[i]);
    // K_dl(0x7f、DEL)とK_at(0x80)は、「このバイトを反転表示で示す」
    // という意味に転用されたlegacyの番兵値である。K_atは、最も一般的
    // な単一のUTF-8継続バイトと衝突する(これが移植元であるCLAUDE.md
    // の作業ログ15番、print_buff()のバグ修正参照)――0x80の特別扱いは
    // UTF-8モード以外でのみ行い、そのバグを再導入するのではなくこの
    // 修正を持ち越す。
    if (c && (c < ' ' || c == 0x7f || (encoding_ != Charset::Encoding::Utf8 && c == 0x80))) {
      int shown = (c == 0x7f) ? c - 0x80 : (c == 0x80 ? 0 : c);
      display_.setAttribute(Display::Attribute::Standout);
      display_.putChar(shown + '@');
      display_.setAttribute(Display::Attribute::StandoutOff);
    } else {
      display_.putChar(c);
    }
  }
  if (splitEnd) display_.putChar(' ');
}

void LineEditor::redraw() {
  adjustViewport();

  std::string visible;
  int n = static_cast<int>(buffer_.size());
  if (encoding_ != Charset::Encoding::Utf8) {
    int avail = std::min(width_, n - shift_);
    if (avail < 0) avail = 0;
    visible = buffer_.substr(shift_, avail);
  } else {
    int pos = shift_, col = 0;
    while (pos < n) {
      int sl = Charset::sequence_length(static_cast<unsigned char>(buffer_[pos]));
      int w = Charset::display_width(bytes(buffer_) + pos, sl);
      if (col + w > width_) break;
      col += w;
      pos += sl;
    }
    visible = buffer_.substr(shift_, pos - shift_);
  }

  display_.setAttribute(Display::Attribute::StandoutOff);  // key_eeol()のvt_endstand()からの移植
  display_.moveCursor(row_, col0_);
  display_.write(std::string(width_, ' '));
  display_.moveCursor(row_, col0_);
  renderVisible(visible, shift_);

  // moveCursor()ではなくsetVisibleCursor(): これこそが意味のある
  // 編集用キャレットである(このメソッド内の他の全てのmoveCursor()
  // 呼び出しが、次のグリフが書き込まれる場所を位置決めするだけなのと
  // は対照的)――なぜ-gがこの区別を必要とし、後の無関係な再描画に
  // よってカーソルが静かに他の場所へ引きずられないようにするかに
  // ついてはDisplay::setVisibleCursor()自身のコメント参照。
  display_.setVisibleCursor(row_, col0_ + byteToColumn(shift_, cursorPos_));
  display_.flush();
}

void LineEditor::insertText(const std::string &bytesIn) {
  if (bytesIn.empty()) return;
  std::string toInsert = bytesIn;
  int overflow = static_cast<int>(buffer_.size() + toInsert.size()) - (kMaxLength - 1);
  if (overflow > 0) {
    display_.putChar(7);  // BEL、vt_putchar(K_g)からの移植
    if (overflow >= static_cast<int>(toInsert.size())) return;
    toInsert.resize(toInsert.size() - overflow);
  }
  buffer_.insert(cursorPos_, toInsert);
  cursorPos_ += static_cast<int>(toInsert.size());
  redraw();
}

void LineEditor::insertByte(unsigned char byte) {
  if (encoding_ != Charset::Encoding::Utf8) {
    insertText(std::string(1, static_cast<char>(byte)));
    return;
  }
  if (pendingUtf8_.empty()) {
    // (複数バイトかもしれない)シーケンスの最初のバイト。下の長さ
    // チェックが比較対象を持てるようにしておく。
  }
  pendingUtf8_.push_back(static_cast<char>(byte));
  int want = Charset::sequence_length(static_cast<unsigned char>(pendingUtf8_[0]));
  if (static_cast<int>(pendingUtf8_.size()) < want) return;  // さらにバイトを待つ
  std::string complete = pendingUtf8_;
  pendingUtf8_.clear();
  insertText(complete);
}

void LineEditor::backspace() {
  if (cursorPos_ <= 0) {
    display_.putChar(7);
    return;
  }
  int n = charBytesBefore(cursorPos_);
  buffer_.erase(cursorPos_ - n, n);
  cursorPos_ -= n;
  redraw();
}

void LineEditor::deleteForward() {
  if (cursorPos_ >= static_cast<int>(buffer_.size())) {
    display_.putChar(7);
    return;
  }
  int n = charBytesAt(cursorPos_);
  buffer_.erase(cursorPos_, n);
  redraw();
}

void LineEditor::moveHome() {
  cursorPos_ = 0;
  redraw();
}

void LineEditor::moveEnd() {
  cursorPos_ = static_cast<int>(buffer_.size());
  redraw();
}

void LineEditor::moveLeft() {
  if (cursorPos_ <= 0) {
    display_.putChar(7);
    return;
  }
  cursorPos_ -= charBytesBefore(cursorPos_);
  redraw();
}

void LineEditor::moveRight() {
  if (cursorPos_ >= static_cast<int>(buffer_.size())) {
    display_.putChar(7);
    return;
  }
  cursorPos_ += charBytesAt(cursorPos_);
  redraw();
}

void LineEditor::killToStart() {
  killBuffer_ = buffer_.substr(0, cursorPos_);
  buffer_.erase(0, cursorPos_);
  cursorPos_ = 0;
  redraw();
}

void LineEditor::killToEnd() {
  killBuffer_ = buffer_.substr(cursorPos_);
  buffer_.erase(cursorPos_);
  redraw();
}

void LineEditor::yank() { insertText(killBuffer_); }

void LineEditor::setKillBuffer(const std::string &text) { killBuffer_ = text; }

void LineEditor::paste(const std::string &text) { insertText(text); }

void LineEditor::toggleQuotedInsert() { quotedInsertPending_ = !quotedInsertPending_; }

bool LineEditor::quotedInsertPending() const { return quotedInsertPending_; }

void LineEditor::clearQuotedInsert() { quotedInsertPending_ = false; }

}  // namespace fm
