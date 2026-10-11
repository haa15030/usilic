#pragma once

#include <optional>
#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"
#include "grep_mode.hpp"
#include "mode_stack.hpp"
#include "pipe_buffer.hpp"  // StyledLine -- split()自身のコメント参照

namespace fm {

// 1つの、デリミタで区切られた区間: split()に与えられた行群への
// [startLine, endLine)。labelはデリミタ行そのもの(検索パターンが
// マッチした行)――この機能についてのユーザー自身の説明による
// (legacyのCソースだけではこれが本当に曖昧なままだったため、直接
// 確認済み――クラスコメント参照): パターン自体は各マッチで通常
// 一定である(例えばLOGモードは常にリテラルな"usilic(usilic)-1.5b"
// というシェルプロンプトのマーカーで分割する)一方、それが現れる
// 「行全体」は様々である(プロンプト行には例えばそれに続くコマンドも
// 含まれる)。だからこそ、ラベルの一覧が区間を選ぶ際に実際に役立つ。
struct CutSegment {
  std::string label;
  int startLine = 0;
  int endLine = 0;  // 排他的(この行は含まない)
  // 2026-09-30追加、直接のユーザー依頼("CUT風EPUB一覧にも...ルビを実装して"): 見出しの行の
  // ルビの装飾のラン(Display::Attribute::RubyBase/RubyText)だけ。他の装飾は従来どおり落とす。
  std::vector<AttributeRun> labelAttributes;
};

// CUTモード(ファイル内容表示中の^Jc/^J^C、ファイルを開かずに
// ディレクトリ一覧から直接の^Jb/^J^B、およびLOG自身の組み込み用途):
// バッファの各行を、検索パターンにマッチする行ごとに区間へ分割し、
// 区間ラベル(マッチしたデリミタ行)の一覧を選択肢として表示する。
// mode.cのM_cut/T_cut/T_split処理から精神を移植、逐語的にではない
// ――詳細は下記。
//
// legacyのCソースだけ(mode.c/menu.cの深く連鎖するM_egrep/T_hfgrep/
// T_hegrep/M_fgrep/M_cut/M_cut2のswitchフォールスルー、menu.cの
// get_m()参照)では、CUTのプロンプトに入力するテキストが検索パターン
// なのか、別名保存用のファイル名なのか、それとも他の何かなのかが
// それ自体からは明確でない――憶測ではなくユーザーに直接確認済み:
// それは検索パターン(^Jc/^Jb経由なら固定文字列、^J^C/^J^B経由なら
// 正規表現)であり、現在のバッファの内容を、マッチする行ごとに分割
// するのに使われ、それらのマッチした行の一覧が、区間を選ぶための
// ラベルとして提供される。
//
// これは逐語的な移植ではない。この説明を踏まえると、CUTのマッチング
// の仕組みはGrepModeのものと同一である(どちらも、突き詰めれば
// 「これらの行に対して本物のfgrep/egrepバイナリを実行し、一致した
// 行番号を受け取る」――legacyが両機能に使っているのと同じ
// fork_grep()からの移植)――CutModeは、GrepModeへの依存を持つ代わりに
// その少量のマッチングロジックを重複させている。GrepMode自身の
// ContentViewは、区間ラベルではなく「マッチした行そのもの」を
// 表示するためのものであり、GrepMode自身のsearch()を再利用する
// ためだけにCutModeへ使われない2つ目のContentViewを持たせるのは、
// それによって節約できる重複よりも不格好になるため。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - ^Jb/^J^Bの「まずファイルビューを開かずに分割する」ショート
//     カットと、LOGモード自身の組み込みのプロンプトマーカー分割:
//     どちらも、特定の情報源から特定のパターンでsplit()を呼んで
//     いるだけ――ディレクトリ一覧/ログ閲覧のオーケストレータが
//     存在するようになった時点で配線するのはMode/Applicationの
//     仕事であり、CutMode自身の関心事ではない(FileModeが誰が自分を
//     開いたか知らないのと同じ)。
//   - cut/検索パターンをHistoryへ記録すること: GrepModeが既に引いて
//     いるのと同じ境界。
//
// ネスト/再帰的な分割(splitSelected()/goBack())は「対象内」――
// ユーザー自身の追加依頼による: 既に分割済みの区間自身の内容へ
// もう一度split()を適用するのは、あるアーカイブの中に見つかった
// アーカイブを開くのと同じ意味で(「同じ操作を1段深く」)well-defined
// である――必要な新しいことは、「1段上へ戻る」ことをどこかに覚えて
// おくことだけであり、それはまさにModeStackの仕事、DirectoryModeが
// 既に自身の再帰的なディレクトリ降下のためにそれを使っているのと
// 同じ。
class CutMode {
 public:
  CutMode(ContentView &view, ModeStack &modeStack, const Configuration &configuration);

  // sourceLinesを、patternにマッチする行ごとに区間へ分割し(本物の
  // fgrep/egrepバイナリ経由、GrepModeと同じ仕組み――クラスコメント
  // 参照)、区間ラベルを表示する。検索コマンド自体が実行できなかった
  // 場合、またはpatternが空の場合のみfalse。最初のマッチより前の
  // 内容(あれば)は、静かに捨てられるのではなく、それ自身のラベル
  // 無しの先頭区間になる。マッチが全く無ければ、入力全体が1つの
  // 区間になる。ModeStackには触れない――これはトップレベルの
  // エントリポイントである。既に分割済みの区間へ再帰するには
  // splitSelected()を使う。
  //
  // 素のstd::stringではなくStyledLine(2026-08-05): sourceLinesは
  // 今では各行のテキストと一緒にPipeBuffer自身のBold/Underline
  // AttributeRunも運ぶため、CutContent自身の表示される区間内容
  // (下のsegmentContent())が、静かに失うのではなく、FileModeが既に
  // 行っているのと「同じ」nroff風のオーバーストライク装飾を表示
  // できる――HELP.jp自身の節見出しはまさにこの仕組みで太字になって
  // いる。grepのマッチング自体(素のテキストに対して実行される
  // fgrep/egrep)には影響しない――子プロセスへ渡されるのは.textだけで、
  // legacy自身の「オーバーストライクを取り除いたテキストに対して
  // マッチングする」という挙動と同じ。
  bool split(const std::vector<StyledLine> &sourceLines, const std::string &pattern, GrepSyntax syntax);

  const std::string &pattern() const;
  const std::vector<CutSegment> &segments() const;
  // 2026-10-02追加(Application::startKey()): 最初の一致より前の行をまとめた「--」の区間があれば除く。
  void dropLeadingFiller();
  // 次のsplit()/pushSplit()の一覧だけ、行の反転表示をラベルの中の`marker`の手前までにする(2026-10-11、-KのKEYの
  // 「：」、ContentView::Line::highlightEndColumn参照)。その次の分割では呼び直さない限り行末までに戻る。
  void setLabelHighlightEndMarker(const std::string &marker) { nextLabelHighlightEndMarker_ = marker; }
  // 2026-10-01追加: 区間の並びが変わる(split()・goBack())たびに増える番号。Applicationが区間ごとの
  // 中身のカーソル位置(cutSegmentViews_)を、同じ並びの間だけ使うためのもの。
  int generation() const { return generation_; }
  const CutSegment *selectedSegment() const;
  // 2026-09-30追加(EpubLayoutHints::chapterLabel参照): 区間indexの一覧での名前を置き換える
  // (一覧の行も)。ルビの装飾は落とす。
  void setSegmentLabel(std::size_t index, const std::string &label);

  // `segment`を構成する行群、split()に渡されたsourceLinesから
  // 切り出したもの――属性も含む、split()自身のコメント参照。
  std::vector<StyledLine> segmentContent(const CutSegment &segment) const;

  // 現在選択中の区間自身の内容を、新しいパターンで再分割する。その前に
  // 現在の分割状態をModeStackへpushし、goBack()でそこへ戻れるように
  // する。選択が無い、または新しい分割自体が失敗した場合はfalse
  // (状態は変化しない)。
  bool splitSelected(const std::string &pattern, GrepSyntax syntax);

  // splitSelected()と似ているが、「現在選択中の区間自身の内容」では
  // なく、任意の「外部から」与えられたsourceLinesを分割する――
  // splitSelected()自身の実装は、今ではこれに
  // segmentContent(*selectedSegment())をソースとして渡したものに
  // すぎない。HELP(^J^H)のために追加された: CutModeは、本物の
  // ユーザーのCUTセッションとHELP画面の両方で再利用される単一の共有
  // インスタンスであるため(どちらも、突き詰めれば「あるテキストを
  // 見出しパターンで分割し、ユーザーに節を選ばせる」――menu.cの
  // M_help/T_helpケースにより、legacyが両方に同じinit_cut2()を呼んで
  // いることからの移植)、pushSplit()により、HELPは、既に進行中かも
  // しれない本物のCUTセッションを踏みつぶすことなく、新しいトップ
  // レベルの分割経由で自分「自身」の独立した内容を表示できる――
  // (既に存在する)goBack()は、他のどのネストした再分割の場合とも
  // 全く同じようにそこへ戻り、CutMode自体にHELP専用のコードは一切
  // 不要である。
  bool pushSplit(const std::vector<StyledLine> &sourceLines, const std::string &pattern, GrepSyntax syntax);

  // 直近のsplitSelected()呼び出しで保存された分割状態へ戻る。既に
  // トップレベルにいる場合(戻る先が無い)はfalse。
  bool goBack();
  bool canGoBack() const;

  // 「1段上」でアクティブだったパターン(つまり直近のsplitSelected()/
  // pushSplit()呼び出しより前のもの)、トップレベルではnullopt。
  // modeStack_自身の先頭フレームを破壊せずに覗き見る――ネストした
  // 分割におけるモードライン自身の「CUT:<親のパターン>」トレーリング
  // フィールドのためのもの(2026-08-05にユーザーが確認した例: "man"で
  // 分割し、マッチを選択し、それを"usilic"でさらに分割 -> モードライン
  // のトレーリングは"usilic"自身ではなく「親」レベルのパターンである
  // "CUT:man"と表示される――現在のレベルの説明ではなく、このネスト
  // した一覧がどの外側の分割の中にあるかを示すパンくず)。
  std::optional<std::string> parentPattern() const;

 private:
  struct SavedState {
    std::string pattern;
    std::vector<StyledLine> sourceLines;
    std::vector<CutSegment> segments;
    int cursorIndex = 0;
  };

  ContentView &view_;
  ModeStack &modeStack_;
  const Configuration &configuration_;

  std::string pattern_;
  std::string labelHighlightEndMarker_;      // setLabelHighlightEndMarker()参照(今の一覧のもの)
  std::string nextLabelHighlightEndMarker_;  // 同(次の分割のもの)
  ContentView::Line labelViewLine(const CutSegment &seg) const;
  std::vector<StyledLine> sourceLines_;
  std::vector<CutSegment> segments_;
  int generation_ = 0;  // generation()参照
};

}  // namespace fm
