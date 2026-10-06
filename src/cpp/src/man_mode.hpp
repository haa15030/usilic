#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"

namespace fm {

// MANページモード(T_man): `man <name>`を実行しその出力を表示する。
// init.cのinit_man()/fork_man()からの移植――蓋を開けてみればこれは
// 単に`man`(1)を実行し、その出力を、プレーンテキストファイルを
// 閲覧する場合と全く同じnroffオーバーストライク太字検出/文字コード
// 変換/折り返しの仕組み(alloc_pipe()、既にPipeBufferとして移植済み)
// に通しているだけだった――manページは、例えばこのリポジトリ自身の
// doc/README.jpと同じバックスペース・オーバーストライクの慣習を
// 使っている(PipeBuffer自身のクラスコメントとCLAUDE.mdの作業ログ
// 8番参照)。これによりManModeは構造的にFileModeと同一になり、違いは
// ファイル読み込みではなくコマンドの捕捉した出力からバイト列を得る
// 点だけである――詳細は.cpp参照。
//
// 意図的に除外(将来の課題、この段階では対象外): fork_man()が特別
// 扱いしていた`ENVVAR=value manpage`という接頭辞形式(`man`実行前に
// 環境変数を1つ設定する、例えばMANSECTION用)――かなり限定的な用途で
// あり、このプロジェクトには実際に使われていたという証拠が無いため、
// 今きれいなAPIを推測で作るのは憶測に過ぎる。ここで対応しているのは
// 素のページ名のみ。
class ManMode {
 public:
  ManMode(ContentView &view, History &history, const Configuration &configuration, Charset::Encoding encoding,
          int width);

  // `man name`を実行し結果を表示する。このページに対して記憶済みの
  // カーソル位置がHistoryにあればそれを復元する
  // (PositionCategory::ManPage、legacy自身の`ll.p.man`カテゴリの
  // 下に保存)。コマンドの実行に失敗した場合(該当するmanページが
  // 無い、`man`が未設定/未インストール)はfalse。
  bool loadPage(const std::string &name);

  const std::string &currentPage() const;
  void saveCursorPosition();

  // K-27 (^Oa): FileMode::setEncoding()自身のコメント参照――ここでも
  // 同じ形(影響するのは「次の」loadPage()/reflow()であり、既に
  // 表示されている内容ではない)。
  void setEncoding(Charset::Encoding encoding);
  // `man`へ渡す直前に名前を差し替える関数(2026-10-06、Application自身の"usilic"の扱い参照)。空を返せば
  // 名前のまま。モードライン・履歴・位置の記憶は名前のまま(差し替えた先のパスは出さない)。
  void setPageResolver(std::function<std::string(const std::string &)> resolver) { pageResolver_ = std::move(resolver); }

  // `man currentPage()`を再実行し、その出力をnewWidthで再度折り返す。
  // 端末サイズ変更用。共有している設計についてはFileMode::reflow()
  // 自身のコメント参照(alloc_winch()の逐語的な移植ではなく、
  // 現在セッションのカーソルはHistory参照ではなくクランプで保持)
  // ――ここでの唯一の違いは、再度折り返す元となるキャッシュ済みの
  // 生の`man`出力が無いため、ファイルを再読み込みするのではなく本当に
  // `man`自体を再実行する点で、これは隠そうとしていない実際の
  // (明示された)コストである。現在ページが読み込まれていない場合、
  // または`man`の再実行に失敗した場合はfalse(何もしない)。
  bool reflow(int newWidth);

  // FileMode::setWidth()自身のコメント参照(2026-08-30、同種のバグ):
  // Application::handleResize()がManモード以外がアクティブな間も
  // width_を追随させておくための、redraw無しの軽量な更新。
  void setWidth(int newWidth) { width_ = newWidth; }

 private:
  std::optional<std::vector<ContentView::Line>> runAndWrap(const std::string &name) const;
  std::function<std::string(const std::string &)> pageResolver_;  // setPageResolver()参照

  ContentView &view_;
  History &history_;
  const Configuration &configuration_;
  Charset::Encoding encoding_;
  int width_;
  std::string currentPage_;
};

}  // namespace fm
