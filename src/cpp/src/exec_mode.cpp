#include "exec_mode.hpp"

#include "child_process.hpp"

namespace fm {

ExecMode::ExecMode(KeyInput &keyInput) : keyInput_(keyInput) {}

int ExecMode::run(const ProgramCommand &command, const std::string &path) {
  if (command.argv.empty()) return -1;
  std::vector<std::string> argv = command.argv;
  argv.push_back(path);

  keyInput_.leaveRawMode();
  int status = ChildProcess::runInteractive(argv);
  keyInput_.reenterRawMode();
  return status;
}

}  // namespace fm
