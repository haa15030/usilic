#pragma once

#include <string>
#include <utility>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"

namespace fm {

// fgrep(固定文字列)、egrep(拡張正規表現)、grep(基本正規表現) --
// 2026-09-05追加(直接のユーザー依頼、"新たにGREP（grep）を新設"):
// BasicはGrepMode専用(application.cppのgrepSyntaxLabel()/
// grepSyntaxRole()自身のコメント参照)――CutMode自身のcutSyntax_は
// このFixed/Regexの2値のままで変わらない。
enum class GrepSyntax { Fixed, Regex, Basic };

struct GrepMatch {
  int sourceLineIndex = -1;  // search()に渡された行群への0始まりインデックス
  std::string text;
  // 2026-09-10追加、直接のユーザー指摘("行全体がそれでは何にマッチ
  // したかもわからない...「マッチした文字列」だけをハイライト表示
  // には"): `text`内での、実際に一致した部分文字列のバイト範囲
  // ([start, end)、複数マッチがあれば複数要素)。search()自身の実装
  // コメント参照(`-o`を追加した2回目の呼び出しで求める)。取得に失敗
  // した場合(まれ)は空のままになるので、呼び出し元は空の場合に行
  // 全体をハイライトするフォールバックを持つべき。
  std::vector<std::pair<int, int>> matchSpans;
};

// 検索モード(FGREP/EGREP): 行の集合をパターンで検索し、一致した行を
// 表示する。fork.cのfork_grep()からの移植――意外にも、これは手作りの
// マッチングループでは全く無い: 実物の`fgrep`/`egrep`バイナリを
// forkし、検索対象バッファの各行をその標準入力へ書き込み、一致した
// 行を標準出力から捕捉する。ChildProcess(パイプ経由の標準入力、
// 既に構築済み)とConfiguration(fgrep/egrepのコマンドライン、既に
// 構築済み)が今や存在し、それをきれいに行えるので、GrepModeはまさに
// その形をほぼそのまま移植したもの: search()は`-n`(Configurationの
// Fgrep/Egrepロールコマンドに既に焼き込み済み)付きでChildProcess::
// run()経由により本物のバイナリを実行し、各出力行が
// "<1始まりの行番号>:<一致テキスト>"となるため、一致箇所を元の行
// インデックスへそのまま対応付けられる。
//
// これはmode()のM_egrep/M_fgrep/T_grep処理の逐語的な移植ではない。
// legacy版は、ModeStack自身のクラスコメントで既に説明・先送りした
// 2本並行スタックの協調処理(「STACKフレームの中身を奪ってGREP
// ノードの種にする」という一連の動作)と絡み合っている――GrepMode
// 自体は、一致箇所が開かれた時や検索が破棄された時に何が起きるかに
// ついて一切意見を持たない。スコープについてはクラスコメント参照。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - ネストした検索(既に絞り込み済みの結果集合の中をさらに検索
//     する)と、検索前に見ていたものへ戻るためのModeStack/GREP連鎖の
//     協調処理: Mode/Applicationの仕事、DirectoryMode/FileMode/
//     ArchiveModeが自分自身の引き渡しについて既に引いているのと
//     同じ境界――GrepModeはModeStackにもHistoryにも一切触れない。
//   - 行内で一致した部分文字列のハイライト(legacyのT_grep描画は
//     実際の一致箇所を太字にする): ContentViewの文字単位の属性範囲は
//     まだ存在しない(ContentView自身のクラスコメントで既にギャップ
//     として印を付けてある)――このクラスは一致した行をただ素の
//     まま表示するだけ。
//   - 検索パターンをHistoryのhfgrep/hegrep ringへ記録すること:
//     Mode/Applicationの仕事(GrepModeの兄弟クラスが、自分が直接
//     所有するカーソル位置カテゴリ以外の点でHistoryに一切触れない
//     のと同じ)。
class GrepMode {
 public:
  GrepMode(ContentView &view, const Configuration &configuration);

  // sourceLinesを本物のfgrep/egrepバイナリ(Configuration::
  // commandFor(Fgrep/Egrep) + ChildProcess、sourceLinesをパイプ経由の
  // 標準入力として与える)でpatternについて検索する。一致結果を表示
  // する(0件のこともある――grepが「一致無し」で終了するのは検索の
  // 失敗ではない)。falseになるのは、検索コマンド自体が実行できな
  // かった場合(バイナリが無い、ツール自身が拒否した不正なパターン、
  // 等)、またはpatternが空の場合のみ。
  bool search(const std::vector<std::string> &sourceLines, const std::string &pattern, GrepSyntax syntax);

  // 2026-09-10追加、直接のユーザー依頼("インクリメンタルサーチについて
  // はgrepではなく...文字列位置を返す関数等だけでインプリメントは
  // 可能?"): search()とは異なり、実物の`grep`をforkしない――キー入力
  // のたびに呼んでも十分軽い、in-processでの文字列検索。
  // 2026-09-10同日さらに修正(直接のユーザー報告、実機`usilic -c
  // ~/git/usilic`で再現: "^[iでは正規表現検索ではなくfgrepタイプに
  // してほしい。でないと.cloudと打つ場合、最初の.で全てがマッチして
  // しまい、全面が黄色と化す"): 初版はPOSIX`<regex.h>`
  // (`regcomp()`/`regexec()`)によるBRE(基本正規表現)解釈だったが、
  // これだと"."が「任意の1文字」を意味してしまいユーザーの直感に
  // 反するため、fgrep相当のリテラル部分文字列検索(`std::string::
  // find()`)へ差し替えた――GrepSyntax::Basic(正規表現)とは別物で、
  // インクリメンタルサーチはこの`searchInProcess()`が常にリテラル
  // 検索という1本の挙動に統一されている(切り替えの余地は今回のスコープ
  // 外)。matches()/matchSpans()の中身はsearch()と同じ形で埋まる――
  // 呼び出し元(Application::liveIncrementalGrepSearch())はsearch()と
  // 全く同じように読める。search()とは異なり、view_(一覧画面)には
  // 一切触れない――呼び出し元は一覧へ切り替えず、in-place方式
  // (activateGrepInPlace())でmatches()を直接使うため。
  bool searchInProcess(const std::vector<std::string> &sourceLines, const std::string &pattern);
  // falseの間、search()は結果をビューへ入れるだけで描かない(呼び出し元が縦書きにして描く、2026-10-10)。
  void setRedrawOnSearch(bool redraw) { redrawOnSearch_ = redraw; }

  // 検索せずに一覧を出す(2026-10-13、EPUBの記録の一覧、Application::startEpubMarkList())。labelはpattern()になる
  // (一致が無ければ「label: No String」)。
  void setMatches(std::vector<GrepMatch> matches, const std::string &label);

  const std::string &pattern() const;
  const std::vector<GrepMatch> &matches() const;
  const GrepMatch *selectedMatch() const;

 private:
  void showMatches();  // matches_を一覧(view_)に出す(search()・setMatches()の後半)

  ContentView &view_;
  const Configuration &configuration_;

  std::string pattern_;
  std::vector<GrepMatch> matches_;
  bool redrawOnSearch_ = true;
};

}  // namespace fm
