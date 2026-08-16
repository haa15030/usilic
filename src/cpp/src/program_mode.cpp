#include "program_mode.hpp"

#include "child_process.hpp"

namespace fm {

ProgramMode::ProgramMode(KeyInput &keyInput, History &history) : keyInput_(keyInput), history_(history) {}

int ProgramMode::run(const std::string &command, const std::string &recordedCommand) {
  if (command.empty()) return -1;

  int status;
  if (foregroundLauncher_ && foregroundLauncher_(command)) {
    status = 0;
  } else {
    keyInput_.leaveRawMode();
    status = ChildProcess::runInteractive({"sh", "-c", command});
    keyInput_.reenterRawMode();
  }

  history_.pushRecent(RingCategory::ShellCommand, recordedCommand.empty() ? command : recordedCommand);
  return status;
}

void ProgramMode::setForegroundLauncher(std::function<bool(const std::string &)> launcher) {
  foregroundLauncher_ = std::move(launcher);
}

}  // namespace fm
