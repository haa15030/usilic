#include "key_input.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <unistd.h>
#ifndef _WIN32  // Windowsのネイティブ(.steering/20261015-windows-native-gui/)は端末を読まない(常に-g)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace fm {

struct KeyInput::RawState {
#ifndef _WIN32
  struct termios original {};
  struct termios raw {};
#endif
  bool haveOriginal = false;
};

namespace {

// アクティブなKeyInputは1つだけ、TerminalDisplayのg_activeと同じ
// 考え方: 本物のPOSIXシグナルハンドラは素の`void(int)`関数ポインタ
// であり、インスタンスデータを運ぶ場所が無い。usilicはフォアグラウンド
// の入力ハンドラを常に一度に1つしか動かしたことが無い。
KeyInput *g_active = nullptr;
volatile std::sig_atomic_t g_resizePending = 0;
volatile int g_foregroundChildPid = 0;

#ifndef _WIN32
void handleWinch(int) {
  std::signal(SIGWINCH, handleWinch);
  g_resizePending = 1;
  if (g_foregroundChildPid && !kill(g_foregroundChildPid, 0)) {
    // key_winch()の`vt_mode('w')`からの移植: legacyの'W'フラグは
    // 「SHELLの子プロセスが生きている間にリサイズが起きた」ことを
    // 記録し、SHELLクラス(まだ移植されていない)がそれに気づいて
    // 転送するためのものだった。今のところそれを転送する先が無い
    // ため、これは現状、既に行っているkill()による生存確認以上の
    // ことはしない――フック地点として残してある(除外されている
    // スコープについてはクラスコメント参照)。
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
  // 2026-09-20追加、直接のユーザー依頼("^Zも実装して"、Application::
  // handleSuspend()自身のコメント参照): ジョブコントロールで再開
  // された直後(handleTtouの自己停止、またはApplication::
  // handleSuspend()自身のkill(SIGTSTP)のどちらが停止元でも共通)は、
  // 端末サイズが停止中に変わっているかもしれない(別のウィンドウへ
  // フォーカスが移っていた等)し、変わっていなくても画面の再描画は
  // 必要――既存のSIGWINCH検知の仕組み(g_resizePending/
  // consumeResizeSignal())にそのまま相乗りする。
  g_resizePending = 1;
}

// 重複させてあり、terminal_display.cppのwindowSize()とは共有して
// いない: どちらのクラスも、それぞれ独自の理由(Displayは構築時の
// 初期ジオメトリのため、こちらのKeyInputはライブのリサイズシグナル
// を処理するため)で、独立して制御端末のサイズを問い合わせる必要が
// ある。ioctl呼び出し自体は安定した約10行のOSプリミティブであり、
// これのために2クラス間に人工的な結合を強いる価値は無い。
bool queryWindowSize(int fd, int *rows, int *columns) {
  struct winsize ws;
  ws.ws_row = 0;
  if (fd < 0 || ioctl(fd, TIOCGWINSZ, &ws) < 0 || !ws.ws_row) return false;
  *rows = ws.ws_row;
  *columns = ws.ws_col;
  return true;
}

// 2026-10-13、直接のユーザー報告("-gの終わり方によってはエコーバックされなくなる...プログラム終了時に"reset^M"の
// 自動実行は？"): 端末を開いた時の設定(open()、エコーの無い壊れた設定なら人の使う設定に直したもの)を覚えておき、
// close()を通らずに終わった時(std::exit()・クラッシュ等)にも、atexit()と致命的なシグナルのハンドラから戻す。
// resetの代わりに、設定そのものと、画面の状態(スクロール領域・代替画面・マウス・カーソル)だけを戻す。
int g_restoreFd = -1;
struct termios g_restoreTermios {};

void restoreTerminalNow() {
  if (g_restoreFd < 0) return;
  tcsetattr(g_restoreFd, TCSANOW, &g_restoreTermios);
  static const char kReset[] = "\x1b[r\x1b[?1000l\x1b[?1002l\x1b[?1006l\x1b[?2004l\x1b[?1049l\x1b[?25h\x1b[0m";
  if (isatty(STDOUT_FILENO)) {
    ssize_t ignored = ::write(STDOUT_FILENO, kReset, sizeof kReset - 1);
    (void)ignored;
  }
  g_restoreFd = -1;
}

void restoreTerminalAtExit() { restoreTerminalNow(); }

void handleFatalSignal(int sig) {
  restoreTerminalNow();
  std::signal(sig, SIG_DFL);
  ::raise(sig);
}
#endif  // _WIN32

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

int KeyInput::fd() const { return fd_; }

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
  // 多くの端末でCtrl-Spaceはリテラルなnullとして届く。0は既に上で
  // 「何も読めなかった」を意味するため、legacyのvt_getch()が使って
  // いたのと同じ0x80という番兵(K_at)経由でそれを報告する。
  return c ? c : 0x80;
}

void KeyInput::open(const std::string &ttyPath) {
#ifdef _WIN32
  (void)ttyPath;
  fd_ = -1;
  return;
#else
  int fd = ::open(ttyPath.c_str(), O_RDONLY);
  fd_ = (fd < 0) ? 2 : fd;  // legacyのvt_init(1): 標準エラーのfdへフォールバック
  if (!isatty(fd_)) {
    std::fprintf(stderr, "usilic: Invalid terminal.\n");
    if (fd_ != 2) ::close(fd_);
    fd_ = -1;
    return;
  }
  tcgetattr(fd_, &raw_->original);
  // エコーも行単位の入力も無い設定(先に落ちたusilic等が生のモードのまま残したもの)は、元の設定として覚えると
  // 終了時にそれへ戻してしまうので、人が使う設定に直してから覚える(restoreTerminalNow()参照)。
  if (!(raw_->original.c_lflag & ECHO) && !(raw_->original.c_lflag & ICANON)) {
    raw_->original.c_lflag |= ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
    raw_->original.c_iflag |= ICRNL | BRKINT;
    raw_->original.c_oflag |= OPOST | ONLCR;
    raw_->original.c_cc[VMIN] = 1;
    raw_->original.c_cc[VTIME] = 0;
  }
  raw_->haveOriginal = true;
  g_restoreTermios = raw_->original;
  if (g_restoreFd < 0) {
    static bool registered = false;
    if (!registered) {
      registered = true;
      std::atexit(restoreTerminalAtExit);
      for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL}) std::signal(sig, handleFatalSignal);
    }
  }
  g_restoreFd = fd_;
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
#endif
}

void KeyInput::leaveRawMode() {
#ifndef _WIN32
  if (fd_ < 0 || !raw_->haveOriginal) return;
  tcsetattr(fd_, TCSANOW, &raw_->original);
#endif
}

void KeyInput::reenterRawMode() {
#ifndef _WIN32
  if (fd_ < 0 || !raw_->haveOriginal) return;
  tcsetattr(fd_, TCSANOW, &raw_->raw);
#endif
}

void KeyInput::close() {
  leaveRawMode();
#ifndef _WIN32
  if (g_restoreFd == fd_) g_restoreFd = -1;  // ふつうに終わった(atexit()で画面をもう一度戻さない)
  if (fd_ >= 0 && fd_ != 2) ::close(fd_);
#endif
  fd_ = -1;
}

void KeyInput::setPolling(bool polling) {
#ifdef _WIN32
  (void)polling;
#else
  if (fd_ < 0 || !raw_->haveOriginal) return;
  raw_->raw.c_cc[VMIN] = 0;
  raw_->raw.c_cc[VTIME] = polling ? 1 : 0;
  tcsetattr(fd_, TCSANOW, &raw_->raw);
#endif
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
  if (seqs[0].empty()) {
    lastEscapeBytes_.clear();
    return std::nullopt;
  }

  // 移植された癖: 全てのシーケンスがUp(seqs[0])と同じ長さを共有
  // していると仮定される――既に消費済みの先頭ESCの後、ちょうどその
  // 分だけ追加でバイトを読む。これはread_esc()と正確に一致している
  // (そちらも同じようにstrlen(ks[0])を使っている)。各候補自身の
  // 長さを読むのではなく、そのままここへ持ち越してあり、修正して
  // いない。
  int remaining = static_cast<int>(seqs[0].size()) - 1;
  std::string esc(1, static_cast<char>(0x1b));
  for (int i = 0; i < remaining; i++) {
    int c = readByte();
    if (!c) break;
    esc.push_back(static_cast<char>(c));
  }
  lastEscapeBytes_ = esc.substr(1);  // ESCの後のバイト、実際に捕捉された内容そのまま――lastEscapeBytes()参照
  if (esc.size() < 2) return std::nullopt;  // 裸のESC、何も続かなかった

  int matched = -1;
  for (int i = 0; i < 6; i++) {
    if (seqs[i].size() < 3) continue;
    if (esc.size() > 2 && seqs[i][0] == esc[0] && seqs[i][2] == esc[2]) {
      matched = i;
      break;
    }
  }
  // Shift+矢印(2026-10-03、EPUBの章送り): "\x1b[1;2A"〜"\x1b[1;2D"(xterm/Terminal.app/iTerm2の既定)。
  if (matched < 0 && esc == "\x1b[1") {
    std::string rest;
    for (int i = 0; i < 3; i++) {
      int c = readByte();
      if (!c) break;
      rest.push_back(static_cast<char>(c));
    }
    lastEscapeBytes_ += rest;
    if (rest.size() == 3 && rest[0] == ';' && rest[1] == '2') {
      switch (rest[2]) {
        case 'A': return SpecialKey::ShiftUp;
        case 'B': return SpecialKey::ShiftDown;
        case 'C': return SpecialKey::ShiftRight;
        case 'D': return SpecialKey::ShiftLeft;
        default: break;
      }
    }
    return std::nullopt;
  }
  // ^[+矢印を素早く押した時(とOptionをMetaにした端末のOption+矢印): "\x1b\x1b[A"等がひと続きに届く。
  // Shift+矢印と同じものとして返す(2026-10-04、EPUBの章送り、Application::jumpEpubChapterByArrow()参照)。
  // 以前はESC ESCとして^[^[(EXIT)になっていた。
  if (matched < 0 && (esc == "\x1b\x1b[" || esc == "\x1b\x1bO")) {
    int c = readByte();
    if (c) lastEscapeBytes_.push_back(static_cast<char>(c));
    switch (c) {
      case 'A': return SpecialKey::ShiftUp;
      case 'B': return SpecialKey::ShiftDown;
      case 'C': return SpecialKey::ShiftRight;
      case 'D': return SpecialKey::ShiftLeft;
      default: break;
    }
  }
  if (matched < 0) return std::nullopt;
  if (matched >= 4) readByte();  // PageUp/PageDown: 末尾のバイト(例えば'~')を捨てる
  return kSpecialOrder[matched];
}

const std::string &KeyInput::lastEscapeBytes() const { return lastEscapeBytes_; }

void KeyInput::installSignalHandlers() {
#ifndef _WIN32
  std::signal(SIGWINCH, handleWinch);
  std::signal(SIGTTOU, handleTtou);
  std::signal(SIGCONT, handleCont);
#endif
}

bool KeyInput::consumeResizeSignal() {
  if (!g_resizePending) return false;
  g_resizePending = 0;
#ifndef _WIN32
  int rows, columns;
  if (queryWindowSize(fd_, &rows, &columns)) display_.setSize(rows, columns);
#endif
  return true;
}

void KeyInput::setForegroundChildPid(int pid) { g_foregroundChildPid = pid; }

void KeyInput::setErrorPending(bool pending) { errorPending_ = pending; }

void KeyInput::setPidReporter(std::function<void(int)> reporter) {
  pidReporter_ = std::move(reporter);
}

void KeyInput::playRawMacro(const std::string &bytes) {
  // 2026-10-02修正、直接のユーザー報告("Kbt.epubで→を数回押すとEXITになる。escが拾われているかも"):
  // 以前はキューを丸ごと置き換えていた。app/main.cppの覗き見(peekMouseSgr()/peekArrow())は、
  // 読んだ分(例えば→の"[C")を戻した後、呼び出し側がさらに先頭のESCを戻す――置き換えだとESCだけが
  // 残って"[C"が消え、ESCが単独でApplicationへ届き、次のESCとでESC ESC(K-65、EXIT)になっていた。
  // まだ読まれていない分の前に足す。
  macroQueue_ = bytes + macroQueue_.substr(std::min(macroPos_, macroQueue_.size()));
  macroPos_ = 0;
}

bool KeyInput::isPlayingRawMacro() const { return macroPos_ < macroQueue_.size(); }

}  // namespace fm
