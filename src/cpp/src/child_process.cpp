#include "child_process.hpp"

#include <csignal>
#include <cstdlib>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fm {

namespace {

std::vector<char *> toArgv(const std::vector<std::string> &args) {
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);
  return argv;
}

// Every forked child resets signal disposition to default first, so
// it doesn't inherit whatever handler policy the parent (tcb itself)
// has installed -- ported from load_signal(), called at the top of
// every fork.c child branch.
void resetSignalsToDefault() {
  for (int i = 1; i < NSIG; i++) std::signal(i, SIG_DFL);
}

// Ported from dup_error(): append the child's stderr to a log file
// instead of inheriting the parent's.
void redirectStderr(const std::string &path) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) return;
  dup2(fd, 2);
  close(fd);
}

// Child-side setup shared by run() and runPipeline()'s second stage:
// reset signals, optionally force LC_ALL=C, optionally redirect
// stderr, then wire outFd to stdout (and inFd to stdin, if given).
void prepareChild(int outFd, int inFd, const ChildProcess::Options &options) {
  resetSignalsToDefault();
  if (options.forceCLocale) setenv("LC_ALL", "C", 1);
  if (options.stderrLogPath) redirectStderr(*options.stderrLogPath);
  if (inFd >= 0) dup2(inFd, 0);
  dup2(outFd, 1);
}

std::string readAll(int fd) {
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) out.append(buf, n);
  return out;
}

// Forks a small dedicated process that writes `input` to `fd` and
// exits -- writing from the same process that's also about to block
// reading the child's stdout would risk deadlock once the input is
// larger than the pipe buffer (the child can't drain its own stdin
// if nothing is reading its stdout yet, and the parent can't finish
// writing stdin if it's waiting to write more than the pipe can hold
// before it starts reading stdout). Legacy's own fork_arc()/
// fork_chp() sidestep this the same way, with an extra fork() purely
// for the write side of a pipeline.
pid_t spawnWriter(int fd, const std::string &input) {
  pid_t pid = fork();
  if (pid == 0) {
    resetSignalsToDefault();
    std::size_t off = 0;
    while (off < input.size()) {
      ssize_t n = write(fd, input.data() + off, input.size() - off);
      if (n <= 0) break;
      off += static_cast<std::size_t>(n);
    }
    close(fd);
    _exit(0);
  }
  return pid;
}

int reapStatus(pid_t pid) {
  if (pid <= 0) return -1;
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) return -1;
  return (WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}

}  // namespace

ChildProcess::Result ChildProcess::run(const std::vector<std::string> &argv,
                                        const Options &options) {
  Result result;
  if (argv.empty()) return result;

  int outPv[2];
  if (pipe(outPv) < 0) return result;

  int inPv[2] = {-1, -1};
  pid_t writerPid = -1;
  if (options.input) {
    if (pipe(inPv) < 0) { close(outPv[0]); close(outPv[1]); return result; }
  }

  pid_t childPid = fork();
  if (childPid == 0) {
    close(outPv[0]);
    if (options.input) close(inPv[1]);
    prepareChild(outPv[1], options.input ? inPv[0] : -1, options);
    close(outPv[1]);
    if (options.input) close(inPv[0]);
    auto cargv = toArgv(argv);
    execvp(cargv[0], cargv.data());
    _exit(127);  // execvp() failed -- never fall through and keep running as tcb
  }

  close(outPv[1]);
  if (options.input) {
    close(inPv[0]);
    writerPid = spawnWriter(inPv[1], *options.input);
    close(inPv[1]);
  }

  result.output = readAll(outPv[0]);
  close(outPv[0]);

  reapStatus(writerPid);
  result.exitStatus = reapStatus(childPid);
  result.ok = (result.exitStatus == 0);
  return result;
}

ChildProcess::Result ChildProcess::runPipeline(const std::vector<std::string> &argv1,
                                                const std::vector<std::string> &argv2,
                                                const Options &options) {
  Result result;
  if (argv1.empty() || argv2.empty()) return result;

  int outPv[2], midPv[2];
  if (pipe(outPv) < 0) return result;
  if (pipe(midPv) < 0) { close(outPv[0]); close(outPv[1]); return result; }

  int inPv[2] = {-1, -1};
  pid_t writerPid = -1;
  if (options.input) {
    if (pipe(inPv) < 0) {
      close(outPv[0]); close(outPv[1]); close(midPv[0]); close(midPv[1]);
      return result;
    }
  }

  // Stage 1: argv1, stdout -> midPv, stdin <- inPv (if input given).
  pid_t pid1 = fork();
  if (pid1 == 0) {
    close(outPv[0]); close(outPv[1]);
    close(midPv[0]);
    if (options.input) close(inPv[1]);
    resetSignalsToDefault();
    if (options.forceCLocale) setenv("LC_ALL", "C", 1);
    if (options.input) dup2(inPv[0], 0);
    dup2(midPv[1], 1);
    close(midPv[1]);
    if (options.input) close(inPv[0]);
    auto cargv = toArgv(argv1);
    execvp(cargv[0], cargv.data());
    _exit(127);
  }
  close(midPv[1]);
  if (options.input) close(inPv[0]);

  // Stage 2: argv2, stdin <- midPv, stdout -> outPv.
  pid_t pid2 = fork();
  if (pid2 == 0) {
    prepareChild(outPv[1], midPv[0], options);
    close(outPv[1]);
    close(midPv[0]);
    // pid2 was forked before the parent got around to closing its own
    // outPv[0] (the read end of *this same* output pipe) and, when
    // there's piped-in input, inPv[1] (the input pipe's write end) --
    // both are still open in the parent at this point and so land in
    // pid2 as an inherited copy across fork(). Left open across the
    // exec below, that stray inPv[1] copy is enough to keep the input
    // pipe's write-end refcount above zero forever: stage 1 (reading
    // inPv[0] as its stdin) then never sees EOF and hangs, even after
    // every other write-end (the writer process, pid1 itself, the
    // parent) has closed its own copy. Found via a hung test run once
    // the unrelated Options-initialization SIGSEGV (see child_process.hpp)
    // was fixed and this code path could finally run to completion.
    close(outPv[0]);
    if (options.input) close(inPv[1]);
    auto cargv = toArgv(argv2);
    execvp(cargv[0], cargv.data());
    _exit(127);
  }
  close(midPv[0]);
  close(outPv[1]);

  if (options.input) {
    writerPid = spawnWriter(inPv[1], *options.input);
    close(inPv[1]);
  }

  result.output = readAll(outPv[0]);
  close(outPv[0]);

  reapStatus(writerPid);
  reapStatus(pid1);
  result.exitStatus = reapStatus(pid2);
  result.ok = (result.exitStatus == 0);
  return result;
}

}  // namespace fm
