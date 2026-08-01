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

  // baseDirectory: joined in front of rawPath when rawPath is neither
  // absolute nor ~-prefixed, e.g. resolve("plain.txt", "/tmp/sub") ->
  // "/tmp/sub/plain.txt". Empty (the default) preserves this class's
  // original behavior of leaving a relative path exactly as typed
  // (resolved against the PROCESS's own cwd once exists()/
  // is_directory() run, not any notion of "the directory currently
  // browsed on screen" -- usilic never chdir()s, see main.cpp/
  // Application, so those two are NOT the same thing). Added after a
  // real user report (2026-08-01): "^Xd/^Xfではディレクトリ/
  // ファイル名入力ができず" turned out to be exactly this -- typing a
  // RELATIVE path after navigating away from wherever the process
  // happened to be launched FROM silently failed to resolve (exists
  // came back false, so executePathInput() just redrew the unchanged
  // screen with no visible error), which reads as "input doesn't
  // work" even though the prompt's own text-entry was never broken.
  ResolvedPath resolve(const std::string &rawPath, const std::string &baseDirectory = "") const;

 private:
  std::string homeDirectory_;
};

}  // namespace fm
