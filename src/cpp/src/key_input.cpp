#include "key_input.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace fm {

struct KeyInput::RawState {
  struct termios original {};
  struct termios raw {};
  bool haveOriginal = false;
};

namespace {

// Single active KeyInput, mirroring TerminalDisplay's g_active: real
// POSIX signal handlers are plain `void(int)` function pointers with
// nowhere to carry instance data, and usilic has only ever run as one
// foreground input handler at a time.
KeyInput *g_active = nullptr;
volatile std::sig_atomic_t g_resizePending = 0;
volatile int g_foregroundChildPid = 0;

void handleWinch(int) {
  std::signal(SIGWINCH, handleWinch);
  g_resizePending = 1;
  if (g_foregroundChildPid && !kill(g_foregroundChildPid, 0)) {
    // Ported from key_winch()'s `vt_mode('w')`: legacy's 'W' flag
    // recorded "a resize happened while the SHELL's child is alive"
    // for the SHELL class (not yet ported) to notice and forward.
    // There's nowhere to forward that to yet, so this is currently a
    // no-op beyond the kill() liveness probe it was already doing;
    // left as a hook point (see the class comment on excluded scope).
  }
}

void handleTtou(int) {
  std::signal(SIGTTOU, handleTtou);
  if (g_active) g_active->reportPid();
  if (g_active && g_active->errorPending()) kill(getpid(), SIGTSTP);
  else killpg(getpgrp(), SIGTSTP);
}

void handleCont(int) {
  std::signal(SIGCONT, handleCont);
  if (g_active) g_active->reportPid();
  if (g_active) g_active->reenterRawMode();
}

// Duplicated, not shared with terminal_display.cpp's windowSize():
// both classes independently need to query the controlling terminal's
// size for their own reasons (Display for its initial geometry at
// construction; KeyInput here for handling a live resize signal), and
// the ioctl call itself is a stable ~10-line OS primitive not worth
// forcing an artificial coupling between the two classes over.
bool queryWindowSize(int fd, int *rows, int *columns) {
  struct winsize ws;
  ws.ws_row = 0;
  if (fd < 0 || ioctl(fd, TIOCGWINSZ, &ws) < 0 || !ws.ws_row) return false;
  *rows = ws.ws_row;
  *columns = ws.ws_col;
  return true;
}

}  // namespace

KeyInput::KeyInput(Display &display) : display_(display), raw_(new RawState) {
  g_active = this;
}

KeyInput::~KeyInput() {
  if (g_active == this) g_active = nullptr;
  delete raw_;
}

void KeyInput::reportPid() {
  if (pidReporter_) pidReporter_(getpid());
}

bool KeyInput::errorPending() const { return errorPending_; }

int KeyInput::readByte() {
  if (macroPos_ < macroQueue_.size())
    return static_cast<unsigned char>(macroQueue_[macroPos_++]);
  if (macroPos_ && macroPos_ >= macroQueue_.size()) {
    macroQueue_.clear();
    macroPos_ = 0;
  }
  if (fd_ < 0) return 0;
  unsigned char c;
  ssize_t n = read(fd_, &c, 1);
  if (n <= 0) return 0;
  // Ctrl-Space arrives as a literal NUL on many terminals; 0 is
  // already "nothing read" above, so report it via the same 0x80
  // sentinel legacy vt_getch() used (K_at).
  return c ? c : 0x80;
}

void KeyInput::open(const std::string &ttyPath) {
  int fd = ::open(ttyPath.c_str(), O_RDONLY);
  fd_ = (fd < 0) ? 2 : fd;  // legacy vt_init(1): fall back to stderr's fd
  if (!isatty(fd_)) {
    std::fprintf(stderr, "usilic: Invalid terminal.\n");
    if (fd_ != 2) ::close(fd_);
    fd_ = -1;
    return;
  }
  tcgetattr(fd_, &raw_->original);
  raw_->haveOriginal = true;
  raw_->raw = raw_->original;
  raw_->raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  raw_->raw.c_oflag &= ~OPOST;
  raw_->raw.c_oflag &= ~ONLCR;
  raw_->raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN | NOFLSH);
  raw_->raw.c_cflag &= ~(CSIZE | PARENB);
  raw_->raw.c_cflag |= CS8;
  raw_->raw.c_cc[VMIN] = 0;
  raw_->raw.c_cc[VTIME] = 1;
  tcsetattr(fd_, TCSANOW, &raw_->raw);
}

void KeyInput::leaveRawMode() {
  if (fd_ < 0 || !raw_->haveOriginal) return;
  tcsetattr(fd_, TCSANOW, &raw_->original);
}

void KeyInput::reenterRawMode() {
  if (fd_ < 0 || !raw_->haveOriginal) return;
  tcsetattr(fd_, TCSANOW, &raw_->raw);
}

void KeyInput::close() {
  leaveRawMode();
  if (fd_ >= 0 && fd_ != 2) ::close(fd_);
  fd_ = -1;
}

void KeyInput::setPolling(bool polling) {
  if (fd_ < 0 || !raw_->haveOriginal) return;
  raw_->raw.c_cc[VMIN] = 0;
  raw_->raw.c_cc[VTIME] = polling ? 1 : 0;
  tcsetattr(fd_, TCSANOW, &raw_->raw);
}

std::optional<KeyInput::SpecialKey> KeyInput::readEscapeSequence() {
  static const std::array<Display::Key, 6> kDisplayOrder = {
      Display::Key::Up,   Display::Key::Down,     Display::Key::Right,
      Display::Key::Left, Display::Key::PageUp,   Display::Key::PageDown,
  };
  static const std::array<SpecialKey, 6> kSpecialOrder = {
      SpecialKey::Up,   SpecialKey::Down,     SpecialKey::Right,
      SpecialKey::Left, SpecialKey::PageUp,   SpecialKey::PageDown,
  };
  std::array<std::string, 6> seqs;
  for (int i = 0; i < 6; i++) seqs[i] = display_.keySequence(kDisplayOrder[i]);
  if (seqs[0].empty()) return std::nullopt;

  // Ported quirk: every sequence is assumed to share Up's (seqs[0])
  // length -- read exactly that many more bytes after the already-
  // consumed leading ESC. This matches read_esc() exactly (it uses
  // strlen(ks[0]) the same way) rather than reading each candidate's
  // own length, so it's carried forward as-is, not fixed here.
  int remaining = static_cast<int>(seqs[0].size()) - 1;
  std::string esc(1, static_cast<char>(0x1b));
  for (int i = 0; i < remaining; i++) {
    int c = readByte();
    if (!c) break;
    esc.push_back(static_cast<char>(c));
  }
  if (esc.size() < 2) return std::nullopt;  // bare ESC, nothing followed

  int matched = -1;
  for (int i = 0; i < 6; i++) {
    if (seqs[i].size() < 3) continue;
    if (esc.size() > 2 && seqs[i][0] == esc[0] && seqs[i][2] == esc[2]) {
      matched = i;
      break;
    }
  }
  if (matched < 0) return std::nullopt;
  if (matched >= 4) readByte();  // PageUp/PageDown: discard trailing byte (e.g. '~')
  return kSpecialOrder[matched];
}

void KeyInput::installSignalHandlers() {
  std::signal(SIGWINCH, handleWinch);
  std::signal(SIGTTOU, handleTtou);
  std::signal(SIGCONT, handleCont);
}

bool KeyInput::consumeResizeSignal() {
  if (!g_resizePending) return false;
  g_resizePending = 0;
  int rows, columns;
  if (queryWindowSize(fd_, &rows, &columns)) display_.setSize(rows, columns);
  return true;
}

void KeyInput::setForegroundChildPid(int pid) { g_foregroundChildPid = pid; }

void KeyInput::setErrorPending(bool pending) { errorPending_ = pending; }

void KeyInput::setPidReporter(std::function<void(int)> reporter) {
  pidReporter_ = std::move(reporter);
}

void KeyInput::playRawMacro(const std::string &bytes) {
  macroQueue_ = bytes;
  macroPos_ = 0;
}

bool KeyInput::isPlayingRawMacro() const { return macroPos_ < macroQueue_.size(); }

}  // namespace fm
