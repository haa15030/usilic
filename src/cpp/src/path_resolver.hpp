#pragma once

#include <string>

namespace fm {

struct ResolvedPath {
  std::string path;
  bool isDirectory = false;
  bool exists = false;
};

// Resolves a raw, possibly user-typed path string into a clean,
// classified form: ~/~user expansion, ./ and ../ segment cleanup, and
// trailing-slash normalization for real directories. Ported from
// mode.c's M_ldir/M_lfile/M_ldir2/M_lfile2/T_locate case -- the glue
// that decides whether a typed path (or a selected LOCATE result)
// should land in DirectoryMode or FileMode.
//
// This is NOT a literal port of sub.c's erase_dot() (the "./"/"../"
// segment cleanup). erase_dot() is a hand-rolled, fiddly memmove-based
// lexical path normalizer -- exactly the kind of hand-rolled algorithm
// this project replaces with the standard library equivalent when one
// exists (matching e.g. LIST's BST becoming std::map): here, that's
// std::filesystem::path::lexically_normal().
//
// Deliberately excluded (future work, not this slice):
//   - Deciding what to DO with a resolved path (entering DirectoryMode
//     vs. FileMode, pushing a ModeStack frame): Mode/Application's
//     job, the same hand-off boundary every other class in this port
//     already draws.
//   - Symlink resolution / realpath()-style canonicalization:
//     lexically_normal() is purely textual (matching erase_dot()'s
//     own purely-textual approach) and does not consult the
//     filesystem for "." components composed across a symlink
//     boundary the way a true realpath() would.
class PathResolver {
 public:
  // homeDirectory: the caller-supplied $HOME (no hidden getenv() call
  // here, matching this project's standing "no hidden global state"
  // rule already established for Configuration/Shell/etc).
  explicit PathResolver(std::string homeDirectory);

  ResolvedPath resolve(const std::string &rawPath) const;

 private:
  std::string homeDirectory_;
};

}  // namespace fm
