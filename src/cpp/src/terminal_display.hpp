#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "display.hpp"

namespace fm {

// 現在の$TERMに対してterminfoが報告する能力文字列。空文字列は「この
// 端末はこの能力に対応していない」を意味する――呼び出し側は既に
// それを許容できなければならない(legacyのtc.cが使用前に
// Tc.sc/Tc.rc/Tc.csをnullチェックしていたのと同じ)。フィールド名は
// tc.cが使っていたtermcapの2文字名にコメントで合わせてあるので、
// そのファイルと直接突き合わせて確認できる。
struct TerminalCapabilities {
  std::string clearScreen;                    // cl
  std::string clearToEndOfLine;                // ce
  std::string cursorAddress;                   // cm(必須。fromEnvironment()参照)
  std::string cursorLeft;                       // le
  std::string standoutOn, standoutOff;          // so, se
  std::string underlineOn, underlineOff;        // us, ue(so/seへフォールバック)
  std::string boldOn, boldOff;                  // md, me
  // 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>で
  // 記載したいので対応よろしく"): legacyには斜体の慣習が無く(md/me
  // のような2文字termcap名も割り当てられていない)、このC++移植
  // 自身の新規追加――ncurses/terminfoのenter_italics_mode/
  // exit_italics_mode(fromEnvironment()参照)を素直に使う。対応
  // していない端末では空文字列のまま(boldOn/underlineOn等と同じ
  // 「no-opへ優雅に劣化する」慣習)。
  std::string italicOn, italicOff;              // ZH, ZR (terminfo: sitm, ritm)
  std::string saveCursor, restoreCursor;        // sc, rc(省略可)
  std::string changeScrollRegion;               // cs(省略可)
  std::string scrollForward, scrollReverse;     // sf, sr
  std::string enterAltScreen, exitAltScreen;    // ti, te
  std::string keyUp, keyDown, keyLeft, keyRight; // ku, kd, kr, kl
  std::string keyPageUp, keyPageDown;            // kP, kN
};

// legacyのtc.cと同じtputs/tgoto/setupterm方式を使った、termcap/
// terminfoベースのDisplay。忠実に移植済み。legacyのCとの挙動の違いは
// コメントで明示的に述べてある(いくつか小さな、意図的なものがある
// ――close()と、display.hppのoutput-enabledガードについての注記
// 参照)。
//
// テストに関する注記: ユニットテストから本物の端末I/Oをヘッドレスに
// 実行する方法は無い(tputs/setuptermは、実際のterminfoデータベース
// と、意味のある出力のためには本物のttyを必要とする――legacyの
// tc.cも一度もユニットテストされたことは無く、PTYスクリプトでのみ
// 検証されていた)。「テストできる」部分と「できない」部分を誠実に
// 分離しておくために: 実際にI/Oを行う各メソッドは、その一番最初の
// 文としてoutputEnabled()をガードするので、下の注入可能なコンストラ
// クタ(outputEnabledは既定でfalse)経由でTerminalDisplayを構築すれば、
// 全てのI/Oメソッドが確実に何もしなくなる――標準出力にもncursesの
// どのグローバル状態にも触れずにテストから安全に呼び出せる。これに
// より、テストは純粋な状態(属性/キー能力の検索、ジオメトリ、有効/
// スムーズスクロールのフラグ自体)を確かな自信を持って検証でき、
// 実際のエスケープシーケンス出力の方は、legacyコードのカバレッジと
// 同様、ビルドによる検証のみとなる。
class TerminalDisplay : public Display {
 public:
  TerminalDisplay(TerminalCapabilities caps, int rows, int columns);

  // 実際の構築経路: setupterm()経由で$TERMを、ioctl(TIOCGWINSZ)経由で
  // 制御端末のウィンドウサイズを問い合わせる。tc_load()/
  // get_winsize()(tc.c/vt.c)からの移植。既定では、失敗時(TERM未設定、
  // 未認識の端末、またはcursor_addressを欠くterminfoエントリ)に
  // プロセスを終了する。legacyの挙動と正確に一致しており、これが
  // 「唯一」可能なdisplayバックエンドだった当時、この3つはいずれも
  // そこで既に致命的な起動エラーだった。
  //
  // `exitOnFailure = false`(2026-08-24修正、docs/CHANGES.md 423番、
  // 実際のユーザー報告: `make app`が作る新しい.appバンドルを実際に
  // ダブルクリックしても全く何も起きなかった――標準エラーを一時的に
  // ファイルへリダイレクトして確認したところ、まさにこの
  // fprintf+exit(1)が「TERM environment variable is not set」で
  // 毎回発火していた): このクラス自身のlegacyから受け継いだ前提
  // ――「本物の端末が必ず存在する、さもなければプログラム全体が
  // 動けない」――は、GUIモード(GraphicalDisplay、SDL2ベース)が
  // 存在するようになった瞬間に成り立たなくなった。そちらのバック
  // エンドはterminfo/ttyを自前で全く必要としないため。
  // LaunchServices/Finder経由で(どのシェルからでもなく)起動された
  // プロセスには制御端末が「無く」、さらに致命的なことに、その環境に
  // $TERMも無い――この移植自身のシェルからの`open`によるテストは、
  // たまたま呼び出し元シェルの$TERMを継承していてこの経路に一度も
  // 到達せず、長らくこのバグを覆い隠していたのとは対照的。main()から
  // `false`が渡されるのは、具体的には、そのセッションがGUI専用であり、
  // このバックエンドの本物の端末機能を実際に通して描画することは
  // 決して無いと(この呼び出しより前に決定される、argv/-c/-g/
  // プラットフォームの既定ロジックから)既に分かっている場合――
  // その場合は終了する代わりに、劣化した、空の能力を持つ
  // プレースホルダを返し、慣習的な80カラム×24行の端末としてサイズ
  // 設定される(なぜこれがこの独自の値で、このファイルの他の場所に
  // ある windowSize()の異なる80x30フォールバックを意図的に再利用
  // していないのかについてはfromEnvironment()自身の.cpp側コメント
  // 参照)。純粋にCUI専用の起動(forceCui、または非macOSプラット
  // フォームの既定)には全く影響しない――依然として既定は`true`の
  // ままで、依然として同じ診断メッセージと共に終了する。そちらでは
  // 動作する本物の端末が本当に必要であり、「能力無しで静かに続行
  // する」は1つの分かりにくい失敗を別の失敗に置き換えるだけになる
  // ため。
  static std::unique_ptr<TerminalDisplay> fromEnvironment(bool exitOnFailure = true);

  void write(const std::string &text) override;

  // 2026-09-21、直接のユーザー報告(Terminal.app 80x24、実機で確認): タブはPipeBuffer::wrapLine()が
  // U+2060(WORD JOINER、幅0)+スペースへ展開する(検索性のための内部表現)。iTerm2ではこれが
  // 幅0で不可視だが、Terminal.appは1セル分として数えるらしく、タブを含む80桁ぴったりの行が
  // 溢れて折り返し、最下行付近の表示が崩れた(タブ→空白置換で消えることも確認済み)。
  // 内部の幅計算は幅0のままなので、端末へ書き出す時だけU+2060を取り除く。
  static std::string withoutWordJoiners(const std::string &text);
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;
  // 単にmoveCursor(row, col)を呼ぶだけ――本物の端末がこれを別途
  // 追跡する必要が無い理由についてはDisplay::setVisibleCursor()
  // 自身のコメント参照。
  void setVisibleCursor(int row, int col) override;
  void hideVisibleCursor() override;  // 何もしない、Display自身のコメント参照

  void clearScreen() override;
  void clearToEndOfLine() override;
  void scrollForward() override;
  void scrollReverse() override;

  void setAttribute(Attribute attr) override;
  std::string attributeEscape(Attribute attr) const override;

  int rows() const override;
  int columns() const override;
  void setSize(int rows, int columns) override;

  std::string keySequence(Key key) const override;

  bool outputEnabled() const override;
  void setOutputEnabled(bool enabled) override;

  bool smoothScrollEnabled() const override;
  void setSmoothScrollEnabled(bool enabled) override;

  bool modeLineVisible() const override;
  void setModeLineVisible(bool visible) override;

  // Display::hasNativeScrollbar()自身のコメント参照。本物の端末には
  // usilic自身が描く独自のスクロールバーが無いためfalse。
  bool hasNativeScrollbar() const override { return false; }

  void open() override;
  void close() override;

  void leaveForExternalProgram() override;
  void reenterFromExternalProgram() override;

  // Display::renderScrolled()自身のコメント参照: 本物の、行単位の
  // 実装(端末にはサブ行アドレス指定が無いため、pixelOffsetYはその
  // 符号以外には効果を持たない)。main.cpp内の実端末ドラッグスクロール
  // フォールバックが現在はこれを呼ぶ代わりに直接ContentView::redraw()
  // を駆動しているにもかかわらず、正しく使える状態のまま保たれている。
  void renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                       const std::vector<Display::ModeLineSegment> &modeLineSegments) override;

  // 現在のサイズとスムーズスクロール設定を踏まえた、スクロール領域
  // 変更能力のための行/列引数。純粋(I/Oなし)。Displayの抽象
  // インターフェースの一部ではない――主に、本物の端末を必要とせず
  // これを直接ユニットテストできるよう、ここで公開してある。legacyの
  // 呼び出し元が、あらゆるvt_cs()の呼び出し箇所で繰り返していた行の
  // 算術からの移植(display.hpp参照)。
  std::pair<int, int> scrollRegionArgs() const;

 private:
  void applyScrollRegion();
  // 自身の.cppのコメント参照。
  void writeAttributedLine(const std::string &text, const std::vector<Display::AttributeRun> &runs);

  // columns()自身の.cpp側コメント参照。ContentView::
  // renderScrollIndicatorOverlay()が使う、常時予約されるつまみ2列分。
  static constexpr int kScrollbarStripWidth = 2;

  TerminalCapabilities caps_;
  int rows_, columns_;
  bool outputEnabled_ = false;
  bool smoothScroll_ = false;
  bool modeLineVisible_ = true;

  // 2026-09-04追加、直接のユーザー報告("-cでは斜体にならない"=fk)を
  // 調査して発見した実際のバグの修正: setAttribute()自身のコメント
  // 参照(caps_.boldOffがexit_attribute_modeへ、なぜ・どう波及するか)。
  // これら4つは「今、実際に端末へ何を送出したか」の記録であり(単なる
  // 「呼び出し元が最後にBold/Underline/Italic/Standoutを要求したか」
  // の記録ではない――BoldOff自身のリセット波及を検知して修復する
  // には、この2つを区別する必要がある)。
  bool boldOn_ = false, underlineOn_ = false, italicOn_ = false, standoutOn_ = false;
  // 2026-09-19追記(Display::Attribute::HighlightCurrent自身のコメント
  // 参照): Highlight/HighlightCurrentの2種類に分かれたので、単なる
  // bool(highlightOn_)では「今どちらが有効か」を区別できない――
  // BoldOff後の再送出(setAttribute()自身のコメント参照)は正しい方の
  // エスケープを再送する必要があるため、有効な方のAttributeそのもの
  // (無効ならnullopt)を保持する。
  std::optional<Display::Attribute> highlightVariant_;
};

}  // namespace fm
