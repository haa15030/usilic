#include "program_mode.hpp"

#include "child_process.hpp"

namespace fm {

ProgramMode::ProgramMode(KeyInput &keyInput, Display &display, History &history)
    : keyInput_(keyInput), display_(display), history_(history) {}

int ProgramMode::run(const std::string &command, const std::string &recordedCommand) {
  if (command.empty()) return -1;

  int status;
  if (foregroundLauncher_ && foregroundLauncher_(command)) {
    status = 0;
  } else {
    // Display::leaveForExternalProgram()自身のコメント
    // (docs/HISTORY.md:9023)参照: leaveRawMode()/reenterRawMode()を
    // そのまま移植しただけでは、legacyのvt_init(-2)/(2)のもう半分
    // (スクロール領域のリセット/再制限)が欠落しており、それが
    // 「最初の`less`だけ表示が崩れ、`vi`は崩れない」の真の原因だった。
    display_.leaveForExternalProgram();
    keyInput_.leaveRawMode();
    status = ChildProcess::runInteractive({"sh", "-c", command});
    keyInput_.reenterRawMode();
    display_.reenterFromExternalProgram();
  }

  history_.pushRecent(RingCategory::ShellCommand, recordedCommand.empty() ? command : recordedCommand);
  return status;
}

void ProgramMode::setForegroundLauncher(std::function<bool(const std::string &)> launcher) {
  foregroundLauncher_ = std::move(launcher);
}

}  // namespace fm
