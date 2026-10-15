#include "content_view.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "mode_line.hpp"
#include "utf8.hpp"

namespace fm {

namespace {

// renderLine()自身のプレーンな行のケース(自身の呼び出し箇所の
// コメント参照)向けのFinderスタイルの切り詰め: 単に末尾を切り落と
// すのではなく、接頭辞と接尾辞を保ち、中間を"..."で省略する。
// バイトインデックス(renderLine()自身の`col`に一致。それは画面の
// 列ではなくバイトを数える)だが、マルチバイトUTF-8文字が切断箇所
// をまたいで分割されないよう、Utf8::sequence_length()経由で歩く。
//
// 2026-08-16の修正: 元々は頭部1/3、末尾2/3に偏っていた(LOCATEの
// 絶対パスは1つの長い共通接頭辞を共有するので、「異なる」接尾辞が
// 見え続ける必要があった)が、実際の報告+スクリーンショット(=bu)
// が、これは「...」を快適に読むには左に寄せすぎていることを発見
// した――"「...」の位置を行中央付近にしてほしい"。ほぼ均等な
// (1/2、1/2)分割は、(元々の動機となったケースである)長い接頭辞を
// 共有するエントリを区別するのに十分な末尾を保ちつつ、視覚的に
// 中央に見える。legacy自身のalloc.cのalloc_str()/alloc_pp()での
// 等価な修正(同日、同じ「...」を中央近くにという依頼)に合わせて
// いる。
std::string middleEllipsisTruncate(const std::string &text, int maxBytes) {
  static const std::string kEllipsis = "...";
  int budget = maxBytes - static_cast<int>(kEllipsis.size());
  if (budget <= 0) return text.substr(0, static_cast<std::size_t>(std::max(0, maxBytes)));

  std::vector<int> boundaries;  // character-start byte offsets, text.size() itself included as the final one
  boundaries.push_back(0);
  int i = 0;
  while (i < static_cast<int>(text.size())) {
    i += Utf8::sequence_length(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]));
    boundaries.push_back(std::min(i, static_cast<int>(text.size())));
  }

  int headBudget = budget / 2;
  int tailBudget = budget - headBudget;

  int headEnd = 0;
  for (int b : boundaries) {
    if (b > headBudget) break;
    headEnd = b;
  }

  int tailStart = static_cast<int>(text.size());
  for (std::size_t k = boundaries.size(); k-- > 0;) {
    int b = boundaries[k];
    if (static_cast<int>(text.size()) - b > tailBudget) break;
    tailStart = b;
  }

  if (tailStart <= headEnd) return text;  // 有効に省略するには狭すぎる――下のプレーンな切り落としに任せる
  return text.substr(0, static_cast<std::size_t>(headEnd)) + kEllipsis + text.substr(static_cast<std::size_t>(tailStart));
}

// 上のmiddleEllipsisTruncate()の、画面「幅」ベースの兄弟
// (2026-08-19の修正、直接のユーザー報告――ARC自身の内容一覧:
// "内容一覧は^Olでも2/3くらいまでしか表示されていないので右端まで
// よろしく")。下のrenderLine()自身の新しいwidthAware描画経路
// だけに使われる――意図的に別の関数であり、上のバイトベースの
// ものの修正ではない: withTruncatedFields()自身のモードラインの
// name/trailing2の切り詰めもmiddleEllipsisTruncate()を呼ぶ
// (完全に別の、いくつもの呼び出し箇所離れた呼び出し元)。そして
// その自身の周囲の予算計算「と」fitModeLineFields()自身の下流の
// 「実際にこれは収まるか」階層は「両方とも」全体にわたってバイト
// 長ベースである――この修正の以前のバージョンは
// middleEllipsisTruncate()自体を幅ベースへ変更してしまい、その
// 呼び出し元を黙って壊していた: CJK重めの名前は短い画面幅へ切り
// 詰められるが、(2列幅のCJK文字は3バイトかかる)以前より「多く
// の」バイト数で戻ってくることがあり得るので、fitModeLineFields()
// 自身のバイト長による「収まるか」チェックは、切り詰められていない
// 元のものよりさらに長い文字列を見て、モードライン「全体」を隠す
// までの全ての階層を通り抜けてしまっていた(実際の再現で確認済み:
// tarアーカイブ内からCJK名のファイルを開くと、モードラインが
// 素の「1/1 END」だけを読み、他には何も無くなった)。CJK重めの
// テキスト(3バイトのUTF-8文字は「画面上では」たった2列)に対して
// は、バイト予算は実際に利用可能な画面幅より最大3倍厳しい――まさに
// ARCの報告が指していた「2/3幅で止まる」という症状。ここでの
// `maxWidth`は本物の画面列の予算であり、境界はUtf8::
// sequence_length()経由で見つけられ(マルチバイト文字を決して
// 分割しない)、頭部/末尾は生のバイト長ではなく文字ごとの
// Utf8::display_width()によって累積される。
int totalDisplayWidth(const std::string &text);  // 下で定義されている。この関数自身の早期returnのためにここで必要

// 2026-09-11追加、直接のユーザー報告(実機スクリーンショット=gf、
// LOCATE上のin-place GREPハイライトが「行中央の"..."が原因で」ズレて
// いる/一部の行では全く表示されない): middleEllipsisTruncateByWidth()
// /truncateHighlightedSuffix()は文字列そのものを返すだけで、その
// 切り詰めが元のバイトオフセットをどう作り替えたかを一切呼び出し元へ
// 伝えていなかった。ContentView::setGrepMatchHighlight()(2026-09-11
// 追加)が設定するLine::attributes(GREPのHighlight属性)のbyteオフ
// セットは、常に「切り詰め前の」元のline.textに対して計算されている
// ため、renderLine()がellipsisText(切り詰め後の別の文字列)に対して
// そのまま同じオフセットで属性を引き当てようとすると、位置がずれる
// (中間の"..."より後ろの本来の文字位置とは無関係な、たまたま同じ
// バイト位置にある別の文字に当たってしまう)か、あるいは元のオフ
// セットがellipsisText自体の短い長さを超えてしまい一度も一致しない
// (=ハイライトが丸ごと消える)かのどちらかになる。旧来の唯一の
// line.attributes設定元だったFileMode(自身の行は折り返し済みで
// ellipsisEligible/highlightStartColumnのどちらも設定しない)では
// 一度も表面化しなかった問題――renderLine()自身の古いコメント
// ("FileModeがline.attributesの唯一の呼び出し元である")はこの前提の
// 記録であり、in-place GREPをLOCATE/DU/ARCHIVE等へ拡張した今、その
// 前提はもう成り立たない。
//
// EllipsisBoundaries/computeEllipsisBoundaries(): 実際に切り詰めが
// 起きたかどうかと、その時に保持された接頭辞(0..headEnd)・接尾辞
// (tailStart..end、"..."を挟む)の元のバイトオフセットの境界を返す。
// middleEllipsisTruncateByWidth()自身のロジックの複製ではなく、
// そちらが実際に使う「同じ」計算をここへ括り出し、公開関数はこの
// 結果から文字列を組み立てるだけにした(ロジックの二重管理を避ける)。
struct EllipsisBoundaries {
  bool truncated = false;
  int headEnd = 0;    // [0, headEnd) がそのまま保持される接頭辞
  int tailStart = 0;  // [tailStart, text.size()) がそのまま保持される接尾辞
};

EllipsisBoundaries computeEllipsisBoundaries(const std::string &text, int maxWidth) {
  EllipsisBoundaries result;
  if (totalDisplayWidth(text) <= maxWidth) return result;
  static const std::string kEllipsis = "...";
  int budget = maxWidth - static_cast<int>(kEllipsis.size());
  if (budget <= 0) return result;  // プレーンな切り落としに任せる(呼び出し元自身の早期returnと同じ)

  std::vector<int> boundaries;
  std::vector<int> widths;
  boundaries.push_back(0);
  int i = 0;
  while (i < static_cast<int>(text.size())) {
    int len = Utf8::sequence_length(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]));
    len = std::min(len, static_cast<int>(text.size()) - i);
    int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + i, len);
    i += len;
    while (i < static_cast<int>(text.size())) {
      int markLen = Utf8::sequence_length(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]));
      markLen = std::min(markLen, static_cast<int>(text.size()) - i);
      if (Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + i, markLen) != 0) break;
      i += markLen;
    }
    widths.push_back(width);
    boundaries.push_back(i);
  }

  int headBudget = budget / 2;
  int tailBudget = budget - headBudget;

  int headEnd = 0, headWidth = 0;
  for (std::size_t k = 0; k < widths.size(); k++) {
    if (headWidth + widths[k] > headBudget) break;
    headWidth += widths[k];
    headEnd = boundaries[k + 1];
  }

  int tailStart = static_cast<int>(text.size());
  int tailWidth = 0;
  for (std::size_t k = widths.size(); k-- > 0;) {
    if (tailWidth + widths[k] > tailBudget) break;
    tailWidth += widths[k];
    tailStart = boundaries[k];
  }

  if (tailStart <= headEnd) return result;  // 有効に省略するには狭すぎる
  result.truncated = true;
  result.headEnd = headEnd;
  result.tailStart = tailStart;
  return result;
}

// remapByteOffsetThroughEllipsis(): 元のline.text上のバイトオフセット
// (AttributeRunのstart/end)を、"..."で省略された後の文字列上の
// オフセットへ変換する。省略された中間部分に落ちる場合、start側は
// "..."の直前、end側は"..."の直後へ寄せる――マッチが省略部分に完全に
// 埋もれている場合でも"..."全体がハイライトされ、マッチがまたがって
// いる場合も自然に振る舞う。
int remapByteOffsetThroughEllipsis(int pos, const EllipsisBoundaries &b, bool isEnd) {
  constexpr int kEllipsisLen = 3;  // "..." はプレーンなASCII3バイト
  if (pos <= b.headEnd) return pos;
  if (pos >= b.tailStart) return b.headEnd + kEllipsisLen + (pos - b.tailStart);
  return isEnd ? b.headEnd + kEllipsisLen : b.headEnd;
}


// `text`の全文字にわたるUtf8::display_width()の合計――「既に」幅で
// 切り詰められた文字列(renderSplitModeLine()自身のtrailing2)を
// 実際の画面の列へ右揃えするため。バイト長(text.size())は、CJKの
// 内容に対しては不足してしまう(同じバイト対幅の区別については
// middleEllipsisTruncateByWidth()自身のコメント参照)。
int totalDisplayWidth(const std::string &text) {
  int width = 0;
  int i = 0;
  while (i < static_cast<int>(text.size())) {
    int len = Utf8::sequence_length(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]));
    len = std::min(len, static_cast<int>(text.size()) - i);
    width += Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + i, len);
    i += len;
  }
  return width;
}

}  // namespace

// 2026-08-30、直接のユーザー報告(GUI、実際のマウスドラッグ中――特に
// スクロールが起きようが起きまいが、ドラッグ中は毎フレームこれが
// 必要になる――ARCモードで再現、スクリーンショット=ev/=ew): 元々
// 上の匿名namespace限定(ファイル内、renderLine()/renderSplitModeLine()
// からしか呼べない)だったが、GraphicalDisplay::renderScrolled()
// (graphical_display.cpp、通常のContentView::redraw()を一切経由
// しない別の描画経路)からも同じ切り詰めを適用できるよう、公開
// (fm::)関数へ格上げした。ロジック自体は変更していない――
// totalDisplayWidth()(直後の匿名namespace内で定義)は、匿名
// namespaceのメンバーであってもこのファイル内の通常の名前探索で
// 見えるので、ここが匿名namespaceの外であっても問題無く呼べる。
std::string middleEllipsisTruncateByWidth(const std::string &text, int maxWidth) {
  // 2026-08-28の修正、直接のユーザー報告(「CLAUDE.md」自体が、
  // そのままで収まるほど短いにも関わらず、省略記号で「CLA....md」
  // に破壊されて戻ってきた): renderSplitModeLine()自身の
  // 2026-08-28の柔軟な予算の修正より前の全ての既存の呼び出し元は、
  // 常に既に収まらないことが確認された文字列を渡していた(この
  // 関数には自身の早期returnが無く、下の切り詰めロジックのどれも
  // 実際に切り詰めがそもそも必要かどうかをチェックしない)――
  // 古い「両側とも常にちょうど強制された半分」という形の下では
  // 正しかったが、新しい柔軟な割り当ては今や、短い側にそれ自身の
  // 実際の幅をちょうどmaxWidthとして渡すことがあり、それは変更
  // されずに戻ってくる必要があり、破損した内容によって同じ合計
  // 長さへ省略記号で切り詰められてはならない。
  //
  // 2026-09-11修正: 実際の切り詰めロジック(書記素クラスタの境界の
  // 計算・幅0の結合マークの吸収等、下の各コメント参照)自体は
  // computeEllipsisBoundaries()へ括り出した――renderLine()がAttributeRun
  // (GREPのHighlight等)のバイトオフセットを、この切り詰め後の文字列
  // 上の位置へ正しく作り直すために同じ境界情報を必要とするようになった
  // ため(EllipsisBoundaries自身の宣言コメント参照)、ロジックを二重に
  // 保守しないための整理。
  EllipsisBoundaries b = computeEllipsisBoundaries(text, maxWidth);
  if (!b.truncated) {
    if (totalDisplayWidth(text) <= maxWidth) return text;
    int budget = maxWidth - 3;  // "..." はプレーンなASCII3バイト
    if (budget <= 0) return text.substr(0, static_cast<std::size_t>(std::max(0, maxWidth)));
    return text;  // 有効に省略するには狭すぎる――下のプレーンな切り落としに任せる
  }
  static const std::string kEllipsis = "...";
  return text.substr(0, static_cast<std::size_t>(b.headEnd)) + kEllipsis +
         text.substr(static_cast<std::size_t>(b.tailStart));
}

// truncateHighlightedSuffix()自身の宣言側コメント(content_view.hpp)
// 参照。`highlightStart`より手前は常にプレーンなASCII接頭辞である
// (highlightStartColumn自身のコメント参照、バイト数==画面幅)ので、
// その幅はhighlightStartそのもの――残りの予算をファイル名側の
// middleEllipsisTruncateByWidth()へそのまま渡せる。
std::string truncateHighlightedSuffix(const std::string &text, int highlightStart, int maxWidth) {
  highlightStart = std::clamp(highlightStart, 0, static_cast<int>(text.size()));
  if (totalDisplayWidth(text) <= maxWidth) return text;
  int budget = maxWidth - highlightStart;
  if (budget <= 0) return text.substr(0, static_cast<std::size_t>(std::max(0, maxWidth)));
  std::string prefix = text.substr(0, static_cast<std::size_t>(highlightStart));
  std::string suffix = text.substr(static_cast<std::size_t>(highlightStart));
  return prefix + middleEllipsisTruncateByWidth(suffix, budget);
}

// EllipsisBoundaries自身のコメント参照(2026-09-11追加)。
// truncateHighlightedSuffix()自身と同じ計算だが、文字列を組み立てる
// 代わりに境界だけを返す――しかも接尾辞側の相対オフセットではなく
// `text`全体に対するオフセットへ`highlightStart`ぶんずらし済み(呼び
// 出し元がAttributeRunの生のバイトオフセットをそのまま渡せるように)。
EllipsisBoundaries computeHighlightedSuffixBoundaries(const std::string &text, int highlightStart, int maxWidth) {
  highlightStart = std::clamp(highlightStart, 0, static_cast<int>(text.size()));
  EllipsisBoundaries result;
  if (totalDisplayWidth(text) <= maxWidth) return result;
  int budget = maxWidth - highlightStart;
  if (budget <= 0) return result;  // プレーンな切り落としに任せる(呼び出し元自身の早期returnと同じ)
  std::string suffix = text.substr(static_cast<std::size_t>(highlightStart));
  EllipsisBoundaries b = computeEllipsisBoundaries(suffix, budget);
  if (!b.truncated) return result;
  b.headEnd += highlightStart;
  b.tailStart += highlightStart;
  return b;
}

// content_view.hpp自身の宣言コメント参照。GraphicalDisplay::
// drawScrollbar()の比例配置の計算(トラック高さに対する可視割合、
// スクロール位置に応じたつまみの配置)を、ピクセルではなく行単位に
// 移植したもの――変数名(trackHeight→height、totalLines→total等)も
// 対応関係が分かるよう揃えている。total<=height(スクロール不可)の
// 場合は{0,0}を返し、呼び出し元は何も描かない――GraphicalDisplayの
// 本物のスクロールバーは可視割合100%でもトラック全体を塗るが、こちら
// は専用の2列を常時確保してはいるものの、スクロールできないビューで
// 画面全体を反転表示で塗り潰すことに実用上の利点は無く(むしろ視覚的
// なノイズになる)、「既に全部見えている」ことが伝わればよいので何も
// 描かない方を選んだ。
std::pair<int, int> scrollThumbRowRange(int topIndex, int height, int total) {
  if (height <= 0 || total <= height) return {0, 0};
  int scrollableLines = total - height;
  double visibleFraction = static_cast<double>(height) / total;
  int thumbHeight = std::clamp(static_cast<int>(std::lround(height * visibleFraction)), 1, height);
  int maxThumbTop = height - thumbHeight;
  int thumbTop = std::clamp(static_cast<int>(std::lround(maxThumbTop * (static_cast<double>(topIndex) /
                                                                          scrollableLines))),
                             0, maxThumbTop);
  return {thumbTop, thumbHeight};
}

ContentView::ContentView(Display &display) : display_(display) {}

void ContentView::setCursorAtModeLineTrailingEnd(bool enabled) { cursorAtModeLineTrailingEnd_ = enabled; }

void ContentView::setEmptyPlaceholder(Line line, bool marked) {
  emptyPlaceholderLine_ = std::move(line);
  emptyPlaceholderMarked_ = marked;
}

void ContentView::setViewport(int row0, int height) {
  row0_ = row0;
  height_ = height;
  clampViewport();
}

void ContentView::setLines(std::vector<Line> lines) {
  lines_ = std::move(lines);
  markRing_.clear();  // markJumpOrigin()参照――別の内容の行番号は意味を成さない
  if (cursorIndex_ >= static_cast<int>(lines_.size()))
    cursorIndex_ = std::max(0, static_cast<int>(lines_.size()) - 1);
  // topIndex_は、下のhoverIndex_と同様、この「ビューが」以前表示
  // していたものが何であれそこから残った古びたスクロール位置の
  // 状態である――その内容が丸ごと置き換えられた時点で意味を成さ
  // ない(そして積極的に有害である)。実際のユーザー報告/再現の後
  // に追加された: cpp/(7エントリ) → Enterでsrc/(74エントリ)へ →
  // 数行下へスクロール(src/「自身の」topIndex_を2桁台に大きく
  // 進める) → 再びcpp/へ戻る。cursorIndex_自体は(ここと、その後の
  // 呼び出し元自身のsetCursorIndex()呼び出し、例えばDirectoryMode::
  // enterDirectory()の履歴位置復元の両方によって)正しく再制限/
  // 復元されるが、src/からの古い、はるかに大きいtopIndex_はcpp/
  // 自身のredraw()に生き残ってしまった――clampViewport()自身の
  // 「topIndex_ > cursorIndex_なら下へスナップ」分岐は、topIndex_
  // が(今や制限済みの、範囲内の)cursorIndex_より「大きい」場合に
  // だけ発火し、単に依然として範囲内にある古びたtopIndex_は必ずしも
  // それを引き起こさないので、cpp/の一覧は、全7エントリが1画面に
  // 楽々収まるにも関わらず、ほぼ自身の末尾までスクロールされて
  // (最後の数エントリだけを表示して)描画された。ここで0へリセット
  // することは、全ての新しいsetLines()がクリーンで正しいスクロール
  // 位置から始まることを意味し、呼び出し元自身のsetCursorIndex()
  // (もしあれば)は、復元されたカーソル位置が本当にそれを必要と
  // する場合、通常のclampViewport()経路経由で自由に再度下へ
  // スクロールできる。
  topIndex_ = 0;
  // hoverIndex_(別の、純粋に視覚的な上書き――自身のセッターの
  // コメント参照)は、その下の実際の内容が丸ごと置き換えられた
  // 時点で、何かを意味すると信頼することはできない: それは依然と
  // して「古い」lines_へのインデックスであり、たまたま「新しい」
  // lines_への有効なインデックスでもある場合(ほとんどの一覧は
  // 劇的に短くはならないので、よくあるケース)でさえ、それは
  // 意図してではなく偶然に無関係な内容を指している。実際のユーザー
  // 報告の後に追加された: サブディレクトリへ降りて、そこでマウス
  // をホバーし、その後親へ戻る(^G)と、最終的に目に見えてハイ
  // ライトされた行は、「子」ディレクトリでの最後のホバーがたまたま
  // 着地した場所であり――実際のカーソル位置(それ自体はモードライン
  // 自身のcurrent/total数値経由で確認済みの通り正しかったが、
  // redraw()自身の`hoverIndex_.has_value() ? *hoverIndex_ :
  // cursorIndex_`という優先順位により、この古びたhoverIndex_に
  // よって視覚的に上書きされていた)ではなかった。全ての内容の
  // 置き換えが漏斗のように通る単一の場所(DirectoryMode::
  // enterDirectory()、FileMode::loadFile()/reflow()、他の全ての
  // モード自身の検索/一覧リフレッシュ)でここでこれをクリアする
  // ことは、それらの個々の呼び出し箇所それぞれでsetHoverIndex
  // (nullopt)呼び出しを覚えておこうとする(Application::
  // handleCommand()のMove/Page/GoTo/Redrawディスパッチが「キー
  // ボード」ナビゲーションに対して既に取っている、まさにその
  // より狭いアプローチ――自身のコメント参照――だが、それは「同じ」
  // 内容の中を動くことだけをカバーし、内容が完全に置き換えられる
  // ことは決してカバーしない)よりも、はるかに堅牢である。
  hoverIndex_ = std::nullopt;
  clampViewport();
}

const std::vector<ContentView::Line> &ContentView::lines() const { return lines_; }

std::size_t ContentView::totalByteCount() const {
  std::size_t bytes = 0;
  for (const auto &line : lines_) bytes += line.text.size() + 1;
  return bytes;
}

int ContentView::cursorIndex() const { return cursorIndex_; }

int ContentView::lineIndexAtRow(int displayRow) const {
  int row = displayRow - row0_;
  if (row < 0 || row >= height_) return -1;
  int rowOffset = bottomAnchorRowOffset();
  if (row < rowOffset) return -1;  // 空白パディングの行、下端固定の場合のみ
  int idx = topIndex_ + (row - rowOffset);
  if (idx < 0 || idx >= static_cast<int>(lines_.size())) return -1;
  return idx;
}

// scrollHighlightForLine()自身の宣言側コメント(content_view.hpp)参照。
// redraw()自身のループ本体(cursorRow/markedRow/markedRowUsesUnderline_
// の計算式)と意図的に一字一句揃えてある――ずれると、静止中と
// ドラッグ中とでどの行がハイライトされるかが食い違ってしまう。
std::optional<Display::AttributeRun> ContentView::scrollHighlightForLine(int lineIndex) const {
  if (lineIndex < 0 || lineIndex >= static_cast<int>(lines_.size())) return std::nullopt;
  if (!cursorHighlightEnabled_) return std::nullopt;
  const Line &line = lines_[lineIndex];
  if (line.neverHighlighted) return std::nullopt;
  int highlighted = hoverIndex_.has_value() ? *hoverIndex_ : cursorIndex_;
  bool cursorRow = lineIndex == highlighted && cursorRowAlsoHighlighted_;
  bool markedRow = markedPredicate_ && markedPredicate_(lineIndex);
  bool marked, underlineMarked;
  if (markedRowUsesUnderline_) {
    marked = cursorRow && !markedRow;
    underlineMarked = markedRow;
  } else {
    marked = cursorRow || markedRow;
    underlineMarked = false;
  }
  if (!marked && !underlineMarked) return std::nullopt;
  int highlightStart = std::max(0, line.highlightStartColumn.value_or(0));
  int end = static_cast<int>(line.text.size());
  if (line.highlightEndColumn) end = std::min(end, *line.highlightEndColumn);
  if (highlightStart >= end) return std::nullopt;
  return Display::AttributeRun{highlightStart, end,
                                marked ? Display::Attribute::Standout : Display::Attribute::Underline};
}

// ellipsisBudget()自身の宣言側コメント(content_view.hpp)参照。
// renderLine()自身のellipsisBudgetローカル変数と一字一句同じ式。
int ContentView::ellipsisBudget() const {
  return cursorHighlightEnabled_ ? std::max(0, display_.columns() - 1) : display_.columns();
}

bool ContentView::isColumnWithinLineScope(int lineIndex, int column) const {
  if (lineIndex < 0 || lineIndex >= static_cast<int>(lines_.size())) return false;
  const Line &line = lines_[lineIndex];
  int start = std::max(0, line.highlightStartColumn.value_or(0));
  return column >= start && column < static_cast<int>(line.text.size());
}

bool ContentView::isColumnWithinRowText(int lineIndex, int column) const {
  if (lineIndex < 0 || lineIndex >= static_cast<int>(lines_.size())) return false;
  return column >= 0 && column < static_cast<int>(lines_[lineIndex].text.size());
}

int ContentView::cursorDisplayRow() const {
  if (cursorIndex_ < topIndex_ || cursorIndex_ >= topIndex_ + height_) return -1;
  return row0_ + bottomAnchorRowOffset() + (cursorIndex_ - topIndex_);
}

void ContentView::syncVisibleCursor() {
  // cursorAtModeLineTrailingEnd_(2026-08-12の修正): 自身のセッター
  // コメント参照。下の通常のハイライトされた内容行のロジックより
  // 「先に」チェックされる――直接のユーザーによる訂正:
  // "『候補一覧のハイライト行末尾へ正しくカーソルを移動する設計』
  // が有効なのはハイライト行を選択する場合の話で文字入力はモード
  // ラインで行われるので最下行の文字列末尾にないとおかしい"。
  // 実際のmacOS報告で確認済み: ATOK自身の入力モード表示アイコン
  // (macOSがシステムの実際の現在のテキスト挿入キャレットの真下に
  // 描画する)が、モードラインではなく「最上部」の内容行の上に
  // 現れていた――この移植が端末に伝えていたキャレットが、単なる
  // 見た目の好みではなく実際に間違った場所にあったことの、独立した
  // OSレベルの証拠。
  if (cursorAtModeLineTrailingEnd_ && lastModeLineTrailingEndColumn_ >= 0) {
    display_.setVisibleCursor(modeLineRow_.value_or(row0_ + height_), lastModeLineTrailingEndColumn_);
    return;
  }
  if (!cursorHighlightEnabled_) {
    int row = cursorDisplayRow();
    if (row >= 0 && contentCursorColumn_ >= 0) {  // 負: カーソルを出さない(EPUBの画像ページ、2026-10-03)
      display_.setVisibleCursor(row, contentCursorColumn_);
      return;
    }
    display_.hideVisibleCursor();
    return;
  }
  // cursorHighlightEnabled_(2026-08-05の修正): renderLine()が現在の
  // 行に対して描くSGR反転表示のrunこそが、ここで「あなたはここに
  // いる」を示す「べき」ものなので、この分岐は以前、端末自身の
  // 別のブロックカーソルを単にhideVisibleCursor()で邪魔にならない
  // ようにしていた――しかしTerminalDisplay::hideVisibleCursor()は
  // 何もしない(そこにはANSIの「カーソルを隠す」シーケンスが配線
  // されていない、自身のコメント参照)ので、本物の端末バックエンド
  // ではその呼び出しは全く何もしていなかった: 実際のユーザー報告
  // が、実際のブロックカーソルが、最後の画面書き込みがたまたま
  // 着地した場所(通常はモードライン自身の右揃えのカテゴリバッジ、
  // 例えば項目166/167の「CUT」/「FGREP」の透かしの後)に単に座って
  // いることを発見した。そして「空の」一覧(renderLine()がハイライト
  // すべきものが全く無い――redraw()自身の「--」プレースホルダーの
  // コメント参照)では、それと競合する反転表示の行も無かったので、
  // 現在の位置は何によっても視覚的に示されていなかった。それを
  // 明示的にハイライトされた行自身の右端へ動かすことで、実際の
  // カーソルは、最近描画された何かへさまよい出るのではなく、
  // 現在の選択に視覚的に付いたままになる。
  int row = cursorDisplayRow();
  if (row < 0) {
    display_.hideVisibleCursor();
    return;
  }
  // 2026-08-28の修正、実際の報告("ブロックカーソル位置が文字列の
  // 末尾に来ている...日本語ファイルを含む文字列に対して、ベタな文字
  // 列数（UTF8一文字=3バイトを考慮してない）として扱われているため
  // 見た目の文字列末尾にその分の空白が挿入された位置にブロックカー
  // ソルが来てしまう"): これは以前lines_[cursorIndex_].text.size()
  // だった――画面の列数ではなく生の「バイト」数だったので、マルチ
  // バイトのUTF-8名(CJKファイル名を含む)は、実際の端末のキャレット
  // を、テキスト自身の可視の末尾をはるかに過ぎた、空白のパディング
  // の中に置いてしまっていた。ユーザー自身が提案した修正
  // ("同カーソルは文字列末尾ではなく冒頭に表示して欲しい。そうすれば
  // utf8を含むかどうかは無視できる")の通り: その行自身のハイライト
  // される/選択可能なテキストの列(そのようなものが無い行、例えば
  // 空一覧のプレースホルダーに対しては0)は、テキスト自身の幅を
  // 全く測る必要が無いので、バイト対列の問題全体を完全に回避する。
  int width = 0;
  if (cursorIndex_ >= 0 && cursorIndex_ < static_cast<int>(lines_.size()))
    width = std::max(0, lines_[cursorIndex_].highlightStartColumn.value_or(0));
  display_.setVisibleCursor(row, width);
}

int ContentView::topIndex() const { return topIndex_; }

// CopyMode::moveUp()/moveDown()自身のコメント参照(2026-09-07追加、
// 直接のユーザー報告⑥: "コピーモード発動後の^N^Pでページ送りが
// 起きた場合、通常の動作と違うので同じにして")――moveUp()/moveDown()
// 自身のスムーズな1行ずつのスクロール式を、CopyModeのように独自に
// cursorRow_/lines_を管理するクラスからも再現できるようにする。
int ContentView::height() const { return height_; }

void ContentView::setTopIndex(int index) {
  topIndex_ = std::clamp(index, 0, std::max(0, maxTopIndex()));
}

void ContentView::centerOnCursor() { setTopIndex(cursorIndex_ - height_ / 2); }

void ContentView::markJumpOrigin() {
  if (!markRing_.empty() && markRing_.front() == cursorIndex_) return;
  markRing_.push_front(cursorIndex_);
  if (static_cast<int>(markRing_.size()) > kMarkRingSize) markRing_.pop_back();
}

bool ContentView::exchangeJumpMark() {
  if (markRing_.empty() || lines_.empty()) return false;
  int target = std::clamp(markRing_.front(), 0, static_cast<int>(lines_.size()) - 1);
  markRing_.front() = cursorIndex_;
  cursorIndex_ = target;
  if (cursorIndex_ < topIndex_ || cursorIndex_ >= topIndex_ + height_) centerOnCursor();
  redraw();
  return true;
}

bool ContentView::popJumpMark() {
  if (markRing_.empty() || lines_.empty()) return false;
  int mark = markRing_.front();
  markRing_.pop_front();
  markRing_.push_back(mark);
  cursorIndex_ = std::clamp(mark, 0, static_cast<int>(lines_.size()) - 1);
  if (cursorIndex_ < topIndex_ || cursorIndex_ >= topIndex_ + height_) centerOnCursor();
  redraw();
  return true;
}

bool ContentView::stepTopIndex(int direction) {
  if (!canStepTopIndex(direction)) return false;
  if (direction > 0) {
    topIndex_++;
    cursorIndex_ = std::min(topIndex_ + height_ - 1, static_cast<int>(lines_.size()) - 1);
  } else if (direction < 0) {
    topIndex_--;
    cursorIndex_ = topIndex_;
  }
  return true;
}

// stepTopIndex()自体が課しているのとまさに同じ境界条件への、変更
// を伴わない覗き見――2026-08-10に追加され、pixelOffsetが既にセル
// の高さ1つ分をまるまる蓄積した「後で」境界を発見するのではなく、
// ピクセルオフセットの移動を確定する「前に」呼び出し元が
// 「そもそもこの方向へ動く余地があるか」をチェックできるように
// する(これが修正する実際のバグについてはmain.cpp自身の惰性
// ループの呼び出し箇所のコメント参照: 慣性で惰性している間、実際
// の上端/下端の行を過ぎて数フレームの空白が短く見える、
// "空行を表示したのち一行分戻って正規画面になる")。
bool ContentView::canStepTopIndex(int direction) const {
  if (height_ <= 0 || lines_.empty()) return false;
  int maxTop = maxTopIndex();
  if (direction > 0) return topIndex_ < maxTop;
  if (direction < 0) return topIndex_ > 0;
  return true;
}

void ContentView::setHoverIndex(std::optional<int> index) { hoverIndex_ = index; }
std::optional<int> ContentView::hoverIndex() const { return hoverIndex_; }

void ContentView::setCursorHighlightEnabled(bool enabled) { cursorHighlightEnabled_ = enabled; }
void ContentView::setHighlightToRowEnd(bool enabled) { highlightToRowEnd_ = enabled; }
bool ContentView::cursorHighlightEnabled() const { return cursorHighlightEnabled_; }

void ContentView::setContentCursorColumn(int column) {
  contentCursorColumn_ = column;
  if (!cursorHighlightEnabled_) syncVisibleCursor();
}

void ContentView::setPositionTextOverride(std::optional<std::string> text, bool plain) {
  positionTextOverride_ = std::move(text);
  positionTextPlain_ = plain;
}

// setGrepInPlaceOverlay()自身の宣言コメント参照(content_view.hpp、
// 2026-09-11、DirectoryMode::setGrepInPlaceOverlay()から一本化)。
void ContentView::setGrepInPlaceOverlay(bool active) { grepInPlaceActive_ = active; }
bool ContentView::grepInPlaceActive() const { return grepInPlaceActive_; }

// setGrepInPlaceLabel()自身の宣言コメント参照(content_view.hpp)。
void ContentView::setGrepInPlaceLabel(std::string label) { grepInPlaceLabel_ = std::move(label); }
const std::string &ContentView::grepInPlaceLabel() const { return grepInPlaceLabel_; }

// setGrepInPlaceBadge()自身の宣言コメント参照(content_view.hpp)。
void ContentView::setGrepInPlaceBadge(std::string badge) { grepInPlaceProgramBadge_ = std::move(badge); }
const std::string &ContentView::grepInPlaceProgramBadge() const { return grepInPlaceProgramBadge_; }

// DirectoryMode::reportGrepNoMatchMessage()自身の旧コメント参照
// (2026-09-11、こちらへ一本化): plain=trueで非ハイライト表示にする
// (直接のユーザー指摘: "非ハイライトの「〜:No String」を表示して
// 欲しい")。
void ContentView::reportGrepNoMatchMessage(const std::string &message) {
  grepNoMatchActive_ = true;
  setPositionTextOverride(message, /*plain=*/true);
}

void ContentView::clearGrepNoMatchMessage() {
  grepNoMatchActive_ = false;
  setPositionTextOverride(std::nullopt);
}

bool ContentView::grepNoMatchActive() const { return grepNoMatchActive_; }

// 旧FileMode::setGrepMatchHighlight()自身のコメント参照(2026-09-11、
// こちらへ一本化): セッション最初の1回だけlines_を「本当にクリーンな」
// スナップショットとして撮り、以後(ESC iの毎キー入力での再呼び出し
// 含む)はそこから毎回組み直す――呼ぶたびにその時点のlines_(前回の
// Highlightが既に乗っている可能性がある)を「元の状態」として撮り
// 直すと、前回分が焼き込まれて消せなくなる(実際の旧バグ)。File/Man等
// が既に持つPipeBuffer由来のBold/Underlineも、このスナップショット
// 経由で保持される。
void ContentView::setGrepMatchHighlight(const std::vector<GrepHighlightMatch> &matches, int currentLineIndex,
                                         std::pair<int, int> currentSpan) {
  int savedTop = topIndex_;
  int savedCursor = cursorIndex_;
  if (!grepMatchHighlightActive_) grepMatchHighlightSnapshot_ = lines_;
  std::vector<Line> newLines = grepMatchHighlightSnapshot_;
  for (const auto &match : matches) {
    int idx = match.sourceLineIndex;
    if (idx < 0 || idx >= static_cast<int>(newLines.size())) continue;
    if (match.matchSpans.empty()) {
      // 空(行全体をハイライトするフォールバック、GrepHighlightMatch
      // 自身の宣言コメント参照)の場合、行全体を1件の出現として扱う
      // ――currentSpanとの厳密な一致判定はできないので、行番号だけで
      // 現在位置かどうかを決める。
      Display::Attribute attr =
          (idx == currentLineIndex) ? Display::Attribute::HighlightCurrent : Display::Attribute::Highlight;
      newLines[idx].attributes.push_back(AttributeRun{0, static_cast<int>(newLines[idx].text.size()), attr});
    } else {
      for (const auto &span : match.matchSpans) {
        Display::Attribute attr = (idx == currentLineIndex && span == currentSpan)
                                       ? Display::Attribute::HighlightCurrent
                                       : Display::Attribute::Highlight;
        newLines[idx].attributes.push_back(AttributeRun{span.first, span.second, attr});
      }
    }
  }
  grepMatchHighlightActive_ = true;
  setLines(std::move(newLines));
  setTopIndex(savedTop);
  setCursorIndex(savedCursor);
}

void ContentView::clearGrepMatchHighlight() {
  if (!grepMatchHighlightActive_) return;
  grepMatchHighlightActive_ = false;
  int savedTop = topIndex_;
  int savedCursor = cursorIndex_;
  setLines(std::move(grepMatchHighlightSnapshot_));
  grepMatchHighlightSnapshot_.clear();
  setTopIndex(savedTop);
  setCursorIndex(savedCursor);
}

void ContentView::setBottomAnchored(bool enabled) { bottomAnchored_ = enabled; }

void ContentView::setContentMayGrow(bool mayGrow) { contentMayGrow_ = mayGrow; }
bool ContentView::contentMayGrow() const { return contentMayGrow_; }

// redraw()/lineIndexAtRow()で共有され、両方とも下端固定の時に内容
// がどこから始まるかについて一致するようにする――setBottomAnchored()
// 自身のコメント参照。上端固定(デフォルト)の場合、またはどちらに
// せよビューポートを満たすのに十分な内容がある(パディング不要)
// 場合は0。
int ContentView::bottomAnchorRowOffset() const {
  if (!bottomAnchored_) return 0;
  int visibleCount = std::min(static_cast<int>(lines_.size()) - topIndex_, height_);
  return std::max(0, height_ - visibleCount);
}

void ContentView::setCursorIndex(int index) {
  if (lines_.empty()) {
    cursorIndex_ = 0;
    return;
  }
  cursorIndex_ = std::clamp(index, 0, static_cast<int>(lines_.size()) - 1);
  clampViewport();
}

void ContentView::clampViewport() {
  if (topIndex_ > cursorIndex_) topIndex_ = cursorIndex_;
  else if (height_ > 0 && cursorIndex_ >= topIndex_ + height_)
    topIndex_ = cursorIndex_ - height_ + 1;
  if (topIndex_ < 0) topIndex_ = 0;
}

void ContentView::moveUp() {
  if (cursorIndex_ <= 0) {
    display_.putChar(7);
    return;
  }
  cursorIndex_--;
  if (cursorIndex_ < topIndex_) {
    topIndex_ = cursorIndex_;
    if (display_.smoothScrollEnabled()) {
      // sr_row()からの移植: ページ全体を再描画する代わりに、端末の
      // 行を1行分下へスクロールして(上に新しい行を明かす)、その
      // 行だけを描く。
      display_.moveCursor(row0_, 0);
      display_.scrollReverse();
      renderLine(lines_[topIndex_], topIndex_ == cursorIndex_);
      renderModeLine();
      renderScrollIndicatorOverlay();
      display_.flush();
      return;
    }
  }
  redraw();
}

void ContentView::moveDown() {
  if (lines_.empty() || cursorIndex_ >= static_cast<int>(lines_.size()) - 1) {
    display_.putChar(7);
    return;
  }
  cursorIndex_++;
  if (cursorIndex_ >= topIndex_ + height_) {
    topIndex_++;
    if (display_.smoothScrollEnabled()) {
      // sf_row()からの移植: 端末の行を1行分上へスクロールして
      // (下に新しい行を明かす)、その行だけを描く。
      int bottomRow = row0_ + height_ - 1;
      display_.moveCursor(bottomRow, 0);
      display_.scrollForward();
      display_.clearToEndOfLine();
      int bottomIndex = topIndex_ + height_ - 1;
      if (bottomIndex < static_cast<int>(lines_.size()))
        renderLine(lines_[bottomIndex], bottomIndex == cursorIndex_);
      renderModeLine();
      renderScrollIndicatorOverlay();
      display_.flush();
      return;
    }
  }
  redraw();
}

void ContentView::scrollLineDown() {
  if (height_ <= 0 || lines_.empty()) {
    display_.putChar(7);
    return;
  }
  int maxTop = maxTopIndex();
  if (topIndex_ >= maxTop) {
    display_.putChar(7);
    return;
  }
  topIndex_++;
  cursorIndex_ = std::min(topIndex_ + height_ - 1, static_cast<int>(lines_.size()) - 1);
  if (display_.smoothScrollEnabled()) {
    // moveDown()自身の高速経路が使うのと同じsf_row()スタイルの
    // 1行単位のハードウェアスクロール――自身のコメント参照。ここ
    // での唯一の違いは、これが常に実行される(「カーソルがビュー
    // ポートから出る場合にのみ」というガードが無い)ことで、
    // scrollLineDown()が常にスクロールすることに合わせている。
    int bottomRow = row0_ + height_ - 1;
    display_.moveCursor(bottomRow, 0);
    display_.scrollForward();
    display_.clearToEndOfLine();
    int bottomIndex = topIndex_ + height_ - 1;
    if (bottomIndex < static_cast<int>(lines_.size()))
      renderLine(lines_[bottomIndex], bottomIndex == cursorIndex_);
    renderModeLine();
    renderScrollIndicatorOverlay();
    display_.flush();
    return;
  }
  redraw();
}

void ContentView::scrollLineUp() {
  if (height_ <= 0 || lines_.empty()) {
    display_.putChar(7);
    return;
  }
  if (topIndex_ <= 0) {
    display_.putChar(7);
    return;
  }
  topIndex_--;
  cursorIndex_ = topIndex_;
  if (display_.smoothScrollEnabled()) {
    // moveUp()自身の高速経路が使うのと同じsr_row()スタイルの1行
    // 単位のハードウェアスクロール――自身のコメント参照。
    display_.moveCursor(row0_, 0);
    display_.scrollReverse();
    renderLine(lines_[topIndex_], topIndex_ == cursorIndex_);
    renderModeLine();
    renderScrollIndicatorOverlay();
    display_.flush();
    return;
  }
  redraw();
}

void ContentView::pageUp() {
  if (height_ <= 0) return;
  topIndex_ = std::max(0, topIndex_ - height_);
  cursorIndex_ = std::max(0, cursorIndex_ - height_);
  redraw();
}

void ContentView::pageDown() {
  if (height_ <= 0 || lines_.empty()) return;
  int maxTop = maxTopIndex();
  topIndex_ = std::min(maxTop, topIndex_ + height_);
  cursorIndex_ = std::min(static_cast<int>(lines_.size()) - 1, cursorIndex_ + height_);
  redraw();
}

void ContentView::goToTop() {
  cursorIndex_ = 0;
  topIndex_ = 0;
  redraw();
}

void ContentView::goToBottom() {
  if (lines_.empty()) {
    cursorIndex_ = topIndex_ = 0;
    redraw();
    return;
  }
  cursorIndex_ = static_cast<int>(lines_.size()) - 1;
  topIndex_ = std::max(0, cursorIndex_ - height_ + 1);
  redraw();
}

void ContentView::renderLine(const Line &line, bool marked, bool underlineMarked) {
  marked = marked && cursorHighlightEnabled_;
  underlineMarked = underlineMarked && cursorHighlightEnabled_;
  int highlightStart = std::max(0, line.highlightStartColumn.value_or(0));
  bool standoutOn = false, boldOn = false, underlineOn = false, italicOn = false;
  auto setAttr = [&](bool &state, bool on, Display::Attribute onAttr, Display::Attribute offAttr) {
    if (on == state) return;
    display_.setAttribute(on ? onAttr : offAttr);
    state = on;
  };
  auto setStandout = [&](bool on) {
    setAttr(standoutOn, on, Display::Attribute::Standout, Display::Attribute::StandoutOff);
  };
  // Line::attributes自身の太字/下線のバイト範囲(2026-08-05、自身の
  // コメント参照)――上のsetStandout()とは独立している(マーク/反転
  // 表示された行でも、他のどんな端末アプリがSGRコードを組み合わせ
  // るのと同様に、その下に自身の太字/下線のrunを運べる)。
  auto setBold = [&](bool on) { setAttr(boldOn, on, Display::Attribute::Bold, Display::Attribute::BoldOff); };
  auto setUnderline = [&](bool on) {
    setAttr(underlineOn, on, Display::Attribute::Underline, Display::Attribute::UnderlineOff);
  };
  // 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>で
  // 記載したいので対応よろしく")――setBold()/setUnderline()と全く
  // 同じ形。
  auto setItalic = [&](bool on) {
    setAttr(italicOn, on, Display::Attribute::Italic, Display::Attribute::ItalicOff);
  };
  bool serifOn = false;  // Display::Attribute::Serif参照
  auto setSerif = [&](bool on) { setAttr(serifOn, on, Display::Attribute::Serif, Display::Attribute::SerifOff); };
  bool largeOn = false, smallOn = false;  // Display::Attribute::Large/Small参照
  auto setLarge = [&](bool on) { setAttr(largeOn, on, Display::Attribute::Large, Display::Attribute::LargeOff); };
  auto setSmall = [&](bool on) { setAttr(smallOn, on, Display::Attribute::Small, Display::Attribute::SmallOff); };
  // Display::Attribute::Highlight自身の宣言コメント参照(2026-09-10
  // 追加、直接のユーザー依頼: "マッチ文字列をboldの代わりにハイ
  // ライトでやってみて")――setBold()/setItalic()と全く同じ形…
  // だったが、2026-09-19追記(Display::Attribute::HighlightCurrent
  // 自身のコメント参照)でHighlight("他の"マッチ)とHighlightCurrent
  // (現在位置のマッチ)の2つの"on"状態がHighlightOffという1つの"off"
  // を共有するようになったため、setAttr()自身の単純なbool版では
  // 足りない――3状態(Off/Regular/Current)を自前で追跡する。
  enum class HighlightState { Off, Regular, Current };
  HighlightState highlightState = HighlightState::Off;
  auto setHighlightState = [&](HighlightState want) {
    if (want == highlightState) return;
    switch (want) {
      case HighlightState::Off:
        display_.setAttribute(Display::Attribute::HighlightOff);
        break;
      case HighlightState::Regular:
        display_.setAttribute(Display::Attribute::Highlight);
        break;
      case HighlightState::Current:
        display_.setAttribute(Display::Attribute::HighlightCurrent);
        break;
    }
    highlightState = want;
  };
  // 2026-09-26追加(Display::Attribute::Color自身のコメント参照): ANSIの
  // 色エスケープ由来の色。setBold()等と同じく、変化した時だけ送る
  // (BoldOff/HighlightOffによる端末側のリセットからの復帰はDisplay側
  // ――TerminalDisplay::emitColor()――が受け持つ)。
  int colorFg = Display::kDefaultColor, colorBg = Display::kDefaultColor;
  auto setColor = [&](int fg, int bg) {
    if (fg == colorFg && bg == colorBg) return;
    display_.setColor(fg, bg);
    colorFg = fg;
    colorBg = bg;
  };

  // `markedNow`は、highlightStartが0の場合(よくあるケースで、
  // DirectoryModeのls一覧以外の全ての呼び出し元)にだけtrueで始まる
  // ――Line::highlightStartColumn自身のコメント参照。それは、下の
  // ループが列highlightStartに到達した時点で一度だけ切り替わる。
  // underlineMarkedNowは、(setMarkedRowUsesUnderline()自身のコメント
  // 参照)「別の」Underlineチャネルに対して、それを正確に反映する
  // ――実際にはDirectoryModeのls一覧(現在唯一のunderlineMarkedの
  // 呼び出し元)は常にhighlightStart > 0を持つので、これはfalseで
  // 始まり、markedNowと同様、下のhighlightStartを横切る地点でだけ
  // 切り替わる。highlightStart <= 0を持ち得る将来の呼び出し元の
  // ために、それでもここでは対称に保たれている。
  bool markedNow = marked && highlightStart <= 0;
  bool underlineMarkedNow = underlineMarked && highlightStart <= 0;
  setStandout(markedNow);
  setUnderline(underlineMarkedNow);
  int col = 0;
  // 2026-08-13の修正: 実際の報告(LOCATEの結果、スクリーンショット
  // =am/=an)――端末自身の幅(80桁)にちょうど着地する長い絶対パスが、
  // 反転表示ハイライトを視覚的に1行ずれさせ(行Nのハイライトが行
  // N+1に着地しているように見える)、「後続の」行のEnterが反応しな
  // くなった。PTYでの再現+一時的な計測により根本原因を特定: ContentView
  // 自身のcursorIndex_/topIndex_/markedの計算は、全ての段階で正しい
  // ことが確認された(記録され、モードライン自身の位置テキストに
  // 正確に一致した)――バグはその下流の、生の端末バイトの中にあった。
  // 端末の「最後の」列へ文字を書くことは、ほとんどの端末自身の自動
  // 折り返し(実際のカーソルが黙って次の行の先頭へ進む)を引き起こす
  // が、このクラス自身のエスケープシーケンス発行(この関数の末尾の
  // setStandout(false)等、下参照)はそれが起きたことを知らない――
  // それらの末尾の「属性を元に戻す」コードは、「この」行の「末尾」
  // ではなく「次の」行の「先頭」に適用されてしまう。非印字のSGR
  // コードはそれ自体折り返しを引き起こさないので、カーソルが既に
  // 黙って移動した先にそのまま着地するため。同じセッションで、
  // 長い(80桁、バグあり)と短い(80桁未満、バグなし)両方の実際の
  // `locate`結果で再現して確認済み。columns()-1で上限を設ける
  // (まさにこの理由による、標準的な端末プログラミングの慣習である、
  // 最後の1列を予約する)ことで折り返しを完全に回避する。legacy
  // 自身の他所での行切り詰めの前例(CLAUDE.mdの履歴項目5のStep5
  // 参照)に合わせている――これは、モードごとの回避策ではなく、
  // renderLine()の全呼び出し元が一度に恩恵を受ける、共有された、
  // 一元化された修正地点である。
  // これは、この呼び出しが「そもそも」Standoutをオンに切り替える
  // 可能性がある場合にだけ適用される(`marked`、呼び出し全体で
  // 固定。highlightStart経由で行の途中で切り替わり得る`markedNow`
  // ではない): プレーンなmarked=falseの行は決してsetStandout(true)
  // を呼ばないので、その末尾のリセットは、どんな自動折り返しに
  // 関わらず既に何もしておらず、それも切り詰めることは対応する
  // 修正の無い純粋な退行になる――handleResize()自身のFileモード
  // 再折り返しのテスト(そこではcursorHighlightEnabled_がfalse、
  // FileMode自身のクラスコメント参照)によって確認済み。それらは
  // 意図的に、フル幅で折り返された行がその最後の列を描画すること
  // に依存している。underlineMarkedもここに含まれる(markedだけで
  // はなく): この予約が回避するために存在する同じ自動折り返しの
  // 危険は、Standout自身のコードに限らず、端末の最後の列付近の
  // どんな属性を切り替えるエスケープシーケンスにも当てはまるため。
  int maxCol =
      (marked || underlineMarked) ? std::max(0, display_.columns() - 1) : display_.columns();
  // 中間省略記号の切り詰め(2026-08-14): 直接のユーザー報告――1つの
  // 長い共通の接頭辞を共有するLOCATE結果は、端末自身の幅で素朴に
  // 切り落とされると全て「同一の」可視テキストとして描画され、
  // 個々のエントリを区別できなくしていた("行末が同じで区別でき
  // ない")。macOS Finder自身の長いファイル名の慣習に合わせている
  // ("macOSのFinderと同じく行の中央を「...」として行末まで表示して
  // 欲しい"――「中間」を省略し、開始と終わりの両方を可視のまま
  // 保つ)。highlightStartColumn/attributesが「無い」行(プレーン
  // な、スタイル無しのテキスト)にスコープされている――これは、
  // この移植自身の結果一覧のLineが実際に使う唯一の形である
  // (LocateMode/DuMode等はどちらも決して設定しない): ハイライト
  // された「部分文字列」自身のバイトオフセットでインデックスされた
  // 属性範囲を、中間が省略された文字列を通して書き換えることは、
  // 本当に別の、より難しい問題であり、ここではそれを解決しようと
  // していない――Directory自身のls一覧のhighlightStartColumn、
  // GrepModeのマッチのハイライト等は、既存の単純な右切り詰め
  // (下のループ自身の`col >= maxCol`break)を変更せずに保つ。
  // 2026-08-14の修正: 下の省略記号の予算は、maxCol自身のmarked
  // 依存の値を直接再利用するのではなく、Standoutがこの「ビュー」
  // 上でそもそも切り替わり得る限り(cursorHighlightEnabled_)常に
  // columns()-1である――実際の報告("ハイライト時は「...bat
  // Uninstaller.app/...」だったのが「...obat Uninstaller.app/...」"、
  // ^Nを押して行から離れることで再現し、マウスで再度ホバーすると
  // 戻ることを確認)が、まさにこれを突き止めた: maxColは、省略記号
  // の切り詰めとは全く関係が無い理由(Standoutが切り替わる時にだけ
  // 重要になる、無関係な自動折り返しバグを避ける)でmarkedに依存
  // している(columns()-1対columns()、すぐ上の自身のコメント参照)。
  // 省略記号の切り詰めはmarked=trueの行と同じくらい頻繁にmarked=false
  // のプレーンな行に対しても実行される(LOCATE/DU結果はカーソルを
  // 常に通り過ぎてスクロールする)ので、maxColを再利用すると、
  // 「同じ」行自身の描画されたテキストが、その瞬間にたまたま
  // ハイライトされた行だったかどうかだけによって、目に見えて1文字
  // ずれてしまう――2つの独立に有効な描画ではなく、本物の、ユーザー
  // に見えるちらつきである。cursorHighlightEnabled_によってゲート
  // されている(無条件ではない): それがfalseの時、`marked`はカーソル
  // 位置に関わらず全ての行で常にfalseである(この関数自身の冒頭の
  // 行参照)ので、修正すべきちらつきは無く、maxColは既に完全な
  // columns()に等しい――これはまさに、「redraw: 端末とちょうど
  // 同じ幅のマークされていない行は依然としてその最後の列を描画
  // する」(そしてhandleResize()自身のFileモード再折り返しの
  // テスト)が意図的に依存しているフル幅の描画である。ここで
  // 無条件にcolumns()-1を強制すると、それを退行させてしまう。
  int ellipsisBudget = cursorHighlightEnabled_ ? std::max(0, display_.columns() - 1) : maxCol;
  // この幅を意識した歩み(2026-08-19の修正、2026-08-21と2026-08-26
  // に広げられた――なぜこれがもはや全くhighlightStartColumnに
  // ゲートされていないのかについては下のこのループ自身の2026-08-26
  // のコメント参照)は、今や全てのLineに一様に適用される: LOCATE/
  // DU/Archiveの結果、DirectoryModeのls一覧、通常のFileModeの内容、
  // その全てが、各UTF-8シーケンスを(Utf8::sequence_length()経由で)
  // 1単位として消費し、それ自身のUtf8::display_width()分だけ進む、
  // 本物の画面列数として`col`を歩く。この部分は結果一覧のLineに
  // 限定されない: FileMode自身の内容はPipeBuffer::wrapLine()から
  // 到着する。それは既に全ての行を、ちょうど`width_`「画面」列に
  // 収まるよう事前に折り返している――CJK重めのテキストに対しては、
  // それは日常的に`width_`「バイト」をはるかに超える(2列の文字は
  // 3バイトかかる)ので、プレーンなバイトを数えるループは以前、
  // それを本来の(既に幅として正しい)末尾よりはるかに手前で切って
  // しまい、さらに悪いことに、文字の途中で止まり、切り詰められた
  // UTF-8シーケンスをそのまま端末へ送ってしまうことがあった(実際
  // の報告+生バイトのキャプチャ: 単独の先導バイト0xe3の直後に
  // カーソル移動のエスケープが続き、継続バイトが全く無い)。元々の
  // 2026-08-19の修正は、ハイライトが無く「属性も」無い行に対して
  // だけゲートを広げ、FileMode自身の太字/下線(nroffオーバー
  // ストライク、例えばHELP.jpの節見出し)の行を古いバイトを数える
  // ループのままにしていた――まさに同じ種類のバグが、CJK重めの
  // 太字/下線付きの行に対してそこに生き残っており、実際の報告
  // (2026-08-21、-gモード、HELP第4章のスクリーンショット=eb/=ea)
  // まで発見されなかった: ドラッグ中の描画(別のコードパス、
  // renderScrolled()自身のコメント参照)ではそれは決して見えな
  // かったが、解放時に通常のflush()/commitGlyph()パイプラインが
  // 再び実行された瞬間、(moveCursor()が行の間でそれをリセットしな
  // いので)バイトを数えるループの文字途中での中断から宙ぶらりんの
  // 部分シーケンスを保持したままだったGraphicalDisplay::putByte()
  // 自身のpendingUtf8バッファが、その古びた残りのバイトへ「次の」
  // 行の最初のバイトを黙って継ぎ足し、「後続」行の列0に1つのゴミ
  // グリフを確定していた(=eaの小さな黒い四角/ハッチングされた
  // 箱の跡、常に折り返された継続行の上、つまりまさに、段落の他所
  // で文字の途中で1行を終えたばかりである可能性が最も高い行)。
  // FileModeが`line.attributes`の唯一の呼び出し元である(確認済み:
  // 他のモードは決してそれを設定せず、highlightStartColumnと一緒に
  // 設定されることも決してない――Line::attributes自身のコメント
  // 参照)ので、それをこの同じ幅を意識した歩みに折り込む(属性の
  // マッチングは今やループ自身のバイトインデックス`idx`をキーと
  // する。AttributeRunが既に使っているのと同じ慣習)ことは無条件に
  // 安全である: FileModeの属性付きの行は、プレーンなFileModeの行が
  // 既に受けている、文字として安全な、既に幅として正しい扱いを
  // 全く同じように受ける。
  //
  // ellipsisEligible(2026-08-20の追加、上と同じ報告――
  // docs/HISTORY.md自身の長い行が、特に-gでその「中間」が"..."に
  // 置き換わっているのを示していた一方、GraphicalDisplayの別の
  // 慣性スクロール描画経路は同じ行を完全に表示していた): 下の
  // Finderスタイルの中間省略記号の「置き換え」は、幅を意識した
  // 歩み自体とは別の、より狭い関心事である――LOCATE/DU/Archive
  // 自身の離散的な、折り返されていないname/path Line(この機能が
  // 構築された元々の2026-08-14の報告)に対しては適切だが、中間を
  // 省略することが、人為的な「両端を保つ」という切り方ではなく、
  // 読者がスクロールして見つけることを期待している実際の情報を
  // 破壊してしまう、通常の連続した散文/ログの内容に対しては誤り
  // である。LocateMode/DuMode/ArchiveMode自身の結果一覧のLine構築
  // だけがこれをtrueに設定する。FileMode自身の(既に幅で折り返
  // された)行は、下の幅を意識した歩みへ変更されずに落ち、それは
  // 単にmaxColで止まる――省略ではなく、文字として安全なプレーンな
  // 切り落としである。
  //
  // 2026-08-26の修正、実際の報告(ArchiveMode自身のutf8.tar
  // フィクスチャ: "utf8.tarの一覧表示が画面の2/3ほどで途切れる"):
  // これは以前`bool widthAware =
  // !line.highlightStartColumn.has_value();`だったので、
  // ellipsisEligible(この関数自身の幅を意識したCJK安全な切り詰め、
  // 自身のコメント参照)と、同日のクリックして選択の修正時点での
  // highlightStartColumn(DirectoryMode自身の名前のみハイライトの
  // 慣習)の「両方を」設定するArchiveModeのエントリは、代わりに
  // 画面幅という概念を全く持たない、古い生のバイトを数える分岐に
  // 落ちてしまっていた: CJKファイル名(3バイト/2列)は、端末自身の
  // 実際の列幅のおおよそ2/3で約79「バイト」の予算に達し、まさに
  // この同じ幅を意識した経路が元々修正するために構築された
  // "内容一覧は^Olでも2/3くらいまでしか表示されていない"バグ
  // (上の自身のコメント参照)を、単に「新しい」呼び出し元に対して
  // 再現していた。DirectoryModeのls一覧/WORK/arc.Nバナー(他の
  // 唯一2つのhighlightStartColumnの設定者、Line::
  // highlightStartColumn自身のコメント参照)は、決してellipsisEligible
  // が設定されていなかったので、それら自身は古い切り詰めのバグに
  // 一度も当たらなかった――しかし、それら自身のハイライト開始の
  // バイトオフセット(常にプレーンなASCIIの接頭辞、例えば名前の
  // 前の「  1234  」)は、別のバイトを数える歩みを全く必要とする
  // 代わりに、この「同じ」ループの既存のバイトインデックス`idx`
  // (下)に折り込まれるので、もはや古い分岐を必要とする実際の
  // 呼び出し元は残っていない――到達不能なデッドコードとして残す
  // のではなく削除された。
  std::string ellipsisText;
  const std::string *renderedText = &line.text;
  // 2026-09-11追加、EllipsisBoundaries自身の宣言コメント参照:
  // 切り詰めが実際に起きた場合、line.attributesのバイトオフセット
  // (常に切り詰め前のline.text基準)を、renderedTextの座標系へ作り
  // 直したこのローカルコピーへ差し替える――さもないとGREPの
  // Highlight属性等の位置がずれる/消える(実際のユーザー報告、実機
  // スクリーンショット=gf参照)。
  std::vector<AttributeRun> remappedAttributes;
  const std::vector<AttributeRun> *renderedAttributes = &line.attributes;
  if (line.ellipsisEligible && static_cast<int>(line.text.size()) > ellipsisBudget) {
    EllipsisBoundaries b = computeEllipsisBoundaries(line.text, ellipsisBudget);
    if (b.truncated) {
      static const std::string kEllipsis = "...";
      ellipsisText = line.text.substr(0, static_cast<std::size_t>(b.headEnd)) + kEllipsis +
                     line.text.substr(static_cast<std::size_t>(b.tailStart));
      renderedText = &ellipsisText;
      if (!line.attributes.empty()) {
        for (const AttributeRun &run : line.attributes) {
          AttributeRun remapped = run;
          remapped.start = remapByteOffsetThroughEllipsis(run.start, b, /*isEnd=*/false);
          remapped.end = remapByteOffsetThroughEllipsis(run.end, b, /*isEnd=*/true);
          if (remapped.end > remapped.start) remappedAttributes.push_back(remapped);
        }
        renderedAttributes = &remappedAttributes;
      }
    } else {
      ellipsisText = middleEllipsisTruncateByWidth(line.text, ellipsisBudget);
      renderedText = &ellipsisText;
    }
  } else if (line.highlightStartColumn.has_value() && static_cast<int>(line.text.size()) > ellipsisBudget) {
    // 2026-09-03追加、直接のユーザー報告("/Users/haa15030/（ls）画面に
    // 収まらないファイル名...を Screensho...5-15-13.png で表示
    // よろしく"): DirectoryModeのls一覧/WORK/arc.Nバナー(常に
    // highlightStartColumnを持つが、上のellipsisEligibleは決して
    // 設定しない――自身のコメント参照)に、Finderスタイルの中間省略を
    // 「ファイル名部分だけ」に適用する。truncateHighlightedSuffix()
    // 自身のコメント参照(なぜellipsisEligible全体版をそのまま使えない
    // のか)。
    int highlightStart = std::max(0, *line.highlightStartColumn);
    EllipsisBoundaries b = computeHighlightedSuffixBoundaries(line.text, highlightStart, ellipsisBudget);
    ellipsisText = truncateHighlightedSuffix(line.text, highlightStart, ellipsisBudget);
    renderedText = &ellipsisText;
    if (b.truncated && !line.attributes.empty()) {
      for (const AttributeRun &run : line.attributes) {
        AttributeRun remapped = run;
        remapped.start = remapByteOffsetThroughEllipsis(run.start, b, /*isEnd=*/false);
        remapped.end = remapByteOffsetThroughEllipsis(run.end, b, /*isEnd=*/true);
        if (remapped.end > remapped.start) remappedAttributes.push_back(remapped);
      }
      renderedAttributes = &remappedAttributes;
    }
  }
  {
    const std::string &text = *renderedText;
    // 2026-09-30追加、直接のユーザー依頼("-gでのルビを実装してみて"): ルビを親字に添えて
    // 描ける表示(-g)では、読み(RubyText)をマスに書かず、親字(RubyBase)の最初の字の前に
    // setPendingRuby()で渡す。端末では「燈《ひ》」のまま書く。
    bool rubyOn = display_.drawsRuby();
    auto inRun = [&](std::size_t at, Display::Attribute attribute) -> const AttributeRun * {
      for (const AttributeRun &run : *renderedAttributes)
        if (run.attribute == attribute && static_cast<int>(at) >= run.start && static_cast<int>(at) < run.end)
          return &run;
      return nullptr;
    };
    auto rubyAt = [&](std::size_t at) {
      for (const auto &mark : line.rubyMarks)
        if (mark.byte == static_cast<int>(at)) display_.setPendingRuby(mark.reading, mark.span, mark.vertical);
      const AttributeRun *base = inRun(at, Display::Attribute::RubyBase);
      if (!base || base->start != static_cast<int>(at)) return;
      // 親字のランは字ごとに積まれているので、続くランをつないで親字全体と読みを求める
      int baseEnd = base->end;
      for (bool extended = true; extended;) {
        extended = false;
        if (const AttributeRun *next = inRun(static_cast<std::size_t>(baseEnd), Display::Attribute::RubyBase)) {
          baseEnd = next->end;
          extended = true;
        }
      }
      int readingEnd = baseEnd;
      while (const AttributeRun *next = inRun(static_cast<std::size_t>(readingEnd), Display::Attribute::RubyText))
        readingEnd = next->end;
      if (readingEnd <= baseEnd) return;
      std::string reading = text.substr(static_cast<std::size_t>(baseEnd), static_cast<std::size_t>(readingEnd - baseEnd));
      for (const char *bracket : {"\xE3\x80\x8A", "\xE3\x80\x8B"})  // 《》
        for (std::size_t p; (p = reading.find(bracket)) != std::string::npos;) reading.erase(p, 3);
      int width = 0;
      for (std::size_t p = at; p < static_cast<std::size_t>(baseEnd);) {
        int l = std::max(1, Utf8::sequence_length(static_cast<unsigned char>(text[p])));
        width += Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + p, l);
        p += static_cast<std::size_t>(l);
      }
      if (!reading.empty()) display_.setPendingRuby(reading, std::max(width, 1), false);
    };
    std::size_t idx = 0;
    while (idx < text.size()) {
      if (rubyOn && (!line.rubyMarks.empty() || !renderedAttributes->empty())) {
        if (inRun(idx, Display::Attribute::RubyText)) {
          idx += static_cast<std::size_t>(std::max(1, Utf8::sequence_length(static_cast<unsigned char>(text[idx]))));
          continue;
        }
        rubyAt(idx);
      } else if (!rubyOn && inRun(idx, Display::Attribute::RubyText) && text.compare(idx, 3, "\xE3\x80\x8A") == 0) {
        // 傍点(isEmphasisMarkReading())の「《﹅》」は端末では書かない
        std::size_t close = text.find("\xE3\x80\x8B", idx + 3);
        if (close != std::string::npos && isEmphasisMarkReading(text.substr(idx + 3, close - idx - 3))) {
          idx = close + 3;
          continue;
        }
      }
      // highlightStartColumn自身の遷移地点(DirectoryModeのls一覧/
      // WORK/arc.Nバナー、この関数自身の上のコメント参照)――
      // `highlightStart`は、実際の全呼び出し元に対して常にプレーン
      // なASCIIの接頭辞へのバイトオフセットなので、それをこの
      // ループ自身の実行中のバイトオフセット`idx`と直接比較する
      // ことは正確であり、ここでは画面幅への変換は不要である。
      if (line.highlightStartColumn.has_value() && static_cast<int>(idx) == highlightStart) {
        markedNow = marked;
        underlineMarkedNow = underlineMarked;
        setStandout(markedNow);
        setUnderline(underlineMarkedNow);
      }
      if (line.highlightEndColumn.has_value() && static_cast<int>(idx) == *line.highlightEndColumn) {
        markedNow = false;  // Line::highlightEndColumn参照
        underlineMarkedNow = false;
        setStandout(false);
        setUnderline(false);
      }
      unsigned char c = static_cast<unsigned char>(text[idx]);
      int len = Utf8::sequence_length(c);
      len = std::min(len, static_cast<int>(text.size() - idx));
      // 太字/下線のマッチング、2026-08-21にここへ折り込まれた
      // (この関数自身のwidthAwareのコメント参照)――ループ自身の
      // 実行中の「バイト」オフセットである`idx`をキーとしており、
      // AttributeRun{start,end}のバイトオフセットの慣習に正確に
      // 一致する(この分岐では画面列数であり、CJK重めの内容に対
      // しては位置がずれてしまう`col`とは異なる)。
      if (!renderedAttributes->empty()) {
        bool wantBold = false, wantUnderline = false, wantStandout = false, wantItalic = false,
             wantHighlight = false, wantHighlightCurrent = false, wantSerif = false, wantLarge = false,
             wantSmall = false;
        int wantFg = Display::kDefaultColor, wantBg = Display::kDefaultColor;
        for (const AttributeRun &run : *renderedAttributes) {
          if (static_cast<int>(idx) < run.start || static_cast<int>(idx) >= run.end) continue;
          if (run.attribute == Display::Attribute::Serif) wantSerif = true;
          else if (run.attribute == Display::Attribute::Large) wantLarge = true;
          else if (run.attribute == Display::Attribute::Small) wantSmall = true;
          else if (run.attribute == Display::Attribute::Bold) wantBold = true;
          else if (run.attribute == Display::Attribute::Underline) wantUnderline = true;
          else if (run.attribute == Display::Attribute::Standout) wantStandout = true;
          else if (run.attribute == Display::Attribute::Italic) wantItalic = true;
          else if (run.attribute == Display::Attribute::Highlight) wantHighlight = true;
          else if (run.attribute == Display::Attribute::HighlightCurrent) wantHighlightCurrent = true;
          else if (run.attribute == Display::Attribute::Color) {
            wantFg = run.fg;
            wantBg = run.bg;
          }
        }
        setColor(wantFg, wantBg);
        setBold(wantBold);
        setUnderline(wantUnderline);
        setItalic(wantItalic);
        setSerif(wantSerif);
        setLarge(wantLarge);
        setSmall(wantSmall);
        setHighlightState(wantHighlightCurrent  ? HighlightState::Current
                           : wantHighlight       ? HighlightState::Regular
                                                  : HighlightState::Off);
        // 2026-08-29の追加(CopyMode自身の選択ハイライト、
        // copy_mode.hpp参照): この行におけるStandoutの唯一「他の」
        // 源はmarkedNow(highlightStartColumnの、「ここから行末まで」
        // という制限のない慣習で、上の遷移チェックによって既にこの
        // 同じidxに対して更新済み)である――それらをORすることで、
        // 有界なAttributeRunの範囲と行全体のマークが、互いを壊す
        // ことなく共存できる。既存の呼び出し元でline.attributesと
        // highlightStartColumnを同じLineに両方設定するものは無く
        // (上で確認済み: FileModeがattributes_の唯一の設定者であり、
        // 決してhighlightStartColumnを設定しない)、既存の呼び出し
        // 元でStandout型のAttributeRunを使うものも全く無いので、
        // この追加より前の全てのLineに対しては、ここでwantStandout
        // は常にfalseであり、これは正確に以前の何もしない挙動に
        // 帰着する。
        setStandout(markedNow || wantStandout);
      }
      if (len <= 1) {
        // 1バイト(ASCII範囲)の単位は常にちょうど画面1列幅なので、
        // ここでは`col`はいずれにせよ1ずつ進む。
        if (col >= maxCol) break;
        // 2026-09-05変更、直接のユーザー依頼("行末にtabコードがある
        // 行をCUT一覧に載せたい"): PipeBuffer::wrapLine()は、今は
        // 行末(改行の直前)のタブだけを生のバイトのまま残す(自身の
        // コメント参照)――そのタブがCUT/GREPで検索でき、一覧でも
        // 見える必要がある。一方DuMode::formatEntryLine()("size\tpath"、
        // PipeBufferを一切経由せずLineへ直接タブを埋め込む唯一の
        // 呼び出し元)は依然として行の「途中」にタブを置くので、
        // こちらは元通りスペースとして描画すべき――ここを無条件に
        // 変えるとDU一覧のサイズ/パスの間に目障りな反転表示グリフが
        // 割り込んでしまう(2026-08-02の元のバグの、DU版の再発)。
        // このLineの「本当に最後のバイト」かどうかで両者を区別する
        // ――行末のタブだけがtrue になる。
        bool trailingTab = c == '\t' && idx + 1 == text.size();
        if (c == '\t' && !trailingTab) {
          display_.putChar(' ');
        } else if (c && (c < ' ' || c == 0x7f)) {
          setStandout(!markedNow);
          int shown = (c == 0x7f) ? c - 0x80 : c;
          display_.putChar(shown + '@');
          setStandout(markedNow);
        } else {
          display_.putChar(c);
        }
        col += 1;
        idx += 1;
      } else {
        int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + idx, len);
        // 幅広(2列)の文字が、予約された本当の最後の列の境界を
        // またぐことを決して許さない――ここでは`col >= maxCol`だけ
        // では不十分である。maxCol-1で始まる2幅の文字はそのチェック
        // を通過してしまうが、ちょうどmaxColで終わってしまい、
        // まさにrenderLine()自身のmaxColの予約(上の自身のコメント
        // 参照)が避けるために存在する自動折り返しの危険そのもの
        // になる。
        //
        // 2026-09-05、直接のユーザー報告("禁則処理が利いていない
        // のと、一文字欠落がある")を調査して、当初はここに例外を
        // 設けて対処しようとした(PipeBuffer::wrapLine()自身の禁則の
        // 「追い出し」が行の幅を最大2カラム超過させることを承知の
        // 上で許容していたため)――しかしGraphicalDisplay自身の
        // セル格子はcolumns()ぴったりの大きさしか無く、この超過分は
        // -gではどのみち描画され得ないと判明した。真の修正は
        // PipeBuffer::wrapLine()側(直前の普通の文字を次行へ戻して
        // から禁則対象の文字を続ける、本来の日本語組版の「追い出し」
        // ――pipe_buffer.cpp自身のpopLastCharForKinsoku()参照)にあり、
        // それによって行がwidth_を超えることはもう無いはずなので、
        // ここは元通り無条件のクリップのままにしてある。
        if (col + std::max(width, 0) > maxCol) break;
        // 2026-09-21、直接のユーザー報告(Terminal.app 80x24、実機で確認): タブ展開の内部マーカー
        // U+2060(WORD JOINER、幅0、PipeBuffer::wrapLine()自身のコメント参照)は、iTerm2では
        // 不可視だがTerminal.appは1セル分として数えるらしく、タブを含む80桁ぴったりの行が
        // 溢れて折り返し、表示が崩れた。内部の幅計算は幅0のままなので、画面へは書き出さない
        // (検索用の内部文字列wrapText_/Line::textは変えない)。
        bool isWordJoiner = len == 3 && static_cast<unsigned char>(text[idx]) == 0xE2 &&
                            static_cast<unsigned char>(text[idx + 1]) == 0x81 &&
                            static_cast<unsigned char>(text[idx + 2]) == 0xA0;
        if (!isWordJoiner)
          for (int b = 0; b < len; b++) display_.putChar(static_cast<unsigned char>(text[idx + static_cast<std::size_t>(b)]));
        col += width;
        idx += static_cast<std::size_t>(len);
      }
    }
    // highlightToRowEnd_(setHighlightToRowEnd()参照): 反転表示の行を、文字の後ろも画面の右端まで反転する。
    if (marked && highlightToRowEnd_ && !line.highlightEndColumn) {
      setBold(false);
      setUnderline(false);
      setItalic(false);
      setStandout(true);
      for (; col < maxCol; col++) display_.putChar(' ');
    }
    setStandout(false);
    setBold(false);
    setUnderline(false);
    setItalic(false);
    setSerif(false);
    setLarge(false);
    setSmall(false);
    setHighlightState(HighlightState::Off);
    setColor(Display::kDefaultColor, Display::kDefaultColor);
  }
}

std::string ContentView::modeLineText() const {
  int total = static_cast<int>(lines_.size());
  int current = total > 0 ? cursorIndex_ + 1 : 0;
  applyLivePromptLineAdjustment(total, current);
  applyRawLineNumbers(current, total);
  return composeModeLineRow(positionTextOverride_ ? *positionTextOverride_
                             : useFixedWidthPosition_ ? ModeLine::formatFixedWidth(current, total)
                                                       : ModeLine::format(current, total));
}

std::string ContentView::naturalPositionText() const {
  int total = static_cast<int>(lines_.size());
  int current = total > 0 ? cursorIndex_ + 1 : 0;
  applyLivePromptLineAdjustment(total, current);
  applyRawLineNumbers(current, total);
  return useFixedWidthPosition_ ? ModeLine::formatFixedWidth(current, total) : ModeLine::format(current, total);
}

std::string ContentView::scrollModeLineText() const {
  int total = static_cast<int>(lines_.size());
  int bottomVisible = std::min(topIndex_ + std::max(height_, 0), total);
  int current;
  if (total <= 0) {
    current = 0;
  } else if (bottomVisible >= total && !contentMayGrow_) {
    // 末尾まで(または惰性で)スクロールされた――topIndex_自体は
    // total-height_で頭打ちになるので、modeLineText()でcursorIndex_
    // ができるように自力で`total`に到達することは決してできない。
    // 代わりにbottomVisibleをチェックすることが、末尾まで惰性で
    // 行くとENDを表示するようにするものである。プレーンな'j'/'s'
    // ナビゲーション(modeLineText())が正しくENDに到達していたのに
    // 対し、そこでパーセンテージのまま止まっていたという実際の
    // 報告に合わせている。
    // !contentMayGrow_(2026-09-07追加): setContentMayGrow()自身の
    // 宣言コメント参照――生きたページャーストリームがまだ続き得る
    // 間は、たまたま今読み込めている行数がbottomVisibleに一致する
    // 瞬間があっても、それは「本当の末尾」ではなく「先読みがまだ
    // 追いついていないだけ」の可能性がある。これを見ずに末尾判定
    // すると、GUIの毎フレーム描画のような高頻度の呼び出し元では
    // 一瞬だけ誤ってENDと表示してしまう(scrollModeLineSegments()
    // 自身のコメント参照)。
    current = total;
  } else {
    current = topIndex_ + 1;  // ここでtopIndex_==0は自然にTOPを表示する
  }
  applyLivePromptLineAdjustment(total, current);
  applyRawLineNumbersAt(current, total);
  return composeModeLineRow(positionTextOverride_ ? *positionTextOverride_
                             : useFixedWidthPosition_ ? ModeLine::formatFixedWidth(current, total)
                                                       : ModeLine::format(current, total));
}

std::vector<Display::ModeLineSegment> ContentView::scrollModeLineSegments(bool trackCursorPosition) const {
  int total = static_cast<int>(lines_.size());
  int bottomVisible = std::min(topIndex_ + std::max(height_, 0), total);
  int current;
  if (total <= 0) {
    current = 0;
  } else if (total <= height_) {
    // 2026-08-30、直接のユーザー報告(GUI、実際のマウスドラッグ中に
    // 「14/14 END」のような誤った位置表示になる件、ARCモードで再現):
    // 全内容が既に画面に収まっている場合、topIndex_は常に0で
    // bottomVisibleは常にtotal以上になるため、下のbottomVisible>=total
    // 分岐が想定する「末尾までスクロール/惰性で到達した」という前提が
    // そもそも成立しない(スクロール自体が一切起こり得ない)。この場合は
    // 代わりに、このフレームの直前/直後の通常描画(modeLineText())と
    // 一致するよう、cursorIndex_基準で表示する。
    current = std::min(total, cursorIndex_ + 1);
  } else if (trackCursorPosition) {
    // trackCursorPosition自身の宣言コメント参照(2026-09-07追加):
    // cursorIndex_はGUIのコースト/ドラッグの全経路で継続的に追従
    // させ続けられているので、これを直接使う――topIndex_はスクロール
    // 開始直後(まだ1行もクロスしていない間)はまだ古い値のままなので、
    // そちらを使うと選択中の行(例: 24行目)ではなく常に「00001」から
    // 始まってしまっていた。
    current = cursorIndex_ + 1;
    // contentMayGrow_(6fd4c36で新設)も同じ理由でここに適用する:
    // 生きたページャーストリームがまだ続き得る間、cursorIndex_が
    // 「今読み込めている最後の行」にたまたま追いついてcurrent==total
    // になっても、それは本当の末尾ではなく先読みが追いついていない
    // だけの可能性がある――そのままだとModeLine::format()が一瞬だけ
    // 誤ってENDと表示してしまう。
    if (contentMayGrow_ && current >= total) current = total - 1;
  } else if (bottomVisible >= total && !contentMayGrow_) {
    // なぜこのチェックであってtopIndex_自体ではないのかについては
    // scrollModeLineText()自身のコメント参照。!contentMayGrow_も
    // 同じ関数の2026-09-07追加分のコメント参照――GUIのコースト/
    // ドラッグは毎フレームここを呼ぶため、この関数こそがまさに
    // その「一瞬のEND誤表示」が実際に観測されていた箇所そのもの。
    current = total;
  } else {
    current = topIndex_ + 1;
  }
  applyLivePromptLineAdjustment(total, current);
  applyRawLineNumbersAt(current, total);
  std::string positionText = positionTextOverride_ ? *positionTextOverride_
                              : useFixedWidthPosition_ ? ModeLine::formatFixedWidth(current, total)
                                                        : ModeLine::format(current, total);

  std::vector<Display::ModeLineSegment> segments;
  if (!modeLineFieldsProvider_) {
    segments.push_back({positionText, true});
    return segments;
  }
  ModeLineFields fields = withTruncatedTrailing3(
      withTruncatedFields(providedModeLineFields(), positionText), positionText);
  int cols = display_.columns();

  // 2026-08-30、直接のユーザー報告(GUI、実際のマウスドラッグ中は
  // モードライン末尾のフィールド――ARCの場合アーカイブパス全体――が
  // 消える件): renderModeLine()自身のsplitLayoutWhenCrowded分岐
  // (renderSplitModeLine()参照)をここでも同じ考え方で反映する――
  // 以前はここが常に下のfitModeLineFields()の「収まらなければ
  // 丸ごと落とす」という単純な階層だけを使っていた。renderSplitMode
  // Line()自身は絶対カーソル移動で直接描画するため、その場所計算
  // (center/leftBudget/rightBudget/centerStart)だけを移植し、
  // ModeLineSegmentは並び順でしか位置を表現できないので、その間隔を
  // 非standoutのスペースセグメントとして表現する。
  if (fields.splitLayoutWhenCrowded) {
    int naiveLen = static_cast<int>(fields.name.size()) + 1 + static_cast<int>(positionText.size()) +
                   (fields.trailing.empty() ? 0 : 1 + static_cast<int>(fields.trailing.size())) +
                   (fields.trailing2.empty() ? 0 : 1 + static_cast<int>(fields.trailing2.size()));
    if (cols > 0 && naiveLen > cols) {
      std::string center = positionText;
      if (!fields.trailing.empty()) center += " " + fields.trailing;
      int centerLen = static_cast<int>(center.size());
      int available = std::max(0, cols - centerLen - 2);
      int nameWidth = totalDisplayWidth(fields.name);
      int trailing2Width = totalDisplayWidth(fields.trailing2);
      int leftBudget, rightBudget;
      if (nameWidth <= available / 2) {
        leftBudget = nameWidth;
        rightBudget = available - leftBudget;
      } else if (trailing2Width <= available / 2) {
        rightBudget = trailing2Width;
        leftBudget = available - rightBudget;
      } else {
        leftBudget = available / 2;
        rightBudget = available - leftBudget;
      }
      int centerStart = leftBudget + 1;
      std::string name = middleEllipsisTruncateByWidth(fields.name, leftBudget);
      std::string trailing2 = middleEllipsisTruncateByWidth(fields.trailing2, rightBudget);

      int col = 0;
      if (!name.empty()) {
        segments.push_back({name, true});
        col += totalDisplayWidth(name);
      }
      if (centerStart > col) segments.push_back({std::string(static_cast<std::size_t>(centerStart - col), ' '), false});
      col = std::max(col, centerStart);
      segments.push_back({positionText, true});
      col += static_cast<int>(positionText.size());
      if (!fields.trailing.empty()) {
        segments.push_back({" ", false});
        col += 1;
        segments.push_back({fields.trailing, true});
        col += static_cast<int>(fields.trailing.size());
      }
      if (!trailing2.empty()) {
        int trailing2Start = std::max(0, cols - totalDisplayWidth(trailing2));
        if (trailing2Start > col) segments.push_back({std::string(static_cast<std::size_t>(trailing2Start - col), ' '), false});
        segments.push_back({trailing2, true});
      }
      return segments;
    }
  }

  FieldFit fit = fitModeLineFields(fields, positionText);
  int drawnLen = 0;
  if (fit.showName && !fields.name.empty()) {
    segments.push_back({fields.name, true});
    segments.push_back({" ", false});
    drawnLen += totalDisplayWidth(fields.name) + 1;
  }
  segments.push_back({positionText, true});
  drawnLen += totalDisplayWidth(positionText);
  // 2026-08-11の修正: renderModeLine()自身の同一の修正参照――各
  // フィールド自身の空かどうかは今やそれ自身だけをゲートするので、
  // trailingが空だからといってtrailing2がもはや黙って消えなく
  // なった。
  if (fit.showTrailing && !fields.trailing.empty()) {
    segments.push_back({" ", false});
    drawnLen += 1;
    std::string trailing = fields.trailing;
    // renderModeLine()自身の同一のtrailing2処理参照――端へのパディ
    // ングは、存在する場合はtrailing自体ではなくtrailing2(最後の
    // セグメント)に適用される。
    if (fields.fillTrailingToEdge && fields.trailing2.empty()) {
      // renderModeLine()自身の同一のパディング参照――平坦な文字列
      // バージョンと同様、それを望むモードにおいて、ハイライトが
      // 画面の右端に到達するのを保つ。
      int remaining = cols - drawnLen - totalDisplayWidth(trailing);
      if (remaining > 0) trailing += std::string(remaining, ' ');
    }
    segments.push_back({trailing, !fields.trailingPlain});  // ModeLineFields::trailingPlain自身のコメント参照
    drawnLen += totalDisplayWidth(trailing);
  }
  if (fit.showTrailing2 && !fields.trailing2.empty()) {
    segments.push_back({" ", false});
    drawnLen += 1;
    std::string trailing2 = fields.trailing2;
    // 2026-08-30、直接のユーザー報告(GUI、実際のドラッグ中はARC自身の
    // アーカイブパス(trailing3)が丸ごと消える件、renderModeLine()自身の
    // 同一のtrailing3ゲート参照): trailing3が存在する場合は、この端への
    // パディングをtrailing3自身に譲る――そうしないとtrailing2がここで
    // 行の残り全てを食べてしまい、trailing3の描画余地が無くなる。
    if (fields.fillTrailingToEdge && fields.trailing3.empty()) {
      int remaining = cols - drawnLen - totalDisplayWidth(trailing2);
      if (remaining > 0) trailing2 += std::string(remaining, ' ');
    }
    segments.push_back({trailing2, !fields.trailing2Plain});  // ModeLineFields::trailing2Plain自身のコメント参照
    drawnLen += totalDisplayWidth(trailing2);
  }
  // 2026-08-30、直接のユーザー報告(GUI、実際のドラッグ中はモードライン
  // の戻り値(ARCのアーカイブパス等)が表示されない件): 以前はここに
  // trailing3自体の描画が丸ごと欠けていた――renderModeLine()自身の
  // ModeLineFields::trailing3向けの処理(自身のコメント参照)と同じ
  // ロジックをここに移植する: 右揃え/切り詰めのオプションは無く、単に
  // 「余地があれば自身のプレーンなギャップの後に表示し、そうでなければ
  // 落とす」。
  if (!fields.trailing3.empty()) {
    std::string trailing3 = fields.trailing3;
    int candidateLen = 1 + totalDisplayWidth(trailing3);
    if (cols > 0 && drawnLen + candidateLen <= cols) {
      segments.push_back({" ", false});
      drawnLen += 1;
      if (fields.fillTrailingToEdge) {
        int remaining = (cols - 1) - drawnLen - totalDisplayWidth(trailing3);
        if (remaining > 0) trailing3 += std::string(remaining, ' ');
      }
      segments.push_back({trailing3, true});
      drawnLen += totalDisplayWidth(trailing3);
    }
  }
  // 2026-09-03追加、直接のユーザー報告("右下透かしも消えたままなので
  // 反映よろしく" -- ドラッグ/慣性スクロール中は右下の「usilic
  // v1.5b」透かしが消えたままな件): renderModeLine()自身の
  // rightWatermark処理(自身のコメント、"disp_window()自身の
  // フォールバックからの移植"参照)をここにも移植する――同じ条件
  // (収まる場合のみ)、同じ見た目(反転ではなく太字、右揃え)。
  // segments自体は連結でしか位置を表現できないので、moveCursor()の
  // 代わりに、必要な余白を非standout/非boldのプレーンな空白セグメント
  // として先に挿入してからwatermark自身のセグメントを続ける。
  if (!fields.rightWatermark.empty()) {
    std::string watermark = " " + fields.rightWatermark + " ";
    int watermarkLen = static_cast<int>(watermark.size());
    if (cols > 0 && drawnLen + watermarkLen <= cols) {
      int gap = cols - watermarkLen - drawnLen;
      if (gap > 0) segments.push_back({std::string(static_cast<std::size_t>(gap), ' '), false});
      Display::ModeLineSegment watermarkSegment;
      watermarkSegment.text = watermark;
      watermarkSegment.bold = true;
      segments.push_back(watermarkSegment);
    }
  }
  return segments;
}

std::string &ContentView::modeNameOverride() {
  static std::string name;
  return name;
}

std::string &ContentView::programWatermark() {
  static std::string watermark = "usilic-1.5b";
  return watermark;
}

// modeLineFieldsProvider_の結果に、modeNameOverride()(^X(〜^X)の"MACRO")を当てたもの。
ContentView::ModeLineFields ContentView::providedModeLineFields() const {
  ModeLineFields fields = modeLineFieldsProvider_(cursorIndex_);
  if (!modeNameOverride().empty()) fields.name = modeNameOverride();
  return fields;
}

void ContentView::setModeLineFieldsProvider(ModeLineFieldsProvider provider) {
  modeLineFieldsProvider_ = std::move(provider);
}

ContentView::ModeLineFields ContentView::currentModeLineFields() const {
  return modeLineFieldsProvider_ ? modeLineFieldsProvider_(cursorIndex_) : ModeLineFields{};
}

void ContentView::setUseFixedWidthPosition(bool enabled) { useFixedWidthPosition_ = enabled; }
void ContentView::setCountLivePromptAsLine(bool enabled) { countLivePromptAsLine_ = enabled; }
void ContentView::setAtLivePromptLine(bool atLivePromptLine) { atLivePromptLine_ = atLivePromptLine; }

void ContentView::applyLivePromptLineAdjustment(int &total, int &current) const {
  if (!countLivePromptAsLine_) return;
  total += 1;
  if (atLivePromptLine_) current = total;
}

// 2026-09-08追加、直接のユーザー報告("usilic画面では98行だった行が
// 97として表示されている"): current/totalは元々cursorIndex_+1/
// lines_.size()――幅超過による折り返しが起こるFile mode(PipeBuffer::
// wrapLine())では「表示行」の数であって、vi等が理解する「生の
// (改行区切りの)行」の数ではない。ContentView::Line::rawLineNumber
// (折り返しを起こし得るFileModeだけが設定する、他の全呼び出し元は
// nullopt)があればそちらへ翻訳する――無ければ何もしない(既存の
// 挙動のまま)。applyLivePromptLineAdjustment()の「後」に呼ぶこと
// (Shell自身の生きたプロンプト行の水増しと独立した、別の補正のため)。
// 2026-10-15、直接のユーザー報告("HELPの画面ドラッグ中は総行数が「1050」なのに止めると「759」"): ドラッグ・慣性の
// 間のモードライン(scrollModeLineText()・scrollModeLineSegments())も、止まっている時(applyRawLineNumbers())と
// 同じく折り返す前の行で数える。currentは画面の行の番号(1始まり)で、その行の元の行番号にする。
void ContentView::applyRawLineNumbersAt(int &current, int &total) const {
  if (total <= 0 || lines_.empty() || !lines_[0].rawLineNumber) return;
  int idx = std::clamp(current - 1, 0, static_cast<int>(lines_.size()) - 1);
  current = lines_[idx].rawLineNumber.value_or(current);
  total = lines_.back().rawLineNumber.value_or(total);
}

void ContentView::applyRawLineNumbers(int &current, int &total) const {
  if (total <= 0 || lines_.empty() || !lines_[0].rawLineNumber) return;
  int idx = std::clamp(cursorIndex_, 0, static_cast<int>(lines_.size()) - 1);
  current = lines_[idx].rawLineNumber.value_or(current);
  total = lines_.back().rawLineNumber.value_or(total);
}

void ContentView::setMarkedPredicate(std::function<bool(int)> predicate) { markedPredicate_ = std::move(predicate); }

void ContentView::setCursorRowAlsoHighlighted(bool enabled) { cursorRowAlsoHighlighted_ = enabled; }

void ContentView::setMarkedRowUsesUnderline(bool enabled) { markedRowUsesUnderline_ = enabled; }

void ContentView::setModeLineRow(std::optional<int> row) { modeLineRow_ = row; }

std::string ContentView::truncateFieldByKind(const std::string &text, TrailingKind kind, int budget) const {
  if (budget <= 0) return "";
  if (static_cast<int>(text.size()) <= budget) return text;
  if (budget < 4) return "";  // 「...」+実際の文字1つにすら余地が無い
  if (kind == TrailingKind::PathMiddle) return middleEllipsisTruncate(text, budget);
  int tailChars = kind == TrailingKind::QueryEcho ? std::min(16, budget - 3) : budget - 3;
  return "..." + text.substr(text.size() - static_cast<std::size_t>(tailChars));
}

ContentView::ModeLineFields ContentView::withTruncatedFields(ModeLineFields fields,
                                                                  const std::string &positionText) const {
  int cols = display_.columns();
  if (cols <= 0) return fields;
  // 2026-08-15の修正: name自体は任意に長くなり得る(例えば
  // Application::goToFileSelector()が深くネストされたパスへ直接
  // ジャンプした後のDirectoryMode::currentDirectory())――実際の
  // 報告+スクリーンショット(=ba、"モードラインが不調（←ディレクト
  // リ名長過ぎ？」)が、長すぎるnameが、単に収まらないだけではなく
  // モードラインの行全体を壊していることを発見した
  // (fitModeLineFields()自身の階層は決してnameを落とさないので、
  // 幅に関わらず完全に描画されていた)。全「3つの」モードライン
  // 描画経路が既に全フィールドを通す唯一の場所であるここで切り
  // 詰められる――少なくとも位置テキスト(この移植が決して落とさない
  // 唯一のフィールド)のための余地を確保する。下のtrailing2と同じ、
  // 予算を決めてから切り詰めるという形。
  //
  // 2026-08-15の修正: これの最初のバージョンはpositionのための
  // 余地だけを確保し、残り「全て」を埋めるようnameを貪欲に切り
  // 詰めていた――実際の報告+スクリーンショット(=bd、深いディレク
  // トリ自身のモードラインが、参照の=beとは異なり、「(ls)」と
  // 「usilic v1.5b」のrightWatermarkの「両方」を欠いていた)が、
  // それがtrailing/rightWatermarkの余地をゼロにしてしまい、
  // fitModeLineFields()自身の後の階層が(それ自身のロジックに
  // よって)正しく、もはやどちらも収まらないと判断して両方を落と
  // していたことを発見した。nameの自身の予算を計算する際、
  // trailingとrightWatermarkの余地も確保するので、「(ls)」/
  // 「usilic v1.5b」のような短い、価値の高いペアは、黙って絞り
  // 出されるのではなく、切り詰められたnameと一緒に生き残る。
  // trailing2はここでは確保されない――下のそれ自身の予算ロジック
  // が、残っているものに収まるかどうかを既に独立して決めている。
  //
  // 各フィールドは、それが個別に短い場合(<= kReservableFieldLength)
  // にだけ確保される――実際の退行が、この同じ確保を無条件に適用
  // すると、あるdoctestフィクスチャ自身の「CUT」の名前が完全に
  // 消えてしまうことを発見した: ^J^C候補一覧自身の`trailing`は
  // 70文字の候補プレビュー文字列であり、いずれにせよname+position
  // と一緒には決して収まらなかった(そしてfitModeLineFields()自身の
  // 「既存の」階層が既にその場合それを正しく落とす――2026-08-11の
  // 修正参照)が、ここでその完全な長さのための余地を確保すると、
  // そのロジックが実行される機会を得るより前に、nameを2文字にまで
  // 飢えさせてしまっていた。「(ls)」(4)/バイト数(約10)/
  // 「usilic v1.5b」(12)は全て快適にこの上限をクリアする。任意の
  // 長いプレビュー/注釈はクリアせず、代わりに意図的に
  // fitModeLineFields()自身の階層に決めさせるために残される。
  constexpr int kReservableFieldLength = 20;
  // +1: name自体とpositionの間のギャップ(この関数の他の全ての
  // 階層が既にrowLen()自身の「+1」経由で計上しているのと同様、
  // renderModeLine()は両方が表示される時常にこれを描く)。
  int reserved = 1;
  // trailingは" " + trailing(renderModeLine()自身のdrawnLen追跡)
  // として描かれる: 1つの区切り+それ自身の長さ。
  if (!fields.trailing.empty() && static_cast<int>(fields.trailing.size()) <= kReservableFieldLength) {
    reserved += 1 + static_cast<int>(fields.trailing.size());
  }
  // rightWatermarkは" " + rightWatermark + " "として描かれる(自身
  // のrenderModeLine()呼び出し箇所参照: `watermark = " " +
  // rightWatermark + " "`)――1つではなく「2つ」の追加の空白。
  // 実際の退行(この確保は最初「1 +」だけを使っており、1つ足り
  // なかった)が、この修正の最初の試みの後でも、透かしがまだ
  // renderModeLine()自身の`drawnLen + watermark.size() <= cols`
  // チェックによって黙って落とされ、ちょうど1列足りない状態に
  // 着地していることを発見した。
  if (!fields.rightWatermark.empty() && static_cast<int>(fields.rightWatermark.size()) <= kReservableFieldLength) {
    reserved += 2 + static_cast<int>(fields.rightWatermark.size());
  }
  // trailing2も同様(例えばDirectoryMode自身の「(ls)」フォール
  // バック、検索注釈が無くても常に埋められる)――実際の退行が、
  // trailing/rightWatermarkだけを確保するのではまだ不十分である
  // ことを発見した: trailing2「自身の」下流の切り詰めロジック
  // (すぐ下)は、nameの後に「残る」どんな予算にでもそれを収めよう
  // とするが、その予算は既に、trailing+rightWatermarkに丁度十分な
  // だけを確保するために完全に使い切られていたので、「(ls)」の
  // ような短いtrailing2でも合計がいずれにせよ予算を再び超えてしま
  // い、同じ下流の`drawnLen + watermark.size() <= cols`チェック
  // 経由で再び透かしを黙って落としていた。それが中間省略記号で
  // 切り詰められる対象(FileMode自身の長いLOCATEパス注釈)である
  // 場合は確保されない――それらは構築上長く、kReservableFieldLength
  // をはるかに超えるので、既に長すぎるtrailingが除外されるのと
  // 同じ方法で除外される。
  if (!fields.trailing2.empty() && static_cast<int>(fields.trailing2.size()) <= kReservableFieldLength) {
    reserved += 1 + static_cast<int>(fields.trailing2.size());
  }
  // trailing3(2026-09-16追加、実機報告"戻り値が「.../g.../src/」
  // （...が2カ所）"): trailing/rightWatermark/trailing2と全く同じ
  // 理由でここでも確保する。ARCHIVEの新しいtrailing3(archive_mode.cpp
  // 自身のtruncatedTrailingPath()参照、常に19文字以下)のような短い
  // 値は、この確保が無いとnameが先にこの関数で育ち切ってしまい、
  // withTruncatedTrailing3()自身の下流の予算計算に一切余地を残さず、
  // 既に切り詰め済みのtrailing3を「二重に」middleEllipsisTruncate()
  // してしまっていた。他モード自身の(意図的に長い、untruncatedな)
  // trailing3はkReservableFieldLengthを超えるので、この確保は効かず、
  // 既存の挙動を変えない。
  if (!fields.trailing3.empty() && static_cast<int>(fields.trailing3.size()) <= kReservableFieldLength) {
    reserved += 1 + static_cast<int>(fields.trailing3.size());
  }
  int positionBudget = cols - static_cast<int>(positionText.size()) - reserved;
  if (positionBudget < 0) {
    // trailing/rightWatermarkも確保してなお余地が足りない――「元々
    // の」、無条件の保証(nameは「少なくとも」positionと一緒に収まる)
    // へフォールバックする。fitModeLineFields()自身の階層が、ここ
    // から、trailing/trailing2が実際に表示されるかどうかを依然として
    // 決める。
    positionBudget = cols - static_cast<int>(positionText.size()) - 1;
  }
  if (positionBudget < 0) positionBudget = 0;
  if (static_cast<int>(fields.name.size()) > positionBudget) {
    fields.name = positionBudget >= 4 ? middleEllipsisTruncate(fields.name, positionBudget)
                                       : fields.name.substr(0, static_cast<std::size_t>(positionBudget));
  }

  // 2026-08-17の追加: `trailing`自体が切り詰めを選択している場合
  // (trailingKind != Tag)、それを完全に下のfitModeLineFields()
  // 自身のall-or-nothingの階層に任せるのではなく、切り詰める
  // ――実際の再現(=bw)が、`trailing`が以前これに対して全く切り
  // 詰めのオプションを持たなかった(上下で切り詰められる`name`/
  // `trailing2`とは異なり)ため、listBrowserView_自身のFILE/DIR/
  // COMMANDピッカーの`trailing`(長い絶対パス、trailing2は関与
  // しない)が、列の予算を超えた時点で「完全に」消えてしまうこと
  // を発見した。他の「全ての」プロバイダはtrailingKindをデフォルト
  // (Tag)のままにするので、このブロックはそれらに対しては何もし
  // ない。純粋に安全マージンとして、trailing2の余地も確保する
  // (上のnameの自身の確保が既に使っているのと同じ
  // kReservableFieldLengthのヒューリスティック)――現在切り詰め
  // 可能な`trailing`と空でない`trailing2`を組み合わせるプロバイダ
  // は無い。
  if (fields.trailingKind != TrailingKind::Tag && !fields.trailing.empty()) {
    int base = static_cast<int>(positionText.size());
    if (!fields.name.empty()) base += static_cast<int>(fields.name.size()) + 1;
    int trailing2Reserved = 0;
    if (!fields.trailing2.empty() && static_cast<int>(fields.trailing2.size()) <= kReservableFieldLength) {
      trailing2Reserved = 1 + static_cast<int>(fields.trailing2.size());
    }
    int budget = cols - base - 1 - trailing2Reserved;
    fields.trailing = truncateFieldByKind(fields.trailing, fields.trailingKind, budget);
  }

  if (fields.trailing2.empty()) return fields;
  int base = static_cast<int>(positionText.size());
  if (!fields.name.empty()) base += static_cast<int>(fields.name.size()) + 1;
  int trailingLen = fields.trailing.empty() ? 0 : static_cast<int>(fields.trailing.size()) + 1;
  int budgetWith = cols - base - trailingLen - 1;      // trailingと一緒のtrailing2のための余地
  int budgetWithout = cols - base - 1;                 // trailingが落とされた場合のtrailing2のための余地
  int trailing2Len = static_cast<int>(fields.trailing2.size());

  if (trailing2Len <= budgetWith) return fields;  // trailingと一緒に既に完全に収まる――何もすることは無い

  // 2026-08-14の修正: 以前は末尾のみ("..." + まさに末端)で、
  // 「頭部」を完全に失っていた(例えばFileMode::setSearchOrigin()の
  // 完全な絶対パスは「/Applications/...」の先頭を失っていた)。
  // 直接のユーザー依頼("戻り値も検索一覧同様「/Applications/Adobe
  // Acrob...source/.../HKSCS.txt」形式で")が、ここでもLOCATE自身の
  // 結果一覧の行が既に使っているのと「同じ」中間省略記号の慣習
  // (head...tail、middleEllipsisTruncate()自身のコメント参照)を
  // 求めた――完全なパスの両端は、この関数自身のすぐ上の
  // trailing/trailing2の「選択」ロジック(1つを省略する方法では
  // なく、どちらのフィールドを保つかについてのもの)とは異なり、
  // 実際の情報(どこに住んでいるか、何と名付けられているか)を
  // 運ぶ。
  //
  // 2026-08-15の修正: その中間省略記号への切り替えは広すぎること
  // が判明した――ここで「無条件に」適用されると、他の「全ての」
  // モード自身のtrailing2(例えばDirectoryMode::searchAnnotation_)
  // も変えてしまい、それは直接のユーザーポリシー声明が末尾のみで
  // あるべきと明確化していた: "なおLOCATE以外では、右下端の戻り値
  // が長すぎる場合「...16文字」でよろしく"。今はfields.trailing2Kind
  // によってゲートされている(2026-08-17: 以前は
  // trailing2MiddleEllipsis、TrailingKind自身のコメント参照)――
  // FileMode自身のLOCATE起源のケースはそれを明示的に選択する。
  // 他の全てのプロバイダは「元々の」末尾のみの挙動(QueryEchoの
  // デフォルト)を保ち、他所でtruncatedTrailingPath()が既に確立
  // したのと同じ16文字に制限される(ただし依然として予算を意識
  // する: そのヘルパー自身の固定幅バージョン(文字列がModeLineFields
  // に到達する前に事前に切り詰められる場所で使われる)とは異なり、
  // 実際に収まる以上のものは決して表示しない)。truncateFieldByKind()
  // (自身のコメント参照)に委ねる。上の新しい`trailing`切り詰め
  // ブロックが今共有しているのと同じヘルパー。
  auto truncate = [&](int budget) -> std::string {
    return truncateFieldByKind(fields.trailing2, fields.trailing2Kind, budget);
  };

  if (trailing2Len <= budgetWithout && !fields.trailing.empty()) {
    // trailingを落とすとtrailing2を「完全に」表示できる――価値が
    // ある。2026-08-11のGREPの修正が確立したのと同じ優先順位
    // (長い候補プレビュー対短い移動先ディレクトリ)。
    fields.trailing.clear();
    return fields;
  }
  if (budgetWith >= 4) {
    fields.trailing2 = truncate(budgetWith);
    return fields;
  }
  if (budgetWithout >= 4 && !fields.trailing.empty()) {
    fields.trailing.clear();
    fields.trailing2 = truncate(budgetWithout);
    return fields;
  }
  fields.trailing2.clear();
  return fields;
}

// 2026-08-27の修正、実際の報告+PTY再現(長い、実際の$HOME起点の
// 移動先パス――「~/.usilic/saved/」――が、setArcExtractSourceAnnotation()
// 経由でDirectoryModeに着地するArchiveMode自身の「ARC: <アーカイブ
// パス>」trailing3と組み合わさった、自身のコメント参照): trailing3
// は以前ここに全く切り詰めのオプションが無かった(上で扱われる
// name/trailing/trailing2とは異なる)――renderModeLine()自身の
// 「余地があれば表示、そうでなければ完全に落とす」ポリシー(自身
// のコメント参照)は、収まるにはほんの少し長すぎるtrailing3が、
// 「/tmp/」のような短い移動先ならまさに同じ注釈に十分な余地を
// 残しているにも関わらず、完全に消えてしまう(「戻り値=空白」)
// ことを意味していた。ここでそれを中間省略記号で切り詰める
// (すぐ上のtrailing2自身のPathMiddle処理と同じ形)ことは、それが
// 今や「常に」表示される――他の全てと同様、行が混雑している時は
// 切り詰められる――ことを意味し、周囲の「他の」フィールドがたま
// たまどれだけ長いかだけに依存する、全か無かのコイントスではなく
// なる。
ContentView::ModeLineFields ContentView::withTruncatedTrailing3(ModeLineFields fields,
                                                                  const std::string &positionText) const {
  int cols = display_.columns();
  if (cols <= 0 || fields.trailing3.empty()) return fields;
  int base = static_cast<int>(positionText.size());
  if (!fields.name.empty()) base += static_cast<int>(fields.name.size()) + 1;
  if (!fields.trailing.empty()) base += static_cast<int>(fields.trailing.size()) + 1;
  if (!fields.trailing2.empty()) base += static_cast<int>(fields.trailing2.size()) + 1;
  int budget = cols - base - 1;  // trailing3自体の前のギャップのための1
  if (static_cast<int>(fields.trailing3.size()) <= budget) return fields;
  fields.trailing3 = budget >= 4 ? middleEllipsisTruncate(fields.trailing3, budget) : std::string();
  return fields;
}

ContentView::FieldFit ContentView::fitModeLineFields(const ModeLineFields &fields,
                                                       const std::string &positionText) const {
  int cols = display_.columns();
  auto rowLen = [&](bool withName, bool withTrailing, bool withTrailing2) {
    int len = static_cast<int>(positionText.size());
    if (withName && !fields.name.empty()) len += static_cast<int>(fields.name.size()) + 1;
    if (withTrailing && !fields.trailing.empty()) len += static_cast<int>(fields.trailing.size()) + 1;
    if (withTrailing2 && !fields.trailing2.empty()) len += static_cast<int>(fields.trailing2.size()) + 1;
    return len;
  };
  if (cols <= 0 || rowLen(true, true, true) <= cols) return {true, true, true};
  // 2026-08-11の修正: `trailing`と`trailing2`は以前、合計幅が
  // 溢れるたびに1つのall-or-nothingの単位として一緒に落とされて
  // いた――実際の報告が、^J^C候補一覧自身の長い正規表現パターンの
  // プレビュー(`trailing`)が行を列の予算を超えて押し出し、
  // trailing2(移動先ディレクトリ)単独なら簡単に収まったはずなのに
  // それも黙って落としてしまうことを発見した。`trailing`(他の
  // 全てのプロバイダに対しては「(fgrep)」のような短い装飾的な
  // タグ、あるいはここでは長い、それほど重要でないライブプレビュー)
  // が今は先に落とされ、trailing2(より重要な文脈――ディレクトリ
  // パス)は、それ単独で収まる余地がある限り可視のままになる。
  if (rowLen(true, false, true) <= cols) return {true, false, true};
  // 2026-08-14の修正: 上の階層は、`trailing2`が単独で収まる時点で
  // `trailing`だけを落とせば常に十分だと想定している――GREP自身の
  // ケース(長いプレビュー対短い移動先ディレクトリ)では真だが、
  // trailing2自体が長いフィールドである(例えばFileModeの
  // ディレクトリ、またはその新しいLOCATEパス注釈――FileMode::
  // setSearchAnnotation()参照)かつtrailingが短い(例えばFileMode
  // のバイト数)場合は「偽」である。実際の報告("モードラインに
  // バイト数「397723」...が無い")がまさにこれを発見した: 長い
  // ディレクトリ単独でもまだ収まらないので、上の「両方の」階層が
  // 失敗し、下のフォールバックはtrailing(バイト数)も落として
  // しまっていた。trailing単独ならtrailing2が邪魔をしなければ
  // 簡単に収まったはずなのに。完全に諦める前に試すことで、実際に
  // 問題なのが長いtrailing2である場合には、短い、有用なtrailingが
  // 表示される。
  if (rowLen(true, true, false) <= cols) return {true, true, false};
  if (rowLen(true, false, false) <= cols) return {true, false, false};
  return {false, false, false};
}

std::string ContentView::composeModeLineRow(const std::string &positionText) const {
  if (!modeLineFieldsProvider_) return positionText;
  ModeLineFields fields = withTruncatedTrailing3(
      withTruncatedFields(providedModeLineFields(), positionText), positionText);
  FieldFit fit = fitModeLineFields(fields, positionText);

  std::string row = positionText;
  // 2026-08-11の修正: trailing2は以前`!fields.trailing.empty()`の
  // 中にネストされていたので、空でないtrailing2を持つが「空の」
  // trailingを持つフィールド(例えば^J^C候補一覧自身のプロバイダ、
  // その`trailing`は現在ハイライトされている項目であり、空のリング
  // では正当に空であり得る一方、trailing2――移動先ディレクトリ
  // ――は依然として表示すべき何かを持つ)は、trailing2を完全に
  // 黙って落としてしまっていた。各フィールド自身の空かどうかは今や
  // それ自身だけをゲートする。
  if (fit.showTrailing && !fields.trailing.empty()) row += " " + fields.trailing;
  if (fit.showTrailing2 && !fields.trailing2.empty()) row += " " + fields.trailing2;
  // ModeLineFields::trailing3自身のコメント参照――存在する場合は
  // 常に追加される(fit階層によるゲートは無く、renderModeLine()
  // 自身の寛容な「余地が無ければ落とす」ポリシーに合わせている。
  // このプレーンな文字列の消費者は、ここでチェックすべき自身の
  // 列の予算を持たない)。
  if (!fields.trailing3.empty()) row += " " + fields.trailing3;
  if (fit.showName && !fields.name.empty()) row = fields.name + " " + row;
  return row;
}

// 2026-09-13追加、直接のユーザー依頼(項目458フィードバック)。
// 2026-09-13修正、項目459フィードバック("スクロールバーがクネクネに
// なる...やはりそれ用のエリアが必要かと...-gと同じく...一度でも
// バーが表示されたら以後は常時...幅は今の倍(二文字分)で"): 当初は
// モードライン非表示中だけ、既存の行の最終列へ上書きしていたが、行
// ごとに元々あった内容と衝突して縁がガタガタになる問題があった。
// TerminalDisplay::columns()自身の.cpp側コメント参照――この関数の
// 呼び出し元(ここ)が意識すべきなのは、columns()が返す値は既に
// つまみ2列ぶん差し引かれた「コンテンツ用の」幅だということ。つまり
// ここでの書き込み先(columns()、columns()+1)は、他の全ての描画が
// 一切使わない、常に予約済みの専用領域であり、上書きの心配は無い。
// 2026-09-13訂正、直接のユーザー自身の言葉("張りぼては-gだけの話
// だったなと途中で気づいた。気づかぬまま-gと同じく完全に常時、とは
// 書いたけど、モードライン有りの時は消せる？"): 「完全に常時」は
// GraphicalDisplay自身の(この機能とは無関係な)張りぼて/本物の話との
// 混同に気づく前の回答だったため、モードライン表示中はこの関数の
// 描画自体を抑制する――ただし2列の幅の予約(TerminalDisplay::
// columns()側)は、^Ohの切り替えのたびに全コンテンツが再折り返しに
// なる方が却って落ち着かないと判断し、意図的にモードラインの表示/
// 非表示に関わらず常時のままにしてある(モードライン表示中はこの2列
// が単なる未使用の余白になるだけ)。
void ContentView::renderScrollIndicatorOverlay() {
  if (display_.modeLineVisible() || display_.hasNativeScrollbar()) return;
  int total = static_cast<int>(lines_.size());
  auto [thumbTop, thumbHeight] = scrollThumbRowRange(topIndex_, height_, total);
  if (thumbHeight <= 0) return;
  int col = columns();
  for (int row = thumbTop; row < thumbTop + thumbHeight; row++) {
    display_.moveCursor(row0_ + row, col);
    display_.setAttribute(Display::Attribute::Standout);
    display_.putChar(' ');
    display_.putChar(' ');
    display_.setAttribute(Display::Attribute::StandoutOff);
  }
}

// print_line()の現在行/合計行/パーセント表示からの移植(disp_window()
// のこの一片だけがこのクラスへ移された理由についてはModeLine自身の
// クラスコメント参照)。row0_+height_は、まさにこの目的のために全て
// のモードのビューポートが未使用のままにしておく1つの画面の行に
// 常になっている(Application::resize()の「contentHeight =
// rows()-1」参照)ので、setViewport()の呼び出し元が既に保証している
// もの以上の境界チェックはここでは不要である。
//
// name/position/trailingを、composeModeLineRow()の結合された文字列
// の上の1つの連続したrunではなく、それぞれの間に単一のプレーンな
// (非standoutな)空白を挟んだ、「別々の」反転表示のrunとして描く
// ――disp_window()自体が各フィールドを個別にvt_stand()/
// vt_endstand()で囲んで呼んでいた(nameに対してはput_str()、
// positionに対してはprint_line()内のそれ自身のペア、disp.c参照)
// ことからの移植。実際の並べたCUIスクリーンショットとの比較が
// これを捉えた: この移植の最初の試みは、完全に組み立てられた
// 文字列を1つのrunとして描いており、それは1つの途切れないハイ
// ライトバーとして読める――CUIのモードラインは、フィールドの間に
// 目に見える小さな黒い(ハイライトされていない)ギャップを持つ。
void ContentView::renderModeLine() {
  // 2026-09-12追加、項目⑫(display_.modeLineVisible()自身のコメント
  // 参照): 非表示の間はsetViewport()経由のheight_がモードライン用の
  // 1行を予約しなくなっている(Application::layoutViews()参照)ため、
  // 下のmodeLineRow_.value_or(row0_+height_)は画面最終行の「次」を
  // 指してしまう――描画を単に省くだけでなく、この関数冒頭で早期
  // returnしないと範囲外へのmoveCursor()になる。total/current計算
  // (applyLivePromptLineAdjustment()/applyRawLineNumbers())はこの関数
  // のローカル変数だけを更新するconstヘルパーなので、丸ごと省いても
  // 他のどこにも副作用は残らない。
  if (!display_.modeLineVisible()) return;
  int total = static_cast<int>(lines_.size());
  int current = total > 0 ? cursorIndex_ + 1 : 0;
  applyLivePromptLineAdjustment(total, current);
  applyRawLineNumbers(current, total);
  std::string positionText = positionTextOverride_ ? *positionTextOverride_
                              : useFixedWidthPosition_ ? ModeLine::formatFixedWidth(current, total)
                                                        : ModeLine::format(current, total);
  int modeLineRow = modeLineRow_.value_or(row0_ + height_);

  display_.moveCursor(modeLineRow, 0);
  display_.clearToEndOfLine();

  // 2026-08-28の修正、直接のユーザー報告("~/.usilic/usilicrcのGREP
  // セクションに実際にある^L(0x0C、historyファイル内のモード区切り
  // 用)がHISTORY一覧画面で「ハイライトのLが無い」"): 項目283自身の
  // 修正(このファイル内の自身のコメント、trailingPlain分岐参照)は
  // 「その」1つの描画経路だけを修正していた――この汎用的な
  // drawSegment()(renderModeLine()/renderSplitModeLine()が描く他の
  // 全てのフィールドに使われる。promptHistoryRecallKind_自身の
  // !directEntry(「HISTORY」時系列トグル)分岐が明示的に
  // trailingPlain=falseを設定する場合の同じ候補文字列を含む)は、
  // 依然として生の制御バイトをそのまま通しており、それに対する
  // 本物の端末自身のカーソル移動の反応(例えば^L、フォームフィード、
  // 一般に「消去/次のページ」を意味する)が、それに続くものを
  // 破壊するに任せていた。まさに同じ置き換えをここでも適用する。
  // このセグメント全体が既に至る所Standoutである(なので
  // trailingPlainのその他の点ではプレーンな周囲のテキストが持つ
  // ような、別の「目立たせる」視覚的な必要性は無い)にも関わらず
  // ――生のバイトが端末に到達することへの安全対策こそが実際の
  // 要点である。
  auto drawSegment = [&](const std::string &text) {
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : text) {
      bool isControl = c && (c < ' ' || c == 0x7f);
      display_.putChar(isControl ? static_cast<unsigned char>((c == 0x7f ? c - 0x80 : c) + '@') : c);
    }
    display_.setAttribute(Display::Attribute::StandoutOff);
  };
  // ModeLineFields::nameUnderlined自身のコメント参照。
  auto drawUnderlineSegment = [&](const std::string &text) {
    display_.setAttribute(Display::Attribute::Underline);
    for (unsigned char c : text) display_.putChar(c);
    display_.setAttribute(Display::Attribute::UnderlineOff);
  };
  // ModeLineFields::trailing2Plain自身のコメント参照(2026-09-10追加、
  // 直接のユーザー要望: in-place GREPのマッチ無しメッセージを
  // "非ハイライト"で表示したい)。drawPositionText()自身のplain経路
  // と同じ発想だが、trailing2専用。
  auto drawPlainSegment = [&](const std::string &text) {
    for (unsigned char c : text) display_.putChar(c);
  };
  // setPositionTextOverride()自身の`plain`引数のコメント参照
  // (2026-09-08追加、直接のユーザー要望): 位置フィールドだけ、
  // Standout無しで描画する経路。他の全フィールド(name/trailing等)は
  // 影響を受けない。
  auto drawPositionText = [&](const std::string &text) {
    if (positionTextOverride_ && positionTextPlain_) {
      for (unsigned char c : text) {
        bool isControl = c && (c < ' ' || c == 0x7f);
        display_.putChar(isControl ? static_cast<unsigned char>((c == 0x7f ? c - 0x80 : c) + '@') : c);
      }
    } else {
      drawSegment(text);
    }
  };

  if (!modeLineFieldsProvider_) {
    drawPositionText(positionText);
    return;
  }

  // 2026-10-03修正、直接のユーザー報告("MACRO定義中にモード名がMACROにならない"): ここだけ
  // providedModeLineFields()を通さずに読んでいたので、^X(〜^X)の"MACRO"が当たらなかった。
  ModeLineFields rawFields = providedModeLineFields();
  // ModeLineFields::splitLayoutWhenCrowded/renderSplitModeLine()自身
  // のコメント参照。withTruncatedFields()の出力ではなく「生の」
  // (切り詰められていない)フィールドに対してチェックされる――実際
  // のフィールドの長さの素朴な合計こそが、そもそも通常のレイアウト
  // がフィールドを落とし始める必要があったかどうかを決めるもので
  // ある。
  if (rawFields.splitLayoutWhenCrowded) {
    int cols = display_.columns();
    int naiveLen = static_cast<int>(positionText.size());
    if (!rawFields.name.empty()) naiveLen += static_cast<int>(rawFields.name.size()) + 1;
    if (!rawFields.trailing.empty()) naiveLen += static_cast<int>(rawFields.trailing.size()) + 1;
    if (!rawFields.trailing2.empty()) naiveLen += static_cast<int>(rawFields.trailing2.size()) + 1;
    if (cols > 0 && naiveLen > cols) {
      renderSplitModeLine(rawFields, positionText);
      return;
    }
  }

  ModeLineFields fields = withTruncatedTrailing3(withTruncatedFields(rawFields, positionText), positionText);
  FieldFit fit = fitModeLineFields(fields, positionText);
  int cols = display_.columns();

  int drawnLen = 0;
  if (fit.showName && !fields.name.empty()) {
    if (fields.nameUnderlined) drawUnderlineSegment(fields.name);
    else drawSegment(fields.name);
    display_.putChar(' ');
    drawnLen += totalDisplayWidth(fields.name) + 1;
  }
  drawPositionText(positionText);
  drawnLen += totalDisplayWidth(positionText);
  // 2026-08-11の修正: trailing2は以前`!fields.trailing.empty()`の
  // 中にネストされており、trailing自体が空の時はそれを黙って落とし
  // ていた(例えば^J^C候補一覧自身のプロバイダ、その`trailing`
  // ――現在ハイライトされている項目――は空のリングでは正当に空で
  // あり得る一方、trailing2、移動先ディレクトリは依然として表示
  // すべき何かを持つ)。各フィールド自身の空かどうかは今やそれ
  // 自身だけをゲートする。
  if (fit.showTrailing && !fields.trailing.empty()) {
    display_.putChar(' ');
    drawnLen += 1;
    std::string trailing = fields.trailing;
    // 端へのパディング(下参照)は、存在する場合はtrailing2(実際に
    // 描画される「最後の」フィールド)に適用される――一度trailing2
    // が存在すれば`trailing`自体が決してパディングされない理由に
    // ついてはModeLineFields::trailing2自身のコメント参照(それも
    // パディングすると、そもそもtrailing2が作るために存在する2つ
    // の間のプレーンなギャップを消してしまう)。
    // 2026-08-19の修正: 存在する場合、すぐ下のtrailing2自身の同一
    // の譲歩と同じ理由で、trailing3(自身のコメント参照)にも譲る
    // ――今日のArchiveModeでは到達不能(そこではtrailing2と
    // trailing3は常に一緒に設定される)だが、将来のtrailing2無し、
    // trailing3のみのプロバイダが、自身の端埋めを`trailing`が
    // 行全体を先に食べてしまうことで黙って失わないよう、一貫性を
    // 保つために保持されている。
    if (fields.fillTrailingToEdge && fields.trailing2.empty() && fields.trailing3.empty()) {
      // disp_window()自身の`for (...) prev[i] = ' '`ループからの
      // 移植: (依然としてこのフィールド自身のstandout run内で)
      // 空白でパディングし、テキストのすぐ後で止まるのではなく
      // ハイライトが画面の右端に到達するようにする――どのモードが
      // これを望むかについてはModeLineFieldsのfillTrailingToEdge
      // コメント参照。
      // cols-1で上限を設ける。colsでは「ない」(2026-08-18の修正、
      // 実際の報告: RecentFile/RecentDirectory自身の候補一覧の
      // モードライン。これをたまたま覆い隠すrightWatermarkを持たない
      // 最初の呼び出し元は、バイト単位で正しい出力にも関わらず、
      // 実際のTerminal.appでは依然として埋まっていないように
      // 見えた――`script`でキャプチャした生のセッションで確認済み)。
      // このファイル内のすぐ上のrenderLine()自身の2026-08-13の修正
      // (自身の長いコメント参照)と同じ根本原因: 端末の本当の
      // 「最後の」列へStandoutでマークされた文字を書くと自動折り
      // 返しが引き起こされ、実際のカーソルが存在しない行(これは
      // 行24、画面のまさに最下部で、1-23行に設定されたDECSTBM
      // スクロール領域の意図的に外側)へ黙って移動してしまう
      // ――本物の端末は「右下端を越えて折り返す、スクロール領域の
      // 外側」を一貫性なく処理し、ここでのTerminal.app自身の処理は
      // どうやら、何か「後の」、無関係な書き込みがそれを促すまで、
      // その行の再描画を先延ばしにしているようだ(まさにその症状に
      // 一致する: 他に何も再描画しないキーであっても、さらなる
      // キー入力が押された瞬間に直った)。DirectoryMode自身の
      // 「(ls)」trailingフィールドは、既にこの同じfillTrailingToEdge
      // 経路を使っており、同一の最終列への書き込みに当たっている
      // が、その直後に描画される自身のrightWatermark("usilic
      // v1.5b")によって覆い隠されている。それは、自動折り返しが
      // 黙ってどこへカーソルを残したかに関わらず、自身の
      // moveCursor()呼び出し経由でカーソルを再配置するため――偶然の
      // 覆い隠しであり、本当の修正ではない。これが、透かしの無い
      // RecentFile/RecentDirectoryのケースだけがこれを表面化させた
      // まさにその理由である。本当の最後の列を埋めないままにする
      // ことは、この関数が新たに考案したものではなく、renderLine()
      // が既に意図的に行った同じ実用的なトレードオフである(自身の
      // コメント参照)。
      int remaining = (cols - 1) - drawnLen - totalDisplayWidth(trailing);
      if (remaining > 0) trailing += std::string(remaining, ' ');
    }
    // ModeLineFields::trailingPlain自身のコメント参照。2026-08-12の
    // 修正: これは以前、候補パターンに埋め込まれたリテラルな制御
    // バイト(例えばusilicrc自身の"^  [0-9]\x08[0-9]+\."というGREP
    // パターン、^H = 0x08)を含む全てのバイトを完全に生のまま
    // putChar()していた――生の0x08を受け取った本物の端末は、何かを
    // 印字する代わりに実際にカーソルを1列「後ろへ動かす」ので、
    // その後に続くバイトは、既にそこにあったものを黙って上書きして
    // いた(実際の報告/スクリーンショットが"[0-9]\x08[0-9]+."が
    // 文字化けした"[0-9[0-9]+."として描画されることを示した。「]」
    // がバックスペース自身のカーソル移動によって壊され、さらに次の
    // 「[」がその上に上書きされる)。renderLine()(この同じクラス、
    // すぐ上)が通常の「内容」行で既に制御バイトを扱っているのと
    // 同じ方法――反転表示の文字+'@'――で修正されている。この移植
    // 内での一貫性のため(legacy自身の参照描画は、ここでは異なる、
    // リテラルな「^」+文字の2文字の慣習を使うが、この移植は、制御
    // バイトが現れ得る他の全ての場所――LineEditor::renderVisible()、
    // renderLine()自体――で、既に意図的に単一の反転グリフの慣習へ
    // 逸脱している。そのため、legacyそのままではなく「それら」に
    // 合わせることで、この1つの候補プレビューテキストが、3つ目の
    // 一回限りの慣習を拾い上げるのではなく、表示される場所に関わら
    // ず同一に描画される)。
    if (fields.trailingPlain) {
      for (unsigned char c : trailing) {
        if (c == '\t') {
          display_.putChar(' ');
          continue;
        }
        bool isControl = c && (c < ' ' || c == 0x7f);
        if (isControl) {
          int shown = (c == 0x7f) ? c - 0x80 : c;
          display_.setAttribute(Display::Attribute::Standout);
          display_.putChar(shown + '@');
          display_.setAttribute(Display::Attribute::StandoutOff);
        } else {
          display_.putChar(c);
        }
      }
    } else {
      drawSegment(trailing);
    }
    drawnLen += totalDisplayWidth(trailing);
    // setCursorAtModeLineTrailingEnd()自身のコメント参照: ここで
    // キャッシュされる(syncVisibleCursor()自体によって再計算され
    // ない)ので、常に実際に描画されたものと「正確に」一致する
    // ――trailingは、trailing2が空の場合にだけ、上で行自身の端まで
    // パディングされる結果になる(すぐ上のfillTrailingToEdgeの
    // チェック参照)。これはこれが存在する対象である^J^S/^J^C候補
    // 一覧には決して当てはまらない(それは常に空でないtrailing2、
    // 移動先ディレクトリを持つ)ので、ここでのdrawnLenは、「行の
    // 全幅」ではなく、本当に「候補自身のテキストのすぐ後」である。
    lastModeLineTrailingEndColumn_ = drawnLen;
  }
  if (fit.showTrailing2 && !fields.trailing2.empty()) {
    std::string trailing2 = fields.trailing2;
    // trailing2RightJustified(2026-08-12の修正): 自身のコメント
    // 参照。`trailing`自身のギャップのすぐ後に順次描画する代わりに、
    // カーソルを端揃えの開始列へ直接移動する――候補プレビュー
    // テキスト自身の長さは検索ごとに変わるので、純粋に順次的な
    // trailing2(この移植の「元々の」、他の全てのプロバイダに対して
    // 依然としてデフォルトの挙動)は、端に固定されたままである
    // 代わりに、それと一緒に左右に漂ってしまう。
    if (fields.trailing2RightJustified) {
      display_.moveCursor(modeLineRow, cols - totalDisplayWidth(trailing2));
      if (fields.trailing2Plain) drawPlainSegment(trailing2); else drawSegment(trailing2);
      drawnLen = cols;
    } else {
      // ModeLineFields::trailing2NoLeadingSpace自身のコメント参照
      // (2026-09-10追加)。
      if (!fields.trailing2NoLeadingSpace) {
        display_.putChar(' ');
        drawnLen += 1;
      }
      // 2026-08-19の修正、すぐ上のtrailing自身の「trailing2も
      // 空の場合にだけパディングする」ゲートと同じ形: trailing3
      // (自身のコメント参照)は今やtrailing2の「後の」本物の
      // フィールドとして存在し得るので、ここでの端へのパディング
      // は、それが存在する時はそれに譲る――そうしないと、trailing2
      // (例えばArchiveMode自身の「(tar)」バッジ)が行の残り「全て」
      // を黙って食べてしまい、trailing3(パス)が全く描画される余地
      // を残さなくなる(このファイル自身のdoctestが捉えた実際の
      // 退行: modeLineText()はcomposeModeLineRow()の無条件の追加
      // 経由でパスを依然として報告していたが、実際のrenderModeLine()
      // の行は決してそれを表示していなかった)。
      if (fields.fillTrailingToEdge && fields.trailing3.empty()) {
        int remaining = cols - drawnLen - totalDisplayWidth(trailing2);
        if (remaining > 0) trailing2 += std::string(remaining, ' ');
      }
      if (fields.trailing2Plain) drawPlainSegment(trailing2); else drawSegment(trailing2);
      drawnLen += totalDisplayWidth(trailing2);
    }
  }
  // ModeLineFields::trailing3自身のコメント参照。(上のtrailing/
  // trailing2とは異なり)右揃え/切り詰めのオプションは無い――単に
  // 「余地があれば自身のプレーンなギャップの後に表示し、そうでな
  // ければ落とす」。ただしfillTrailingToEdgeはここで尊重される
  // (2026-08-19の追加依頼、実際の報告=dj: "戻り値が右端まで埋まっ
  // てない"――trailing3は今やArchiveMode自身の一覧表示に対して
  // 実際に描画される「最後の」フィールドであり、trailing3が空の
  // 時に「それらの」うちどちらであれ最後のものにtrailing/trailing2
  // が既に与えている「行を端までパディングする」扱いと同じ。
  // 上のtrailing自身のfillTrailingToEdgeブロックで文書化されて
  // いるのと同一の、本当の最終列を越える自動折り返しの理由による、
  // その2つと同じcols-1の上限)。
  if (!fields.trailing3.empty()) {
    std::string trailing3 = fields.trailing3;
    int candidateLen = 1 + totalDisplayWidth(trailing3);
    if (cols > 0 && drawnLen + candidateLen <= cols) {
      display_.putChar(' ');
      drawnLen += 1;
      if (fields.fillTrailingToEdge) {
        int remaining = (cols - 1) - drawnLen - totalDisplayWidth(trailing3);
        if (remaining > 0) trailing3 += std::string(remaining, ' ');
      }
      drawSegment(trailing3);
      drawnLen += totalDisplayWidth(trailing3);
    }
  }

  // disp_window()自身のフォールバックからの移植: 「前モード」の
  // テキストが表示すべきものとして無い場合(mode-line[6]――正確に
  // これがどのモードなのかについてはModeLineFields::rightWatermark
  // 自身のコメント参照)、代わりにプログラム名とバージョンが、
  // 画面自身の右端に(反転表示ではなく)「太字」で右揃えされる
  // ――特にディレクトリ一覧画面でそこに「usilic v1.5b」を表示する
  // 実際のCUIスクリーンショットに対して確認済み(ファイルビューで
  // はない。そこでは上のfillTrailingToEdgeが既に行を満たしており、
  // 自然に余地を残さない。legacyに合わせている: そこではprev0が
  // 空でないので、この全体のフォールバック分岐はC言語のソースでも
  // 決して実行されない)。
  if (!fields.rightWatermark.empty()) {
    std::string watermark = " " + fields.rightWatermark + " ";
    if (cols > 0 && drawnLen + static_cast<int>(watermark.size()) <= cols) {
      display_.moveCursor(modeLineRow, cols - static_cast<int>(watermark.size()));
      display_.setAttribute(Display::Attribute::Bold);
      for (unsigned char c : watermark) display_.putChar(c);
      display_.setAttribute(Display::Attribute::BoldOff);
    }
  }
}

// これが存在する理由と、renderModeLine()が、通常の
// fitModeLineFields()/withTruncatedFields()の階層の代わりにいつ
// これへ手を伸ばすかについてはModeLineFields::splitLayoutWhenCrowded
// 自身のコメント参照。直接のユーザー提案(報告=dhの追加依頼):
// "『「1/1 END」「111」』をまずは中央へ→残りのエリア半分ずつを
// 「モード名」「戻り値」で分け合った上でそれぞれ...". `positionText`
// +`trailing`はペアとして中央に置かれる(2つの間に自身のプレーンな
// ギャップ、renderModeLine()の他の全てのフィールドのペアと同じ
// 慣習)。nameは列0から、trailing2は右端から描かれ、それぞれ独立に
// Finderスタイルの中間省略記号で、中央のペアが自身の空間を確保した
// 後に実際に残っている幅まで切り詰められる――fitModeLineFields()の
// all-or-nothingの落とし方とは異なり、これは、片方が完全に消える
// のではなく、nameとtrailing2の「両方」の一部が可視のままである
// ことを保証し、name+trailing2が一緒になって収まらない時に古い
// 階層がまさに行っていた(これが存在する理由の報告)、`trailing`
// (バイト数)を全く落とすことを決してしない。
//
// middleEllipsisTruncateByWidth()を使う(2026-08-20の修正、実際の
// 報告"モード名...が短め"――CJK重めの名前が、自身のleftBudgetの
// 列数よりも目に見えて短く戻ってきた): 下のleftBudget/rightBudget
// は本物の画面「列」数だが、この関数の元のバージョンはバイト
// ベースのmiddleEllipsisTruncate()で切り詰めていた――
// withTruncatedFields()自身の呼び出し元にとっては安全(なぜそれが
// バイトベースのままなのかについてはその関数自身のコメント参照)
// だが、ここでは「誤り」であり、33列の予算が33「バイト」であるか
// のように使われてしまい、実際の列予算が許す約16文字ではなく、
// (それぞれ3バイトの)約11個のCJK文字しか通していなかった。この
// 関数は、その出力を(withTruncatedFields()の呼び出し元とは異なり)
// 決してfitModeLineFields()自身のバイト長の階層へ戻して供給しない
// ――それはここで直接計算された明示的な列位置で直接描画する――
// ので、幅ベースの兄弟へ切り替えることは安全である。
void ContentView::renderSplitModeLine(const ModeLineFields &fields, const std::string &positionText) {
  int modeLineRow = modeLineRow_.value_or(row0_ + height_);
  int cols = display_.columns();
  if (cols <= 0) return;

  // 2026-08-28の修正、直接のユーザー報告("~/.usilic/usilicrcのGREP
  // セクションに実際にある^L(0x0C、historyファイル内のモード区切り
  // 用)がHISTORY一覧画面で「ハイライトのLが無い」"): 項目283自身の
  // 修正(このファイル内の自身のコメント、trailingPlain分岐参照)は
  // 「その」1つの描画経路だけを修正していた――この汎用的な
  // drawSegment()(renderModeLine()/renderSplitModeLine()が描く他の
  // 全てのフィールドに使われる。promptHistoryRecallKind_自身の
  // !directEntry(「HISTORY」時系列トグル)分岐が明示的に
  // trailingPlain=falseを設定する場合の同じ候補文字列を含む)は、
  // 依然として生の制御バイトをそのまま通しており、それに対する
  // 本物の端末自身のカーソル移動の反応(例えば^L、フォームフィード、
  // 一般に「消去/次のページ」を意味する)が、それに続くものを
  // 破壊するに任せていた。まさに同じ置き換えをここでも適用する。
  // このセグメント全体が既に至る所Standoutである(なので
  // trailingPlainのその他の点ではプレーンな周囲のテキストが持つ
  // ような、別の「目立たせる」視覚的な必要性は無い)にも関わらず
  // ――生のバイトが端末に到達することへの安全対策こそが実際の
  // 要点である。
  auto drawSegment = [&](const std::string &text) {
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : text) {
      bool isControl = c && (c < ' ' || c == 0x7f);
      display_.putChar(isControl ? static_cast<unsigned char>((c == 0x7f ? c - 0x80 : c) + '@') : c);
    }
    display_.setAttribute(Display::Attribute::StandoutOff);
  };
  // renderModeLine()自身のdrawPositionText()と同じ理由(setPositionTextOverride()
  // 自身の`plain`引数のコメント参照)。
  auto drawPositionText = [&](const std::string &text) {
    if (positionTextOverride_ && positionTextPlain_) {
      for (unsigned char c : text) {
        bool isControl = c && (c < ' ' || c == 0x7f);
        display_.putChar(isControl ? static_cast<unsigned char>((c == 0x7f ? c - 0x80 : c) + '@') : c);
      }
    } else {
      drawSegment(text);
    }
  };

  std::string center = positionText;
  if (!fields.trailing.empty()) center += " " + fields.trailing;
  int centerLen = static_cast<int>(center.size());
  // 2026-08-28の修正、直接のユーザー報告+自身の明示的な仕様
  // ("戻り値＋モード名エリアがいずれも長過ぎ名称の時に限り中央に
  // ...残りを半々でいいけれども今回は「CLAUDE.md」だけなので臨機
  // 応変で"): centerStartは以前、name/trailing2が実際にどれだけ
  // 長いかとは独立に、真の数学的な中央((cols-centerLen)/2)だった
  // ――そのため短いname(例えば「CLAUDE.md」)は、それでも自身の
  // 固定された半分(おおよそcenterStart-1列)しか得られず、その
  // 半分の「残り」は死んだ、未使用の空白として残る一方、trailing2
  // (本当に長い側)は、実際に利用可能な幅が必要とするよりもきつく
  // 切り詰められて、いずれにせよ「別の」固定された半分に制限され
  // たままだった。`available`は、中央のペアとその2つのギャップが
  // 計上された後の、name+trailing2の合わせた予算である。どちらの
  // 側であれ、その側「自身の」実際の幅がその半分に収まる方が、
  // (それ以上ではなく)ちょうどその分を得て、「もう一方の」側が
  // 残りを得る――「両方の」側が本当に半分以上を必要とする場合に
  // だけ、これは均等分割へフォールバックする。centerStartは
  // 「その後」leftBudgetから導出される(下参照、逆ではない)ので、
  // 中央のペアは視覚的にどちらか短い側へ寄る――真の50/50のケース
  // では依然として中央にあるが、もはやそれとは独立ではない(これが、
  // ITS幅だけからさらに下で計算されるtrailing2自身の右揃えの描画
  // 位置が、中央のペアと決して重なることが無いようにするもので
  // ある: 構築上、leftBudget+centerLen+rightBudget+2ギャップ ==
  // colsに正確になる)。
  int available = std::max(0, cols - centerLen - 2);
  int nameWidth = totalDisplayWidth(fields.name);
  int trailing2Width = totalDisplayWidth(fields.trailing2);
  int leftBudget, rightBudget;
  if (nameWidth <= available / 2) {
    leftBudget = nameWidth;
    rightBudget = available - leftBudget;
  } else if (trailing2Width <= available / 2) {
    rightBudget = trailing2Width;
    leftBudget = available - rightBudget;
  } else {
    leftBudget = available / 2;
    rightBudget = available - leftBudget;
  }
  int centerStart = leftBudget + 1;  // +1: 中央ブロックの前のプレーンなギャップ

  std::string name = middleEllipsisTruncateByWidth(fields.name, leftBudget);
  std::string trailing2 = middleEllipsisTruncateByWidth(fields.trailing2, rightBudget);

  if (!name.empty()) {
    display_.moveCursor(modeLineRow, 0);
    drawSegment(name);
  }
  display_.moveCursor(modeLineRow, centerStart);
  drawPositionText(positionText);
  if (!fields.trailing.empty()) {
    display_.putChar(' ');
    drawSegment(fields.trailing);
  }
  if (!trailing2.empty()) {
    // cols - 画面「幅」(バイト長ではない――この関数自身のコメント
    // 参照)。CJK重めのtrailing2が(バイト-幅)列分手前で止まる
    // のではなく、本当の右端にぴったり着地するように。
    int trailing2Start = std::max(0, cols - totalDisplayWidth(trailing2));
    display_.moveCursor(modeLineRow, trailing2Start);
    drawSegment(trailing2);
  }
}

int ContentView::maxTopIndex() const {
  int maxTop = std::max(0, static_cast<int>(lines_.size()) - height_);
  if (shownIndexRange_) maxTop = std::max(maxTop, std::min(shownIndexRange_->first, static_cast<int>(lines_.size()) - 1));
  // 章区切り(setChapterStarts())では、最後の章の頭も画面の上端まで送れるように(1画面に満たない最後の章が
  // 描かれないままにならないよう、縦書きの最後のpadToPage()と同じ考え)。
  if (!chapterStarts_.empty()) maxTop = std::max(maxTop, std::min(chapterStarts_.back(), static_cast<int>(lines_.size()) - 1));
  return maxTop;
}

bool ContentView::renderContentRows() {
  clampViewport();
  // setContentImages()の画像に、見えている画像の行(Line::imageKey)の帯を足して渡す。帯の上が画面の上に
  // はみ出していても、帯ごと(負の行から)渡す(描く側が本文の行でクリップする)。
  {
    std::vector<Display::ContentImage> images = images_;
    int rowOffset = bottomAnchorRowOffset();
    for (int row = 0; row + rowOffset < height_; row++) {
      int idx = topIndex_ + row;
      if (idx < 0 || idx >= static_cast<int>(lines_.size())) break;
      const Line &line = lines_[idx];
      if (!isIndexShown(idx)) continue;
      if (line.imageKey.empty() || line.imageRows <= 0 || (row > 0 && line.imageRow > 0)) continue;
      images.push_back({line.imageKey, row0_ + rowOffset + row - line.imageRow, 0, line.imageRows, display_.columns()});
    }
    display_.setContentImages(images);
  }
  int highlighted = hoverIndex_.has_value() ? *hoverIndex_ : cursorIndex_;
  int rowOffset = bottomAnchorRowOffset();
  bool renderedAny = false;
  for (int row = 0; row < height_; row++) {
    display_.moveCursor(row0_ + row, 0);
    display_.clearToEndOfLine();
    if (row < rowOffset) continue;  // 内容の上の空白パディング、下端固定の場合のみ
    int idx = topIndex_ + (row - rowOffset);
    if (!isIndexShown(idx)) continue;  // setShownIndexRange()・setChapterStarts()参照
    if (idx < static_cast<int>(lines_.size())) {
      // Line::neverHighlighted自身のコメント参照: この行は、
      // カーソル/マーク状態に関わらず、Standout/Underlineを完全に
      // 拒否する。
      bool cursorRow = idx == highlighted && cursorRowAlsoHighlighted_ && !lines_[idx].neverHighlighted;
      bool markedRow = markedPredicate_ && markedPredicate_(idx) && !lines_[idx].neverHighlighted;
      // setMarkedRowUsesUnderline()自身のコメント参照: 有効な場合、
      // 実際にマークされた行は、カーソル行のStandoutに折り込まれる
      // のではなく、それ自身の「独立した」Underlineチャネルを得る
      // ので、2つは区別可能なままである。2026-08-25の追加依頼
      // (直接のユーザー報告: 同じ行にStandout+Underlineを組み合わ
      // せると読みにくい――反転表示の背景がUnderline自体を見にくく
      // する): 両方が当てはまる行では、両方のSGR属性を同時に表示
      // するのではなく、マーキングが今は完全に優先される――
      // Underlineのみ、Standoutは無し。無効な場合(デフォルト)、
      // これは2026-08-25より前のまさに単一のboolean呼び出しに
      // 戻る。
      const Line *line = &lines_[idx];
      Line withStandout;
      if (extraStandout_ && extraStandout_->index == idx) {  // setExtraStandout()参照
        withStandout = lines_[idx];
        withStandout.attributes.push_back({extraStandout_->start, extraStandout_->end, Display::Attribute::Standout});
        line = &withStandout;
      }
      if (markedRowUsesUnderline_) {
        renderLine(*line, cursorRow && !markedRow, markedRow);
      } else {
        renderLine(*line, cursorRow || markedRow);
      }
      renderedAny = true;
    }
  }
  return renderedAny;
}

void ContentView::redraw() {
  display_.setVerticalTextHint(verticalText_);  // setVerticalText()参照
  bool renderedAny = renderContentRows();
  int rowOffset = bottomAnchorRowOffset();
  // 2026-08-05の修正: 行が0の場合、上のループは全くrenderLine()を
  // 呼ばない(idx < lines_.size()は決してtrueにならない)――実際の
  // ユーザー報告が、これが「空の」ハイライトされた一覧(例えば
  // まだ履歴の無い^R)を、端末自身の実際のブロックカーソルが
  // モードラインが最後に描かれた場所へさまよい出ることによって
  // 悪化した(syncVisibleCursor()自身のコメント参照)、「あなたは
  // ここにいる」という画面上の指標が全く無い状態のままにしていた
  // ことを発見した。このプレースホルダーの行は、最初の本物の
  // エントリが占めるであろうのと同じ行で、「(存在しない)現在の
  // エントリ」の代わりを務める――それが本物のエントリのように
  // 「完全に」反転表示でも「ある」かどうかは、emptyPlaceholderMarked_
  // (デフォルトtrue、なぜgrepView_がこれをオフにするのかについては
  // setEmptyPlaceholder()自身のコメント参照)による。
  if (!renderedAny && cursorHighlightEnabled_ && height_ > rowOffset) {
    display_.moveCursor(row0_ + rowOffset, 0);
    renderLine(emptyPlaceholderLine_, emptyPlaceholderMarked_);
  }
  renderModeLine();
  renderScrollIndicatorOverlay();
  if (cursorHighlightEnabled_) syncVisibleCursor();
  display_.flush();
}

}  // namespace fm
