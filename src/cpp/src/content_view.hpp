#pragma once

#include <algorithm>
#include <functional>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "display.hpp"
#include "pipe_buffer.hpp"  // AttributeRun -- see Line::attributes' own comment

namespace fm {

// 定義はcontent_view.cpp。文字列`text`を、必要なら先頭/末尾を残して
// 中間を"..."で置き換え、画面幅`maxWidth`列以内に収める(既に収まって
// いれば無変更)――ContentView::renderLine()自身が長い行の表示に使う
// のと同じロジック。GraphicalDisplay::renderScrolled()(graphical_display.cpp、
// 通常のContentView::redraw()を一切経由しない別の描画経路)からも
// 同じ切り詰めを適用できるよう公開している――2026-08-30、直接の
// ユーザー報告(GUI、実際のマウスドラッグ中に「...」省略が反映され
// ない件)参照。
std::string middleEllipsisTruncateByWidth(const std::string &text, int maxWidth);

// 2026-09-03追加、直接のユーザー報告("/Users/haa15030/（ls）画面に
// 収まらないファイル名...を Screensho...5-15-13.png で表示よろしく"):
// DirectoryModeのls一覧/WORK/arc.Nバナー(highlightStartColumn自身の
// コメント参照、常にプレーンなASCIIの固定接頭辞――権限/サイズ/日付
// 等)+ハイライト対象の可変長部分(ファイル名)という形の行のための、
// middleEllipsisTruncateByWidth()の亜種。ellipsisEligible(自身の
// コメント参照)は行「全体」を中間省略する――LocateMode/DuMode/
// ArchiveModeの離散的な名前/パス行には正しいが、DirectoryModeの
// ような固定接頭辞付きの行にそのまま使うと、接頭辞自身の途中に
// "..."が入ってしまい壊れる(=fe/=ff自身の教訓、renderScrolled()側
// で全く同じ理由でellipsisEligibleを無条件適用した時に起きたのと
// 同じ壊れ方)。この関数は`highlightStart`より手前(接頭辞)は一切
// 変更せず、`highlightStart`以降(ファイル名)だけをFinderスタイルで
// 中間省略する――`text`が既に`maxWidth`列に収まっていれば無変更。
// ContentView::renderLine()自身と、main.cpp経由のGraphicalDisplay::
// renderScrolled()の両方から同じ結果を得るために公開している
// (middleEllipsisTruncateByWidth()自身と同じ理由)。
std::string truncateHighlightedSuffix(const std::string &text, int highlightStart, int maxWidth);

// 2026-09-13追加、直接のユーザー依頼(項目458フィードバック、モード
// ラインを消した代わりの最低限のインジケーターとして"ブロックカーソル
// によるスクロールバー風の構造")、同日項目459フィードバック("スク
// ロールバーがクネクネになる...それ用のエリアが必要...-gと同じく
// 常時")で改訂: GraphicalDisplay::drawScrollbar()のつまみ位置/高さの
// 計算(トラック高さに対する可視割合・スクロール位置に応じた比例
// 配置)を、ピクセル単位ではなく端末の行単位に移植したもの。戻り値は
// {つまみの先頭行(ビューポート内、0起点)、つまみの行数}。
// scrollableLines(=total-height)が0以下(スクロールする余地が無い)の
// 場合は{0, 0}を返し、呼び出し元は何も描かない――専用の2列は常時
// 確保されているものの、スクロール不要な画面を反転表示で塗り潰す
// ことに実用上の利点は無い(GraphicalDisplay::drawScrollbar()自身は
// 可視割合100%でもトラック全体を塗るが、そちらに厳密に合わせる
// 必要は無いと判断した)。
std::pair<int, int> scrollThumbRowRange(int topIndex, int height, int total);

// スクロール可能なテキスト行の一覧: 1ページずつ描画し、カーソル行を
// 追跡し、利用可能な場合はDisplayのハードウェアスクロール領域経路
// を含めて、一覧を通して(1行単位またはページ単位で)スクロールする。
// disp.cのput_str()/disp_pp()とscroll.cのmove_page()/move_row()/
// sr_row()/sf_row()からの移植。
//
// legacyのCからのデータモデルの変更: PPは手作りの双方向連結リスト
// (prev/nextポインタ)だった。std::vectorが存在する前に利用可能な
// 唯一の選択肢だったため。ContentViewは代わりに、行をインデックス
// ベースのカーソル/ビューポート追跡付きのプレーンなstd::vector<Line>
// に保持する――legacyのRV構造体は3つの別々のポインタ(ps = ビュー
// ポート開始、pe = ビューポート終了、pp = カーソル)を追跡していた。
// 連結リストには「自分の可視ウィンドウの終わりまでどれだけか」を
// O(1)で知る方法が無いので、それを明示的に歩いて覚えておく必要が
// あったためだ。vectorでは、ビューポートの終わりは単に
// `topIndex_ + viewportHeight_`という導出値なので、同期させておく
// べき別のものが無い(そして、スクロールの副作用として正しく保つ
// べきPP::prev/nextの連鎖も無い)。
//
// 意図的に除外(将来のクラスに属する、ContentViewではない):
//   - disp_window()/print_line()/disp_prev()/disp_copy(): 下端の
//     モードライン/ステータスバーの描画(ファイル名の切り詰め、
//     それ自身の対話的な行ジャンプ入力ウィジェットを持つ行カウン
//     ター、前モードのパンくず、PROGRAM名の表示)。これは各モード
//     自身のフォーマットに深く特化したステータスバーの描画であり、
//     スクロール可能な行の一覧を見せることの一部ではない――将来の
//     StatusBar/Modeの領域。
//   - get_col()/init_col(): ファイル選択のためのモードごとの列
//     オフセット計算(既に歴史的に修正済み、CLAUDE.mdの項目13参照)
//     ――Modeの仕事であり、スクロール/描画とは無関係。
//   - 文字ごとの属性ラン(legacyのPP::attr、例えばGREPマッチの
//     ハイライトやhex-dumpモードのバイト着色に使われる)と、それに
//     紐付くput_str()内のK_a0番兵バイト処理: 両方とも、まだ存在
//     しない内容の前処理(将来のBuffer/PipeBufferクラスが生み出す
//     仕事)に依存する。ここで「移植されている」のは、put_str()の
//     他の2つの、その前処理を必要としない描画動作である: カーソル
//     行の行全体の反転表示と、生の制御バイトに対する反転表示――
//     renderLine()参照。
//   - 文字コードの認識: LineEditorとは異なり、ContentViewはバイト
//     オフセットを画面の列へ変換する必要が決して無い(行全体を印字
//     し、端末に自然に折り返し/進めさせる)し、制御バイトの検出
//     (< 0x20)は全てのエンコーディングで同一である。ここで移植
//     すべき文字コードに依存するものは残っていないので、このクラス
//     はCharset::Encodingを全く取らない。(モードラインフィールド
//     自身のオーバーフロー処理、2026-07-31に追加、はこれと一貫して
//     いる: マルチバイト文字を分割してしまうかもしれないバイト
//     オフセットで切り詰めるのではなく、フィールド全体を省略する
//     ――composeModeLineRow()自身のコメント参照――のでこのクラスは
//     依然として文字の幅を知る必要が全く無い。)
class ContentView {
 public:
  struct Line {
    std::string text;
    // 実際に送信されたコマンドが紐付いているSHELLのプロンプト行に
    // だけ設定される(Shell::submitLine()自身の使用)――ユーザーが
    // カーソルをこの行に戻してEnterを押した場合に再送信すべき
    // 正確なテキストで、legacyのshell.cが、カーソルが移動できる
    // スクロールバック+プロンプト履歴全体を「1つの」バッファとして
    // 扱っていたことからの移植(Shell自身のクラスコメント参照)。
    // ContentViewの他の全ての呼び出し元(DirectoryMode、FileMode、
    // GrepMode等)はこれをデフォルトのnulloptのままにする。
    // ContentView自体は決してこれを読まない――純粋に不透明な、
    // Shellが付加し、後で自身のlines_に対して調べる行ごとのデータ。
    std::optional<std::string> shellCommand;

    // カーソル行自身の反転表示ハイライトが「開始」すべきバイト
    // オフセット(== 画面の列。このクラスがなぜ2つを決して区別
    // しないのかについてはクラスコメント参照)。この行がカーソル行
    // である場合。nullopt(デフォルト)は「列0から」を意味し、既存の
    // 全呼び出し元が依然として受け取る、以前の無条件の行全体の挙動
    // である。scroll.cのget_col()/init_col()からの移植: legacyの
    // `ls -alF`スタイルのディレクトリ一覧に対するカーソルハイライト
    // は、列0からではなくファイル名から始まる(stand_str()の
    // `&rvp->pp->cs[rvp->col]`)――実際の並べたCUI比較スクリーン
    // ショット(2026-08-03)により、この移植のディレクトリ一覧が、
    // 名前と一緒に権限/サイズ/日付の接頭辞全体を誤って反転表示して
    // いることが発見された。今日これを設定する唯一の呼び出し元は
    // DirectoryModeである。他の全てのContentViewの利用者
    // (locate/grep/history/man/...一覧)は、legacyでも正当に行全体を
    // ハイライトする(それらのモードに対してinit_col()は0を返す)
    // ので、それらに対してこれをnulloptのままにしておくことは
    // 見落としではなく正しい。
    std::optional<int> highlightStartColumn;

    // 太字/下線のバイト範囲(2026-08-05)、例えばPipeBuffer自身の
    // オーバーストライク検出("X\bX"太字、"_\bX"下線)が既に生の
    // ファイル内容から解析してAttributeRun{start,end,
    // Display::Attribute}にしたもの――PipeBuffer自身のクラス
    // コメント参照。以前は検出はされていたが至る所で捨てられていた
    // (FileModeのloadFile()/reflow()はStyledLine::textだけを保持
    // し、CutModeは内容をPipeBuffer経由に全くルーティングしていな
    // かったので、HELP.jp自身の太字の節番号/見出しの装飾は、File
    // モードでもHELP/CUTでも見えなかった)――実際のユーザーからの
    // 質問("HELP.jp画面内文字列の装飾（ボールド・アンダーライン）
    // は簡単？")が、これを既知のギャップとして残すのではなく配線
    // することを促した。他の「全ての」呼び出し元(DirectoryMode、
    // GrepMode、ListBrowserMode等)に対してはhighlightStartColumnと
    // まさに同様に空――それらはそもそもオーバーストライク形式の
    // 内容を一度も持ったことが無い。
    std::vector<AttributeRun> attributes;

    // この行を、収まらない時のrenderLine()自身のFinderスタイルの
    // 中間省略記号("...")切り詰めに参加させる――2026-08-20の追加、
    // 実際の報告+スクリーンショット(=dn/=do、-gモード特有):
    // 長い行を持つ通常のテキストファイル(docs/HISTORY.md)を閲覧
    // すると、全ての長い行自身の「中間」が"..."に置き換えられ、
    // 実際の内容を破壊していた。一方GraphicalDisplay「自身」の
    // 慣性スクロール描画経路(renderScrolled()、renderLine()を全く
    // 呼ばない、完全に別のコードパス)は、スクロールが始まると
    // 全く同じ行を完全に表示していた――この不整合こそが、決して
    // 意図されたことのなかった描画の違いではなく、実際にこのバグを
    // 表面化させたものである。既存のゲート(さらに下のこの関数
    // 自身のコメント参照)は「highlightStartColumn/attributesが無い」
    // ことであり、LOCATE/DU/ArchiveMode風の結果一覧だけがその方法で
    // renderLine()に到達するという前提に基づいていた――「誤り」:
    // FileMode自身の通常の内容行(テキストファイルにも行ごとの
    // ハイライトデータが無い)は、まさに同じ条件を満たすので、任意の
    // ログ/散文の行が、継続的なテキストには意味を成さない(人間は
    // それを開始から...どこで切られようとその通りに読むのであって、
    // 開始+末尾を読むのではない)、Finderスタイルのファイル名が
    // 望む「中間を省略し、両端を保つ」という同じ扱いを受けていた。
    // falseがデフォルト(通常の内容に合わせたプレーンな右切り詰め)
    // ――LocateMode/DuMode/ArchiveMode自身の結果一覧のLine構築は
    // これを明示的にtrueに設定する(それら「自身」の各行は、実際に
    // Finderスタイルの省略記号がまさに正しい判断である、1つの離散
    // した名前/パスであり、この機能が構築された元々の2026-08-14の
    // 報告)。
    bool ellipsisEligible = false;

    // 2026-08-28の追加、直接のユーザー報告("Boot optionsは文字列
    //選択の場面ではないがブロックカーソル位置は「WORK : 」と同じで
    // よろしく"): この行は、cursorIndex_がそこに着地した場合でも
    // markedPredicate_がそう言った場合でも、カーソル行/マーク行の
    // SGRハイライト(Standout/Underline)に決して全く参加しない――
    // 特にexecuteShowUsilicBanner()自身の「Boot options」行のため。
    // これは選択可能な項目ではなく(openSelectedBannerEntry()自身の
    // コメント参照)、決してそれのように「見えて」はならないが、
    // それでも実際の端末のキャレットが、古い「highlightStartColumn
    // をテキストの先へ向ける」抑制のトリックが必要としていたように、
    // それ自身の(WORKの通常空とは異なり可変長の)値テキストを越えて
    // 押し出されるのではなく、WORK/ERROR等自身のhighlightStartColumn
    // が使うのと「同じ」列に位置してほしい(それらが共有する「値の
    // 前面」慣習に合わせている)。他の全ての呼び出し元に対しては
    // デフォルトでfalse(影響なし)。
    bool neverHighlighted = false;
    // 2026-09-30追加(Display::setPendingRuby()参照): このバイト位置の字に添えるルビ。
    // 縦書き(renderEpubVerticalWindow())が、列の右の行間に描く読みを渡すのに使う。横書きの
    // ルビは装飾のラン(Display::Attribute::RubyBase/RubyText)から求める(renderLine())。
    struct RubyMark {
      int byte = 0;
      std::string reading;
      int span = 0;
      bool vertical = false;
    };
    std::vector<RubyMark> rubyMarks;

    // 2026-09-08追加、直接のユーザー報告("usilic画面では98行だった
    // 行が97として表示されている"――幅超過で折り返された1つの生の
    // 行が、cursorIndex_上では複数行として数えられてしまい、
    // Command::StartEdit('e')がエディタへ渡す"+N"がvi自身の実際の
    // 行番号と食い違っていた): PipeBuffer::wrapLine()/StyledLine::
    // rawLineNumber自身のコメント参照――この行が属する、元のバイト
    // 列上での1始まりの「生の」行番号。折り返し継続行は直前の行と
    // 同じ番号を引き継ぐ。FileModeだけが設定する(幅超過による折り
    // 返しが起こり得る唯一の呼び出し元、file_mode.cpp参照)――他の
    // 全呼び出し元(DirectoryMode等、そもそも折り返しの概念が無い)は
    // nullopt(デフォルト)のままにする。
    std::optional<int> rawLineNumber;
    // 画像の行(2026-10-03、横書きのEPUBの画像、FileMode::readAndWrap()参照): 画像(Display::ContentImage
    // のkey)をimageRows行の帯に収めて描き、この行はその上からimageRow番目。字は空。
    std::string imageKey;
    int imageRow = 0;
    int imageRows = 0;
    // 反転表示をこのバイトオフセットの手前で止める(2026-10-11、-KのKEYの「：」の手前まで、CutMode::
    // setLabelHighlightEndMarker()参照)。無ければ行末まで(highlightStartColumnと対)。
    std::optional<int> highlightEndColumn;
  };

  explicit ContentView(Display &display);

  // 2026-09-12追加、直接のユーザー依頼(DirectoryMode::
  // applyColumnAutoDisplay()自身のコメント参照、usilicrc新設のCOLUMN
  // ディレクティブ向け): display_自身のcolumns()への単純な転送。
  // display_はこのクラスの他のメソッド内部で既に何度も読まれている
  // が、公開アクセサが無かったため、呼び出し元(DirectoryMode)が
  // 現在の端末幅を必要とする初めてのケースとしてここへ追加した。
  int columns() const { return display_.columns(); }

  // このビューが画面上でどこに住んでいるか(row0..row0+height-1)、
  // 何行分の高さか。
  void setViewport(int row0, int height);
  // ビューポートの上のtop行・下のbottom行を、描くたびに空にする(縦書きのEPUBの上下の余白、2026-10-30、
  // Application::layoutViews()参照)。
  void setMarginRows(int top, int bottom) {
    marginTop_ = top;
    marginBottom_ = bottom;
  }

  void setLines(std::vector<Line> lines);
  // 2026-10-03追加(Display::ContentImage参照、EPUBの縦書きの画像ページ): この画面の本文に描く画像の
  // 置き場所。renderContentRows()が毎回Displayへ渡す(画像の無い画面は空なので、前の画面の画像が残らない)。
  void setContentImages(std::vector<Display::ContentImage> images) { images_ = std::move(images); }
  const std::vector<Line> &lines() const;

  // 現在読み込まれている全ての行の合計バイトサイズ、バッファ
  // 全体の「TOPからENDまで」(ビューポートに見えているものだけでは
  // ない)――legacyのdisp_window()自身の汎用的なprint_arg()が埋める
  // バッファサイズフィールドを反映している(menu.cのメイン再描画
  // ループは、disp_window()自体を呼ぶ前に、本質的に全てのモードに
  // 対してh.bp->sizeでprint_arg()を呼ぶ。モードごとの特別扱いでは
  // ない)。元のバッファ内の全ての本物の行が持っていた改行文字の
  // 分、1行につき+1。元々はMAN/LOCATE/DU/CUT/アーカイブエントリの
  // 内容自身のプロバイダによってインラインで複製されていたプライ
  // ベートなヘルパーだった(Application::contentByteCount()の履歴
  // 参照)――DirectoryMode/ArchiveMode(別の翻訳単位、そのプライ
  // ベートなヘルパーへのアクセスが無い)もそれを共有できるよう、
  // 直接のユーザー依頼("全モードのモードラインに「画面TOPから
  // ENDまでのバイト数」を追加してほしい")に基づき、2026-08-15に
  // 本物のContentViewメソッドへ昇格された。
  std::size_t totalByteCount() const;

  int cursorIndex() const;
  void setCursorIndex(int index);

  // 絶対的な「表示」行(moveCursor()/setViewport()が使うのと同じ
  // 座標空間、ビューポート相対ではない)を、そこに現在描画されて
  // いる行のインデックスへマップする。displayRowがこのビュー自身の
  // ビューポートの外側、またはlines_の末尾を過ぎている(ビュー
  // ポート内の空白行)場合は-1。マウスクリック処理
  // (Application::handleMouseClick()、IDEAS.mdのGUIモードの
  // クリックして選択/開く要望参照)のために追加された――redraw()が
  // 内部で既に計算している(row0_+row <-> topIndex_+row)ことの逆
  // だが、ポインティングデバイスが存在する前は誰もそれを必要と
  // しなかったので以前は公開されていなかった。
  int lineIndexAtRow(int displayRow) const;

  // `column`がlineIndex自身のハイライトされた範囲内に収まるかどうか
  // ――[highlightStartColumn, text.size())。なぜここでバイトオフ
  // セットと画面の列が同じ数なのかについてはLine::
  // highlightStartColumn自身のコメント参照。lineIndexが範囲外なら
  // false。Application::handleMouseClick()自身の使用のために追加
  // (2026-08-05のユーザー依頼): この範囲の「外側」に着地したマウス
  // 押下――DirectoryModeの`ls`スタイルの一覧は、ハイライトが始まる
  // 前に固定幅の左マージン(権限/サイズ/日付)を持っており、左揃えの
  // 一覧(最近使ったファイル/ディレクトリのピッカー等)は、短い
  // エントリ名の後に可変幅の空の末尾を残し得る――は、実際には視覚的
  // にエントリの「上」では決してなかった。エントリへの本物のクリッ
  // クと、「猫の手」画面ドラッグを開始する押下とを区別する、
  // ユーザー自身の提案: タイミング/動作ベースのヒューリスティック
  // (脆弱であることが判明した、Application::handleMouseClick()自身
  // の履歴参照)ではなく、単に押下が「どこに」着地したかをチェック
  // する――エントリ自身の可視のハイライトされたテキストの内側か、
  // そもそもその一部ではなかった周囲のマージンか。
  bool isColumnWithinLineScope(int lineIndex, int column) const;

  // isColumnWithinLineScope()と同じ上限チェック([0, text.size()))
  // だが、highlightStartColumnによる左マージンの除外は行わない――
  // 2026-09-15追加、直接のユーザー依頼(「（ls）、LOCATE等で導入して
  // いた『ドラッグエリア』も不要でいいので反映してみて」)。
  // Application::handleMouseClick()が左クリックの「実行」自体を
  // ボタン解放まで遅らせるようになった(GraphicalDisplay::Impl::
  // pendingLeftClick自身のコメント参照)ことで、そもそもマージン
  // 除外(isColumnWithinLineScope()自身の履歴コメント参照、2026-08-05
  // のユーザー提案)がクリックとドラッグを区別するために存在していた
  // 前提が崩れた――押下位置がスコープに入っていても、それがそのまま
  // ドラッグの開始点として自由に使える。Line::highlightStartColumn
  // 自体(legacyの`ls -alF`に合わせたカーソル反転表示の開始列という
  // 「見た目」の役割)は変更せず、クリックスコープ判定だけをこちらの
  // 緩い方へ切り替えるためにisColumnWithinLineScope()とは別関数に
  // した。
  bool isColumnWithinRowText(int lineIndex, int column) const;

  // 2026-09-02追加、直接のユーザー依頼("ついでに透かしが消える件も
  // よろしく" -- 慣性スクロール/ドラッグ中はDirectoryModeのカーソル
  // ハイライト(Standout)が常に消える件): GraphicalDisplay::
  // renderScrolled()(main.cppのドラッグ/コースト専用の描画経路)
  // 自身のScrolledLine::attributesはFileMode専用のBold/Underline
  // runしか運ばない――DirectoryModeのカーソル/マーク行ハイライトは
  // 完全に別の仕組み(redraw()自身のループのcursorRow/markedRow/
  // markedRowUsesUnderline_)で計算されるので、ドラッグ中は一度も
  // 描かれない。この関数は、redraw()の同じループがlineIndex1行分に
  // ついて計算するのとまったく同じcursorRow/markedRow/
  // markedRowUsesUnderline_の判定を再現し、その行が今まさに(hover/
  // cursor優先順位も含め)ハイライトされるべきなら、注入すべき単一の
  // AttributeRun([highlightStartColumn, text.size())、
  // Standoutまたは(markedRowUsesUnderline_有効時のmarkedRow分)
  // Underline)を返す――呼び出し元(main.cpp)がこれをScrolledLine::
  // attributesへ足し込むことで、renderScrolled()側でも同じ見た目に
  // なる。cursorHighlightEnabled_がfalse、または対象行がneverHighlighted
  // の場合、あるいはそもそもマークされていない場合はnullopt。
  std::optional<Display::AttributeRun> scrollHighlightForLine(int lineIndex) const;

  // 2026-09-03追加: renderLine()自身のellipsisBudgetローカル変数と
  // 一字一句同じ式(cursorHighlightEnabled_ ? columns()-1 : columns())
  // ――「このビュー」全体で1つの安定した値であり、個々の行の
  // marked/underlineMarked状態には「依存しない」(依存させると、
  // カーソルがその行を跨ぐたびに省略記号の位置が1文字分ずれて見える
  // ――renderLine()自身のこの式についてのコメント参照)。main.cpp
  // 経由のGraphicalDisplay::renderScrolled()が、truncateHighlightedSuffix()
  // /middleEllipsisTruncateByWidth()へ渡す予算として、redraw()と
  // 同じ値を得るために公開している。
  int ellipsisBudget() const;

  // lineIndexAtRow()の逆: cursorIndex_自身が現在描画されている絶対
  // 的な「表示」行、または表示範囲外へスクロールされていれば-1。
  // Shell自身の^P/^N履歴閲覧のために追加(2026-08-04):
  // setCursorHighlightEnabled(false)(自身のコメント参照――legacyの
  // stand_str()はSHELL自身の行全体の反転表示ハイライトを完全に
  // スキップする、その`f_M == M_shell`の早期return)の下では、
  // 閲覧中にどの行が「現在」かを示す色付きのバーが残っていない
  // ――代わりにここへ移動される端末自身のプレーンなテキスト
  // カーソルが、legacyの実際の代替である。
  int cursorDisplayRow() const;

  // このビューの現在のcursorHighlightEnabled_状態に合わせて、
  // ディスプレイ自身の可視カーソル(Display::setVisibleCursor()/
  // hideVisibleCursor())を明示的に主張(またはクリア)する: ハイ
  // ライトがOFFの場合(Fileモード自身のケース――プレーンな
  // ファイル内容がなぜ行全体のハイライトを持たないかについては
  // setCursorHighlightEnabled()のコメント参照。それでも実際の
  // 2026-08-04のユーザー依頼が、「現在の」行が-c/-gの両方で依然と
  // して視覚的かつプログラムから知ることができる必要があることを
  // 説明した: 現在の行でエディタを起動すること、そして将来のCOPY
  // モードの行+列ベースの選択は、両方とも、ハイライトされた
  // セレクタ画面のcursorIndex_だけが既に提供しているのと同じ方法
  // で本物のカーソル位置を必要とする)、cursorDisplayRow()にプレー
  // ンなテキストカーソルを表示する。ハイライトがONの場合はそれを
  // クリアする(ハイライトされたセレクタは既に自力で「あなたが
  // どこにいるか」を示している――その上に重ねる2つ目の、冗長な
  // カーソルのグリフは視覚的なノイズになる。そして、それを使って
  // いた「前の」モードから残った古びたカーソル、例えばDirectory
  // モードへ戻った後もFileモードのカーソルがそこに座ったままに
  // なっているのは、まさにhideVisibleCursor()が存在して防ぐべき
  // 種類の残留状態バグである――このパターンの最初の出現について
  // はApplication::returnFromShell()自身のコメント参照)。
  //
  // redraw()/moveUp()/moveDown()等自体からは意図的に自動では呼ば
  // れない: SHELL自身のscrollback_も「同様に」cursorHighlightEnabled_
  // をoffにしているが、生きたプロンプト行を積極的に編集している間の
  // その意味のあるカーソル位置は、cursorDisplayRow()の列0では
  // なく、プロンプト自身の「編集列」(LineEditor::redraw()自身の
  // setVisibleCursor()呼び出しによって設定される)である――redraw()
  // 自体に自動呼び出しを焼き込むと、「無関係な」redraw()が何であれ
  // 実行されるたびに、そのより精密な位置を黙って壊してしまう
  // (この機構全体がまさに排除するために構築された「-gのSHELLでは
  // ...一瞬出て消えるパターン」バグ)。「ユーザーがまさに本当に
  // ナビゲートした」ことを表す呼び出し元(FileMode::loadFile()/
  // reflow()、Applicationの汎用MoveUp/MoveDown/PageUp/PageDown/
  // GoToTop/GoToBottom/Redrawのディスパッチ)は、代わりにこれを
  // 明示的に呼ぶ――SHELLは自身の別の、既に正しい配線
  // (moveDisplayCursorToShellHistoryLine())を保ち、Applicationの
  // 汎用コマンドディスパッチを全く通らない(dispatchByte()自身の
  // Shellモード分岐参照)ので、この2つの機構は実際には決して衝突
  // しない。
  void syncVisibleCursor();

  // 現在可視の最上部の行のインデックス。GUIドラッグスクロールの
  // プロトタイプ(GraphicalDisplay自身のクラスコメント参照)のため
  // にsetTopIndex()と並んで追加された――cursorIndex_を動かさずに
  // ビューポートをスクロールすること(常に2つを結合するmoveUp()/
  // moveDown()とは異なる)は、ほとんどのアプリケーションで本物の
  // トラックパッド/マウスホイールスクロールが動作する方法に合わ
  // せている: それは見えているものを再配置するのであって、選択を
  // 変更するのではない。
  int topIndex() const;
  // CopyMode::moveUp()/moveDown()自身のコメント参照(2026-09-07追加)
  // ――ビューポートの高さ(表示行数)。
  int height() const;
  // 最上部の可視行を直接設定し(有効な範囲に制限される)、
  // cursorIndex_には手を付けない。再描画はしない――カスタムの描画
  // ループを駆動する呼び出し元(ドラッグスクロールのプロトタイプ
  // など)は自身の再描画のタイミングを所有する。
  void setTopIndex(int index);

  // 2026-09-08追加、直接のユーザー要望("ページTOPとENDいがいでは、
  // ^L（描き直し）が投入された場合、ブロックカーソル位置のカレント行が
  // 画面中央に来るようにしてほしい。viの番号指定起動およびemacsの
  // ^Lが同じ仕様"): cursorIndex_が画面の縦方向中央(height_/2)に来る
  // よう、setTopIndex()経由でtopIndex_を設定する。setTopIndex()自身の
  // 既存のクランプ(0〜maxTop)により、バッファのTOP付近/END付近では
  // 自然に「可能な範囲で」寄るだけになる(要望文の「TOPとEND以外では」
  // という条件を、特別扱いのコード無しに自動的に満たす)。再描画は
  // しない(setTopIndex()自身と同じ)――呼び出し元が自分のタイミングで
  // 行う。
  void centerOnCursor();
  // 2026-09-25追加、直接のユーザー要望("`^X^X`連打で元のカーソル行との
  // 間で行き来"、legacyのK-15/16 M_mark/M_jumpに相当――ただし^SPCに
  // よる明示的なマーク設定はCP/MV/RMの選択キーと衝突するため保留の
  // まま、emacs流に「大きく移動する直前」の位置を自動で記録する形):
  // markJumpOrigin()は現在のカーソル行を記録し、ESC </ESC >/スクロール
  // バーのクリック等、大きく飛ぶ操作の直前に呼ぶ。exchangeJumpMark()は
  // 記録位置と現在位置を入れ替える(連打で行き来)。記録が無ければfalse。
  // 内容が丸ごと置き換わる(setLines())と記録は捨てる。
  //
  // 2026-10-09、直接のユーザー依頼(EmacsのC-x C-xの説明=ih.pdfの機能をヤンク以外): 記録は1つではなく
  // Emacsのマークリングと同じく最大kMarkRingSize個の履歴にした。markJumpOrigin()は今の行をリングの先頭へ
  // 積み(先頭と同じ行なら積まない)、exchangeJumpMark()は先頭と今の行を入れ替える。popJumpMark()
  // (EmacsのC-u C-SPC)は先頭の行へ飛び、その記録をリングの最後へ回す(連打で古い記録へ遡る)。
  static constexpr int kMarkRingSize = 16;
  void markJumpOrigin();
  bool exchangeJumpMark();
  bool popJumpMark();

  // topIndex_を`direction`(>0は「より後の」内容を明かし、<0は
  // 「より前の」内容を明かす――GraphicalDisplay::renderScrolled()の
  // pixelOffsetYが使うのと同じ符号の慣習)にちょうど1行動かし
  // 「かつ」cursorIndex_を新しく明かされた端に固定する。
  // scrollLineDown()/scrollLineUp()自身の状態更新と同じだが、それ
  // らの描画の副作用は無い(カスタムの描画ループを駆動する呼び出し
  // 元、例えばmain.cppのGUIドラッグ/惰性スクロールの物理演算は、
  // setTopIndex()と同様、renderScrolled()経由で自身の再描画の
  // タイミングを所有する)。一覧の境界では(何もせず)falseを返す。
  //
  // 実際の報告を修正するために追加された: GUIの惰性/ドラッグ経路
  // は以前、cursorIndex_を、スクロールが始まる「前」にどこにあった
  // かのままに凍結させ(setTopIndex()は意図的にそれに触れない、
  // 自身のコメント参照)、スクロールが落ち着いた時点で一度だけ
  // topIndex_へ同期していた――これは、一覧の末尾まで惰性で行った
  // 後、'j'(scrollLineDown())が行うように下端に追従する代わりに、
  // 今下端までスクロールされたビューポートの「上端」にカーソルを
  // 置いてしまっていた。代わりにこのメソッド経由で継続的に端を
  // 追跡することで、そのスクロール終了時の同期の必要性を完全に
  // 取り除く(cursorIndex_はそもそも決して古びない)。
  bool stepTopIndex(int direction);

  // stepTopIndex()自身の境界条件への、変更を伴わない覗き見――理由
  // については自身の.cppコメント参照(呼び出し元が、行未満の
  // ピクセル移動を確定する前に、事後にのみ境界を発見するのでは
  // なく、「この方向へ動く余地があるか」を事前にチェックできる
  // ようにする)。
  bool canStepTopIndex(int direction) const;

  // cursorIndex_の代わりに任意の行をハイライトする。カーソル/選択
  // は動かさない――2026-07-30の実端末ドラッグスクロールのトリガー
  // (CLAUDE.md参照)のため: 惰性中は、クリックが直接それを開ける
  // ように、マウスポインタの下の行がハイライトされるが、単にホバー
  // しただけで実際のキーボードカーソル位置は動くべきではない。
  // nullopt(デフォルト)を渡すと通常通りcursorIndex_のハイライト
  // に戻る。再描画はしない――setTopIndex()と同じ「呼び出し元が
  // 描画タイミングを所有する」慣習。
  void setHoverIndex(std::optional<int> index);
  std::optional<int> hoverIndex() const;

  // syncVisibleCursor()が、ハイライトされた「内容」行ではなく、
  // 「モードライン」自身のtrailingフィールドのテキスト
  // (ModeLineFieldsProviderの`trailing`が現在そこに表示している
  // ものの直後)に端末の実際のキャレットを配置するかどうか――
  // デフォルトfalse(通常の全ての一覧閲覧用途: FILE/DIR/MAN等の
  // ピッカー、そこではEnterは単にハイライトされた行を選択し、
  // ハイライトされた行自体がキャレットにとって正しい場所である)。
  // listBrowserView_は、^J^S/^J^C正規表現パターン候補一覧を表示中
  // に限りこれを有効にする(2026-08-12の修正、直接のユーザーに
  // よる訂正――正確な文言と独立したATOKアイコン位置の証拠について
  // はsyncVisibleCursor()自身のコメント参照): 「その」一覧は単なる
  // セレクタではなく、生きた編集可能なコンボボックスである――
  // タイプされた「どんな」バイトも、ハイライトされた候補自身の
  // テキストを編集することへ遷移し(feedRegexListByte()の「編集を
  // 開始する」フォールバック)、そのテキストは、編集が始まる前
  // でさえ、(内容行ではなく)「モードライン」に描画される
  // (ModeLineFields::trailingPlain自身のコメント参照)ので、
  // キャレットは、activatePrompt()が引き継いだ時点だけでなく、
  // まさに最初のフレームからそこに属する。
  void setCursorAtModeLineTrailingEnd(bool enabled);

  // カーソル/ホバー行が全く行全体の反転表示を受けるかどうか
  // (デフォルトtrue)。legacyのput_str()は、CP/RM/MV/ARC自身の
  // マーク選択の追跡(fmark()のf_Mスイッチ、移植済み: プレーンな
  // ディレクトリ一覧やファイル閲覧にはそのどれも当てはまらない)
  // に対してだけ行全体を反転させる――この移植は、このフラグが
  // 存在する前から既に、基本的な「自分はどこにいるか」の合図として
  // 至る所でカーソル行のハイライトを描いていた。実際のCUI/GUIの
  // 並べた比較により、これはDirectoryモードの実際の見た目には
  // 一致するが、Fileモードのものには一致しないことが確認された
  // (実際のスクリーンショット: プレーンなファイル内容にはそのような
  // ハイライトが全く無い)。FileModeは自身のContentViewでこれを
  // 無効にする。DirectoryModeはデフォルトのままにする。
  void setCursorHighlightEnabled(bool enabled);
  // 2026-10-02追加(Application::openPlainHelp()、KEYのhで飛んだ先の行): 反転表示の行を画面の右端まで
  // 反転する(文字の部分だけでなく「一行に渡る」ハイライト)。
  void setHighlightToRowEnd(bool enabled);
  bool cursorHighlightEnabled() const;

  // contentCursorColumn_自身のコメント参照(CopyModeの位置的な
  // キャレット、cursorHighlightEnabled()がfalseの間だけ意味を
  // 持つ)。
  void setContentCursorColumn(int column);
  int contentCursorColumn() const { return contentCursorColumn_; }

  // 他のモードラインの描画経路であればcursorIndex_/lines_.size()
  // から導出する位置フィールド("N/M"/"TOP"/"END")を上書きする
  // ――CopyModeのために2026-08-29に追加された。そのモードラインは、
  // 自身のカーソルが現在どこに座っていようと、legacy自身の固定
  // "---"プレースホルダー(disp.c自身の`case M_copy:
  // strcpy(line, "---")`)をそこに表示する。std::nullopt(デフォル
  // ト)は通常の計算されたテキストを復元する。他の全ての呼び出し
  // 元はこれを未設定のままにし、影響を受けない。
  // `plain`(2026-09-08追加、直接のユーザー要望"数字入力は、他の文字列
  // 入力エリア同様、ハイライト無しにして欲しい"): trueなら、この
  // 位置フィールドを他の全モードライン欄と同じStandout(反転表示)無し
  // で描画する――goto-line(^[N)の桁入力プレビュー専用。falseの間は
  // 従来通りStandout付きで描画される("COPY"/"Rectangle-Mark"/"---"
  // 等、他の全呼び出し元に挙動の変更は無い)。
  void setPositionTextOverride(std::optional<std::string> text, bool plain = false);

  // in-place GREP(2026-09-09〜11、design経緯は.steering/20260909-grep-
  // inline-highlight/参照)一式――当初はDirectoryMode/FileModeそれぞれに
  // 個別に実装されていたが、2026-09-11、直接のユーザー依頼("^Sと^[iを
  // FILE/DIR以外(現状GREPが稼働している全てのモード)へも反映して
  // ほしい")によりMan/Locate/Du/Archive/ArchiveContent/Shell/CutContent/
  // ListBrowser/KEYへも展開するにあたり、この場所へ一本化した――実際の
  // 実装(下記コメント参照)はどちらの旧実装も、行/カーソル/position
  // 表示というContentView自身の状態しか操作しておらず、Directory/File
  // 固有のものには一切依存していなかったため。
  //
  // grepInPlaceActive()/setGrepInPlaceOverlay(): 各モードのModeLineFields
  // Providerが、name="GREP"/trailing2="(grep)"へ一時的に差し替えるか
  // どうかを判定するためのフラグ(true/falseの記録だけ、描画自体はしない)。
  void setGrepInPlaceOverlay(bool active);
  bool grepInPlaceActive() const;
  // setGrepInPlaceLabel()/grepInPlaceLabel(): 2026-09-11追加、直接の
  // ユーザー依頼("FGREPおよびEGREPを同じ仕様に統一して欲しい")により
  // FGREP/EGREPもGREP(Basic)と同じin-place方式になったのに伴う追加。
  // 以前は各ModeLineFieldsProviderがgrepInPlaceActive()/
  // grepNoMatchActive()の間、name欄をリテラルな"GREP"へ無条件に
  // 差し替えていた(GrepSyntax::Basicしかin-place化されていなかった
  // 頃はそれで正しかった)――FGREP/EGREPも同じ経路を通るようになった今、
  // それらの検索については代わりに"FGREP"/"EGREP"を出す必要がある。
  // Application::activateGrepInPlace()/executeGrep()のNo-match分岐が、
  // grepSyntax_から実際のラベル(grepSyntaxLabel()自身の定義参照)を
  // ここへ渡す――ContentViewはGrepMode/GrepSyntaxより下位のレイヤー
  // なので、それらの型自体は持ち込まず、素の文字列だけを保持する。
  void setGrepInPlaceLabel(std::string label);
  const std::string &grepInPlaceLabel() const;
  // setGrepInPlaceBadge()/grepInPlaceProgramBadge(): 2026-09-12追加、
  // 直接のユーザー依頼("GREP/FGREP/EGREPの違いを明示するために...
  // (fgrep)/(egrep)表示のため")により、trailing2欄のリテラルな
  // "(grep)"も、grepInPlaceLabel()と全く同じ理由・同じ形で差し替え
  // 可能にする。実際の検索プログラム自体はgrep -F/-E/(無指定)の
  // ままで変えない(バイナリをfgrep/egrepへ切り替えると、ユーザーの
  // PATH次第で検索エンジン実装そのものが暗黙に変わってしまうリスクが
  // あるため、見送られた)――バッジ文字列だけをgrepSyntax_から導出する。
  // Application::activateGrepInPlace()/executeGrep()のNo-match分岐が、
  // grepSyntaxBadge()(application.cpp、grepSyntaxLabel()の並び)を
  // ここへ渡す。
  void setGrepInPlaceBadge(std::string badge);
  const std::string &grepInPlaceProgramBadge() const;
  // grepNoMatchActive()/reportGrepNoMatchMessage()/clearGrepNoMatchMessage():
  // 上と対になる、マッチ0件時のメッセージ表示フラグ。メッセージ本体は
  // setPositionTextOverride(message, plain=true)でposition枠へ直接
  // 渡される(旧DirectoryMode::reportGrepNoMatchMessage()自身のコメント
  // 参照)。
  void reportGrepNoMatchMessage(const std::string &message);
  void clearGrepNoMatchMessage();
  bool grepNoMatchActive() const;
  // 1件のマッチ: 元の行(sourceLineIndex)と、その中で実際に一致した
  // バイト範囲群(matchSpans、空なら行全体をハイライトするフォール
  // バック――GrepMatch::matchSpans自身のコメント参照)。GrepMatch
  // (grep_mode.hpp)そのものを使わないのは、ContentViewがGrepModeより
  // 下位のレイヤーであり、上位のものを型として持ち込みたくないため
  // ――呼び出し元(Application)がGrepMatchからこれへ変換する。
  struct GrepHighlightMatch {
    int sourceLineIndex = -1;
    std::vector<std::pair<int, int>> matchSpans;
  };
  // setGrepMatchHighlight()/clearGrepMatchHighlight(): 指定された行群に
  // Highlight属性を付け直す(呼ばれるたびに全行の属性を一旦クリアして
  // から組み直す――旧DirectoryMode::setGrepMatchHighlight()自身の
  // コメント参照、ESC iの毎キー入力での再呼び出しに対応するため)。
  // 2026-09-19追記、直接のユーザー報告(Application::GrepOccurrence
  // 自身のコメント参照、^N/^Pを出現単位まで細分化した際に発覚): 全て
  // の一致は一律Display::Attribute::Highlight(Emacsのlazy-highlight
  // 相当)になるが、`currentLineIndex`/`currentSpan`で指定された1件
  // (現在位置)だけはDisplay::Attribute::HighlightCurrent(isearch
  // 相当)になる――既定値(-1、{-1,-1})はどのmatchSpansとも一致し
  // 得ないので、呼び出し元が「現在位置」を持たない場合は従来通り
  // 全マッチが同じ扱いになる。
  void setGrepMatchHighlight(const std::vector<GrepHighlightMatch> &matches, int currentLineIndex = -1,
                              std::pair<int, int> currentSpan = {-1, -1});
  void clearGrepMatchHighlight();

  // 他の点では空の一覧に対してredraw()が表示するテキスト/整形
  // (redraw()自身のコメント参照)――デフォルトは「--」、本物の
  // エントリがそうであるように「完全に」反転表示/「マーク」される
  // (ディレクトリ/ファイル一覧の慣習: CUTはこの同じビューを自身の
  // セグメント一覧表示に再利用しており、そこでは「--」は既に
  // 「2つのマッチの間の空の領域」を意味するので、全体として空の
  // 一覧も一貫して「空の領域」として読める)。
  //
  // GrepModeは検索ごとにこれを上書きする(2026-08-12、2回の
  // ユーザーによる訂正)。最初: "EGREPはファイルの切り分けでは
  // ないので「--」ではなくて「No String」"(EGREPはファイルを
  // 領域に分割することについてのものではないので、空の結果は、
  // CUTの実際の領域分割の文脈の外では誤解を招く「--」ではなく
  // 「No String」と読めるべき)。その後、単なる「No String」だけ
  // でも本物の(選択可能な)候補と「同じ」完全な反転表示の扱いを
  // 再利用していることを見て: "ハイライトだと選択可能文字列と
  // 紛らわしいのでアンダーライン"(完全なハイライトは「これは
  // 選択可能な文字列である」と読めるが、そうではない――代わりに
  // 下線)――下の`marked=false`が、これを「この」行に特有に完全に
  // 無効にし、"さらに要望だがマッチしないと検索対象文字列情報が
  // 失われるので「abc: No String」かつ「No String」のみアンダー
  // ラインで"(どのパターンが検索されたかを失うことはそれ自体
  // 問題である――「No String」の部分「だけ」に下線を引いた
  // 「abc: No String」を表示する)――Line自身の`attributes`
  // (AttributeRun、既にHELP.jpのオーバーストライク太字/下線に
  // 使われている)がここで再利用され、その接尾辞だけに下線を引き、
  // "abc: "というパターンの接頭辞はプレーンなままにする。
  //
  // `marked`(2026-08-12、上の2番目の訂正): この行が、
  // `line.attributes`自身の下線/太字のrun(renderLine()が他の
  // 「あらゆる」Lineに対して既に行っている、同じ「standoutと
  // underlineは直交して共存できる」という組み合わせ)とは独立に、
  // renderLine()自身の反転表示の「ここにカーソルがある」扱いを
  // 受けるかどうか。true(デフォルト)はcutView_の既存の「--」の
  // 見た目を手を付けずに保つ。
  void setEmptyPlaceholder(Line line, bool marked = true);

  // scrollModeLineText()/scrollModeLineSegments()自身の"bottomVisible
  // >= total"判定(2026-09-07追加、直接のユーザー報告"最下行(モード
  // ラインのすぐ上)の値を表示する箇所は見当たらない？"を受け、
  // GUIの慣性スクロール/ドラッグで境界到達時に一瞬「END」が出る
  // バグの根本原因を再検討して特定)専用のフラグ――このビューは
  // lines_.size()を「今読み込めている行数」としてしか知らず、それが
  // ページャーの生きたストリームの「本当の末尾」なのか、単に「まだ
  // 先読みが追いついていないだけ」なのかを区別する手段を持たない。
  // それまでの修正は、後者(先読みの遅れ)を減らすことでこの一瞬の
  // 誤判定を「レースに勝って」隠そうとするものだったが、GUIの毎
  // フレーム描画とペーシング済みの先読み(呼び出し1回につき既読+1
  // 行までしか進まない、FileMode::ensureLoadedThrough()自身のコメ
  // ント参照)の相性が悪く、何度直しても再発していた――そもそも
  // 「まだ後ろに続きがあるかもしれない」間はEND判定自体を一切
  // 行わないようにすれば、タイミングに一切依存しない。デフォルト
  // false(=「これ以上増えない」、ストリーミングしない通常の一覧
  // 全て)。FileModeは生きたストリームを開いている間だけtrueを
  // 設定し、本物のEOFに達した瞬間(FileMode::readMoreIntoPipe()の
  // streamAtEof_=true代入と同じ場所)にfalseへ戻す。
  void setContentMayGrow(bool mayGrow);
  bool contentMayGrow() const;

  // height_未満の行が、デフォルトの「上端」に密着(下に空白の
  // パディング行)する代わりに、ビューポートの「下端」に密着
  // (その上に空白のパディング行)して描画されるかどうか――
  // デフォルトfalse、既存の呼び出し元は全て影響を受けない。
  // Shell自身のscrollback_のために追加(2026-08-04): 生きた
  // プロンプトが正確にrow0_+height_(このビュー自身の内容領域の
  // すぐ後の行)に固定されているのは、最後の本物の内容行が「常に」
  // そのすぐ上にある場合にだけ正しく見える――以前の上端固定の
  // デフォルトでは、それはscrollback_がheight_を完全に満たすほど
  // 成長した時点でだけ真である。それより前の時点では、プロンプト
  // は、少量の内容のはるか下の固定行に座る(不自然な隙間)か、
  // 内容自身の成長する端に追従する(Shell自身の「以前の」修正、
  // それは独自の再同期バグを引き起こした――より詳しい経緯に
  // ついてはShellのクラスコメント参照)しかなかった。代わりに
  // 下端固定は、「内容のすぐ後の行はどれか」という問いへの答えが、
  // 存在する内容がどれだけ少なくても「同じ」固定行になることを
  // 意味し、見つかるたびに1つずつ修正するのではなく、プロンプト/
  // 内容の再同期バグの種類全体を根源で排除する。
  void setBottomAnchored(bool enabled);

  // -- スクロール --
  // 1行単位のカーソル移動(M_up/M_dn/M_kup/M_kdn): カーソルを
  // 1行動かし、カーソルがそうしなければビューポートから出て
  // しまう場合にだけ、ビューポートを1行スクロールする
  // (smoothScrollEnabled()の場合はDisplayのハードウェアスクロール
  // を使い、そうでなければ完全な再描画)。
  void moveUp();
  void moveDown();

  // 「ウィンドウを1行上下へ動かす」(usilicrc自身のK-8/K-9の説明、
  // デフォルトで'j'/'k'に紐付く)――moveUp()/moveDown()とは意図的
  // に「異なる」: それらはカーソルを1行動かし、カーソルがそう
  // しなければビューポートから出てしまう場合にだけビューポートを
  // スクロールする(usilicrcはそのペア、^N/^Pに紐付く、を「カーソル
  // を1行動かす」と呼んでいる)。このペアは常にtopIndex_をちょうど
  // 1行スクロールし(両端で制限される、moveUp()/moveDown()と同じ
  // 境界ベルの慣習)、cursorIndex_を新しく明かされた端の行へ動かす。
  // `less`の1行スクロールと同じ。この移植の以前のバージョンは
  // 代わりに'j'/'k'を単なるカーソル移動へエイリアスしていた
  // (Application::resolveCommand()自身のコメントにその訂正の完全な
  // 経緯がある)――usilicrcに対して直接確認済みで、scroll.cの
  // move_row()自体には無い(C言語のソースはどこも'j'/'k'をリテラル
  // なキーとして紐付けていない。usilicrcの設定可能なキーテーブル
  // だけがそうする)。
  void scrollLineDown();
  void scrollLineUp();

  // 全ページ単位の移動(M_back/M_forw): move_page()からの移植で、
  // 常に完全な再描画。legacyのスムーズスクロール経路は、ハード
  // ウェアスクロールが利用可能な時、ページ移動も1行ずつアニメー
  // ションしていた。ここでは、いずれにせよ単一の再描画に単純化
  // されている――範囲についてはクラスコメント参照。
  void pageUp();
  void pageDown();

  // 最初/最後の行へジャンプする(M_head/M_tail)。
  void goToTop();
  void goToBottom();

  // -- 描画 --
  void redraw();
  // 2026-10-20、直接のユーザー提案(最初の起動のbrew installの間、SHELLの上にHELPの表紙を残す): redraw()の最後
  // (flushの前)に呼ぶもの。setViewport()で空けた行に、別のものを描く(Application::startShellBanner()参照)。
  void setAfterRedraw(std::function<void()> hook) { afterRedraw_ = std::move(hook); }
  // redraw()のうち本文の行だけを描く(モードライン・flush()無し)。2026-09-30追加:
  // Application::captureEpubVerticalEdges()が、画面の外の列を裏に描くのに使う。
  // 戻り値は1行でも描いたか。
  bool renderContentRows();
  // 2026-09-30追加、直接のユーザー報告("1画面に満たない奥付もなかなかのカオス"=ht): 行の添字の
  // 範囲[first, second)の外の行は空白として描き、範囲の頭を画面の頭に置けるよう、画面を送れる
  // 上限(maxTop)を範囲の頭まで広げる。EPUBの縦書きの本の横書きの範囲(奥付等、
  // Application::syncEpubHorizontalPresentation())だけが使う。
  void setShownIndexRange(std::optional<std::pair<int, int>> range) { shownIndexRange_ = range; }
  // 2026-09-30追加、直接のユーザー報告(=hv、src/K3t.epubの縦書きの閲覧中に^X^Dの一覧が縦書きの
  // 描き方で崩れた): -gの縦書き用の描き方(Display::setVerticalTextHint())は、ビューが自分を描く
  // たび(redraw())に自分のこの値で切り替える。縦書きの本文・章の一覧のビューだけがtrue。以前は
  // Application::activeView()が「今のモード」から決めていたので、縦書きのままの状態で他の一覧・
  // プロンプトの画面を描くと縦書きの描き方が残っていた。
  void setVerticalText(bool vertical) { verticalText_ = vertical; }
  // 2026-09-30追加(Application::selectAdjacentEpubHorizontalLink()): 行indexの[start, end)バイトを
  // 反転表示する(横書きのEPUBの選んでいるリンク)。nulloptで消す。
  struct ByteSpan {
    int index = 0;
    int start = 0;
    int end = 0;
  };
  void setExtraStandout(std::optional<ByteSpan> span) { extraStandout_ = span; }
  const std::optional<ByteSpan> &extraStandout() const { return extraStandout_; }
  int maxTopIndex() const;
  bool isIndexShown(int index) const {
    if (shownIndexRange_ && (index < shownIndexRange_->first || index >= shownIndexRange_->second)) return false;
    // 2026-10-27、直接のユーザー報告("「スクロール：章毎」に切り替えた後、目次→付録／付録→目次で空白行が大量に出る"):
    // 縦書きの本の横書きの範囲へ下から入ると、画面の上端はまだ範囲の前の行(描かない行)にあり、範囲の章は頭が上端に
    // 来るまで描かれなかった。見せる範囲の前の行は描かないので、上端の章は見せる範囲の最初の行から数える。
    int top = shownIndexRange_ ? std::max(topIndex_, shownIndexRange_->first) : topIndex_;
    return chapterStarts_.empty() || chapterOf(index) == chapterOf(top);
  }
  // 2026-10-03追加、直接のユーザー依頼(EPUBの「連続スクロール/章区切り」、横書きの章区切り): 章の頭の
  // 行の添字(昇順)。空でなければ、画面の上端の行の章だけを描く(次の章は頭が上端に来るまで描かない、
  // 縦書きのApplication::epubVerticalShownRange()と同じ)。Application::syncEpubHorizontalPresentation()が渡す。
  void setChapterStarts(std::vector<int> starts) { chapterStarts_ = std::move(starts); }
  int chapterOf(int index) const {
    return static_cast<int>(std::upper_bound(chapterStarts_.begin(), chapterStarts_.end(), index) - chapterStarts_.begin());
  }

  // このビューが現在、そのモードライン行に表示している、整形済み
  // の「current/total NN%」テキスト(renderModeLine()参照)――
  // redraw()を迂回するカスタムの描画ループ(GraphicalDisplayの
  // ドラッグスクロール経路、自身のrenderScrolled()オーバーロード
  // 参照)が、ModeLineの位置計算を重複させることなく同じテキスト
  // を自力で描画できるように公開されている。パーセントはカーソル
  // 位置に基づく(ModeLine::format()自身のコメント参照): ビュー
  // ポートが実際にどこへスクロールされていようと、まさに最初は
  // 0近く/「TOP」で、まさに最後で「END」まで上がっていく。
  std::string modeLineText() const;

  // 2026-09-08追加、直接のユーザー要望("「現在行/総行 ??%」エリア幅の
  // まま非ハイライト入力エリアとして使用して欲しい"): modeLineText()と
  // 違い、positionTextOverride_を一切見ない――setPositionTextOverride()
  // で上書きする「前」に、元々そこにあったはずの("N/M XX%"等の)幅を
  // 問い合わせるために使う(goto-line入力欄をその幅に合わせてパディング
  // するため、Application::dispatchLeaderByte()自身のgotoLine*コメント
  // 参照)。
  std::string naturalPositionText() const;

  // 同じフォーマットだが、特にドラッグ/惰性スクロールの描画経路
  // 向け: cursorIndex_の代わりにtopIndex_(ビューポート自身の最上部
  // の行)を使う。マウスドラッグ/Shift+矢印の惰性は、意図的に
  // cursorIndex_を決して動かさない(ビューポートをスクロールする
  // ことは選択を動かすことと同じではない――moveUp()/moveDown()対
  // setTopIndex()自身のコメント参照)ので、惰性中はmodeLineText()の
  // cursorIndex_ベースの数字が、ビューが目に見えてスクロールして
  // いる間凍結したまま座ってしまう――代わりにtopIndex_を使うこと
  // で、スクロール中に実際に画面上にあるものと歩調を合わせてモード
  // ラインが動き続けるが、その代償として、積極的にスクロールして
  // いる間は「選択された行」を意味しなくなる(スクロールが落ち着
  // き、modeLineText()が再び引き継いだ瞬間、その意味に戻る)。
  // 「下端」の境界(ビューポート自身の最下行が一覧の最後の行を
  // 表示する)に到達すると、modeLineText()と同様にENDを報告する
  // ――topIndex_自体からは別にチェックされる。topIndex_は
  // total-height_で頭打ちになるので、cursorIndex_のようには
  // totalLinesに決して到達できないため(実際の報告: 一番下まで
  // 惰性で行くと、プレーンな'j'/'s'ナビゲーションが既にENDに到達
  // していたのに対し、パーセンテージのまま止まってしまっていた)。
  std::string scrollModeLineText() const;

  // scrollModeLineText()と同じ組み立て(topIndex_ベースの現在行、
  // 同じname/position/trailingフィールド)だが、1つの平坦な文字列
  // ではなくDisplay::ModeLineSegmentの列として――Display::
  // renderScrolled()の呼び出し元向け。それらは、renderModeLine()
  // 自身の「フィールドの間にプレーンなギャップを挟んだ、別々の
  // 反転表示run」という見た目を再現する必要がある(これが修正する
  // 実際のスクリーンショットのバグについてはModeLineSegment自身の
  // コメント参照: renderScrolled()のモードラインは以前、
  // scrollModeLineText()の平坦な文字列を、ギャップの全く無い1つの
  // 途切れないハイライトバーとして描いていた)。renderModeLine()
  // 自身の、右揃えの太字の透かしフォールバック(Directoryモード、
  // 空のtrailing)は再現しない――そのフォールバックは、
  // renderScrolled()自体が使われるどのモードにも決して当てはまら
  // ない(ファイル内容のスクロールは常に空でないtrailingパスを
  // 持つ)ので、未検証のまま移植するのではなく意図的に省かれた。
  //
  // `trackCursorPosition`(2026-09-07追加、直接のユーザー報告:
  // "（ls）のようにハイライト表示画面では最下行のハイライト位置が
  // 更新されながらスクロールしていくんだが、同じ動作でもある`j`/`k`
  // ではハイライト位置がちゃんと現在行の値に反映されている...ところが
  // GUIの慣性スクロールでは...「00001」から始まる"): デフォルトfalse
  // (上のtopIndex_基準、既存の全呼び出し元/テストの契約を保つ)。
  // このメソッド自身の唯一の実呼び出し元であるGUI(main.cppの
  // runGuiPhase()、gui.renderScrolled()経由)はtrueを渡す――GUIの
  // コースト/ドラッグは(scrollbarドラッグのjumpToScrollbarY()、
  // 通常のcommitRowCrossings()/stepTopIndex()のどちらの経路でも)
  // 毎回cursorIndex_を明示的に追従させ続けている(topIndex_のように
  // 「デタッチされたまま」ではない)ため、cursorIndex_基準で表示
  // した方がキーボードj/kの表示(modeLineText()、同じcursorIndex_
  // 基準)と一致し、かつ正確――スクロール開始時にtopIndex_がまだ
  // 動いていなくても(=00001)、選択中の行(例: 00024)を正しく
  // 反映できる。
  std::vector<Display::ModeLineSegment> scrollModeLineSegments(bool trackCursorPosition = false) const;

  // legacyのdisp_window()がcurrent/total位置テキストの周りに包む、
  // 2つのモード固有の断片(自身のフィールドレイアウトからの移植、
  // 下のsetModeLineFieldsProvider()のコメント参照):
  //   name     -- mode-line[1]、位置の前に描画される(例えば
  //               Directoryモードでのディレクトリの完全なパス、
  //               またはFileモードでのファイルの裸の名前――
  //               legacyがどちらを表示するかはContentViewではなく
  //               モードに依存する)。
  //   trailing -- mode-line[6](legacy自身のコメントでは
  //               「前モード」)、位置の後に描画される(例えば
  //               Directoryモードの「(ls)」、またはFileモードの
  //               「<size> <directory>/」)。空はそこに何も描画
  //               されないことを意味する。
  //   fillTrailingToEdge -- legacyは特に「この」フィールドを、
  //               端末自身の右端まで空白でパディングする
  //               (disp_window()自身の`for (...) prev[i] = ' '`
  //               ループ参照)ので、その反転表示runは視覚的に
  //               ギャップ無く端に到達する――しかしそれは、
  //               trailingテキストをその特定のフィールドに通す
  //               モードに対してだけ(Fileモードのサイズ+
  //               ディレクトリはそうする。Directoryモードの
  //               「(ls)」は、legacyが「パディングしない」短い、
  //               別のフィールドで、その後にプレーンなギャップを
  //               残す――C言語のソースの検査だけではなく、両方の
  //               モードの実際のCUIスクリーンショットを比較する
  //               ことで確認済み。ソースはそれらを、各mode.cの
  //               呼び出し箇所がたまたま2つの異なる
  //               disp_window()の引数(`arg`対`prev0`)のどちらを
  //               埋めるかによってのみ区別しているため)。trailing
  //               が空の場合は無視される。
  //   rightWatermark -- mode-line[6]に「前モード」テキストが全く
  //               無い(C言語のソースでprev0がK_z/K_y番兵である)
  //               場合のdisp_window()自身のフォールバック: プロ
  //               グラム名とバージョンが、画面自身の右端に(反転
  //               表示ではなく)「太字」で右揃えされる、例えば
  //               「usilic v1.5b」――特にディレクトリ一覧画面の
  //               実際のCUIスクリーンショットに対して確認済み
  //               (実際の報告: この移植の以前の試みはそこに何も
  //               表示していなかった。「(ls)」フィールド――完全に
  //               別物で、別のdisp_window()の引数から埋められる
  //               ――がこのスロットもカバーしていると誤解して
  //               いた)。Fileモード自身のtrailingフィールド
  //               (fillTrailingToEdge)は既に行を満たし余地を
  //               残さない。legacyに合わせている: そこではprev0
  //               が空でないので、そのモードでもC言語のソースで
  //               フォールバック分岐は決して実行されない――
  //               DirectoryModeが、現在これを設定する唯一の
  //               呼び出し元である。既に描画されているものと
  //               並んで収まる場合にだけ描かれる(name/trailing
  //               と同じオーバーフローポリシーで、切り詰められる
  //               のではなく落とされる)。
  // `trailing`/`trailing2`のための意味的な種類。古い
  // trailing2MiddleEllipsis/trailing2FullWidthTailのbool対を
  // 置き換え「かつ」`trailing`自身に切り詰めのオプションを追加する
  // (以前は「全く」無かった――なぜそれが単なる見た目の整理ではなく
  // 実際のギャップだったのかについては下の2026-08-17の修正参照)。
  // 実際のユーザーからの質問("黒箱にはなってないのか")が、5個の
  // 独立したboolという設計は、新しいプロバイダが正しい組み合わせを
  // 忘れやすくすることを指摘した。これは、`trailing`用に2つ目の
  // bool対を複製する代わりに、2つの切り詰め「スタイル」boolを、
  // 両フィールドで共有される1つのenumへ折りたたむ。
  //   Tag        -- 短い、固定のテキスト(「(fgrep)」のような静的
  //                 なラベル、バイト数、ファイル名): このクラスに
  //                 よって決して切り詰められない――このenumが存在
  //                 する前に全てのプロバイダの`trailing`が振る舞っ
  //                 ていたのとまさに同じように、fitModeLineFields()
  //                 自身の階層によって完全に表示されるか完全に
  //                 落とされる。`trailingKind`のデフォルトなので、
  //                 既存の全プロバイダは、選択しない限り影響を
  //                 受けない。
  //   QueryEcho  -- タイプされた検索パターン/クエリ。それ自身の
  //                 プレビュー領域は、端末サイズに関わらず慣習
  //                 として固定幅である(直接のユーザーポリシーで
  //                 確認済み: "戻り値が長くて「...16文字」になる
  //                 のは、文字列入力の場合のみ"): 末尾のみの
  //                 「...」+最大16文字。`trailing2Kind`のデフォ
  //                 ルトで、既存の全プロバイダの選択していない
  //                 trailing2の挙動に合わせている。
  //   Path       -- 自然な固定幅を持たない、完全なファイルシステム
  //                 パス(または同様の長いテキスト): 実際に利用
  //                 可能などんな予算でも使う末尾のみの「...」、
  //                 16文字の上限は無い(FileMode自身の含む
  //                 ディレクトリのtrailing2、ArchiveContent自身の
  //                 アーカイブパスのtrailing2)。
  //   PathMiddle -- 「頭部」も末尾だけでなく実際の情報を運ぶ、
  //                 完全な絶対パス(LOCATE経由で到達した時の
  //                 FileMode自身のtrailing2。2026-08-17の追加:
  //                 listBrowserView_自身のFILE/DIR/COMMANDピッカー
  //                 の`trailing`も。自身のプロバイダのコメント
  //                 参照): Finderスタイルの中間省略記号
  //                 (head...tail、LOCATE自身の結果一覧の行が既に
  //                 使っているのと同じ慣習、middleEllipsisTruncate()
  //                 参照)。
  enum class TrailingKind { Tag, QueryEcho, Path, PathMiddle };

  struct ModeLineFields {
    std::string name;
    std::string trailing;
    bool fillTrailingToEdge = false;
    std::string rightWatermark;
    // `trailing`の後に、その間に独自のプレーンな(非standoutな)
    // 1文字幅のギャップを挟んで描画される、2番目の独立してハイライ
    // トされたtrailingセグメント――FileMode自身のバイトサイズ/
    // ディレクトリの分割のために2026-08-10に追加(実際の報告、
    // スクリーンショット: バイトサイズとディレクトリが、他の全ての
    // モードラインフィールドのペアとは異なり、ギャップの無い1つの
    // 途切れないハイライトrunだった――「1108009  /Users/.../」では
    // なく、代わりに2つの別々の箱の「CLAUDE.md [ギャップ]
    // 00034/13761  0%」のように見えるべき)。他の全てのモードの
    // プロバイダに対しては空(デフォルト)であり、そこでの挙動の
    // 変更は無い。これと`fillTrailingToEdge`の両方が設定されている
    // 場合、端へのパディングは`trailing`ではなく「この」フィールド
    // (最後に描画されるもの)に適用される――renderModeLine()/
    // scrollModeLineSegments()自身のコメント参照。
    std::string trailing2;
    // 通常のハイライトされたrunの代わりに、`trailing`をプレーンな
    // (非反転表示の)テキストとして描画する――^J^S/^J^C候補一覧
    // 自身の「ライブプレビュー」用途のために2026-08-11に追加
    // (listBrowserView_自身のプロバイダ参照): 実際の報告+参照
    // スクリーンショットが、現在ハイライトされている候補のパターン
    // テキスト(例えば「^[0-9]*¥.」)がモードラインに反転表示で
    // 描かれていることを示した。しかしその位置は「編集可能な
    // テキストエリア」(入力を始めたら何が入るか)として読める
    // ことを意図しており、この移植の他の全ての編集可能なフィールド
    // (promptEditor_自身のLineEditor)は、ハイライトではなくプレー
    // ンに描画する――それをハイライトすると、代わりに静的な
    // ラベルのように見えてしまっていた。他の全てのプロバイダは
    // これをデフォルト(false、既存の常にハイライトされる挙動)の
    // ままにする。この6番目のフィールドを明示的に列挙しない呼び
    // 出し元向けの、集成体初期化のデフォルトメンバ初期化子による
    // フォールバック経由で。
    bool trailingPlain = false;
    // `trailing2`を、`trailing`自身の1文字幅のギャップの直後に
    // 描画する代わりに、画面自身の右端へ右揃えする――^J^S/^J^C
    // 候補一覧自身の移動先ディレクトリフィールドのために2026-08-12
    // に追加(listBrowserView_自身のプロバイダ参照): 実際の報告+
    // 参照スクリーンショット(=ac)が、切り詰められた「...<末尾>」
    // ディレクトリ(drawPromptLabel()自身の対応するフィールドは、
    // 明示的なmoveCursor()経由で「右揃えされて『いる』」)が、端
    // では「なく」、`trailing`(候補プレビューテキスト)がたまたま
    // 終わった場所に依然として着地していることを発見した――
    // "戻り値自体はいいけど位置が右端じゃない"(値自体は良いが、
    // 位置が右端ではない)。他の全てのプロバイダは、上の
    // trailingPlainが既に依存しているのと同じ集成体初期化の
    // フォールバック経由で、これをデフォルト(false、既存の
    // trailingの直後という挙動)のままにする。
    bool trailing2RightJustified = false;
    // withTruncatedFields()が溢れたtrailing2を「どのように」省略
    // するかを選択する: false(デフォルト)は末尾のみを切り詰める、
    // 「...」+最後の最大16文字――他所の移動先ディレクトリフィール
    // ド(drawPromptLabel()、listBrowserView_自身の直接エントリ
    // 分岐)に対してtruncatedTrailingPath()が既に確立した「同じ」
    // 慣習で、直接のユーザーによるポリシー声明("なおLOCATE以外
    // では、右下端の戻り値が長すぎる場合「...16文字」でよろしく")
    // に基づく。trueは例外である: LOCATE経由で到達した時のFileMode
    // 自身のtrailing2(FileMode::setSearchOrigin()自身のコメント
    // 参照)は、代わりにFinderスタイルの中間省略記号(head...tail)
    // を表示する。末尾しか重要ではなかった短い検索パターン/クエリ
    // とは異なり、完全な絶対パスの「頭部」も実際の情報を運ぶため。
    //
    // 2026-08-17のリファクタリング: このフィールドと、以前これに
    // 続いていた古いtrailing2FullWidthTail boolが、下の新しい
    // `trailingKind`フィールドと共有される1つの`trailing2Kind`
    // (TrailingKind、上の自身のコメント参照)に折りたたまれた
    // ――直接のユーザーからの質問("黒箱にはなってないのか")と、
    // 実際の再現(=bw: listBrowserView_自身のFILEピッカーの
    // trailing、長い絶対パスが、切り詰められるのではなく「完全に」
    // 消えていた。これより前は`trailing`には切り詰めのオプションが
    // 「全く」無かったため)が、これを「単に2つのtrailing2専用bool
    // をマージする」ことから「trailing2自身の切り詰めスタイルを
    // `trailing`にも与える」ことへ広げることを促した。フィールド
    // の位置は安定に保たれている(trailing2Kindは、
    // trailing2MiddleEllipsisが使っていたのと「同じ」8番目のスロッ
    // トを占める)ので、フィールド7(trailing2RightJustified)まで
    // しか指定しない既存の位置指定の集成体初期化の呼び出し箇所は、
    // この変更で手を付けられない。
    TrailingKind trailing2Kind = TrailingKind::QueryEcho;
    // `trailing`自身に、このフィールドが存在する前に全てのプロバイ
    // ダが持っていた歴史的な「完全に表示されるか完全に落とされる
    // か」の挙動の代わりに、trailing2Kindがtrailing2に与えるのと
    // 「同じ」切り詰め処理を選択させる(Tag、デフォルト、はその
    // 歴史的な挙動を正確に保つ)。TrailingKind自身のコメント参照。
    // PathMiddleはlistBrowserView_自身のFILE/DIR/COMMANDピッカー
    // の修正である(=bw)。
    TrailingKind trailingKind = TrailingKind::Tag;
    // 通常の反転表示(Standout)の箱の代わりに、`name`を下線付き
    // (Display::Attribute::Underline)で描画する――DirectoryMode
    // 自身の一時的な「Invalid mode」エラー(legacyのdo_edit()の
    // S_errvメッセージ、mode.cからの移植: ファイルではなくディレ
    // クトリエントリに対して^Je/EDITを押す)のために追加された。
    // これは、直接のユーザー依頼が特に"アンダーラインエラーメッ
    // セージ（適切なやつ）をモードラインのモード名位置に"表示する
    // よう求めたもの――つまり、`name`自体を(別のフィールドでは
    // なく)下線付きのメッセージで置き換える。この移植がGrepMode
    // 自身の「No String」プレースホルダー(自身のコメント参照)
    // に対して既に確立した「ステータスメッセージには、選択可能な
    // /ハイライトされた文字列として読めないよう、反転表示ではなく
    // 下線を使う」という慣習に合わせている。他の全てのプロバイダは
    // これをデフォルト(false)のままにし、影響を受けない。現在は
    // renderModeLine()(プレーンな、ドラッグスクロールしない再描画
    // 経路。このようなキー入力の結果は常にこれを使う)だけがこれを
    // 尊重する――scrollModeLineSegments()/composeModeLineRow()は
    // まだこの扱いを持たない(より狭いギャップで、重要になるとは
    // 予想されない: ドラッグスクロールジェスチャーの途中にエラー
    // メッセージが現れることは実際のシナリオではない)。
    bool nameUnderlined = false;
    // trailing2の後に、その間に独自のプレーンな1文字幅のギャップを
    // 挟んで描画される、3番目の独立してハイライトされたtrailing
    // セグメント――ArchiveMode自身の一覧表示のモードラインのために
    // 2026-08-19に追加(「戻り値」報告=dh:
    // "「(tar)」と「/Users/.../utf8.tar」の間に空白が無い"）。
    // trailing2は「既に」"(tar)"バッジとパス(`badge +
    // currentArchive_`)の間に本物のASCII空白を含んでいたが、
    // renderModeLine()は1つのフィールドを1つの途切れないStandout
    // runとして描画する(理由については自身の長いコメント参照:
    // CUIのモードラインは、フィールドの「1つの中」ではなく
    // 「間」に見える黒いギャップを必要とする)ので、その埋め込ま
    // れた空白は反転表示の空白として描画される――隣接するものと
    // 同じ暗い背景なので、バイトはそこにあるにも関わらず、2つの
    // 断片は「くっついている」ように見える。他の全てのフィールド
    // のペア(name/position、position/trailing、trailing/trailing2)
    // は、renderModeLine()が1つのStandout runを終えてプレーン
    // モードの空白の周りで新しいものを始めるので、本物のギャップ
    // を得る。trailing3は、バッジ+パスのペアに、同色の埋め込まれ
    // た文字ではなくその同じ本物の箱の区切りを与える。他の全ての
    // モードのプロバイダに対しては空(デフォルト)で、そこでの
    // 挙動の変更は無い。(trailing/trailing2とは異なり)
    // fitModeLineFields()の落とす優先順位の階層には統合されて
    // いない――意図的に単純に保たれており、切り詰められるのでは
    // なく、収まらなければそのまま落とされる。rightWatermark自身
    // の寛容な収まりチェックと同じ。ArchiveMode自身のここでの
    // パスは、実際の端末幅の大部分で収まると予想され、病的に狭い
    // ものでそれを完全に失うことは、最初の一切れとしては受け入れ
    // 可能な劣化である。現在はrenderModeLine()とcomposeModeLineRow()
    // だけがこれを尊重する――scrollModeLineSegments()
    // (GraphicalDisplay自身の経路)はまだこれを持たない。すぐ上の
    // 既存のnameUnderlinedの前例と同じ理由に合わせている(より
    // 狭いギャップで、まだ取り組むべきGUIの報告が無い)。
    std::string trailing3;
    // このモードラインを、通常の1行レイアウトが収まらない時「だけ」
    // 使われる、fitModeLineFields()の通常の「trailing、次にtrailing2
    // を落とす」階層の代わりに、全く異なるフォールバックレイアウト
    // (renderSplitModeLine()、自身のコメント参照)へ選択させる――
    // アーカイブcontent view自身のモードラインのために2026-08-19に
    // 追加(報告=dg/=dhの追加依頼: 実際の提案、"モードラインの長
    // 過ぎモード名および戻り値対処としては...『「1/1 END」「111」」
    // をまずは中央へ→残りのエリア半分ずつを「モード名」「戻り値」
    // で分け合った上でそれぞれ..."）。デフォルトの階層は、アーカ
    // イブ内の長いCJKファイル名に対してname+trailing2が一緒になっ
    // て長すぎる時、`trailing`(バイト数)を黙って落としていた
    // ――これは代わりに、position+trailingを中央に置き、name/
    // trailing2にそれぞれ残りの幅の半分を与え、収まるようFinder
    // スタイルの中間省略記号で切り詰めることで、4つの情報全てを
    // 画面上に保つ。他の全てのプロバイダに対しては空/false
    // (デフォルト)で、影響を受けない。意図的に、fitModeLineFields()
    // 自体に組み込むのではなく選択制にしてある。これは、全モード
    // 自身の混雑時の挙動を一度に変えるのではなく、これが依頼された
    // 1つの画面に狭くスコープを保つため。
    bool splitLayoutWhenCrowded = false;
    // `trailing2`を、通常のハイライトされたrunの代わりに、プレーン
    // な(非反転表示の)テキストとして描画する――trailingPlain自身の
    // コメント参照、同じ発想の`trailing2`版。2026-09-10追加、直接の
    // ユーザー依頼("同じ位置に、非ハイライトの「〜:No String」を
    // 表示して欲しい"): in-place GREPのマッチ無しメッセージ
    // (DirectoryMode::reportGrepNoMatchMessage()/FileMode::
    // reportGrepNoMatchMessage())は、"GREP"という短いラベル(name、
    // Standoutのまま)の後に続く実際の情報であり、選択可能な項目や
    // バッジのようには見えるべきでない。他の全てのプロバイダは
    // これをデフォルト(false、既存の常にハイライトされる挙動)の
    // ままにする。既存の位置指定の集成体初期化の呼び出し箇所を
    // 一切崩さないよう、末尾に追加してある。
    bool trailing2Plain = false;
    // renderModeLine()がtrailing2を描く直前に無条件で挿入する1文字の
    // 区切りスペース(trailing/positionTextが非空の他の全プロバイダに
    // とっての、それらとの間の唯一の区切りであり、既存のあらゆる
    // モードラインが依存している共通部品)を、この呼び出しだけ省略
    // する。2026-09-10追加、直接のユーザー指摘(実機スクリーンショット
    // での比較=gd/=gc、ライブ編集中と確定後のNo Stringメッセージとで
    // 文字列の列位置が食い違う件のさらなる追及: 「これは致命的な
    // バグと同じ。見える場所だから」)――当初、この汎用のスペース
    // 自体を全モードから削除しようとしたところ、ArchiveModeの
    // "1765(tar)"のような既存の表示が壊れることが確認された
    // (doctest実験で確認済み)ため、代わりにこのopt-inフラグとして
    // 切り出した。DirectoryMode/FileModeのGREP no-matchメッセージ
    // 分岐だけがこれをtrueにする――そちらは既にpositionText/trailing
    // 自体を空にしている(setPositionTextOverride("")/
    // fields.trailing.clear())ので、name自身の末尾のスペース1つだけが
    // 残り、ESC iのライブ編集中の表示("GREP"+1スペース+パターン
    // 文字列)と列位置が完全に一致する。他の全プロバイダはデフォルト
    // (false、既存の常にスペースを挿入する挙動)のままで、影響を
    // 受けない。既存の位置指定の集成体初期化の呼び出し箇所を一切
    // 崩さないよう、末尾に追加してある。
    bool trailing2NoLeadingSpace = false;
  };

  // このビューのModeLineFieldsを供給する。モードラインが描画され
  // ようとするたびに再計算される(そのためこれは、カーソルが動く
  // か新しいファイルが読み込まれるたびにContentViewに再度伝える
  // 必要無く、常に現在選択/読み込まれている行を反映する)。
  // ContentView自体は、行の「完全な名前」やモード自身のアイデン
  // ティティという概念を持たない(Lineは整形された表示行全体、
  // 例えばls -lの完全な列レイアウトしか運ばない)――それはモード
  // 固有の知識(DirectoryModeのcurrentDirectory()、FileModeの
  // currentFile()等)なので、これは単なる文字列ではなくコールバック
  // である。デフォルト構築された(空の)プロバイダは、追加の
  // フィールドが全く無いことを意味する――単なる裸の位置テキストだ
  // け(既存の全モードのモードラインは、それ自身のModeクラスが
  // 選択するまで、これが追加される前から変わらない)。
  using ModeLineFieldsProvider = std::function<ModeLineFields(int cursorIndex)>;
  void setModeLineFieldsProvider(ModeLineFieldsProvider provider);
  const ModeLineFieldsProvider &modeLineFieldsProvider() const { return modeLineFieldsProvider_; }
  // 2026-10-02追加、直接のユーザー依頼("^X(から^X)までの間、モード名をMACROで乗っ取り"): 空でなければ、
  // 全てのビューのモードラインの名前欄をこれにする(Application::startMacroRecording()が立て、
  // stopMacroRecording()が下ろす)。プロンプトのラベル(drawPromptLabel())は対象外。
  static std::string &modeNameOverride();
  // 右下の透かし(戻り先の無い画面のrightWatermark)の文字列、既定は「usilic v1.5b」。2026-10-10、直接のユーザー
  // 依頼(2026-10-09の反転表示は"印象が薄いので紛らわしさは拭えない"、"非ハイライトの「PID:USILIC」"): 親のusilicが
  // 外部起動した子(main.cppのUSILIC_TRANSIENT)では「親のPID:USILIC」にする(太字のまま)。
  static std::string &programWatermark();
 private:
  ModeLineFields providedModeLineFields() const;
 public:

  // 現在のプロバイダを(このビュー自身のcursorIndex_で)今、一度
  // だけ評価する――このクラスの他の全ての呼び出し元のように将来の
  // 全ての描画に対してライブに反応するのではなく、そこから切り
  // 替わる前にこのビューのモードラインが現在表示しているものの
  // 「スナップショット」を凍結する必要がある呼び出し元
  // (Application::startCopy())のため。プロバイダが設定されて
  // いなければデフォルト構築されたModeLineFields(空のname/
  // trailing)。
  ModeLineFields currentModeLineFields() const;

  // このビューの位置フィールドを、他の全てのモードが使う
  // デフォルトのModeLine::format()(現在はTOTAL自身の桁数にだけ
  // パディングされる、例えば「03/15」)の代わりに、
  // ModeLine::formatFixedWidth()(固定幅、5桁ゼロパディングの
  // 「NNNNN/NNNNN END」カウンター、print_line()自身のlen<=0分岐、
  // disp.cからの移植――legacyではSHELL特有: disp_shell()が負の
  // lenを渡す唯一の呼び出し元であるが、legacy自身のC言語のソース
  // は実際には5ではなく6桁にゼロパディングする――
  // formatFixedWidth()自身のコメント参照)へ選択させる。
  // false(デフォルト)は既存の全モードを影響を受けないままにする。
  // 2026-08-24の追加、このまさに同じlegacyの挙動と比較した直接の
  // ユーザー依頼。
  void setUseFixedWidthPosition(bool enabled);

  // 2026-08-26の追加(直接のユーザー依頼、同じ会話で2回洗練され
  // た): 生きたプロンプト行は、実際のscrollback_の内容が何であれ
  // その上に、常に目に見える形で画面上の1行を占めている――そのため
  // totalは常に、単なるlines_.size()ではなくlines_.size()+1で
  // あるべきで、`current`は、何も積極的に閲覧されていない時は
  // その同じtotal(生きたプロンプト自身の末尾の位置)として読める
  // べきである。この修正の「以前の」、より狭いバージョンは、本当に
  // 「空の」scrollback_だけを特別扱いしていた(最初のコマンドの前
  // は「00000/00000」→「00001/00001」)。理由は"プロンプトがすで
  // に1行使ってるんで「00001/00001 END」が自然では"――しかし直接の
  // ユーザーによる追加("あなたの理論が正しければ...①で表示される
  // 「00001/00001 END」がそもそも間違い")が、その同じ推論を一貫
  // して適用すると、1つの本物のスクロールバック行は「00001/00001」
  // ではなく「00002/00002」(本物1+生きたプロンプト1)を表示すべき
  // ことになると指摘した――より狭い修正は自身の正当化と矛盾して
  // いた。2つのフラグ: `enabled`(Shell自身のコンストラクタで、
  // setUseFixedWidthPosition(true)と並んで一度、永続的に設定される)
  // は、この扱い全体をShellのscrollback_に特有に有効にする――共有
  // のデフォルトでは「ない」。本当に空のFILEモードのページャー
  // (実際の0バイトの入力元)には、同じ扱いを正当化する等価な
  // 「生きたプロンプト行」が無いため。`atLivePromptLine`は動的な
  // 半分で、Shell自身によってhistoryBrowsing()(自身の宣言コメント
  // 参照)と歩調を合わせて切り替えられる――生きたプロンプトに
  // 留まっている限り(デフォルト)true、閲覧がcursorIndex_を本物の
  // 内容へ動かした瞬間false。そこでは`current`が、生きたプロンプト
  // 自身の末尾の位置ではなく、本物のcursorIndex_+1として読める。
  void setCountLivePromptAsLine(bool enabled);
  void setAtLivePromptLine(bool atLivePromptLine);

  // redraw()の間、候補行ごとに一度呼ばれ(自身のコメント参照)、
  // ある行が、それが「カーソル行ではない」場合でも、ハイライト
  // (Standout、カーソル行自体が既に受けているのとまさに同じ扱い
  // ――renderLine()自身の`marked`引数参照)されて描画されるべきか
  // どうかを決める。DirectoryMode::isMarked()のために2026-08-21に
  // 追加された――実際のユーザー報告("^Xc,m,r等反応無し")は、部分
  // 的に、マーキング(^Space)が視覚的フィードバックを全く持たない
  // ことに起因すると突き止められた: toggleMark()は、描画経路が
  // 一度も参照しないstd::setを変異させるだけだったので、実際に
  // 登録されたマークは、登録されなかったものと画面上で見分けが
  // 付かなかった。デフォルト構築された(空の)述語は、カーソル行
  // だけが常にマークされることを意味する――選択しない全てのContentView
  // の利用者(ArchiveMode自身のマーキング、GrepMode等)に対しては
  // 変更されていない、元の挙動。
  void setMarkedPredicate(std::function<bool(int index)> predicate);

  // trueの間(デフォルト)、何もマークされていなくてもカーソル行は
  // 「常に」Standoutで描画される――既存の全呼び出し元の通常の
  // 慣習。DirectoryModeのCP/MV/RM準備済み閲覧(Application::
  // startCopyPrompt()/startMovePrompt()/startRemovePrompt())に
  // 対してfalseに設定される: 2026-08-22の修正、実際の参照
  // スクリーンショット(=ed、実際のlegacyバイナリの実行)が、
  // この移植自身の挙動(=ei)と直接矛盾していた:
  // "（ls）と違って最初は何も選択していないためハイライト表示も
  // 無く"――通常の`ls`閲覧とは異なり、legacy自身のM_cp/M_mv/M_rm
  // サブモードは、何かが実際にマークされるまで全くハイライトを
  // 表示しない(参照の自身のカーソル行は、行全体の反転ではなく、
  // プレーンなテキストカーソルブロックだけを表示する)。そこでの
  // Standoutは、カーソルがたまたまどこに座っていようと、完全に
  // 実際にマークされたエントリ(setMarkedPredicate()自身の結果)
  // のために予約されている。
  void setCursorRowAlsoHighlighted(bool enabled);

  // false(デフォルト――既存の全呼び出し元の挙動、変更なし)の間、
  // 実際にマークされた行(setMarkedPredicate()自身の結果)は、
  // カーソル行自体が受けるのと「同じ」Standoutの反転表示の箱を
  // 描画するので、2つは視覚的に見分けが付かない。DirectoryModeの
  // 「プレーンな」(CP/MV/RM準備済みではない)ls一覧のビューに
  // 対してtrueに設定される: 2026-08-25、直接のユーザー報告――
  // '['で一連のエントリを繰り返しマークすると、カーソルは実際に
  // マークされた最後のものから1行「過ぎた」ところに留まり
  // (MarkAndRetreatはマークし、「その後」動く)、カーソル行はマー
  // キングに関わらず常にStandoutを表示するので、その最終的な留ま
  // り位置の行は、実際にはtargetEntries()に全く含まれていない
  // にも関わらず「これも選択されている」ように読める
  // ("それはCP等へ渡す分ではなく（ls）のターゲットに過ぎない")。
  // これが有効な場合、実際にマークされた行はStandoutではなく
  // Underlineを描画し、カーソル行はマーキングとは独立にそれ自身
  // の通常のStandoutを保つ――2つの状態は同時に可視で、区別可能な
  // ままである(カーソルであり「かつ」マークされている行は両方を
  // 表示する)。CP/MV/RM準備済みの画面には使われない(上の
  // setCursorRowAlsoHighlighted(false)が、代わりにカーソル自身の
  // ハイライトを抑制するという別の方法で、その画面自身のこの
  // 曖昧さのバージョンを既に解決している――実際のlegacy参照
  // スクリーンショットに照らして一致しているので、そこでも
  // Underlineに変更するのではなくマーク用にStandoutのまま残さ
  // れている)。ArchiveModeのマーキングにも使われない(そこには
  // まだ等価な報告が無い)。
  void setMarkedRowUsesUnderline(bool enabled);

  // モードラインがどの画面の行に描画するかを上書きし、デフォルト
  // のrow0_+height_から切り離す――nullopt(デフォルト)は既存の
  // 全呼び出し元の挙動を変更しないままにする。Shell自身の
  // scrollback_ビューのために追加(2026-08-03): SHELLは、生きた
  // プロンプトのオーバーレイのために、スクロール可能な内容の下に
  // 永続的に予約された「3番目の」行を必要とする(Shell::
  // commandLineRow()自身のコメント参照)――これが無いと、どれだけ
  // スクロールバックが存在するかによって、プロンプトとモードライン
  // は互いに衝突するか、本物のスクロールバックの内容と衝突する
  // ことになる(legacyに対する実際の並べた比較が、スクロールバック
  // がビューポートを満たした時点で、プロンプトと本物の最後の内容行
  // が同じ行を奪い合うことを発見した。完全な調査についてはShell
  // 自身のクラスコメント参照)。
  void setModeLineRow(std::optional<int> row);

 private:
  // underlineMarked(2026-08-25): `marked`/Standoutとは「別の」
  // 属性チャネル――実際にマークされた行とプレーンなカーソル行が
  // 両方とも同じ反転表示の箱に折りたたまれるのではなく、視覚的に
  // 区別可能である必要がある理由についてはsetMarkedRowUsesUnderline()
  // 自身のコメント参照。setMarkedRowUsesUnderline(true)経由で選択
  // した呼び出し元(DirectoryModeのプレーンな閲覧ビュー)に対して
  // だけ常にtrueになる。他の全ての呼び出し元は常にここへfalseを
  // 渡すので、これは既存の挙動への影響が無い純粋な追加である。
  void renderLine(const Line &line, bool marked, bool underlineMarked = false);
  void renderModeLine();
  // 2026-09-13追加、項目458フィードバック("最低限のインジケーター")、
  // 同日項目459フィードバック("クネクネになる...それ用のエリアが
  // 必要...常時...幅は倍")で改訂、さらに同日、ユーザー自身の言葉
  // ("張りぼては-gだけの話だったなと途中で気づいた。気づかぬまま
  // -gと同じく完全に常時、とは書いたけど、モードライン有りの時は
  // 消せる？")で再改訂: redraw()の通常の行描画の「後」に呼ぶ――
  // display_.modeLineVisible()がfalseかつdisplay_.hasNativeScrollbar()
  // がfalse(=TerminalDisplay/RecordingDisplay、Display::
  // hasNativeScrollbar()自身のコメント参照)の間だけ、
  // scrollThumbRowRange()が計算したつまみの行範囲について、
  // columns()/columns()+1の2列(TerminalDisplay::columns()自身の
  // コメント参照――幅の予約自体はモードラインの表示/非表示に関わらず
  // 常時。ここで描画だけを条件分けしても、予約済みの専用領域が
  // 単なる未使用の余白になるだけで他の描画とは衝突しない)を反転
  // 表示の空白2文字で上書きする。
  void renderScrollIndicatorOverlay();
  // 全ての位置フィールドの計算(modeLineText()、renderModeLine()、
  // scrollModeLineText()、scrollModeLineSegments())で共有される:
  // setCountLivePromptAsLine()自身の、生きたプロンプト行のための
  // +1調整を、4箇所ではなく1箇所で適用する。完全な理由については
  // 自身の宣言コメント参照。countLivePromptAsLine_がfalse(他の
  // 全てのContentViewインスタンス自身のデフォルト)の場合は何も
  // しない(両方の参照は手を付けられないまま)。
  void applyLivePromptLineAdjustment(int &total, int &current) const;
  // 2026-09-08追加、content_view.cpp自身のコメント参照。
  void applyRawLineNumbers(int &current, int &total) const;
  void applyRawLineNumbersAt(int &current, int &total) const;
  int clampToShownRange(int index) const;  // .cpp側のコメント参照
 public:
  // 位置の欄の元の行番号から引く数(2026-10-15、EPUBの注釈だけを見せている間、Application::showEpubNoteAt()):
  // 注釈の最初の行が1になるように。0なら本の行番号のまま。
  void setRawLineBase(int base) { rawLineBase_ = base; }
 private:  // ドラッグ・慣性の間の同じもの
  // ModeLineFields::splitLayoutWhenCrowded自身のコメント参照。
  // その経路自身の素朴な幅チェックが失敗した時点で、renderModeLine()
  // によって、通常のfitModeLineFields()/withTruncatedFields()経路
  // の「代わりに」呼ばれる。(withTruncatedFields()の出力ではなく)
  // プロバイダから直接の「切り詰められていない」フィールドを使う
  // ので、この関数自身のFinderスタイルの中間省略記号が、決して
  // 適用される唯一の切り詰めになる――既に切り詰められたname/
  // trailing2をこれに与えると、withTruncatedFields()自身のより
  // きつい予算が既にそれらを省略していた場合、二重の「......」に
  // なる危険があった。
  void renderSplitModeLine(const ModeLineFields &fields, const std::string &positionText);
  void clampViewport();
  int bottomAnchorRowOffset() const;

  // ModeLineFieldsのname/trailingの断片のうち、ディスプレイ自身の
  // 列数の中で`positionText`と並んで収まるものはどれか――切り詰め
  // るのではなく、まずtrailing、次にname(positionText自体は常に
  // 保たれる)の順で落とされる: 任意のバイトオフセットで切られた
  // 部分的なフィールドは、マルチバイトUTF-8文字を分割してしまう
  // 危険がある(このクラスは他の点では意図的に文字コードに無知の
  // ままである。理由についてはクラスコメント自身の注記参照――
  // ここで幅を意識した切り詰めを導入する代わりに、フィールド全体
  // を黙って落とすことでそれを回避する)。そして本物の端末
  // (TerminalDisplay::write()には自身の境界チェックが無い)では、
  // 実際のオーバーフローは、単に視覚的に混雑して見えるだけでなく、
  // 次の画面の行へ折り返してそれを壊してしまう。composeModeLineRow()
  // (プレーンな文字列の消費者、例えばGraphicalDisplay::
  // renderScrolled()のドラッグスクロール経路)とrenderModeLine()
  // (セグメント化されたstandout runの実際の描画経路)で共有され、
  // 両方が何が表示されるかについて一致するようにする。
  struct FieldFit {
    bool showName = false;
    bool showTrailing = false;
    // showTrailingとは独立(2026-08-11の修正): 実際の報告が、
    // ^J^C候補一覧自身の長い正規表現パターンのプレビュー
    // (`trailing`)がtrailing領域全体を列の予算を超えて押し出し、
    // trailing2(移動先ディレクトリ)単独なら簡単に収まるにも
    // 関わらず、それも黙って落としてしまうことを発見した――落とす
    // 優先順位についてはfitModeLineFields()自身のコメント参照。
    bool showTrailing2 = false;
  };
  FieldFit fitModeLineFields(const ModeLineFields &fields, const std::string &positionText) const;

  // 2026-08-14の修正: 上のfitModeLineFields()は、trailing2を
  // 「完全に」表示するか全く表示しないかしか決めない――実際の報告
  // ("戻り値が無い"、FileMode自身の長いLOCATEパス注釈、FileMode::
  // setSearchAnnotation()参照)が、切り詰められた(Finderスタイル、
  // "..." + 末尾)バージョンなら簡単に収まったはずなのに、これが
  // 収まらない時にそれを「完全に」落としてしまうことを発見した。
  // trailingを完全に落とすとtrailing2が「完全に」表示できるように
  // なる場合を「除いて」、name/position/trailingと並んで収まる
  // ようtrailing2を切り詰めた(または、"...x"にすら本当に全く余地
  // が無い場合のみクリアされた)fieldsのコピーを返す。その場合は
  // 代わりにtrailingが落とされる(これはまさに2026-08-11のGREP
  // 候補プレビュー対移動先ディレクトリのケースであり、同じルール
  // の特殊ケースとして保存されている: 実際に達成可能な、最も
  // 完全なtrailing2を表示する)。modeLineFieldsProvider_()を読んだ
  // 直後に一度、下のfitModeLineFields()の全ての消費者によって
  // 呼ばれる――trailing2は構築によって既に列の予算に収まって
  // いるので、fitModeLineFields()自身の階層1はその後常に成功する。
  // その残りの階層は、この修正とは無関係に、病的に狭い端末
  // (name+position+trailingだけでも収まらない)に対する安全網
  // としてのみ、そのまま残されている。
  //
  // 2026-08-15の追加: 上記のいずれよりも先に、`name`自体もまず
  // 切り詰める――実際の報告+スクリーンショット(Application::
  // goToFileSelector()が深くネストされたパスへジャンプした後の、
  // 長すぎるDirectoryMode::currentDirectory())が、
  // fitModeLineFields()が幅に関わらずnameを全く落とさないので、
  // モードラインの行が壊れていることを発見した。(nameがもはや
  // trailing2だけについてのものではないにも関わらず)この同じ
  // 関数に保たれている。それが依然として、全ての描画経路が既に
  // 呼ぶ、共有される唯一の集約点であるため。
  ModeLineFields withTruncatedFields(ModeLineFields fields, const std::string &positionText) const;
  // withTruncatedFields()自体に折り込むのではなく、それとは分離
  // されている(その関数自身の3つの呼び出し箇所それぞれで、その
  // 直後に呼ばれる): withTruncatedFields()には既に自身のいくつか
  // の早期の`return fields;`地点があり、trailing3自身の予算は
  // 本当にtrailing2の「最終的な」(既に切り詰められた)幅に依存
  // するので、その関数の途中に糸を通すのではなく、その関数が
  // 完全に終わった後に厳密に実行しなければならない。これが修正
  // する実際の報告については自身の定義コメント参照。
  ModeLineFields withTruncatedTrailing3(ModeLineFields fields, const std::string &positionText) const;

  // withTruncatedFields()自身の`trailing`と`trailing2`の省略で
  // 共有される(2026-08-17の追加、TrailingKind自身のコメント参照):
  // `text`が既に`budget`に収まる場合は変更せずに返す。"..."+1文字
  // にすら余地が無い場合は""を返す。それ以外は`kind`に従って
  // 省略する――PathMiddleはFinderスタイルの中間省略記号
  // (middleEllipsisTruncate())を使い、Path/QueryEchoは末尾のみの
  // 「...」を使う。QueryEchoはさらに、実際に利用可能な予算に
  // 関わらず16文字に制限される(理由についてはTrailingKind自身の
  // コメント参照)。kind == Tagで呼ばれることは決してない(呼び
  // 出し元が自分でそれをゲートする。Tagは「これを全く呼ばない」
  // ことを意味するため)。
  std::string truncateFieldByKind(const std::string &text, TrailingKind kind, int budget) const;

  // 「name positionText trailing」(fitModeLineFields()参照)を、
  // フィールドごとに描画しない呼び出し元向けに1つのプレーンな
  // 文字列へ組み立てる(composeModeLineRow()自身の消費者は決して
  // 自分でdisplay_.setAttribute()を呼ばないので、renderModeLine()
  // とは異なり、ここではフィールドごとの別々の反転表示runは無い
  // ――それらの呼び出し元が誰かについてはmodeLineText()自身の
  // コメント参照)。
  std::string composeModeLineRow(const std::string &positionText) const;

  Display &display_;

  std::vector<Line> lines_;
  std::vector<Display::ContentImage> images_;  // setContentImages()参照
  int cursorIndex_ = 0;
  std::deque<int> markRing_;  // markJumpOrigin()参照、先頭が最新
  int topIndex_ = 0;
  std::optional<int> hoverIndex_;
  bool cursorHighlightEnabled_ = true;
  bool highlightToRowEnd_ = false;  // setHighlightToRowEnd()参照
  bool bottomAnchored_ = false;
  bool contentMayGrow_ = false;  // setContentMayGrow()自身の宣言コメント参照
  Line emptyPlaceholderLine_ = Line{"--", std::nullopt, std::nullopt, {}};
  bool emptyPlaceholderMarked_ = true;
  // setGrepInPlaceOverlay()/reportGrepNoMatchMessage()自身のコメント
  // 参照(2026-09-11、DirectoryMode/FileModeから一本化)。
  bool grepInPlaceActive_ = false;
  bool grepNoMatchActive_ = false;
  // setGrepInPlaceLabel()自身の宣言コメント参照(2026-09-11追加、
  // FGREP/EGREP/GREP統一)。
  std::string grepInPlaceLabel_ = "GREP";
  // setGrepInPlaceBadge()自身の宣言コメント参照(2026-09-12追加)。
  std::string grepInPlaceProgramBadge_ = "(grep)";
  // setGrepMatchHighlight()自身のコメント参照(旧FileMode::
  // setGrepMatchHighlight()から一本化): セッション最初の1回だけ
  // lines_を「本当にクリーンな」スナップショットとして保持し、以後は
  // 毎回そこから組み直す――Directory(ls一覧)は元々line.attributesを
  // 一切持たないので実質「全クリア」と同じ結果になるが、File/Man等
  // (PipeBufferのオーバーストライク由来Bold/Underlineを既に持つ)では
  // このスナップショットが無いと、GREPのHighlight適用時に正規の
  // 属性ごと消してしまう。
  bool grepMatchHighlightActive_ = false;
  std::vector<Line> grepMatchHighlightSnapshot_;
  bool cursorAtModeLineTrailingEnd_ = false;
  // 2026-08-29の追加(CopyMode、copy_mode.hpp参照): cursorHighlightEnabled_
  // がfalseの時、syncVisibleCursor()は以前、常に実際の端末の
  // キャレットを列0に停めていた――以前の全呼び出し元に対しては
  // 正しかった(LineEditorは自身の列を別に管理する)が、CopyModeに
  // 対しては誤りだった。それは、凍結されたテキストのスナップ
  // ショット内の実際の列をキャレットに追跡させる必要があるため。
  // setContentCursorColumn()はその列を上書きする。他の全ての呼び
  // 出し元では未使用(0のまま、以前の挙動そのまま)。
  int contentCursorColumn_ = 0;
  // setPositionTextOverride()自身のコメント参照。
  std::optional<std::string> positionTextOverride_;
  bool positionTextPlain_ = false;
  // renderModeLine()自体によって設定される(なぜそこから再計算
  // するのではなくキャッシュされるのかについては自身のコメント
  // 参照)――空でない`trailing`フィールドを持つ最初の
  // renderModeLine()呼び出しまでは-1。
  int lastModeLineTrailingEndColumn_ = -1;
  std::function<bool(int)> markedPredicate_;
  bool cursorRowAlsoHighlighted_ = true;
  std::optional<std::pair<int, int>> shownIndexRange_;  // setShownIndexRange()参照
  int rawLineBase_ = 0;                                  // setRawLineBase()参照
  std::vector<int> chapterStarts_;  // setChapterStarts()参照
  bool verticalText_ = false;  // setVerticalText()参照
  std::optional<ByteSpan> extraStandout_;  // setExtraStandout()参照
  bool markedRowUsesUnderline_ = false;

  int row0_ = 0, height_ = 0;
  int marginTop_ = 0, marginBottom_ = 0;  // setMarginRows()参照
  std::function<void()> afterRedraw_;  // setAfterRedraw()
  ModeLineFieldsProvider modeLineFieldsProvider_;
  bool useFixedWidthPosition_ = false;
  bool countLivePromptAsLine_ = false;  // setCountLivePromptAsLine()自身の宣言コメント参照
  bool atLivePromptLine_ = true;        // setAtLivePromptLine()自身の宣言コメント参照
  std::optional<int> modeLineRow_;
};

}  // namespace fm
