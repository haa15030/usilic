#include "locate_mode.hpp"

#include <sstream>
#include <unordered_set>

#include "child_process.hpp"
#include "unicode_normalize.hpp"

namespace fm {

namespace {

// `role`のコマンドを、`query`を末尾に追加して一度だけ実行し、その
// 出力行を返す(重複除去はここでは一切行わない――呼び出し元がマージ
// する)。
std::vector<std::string> runLocateRoleOnce(const Configuration &configuration, ConfiguredRole role,
                                            const std::string &query, bool errorsToDevNull) {
  ProgramCommand command = configuration.commandFor(role);
  if (command.argv.empty()) return {};
  std::vector<std::string> argv = command.argv;
  argv.push_back(query);
  static const std::string kDevNull = "/dev/null";
  ChildProcessOptions options;
  if (errorsToDevNull) options.stderrLogPath = &kDevNull;
  ChildProcess::Result result = ChildProcess::run(argv, options);
  // 本物の`locate`は、単に何も見つからなかった時に終了コード1を返す
  // (あるいはLocateUserDbについては、初回のビルド前にデータベース
  // ファイルがまだ存在しない時にも)――実行そのものの本当の失敗
  // (あるいは本物のエラー終了)だけを、この検索の失敗として扱う。
  // GrepMode/CutMode自身のfgrep/egrepの終了ステータスの扱いと
  // 同じ規約である。
  if (result.exitStatus < 0 || result.exitStatus > 1) return {};

  std::vector<std::string> lines;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line))
    if (!line.empty()) lines.push_back(line);
  return lines;
}

// `query`のUnicode正規化の候補ごとに`role`を一度ずつ実行し
// (unicodeNormalizationCandidates()自身のコメント参照――2026-08-23
// 追加、docs/CHANGES.md項目407、実際のユーザー報告: LOCATEが、
// ディスク上では分解済みだった実在のiCloud Driveのファイルを見逃した
// ――同じIMEがFGREPへ入力した際に通常の合成済みだったqueryとバイト
// レベルで一致しなかったためで、これは全く同じ盲点にぶつかっていた)、
// 全ての候補の結果をマージする(GrepMode/CutMode自身の「最初にマッチ
// した候補が勝つ」方式とは異なる――ここでは`locate`のデータベース
// エントリが、queryとは全く無関係なファイルについても、ある候補では
// 正当にマッチしても別の候補ではマッチしないことがあり得るので、
// 単純な和集合の方が簡潔で安上がりである: roleごとに高速な`locate`
// の呼び出しは最大でも3回で済む)。
std::vector<std::string> runLocateRole(const Configuration &configuration, ConfiguredRole role,
                                        const std::string &query, bool errorsToDevNull) {
  std::vector<std::string> merged;
  std::unordered_set<std::string> seen;
  for (const auto &candidate : unicodeNormalizationCandidates(query)) {
    for (auto &path : runLocateRoleOnce(configuration, role, candidate, errorsToDevNull))
      if (seen.insert(path).second) merged.push_back(std::move(path));
  }
  return merged;
}

}  // namespace

LocateMode::LocateMode(ContentView &view, const Configuration &configuration)
    : view_(view), configuration_(configuration) {}

bool LocateMode::search(const std::string &query) {
  if (query.empty()) return false;

  std::vector<std::string> results = runLocateRole(configuration_, ConfiguredRole::Locate, query, false);
  bool systemConfigured = !configuration_.commandFor(ConfiguredRole::Locate).argv.empty();
  std::vector<std::string> userResults = runLocateRole(configuration_, ConfiguredRole::LocateUserDb, query, true);
  // どちらのデータベースも全く設定されていない: この関数がLocateUserDb
  // についても知る前から、ConfiguredRole::Locateが無い場合にsearch()が
  // 常に返していたのと同じ「実行できなかった」失敗である――変更無し。
  // LocateUserDbが未設定であること(既存の全テスト、そして非macOS
  // ビルドの全て)が、本物のLocateの失敗を偽の成功に変えてしまうことは
  // 決して無い。
  if (!systemConfigured && configuration_.commandFor(ConfiguredRole::LocateUserDb).argv.empty()) return false;

  if (!userResults.empty()) {
    std::unordered_set<std::string> seen(results.begin(), results.end());
    for (auto &path : userResults)
      if (seen.insert(path).second) results.push_back(std::move(path));
  }

  query_ = query;
  results_ = std::move(results);

  std::vector<ContentView::Line> lines;
  lines.reserve(results_.size());
  // ellipsisEligible=true: Line::ellipsisEligible自身のコメント参照
  // ――各結果は個々の独立したパスであり、まさにこの機能が想定して
  // 設計された「Finder風の中間省略」のケースそのものである。
  for (const auto &path : results_) lines.push_back({path, std::nullopt, std::nullopt, {}, true});
  view_.setLines(std::move(lines));
  view_.setCursorIndex(0);
  view_.redraw();
  return true;
}

namespace {

// application.cpp自身のshellQuoteArg()/configuration.cpp自身の
// shellQuoteSinglePath()と同じPOSIXシングルクォートのエスケープ形式
// ――その2つと同様、別々の翻訳単位であり共有はしていない。
std::string shellQuoteArgForUpdateDb(const std::string &arg) {
  std::string quoted = "'";
  for (char c : arg) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return quoted;
}

std::string shellQuoteArgvForUpdateDb(const std::vector<std::string> &argv) {
  std::string line;
  for (const auto &arg : argv) {
    if (!line.empty()) line += " ";
    line += shellQuoteArgForUpdateDb(arg);
  }
  return line;
}

}  // namespace

pid_t LocateMode::startUpdateDb() {
  ProgramCommand systemCommand = configuration_.commandFor(ConfiguredRole::UpdateDb);
  ProgramCommand userCommand = configuration_.commandFor(ConfiguredRole::LocateUserDbBuild);
  if (systemCommand.argv.empty() && userCommand.argv.empty()) return -1;
  if (userCommand.argv.empty()) return ChildProcess::runDetached(systemCommand.argv);
  if (systemCommand.argv.empty()) return ChildProcess::runDetached(userCommand.argv);
  // 両方設定されている場合(macOS、Configuration::
  // setUserLocateDatabasePath()が呼ばれている): 1つの切り離された子
  // プロセスへ連結する――理由はこの関数自身のヘッダーコメント参照
  // (Applicationの単一pidの待機/ポーリングの仕組みに変更が要らない
  // ため)。userCommand.argvは既にそれ自身の`{"sh", "-c", "<script>"}`
  // である(Configuration::setUserLocateDatabasePath())――これを
  // この「外側」の`sh -c`の中で3つの別々の単語としてクォートし
  // 繋ぎ直し、その外側のシェルにパース+実行させることは、直接実行
  // するのと全く等価である。これは、このポートの他の場所で
  // Application::shellQuoteArgv()の呼び出し元が既に頼っているのと
  // 同じ、argvからコマンドラインを安全に再構築するトリックである。
  std::string combined = shellQuoteArgvForUpdateDb(systemCommand.argv) + " ; " + shellQuoteArgvForUpdateDb(userCommand.argv);
  return ChildProcess::runDetached({"sh", "-c", combined});
}

const std::string &LocateMode::query() const { return query_; }
const std::vector<std::string> &LocateMode::results() const { return results_; }

const std::string *LocateMode::selectedResult() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(results_.size())) return nullptr;
  return &results_[idx];
}

}  // namespace fm
