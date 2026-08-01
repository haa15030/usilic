#include "cp_mv_rm_mode.hpp"

#include <sys/stat.h>
#include <unistd.h>

namespace fm {

CpMvRmMode::CpMvRmMode(const Configuration &configuration, KeyInput &keyInput)
    : configuration_(configuration), keyInput_(keyInput) {}

bool CpMvRmMode::isWritable(const std::string &path, WriteCheck check) {
  if (getuid() == 0) return true;

  struct stat linkSt {};
  bool symlink = (lstat(path.c_str(), &linkSt) == 0) && S_ISLNK(linkSt.st_mode);

  struct stat st {};
  bool exists = (stat(path.c_str(), &st) == 0);

  if (check == WriteCheck::MustBeDirectory) {
    if (!exists) return false;
    if (!S_ISDIR(st.st_mode)) return false;
  } else if (check == WriteCheck::MissingIsWritable) {
    if (!exists) return true;
  }

  if (symlink) return true;
  if (access(path.c_str(), W_OK) == 0) return true;
  if (exists && st.st_uid == getuid()) return true;
  return false;
}

std::vector<std::string> CpMvRmMode::buildArgv(ConfiguredRole role, const std::vector<std::string> &sources,
                                                const std::string *destination) const {
  if (sources.empty()) return {};
  ProgramCommand command = configuration_.commandFor(role);
  if (command.argv.empty()) return {};

  std::vector<std::string> argv = command.argv;
  for (const auto &source : sources) argv.push_back(source);
  if (destination) argv.push_back(*destination);
  return argv;
}

ChildProcess::Result CpMvRmMode::runWithSources(ConfiguredRole role, const std::vector<std::string> &sources,
                                                 const std::string *destination) {
  std::vector<std::string> argv = buildArgv(role, sources, destination);
  if (argv.empty()) return {};
  return ChildProcess::run(argv);
}

int CpMvRmMode::runInteractiveWithSources(ConfiguredRole role, const std::vector<std::string> &sources,
                                           const std::string *destination) {
  std::vector<std::string> argv = buildArgv(role, sources, destination);
  if (argv.empty()) return -1;

  keyInput_.leaveRawMode();
  int status = ChildProcess::runInteractive(argv);
  keyInput_.reenterRawMode();
  return status;
}

ChildProcess::Result CpMvRmMode::copyTo(const std::vector<std::string> &sources, const std::string &destination) {
  return runWithSources(ConfiguredRole::Cp, sources, &destination);
}

ChildProcess::Result CpMvRmMode::moveTo(const std::vector<std::string> &sources, const std::string &destination) {
  return runWithSources(ConfiguredRole::Mv, sources, &destination);
}

ChildProcess::Result CpMvRmMode::remove(const std::vector<std::string> &targets) {
  return runWithSources(ConfiguredRole::Rm, targets, nullptr);
}

int CpMvRmMode::copyToInteractive(const std::vector<std::string> &sources, const std::string &destination) {
  return runInteractiveWithSources(ConfiguredRole::Cp, sources, &destination);
}

int CpMvRmMode::moveToInteractive(const std::vector<std::string> &sources, const std::string &destination) {
  return runInteractiveWithSources(ConfiguredRole::Mv, sources, &destination);
}

int CpMvRmMode::removeInteractive(const std::vector<std::string> &targets) {
  return runInteractiveWithSources(ConfiguredRole::Rm, targets, nullptr);
}

int CpMvRmMode::runInteractive(const std::vector<std::string> &argv) {
  if (argv.empty()) return -1;
  keyInput_.leaveRawMode();
  int status = ChildProcess::runInteractive(argv);
  keyInput_.reenterRawMode();
  return status;
}

ChildProcess::Result CpMvRmMode::run(const std::vector<std::string> &argv) {
  if (argv.empty()) return {};
  return ChildProcess::run(argv);
}

}  // namespace fm
