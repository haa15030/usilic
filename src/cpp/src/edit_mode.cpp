#include "edit_mode.hpp"

#include <filesystem>

#include "child_process.hpp"

namespace fm {

EditMode::EditMode(KeyInput &keyInput, const Configuration &configuration)
    : keyInput_(keyInput), configuration_(configuration) {}

bool EditMode::edit(const std::string &path, int lineNumber) {
  auto argv = configuration_.editorArgv(path, lineNumber);
  if (argv.empty()) return false;

  std::error_code ec;
  auto before = std::filesystem::last_write_time(path, ec);
  if (ec) return false;

  keyInput_.leaveRawMode();
  ChildProcess::runInteractive(argv);
  keyInput_.reenterRawMode();

  auto after = std::filesystem::last_write_time(path, ec);
  if (ec) return false;
  return after != before;
}

}  // namespace fm
