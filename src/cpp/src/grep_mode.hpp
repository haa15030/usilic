#pragma once

#include <string>
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

  const std::string &pattern() const;
  const std::vector<GrepMatch> &matches() const;
  const GrepMatch *selectedMatch() const;

 private:
  ContentView &view_;
  const Configuration &configuration_;

  std::string pattern_;
  std::vector<GrepMatch> matches_;
};

}  // namespace fm
