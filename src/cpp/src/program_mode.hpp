#pragma once

#include <functional>
#include <string>

#include "display.hpp"
#include "history.hpp"
#include "key_input.hpp"

namespace fm {

// PROGRAM mode (M_prog/M_prog2): runs an arbitrary shell command line
// against the current selection with real terminal access, for
// interactive/full-screen programs (`less somefile`, `vi somefile`, a
// user-configured PROGRAM-list entry) that a captured-output
// ChildProcess::run() can't support. Ported from mode.c's do_prog().
//
// This is NOT a literal port. do_prog() hand-rolls its own fork()/
// execlp()/wait() sequence around leaving/re-entering raw termio
// (vt_init(-2)/vt_init(2)) and a dedicated SIGCONT handler
// (mode_cont()) for reporting this process's pid on resume from a
// job-control suspend. The fork/exec/wait mechanics are now
// ChildProcess::runInteractive() (added alongside this class, since
// it's a genuinely reusable primitive: "run this in the foreground
// with real inherited stdio, no pty, no piping" -- distinct from both
// ChildProcess's existing captured-output run()/runPipeline() and
// Shell's pty-attached SHELL session); leaving/re-entering raw mode
// is KeyInput's existing leaveRawMode()/reenterRawMode(), already
// built for exactly this purpose.
//
// Deliberately excluded (future work, not this slice):
//   - The SIGCONT pid-reporting handler (do_prog()'s mode_cont()):
//     KeyInput already has a SIGCONT/foreground-pid mechanism
//     (setForegroundChildPid()/setPidReporter()), but that one is
//     purpose-built for SHELL's persistent, pty-attached child --
//     reusing it here for a one-shot foreground program without
//     confirming legacy actually intended the two to share behavior
//     would be guessing, not porting. Left unwired.
//   - `unsetenv(ENV_PROG)` before exec (do_prog()'s trick for letting
//     a PROGRAM-mode command re-launch usilic itself recursively):
//     narrow enough, and specific enough to this one caller, that
//     baking it into the new shared ChildProcess::runInteractive()
//     primitive would be over-fitting a generic primitive to one
//     caller's need.
//   - Deciding *which* command to run (the M_prog prompt, PROGRAM-
//     list lookup via Configuration, the "did the target file change
//     while the program ran" staleness check that decides whether to
//     redraw): Mode/Application's job, the same hand-off boundary
//     every other *Mode class in this port already draws.
class ProgramMode {
 public:
  ProgramMode(KeyInput &keyInput, Display &display, History &history);

  // Runs `command` as `sh -c command` in the foreground with real
  // terminal access (leaving raw mode first, re-entering it after),
  // and records `recordedCommand` (defaults to `command` itself) into
  // History's ShellCommand ring (the same ring shell.c's own commands
  // feed, matching do_prog()'s own `*histp = save_history(*histp, str,
  // 1)`). Returns the child's exit status, or -1 if command is empty or
  // the child couldn't be spawned.
  //
  // recordedCommand as a SEPARATE parameter (2026-08-17 addition):
  // executeProgram()'s own `cd '<dir>' && ` prefix (see its own
  // comment) is an internal implementation detail of how a bare
  // filename gets resolved, not something the user typed -- recording
  // THAT into history would corrupt ^J^R's command recall and the
  // ^J^P PROGRAM list with a string that doesn't even start with a
  // recognized program name if re-submitted later.
  int run(const std::string &command, const std::string &recordedCommand = "");

  // Overrides HOW run() actually executes `command` in the
  // foreground. When set and it returns true, run() trusts it handled
  // the whole foreground handoff itself (raw-mode leave/re-enter
  // included) and skips leaveRawMode()/ChildProcess::runInteractive()/
  // reenterRawMode() entirely; false falls back to that normal path
  // exactly as if no launcher were set. Unset (the default) always
  // uses ChildProcess::runInteractive() directly, unchanged from
  // before this existed.
  //
  // Ported from nowhere -- legacy has no GUI mode at all. Exists for
  // exactly one caller: main.cpp's tryEnterGuiPhase(), for the
  // duration a GraphicalDisplay window is the active display, when
  // this process's own inherited stdio is no longer a real terminal a
  // foreground program like `less`/`vi` could use directly (see
  // runProgramInOverlayTerminal()'s own comment there for what the
  // launcher actually does: open a real Terminal.app window overlaying
  // the GUI window's own current screen position). Wired as a
  // settable callback rather than a subclass/flag, the same shape
  // Shell::setProgramLauncher() already established, so ProgramMode
  // itself stays exactly as ignorant of SDL2/GraphicalDisplay
  // specifics as Application already is (see Application's own class
  // comment on that boundary) -- main.cpp is the only file that ever
  // actually constructs a GUI-aware launcher.
  void setForegroundLauncher(std::function<bool(const std::string &command)> launcher);

 private:
  KeyInput &keyInput_;
  Display &display_;
  History &history_;
  std::function<bool(const std::string &command)> foregroundLauncher_;
};

}  // namespace fm
