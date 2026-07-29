#pragma once

#include <string>

#include "configuration.hpp"
#include "key_input.hpp"

namespace fm {

// EDITOR launch (mode.c's do_edit()): opens a file in the configured
// EDITOR at a given line number, with real terminal access, and
// reports whether the file changed so the caller knows to reload it.
//
// This is NOT a literal port. do_edit() hand-rolls the same fork()/
// execvp()/wait() + raw-termio-leave/re-enter sequence do_prog() and
// do_exec() also each hand-roll independently -- now
// ChildProcess::runInteractive() (added for ProgramMode) plus
// KeyInput's existing leaveRawMode()/reenterRawMode(). Building the
// argv itself is already Configuration::editorArgv()'s job (it
// builds the real "+<lineNumber>" argument directly, replacing
// legacy's fixed-width "+??????????" placeholder-patched-in-place
// trick -- see Configuration's own class comment), so this class is
// mostly just: run it, then check whether the file's content changed.
//
// Deliberately excluded (future work, not this slice):
//   - The mode-context guards on when editing is even allowed
//     (do_edit()'s "not while viewing an archive/cut/split buffer",
//     "path must be absolute in file-manager mode" checks):
//     Mode/Application's job to enforce before calling edit() at all,
//     the same hand-off boundary every other *Mode class in this
//     port already draws.
//   - The SIGCONT pid-reporting handler: same exclusion FileMode's
//     sibling ProgramMode already documents, for the same reason.
class EditMode {
 public:
  EditMode(KeyInput &keyInput, const Configuration &configuration);

  // Opens path in the configured EDITOR (Configuration::editorArgv())
  // at lineNumber. Returns true if the file's last-write time changed
  // while editing (the caller should reload it), false if unchanged
  // or the file couldn't be queried before or after. Ported from
  // do_edit()'s st_ctime comparison via stat(), but using
  // std::filesystem::last_write_time() instead of a raw ctime field
  // for two reasons: ctime also changes on metadata-only changes
  // (permissions, etc.) that aren't really "the content changed",
  // which is what this check is actually meant to answer; and plain
  // stat()'s st_mtime truncates to whole seconds, which is coarse
  // enough that a fast edit (a scripted EDITOR, or simply a quick
  // real one) can complete within the same second it started in and
  // go undetected -- last_write_time() carries the filesystem's real
  // sub-second precision instead.
  bool edit(const std::string &path, int lineNumber);

 private:
  KeyInput &keyInput_;
  const Configuration &configuration_;
};

}  // namespace fm
