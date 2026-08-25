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
#include <termios.h>
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

std::string baseName(const std::string &argv0) {
  auto pos = argv0.find_last_of('/');
  return (pos == std::string::npos) ? argv0 : argv0.substr(pos + 1);
}

bool isBashProgram(const std::string &argv0) { return baseName(argv0) == "bash"; }

// zsh has been macOS's own default login shell since Catalina (2019)
// -- a real gap found via actual hands-on use (2026-08-01): the setup
// command below used to lump "not bash" into one tcsh/csh-syntax
// branch (`set prompt = "..."`), which zsh does NOT understand (that
// `set VAR = value` form is tcsh/csh-specific; zsh's `set` builtin is
// for shell OPTIONS, not variable assignment). Sent to a real zsh,
// that line silently fails to set PROMPT at all, leaving the user's
// OWN real, fully-themed interactive prompt (oh-my-zsh/powerlevel10k/
// starship/etc, with whatever ANSI color codes and Unicode glyphs it
// uses) as what's actually displayed -- which this port's Shell
// (built assuming plain, synthetic prompt text) has no way to parse
// or safely render, producing exactly the garbled scrollback and
// "controlCode as a highlighted box" symptoms reported.
bool isZshProgram(const std::string &argv0) { return baseName(argv0) == "zsh"; }

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
  // Ported from key.c's stand_str(): its own early-return
  // (`f_M == M_shell`) means legacy NEVER draws the whole-line
  // reverse-video cursor highlight for SHELL's own scrollback
  // browsing (^P/^N through past commands, M_kup/M_kdn) the way it
  // does for ordinary lists -- confirmed by a real side-by-side
  // screenshot comparison against legacy (2026-08-04) showing a plain
  // terminal cursor there, not a highlighted bar, matching how
  // FileMode already disables this for the same reason (see its own
  // comment). Application's own ^P/^N handlers move the display's
  // real cursor to ContentView::cursorDisplayRow() instead, so
  // "current line" is still visible while browsing -- just as a
  // normal text cursor, not a colored highlight.
  scrollback_.setCursorHighlightEnabled(false);
  if (!options_.argv.empty()) {
    isBash_ = isBashProgram(options_.argv[0]);
    isZsh_ = isZshProgram(options_.argv[0]);
  }
  // Three DISTINCT, permanently FIXED rows (2026-08-04 revision):
  // scrollback_'s own CONTENT area is height_ = viewportRows_-2 rows
  // tall (0..viewportRows_-3), the live prompt (commandLineRow(), see
  // its own comment) is the fixed row viewportRows_-2 right after it,
  // and the mode-line is pinned to the TRUE last row viewportRows_-1
  // via setModeLineRow(). scrollback_.setBottomAnchored(true) is what
  // makes this work with a genuinely FIXED prompt row: with it, fewer-
  // than-height_ lines render flush against the BOTTOM of the content
  // area (blank padding rows above them) instead of the ContentView
  // default of flush-against-the-TOP -- so the last real content line
  // is ALWAYS immediately above viewportRows_-2, whether scrollback_
  // has 1 line or 100, and the prompt never needs to move at all.
  //
  // This replaces TWO earlier iterations, each fixing a real bug the
  // previous one didn't fully solve:
  //   1. (2026-08-02) commandLine_ overlaid scrollback_'s own LAST
  //      CONTENT row directly (back when height_ was viewportRows_-1,
  //      no reserved row at all) -- reasoned at the time as "matching
  //      legacy, where the live prompt IS simply the last line of
  //      scrollback". Real output arriving after a command was
  //      submitted legitimately wanted that same row too, and
  //      whichever of the two redrew last silently clobbered the
  //      other (confirmed via real escape-sequence tracing, 2026-08-03:
  //      every command's last output line got overwritten by the
  //      reappearing prompt in -c, and the reverse in -g).
  //   2. (2026-08-03) Reserved a reliably separate row for the prompt,
  //      but made it CONTENT-RELATIVE (grew as scrollback_ did,
  //      settling at viewportRows_-2 only once content filled the
  //      content area) to match legacy's own visible behavior (prompt
  //      immediately after whatever came before it, not a fixed row
  //      with an unnatural gap while scrollback_ was still short).
  //      This introduced its OWN class of bug: nothing kept
  //      commandLine_'s cached row or the prompt's own on-screen text
  //      in sync as scrollback_ kept growing across several separate
  //      pty reads before the next real prompt was detected -- a real
  //      user report (2026-08-03/04): "ls[Enter]→一瞬プロンプト表示→
  //      ls結果表示→一瞬プロンプト表示後消失". Two follow-up patches
  //      (re-syncing commandLine_'s field on every flush, then also
  //      re-asserting redrawPromptRow() on every flush) each closed
  //      part of the gap but the user's own next report ("降下中がNG。
  //      着地後はOK") confirmed the underlying churn -- the prompt's
  //      row genuinely changing, repeatedly, mid-command -- was still
  //      there, just less visible.
  // Bottom-anchoring content instead removes the DYNAMIC positioning
  // entirely: the prompt's row is a compile-time-obvious constant
  // again (like iteration 1), but scrollback_ itself -- not Shell --
  // now guarantees content and prompt never collide (unlike iteration
  // 1) and never requires re-syncing anything as content grows (unlike
  // iteration 2), because there is nothing left TO re-sync.
  scrollback_.setViewport(0, std::max(1, viewportRows_ - 2));
  scrollback_.setModeLineRow(viewportRows_ - 1);
  scrollback_.setBottomAnchored(true);
  updateCommandLineField();
}

Shell::~Shell() { terminate(); }

bool Shell::start() {
  if (masterFd_ >= 0) return true;
  if (options_.argv.empty()) return false;
  resetSessionState();

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
    // 2026-08-25 (a direct user request): ported from tcb.c's own
    // ENV_PROG convention (progname.h's PROGNAME_UPPER, "USILIC" for
    // this build, "U" for the legacy one after its own 2026-08-25
    // rename) -- a real usilic INVOKED FROM WITHIN this shell (e.g.
    // "usilic ." typed at the prompt, or a pipe into it) checks this
    // at its own startup to detect "I'm running inside an already-
    // live usilic's SHELL mode" and, if so, hands its arguments off to
    // THIS process instead of opening an independent second session
    // (see Application::pollShellHandoff()'s own comment for the
    // receiving half). Just a boolean marker (legacy's own value here
    // is a version string, likewise never parsed for content) -- only
    // presence is checked.
    setenv("USILIC", "1", 1);
    // 2026-08-25 (a direct user follow-up, ported into BOTH this
    // binary AND legacy's own tcb/src/shell.c's start_shell() -- see
    // main.cpp's own checkInvalidTerminalMarker() for the receiving
    // half): USILIC above only ever proves "I'm inside a live usilic
    // SHELL specifically" -- typing the OTHER binary's name (legacy
    // "U") at THIS shell's prompt sees no such env var at all and
    // falls through to launching a genuinely independent full session
    // instead (a real report: "うっかり'usilic .'とやってしまうと例の
    // 縦長usilicが出てしまう" -- and the reverse, U typed inside a live
    // usilic SHELL). A first attempt at solving this cross-binary case
    // via an unusual pty baud rate (a real termios field, otherwise
    // completely inert for a *pseudo* terminal) turned out NOT to
    // survive in practice -- a real PTY capture found zsh's own
    // interactive startup silently resets it back to a normal speed
    // before the first prompt even appears, disproving the assumption
    // that a shell's line editor never touches that field. A second,
    // shared, binary-agnostic env var is far simpler and, unlike
    // termios state, is never touched by the shell itself at all: SET
    // by BOTH binaries' own Shell start (this call, and start_shell()'s
    // identical one), CHECKED by BOTH binaries' own main() alongside
    // their own USILIC/U -- present but the binary's OWN var absent
    // means "I'm in someone ELSE's live SHELL", refuse to launch a
    // confusing independent session ("Invalid Terminal") instead.
    setenv("TCB_LIVE_SHELL", "1", 1);
    auto cargv = toArgv(options_.argv);
    execvp(cargv[0], cargv.data());
    _exit(127);  // execvp() failed -- never fall through and keep running as usilic
  }

  childPid_ = pid;
  masterFd_ = master;
  fcntl(masterFd_, F_SETFL, fcntl(masterFd_, F_GETFL) | O_NONBLOCK);

  // Disable ECHO (and ECHONL) before the setup command below is sent
  // -- otherwise the KERNEL's own line-discipline echo of that
  // multi-line burst can reach waitForHandshake()'s scan buffer near-
  // instantly, well before the child shell has actually read and
  // EXECUTED any of it (writing to a pty and the child reading from
  // it are two independent, racing operations; only the kernel echo
  // is synchronous with the write itself). Since the old sentinel
  // command (a silent `:` no-op) produced no output of its own,
  // waitForHandshake() had no way to tell "the sentinel TEXT is
  // visible" apart from "the shell has actually GOTTEN to and run the
  // sentinel line" -- it could and did report success from echo alone
  // while the real shell was still seconds away from even starting
  // its setup, and every prompt it went on to draw for each
  // intervening setup line (in the shell's own STOCK format, since
  // PROMPT/PS1 hadn't taken effect yet) then leaked into scrollback_
  // as ordinary post-handshake content instead of being swept up by
  // this function's own "discard everything through the sentinel"
  // logic below. A real user report (2026-08-02, screenshot): right
  // after SHELL started, before any real command had been run, several
  // blank lines and a stray "user@host dir %"-style prompt fragment
  // already sat in scrollback above where the real first prompt
  // should have been -- reproduced and confirmed via a PTY test that
  // dumped the raw byte stream and found the shell's own STOCK-format
  // prompt text present well before setup had genuinely finished.
  // buildSetupCommand()'s sentinel is now a real `echo` the shell
  // itself must actually reach and execute to produce, which combined
  // with ECHO being off here means the ONLY way that sentinel text can
  // reach this read loop is genuine, sequential shell execution --
  // exactly what this function's existing "discard up to the
  // sentinel" logic was always designed to rely on. The shell's own
  // line editor (bash's readline / zsh's ZLE) reconfigures the pty's
  // echo/canonical settings for itself once it starts controlling
  // interactive input/redraw, so this early override doesn't linger
  // into normal post-setup interactive use.
  struct termios setupTio;
  if (tcgetattr(masterFd_, &setupTio) == 0) {
    setupTio.c_lflag &= ~(ECHO | ECHONL);
    tcsetattr(masterFd_, TCSANOW, &setupTio);
  }

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
    if (consumeEscapeByte(c)) continue;

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

// See the class's own declaration comment.
bool Shell::consumeEscapeByte(unsigned char c) {
  switch (escapeState_) {
    case EscapeKind::None:
      if (c != 0x1b) return false;
      escapeState_ = EscapeKind::JustEsc;
      return true;
    case EscapeKind::JustEsc:
      if (c == '[') {
        escapeState_ = EscapeKind::Csi;
      } else if (c == ']') {
        escapeState_ = EscapeKind::Osc;
      } else {
        // A simple two-byte escape (ESC + one byte -- e.g. ESC =/>/M
        // for keypad/cursor modes). Slightly over-eager for the rarer
        // three-byte forms (e.g. ESC ( B charset designation), which
        // would leave one real byte ('B') incorrectly swallowed too --
        // an accepted, disclosed tradeoff: those don't show up in
        // practice from a real interactive shell's OWN output, unlike
        // CSI/OSC/bracketed-paste, which do on every session.
        escapeState_ = EscapeKind::None;
      }
      return true;
    case EscapeKind::Csi:
      // Parameter bytes (0x30-0x3F) and intermediate bytes (0x20-0x2F)
      // keep the sequence going; a final byte (0x40-0x7E) ends it.
      if (c >= 0x40 && c <= 0x7e) escapeState_ = EscapeKind::None;
      return true;
    case EscapeKind::Osc:
      if (c == 0x07) {
        escapeState_ = EscapeKind::None;  // BEL terminator
      } else if (c == 0x1b) {
        escapeState_ = EscapeKind::OscEscPending;  // possibly the start of an ST (ESC \) terminator
      }
      return true;
    case EscapeKind::OscEscPending:
      // Whether this byte is the '\' completing a real ST or not,
      // either way the OSC sequence is done being scanned past.
      escapeState_ = EscapeKind::None;
      return true;
  }
  return false;
}

// A real bug found via actual zsh use (2026-08-01): zsh's own ZLE
// commonly echoes a command by drawing one character, then a literal
// backspace, then the FULL final text -- e.g. typing "unset ..." over
// the wire arrives as "u\x08unset ..." (draw 'u', erase it, redraw
// the whole word) -- a completely invisible no-op on a real terminal
// (backspace just moves the cursor back one column before the redraw
// overwrites it), but appendBodyByte() was feeding the raw backspace
// BYTE itself into PipeBuffer as literal content, which has no cursor
// concept at all: it just showed up as a visible reverse-video
// control-character glyph in the middle of otherwise-clean text.
// pendingLine_ is a small line-assembly buffer THIS class owns
// (separate from PipeBuffer's own internal line-splitting, which
// still only ever sees complete, backspace/CR-resolved lines) so
// backspace/bare-CR can be resolved BEFORE anything reaches
// PipeBuffer, the same way a real terminal's own line buffer would:
//   - backspace (0x08): drop the last byte already buffered for the
//     current line (a plain ASCII-scoped pop -- not UTF-8-aware --
//     since this exists specifically for ZLE's OWN redraw of this
//     port's own synthetic, ASCII-only setup commands, not general
//     multibyte content).
//   - bare CR (\r NOT immediately followed by \n): a real "return to
//     column 0, about to overwrite" -- discards whatever's buffered
//     for the current line so far, same net effect as backspacing it
//     all away, without needing to guess how many columns wide it was.
//   - CR immediately followed by LF: an ordinary CRLF line ending,
//     the \r itself contributes nothing and is dropped.
//   - anything else: appended normally; a trailing '\n' flushes the
//     now-complete line to pipeBuffer_.feed() in one call.
void Shell::appendBodyByte(unsigned char c) {
  if (sawBareCr_) {
    sawBareCr_ = false;
    if (c == '\n') {
      pendingLine_.push_back('\n');
      pipeBuffer_.feed(pendingLine_);
      pendingLine_.clear();
      return;
    }
    pendingLine_.clear();
    // fall through: `c` itself still needs handling below
  }
  if (c == '\r') {
    sawBareCr_ = true;
    return;
  }
  if (c == 0x08) {
    if (!pendingLine_.empty()) pendingLine_.pop_back();
    return;
  }
  pendingLine_.push_back(static_cast<char>(c));
  if (c == '\n') {
    pipeBuffer_.feed(pendingLine_);
    pendingLine_.clear();
  }
}

// A partial (not yet newline-terminated) line accumulated in
// pendingLine_ needs to reach pipeBuffer_ too whenever the caller is
// about to consult pipeBuffer_.lines()/flush() -- otherwise up to one
// whole in-progress line of real output would simply vanish. Shared
// by handlePromptDetected()'s own pipeBuffer_.flush() call.
void Shell::flushPendingLine() {
  if (pendingLine_.empty()) return;
  pipeBuffer_.feed(pendingLine_);
  pendingLine_.clear();
}

void Shell::flushNewScrollbackLines() {
  const auto &plines = pipeBuffer_.lines();
  if (plines.size() <= linesConsumed_) return;
  std::size_t start = linesConsumed_;
  // pendingEchoFilter_'s own comment: drop exactly the first new line
  // if it's the shell's own line-editor echo of the command just
  // submitted (either the literal text, bash-style, or a bare blank
  // line, zsh-style) -- checked/cleared here, the first point after
  // submission where any new line becomes visible at all, so it never
  // matters how many separate pty reads the echo happened to arrive
  // across.
  if (pendingEchoFilter_) {
    const std::string &first = plines[start].text;
    if (first == *pendingEchoFilter_ || first.empty()) start++;
    pendingEchoFilter_.reset();
  }
  std::vector<ContentView::Line> newLines(scrollback_.lines());
  newLines.reserve(plines.size());
  for (std::size_t i = start; i < plines.size(); i++)
    newLines.push_back({plines[i].text, std::nullopt, std::nullopt, {}});
  linesConsumed_ = plines.size();
  scrollback_.setLines(std::move(newLines));
  exitHistoryBrowsing();  // new output arriving means "back to live", see its own declaration comment
  // Belt-and-suspenders, not load-bearing (2026-08-04): now that
  // scrollback_.setBottomAnchored(true) (see the constructor's own
  // comment) keeps the last real content line permanently pinned
  // immediately above commandLineRow()'s own now-FIXED row, content
  // growth here can no longer collide with or overwrite the prompt no
  // matter how many separate flushes a command's output arrives
  // across -- the two EARLIER, now-obsolete bugs these two calls were
  // originally added to work around (commandLine_'s cached field going
  // stale, then the prompt's own on-screen text getting silently
  // overwritten by a later content redraw) can't recur structurally.
  // Kept anyway: harmless (commandLineRow() is now a pure constant, so
  // updateCommandLineField() just recomputes the same value; a
  // repeated identical redrawPromptRow() is a handful of writes plus
  // one flush -- see its own comment) and keeps this function self-
  // contained rather than relying on callers elsewhere to redraw the
  // prompt at the right moment.
  updateCommandLineField();
  redrawPromptRow();
}

// Appends a PERMANENT record of the just-submitted prompt+command as
// its own scrollback_ line (with `command` attached via Line::
// shellCommand for later resubmission) -- ported from legacy's own
// data model: shell.c's PP chain has no separate "live prompt widget"
// at all, the prompt row IS just another node in the SAME linked list
// as every other line of scrollback, which is exactly what lets
// M_kup/M_kdn move a cursor directly onto a past prompt and re-run it
// via '\r' (see this class's own comment on ^P/^N, and Application::
// feedShellByte()'s Enter handling). Called from submitLine() BEFORE
// the command is actually sent, so the frozen line's own position in
// scrollback_ matches the moment it was submitted -- any output the
// command produces naturally appends AFTER it via the normal
// flushNewScrollbackLines() path, same as legacy's chain growing past
// the prompt node once the command starts producing output.
// 2026-08-24 fix, a real user report: a long CP/MV/ARC-injected
// command line (a full path plus a multi-byte Japanese filename easily
// exceeds `columns_`, far more readily than anything a user would
// hand-type) was silently CUT OFF mid-line in the live SHELL scrollback
// (LOG mode, which reads pipeBuffer_'s own already-wrapped output, was
// unaffected -- this line never went through pipeBuffer_ at all, see
// below). ContentView's own row rendering has no wrapping of its own
// -- every OTHER scrollback line reaches it already wrapped to fit
// `columns_` by pipeBuffer_.wrapLine() (see flushNewScrollbackLines()'s
// own comment), which this function bypassed entirely by building one
// long ContentView::Line directly. Routes the same text through a
// throwaway PipeBuffer instance instead, purely to reuse its already-
// correct (and already-tested elsewhere) UTF-8/EUC-aware width-wrapping
// -- cheaper than re-implementing character-boundary-safe wrapping a
// second time here. `command` (for ^P/^N-cursor resubmission, see this
// function's own class comment) is attached to EVERY wrapped segment,
// not just the first, so resubmission works regardless of which
// visual row the cursor happens to land on.
void Shell::freezePromptLine(const std::string &command) {
  PipeBuffer wrapper(options_.encoding, columns_, /*maxBytes=*/1 << 20);
  wrapper.feed(promptPlain_ + command + "\n");
  wrapper.flush();
  std::vector<ContentView::Line> newLines(scrollback_.lines());
  for (const auto &wrapped : wrapper.lines()) newLines.push_back({wrapped.text, command, std::nullopt, {}});
  scrollback_.setLines(std::move(newLines));
  exitHistoryBrowsing();  // a freshly-submitted command means "back to live", see its own declaration comment
}

// Clears every piece of per-session state before start() spawns a new
// child -- ported from legacy's own do_shell(): once the underlying
// shell process (and, with it, output_shell()'s own relay process)
// has died, the NEXT entry into SHELL allocates a genuinely fresh PP
// chain (print_shell()'s own "SHELL version..." banner line + first
// empty prompt) rather than continuing to append to the old one. A
// real user request (2026-08-02): "exit[Enter]でSHELLが終了し、再起動
// では再び冒頭行から始まる...このほうがSHELLの存在意義がはっきり
// する" -- the user explicitly preferred this over the port's own
// design (Shell as a single long-lived session surviving across ^G)
// once they compared it against the legacy CUI side by side, precisely
// because it makes clear SHELL is a thin, purpose-built launcher for
// file operations/commands, not a persistent terminal replacement.
// Without this, calling start() again after terminate() would spawn a
// new child while every other member here (scrollback_'s lines_,
// pipeBuffer_'s partially-wrapped state, the markerPrefix_ match
// state, etc.) still held onto the dead session's own leftovers.
void Shell::resetSessionState() {
  pipeBuffer_ = PipeBuffer(options_.encoding, columns_, options_.scrollbackBudget);
  pendingLine_.clear();
  linesConsumed_ = 0;
  pendingEchoFilter_.reset();
  markerMatchPos_ = 0;
  markerMatchBuffer_.clear();
  inPromptTail_ = false;
  promptTailBuffer_.clear();
  escapeState_ = EscapeKind::None;
  promptPlain_.clear();
  currentDirectory_.clear();
  scrollback_.setLines({});
  commandLine_.setText("");
}

int Shell::promptGeneration() const { return promptGeneration_; }

void Shell::handlePromptDetected(const std::string &tailBeforeMark) {
  promptGeneration_++;
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
  // 2026-08-17 fix: zsh's default PS1 (`%~`) -- and bash's `\w` too --
  // abbreviate the cwd with a leading '~' whenever it's $HOME itself or
  // a subdirectory of it (e.g. "~" or "~/git/usilic"), which used to be
  // stored into currentDirectory_ completely unexpanded. A real report
  // ("SHELLで'less CLA'^I（補完が利かない）", comparing directly against
  // legacy's own working completion) traced back to setCompletionProvider()'s
  // completeFilesystemPath() calling std::filesystem::directory_iterator()
  // on this raw "~"-prefixed string, which isn't a real filesystem path
  // (tilde expansion is a SHELL convention, never something the OS or
  // std::filesystem understands) -- silently returning zero candidates
  // every time the shell's cwd was anywhere under $HOME, which in
  // practice is nearly always. Expanded here, the one place every
  // consumer of currentDirectory_ (completion, the mode-line's own
  // trailing2 display, directoryChangedCallback_) already funnels
  // through.
  if (const char *home = std::getenv("HOME"); home && *home) {
    std::string homeStr = home;
    if (dir == "~") {
      dir = homeStr;
    } else if (dir.size() > 1 && dir[0] == '~' && dir[1] == '/') {
      dir = homeStr + dir.substr(1);
    }
  }
  if (dir != currentDirectory_) {
    currentDirectory_ = dir;
    if (directoryChangedCallback_) directoryChangedCallback_(dir);
  }

  flushPendingLine();
  pipeBuffer_.flush();
  flushNewScrollbackLines();

  // 2026-08-17 change: the visible prompt prefix is "usilic-1.5b:",
  // NOT options_.programName (still just "usilic") -- programName
  // itself is left untouched since it also feeds markerPrefix_ (the
  // internal PTY-output detection sentinel, an unrelated concept from
  // what's actually drawn on screen) and Shell::programName() (the
  // mode-line's own "(zsh)"/"(bash)" badge, the CHILD shell's name,
  // not this program's). Requested directly, and bold now covers the
  // whole "usilic-1.5b:" including the colon (previously only the
  // name itself was bold, leaving the colon plain) so the LOG feature
  // (docs/CHANGES.md, LOG section) can later distinguish a prompt line
  // recorded to the log from a user manually typing the same plain-text
  // string at a prompt.
  static const std::string kPromptDisplayName = "usilic-1.5b";
  promptPlain_ = kPromptDisplayName + ":" + tailBeforeMark;
  updateCommandLineField();
  commandLine_.setText("");

  promptDecorated_ = display_.attributeEscape(Display::Attribute::Bold) + kPromptDisplayName + ":" +
                      display_.attributeEscape(Display::Attribute::BoldOff) + tailBeforeMark;
  redrawPromptRow();
}

// Right after SHELL starts (or scrollback_ is otherwise short of a
// full viewport), ANY scrollback_ mutation -- freezePromptLine(),
// moveUp()/moveDown() (^P/^N), goToBottom() -- runs a FULL
// ContentView::redraw() that unconditionally clears every row from 0
// to height_-1, including commandLineRow() (scrollback_'s own LAST
// content row, which is exactly where the live prompt lives) --
// ContentView has no line to draw there once lines_.size() is less
// than a full viewport, so that row is simply left blank. A plain
// commandLine_.redraw() only repaints the EDITABLE FIELD portion
// (from promptCol onward, see updateCommandLineField()) -- the bold
// PROMPT PREFIX TEXT ITSELF is drawn separately, once, only inside
// handlePromptDetected(), so nothing else was ever re-issuing it. A
// real user report (2026-08-02): "最初の起動直後および^Pで SHELLプロン
// プトが消える" -- reproduced via PTY for both cases: right after
// ensureShellActive()'s own scrollback_.redraw() call ran AFTER the
// prompt had already been drawn during the handshake, and after ^P's
// moveUp() (which now drives scrollback_'s own cursor directly, see
// Application::feedShellByte()'s own comment) triggered the same
// full redraw with a short scrollback. Every call site that follows a
// scrollback_ mutation with a bare commandLine_.redraw() needed to
// become this instead.
void Shell::redrawPromptRow() {
  int promptRow = commandLineRow();
  display_.moveCursor(promptRow, 0);
  display_.clearToEndOfLine();
  display_.write(promptDecorated_);
  display_.flush();
  commandLine_.redraw();
}

void Shell::updateCommandLineField() {
  int promptCol = displayWidth(promptPlain_);
  int width = std::max(1, columns_ - promptCol);
  commandLine_.setField(commandLineRow(), promptCol, width);
}

int Shell::commandLineRow() const {
  // A fixed constant again (2026-08-04 revision) -- see the
  // constructor's own comment for why this is safe now: scrollback_'s
  // own bottom-anchoring guarantees the last real content line is
  // ALWAYS immediately above this row, so nothing here needs to track
  // how much content currently exists the way the previous (2026-08-03)
  // content-relative revision did.
  if (viewportRows_ <= 1) return 0;
  return viewportRows_ - 2;
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
// Ported from output_shell()'s PS1/eos setup line -- but ALL of its
// statements below, including the sentinel echo, are chained into
// ONE logical shell command via backslash-newline continuation,
// matching legacy's own single semicolon-chained line (shell.c),
// rather than being sent as several independent \n-terminated
// commands (as an earlier version of this method did). The two
// historical bugs that shaped this method turned out to sit on
// different axes and both had to be satisfied at once:
//   - A real bug found via actual zsh use (2026-08-01): a single
//     PHYSICAL line long enough to exceed ~80 columns (mark+
//     programName+the whole assignment easily does) WRAPS before the
//     shell's own line editor (zsh's ZLE, bash's readline too) ever
//     gets to execute it, and a wrapped input line's own redraw logic
//     moves the cursor around and selectively overwrites what's
//     already on screen -- fundamentally incompatible with this
//     class's own byte consumer (appendBodyByte() -> PipeBuffer),
//     which only understands "append this byte," with no concept of
//     cursor movement/overwrite. Symptom: doubled-up command-text
//     fragments (e.g. "uunset", "tt PROMPT") in scrollback.
//   - A real user report (2026-08-02, screenshot): right after SHELL
//     started, before any real command had been run, several blank
//     lines and a stray "user@host dir %"-style prompt fragment
//     already sat in scrollback above where the real first prompt
//     should have been. Root cause: sending each statement as its own
//     separate \n-terminated command makes each one a fully separate
//     INTERACTIVE command, and an interactive shell draws a fresh
//     prompt after every one of them -- including the early ones,
//     before PROMPT/PS1 below has taken effect, so those used the
//     shell's own stock/unmodified prompt format.
//     processOutputBytes()'s markerPrefix_ scan only recognizes
//     prompts in THIS class's own mark+prog+":" format, so every one
//     of those early wrong-format prompt draws fell through and
//     leaked into scrollback as literal text instead of being
//     recognized and consumed as a prompt.
// Backslash-continuation resolves both at once: each individual
// PHYSICAL line stays short (first bug avoided), and the shell reads
// the whole thing as ONE logical command before executing any of it
// or drawing a fresh prompt (second bug avoided) -- exactly one
// prompt redraw happens for the entire setup, and it happens only
// after PROMPT/PS1 has already taken effect.
std::string Shell::buildSetupCommand() const {
  const std::string &mark = options_.promptMark;
  const std::string &prog = options_.programName;
  // A real `echo` here, not the silent `:` no-op this used to be --
  // see start()'s own comment (where ECHO is now disabled on the pty
  // before this is sent) for why: with kernel input-echo off, the
  // sentinel text can now ONLY reach waitForHandshake()'s read loop by
  // the shell actually EXECUTING this line and printing it as real
  // output, which is exactly the guarantee "discard everything through
  // the sentinel" needs to safely sweep up every earlier setup
  // statement's own (possibly stale-prompt-format) output too. `$$`
  // (the shell's own PID, always equal to childPid_ since this child
  // is fork/exec'd directly with no intermediate wrapper) rather than
  // childPid_'s digits spelled out literally -- a real user report
  // (2026-08-02, screenshot) traced a persistent leaked "#eos<pid>#"
  // line in scrollback right after SHELL started back to exactly this:
  // if the shell hasn't yet reasserted its own control over the pty's
  // echo/canonical settings (start()'s ECHO-disable happens once,
  // early, and evidently doesn't always survive the child's own
  // startup), the literal, unexpanded COMMAND TEXT sent here --
  // "echo '#eos12345#'" -- gets echoed back BEFORE the shell ever
  // executes it, and that echoed text itself already contains the
  // exact sentinel substring "#eos12345#" (just wrapped in `echo '...'`
  // rather than being real output). waitForHandshake()'s scan matches
  // on this far-too-early false positive instead of the shell's own
  // later, genuine output. Spelled out with `$$` instead, the RAW
  // command text sent here literally contains "#eos$$#" -- no run of
  // digits at all -- so it can never accidentally contain the fully-
  // expanded sentinel string itself; only the shell's OWN evaluation
  // of `$$` (which happens only once this line is actually executed,
  // by definition after everything before it in the same chained
  // command) can produce a match. Double-quoted rather than single-
  // quoted specifically so `$$` still expands (`#` inside stays safe
  // from comment-parsing either way, being mid-word inside quotes).
  std::string eos = "echo \"#eos$$#\"";
  // Joins statements into one logical command: " ; \<newline>" between
  // each pair (statement separator + line continuation, so the shell
  // keeps reading instead of executing/prompting after each physical
  // line) and a plain newline after the last one to actually submit it.
  auto chain = [](const std::vector<std::string> &statements) {
    std::string result;
    for (std::size_t i = 0; i < statements.size(); i++) {
      result += statements[i];
      result += (i + 1 < statements.size()) ? " ; \\\n" : "\n";
    }
    return result;
  };
  if (isBash_) {
    return chain({
        "unset PROMPT_COMMAND",
        "export PS1='" + mark + prog + ":\\w\\$ " + mark + "'",
        "set -o ignoreeof",
        "export COLUMNS=" + std::to_string(columns_),
        "unset HISTFILE",
        eos,
    });
  }
  if (isZsh_) {
    // zsh's OWN prompt-expansion sequences (%~ for cwd, %# for the
    // trailing '#'-if-root/'%'-otherwise prompt character) are the
    // SAME ones already used below for the tcsh/csh fallback -- those
    // aren't csh-specific syntax, just prompt-expansion tokens zsh
    // happens to share with tcsh. What zsh does NOT share is tcsh's
    // `set VAR = value` assignment form (zsh's own `set` builtin is
    // for shell OPTIONS, not variables) -- so this needs its own
    // branch for the ASSIGNMENT syntax alone. precmd_functions=() and
    // RPROMPT='' silence any theme's own dynamic prompt hooks (oh-my-
    // zsh/powerlevel10k/starship precmd hooks, right-side prompt
    // text) that would otherwise still inject ANSI/Unicode content
    // around this synthetic prompt, matching bash's `unset
    // PROMPT_COMMAND` above. IGNORE_EOF is zsh's `set -o ignoreeof`
    // equivalent (^D shouldn't be able to kill the shell child out
    // from under this port). PROMPT_EOL_MARK='' disables zsh's own
    // "previous output didn't end with a newline" indicator (its
    // real, documented default behavior -- a reverse-video '%'
    // padded to the terminal width -- found via actual use: even
    // with consumeEscapeByte() now stripping the raw ANSI codes
    // around it, the '%' GLYPH ITSELF is real printable content, not
    // an escape sequence, so stripping alone can't remove it; turning
    // the feature off at the source is simpler than trying to also
    // recognize and hide zsh's own specific eol-mark text).
    return chain({
        "unset PROMPT_COMMAND 2>/dev/null",
        "precmd_functions=()",
        "export PROMPT='" + mark + prog + ":%~%# " + mark + "'",
        "RPROMPT=''",
        "PROMPT_EOL_MARK=''",
        "setopt IGNORE_EOF",
        "export COLUMNS=" + std::to_string(columns_),
        "unset HISTFILE",
        eos,
    });
  }
  // csh/tcsh fallback -- unlike bash/zsh above, NOT verified against a
  // real csh/tcsh session (no evidence either was ever exercised in
  // practice); kept as the pre-existing best-effort default rather
  // than guessed at further. Uses the same real-`echo` sentinel and
  // same chained-into-one-command approach as bash/zsh above now (a
  // bare "#eos<pid>#" comment line would print nothing at all with
  // ECHO disabled -- see start()'s own comment -- and csh/tcsh's `#`
  // is unconditionally a comment character, so it couldn't lean on a
  // `:`-style no-op prefix either way).
  return chain({
      "set prompt = \"" + mark + prog + ":%~%# " + mark + "\"",
      "set ignoreeof",
      eos,
  });
}

// One-shot startup handshake: discard everything up through our own
// "#eos<pid>#" sentinel, exactly like output_shell()'s pre-main-loop
// scan -- the terminal emulator (e.g. GNOME Terminal/VTE's
// /etc/profile.d/vte.sh) may have already auto-drawn the shell's very
// first prompt, using its OWN prompt-hook mechanism as it stood
// *before* the setup command above unset/cleared it, before the
// shell ever reads that command; without skipping past it, that
// leftover output would otherwise be misparsed as real prompt/
// content bytes by the main loop. See CLAUDE.md history for why a
// bare '#' isn't enough (it can turn up by coincidence in that
// leftover output) -- this pid-based sentinel is unique enough to
// avoid that for every shell family buildSetupCommand() now echoes it
// for (bash/zsh/csh-tcsh all alike), not just bash.
//
// A real bug found via actual zsh use (2026-08-01): once the sentinel
// is found, everything read here up to THAT POINT is correctly
// discarded (never reaches appendBodyByte()) -- but a single read()
// often returns the sentinel and the start of the very NEXT real
// prompt in the SAME chunk (the shell doesn't pause between finishing
// the setup line and drawing its next prompt). The old code discarded
// the WHOLE scan buffer unconditionally on a match, silently dropping
// that trailing real content -- often the leading ESC byte of the
// next prompt's own escape sequences, leaving just the REST of a
// sequence (e.g. "[27m" with no ESC in front) to flow into
// processOutputBytes() as literal garbage text. The bytes after the
// sentinel are fed through normally instead of discarded -- but see
// below for why "found the sentinel" alone isn't yet "safe to feed
// what follows."
//
// A real user report (2026-08-02, screenshot): even after the sentinel
// itself became a real, shell-emitted `echo` (see buildSetupCommand()'s
// own comment) and ECHO was disabled on the pty (see start()'s own
// comment), a short leaked fragment (zsh's own "> " PS2 continuation-
// prompt indicator, from ZLE still mid-read of the backslash-continued
// setup command at the exact moment its OWN read() call returned) could
// still occasionally end up in scrollback_ right after SHELL started.
// The sentinel's own echo proves the shell has EXECUTED the setup
// command, but says nothing about whether every byte the shell is
// about to write as a RESULT of that (its real first prompt, in the
// now-correctly-configured mark+prog+":" format) has already arrived
// in this read loop's buffer -- reads and writes on the two ends of a
// pty are independent, racing operations. Rather than trust the first
// read() that contains the sentinel, this now keeps draining
// (bounded by the same maxAttempts loop) until a full 20ms poll
// interval passes with nothing more arriving (a "settle" gap, cheap to
// detect since the shell's own post-setup burst is always contiguous
// and fast), and then looks for markerPrefix_ (mark+prog+":", the
// fixed, unmistakable prefix of the REAL prompt this class already
// knows how to recognize during normal operation) within everything
// that arrived after the sentinel -- feeding only from THAT point
// onward, so any stray PS2/other fragment between the sentinel and the
// real prompt is discarded rather than leaking into scrollback as
// literal text. If markerPrefix_ never turns up before the settle gap
// (e.g. an unexpected prompt format), everything after the sentinel is
// fed through unfiltered, same as this function did before this fix --
// not a regression, just no longer the ONLY behavior.
bool Shell::waitForHandshake() {
  std::string sentinel = "#eos" + std::to_string(childPid_) + "#";
  std::string scan;
  bool sentinelFound = false;
  std::string postSentinel;
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
    if (rc == 0) {
      if (sentinelFound) break;  // settle gap: the post-sentinel burst has finished arriving
      continue;
    }
    if (pfd.revents & (POLLHUP | POLLERR)) return false;
    char buf[512];
    ssize_t n = read(masterFd_, buf, sizeof(buf));
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
      return false;
    }
    if (!sentinelFound) {
      scan.append(buf, static_cast<std::size_t>(n));
      if (std::size_t pos = scan.find(sentinel); pos != std::string::npos) {
        sentinelFound = true;
        postSentinel = scan.substr(pos + sentinel.size());
      } else if (scan.size() > 4096) {
        scan.erase(0, scan.size() - sentinel.size());
      }
      continue;
    }
    postSentinel.append(buf, static_cast<std::size_t>(n));
  }
  if (!sentinelFound) return false;
  if (!markerPrefix_.empty()) {
    if (std::size_t markerPos = postSentinel.find(markerPrefix_); markerPos != std::string::npos) {
      postSentinel.erase(0, markerPos);
    }
  }
  if (!postSentinel.empty()) processOutputBytes(postSentinel);
  return true;
}

LineEditor &Shell::commandLine() { return commandLine_; }
ContentView &Shell::scrollback() { return scrollback_; }

void Shell::submitLine() {
  std::string line = commandLine_.text();
  std::size_t start = line.find_first_not_of(' ');
  std::string trimmed = (start == std::string::npos) ? "" : line.substr(start);

  if (!trimmed.empty()) {
    freezePromptLine(trimmed);

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
      pendingEchoFilter_ = trimmed;  // see its own comment -- consumed by flushNewScrollbackLines()
      writeToPty(trimmed + "\n");
      if (historyRecorder_) historyRecorder_(trimmed);
    }
  } else {
    writeToPty("\n");
  }

  commandLine_.setText("");
  redrawPromptRow();
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

std::string Shell::programName() const {
  return options_.argv.empty() ? "" : baseName(options_.argv[0]);
}

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
  scrollback_.setViewport(0, std::max(1, rows - 2));  // see the constructor's own comment
  scrollback_.setModeLineRow(rows - 1);
  updateCommandLineField();
  if (masterFd_ >= 0) {
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(columns);
    ioctl(masterFd_, TIOCSWINSZ, &ws);
  }
  scrollback_.redraw();
  redrawPromptRow();
}

void Shell::setEncoding(Charset::Encoding encoding) {
  options_.encoding = encoding;
  commandLine_.setEncoding(encoding);
}

}  // namespace fm
