#include "cp_mv_rm_mode.hpp"

#include <sys/stat.h>
#include <unistd.h>

namespace fm {

CpMvRmMode::CpMvRmMode(const Configuration &configuration) : configuration_(configuration) {}

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

ChildProcess::Result CpMvRmMode::runWithSources(ConfiguredRole role, const std::vector<std::string> &sources,
                                                 const std::string *destination) {
  if (sources.empty()) return {};
  ProgramCommand command = configuration_.commandFor(role);
  if (command.argv.empty()) return {};

  std::vector<std::string> argv = command.argv;
  for (const auto &source : sources) argv.push_back(source);
  if (destination) argv.push_back(*destination);

  return ChildProcess::run(argv);
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

}  // namespace fm
