#pragma once

#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// リスト/履歴ブラウザ(M_list/M_hist/M_cmd/T_hfile/T_hdir/T_hman/
// T_hlocate/T_hfgrep/T_hegrep): Historyの既に組み立て済みのring/
// name-setカテゴリのどれか、またはConfigurationのgrep/programシード
// リストといった、単純な文字列一覧を表示し、ユーザーに1つ選ばせる。
// menu.cのm_list()から精神面のみ移植: m_list()自体は純粋な
// モード遷移ディスパッチ(文脈から、約9種類あるリスト状のものの
// どれを閲覧するか決めるだけ)であり、それ自体には描画ロジックが
// 無い――実際の「これらの文字列を表示し、選択位置を追跡する」仕事は
// 汎用的で約9箇所の呼び出し元全てで全く同じであり、それこそが
// このクラスが重複させずに切り出しているものだ。
//
// これはlegacyの特定の1関数を逐語的に移植したものではない――移植
// すべき対象がそもそも存在しない。init_list()/init_str()(alloc.c)は
// LIST/STR木から画面表示用のPPリストを組み立てるが、その木自体は
// 既にHistoryのstd::map/std::vector/std::dequeベースのカテゴリと
// Configurationのstd::vectorベースのシードリストに完全に置き換わって
// おり、移植すべきlegacyの木構造の走査処理は残っていない――このクラス
// はただ、呼び出し元がどこから取得したかに関わらず、結果として得られた
// フラットな文字列一覧を受け取って表示すればよい。
//
// 意図的に除外(将来の課題、この段階では対象外): 与えられたキー/
// モードに対して、History/Configurationのどの基盤カテゴリを閲覧
// するかの決定、および選択された項目をどうするか(そのディレクトリへ
// 移動する、そのファイルを開く、そのコマンドを再実行する、等):
// Mode/Applicationの仕事、この移植の他の全`*Mode`クラスが既に
// 引いているのと同じ引き渡しの境界。
class ListBrowserMode {
 public:
  explicit ListBrowserMode(ContentView &view);

  // 項目一覧を表示する(例:
  // History::recentEntries(RingCategory::RecentFile)や
  // Configuration::grepSeedPatterns()から――必要ならstd::dequeから
  // std::vectorへの変換は呼び出し側が行う)。
  //
  // `badges`(2026-08-20追加、省略可、`items`と同じ長さか空):
  // 各項目に対する「表示専用」の接尾辞(例: アーカイブと認識された
  // RecentFileパスに対する"(tar)"、Application自身のarchiveBadgeFor()
  // 参照)で、画面上では各項目自身のテキストの後ろに付加される。
  // `items_`自身とは完全に分離して保持する(selectedItem()が返すのは
  // 依然として変更されていないそのままの文字列)――このクラスは
  // バッジが何を意味するか(RingCategory、アーカイブ判定、等)を
  // 一切知らないままにする、というのが自身のクラスコメントで既に
  // 定めている方針であり、バッジを付けるかどうか・何を付けるかは
  // 呼び出し側が決め、このクラスはただそれを描画するだけ。ある
  // インデックスに対して空文字列(または`badges`ベクタ自体がそれより
  // 短い/渡されない)場合は「このインデックスにはバッジ無し」を意味し、
  // 既存の全呼び出し元の挙動を寸分違わず維持する。
  void show(const std::vector<std::string> &items, const std::vector<std::string> &badges = {});

  const std::vector<std::string> &items() const;
  const std::string *selectedItem() const;

 private:
  ContentView &view_;
  std::vector<std::string> items_;
};

}  // namespace fm
