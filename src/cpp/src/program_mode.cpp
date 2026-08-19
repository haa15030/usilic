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
    // See Display::leaveForExternalProgram()'s own comment
    // (docs/HISTORY.md:9023): a straight port of leaveRawMode()/
    // reenterRawMode() alone dropped legacy vt_init(-2)/(2)'s OTHER
    // half (resetting/re-constraining the scroll region), the actual
    // root cause of "first `less` renders broken, `vi` never does."
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
