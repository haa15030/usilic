#include "path_resolver.hpp"

#include <filesystem>

namespace fm {

PathResolver::PathResolver(std::string homeDirectory) : homeDirectory_(std::move(homeDirectory)) {}

ResolvedPath PathResolver::resolve(const std::string &rawPath, const std::string &baseDirectory) const {
  std::string path = rawPath;
  while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();

  if (path == "~") {
    path = homeDirectory_;
  } else if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
    path = homeDirectory_ + path.substr(1);
  } else {
    std::size_t tilde = path.find("~/");
    if (tilde != std::string::npos) path = homeDirectory_ + path.substr(tilde + 1);
  }

  // Still relative (no leading '/', and not ~-expanded above): join
  // onto baseDirectory, if the caller gave one -- see this parameter's
  // own declaration comment.
  if (!path.empty() && path.front() != '/' && !baseDirectory.empty()) {
    std::string base = baseDirectory;
    if (base.back() != '/') base += '/';
    path = base + path;
  }

  ResolvedPath result;
  if (path.empty()) return result;

  std::error_code ec;
  bool isDir = std::filesystem::is_directory(path, ec) && !ec;
  bool exists = std::filesystem::exists(path, ec) && !ec;

  std::string normalized = std::filesystem::path(path).lexically_normal().string();
  if (isDir && (normalized.empty() || normalized.back() != '/')) normalized += '/';

  result.path = normalized;
  result.isDirectory = isDir;
  result.exists = exists;
  return result;
}

}  // namespace fm
