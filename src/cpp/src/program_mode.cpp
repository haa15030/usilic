#include "program_mode.hpp"

#include "child_process.hpp"

namespace fm {

ProgramMode::ProgramMode(KeyInput &keyInput, History &history) : keyInput_(keyInput), history_(history) {}

int ProgramMode::run(const std::string &command) {
  if (command.empty()) return -1;

  keyInput_.leaveRawMode();
  int status = ChildProcess::runInteractive({"sh", "-c", command});
  keyInput_.reenterRawMode();

  history_.pushRecent(RingCategory::ShellCommand, command);
  return status;
}

}  // namespace fm
