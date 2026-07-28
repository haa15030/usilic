#pragma once

#include <string>
#include <vector>

namespace fm {

// Runs an external program and captures its output. Ported from
// fork.c's repeated pipe()/fork()/dup2()/execvp() dance -- every one
// of fork_key()/fork_bin()/fork_chp()/fork_arc()'s several pipeline
// stages independently hand-wrote the same handful of syscalls, with
// enough small variations (single command vs. two-stage pipeline,
// LC_ALL=C forcing, stdin-from-a-buffer) that ChildProcess exists to
// implement once instead of eight times.
//
// The legacy code's own history is a good argument for this: the
// "child falls through and keeps running as tcb after execvp()
// fails" bug (fork_bin(), fixed with an added exit(0)) and the
// overlapping-strcpy family of bugs were each found independently at
// one of these hand-written call sites; a single, tested
// implementation means a fix here covers all of them at once, and a
// newly-added call site can't reintroduce a variant of the same bug.
//
// Deliberately excluded (belongs to a future class, not here):
//   - Which program handles a given file, and tcbrc's CHILD/EXEC/
//     EDITOR/SHELL directive parsing (child.c's save_child()/
//     get_child()/is_child() and friends): squarely Configuration's
//     job (parsing tcbrc) combined with Mode's (deciding T_roff/
//     T_tar/T_exec dispatch from a suffix match) -- ChildProcess only
//     runs an already-decided argv, it doesn't decide what that argv
//     should be.
//   - Long-lived/interactive child processes with a live pty (SHELL
//     mode): a fundamentally different shape (streaming, bidirectional,
//     terminal-attached) from "run this, wait, capture all of its
//     output" -- future Shell/PtySession class's job.
//   - The keyboard-interrupt-while-reading and growable-linked-list-
//     of-chunks buffering in alloc_buff(): the former is a UI concern
//     (ChildProcess itself has nowhere to check the keyboard from,
//     nor should it), the latter was a 1990s workaround for not
//     having a growable container -- std::string already grows
//     itself.
// Options for a ChildProcess call. Deliberately defined at namespace
// scope rather than nested inside ChildProcess: a nested struct's
// default member initializers aren't visible yet when the enclosing
// class parses a member function's default argument that references
// them (a real two-pass class-parsing quirk -- nesting this struct
// and writing `= false`/`= nullptr` below fails to compile, with
// "default member initializer ... needed within definition of
// enclosing class ... outside of member functions"). Keeping Options
// out here lets it have normal default member initializers, so a
// bare `Options options;` (no braces) is safe by construction instead
// of leaving forceCLocale/input/stderrLogPath as indeterminate
// garbage -- which is exactly what happened when an earlier version
// of this class worked around the compile error by deleting the
// initializers instead of moving the struct out: `Options options;`
// in a test case left stderrLogPath as a garbage non-null pointer,
// and prepareChild() dereferencing it crashed the forked child with
// SIGSEGV.
struct ChildProcessOptions {
  // Force LC_ALL=C in the child only. Ported from fork_bin()'s
  // T_dir case: `ls`'s date column is parsed byte-by-byte
  // downstream (get_col()), and a localized month name would throw
  // off every column position after it.
  bool forceCLocale = false;
  // If set, written to the child's stdin before anything is read
  // from its stdout (via a small dedicated writer process, so a
  // large input can't deadlock against a full output pipe -- the
  // same reason legacy's own fork_arc()/fork_chp() write pipeline
  // input this way rather than as a same-process blocking write).
  const std::string *input = nullptr;
  // If set, the child's stderr is appended to this file instead of
  // inheriting the parent's. Ported from dup_error(); exposed as a
  // plain path instead of a hidden Configuration lookup so this
  // class doesn't need to know about that one, matching how
  // KeyInput exposed setPidReporter() instead of calling sub.c
  // directly.
  const std::string *stderrLogPath = nullptr;
};

class ChildProcess {
 public:
  struct Result {
    std::string output;
    bool ok = false;       // spawned and exited normally with status 0
    int exitStatus = -1;   // raw exit code; -1 if it never exited normally
  };

  using Options = ChildProcessOptions;

  static Result run(const std::vector<std::string> &argv, const Options &options = {});

  // Two-stage pipeline: argv1 | argv2, capturing argv2's stdout.
  // Ported from fork_arc()'s .deb/.tgz decompression chains and
  // fork_chp()'s filter chains (e.g. `ar p file data.tar.gz | tar
  // xvf -`).
  static Result runPipeline(const std::vector<std::string> &argv1,
                             const std::vector<std::string> &argv2,
                             const Options &options = {});
};

}  // namespace fm
