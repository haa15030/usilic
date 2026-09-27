#include "exec_mode.hpp"

#include "child_process.hpp"

namespace fm {

ExecMode::ExecMode(KeyInput &keyInput, History *history) : keyInput_(keyInput), history_(history) {}

int ExecMode::run(const ProgramCommand &command, const std::string &path) {
  if (command.argv.empty()) return -1;
  std::vector<std::string> argv = command.argv;
  argv.push_back(path);

  keyInput_.leaveRawMode();
  int status = ChildProcess::runInteractive(argv);
  keyInput_.reenterRawMode();
  // 2026-09-27追加、直接のユーザー依頼("EXEC経由で開いたファイルもPROGRAMに
  // 残してほしい"、"エラーのファイルはPROGRAMの履歴に入れない"): 正常終了
  // した時だけ、クイックルックと同じ「プログラム ベース名」の形で記録する。
  if (status == 0 && history_) {
    std::vector<std::string> label = command.argv;
    std::size_t slash = path.find_last_of('/');
    label.push_back(slash == std::string::npos ? path : path.substr(slash + 1));
    std::string text;
    for (const auto &word : label) text += (text.empty() ? "" : " ") + word;
    history_->pushRecent(RingCategory::ShellCommand, text);
  }
  return status;
}

}  // namespace fm
