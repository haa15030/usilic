#pragma once

#include <string>

#include "configuration.hpp"
#include "key_input.hpp"

namespace fm {

// EXEC launch (mode.c's do_exec(), T_exec): hands a file off entirely
// to an external program (an EXEC-directive suffix match, e.g. a
// configured PDF viewer), with real terminal access.
//
// This is NOT a literal port. do_exec() builds a single command-line
// string from `command`'s argv (quoting the target filename), then
// re-splits that string back into an argv via alloc_arg() (a small
// shell-word tokenizer) before exec'ing it -- a build-a-string-then-
// reparse-it round trip that only existed because CHILD's own argv
// was itself assembled from a single stored command line originally.
// Configuration::programForSuffix() already returns a real
// std::vector<std::string> argv, so ExecMode just appends the target
// path as its own element and runs -- no string-building or
// re-splitting needed. Execution itself is the same
// ChildProcess::runInteractive() + KeyInput raw-mode leave/re-enter
// shape as EditMode/ProgramMode.
//
// Deliberately excluded (future work, not this slice): deciding that
// a suffix is EXEC-mapped in the first place (Configuration::isExec(),
// already built) and what to do afterward (Mode/Application's job).
class ExecMode {
 public:
  explicit ExecMode(KeyInput &keyInput);

  // Runs command.argv with path appended as the final argument.
  // Returns the exit status, or -1 if command.argv is empty.
  int run(const ProgramCommand &command, const std::string &path);

 private:
  KeyInput &keyInput_;
};

}  // namespace fm
