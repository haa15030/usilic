#pragma once

#include <string>
#include <vector>

namespace fm {

// 抽象的な画面: テキスト出力、カーソル移動、属性、端末のジオメトリ。
// tcb/src/tc.c + vt.cからの移植で、これらは(tcb.hのVT構造体を見ると
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
  };

  enum class Key { Up, Down, Left, Right, PageUp, PageDown };

  virtual ~Display() = default;

  // -- raw output --
  virtual void write(const std::string &text) = 0;
  virtual void putChar(int c) = 0;
  virtual void flush() = 0;

  // -- cursor --
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

  // -- erasing / scrolling --
  virtual void clearScreen() = 0;
  virtual void clearToEndOfLine() = 0;
  virtual void scrollForward() = 0;
  virtual void scrollReverse() = 0;

  // -- attributes --
  virtual void setAttribute(Attribute attr) = 0;
  // その属性の生のエスケープ文字列、他の場所で組み立てたテキストへ
  // インラインで埋め込む必要がある呼び出し側のため(legacyでの使用例:
  // shell.cは、vt_geta(A_md)/vt_geta(A_me)をbashのPS1文字列へ直接
  // 継ぎ込み、bash自身がその前後で太字オン/オフを出力するように
  // している)。端末がその属性に対応していなければ空文字列を返す。
  virtual std::string attributeEscape(Attribute attr) const = 0;

  // -- terminal geometry --
  virtual int rows() const = 0;
  virtual int columns() const = 0;
  virtual void setSize(int rows, int columns) = 0;

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

// See Display::AttributeRun's own comment: kept as a bare fm::AttributeRun
// alias so every existing caller (ContentView::Line::attributes,
// PipeBuffer::StyledLine::attributes, CutMode/GrepMode's own overstrike
// plumbing) keeps compiling unchanged after AttributeRun's own
// definition moved here from pipe_buffer.hpp.
using AttributeRun = Display::AttributeRun;

}  // namespace fm
