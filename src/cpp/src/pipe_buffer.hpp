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
  // 2026-09-08追加、直接のユーザー報告("usilic画面では98行だった行が
  // 97として表示されている"――usilic自身のCommand::StartEdit('e')が
  // エディタへ渡す"+N"がvi自身の実際の行番号と食い違っていた): この
  // StyledLineが属する、元のバイト列上での1始まりの「生の」行番号
  // ――幅超過による折り返し継続行は、直前の(改行で終わる)行と同じ
  // 番号を引き継ぐ。endsWithNewline自体は「この行が改行で終わるか」
  // だけを表し、どの生の行に属するかまでは表さなかったため、
  // PipeBuffer::wrapLine()/flush()自身がこのフィールドで明示的に
  // スタンプする。
  int rawLineNumber = 1;
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
  // detectOverstrike(2026-09-14追加、直接のユーザー依頼"（ls）での
  // ^O^M/^[^Mを実装して"、QUICK.txt自身の"C-[ C-m：<(ls)> 制御コードを
  // そのまま可視化（ls）"の実装): falseにすると、nroff重ね打ち
  // ("X\bX"=太字、"_\bX"=下線)の解釈を一切行わない――バックスペース
  // バイト自体も他の生の制御バイトと全く同じ「素の文字」経路へ落ち、
  // ContentView::renderLine()自身の既存の制御バイト表示規約
  // (0x20未満を反転表示のグリフとして描く、QUICK.txt自身の
  // "^A^B^Cはハイライト表示のABCで表示"と同じ)でそのまま可視化
  // される。既定はtrue(従来通りの挙動、他の全ての呼び出し元に影響
  // しない)。
  PipeBuffer(Charset::Encoding encoding, int width, long maxBytes, bool detectOverstrike = true);

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
  // 2026-09-26追加、直接のユーザー依頼("ページャーとSHELL両方で色を
  // 解釈するよう実装して"): `git log`等が出すANSIのエスケープシーケンス
  // の扱い。escapeLengthAt()は`pos`(ESC)から始まる完結したシーケンスの
  // バイト長(完結していなければ0――その場合は従来通り制御文字として
  // 表示する)。applyEscape()はそれがSGR(`ESC[...m`)なら下のsgr*_へ
  // 反映し、それ以外(カーソル移動・行消去`ESC[K`・OSC等)は何もしない
  // (表示からは取り除く)。detectOverstrike_がfalse(^[^Mの"as is"表示、
  // 制御コードをそのまま見せるモード)の時は、どちらも使われない。
  int escapeLengthAt(const std::string &s, int pos) const;
  void applyEscape(const std::string &s, int pos, int len);

  Charset::Encoding encoding_;
  int width_;
  long maxBytes_;
  bool detectOverstrike_;
  long usedBytes_ = 0;
  bool overBudget_ = false;

  std::string rawPending_;      // まだ文字コード変換されていないバイト('\n'がまだ見つかっていない)
  std::string wrapText_;        // 構築中の現在の出力行
  std::vector<AttributeRun> wrapAttrs_;
  int wrapCol_ = 0;

  // 2026-09-04追加、直接のユーザー依頼("行頭にパディングする全角
  // スペースの個数を指定するだけのusilic特化マークアップ書式"、
  // help_text.hpp自身のexpandHelpMarkup()コメント参照): HELP自身の
  // <indentN>...</indent>マークアップが展開する制御バイト
  // \x01+数字1桁(wrapLine()自身のコメント参照)で更新される、
  // 「現在アクティブな字下げの全角スペース個数」――0は「字下げ無し」
  // (既定)。2026-09-04訂正(初回のsrc/HELP.txt全面改稿を実際に流し
  // 込んで再現・修正): 当初は「段落の最初の行はソース側の実文字の
  // 字下げをそのまま使い、折り返し継続行にだけ適用する」つもりで
  // finalize()自身が即座に種付けしていたが、実際の書き方
  // ("<indentN>本文...</indent>"、タグの直後に本文、手動の字下げ
  // 文字を一切伴わない)では、その場で種付けする方式だと
  // </indent>だけが続く(本物のコンテンツが無い)行にまで先回りして
  // パディングだけの行を作ってしまっていた――wrapLine()自身の
  // seedIndentIfAtLineStart()のコメント参照。修正後は「新しい行の
  // 最初の可視文字が実際に置かれようとする、その場」でだけ遅延して
  // 種付けする――結果として、タグ自身の1行目「にも」継続行と全く
  // 同じに字下げが適用されるようになった(むしろ実際の書き方に合って
  // いる: 手動の字下げ文字を一切必要としない)。
  int currentIndent_ = 0;

  // StyledLine::rawLineNumber自身のコメント参照(2026-09-08追加)――
  // 次にfinalize()/flush()する行に付ける、1始まりの生の行番号。
  // finalize(true)(本物の改行)の「後」だけ+1する――finalize(false)
  // (折り返し継続)は同じ番号を使い回す。
  int nextRawLineNumber_ = 1;

  // 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>で
  // 記載したいので対応よろしく"): HELP自身の<i>...</i>マークアップが
  // 展開する制御バイト\x02+数字1桁(wrapLine()自身のコメント参照)で
  // 更新される、「現在の文字が斜体を引き継ぐべきか」――currentIndent_
  // とは異なり、こちらは折り返しをまたいでリセットされない(斜体は
  // 折り返し位置とは無関係な、通常のテキスト装飾なので、太字/下線と
  // 同じくfinalize()による特別扱いは不要)。
  int currentItalic_ = 0;  // currentBold_自身のコメント参照(カウンタ化の理由)

  // 2026-09-22追加、直接のユーザー依頼("CommonMarkの強調解決アルゴリズム
  // 対応が普通なら実装してほしい"): expandHelpMarkup()自身の<b>/<u>の
  // エンコーディングを、currentItalic_と同じ「トグル式の制御バイト」方式
  // (\x03+数字1桁=太字、\x04+数字1桁=下線)へ統一した――従来の「開始〜
  // 終了タグの間を生の文字列として1文字ずつ重ね打ち」方式(help_text.cpp
  // 自身の旧コメント参照)は、中に他のタグがあっても再帰的に解釈しない
  // ため、Markdownの`**太字 *斜体* まだ太字**`のような入れ子ができな
  // かった。currentItalic_と全く同様、折り返しをまたいでリセットしない。
  // 実際のgroff/man出力が使う本物のnroff重ね打ち("X\bX"/"_\bX")の検出
  // (このファイル自身の別の、無関係なコードパス)には一切手を付けて
  // いない――そちらはexpandHelpMarkup()を経由しない実ファイルの内容
  // (manページ等)向けで、このトグルバイトとは独立に動く。
  // 深さのカウンタにしてある(bool一つではない)理由: Markdownの見出し
  // (例えば`# usilic **強調** タイトル`)が、見出し自身の外側の<b>の中に、
  // 本文の**強調**由来の内側の<b>が入れ子になることがある(markdownToHelpMarkup()
  // 自身の見出し処理参照)。単純なboolだと、内側の</b>(\x030)が外側の<b>の
  // 有効状態まで巻き込んで解除してしまい、"タイトル"部分の太字が消えていた
  // (2026-09-22の実機PTY確認で発見、実際のバグ)。カウンタなら、開く(\x031)で
  // +1・閉じる(\x030)で-1(0未満にはしない)し、0より大きい間だけ有効とすることで、
  // 同じタグの入れ子でも外側の区間全体で正しく有効なままになる。currentItalic_も
  // 同じ理由で合わせてカウンタ化した。
  int currentBold_ = 0;
  int currentUnderline_ = 0;
  // 2026-09-29追加: <serif>(\x05+数字1桁)、Display::Attribute::Serif参照。
  // currentItalic_と同じトグル式のカウンタ。
  int currentSerif_ = 0;
  // 2026-09-30追加: <rb>(\x06)/<rt>(\x10)、Display::Attribute::RubyBase/RubyText参照。
  int currentRubyBase_ = 0;
  int currentRubyText_ = 0;
  // 2026-10-01追加: <big>(\x11)/<small>(\x12)、Display::Attribute::Large/Small参照。
  int currentLarge_ = 0;
  int currentSmall_ = 0;
  // 2026-10-24追加: <note>(\x13)、本文の注釈番号(EPUBのnoteref)。決まった色(kNoteColor)の色のランにする
  // (Kindleの注釈番号と同じく青、本の字の色(CSSのcolor)は使わない)。
  int currentNote_ = 0;
  static constexpr int kNoteColor = Display::kTrueColorFlag | 0x1E64C8;

  // escapeLengthAt()自身のコメント参照。SGRで指定された現在の状態――
  // 本物の端末と同じく、改行や折り返しをまたいで次のSGRまで持続する。
  // 色の値の表現はDisplay::Attribute::Color自身のコメント参照。
  bool sgrBold_ = false, sgrUnderline_ = false, sgrItalic_ = false, sgrReverse_ = false;
  int sgrFg_ = Display::kDefaultColor, sgrBg_ = Display::kDefaultColor;

  std::vector<StyledLine> lines_;
};

}  // namespace fm
