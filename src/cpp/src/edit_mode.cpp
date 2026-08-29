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

  if (foregroundLauncher_ && foregroundLauncher_(argv)) {
    // launcher側で全て処理済み
  } else {
    keyInput_.leaveRawMode();
    ChildProcess::runInteractive(argv);
    keyInput_.reenterRawMode();
  }

  auto after = std::filesystem::last_write_time(path, ec);
  if (ec) return false;
  return after != before;
}

void EditMode::setForegroundLauncher(std::function<bool(const std::vector<std::string> &)> launcher) {
  foregroundLauncher_ = std::move(launcher);
}

}  // namespace fm
