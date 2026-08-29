#pragma once

#include <string>
#include <vector>

#include "charset.hpp"
#include "display.hpp"

namespace fm {

// AttributeRun自体は現在display.hppにある(Display::AttributeRun、
// そちらでプレーンなfm::AttributeRunへ別名を戻している)――理由は
// そちら自身のコメント参照(renderScrolled()の新しいScrolledLineが
// これを必要とし、pipe_buffer.hppは既にdisplay.hppに依存している
// ので逆向きにはできない)。

struct StyledLine {
  std::string text;
  std::vector<AttributeRun> attributes;
  bool endsWithNewline = true;  // false: 本物の改行ではなく`width`に達したことによる折り返し
};

// 生のバイト列(ファイル/パイプの内容)を、幅で折り返し済み・文字
// コード正規化済み・属性付与済みの行へ変換する。alloc.cの
// alloc_pipe()(幅折り返し/タブ/オーバーストライク検出のステート
// マシン)とlinein()(チャンクごとの文字コード検出/変換、こちらは
// 今ではalloc_pipe()自身のinit_code()/toeuc()/jistoeuc()/
// euctoutf8_buf()呼び出しの代わりに、既に移植済みのCharsetクラスを
// 呼ぶだけになっている)からの移植。
//
// これは逐語的な移植ではない。legacyのalloc_pipe()は1つの関数の中で
// 2つの無関係なことを行っていた: PPリストの構築、そしてデータが
// ストリーミングされてくるのに合わせて画面を直接駆動すること
// (「長いパイプの出力が徐々に届くのを見る」というユースケースの
// ために、パースと絡み合ったvt_move()/put_str()/print_line()呼び
// 出し)。PipeBufferは前半だけを行う――feed()はバイト列を受け取り、
// lines()はここまでにパースされた内容を返す。「届くたびに表示する」
// という挙動自体は、将来のMode/Applicationクラスが、feed()をループで
// 呼び、呼び出しのたびにlines()をContentViewへ積んで+redraw()する
// ことで駆動する――「バイト列を行へパースする」ことと「行を画面に
// 表示する」ことを別の関心事のまま保ち、Buffer系クラスとContentView/
// Displayの間で既に確立されている分離と同じ形にする。
//
// 意図的に除外(具体的な箇所は個々のメソッドコメント参照): K_a0の
// 16進ダンプモードのフォーマット(ContentViewも同じ「存在しない
// Buffer側の前処理に依存する」という理由で既にこれを除外している
// ――このクラスもそれを生成しない)、「純粋ASCII表示モード時に
// 非ASCIIバイトを<XX>形式の16進として表示する」機能、そして
// A_md3(繰り返しバイトが非ASCIIであるような単一バックスペースの
// オーバーストライク)――legacy自身のCLAUDE.mdの作業ログで、A_md3は
// 実ファイルで一度も再現したことが無く、そちらでも未修正のまま
// 残されているとの印が既に付いている。ここでも新たなギャップとして
// ではなく、同じ既知の意図的なギャップとして移植している。
class PipeBuffer {
 public:
  // width: 折り返し列(legacy: vt_col(0))。maxBytes: 全行を通じての
  // コンテンツ総予算(legacy: `bsize`)。超過するとoverBudget()がtrueに
  // なり、alloc_pipe()の「バッファ不足」中断と同じ挙動になる。
  PipeBuffer(Charset::Encoding encoding, int width, long maxBytes);

  void feed(const std::string &bytes);
  // まだ保留中の途中までの行を確定させる(入力の終わり)。
  void flush();

  const std::vector<StyledLine> &lines() const;
  bool overBudget() const;

 private:
  void processPending();
  std::string convertChunk(const std::string &rawChunk);
  void wrapLine(const std::string &convertedLine);
  int byteLengthAt(const std::string &s, int pos) const;
  int widthAt(const std::string &s, int pos, int charBytes) const;

  Charset::Encoding encoding_;
  int width_;
  long maxBytes_;
  long usedBytes_ = 0;
  bool overBudget_ = false;

  std::string rawPending_;      // まだ文字コード変換されていないバイト('\n'がまだ見つかっていない)
  std::string wrapText_;        // 構築中の現在の出力行
  std::vector<AttributeRun> wrapAttrs_;
  int wrapCol_ = 0;

  std::vector<StyledLine> lines_;
};

}  // namespace fm
