// glibc needs _XOPEN_SOURCE for posix_openpt/grantpt/unlockpt/ptsname;
// _DEFAULT_SOURCE keeps other glibc extensions (e.g. NSIG) visible
// alongside it. Gated to Linux only -- macOS/BSD libc expose both
// without this, and defining _XOPEN_SOURCE there can instead hide
// their own BSD extensions. Same rationale as tcb.h's own version of
// this guard (see tcb/src/tcb.h).
#ifdef __linux__
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif

#include "shell.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
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

void resetSignalsToDefault() {
  for (int i = 1; i < NSIG; i++) std::signal(i, SIG_DFL);
}

bool isBashProgram(const std::string &argv0) {
  auto pos = argv0.find_last_of('/');
  std::string base = (pos == std::string::npos) ? argv0 : argv0.substr(pos + 1);
  return base == "bash";
}

// Portable pty acquisition (POSIX/Unix98 API), ported near-verbatim
// from sub.c's open_pty() -- see CLAUDE.md history for why this
// replaced the original BSD /dev/ptyp0.."/dev/ptysf" device-node
// scanning approach (didn't exist on modern Linux or macOS at all).
// Unlike open_pty(), this always keeps the master fd open (legacy's
// f_open==0 "just reserve it" mode had no caller left that used it by
// the time Shell was written).
int openPty(std::string &slavePathOut) {
  int fd = posix_openpt(O_RDWR | O_NOCTTY);
  if (fd < 0) return -1;
  if (grantpt(fd) < 0 || unlockpt(fd) < 0) {
    close(fd);
    return -1;
  }
  char *slave = ptsname(fd);
  if (!slave) {
    close(fd);
    return -1;
  }
  slavePathOut = slave;
  return fd;
}

}  // namespace

Shell::Shell(Display &display, const ShellOptions &options)
    : display_(display),
      options_(options),
      pipeBuffer_(options.encoding, options.columns, options.scrollbackBudget),
      scrollback_(display),
      commandLine_(display, options.encoding) {
  markerPrefix_ = options_.promptMark + options_.programName + ":";
  viewportRows_ = options_.rows;
  columns_ = options_.columns;
  if (!options_.argv.empty()) isBash_ = isBashProgram(options_.argv[0]);
  scrollback_.setViewport(0, std::max(1, viewportRows_ - 1));
  updateCommandLineField();
}

Shell::~Shell() { terminate(); }

bool Shell::start() {
  if (masterFd_ >= 0) return true;
  if (options_.argv.empty()) return false;

  std::string slavePath;
  int master = openPty(slavePath);
  if (master < 0) return false;

  pid_t pid = fork();
  if (pid < 0) {
    close(master);
    return false;
  }

  if (pid == 0) {
    // Child: become the shell. Ported from start_shell()'s fork
    // branch. Not ported: the original's rendezvous-file pid handoff
    // (writing its own getpid() to a temp file for the parent to poll
    // for and read back) -- fork()'s return value already gives the
    // parent the child pid directly and synchronously, making that
    // dance pure legacy cruft rather than something this design still
    // needs (this parent isn't itself the result of a double-fork, so
    // there's no "lost the real pid" problem to work around).
    close(master);
    setsid();
    int slaveFd = open(slavePath.c_str(), O_RDWR);
    if (slaveFd < 0) _exit(127);
#ifdef TIOCSCTTY
    ioctl(slaveFd, TIOCSCTTY, 0);
#endif
    dup2(slaveFd, 0);
    dup2(slaveFd, 1);
    dup2(slaveFd, 2);
    close(slaveFd);
    resetSignalsToDefault();
    if (isBash_) {
      // Suppresses bash's own "erase and redraw using COLUMNS bytes
      // of padding" prompt-redisplay logic before Shell has had a
      // chance to report the real terminal width -- ported from
      // start_shell(); onResize()/buildSetupCommand() set the real
      // value once the shell is actually up.
      setenv("COLUMNS", "1024", 1);
    }
    auto cargv = toArgv(options_.argv);
    execvp(cargv[0], cargv.data());
    _exit(127);  // execvp() failed -- never fall through and keep running as usilic
  }

  childPid_ = pid;
  masterFd_ = master;
  fcntl(masterFd_, F_SETFL, fcntl(masterFd_, F_GETFL) | O_NONBLOCK);

  writeToPty(buildSetupCommand());

  if (!waitForHandshake()) {
    terminate();
    return false;
  }
  return true;
}

bool Shell::alive() const {
  if (childPid_ <= 0) return false;
  return kill(childPid_, 0) == 0;
}

int Shell::childPid() const { return childPid_; }

void Shell::terminate() {
  if (childPid_ > 0) {
    kill(childPid_, SIGHUP);
    int status = 0;
    bool reaped = false;
    for (int i = 0; i < 100 && !reaped; i++) {
      if (waitpid(childPid_, &status, WNOHANG) > 0) reaped = true;
      else usleep(10000);
    }
    if (!reaped && kill(childPid_, 0) == 0) {
      kill(childPid_, SIGKILL);
      // Bounded WNOHANG polling here too, not a blocking waitpid --
      // found via a real bash test hanging indefinitely on macOS: the
      // shell child (session leader of the pty) was occasionally
      // still observed mid-exit (ps STAT "E", "trying to exit") well
      // after SIGKILL, for reasons that didn't reproduce in an
      // isolated repro of just the fork/signal/wait sequence alone --
      // most likely some pty-controlling-terminal teardown edge case
      // specific to running inside the larger test binary. Whatever
      // the exact cause, a teardown path that can block its caller
      // forever is worse than the rare leaked zombie this leaves
      // behind (reaped by init once this process itself exits) if the
      // kernel is unusually slow to finish reaping even after
      // SIGKILL.
      for (int i = 0; i < 100 && !reaped; i++) {
        if (waitpid(childPid_, &status, WNOHANG) > 0) reaped = true;
        else usleep(10000);
      }
    }
    childPid_ = -1;
  }
  if (masterFd_ >= 0) {
    close(masterFd_);
    masterFd_ = -1;
  }
}

int Shell::ptyFd() const { return masterFd_; }

bool Shell::onPtyReadable() {
  if (masterFd_ < 0) return false;
  char buf[4096];
  ssize_t n = read(masterFd_, buf, sizeof(buf));
  if (n > 0) {
    processOutputBytes(std::string(buf, static_cast<std::size_t>(n)));
    return true;
  }
  if (n == 0) return false;  // EOF: the shell child exited
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return true;  // no data right now
  return false;
}

// Ported from output_shell()'s main read loop (the errp[]/f.i_err/
// f.i_errp/f.n_err byte-by-byte state machine), reworked as a
// buffered substring match instead: markerPrefix_ (promptMark +
// programName + ":", i.e. PS1's literal fixed prefix) is matched a
// byte at a time against markerMatchPos_; once fully matched, bytes
// are buffered into promptTailBuffer_ until promptMark reappears
// (closing the PS1 = mark+program+":"+"\w\$ "+mark cycle), at which
// point handlePromptDetected() fires with everything in between (the
// shell's own \w-expanded cwd plus its trailing "$ "/"# "). A partial
// match that turns out to be a false alarm is replayed byte-for-byte
// as ordinary content instead of being lost, matching legacy's own
// rollback behavior for the same case.
void Shell::processOutputBytes(const std::string &bytes) {
  const std::string &mark = options_.promptMark;
  for (unsigned char c : bytes) {
    if (inPromptTail_) {
      promptTailBuffer_.push_back(static_cast<char>(c));
      if (!mark.empty() && promptTailBuffer_.size() >= mark.size() &&
          promptTailBuffer_.compare(promptTailBuffer_.size() - mark.size(), mark.size(), mark) == 0) {
        std::string tail = promptTailBuffer_.substr(0, promptTailBuffer_.size() - mark.size());
        inPromptTail_ = false;
        promptTailBuffer_.clear();
        handlePromptDetected(tail);
      } else if (promptTailBuffer_.size() > 4096) {
        // Safety valve: no closing mark showed up in a plausible
        // amount of output. Give up on this being a real prompt and
        // replay the buffered bytes as ordinary content rather than
        // buffering forever.
        for (unsigned char b : promptTailBuffer_) appendBodyByte(b);
        promptTailBuffer_.clear();
        inPromptTail_ = false;
      }
      continue;
    }

    if (!markerPrefix_.empty() && c == static_cast<unsigned char>(markerPrefix_[markerMatchPos_])) {
      markerMatchBuffer_.push_back(static_cast<char>(c));
      markerMatchPos_++;
      if (markerMatchPos_ == markerPrefix_.size()) {
        markerMatchPos_ = 0;
        markerMatchBuffer_.clear();
        inPromptTail_ = true;
        promptTailBuffer_.clear();
      }
      continue;
    }

    // Mismatch: the tentative match so far was a false alarm --
    // replay it as ordinary content, then retry the current byte from
    // position 0 (it may itself start a fresh match).
    for (unsigned char b : markerMatchBuffer_) appendBodyByte(b);
    markerMatchBuffer_.clear();
    markerMatchPos_ = 0;
    if (!markerPrefix_.empty() && c == static_cast<unsigned char>(markerPrefix_[0])) {
      markerMatchBuffer_.push_back(static_cast<char>(c));
      markerMatchPos_ = 1;
      if (markerMatchPos_ == markerPrefix_.size()) {
        markerMatchPos_ = 0;
        markerMatchBuffer_.clear();
        inPromptTail_ = true;
        promptTailBuffer_.clear();
      }
    } else {
      appendBodyByte(c);
    }
  }
  flushNewScrollbackLines();
}

void Shell::appendBodyByte(unsigned char c) {
  pipeBuffer_.feed(std::string(1, static_cast<char>(c)));
}

void Shell::flushNewScrollbackLines() {
  const auto &plines = pipeBuffer_.lines();
  if (plines.size() <= linesConsumed_) return;
  std::vector<ContentView::Line> newLines(scrollback_.lines());
  newLines.reserve(plines.size());
  for (std::size_t i = linesConsumed_; i < plines.size(); i++) newLines.push_back({plines[i].text});
  linesConsumed_ = plines.size();
  scrollback_.setLines(std::move(newLines));
  scrollback_.goToBottom();
}

void Shell::handlePromptDetected(const std::string &tailBeforeMark) {
  // The shell's own \w (bash) / %~ (csh) expansion, followed by its
  // literal prompt character ('$'/'#'/'%') and one space -- strip
  // that trailing two bytes to recover a clean directory string. This
  // is a textual heuristic on the shell's own rendered prompt text
  // (there's no side channel to ask the shell for its cwd directly),
  // same approach legacy's write_shell() took -- but built with
  // std::string instead of write_shell()'s fixed buffer + strcpy,
  // which is what made that function's overlapping-copy bug possible
  // in the first place (see CLAUDE.md history).
  std::string dir = tailBeforeMark;
  if (dir.size() >= 2 && dir.back() == ' ' &&
      (dir[dir.size() - 2] == '$' || dir[dir.size() - 2] == '#' || dir[dir.size() - 2] == '%')) {
    dir.erase(dir.size() - 2);
  }
  if (dir != currentDirectory_) {
    currentDirectory_ = dir;
    if (directoryChangedCallback_) directoryChangedCallback_(dir);
  }

  pipeBuffer_.flush();
  flushNewScrollbackLines();

  promptPlain_ = options_.programName + ":" + tailBeforeMark;
  updateCommandLineField();
  commandLine_.setText("");

  std::string decorated = display_.attributeEscape(Display::Attribute::Bold) + options_.programName +
                           display_.attributeEscape(Display::Attribute::BoldOff) + ":" + tailBeforeMark;
  int promptRow = (viewportRows_ > 0) ? viewportRows_ - 1 : 0;
  display_.moveCursor(promptRow, 0);
  display_.clearToEndOfLine();
  display_.write(decorated);
  display_.flush();

  commandLine_.redraw();
}

void Shell::updateCommandLineField() {
  int promptRow = (viewportRows_ > 0) ? viewportRows_ - 1 : 0;
  int promptCol = displayWidth(promptPlain_);
  int width = std::max(1, columns_ - promptCol);
  commandLine_.setField(promptRow, promptCol, width);
}

int Shell::displayWidth(const std::string &text) const {
  if (options_.encoding != Charset::Encoding::Utf8) return static_cast<int>(text.size());
  int width = 0;
  std::size_t i = 0;
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
  while (i < text.size()) {
    int len = Charset::sequence_length(bytes[i]);
    if (i + static_cast<std::size_t>(len) > text.size()) len = static_cast<int>(text.size() - i);
    width += Charset::display_width(bytes + i, len);
    i += static_cast<std::size_t>(len);
  }
  return width;
}

// Ported from output_shell()'s PS1/eos setup line. Not ported: the
// PAGER export (legacy pointed it at the tcb binary itself, via
// s_S(S_tcb), to integrate with tcb's own internal pager mode -- no
// equivalent exists yet in this C++ port for it to point at, so it's
// omitted rather than pointing at something meaningless; revisit once
// a pager mode exists).
std::string Shell::buildSetupCommand() const {
  const std::string &mark = options_.promptMark;
  const std::string &prog = options_.programName;
  if (isBash_) {
    std::string eos = "#eos" + std::to_string(childPid_) + "#";
    return "unset PROMPT_COMMAND; export PS1='" + mark + prog + ":\\w\\$ " + mark +
           "'; set -o ignoreeof; export COLUMNS=" + std::to_string(columns_) + "; unset HISTFILE\n" + eos +
           "\n";
  }
  return "set prompt = \"" + mark + prog + ":%~%# " + mark + "\"; set ignoreeof\n";
}

// One-shot startup handshake: discard everything up through our own
// "#eos<pid>#" sentinel (bash) or the first literal '#' (csh/tcsh),
// exactly like output_shell()'s pre-main-loop scan -- the terminal
// emulator (e.g. GNOME Terminal/VTE's /etc/profile.d/vte.sh) may have
// already auto-drawn bash's very first prompt, using PROMPT_COMMAND
// as it stood *before* the setup command above unset it, before bash
// ever reads that command; without skipping past it, that leftover
// output would otherwise be misparsed as real prompt/content bytes by
// the main loop. See CLAUDE.md history for why a bare '#' isn't
// enough for bash (it can turn up by coincidence in that leftover
// output).
bool Shell::waitForHandshake() {
  std::string sentinel = isBash_ ? ("#eos" + std::to_string(childPid_) + "#") : "#";
  std::string scan;
  const int maxAttempts = 500;  // 500 * 20ms = up to ~10s
  for (int attempt = 0; attempt < maxAttempts; attempt++) {
    struct pollfd pfd {
      masterFd_, POLLIN, 0
    };
    int rc = poll(&pfd, 1, 20);
    if (rc < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (rc == 0) continue;
    if (pfd.revents & (POLLHUP | POLLERR)) return false;
    char buf[512];
    ssize_t n = read(masterFd_, buf, sizeof(buf));
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
      return false;
    }
    scan.append(buf, static_cast<std::size_t>(n));
    if (scan.find(sentinel) != std::string::npos) return true;
    if (scan.size() > 4096) scan.erase(0, scan.size() - sentinel.size());
  }
  return false;
}

LineEditor &Shell::commandLine() { return commandLine_; }
ContentView &Shell::scrollback() { return scrollback_; }

void Shell::submitLine() {
  std::string line = commandLine_.text();
  std::size_t start = line.find_first_not_of(' ');
  std::string trimmed = (start == std::string::npos) ? "" : line.substr(start);

  if (!trimmed.empty()) {
    // Trailing pipeline stage's program token (split on '|', take the
    // last stage, then its first whitespace-delimited word). Ported
    // from input_shell()'s command-dispatch logic; the PROGRAM-table
    // lookup itself (find_prog()) is not ported here -- see the class
    // comment.
    std::size_t pipePos = trimmed.find_last_of('|');
    std::string lastStage = (pipePos == std::string::npos) ? trimmed : trimmed.substr(pipePos + 1);
    std::size_t s = lastStage.find_first_not_of(' ');
    std::string stageTrimmed = (s == std::string::npos) ? "" : lastStage.substr(s);
    std::size_t e = stageTrimmed.find_first_of(" \t");
    std::string programToken = (e == std::string::npos) ? stageTrimmed : stageTrimmed.substr(0, e);

    bool handled = programLauncher_ && !programToken.empty() && programLauncher_(programToken, trimmed);
    if (!handled) {
      writeToPty(trimmed + "\n");
      if (historyRecorder_) historyRecorder_(trimmed);
    }
  } else {
    writeToPty("\n");
  }

  commandLine_.setText("");
  commandLine_.redraw();
}

void Shell::onTab() {
  if (!completionProvider_) return;
  const std::string &text = commandLine_.text();
  int cursor = commandLine_.cursorBytePos();
  std::size_t searchFrom = (cursor > 0) ? static_cast<std::size_t>(cursor - 1) : 0;
  std::size_t wordStart = text.find_last_of(" \t", searchFrom);
  wordStart = (wordStart == std::string::npos) ? 0 : wordStart + 1;
  if (static_cast<int>(wordStart) > cursor) wordStart = static_cast<std::size_t>(cursor);
  std::string partial = text.substr(wordStart, static_cast<std::size_t>(cursor) - wordStart);

  auto candidates = completionProvider_(partial);
  if (candidates.size() != 1) return;  // ambiguous/none: a future PathCompleter's job to resolve
  const std::string &full = candidates[0];
  if (full.size() <= partial.size()) return;
  commandLine_.paste(full.substr(partial.size()));
}

void Shell::setCompletionProvider(
    std::function<std::vector<std::string>(const std::string &partialWord)> provider) {
  completionProvider_ = std::move(provider);
}

void Shell::sendControlByte(unsigned char byte) { writeToPty(std::string(1, static_cast<char>(byte))); }

void Shell::writeToPty(const std::string &bytes) {
  if (masterFd_ < 0) return;
  std::size_t off = 0;
  while (off < bytes.size()) {
    ssize_t n = write(masterFd_, bytes.data() + off, bytes.size() - off);
    if (n > 0) {
      off += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      // masterFd_ is non-blocking (see start()), and right after
      // fork() the pty's input queue can reject writes outright until
      // the child has actually opened the slave side -- found via a
      // real bash test hanging/failing start() outright: the initial
      // PS1 setup command silently lost every byte to this exact
      // race, so bash came up with its stock prompt and the "#eos
      // <pid>#" handshake sentinel never appeared. Wait briefly for
      // the pty to accept more and retry, rather than silently
      // dropping the rest of the write on the first transient EAGAIN.
      struct pollfd pfd {
        masterFd_, POLLOUT, 0
      };
      poll(&pfd, 1, 100);
      continue;
    }
    break;  // a real write error -- give up
  }
}

void Shell::setDirectoryChangedCallback(std::function<void(const std::string &)> callback) {
  directoryChangedCallback_ = std::move(callback);
}

const std::string &Shell::currentDirectory() const { return currentDirectory_; }

void Shell::setProgramLauncher(
    std::function<bool(const std::string &, const std::string &)> launcher) {
  programLauncher_ = std::move(launcher);
}

void Shell::setHistoryRecorder(std::function<void(const std::string &)> recorder) {
  historyRecorder_ = std::move(recorder);
}

void Shell::setBusy(bool busy) { busy_ = busy; }
bool Shell::busy() const { return busy_; }

void Shell::onResize(int rows, int columns) {
  viewportRows_ = rows;
  columns_ = columns;
  scrollback_.setViewport(0, std::max(1, rows - 1));
  updateCommandLineField();
  if (masterFd_ >= 0) {
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(columns);
    ioctl(masterFd_, TIOCSWINSZ, &ws);
  }
  scrollback_.redraw();
  commandLine_.redraw();
}

void Shell::setEncoding(Charset::Encoding encoding) {
  options_.encoding = encoding;
  commandLine_.setEncoding(encoding);
}

}  // namespace fm
