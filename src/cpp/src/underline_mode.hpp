#pragma once

#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// UNDERモード(2026-09-20新設): `^Ou`でFile-selectorのアンダーライン
// (マーク済み、無ければカーソル選択中の1件)通常ファイル一覧を表示する
// 専用の一覧モード。LocateMode(`locate`の生の結果パス一覧、一切の
// パースが不要)と全く同型――唯一の違いは、検索の実行(search())が
// 無く、Application側が既に集めた絶対パス一覧をそのまま受け取るだけ
// であること。
//
// 意図的に除外(この移植の他の全`*Mode`クラスと同じ引き渡しの境界):
// 選択されたエントリを開く処理、`^Ou`発動時のマーク済みエントリの
// 収集(Application::collectTargetFilePaths())。
//
// 2026-09-20設計変更の経緯: 初回実装(撤回済み)は既存のCLI複数位置
// 引数起動用の「ページャーファイル一覧」(pagerFileList_/
// navigateToPagerFile())を再利用し、';'/':'をdispatchByte()で横取り
// する形だった。しかしそれは(1)`FileMode::pagerMode_`という、'q'が
// usilicプロセス全体を終了させる等の外部ページャー的挙動を伴う仕組み
// に相乗りしてしまう、(2)';'/':'本来のCommand::JumpNext/JumpPrevious
// (Back+Move+Enterマクロ)を隠してしまう、という2点で筋が悪いとの
// ユーザー指摘を受け、CUT/LOCATE/DUと同型の独立した一覧モードへ
// 作り直した。専用モードにすることで、';'/':'は本来のJumpNext/
// JumpPrevious「そのまま」(横取り不要)で、この一覧「だけ」を巡回
// できる(Back(^G)がこの一覧へ戻ることを、Du/Locateと同じ
// fileReturnMode_の仕組みで保証するため)。詳細は
// .steering/20260920-underline-mode-redesign/design.md参照。
class UnderlineMode {
 public:
  explicit UnderlineMode(ContentView &view);

  // `paths`(絶対パス、通常ファイルのみ、呼び出し側が既に収集済み)を
  // 一覧表示する。
  void show(std::vector<std::string> paths);

  const std::vector<std::string> &entries() const;
  const std::string *selectedEntry() const;

 private:
  ContentView &view_;
  std::vector<std::string> entries_;
};

}  // namespace fm
