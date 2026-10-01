#pragma once

#include <string>
#include <vector>

namespace fm {

// 抽象的な画面: テキスト出力、カーソル移動、属性、端末のジオメトリ。
// old/src/tc.c + vt.cからの移植で、これらは(usilic.hのVT構造体を見ると
// 分かる通り)既にCにおける事実上のvtableだった――このクラスは
// その形を正式なものにすることで、2つ目の実装(「猫の手」慣性
// スクロールGUIモード用の、将来のSDL2ベースGraphicalDisplay、
// docs/ideas/IDEAS.md参照)を、呼び出し側に一切触れずに差し込める
// ようにする。
//
// 意図的に除外(将来のクラスの仕事であり、Displayのものではない):
//   - raw termiosの設定とキーボードバイトの読み込み(vt_init()、
//     vt_getch()、vt_tfd()) -- それは出力がどう描画されるかではなく
//     入力がどう読まれるかについての話であり、将来のKeyInputクラス
//     行き。
//   - vt_cursor()/tc_cursor(): 名前に反して、これはカーソルの可視性
//     には一切触れない――どのSHELL子プロセスのpidへSIGWINCHを転送
//     すべきかを記録するだけ(legacyのCでのkey_winch(-f_cursor))。
//     それは将来のShellクラスのための端末リサイズ転送の管理であり、
//     Displayの関心事ではない。
//   - legacyの'W'というvt_modeフラグ(SHELLの子プロセスが生きている
//     間にSIGWINCHを受信した): 同じ理由で、将来のShell/KeyInputの
//     領分。
//   - legacyのコードが、あるオーバーストライク範囲が「なぜ」太字/
//     下線と検出されたかを覚えておくために使っていたA_md2/A_md3/
//     A_us2のサブタグ(alloc.cのsave_attr()参照) -- Displayは「太字を
//     オンにする」ことだけ知ればよく、どのnroffオーバーストライク
//     パターンがその要求を生んだかは知る必要が無い。その分類は
//     Buffer/ContentViewの関心事である。
class Display {
 public:
  enum class Attribute {
    Underline,
    UnderlineOff,
    Bold,
    BoldOff,
    Standout,     // 反転表示。legacyにはこれのための2つのコード経路
                  // (vt_stand()/vt_endstand()がso/seを直接呼ぶ方式と、
                  // vt_seta(A_cs)/vt_seta(A_ce)が全く同じことをする
                  // 方式)があった――tc_geta()のA_cs/A_ceケースを
                  // 読んで同一であることを確認済みなので、ここでは
                  // 2つのAPIではなく1つのAttributeへ統一してある。
    StandoutOff,
    // 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>
    // で記載したいので対応よろしく"): legacyのnroff重ね打ち規約には
    // 斜体の慣習が無い(Bold/Underlineとは異なりlegacyからの移植では
    // なく、このC++移植自身の新規追加)ので、専用の属性を新設した。
    Italic,
    ItalicOff,
    // 2026-09-29追加、直接のユーザー依頼(EPUBの段階2、"sans-serif(ゴシック体)と
    // serif(明朝体)をページ内に混在できるかどうか"): 明朝体で描く字。GraphicalDisplay
    // (-g)だけが明朝体のフォントへ切り替え、端末(-c)は何もしない(フォントを選べない)。
    // EPUBの変換(epubToHelpMarkup())がCSSのfont-familyから<serif>を付ける。
    Serif,
    SerifOff,
    // 2026-10-01追加、直接のユーザー依頼("フォントサイズの設定を実装よろしく"、EPUBのCSSの
    // font-size): 大きい字(Large、115%以上)と小さい字(Small、90%以下)。GraphicalDisplay(-g)
    // だけがマス目はそのままで字を拡大縮小して描き、端末(-c)は何もしない。EPUBの変換
    // (epubToHelpMarkup())が<big>/<small>を付ける。
    Large,
    LargeOff,
    Small,
    SmallOff,
    // 2026-09-30追加、直接のユーザー依頼("-gでのルビを実装してみて"): ルビの親字(RubyBase)と
    // 読み(RubyText、「《ひ》」の《》を含む)。行の装飾のラン(AttributeRun)にだけ現れ、
    // setAttribute()には渡さない。ContentView::renderLine()が、drawsRuby()の表示では読みを
    // マスに書かずにsetPendingRuby()で親字に添え、端末では「燈《ひ》」のまま書く。
    RubyBase,
    RubyText,
    // 2026-09-10追加、直接のユーザー依頼("マッチ文字列をboldの代わり
    // にハイライトでやってみて。色は変えられない？薄めの色とか")、
    // 同日さらに前景色だけの版(初版)に対する直接の追加依頼("文字色
    // ではどうしても判別しにくい...emacsでは薄い背景色なのに一目で
    // わかるのは反転表示だから...ハイライト(反転表示)にはどうしても
    // できない？可能なら背景色薄めでやってみて"): in-place GREPの
    // マッチ文字列表示専用――BoldがGrepMatch::matchSpans自身のBold
    // から置き換わった(FileMode自身のオーバーストライク由来のBold、
    // legacyのnroff重ね打ち規約からの移植とは無関係、共存できる)。
    // 単なる前景色変更(初版)では、周囲のテキストと明るさが近い場合に
    // 判別しにくいという実際の指摘を受け、emacsのisearch/lazy-highlight
    // に倣い「背景色を塗って前景をそれに対して読みやすい色にする」
    // 版へ改訂――本物のSGR反転表示(コード7、Standout自身が既に使って
    // いる)ではなく、明示的な前景/背景の色ペアとして実装してある
    // (Standoutの「今の前景/背景をそのまま入れ替える」という性質は、
    // 何が「今の色」かに依存し薄い色を保証できないため、この用途には
    // 使えない)。他のAttributeとは異なり、legacyにもこのC++移植の
    // 他の場所にも前例が無い、色そのものを運ぶ唯一のAttributeなので、
    // terminfoのパラメータ付きcapability(setaf、tparm()が必要)では
    // なく、生のANSI SGRコードをTerminalDisplay側で直接出す(この
    // ポートが読者に想定している現実的なターミナル環境――
    // Terminal.app/iTerm2/GNOME Terminal等――は全てこの基本的なSGR
    // 色コードに対応している)。
    // 2026-09-10さらに改訂(実機スクリーンショット=fz添付での直接の
    // 指摘): 当初は基本16色(前景=黒/背景=明るい黄色)を使っていたが、
    // ターミナルのプロファイルがその16色自体を再定義している場合
    // (Terminal.appの"Basic"プロファイル等)、判別しにくい問題が別の
    // 形でぶり返し得ると判明。拡張256色パレット(16〜255)は各端末が
    // 固定RGBとして扱うのが実質的な標準であり、基本16色(0〜15、
    // ユーザーが再定義できる)とは別の色番号空間なので、そちらへ
    // 切り替えた(黒のキューブ色+薄い黄色)。
    // ただし注意: これが解決するのは「選んだ色番号スロット自体を
    // ユーザーが再定義できてしまう」という狭い問題だけであり、
    // 「選んだ絶対RGB値がそのターミナルの実際の通常色/Standoutの
    // 結果と偶然一致してしまう」という別の問題(直前のパラグラフで
    // 触れているStandoutとの区別の話そのもの)は解決しない――
    // 実際の色を問い合わせる手段が無い以上、どんな固定色を選んでも
    // 原理的に避けられない残存リスクである(実際、Terminal.appの
    // "Solid Colors"プロファイル――macOSがランダムに選ぶ色――では
    // 文字色/背景色が同化する実例が出た=ga)。
    // 2026-09-10三度目の改訂、直接のユーザー提案("手近なところで
    // emacs仕様はどう？"): 自前で「無難な絶対色」を発明するのを
    // やめ、何十年も無数のテーマの上で実績のあるGNU Emacs自身の
    // isearch faceをそのまま拝借する方針へ転換――具体的な色番号は
    // TerminalDisplay::attributeEscape()自身のコメント参照(当て推量
    // ではなく、実際にEmacsをPTY経由で起動して送出される生の
    // エスケープシーケンスをキャプチャして確認済み)。中間的な明度・
    // 高い彩度の色は、近黒/近白のような極端な色と違い、明るい/暗い
    // どちらのテーマとも十分なコントラストを保ちやすいという性質を
    // 利用しており、これは今度こそ直前のパラグラフで述べた「原理的に
    // 避けられない残存リスク」を実質的に縮小する(ゼロにはできない
    // ――絶対的な保証は無いが、実績のある選択によりリスクを大きく
    // 下げられる、という主張であり、"Solid Colors"のような完全に
    // ランダムな配色に対してまで無敵になるわけではない)。
    //
    // 2026-09-19追記、直接のユーザー報告(PTYハーネスでの再現確認、
    // Emacsとの比較: "本来マッチするのは*ひとつだけなので**の行は
    // ^Nを2回押さないと次行へ行かないはずが1回で行ってしまう"):
    // 上の全ての改訂は「全マッチを一律に同じ色で塗る」version 1の
    // 話であり、その一律の色として採用したのは実はEmacs自身の
    // isearch face(現在マッチ専用の強調)だった――^N/^Pの移動先
    // (grepInPlaceIndex_)が「行」単位だったため「現在マッチ」という
    // 概念が「行」の粒度でしか存在せず、isearch face本来の役割
    // (現在マッチ"だけ"の強調)を全マッチに転用しても実害が無かった。
    // 今回、^N/^Pの移動先を「一致した個々の出現位置」単位まで細かく
    // した(Application::GrepOccurrence自身のコメント参照)ことで、
    // 「今どの出現位置にいるか」が初めて全マッチとは別の情報になった
    // ため、Highlightは「他の全マッチ」(Emacsのlazy-highlight face
    // 相当、背景paleturquoiseのみ)へ格下げし、新設のHighlightCurrent
    // が「現在位置の出現」(旧Highlightの値そのまま、isearch face)を
    // 引き継ぐ。値は当て推量ではなく、こちらも同じくEmacs 30.1を
    // PTY経由で実際に起動しキャプチャ済み(TerminalDisplay::
    // attributeEscape()自身のコメント参照)。
    Highlight,
    HighlightCurrent,
    HighlightOff,
    // 2026-09-26追加、直接のユーザー依頼("ページャーとSHELL両方で色を
    // 解釈するよう実装して"、IDEAS.mdの「PAGERのANSI非対応」項目):
    // `git log`等が出すANSIの色エスケープ(SGR 30-37/90-97/38;5;n/
    // 38;2;r;g;b等)を、生の`^[[33m`ではなく本物の色として表示する
    // ためのラン専用の種別。他の種別と違い、色の値そのものを
    // AttributeRun::fg/bg(下記)に持つ――setAttribute()には渡さず、
    // 描画側はsetColor()で適用する。
    Color,
  };

  // Attribute::Color自身のコメント参照。色の値の表現: kDefaultColor
  // (-1)=端末/ウィンドウの既定色、0〜255=xterm互換の256色パレット
  // 番号(0〜15は利用者の配色設定に従う基本16色)、kTrueColorFlag|0xRRGGBB
  // =24bitの直接指定。
  static constexpr int kDefaultColor = -1;
  static constexpr int kTrueColorFlag = 0x1000000;

  enum class Key { Up, Down, Left, Right, PageUp, PageDown };

  virtual ~Display() = default;

  // -- 生の出力 --
  virtual void write(const std::string &text) = 0;
  virtual void putChar(int c) = 0;
  virtual void flush() = 0;

  // -- カーソル --
  virtual void moveCursor(int row, int col) = 0;
  virtual void cursorLeft() = 0;
  virtual void saveCursor() = 0;     // no-op if the terminal lacks this
  virtual void restoreCursor() = 0;  // capability (legacy: sc/rc optional)

  // (row, col)を、「論理的な」/意味のあるカーソル――ユーザーが
  // 実際に「カーソル」と呼ぶであろうもの(ライブなプロンプト編集の
  // キャレット、^P/^Nで今閲覧中のSHELLスクロールバック行、いずれ
  // カーソル位置を意識したGUI対応が育てばページャの現在行/COPY
  // モードの文字範囲の端点も)――が視覚的に留まるべき場所として
  // 記録する。これはmoveCursor()とは対照的で、そちらは「次のバイトが
  // 書き込まれる場所」も兼ねている(ContentView::redraw()の行ごとの
  // 書き込み、モードライン描画、その全てが、そこに可視カーソルを
  // 置く意図無く、純粋に書き込みの副作用としてカーソルを動かす)。
  //
  // 本物の端末(TerminalDisplay)では、この区別は問題にならない:
  // -cの描画モデルには、-gのように別途「画面全体をフラッシュする」
  // ステップは一切必要無く(全てのバイトは書き込まれた瞬間に既に
  // 見えている)、-c自身の実行ループは1回の反復で1つの入力イベントを
  // 処理し、その後は他に何も画面に触れない――そのため、本物の
  // ハードウェアカーソルが既に使っている、素の「端末がアイドルに
  // なる前の最後のmoveCursor()呼び出し」で十分であり、
  // TerminalDisplay自身の実装は単なるmoveCursor(row, col)である。
  //
  // 一方GraphicalDisplayの-g実行ループは、安定して約60Hzでポーリング
  // し、Shellの SHELL履歴の^P/^N処理が明示的に閲覧先の行を要求したのと
  // 「同じ」フレーム内で、無条件に完全なContentView::redraw()
  // (それ自身の内部のmoveCursor()呼び出しは純粋なグリフ配置であり、
  // モードライン自身のテキストが最後に終わった場所へ着地する)を
  // 再発行する――この別概念が無いと、その後の偶発的な再描画が、
  // 意図的に設定されたカーソル位置を、それが現れた直後に静かに
  // 踏みつぶしてしまっていた(実際の2026-08-04のユーザー報告:
  // 「-gのSHELLではプロンプトと同じく一瞬出て消えるパターン」)。
  // これを、あらゆる偶発的なテキスト配置のmoveCursor()呼び出しでは
  // なく、「ここにカーソルを表示せよ」を意味する呼び出し側だけが
  // 更新する、それ自身の明示的な状態として追跡することで、特定の
  // フレームの呼び出し順序に関わらず、構造的にその種の踏みつぶしを
  // 防げる(どう使われるかについてはGraphicalDisplay自身の
  // renderCursor()/Impl::visibleCursorRowのコメント参照)。
  virtual void setVisibleCursor(int row, int col) = 0;

  // setVisibleCursor()が最後にアサートしたものをクリアする。これに
  // より、setVisibleCursor()自体を一切呼ばない何かへモードが切り
  // 替わった際に、古い位置が可視のまま居座らないようにする(例えば、
  // ^P/^N履歴閲覧/ライブなプロンプト編集の両方でそれを使うSHELLを
  // 離れて、代わりにContentView自身の行ハイライトに頼り、自分自身の
  // カーソル位置をアサートすることが無い素のディレクトリ一覧へ戻る
  // 場合)。TerminalDisplay/RecordingDisplayでは何もしない: -cの本物の
  // ハードウェアカーソルと、このテスト用ダブル自身のmoveCursor()
  // ベースの記録は、そもそも別個の永続的な「可視カーソル」状態を
  // 蓄積したことが一度も無い(なぜGraphicalDisplayだけがこの分離を
  // 必要としたかについてはsetVisibleCursor()自身のコメント参照)
  // ため、クリアすべきものが何も無い。
  virtual void hideVisibleCursor() = 0;

  // -- 消去 / スクロール --
  virtual void clearScreen() = 0;
  virtual void clearToEndOfLine() = 0;
  virtual void scrollForward() = 0;
  virtual void scrollReverse() = 0;

  // -- 属性 --
  virtual void setAttribute(Attribute attr) = 0;
  // その属性の生のエスケープ文字列、他の場所で組み立てたテキストへ
  // インラインで埋め込む必要がある呼び出し側のため(legacyでの使用例:
  // shell.cは、vt_geta(A_md)/vt_geta(A_me)をbashのPS1文字列へ直接
  // 継ぎ込み、bash自身がその前後で太字オン/オフを出力するように
  // している)。端末がその属性に対応していなければ空文字列を返す。
  virtual std::string attributeEscape(Attribute attr) const = 0;
  // Attribute::Color自身のコメント参照: 以後putChar()する文字の前景色/
  // 背景色(kDefaultColorで既定色へ戻す)。Highlight/HighlightCurrentが
  // 有効な間はそちらの色が優先される。色を表現できない実装(テスト用の
  // 記録ディスプレイ等)のため、既定では何もしない。
  virtual void setColor(int /*fg*/, int /*bg*/) {}

  // -- 端末のジオメトリ --
  virtual int rows() const = 0;
  virtual int columns() const = 0;
  virtual void setSize(int rows, int columns) = 0;

  // 2026-08-30、直接のユーザー依頼(HELP.jp/HELPの書き換え、"-g"/
  // "open -a"の時だけ表示される章を追加したい): 現在のバックエンドが
  // GraphicalDisplay(GUI)かどうか。デフォルトはfalse――TerminalDisplay
  // 自身は一切オーバーライドしない(CUIは常にfalseのまま)。
  // DelegatingDisplay::isGraphical()はbackend()へ転送するので、
  // Application側は^Ogによるバックエンド切り替えを何も気にせず
  // 呼ぶだけで常に「今現在の」正しい値が返る――Application::
  // wrapHelpText()自身のコメント参照。
  virtual bool isGraphical() const { return false; }

  // 与えられたキーに対して端末が送る生のエスケープシーケンス、将来の
  // KeyInputの生バイトストリームパーサのため。非対応なら空。
  virtual std::string keySequence(Key key) const = 0;

  // グローバルな出力抑制スイッチ(legacy: vt_mode('D')/'d'/-'d')。
  // 無効の間は、このインターフェースのどのメソッドも実際の画面に
  // 触れてはならない(標準出力への書き込みもOS/端末ライブラリの
  // 呼び出しも無し)――実装は、I/Oを行う全メソッドをこれでガード
  // しなければならず、一部だけではいけない(legacyのCはこの点に
  // ついて一貫していなかった――どこがそれを引き締めているかは
  // terminal_display.cpp参照)。
  virtual bool outputEnabled() const = 0;
  virtual void setOutputEnabled(bool enabled) = 0;

  // 「スムーズスクロール」(legacy: vt_mode('H')/'h'/-'h'、^Oh
  // キーとusilicrcのSCROLL設定で切り替わる)。画面のどれだけを
  // スクロール領域がカバーするかを変える。正確な行の算術は、
  // 全ての呼び出し側がそれぞれ再計算するのではなく、このsetterの
  // 裏に隠された実装の詳細である(legacyは、その約6箇所の呼び出し
  // 箇所の全てで`vt_cs(vt_row(0) - (vt_mode('H') ? 1 : 2), 0)`を
  // 繰り返していた)。
  virtual bool smoothScrollEnabled() const = 0;
  virtual void setSmoothScrollEnabled(bool enabled) = 0;

  // 2026-09-12追加、直接のユーザー依頼(項目⑫、^Ohの再割り当て):
  // モードライン(画面最終行)の表示/非表示。^Ohキーとusilicrcの
  // 起動時デフォルト値(History永続化)で切り替わる――smoothScrollEnabled()
  // 自身の設計と全く同じ形(正確な行算術はこのペアの裏に隠す)。
  // falseの間は、Application::layoutViews()が全ContentViewへ渡す
  // contentHeightがdisplay_.rows()そのもの(1行減算無し)になり、
  // ContentView::renderModeLine()はこの値を見て実際の描画を丸ごと
  // 抑制する(自身のコメント参照)。TerminalDisplay側はさらに
  // スクロール領域(DECSTBM)もrows_全体へ広げる(scrollRegionArgs()
  // 参照)。GraphicalDisplay側は独立したモードライン専用の描画要素を
  // 持たない(ContentViewが渡す行データをそのままセルグリッドへ描く
  // だけ)ため、同じ仕組みだけで両バックエンドに効く。
  virtual bool modeLineVisible() const = 0;
  virtual void setModeLineVisible(bool visible) = 0;

  // 2026-09-13追加、直接のユーザー依頼(項目458フィードバック、
  // "-cで感じたのは、最低限のインジケーターはあったほうがいいって
  // こと...ブロックカーソルによるスクロールバー風の構造って作成可能?"):
  // このバックエンド自身が、常時表示される本物のスクロールバーを
  // 独自に持っているかどうか。GraphicalDisplayはtrue(drawScrollbar()/
  // drawIdleScrollbarImage()、モードラインの表示/非表示に関わらず
  // 常時右端に描かれる)、TerminalDisplayはfalse――ContentView::
  // renderScrollIndicatorOverlay()は、この値がfalseかつ
  // modeLineVisible()がfalseの間だけ(=モードラインを消した代わりの
  // 最低限の代替インジケーターとして)、既存の行の右端1文字を反転
  // 表示のブロックで上書きする、文字セル版の簡易スクロールバーを
  // 描く。GraphicalDisplayでは自前の本物のスクロールバーが常に既に
  // あるため、この文字オーバーレイは常にfalseを返して無効化する。
  virtual bool hasNativeScrollbar() const = 0;

  // 画面へ入る/画面から出る(legacy: ti/te能力経由での端末の代替
  // スクリーンへの出入り)。注記: legacyのtc.cはte(exit_ca_mode)を
  // 取得していたが、どこでも実際には一度も呼んでいなかった――usilic
  // は終了時に常に代替スクリーンをアクティブなままにしていた。
  // close()はそれを修正する――terminal_display.cpp参照。
  virtual void open() = 0;
  virtual void close() = 0;

  // legacyのvt.c自身のvt_init(-2)/vt_init(2)ケースからの移植――実際の
  // ユーザーからの質問(2026-08-20、docs/HISTORY.md:9023、この
  // セッション自身のSIGTERM/raw mode作業を診断した直後に提起された):
  // 「-cでも起きるんだが、^Jp'vi CLAUDE.md'[return]は問題無い。less
  // とviの違いって何？」(なぜPROGRAMモードから起動した「最初の」
  // `less`は表示が崩れるのに`vi`は決して崩れないのか、そしてなぜ
  // `vi`を一度実行した後は「後の」`less`さえも問題無く動くのか)。
  // legacy自身のvt_init()のソースを直接読んで根本原因を特定: 外部
  // プログラムのためにraw modeを離れる(`case -2`)際に、スクロール
  // 領域も`vt_cs(vt_row(0) - 1, 0)`経由で「画面全体」へリセットされ、
  // 再突入(`case 2`)は同じ`vt_cs()`呼び出し経由でそれを
  // `vt_row(0) - 2`(usilic自身のモードライン用に最後の行を確保)へ
  // 再び制限し直す――この移植自身のKeyInput::leaveRawMode()/
  // reenterRawMode()(ProgramMode::run()自身のペア)は、vt_init(-2)/(2)
  // のtermios半分だけを移植し、スクロール領域半分を完全に落として
  // いた。TerminalDisplay自身のapplyScrollRegion()(自身のコメント
  // 参照)は起動時にスクロール領域を0行目..rows-2行目に制限し、外部
  // プログラムへ引き渡す前にそれを一度もリセットしない――そのため
  // `less`はその同じ制限された領域を継承し、端末自身の最後の行を
  // 通常のようには使えない。`vi`/`vim`は、起動時の自身の積極的な
  // 端末(再)初期化の一環として、(何を継承していたかに関わらず
  // terminfo経由で)無条件にスクロール領域自体をリセットすることで
  // よく知られている――これが報告の両方の半分を説明する: `vi`は
  // このギャップに関わらず常に自己修正し、一度実行された後は、端末
  // 自身の(プロセスレベルではなくカーネル/端末エミュレータレベルの)
  // スクロール領域の状態が、usilic自身の次の再描画が下の
  // reenterFromExternalProgram()経由で自身の制限された領域を再確立
  // するまで、「後の」`less`を含め次に何が実行されても画面全体への
  // リセットのまま残る、というサイクルが完成する。GraphicalDisplay
  // にはスクロール領域という概念が全く無い(自身の何もしない
  // オーバーライド参照)――これは「-g起動では最初からOK」(元の報告に
  // ある実際の裏付けとなる詳細)と正確に一致する。-g自身のPROGRAM
  // 起動経路(runProgramInLaunchingTerminal())は、この移植自身の
  // Displayが一切触れない、完全に別個の本物のターミナルウィンドウを
  // 使うため。
  virtual void leaveForExternalProgram() = 0;
  virtual void reenterFromExternalProgram() = 0;
  // 2026-09-28追加、直接のユーザー報告("^Zを投入するとその場サスペンド
  // なので画面が煩雑...画面最下行でよろしく(これは旧がお手本)"):
  // ^Z(Application::handleSuspend())でシェルへ戻る直前に呼ぶ。legacyの
  // vt_init(-2)(key.cのM_susp)と同じく、スクロール領域を全画面へ戻し、
  // カーソルを最下行の先頭へ移してその行を消す――画面の内容自体は
  // 消さない(leaveForExternalProgram()との違い)。スクロール領域も
  // カーソルも持たない表示(GraphicalDisplay等)では何もしない。
  virtual void leaveForSuspend() {}
  // 2026-09-28追加(Application::activeView()から、EPUBの縦書き(Application::
  // rebuildEpubVertical())を見せている間だけtrue): 字を自前で描くGraphicalDisplay
  // は、縦書き向けに小書きの仮名を右上へ、1字だけの半角を2セルの中央へ寄せ、
  // 「ー」(縦書きでは「｜」)とダッシュをセルの高さいっぱいの縦線にしてつなげ、
  // 「：」等を90度回して描く(直接のユーザー報告"-gなので改善を期待")。端末は
  // 字の置き方を変えられないので何もしない。
  virtual void setVerticalTextHint(bool) {}
  // ルビ(Attribute::RubyBase参照)を親字に添えて描けるか(-gだけ)。
  virtual bool drawsRuby() const { return false; }
  // 次に書く字(親字の最初の字)に読みを添える。spanは親字の幅(横書きは桁、verticalなら
  // 縦書きの行=マスの数)。縦書きは読みを列の右の行間に、横書きは親字の上に描く。
  virtual void setPendingRuby(const std::string &reading, int span, bool vertical) {
    (void)reading;
    (void)span;
    (void)vertical;
  }
  // 2026-09-30追加、直接のユーザー報告("慣性スクロールの左右の端に行が突然現れる動きが
  // 縦方向の時と違うので違和感がある。縦と同じにして"): 縦書きの左右のドラッグ/慣性で
  // 本文を横にずらした時(GraphicalDisplay::setContentOffsetX())、画面の外にあった列が
  // 端から少しずつ見えてくるように、その列を含む画面を裏に描いておく。begin〜endの間の
  // 書き込みは画面ではなく裏の画面へ行き、flush()もしない。sideは+1で先(左、
  // kEpubVerticalEdgeColumns列だけ先から描いた画面)、-1で前(右、同じだけ前から)。
  // 端末は横に少しずらせないので何もしない。
  virtual void beginContentEdgeCapture() {}
  virtual void endContentEdgeCapture(int side) { (void)side; }

  // モードライン行の1要素――ハイライトされた(反転表示/「standout」)
  // フィールド(ファイル名、位置インジケータ、末尾のパス)か、2つの
  // フィールドの間の、ハイライトされていない素の1スペースの隙間の
  // どちらか。これが1本のフラットな文字列ではなく「並び」として
  // 存在する理由については、ContentView::scrollModeLineSegments()
  // 自身のコメント参照: 実際のスクリーンショット比較(2026-08-07)
  // により、renderScrolled()のモードラインが1本の途切れないハイライト
  // バーとして描画されている一方、通常の(スクロールしていない)
  // ContentView::renderModeLine()経路は、legacy自身のdisp_window()
  // (フィールドごとに個別のvt_stand()/vt_endstand()呼び出し、組み
  // 立てた文字列全体を囲む1回の呼び出しではない)と一致して、
  // フィールド間に正しく小さな黒い隙間を表示することが判明した。
  struct ModeLineSegment {
    std::string text;
    bool standout = true;
    // 2026-09-03追加、直接のユーザー報告("右下透かしも消えたまま"):
    // ContentView::renderModeLine()自身のModeLineFields::rightWatermark
    // ("usilic v1.5b"等)は反転表示ではなく「太字」で右揃えされる
    // (自身のコメント参照)――standoutだけでは表現できない、単独の
    // 3値目。standout==falseかつbold==falseの素のギャップ(半角
    // スペース)とはこれで区別できる。
    bool bold = false;
  };

  // 2026-08-21にここへ移設(以前はpipe_buffer.hppの独立したfm::
  // AttributeRunだった)。これにより、下のrenderScrolled()が、
  // pipe_buffer.hpp自身の「この」ヘッダーへの依存を循環させることなく
  // (pipe_buffer.hppは既にAttributeのためdisplay.hppをincludeして
  // おり、display.hppはそれを逆にincludeし返すことができない)、
  // ScrolledLineを通して太字/下線の情報を運べるようになる。実際の
  // ユーザー報告(「マウスの左クリック（画面ドラッグスタンバイ状態）
  // 中だと文字装飾が利かず」――HELP自身の太字/下線付きの単語が、
  // ドラッグ中は素のまま描画される、renderTextRun()自身の既存の
  // コメント「No per-glyph bold info reaches this function...
  // always NORMAL」により確認済み)が、ついにこのギャップを閉じる
  // 具体的な理由を与えた。`fm::AttributeRun`(このクラスのすぐ下に
  // ある名前空間スコープのエイリアス)は、既存の全呼び出し元
  // (ContentView::Line::attributes、PipeBuffer自身のStyledLine、
  // CutMode/GrepMode自身のオーバーストライク属性の配線)を変更無く
  // コンパイルできるようにしている。
  struct AttributeRun {
    int start;  // 所有する行自身のテキストへのバイトオフセット
    int end;
    Attribute attribute;
    // attribute==Colorのときだけ意味を持つ(Attribute::Color自身のコメント参照)。
    int fg = kDefaultColor;
    int bg = kDefaultColor;
  };

  // 1本のrenderScrolled()の行: `text`に、他の全ての場所で既に
  // ContentView::Line::attributes/PipeBuffer::StyledLine::attributes
  // が運んでいるのと同じ太字/下線のバイト範囲ランを加えたもの――
  // StyledLine自体を再利用するのではなくこれがここにある理由に
  // ついてはAttributeRun自身のコメント参照(このヘッダーは
  // pipe_buffer.hppに依存できない)。
  struct ScrolledLine {
    std::string text;
    std::vector<AttributeRun> attributes;
    // ContentView::Line::ellipsisEligible自身のコメント参照(2026-09-02
    // 追加、直接のユーザー報告: -gでのマウスクリック/ドラッグ中、
    // DirectoryModeのls一覧行のような、決してellipsisEligibleでない
    // (常にfalseがデフォルトの)行の途中に"..."が誤って挿入され、
    // ファイル名を誤認しているように見えた)。GraphicalDisplay::
    // renderScrolled()自身がこのフラグに基づいてmiddleEllipsisTruncateByWidth()
    // の適用を条件分けする――ContentView::renderLine()自身の同じ
    // 条件分岐(このフラグがfalseの行は、省略記号ではなく、収まる
    // 分だけをプレーンに描き、残りは既存のSDLクリップ矩形にまかせて
    // 静かに切り落とす)と一致させるため。
    bool ellipsisEligible = false;
  };

  // -- スムーズな(ドラッグ/慣性)スクロール描画 --
  // `lines`(見えている部分だけでなく「全」内容)を、行`topIndex`が
  // その通常(offset==0)の位置より`pixelOffsetY`ピクセル下に位置する
  // 状態で描画し、`modeLineSegments`(ContentView::
  // scrollModeLineSegments()参照)を固定された最下行に描画する――
  // GUIドラッグスクロール機能(docs/ideas/IDEAS.mdの「猫の手」要望)
  // を支える描画プリミティブ。このインターフェースへ移動した
  // (2026-07-31、以前はGraphicalDisplay専用だった)ことで、呼び出し
  // 側(main.cppのrunGuiPhase())は、GraphicalDisplay固有のメソッドへ
  // 手を伸ばすのではなく、他の全描画呼び出しと同じDisplay抽象化を
  // 経由するようになる。
  //
  // `pixelOffsetY`は本質的にピクセル/ドット粒度である: 正の値は
  // 全てを「下」へシフトし(より前の内容を露出させる、下方向への
  // ドラッグ)、負の値は「上」へシフトする。それを持つ実装
  // (GraphicalDisplay)では-cellHeightPixels() < pixelOffsetY <
  // cellHeightPixels()を満たさなければならない。本物の端末には
  // サブ行アドレス指定が全く無い――実端末のドラッグスクロール
  // フォールバック(main.cppのrunCoastLoop()/runTerminalMouseDrag()、
  // CLAUDE.mdの2026-07-30の記述参照)は必然的に行単位であり、この
  // メソッド経由ではなく直接ContentView::redraw()を駆動するので、
  // 現在TerminalDisplay自身の実装を呼ぶものは何も無い――それでも
  // これは本物の、正しい、行単位の描画(pixelOffsetYはその符号以外
  // そこでは効果を持たない)であり、スタブのまま放置するのではなく、
  // Displayに依存しないスムーズスクロール描画を望む将来のどんな
  // 呼び出し元のためにも独立して使える状態に保ってある。
  virtual void renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                               const std::vector<ModeLineSegment> &modeLineSegments) = 0;
};

// Display::AttributeRun自身のコメント参照: 素のfm::AttributeRun
// エイリアスとして残してあるので、既存の呼び出し元すべて
// (ContentView::Line::attributes、PipeBuffer::StyledLine::attributes、
// CutMode/GrepMode自身のオーバーストライク配管)が、AttributeRun自身の
// 定義がpipe_buffer.hppからここへ移った後もそのままコンパイルできる。
using AttributeRun = Display::AttributeRun;

// 2026-10-01追加、直接のユーザー依頼("span class=\"em_accent\"の実装"): EPUBの傍点は、字ごとに
// 傍点の字を読みにしたルビ(Display::Attribute::RubyText、epubToHelpMarkup()参照)にしてある。
// `reading`(《》を除いた読み)がその傍点の字か。ルビを描けない端末(-c)は、この読みを《》ごと
// 書かない(「そ《﹅》れ《﹅》」にしない)。
inline bool isEmphasisMarkReading(const std::string &reading) {
  for (const char *mark : {"\xEF\xB9\x85", "\xEF\xB9\x86", "\xE2\x97\x8E", "\xE2\x97\x89", "\xE2\x97\x8B",
                           "\xE2\x97\x8F", "\xE2\x96\xB3", "\xE2\x96\xB2", "\xE2\x97\xA6", "\xE2\x80\xA2"})
    if (reading == mark) return true;
  return false;
}

}  // namespace fm
