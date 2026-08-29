#pragma once

#include <functional>
#include <string>
#include <vector>

#include "configuration.hpp"
#include "key_input.hpp"

namespace fm {

// EDITOR起動(mode.cのdo_edit()): 設定済みのEDITORで、指定行番号の
// 位置からファイルを開き、実端末アクセスを与え、ファイルが変更
// されたかどうかを報告して呼び出し側が再読み込みすべきか分かる
// ようにする。
//
// これは逐語的な移植ではない。do_edit()はdo_prog()やdo_exec()も
// それぞれ独立して手作りしているのと同じfork()/execvp()/wait() +
// raw termioの退出/再突入の手順を、これまた手作りしていた――今では
// ChildProcess::runInteractive()(ProgramMode用に追加)とKeyInputの
// 既存のleaveRawMode()/reenterRawMode()を使う。argv自体の組み立ては
// 既にConfiguration::editorArgv()の仕事(そちらが本物の
// "+<lineNumber>"引数を直接組み立てる。legacyの固定幅
// "+??????????"というプレースホルダをその場でパッチするトリックの
// 置き換え――Configuration自身のクラスコメント参照)なので、この
// クラスの仕事はほぼ「実行し、その後ファイルの内容が変わったか
// 確認する」だけである。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - そもそも編集が許されるかどうかのモード文脈上のガード
//     (do_edit()の「アーカイブ/cut/split中のバッファ閲覧中は不可」
//     「file-manager modeではパスが絶対パスでなければならない」と
//     いったチェック): edit()を呼ぶ前にそれを強制するのは
//     Mode/Applicationの仕事、この移植の他の全`*Mode`クラスが既に
//     引いているのと同じ引き渡しの境界。
//   - SIGCONTによるpid報告ハンドラ: FileModeの兄弟にあたる
//     ProgramModeが既に文書化しているのと同じ理由による同じ除外。
class EditMode {
 public:
  EditMode(KeyInput &keyInput, const Configuration &configuration);

  // 設定済みのEDITOR(Configuration::editorArgv())でpathを
  // lineNumberの位置から開く。編集中にファイルの最終書き込み時刻が
  // 変わっていればtrue(呼び出し側は再読み込みすべき)、変わって
  // いない、または編集の前後どちらかでファイルを問い合わせできな
  // かった場合はfalse。do_edit()のstat()経由のst_ctime比較からの
  // 移植だが、生のctimeフィールドではなくstd::filesystem::
  // last_write_time()を使っている理由は2つ: ctimeはメタデータのみの
  // 変更(パーミッション等)でも変化してしまい、それはこのチェックが
  // 本当に答えたい「内容が変わったか」ではないこと。また素の
  // stat()のst_mtimeは秒単位に切り捨てられるため粗すぎて、素早い
  // 編集(スクリプト化されたEDITOR、あるいは単に素早い本物の編集)が
  // 開始と同じ秒内に完了してしまうと検出漏れになりうる――
  // last_write_time()はファイルシステムが持つ実際のサブ秒精度を
  // そのまま運ぶ。
  bool edit(const std::string &path, int lineNumber);

  // edit()が実際にエディタのargvを「どう」実行するかを上書きする。
  // ProgramMode::setForegroundLauncher()が既に確立したのと同じ
  // 「サブクラス/フラグではなく設定可能なコールバック」という形
  // (そちら自身のコメント参照): 設定済みでtrueを返した場合、edit()は
  // フォアグラウンドへの受け渡し全体をそちらが処理し終えたと信頼し、
  // ChildProcess::runInteractive()/leaveRawMode()/reenterRawMode()を
  // 一切スキップする。falseなら通常のその経路へそのままフォール
  // バックする。ProgramMode版と全く同じ唯一の呼び出し元のために
  // 存在する――main.cppのGUIモード実行ループで、全く同じ理由
  // (`vi`/EDITOR起動は、GraphicalDisplayがウィンドウを所有している
  // 状況では「このプロセス自身が継承したstdioは実際に画面に出ている
  // ものではない」というPROGRAMモードのコマンドと全く同じ問題を
  // 抱えている)で配線されている。
  void setForegroundLauncher(std::function<bool(const std::vector<std::string> &argv)> launcher);

 private:
  KeyInput &keyInput_;
  const Configuration &configuration_;
  std::function<bool(const std::vector<std::string> &argv)> foregroundLauncher_;
};

}  // namespace fm
