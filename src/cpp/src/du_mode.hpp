#pragma once

#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"

namespace fm {

struct DuEntry {
  std::string path;
  long sizeKb = 0;
  bool isDirectory = false;
};

// ディスク使用量モード(T_du): あるディレクトリに対して`du`を実行し、
// 各ファイル/サブディレクトリのサイズを表示する。mode.cのT_duケース
// からの移植――そのケース自体は、`du`呼び出し(実際のコマンドラインは
// 別の場所で既に解決済み、menu.cの`M_du`プロンプト入力、このクラスの
// 対象外――後述)を取り巻く、ほとんどがパス文字列の管理(結果を
// T_dirに戻すかT_fileに戻すかの決定、chdir()、モードラインのパンくず
// のフォーマット)である。DuModeは、FileMode/ArchiveModeと同様、
// ほとんどが配線作業に過ぎない: Configurationが既に`du`のコマンド
// ラインを解決し(commandFor(ConfiguredRole::Du))、ChildProcessが
// 既にそれを実行するので、このクラスの本当の仕事は`du`のタブ区切り
// "<size>\t<path>"出力をエントリへパースして表示することだけである。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - M_duプロンプト(どのディレクトリを計測するか入力/確認する)と、
//     DUのエントリを開いた結果がDirectoryModeとFileModeのどちらへ
//     着地するかの決定: Mode/Applicationの仕事、この移植の他の全
//     `*Mode`クラスが既に引いているのと同じ引き渡しの境界。
//   - chdir()/カレントディレクトリの管理: legacyのT_duケースは、
//     ディレクトリを計測する副作用としてプロセス全体をchdir()する。
//     ここでは再現していない(隠れたグローバル状態を持たない――
//     この点についてのこのプロジェクトの一貫した方針は例えば
//     Configuration/Shell自身のクラスコメント参照)。
class DuMode {
 public:
  DuMode(ContentView &view, History &history, const Configuration &configuration);

  // path に対して`du`を実行し(Configuration::commandFor(Du) +
  // isDirectory判定のための実際のファイルシステム種別チェック経由)、
  // 結果を表示する。このパスに対して記憶済みのカーソル位置が
  // Historyにあればそれを復元する。コマンドの実行に失敗した場合は
  // false。
  bool loadUsage(const std::string &path);

  // 2026-09-19追加、直接のユーザー依頼("LOCATEと同じく^Gでduを中断し、
  // 構築中断時点のDUリストへ移動"): loadUsage()自身の実行(argv構築+
  // ChildProcess::run())と表示(パース+setLines())を分離する――
  // Applicationが自前でChildProcess::startCapturing()/readAvailable()/
  // terminate()経由の非同期実行を行い、完了時であれ^Gによる中断時で
  // あれ、その時点までに集まった出力をここへ渡して表示させる。
  // loadUsage()自身は、この2つを単純にブロッキングで繋いだ薄い
  // ラッパーとして残っている(test_du_mode.cpp自身の既存の単体テスト
  // 群がまさにこのブロッキングな形に依存している)。
  std::vector<std::string> commandArgvFor(const std::string &path) const;
  bool loadUsageFromOutput(const std::string &path, const std::string &output);

  const std::string &currentPath() const;
  const std::vector<DuEntry> &entries() const;
  const DuEntry *selectedEntry() const;

  void saveCursorPosition();

 private:
  // 2026-09-10追加、直接のユーザー依頼("DU画面でのファイルサイズ
  // エリアを切り離すならファイル名表示の開始位置を揃えて欲しい"):
  // サイズの桁数はエントリごとにばらつく("4K"対"123456K")ので、
  // `sizeWidth`(その一覧全体での最大桁数、loadUsage()が算出する)
  // 分だけサイズを右詰めし、どの行でもファイル名が同じ列から
  // 始まるようにする。
  static std::string formatEntryLine(const DuEntry &entry, int sizeWidth);

  ContentView &view_;
  History &history_;
  const Configuration &configuration_;

  std::string currentPath_;
  std::vector<DuEntry> entries_;
};

}  // namespace fm
