// usilic (C++ port) -- real terminal entry point.
//
// This is the first real run loop for the Application class this
// whole port has been building toward: opens a real terminal
// (TerminalDisplay::fromEnvironment()) and a real controlling-tty
// KeyInput, loads ~/.usilic/usilicrc and ~/.usilic/history if
// present, constructs Application, and feeds it bytes until
// Application::wantsExit() (set by the M_exit ^X^C confirmation
// flow) becomes true, saving history back out on the way out.
//
// Deliberately minimal, not a literal port of tcb.c's main()/
// init_menu()/menu():
//   - usilicrc/history loading IS wired (this is what closes the last
//     of the three follow-ups this file's own history left open, see
//     CLAUDE.md): Configuration::loadRcText() and
//     History::loadHistoryText() take already-read text rather than
//     doing file I/O themselves (by design, see their own class
//     comments) -- this file does that reading, following
//     set_tcbrc()/the S_hist path-building in legacy's str.c as
//     closely as practical: both files live under $HOME/.usilic/ (the
//     directory is created if missing), named usilicrc and history.
//     Two things legacy did that this doesn't: falling back to a
//     compiled-in system-wide LIB path when $HOME/.usilic/usilicrc
//     doesn't exist (this port has no installed-package LIB
//     convention yet to fall back to), and the `.old`-then-overwrite
//     backup dance for the RC file itself (only history gets that
//     treatment here, matching what History's own class comment
//     documents as its half of this "caller's job" split).
//   - No SIGWINCH/live-resize handling: KeyInput::consumeResizeSignal()
//     exists and updates Display's own geometry, but propagating a
//     resize to every ContentView's viewport (and Shell's pty via
//     onResize()) isn't wired here.
//   - ESC-leader bindings (SHELL's ^[s) ARE wired, via
//     KeyInput::lastEscapeBytes() (added specifically to close this
//     gap, see its own class comment): when readEscapeSequence()
//     doesn't match one of Display's 6 known terminal-capability
//     sequences, whatever byte(s) it actually read after ESC are fed
//     back through Application::feedByte() the same way a live ^[
//     keypress would arrive, letting Application's own ESC-leader
//     state machine (escPending_) recognize 's' for SHELL.
//   - SHELL's own pty IS drained here: this loop poll()s the keyboard
//     and, while ApplicationMode::Shell is the visible screen, the
//     running Shell's pty at the same time, so neither
//     KeyInput::readByte() nor Shell::onPtyReadable() ever needs to
//     block indefinitely. This is intentionally foreground-only: once
//     the user leaves Shell mode (Back/^G, matching Application's own
//     "the child keeps running in the background" design), this loop
//     stops draining its pty until the user switches back via ^[s
//     again. Real bash output in the meantime simply queues in the
//     pty's own kernel buffer (bounded, not unlimited) rather than
//     being read continuously -- a real terminal multiplexer would
//     drain background panes too; this port doesn't yet.
//   - This loop polls STDIN_FILENO, not KeyInput::fd(), for keyboard
//     readiness -- a deliberate workaround for a real, reproducible
//     macOS/Darwin quirk found while building this loop: poll() on an
//     fd obtained via open("/dev/tty", O_RDONLY) unconditionally
//     reports POLLNVAL on Darwin (verified directly: the exact same
//     open() + poll() sequence against the exact same controlling
//     terminal returns POLLNVAL when polling the /dev/tty-opened fd,
//     but reports correctly when polling fd 0 instead), even though
//     the fd itself is genuinely open and reads from it work fine --
//     confirmed NOT present on Linux, where both fds poll identically.
//     Since this is the real terminal entry point (unlike KeyInput's
//     own tests, which never call poll() against a real tty at all),
//     stdin and the controlling terminal are the same device here, so
//     polling stdin is a correct, portable stand-in. KeyInput::fd()
//     itself is unaffected and still reads correctly either way; only
//     poll()ing that exact fd is unreliable on Darwin.
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "application.hpp"
#include "configuration.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "terminal_display.hpp"

namespace {

std::string readFileIfExists(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Ported from sub.c's save_time(): a plain asctime()-style timestamp
// with the trailing newline asctime() always appends stripped off.
std::string currentTimestampText() {
  std::time_t t = std::time(nullptr);
  std::string text = std::asctime(std::localtime(&t));
  if (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

}  // namespace

int main(int argc, char **argv) {
  std::unique_ptr<fm::TerminalDisplay> display = fm::TerminalDisplay::fromEnvironment();
  display->setOutputEnabled(true);  // fromEnvironment() leaves this off by default -- see its own class comment

  fm::KeyInput keyInput(*display);
  keyInput.open("/dev/tty");
  keyInput.installSignalHandlers();

  // Ported from str.c's set_tcbrc()/S_hist path-building: both files
  // live under $HOME/.usilic/, created if it doesn't exist yet (same
  // directory legacy's own history file lived under after the
  // tcb->usilic rename, see CLAUDE.md).
  const char *homeEnv = std::getenv("HOME");
  std::string usilicDir = (homeEnv ? std::string(homeEnv) : std::string()) + "/.usilic/";
  ::mkdir(usilicDir.c_str(), S_IRWXU);  // ignored if it already exists; real failures surface later as I/O errors

  fm::History history;
  fm::Configuration configuration;

  // Ported from init_config()'s env_charset()/getenv(ENV_CHARSET)
  // check -- explicitly distinguishing "unset" from "set to an empty
  // string" is why this isn't just `getenv(...) ?: ""`, see
  // Configuration's own class comment on CLAUDE.md history item 13.
  const char *charsetEnv = std::getenv("USILICCHARSET");
  configuration.setEnvironmentCharset(charsetEnv ? std::optional<std::string>(charsetEnv) : std::nullopt);

  std::string rcText = readFileIfExists(usilicDir + "usilicrc");
  if (!rcText.empty()) configuration.loadRcText(rcText, /*fileManagerMode=*/true);

  std::string historyText = readFileIfExists(usilicDir + "history");
  if (!historyText.empty()) {
    history.loadHistoryText(historyText, configuration.charsetExplicitlySet());
    if (!history.isComplete()) {
      // Ported from load_list()'s own `.old` fallback: the primary
      // file looked truncated/corrupt (missing EOF sentinel).
      std::string oldText = readFileIfExists(usilicDir + "history.old");
      if (!oldText.empty()) history.loadHistoryText(oldText, configuration.charsetExplicitlySet());
    }
  }

  // History's own saved charset already defers to the environment
  // when charsetExplicitlySet() is true (loadHistoryText() leaves
  // savedCharset() nullopt in that case) -- see History's own class
  // comment. Configuration::defaultCharset() itself already folds in
  // the environment/rc value, or the compiled-in Utf8 default.
  fm::Charset::Encoding encoding = history.savedCharset().value_or(configuration.defaultCharset());

  fm::ShellOptions shellOptions;
  if (const char *shellEnv = std::getenv("SHELL"); shellEnv && *shellEnv) shellOptions.argv = {shellEnv};

  fm::Application app(*display, keyInput, history, configuration, encoding, display->columns(), shellOptions);

  display->open();

  std::string startDirectory = (argc > 1) ? argv[1] : ".";
  if (!app.start(startDirectory)) {
    display->close();
    keyInput.close();
    std::cerr << "usilic: cannot list " << startDirectory << "\n";
    return 1;
  }

  while (!app.wantsExit()) {
    bool watchShellPty = app.currentMode() == fm::ApplicationMode::Shell && app.shell().alive();

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;  // see file comment on why this, not keyInput.fd(), is polled
    fds[0].events = POLLIN;
    fds[1].fd = watchShellPty ? app.shell().ptyFd() : -1;  // negative fd: poll() ignores this slot
    fds[1].events = POLLIN;
    int ready = poll(fds, 2, 100);  // 100ms so SIGWINCH still gets noticed promptly even when both fds are idle

    if (ready <= 0) {
      keyInput.consumeResizeSignal();  // updates Display's geometry; not yet propagated to ContentViews, see above
      continue;
    }
    if (watchShellPty && (fds[1].revents & POLLIN)) app.shell().onPtyReadable();
    if (!(fds[0].revents & POLLIN)) continue;

    int byte = keyInput.readByte();
    if (!byte) {
      keyInput.consumeResizeSignal();
      continue;
    }
    if (byte == 0x1b) {
      auto special = keyInput.readEscapeSequence();
      if (special) {
        app.feedByte(0, special);
        continue;
      }
      // Not one of Display's known terminal sequences -- hand ESC,
      // then whatever byte(s) actually followed it, to Application's
      // own ESC-leader state machine (e.g. ^[s for SHELL). See
      // KeyInput::lastEscapeBytes()'s own comment for the one
      // remaining inherited quirk (a fast-typed subsequent keystroke
      // can occasionally ride along as a second byte here).
      app.feedByte(0x1b, std::nullopt);
      for (unsigned char c : keyInput.lastEscapeBytes()) app.feedByte(c, std::nullopt);
      continue;
    }
    app.feedByte(byte, std::nullopt);
  }

  display->close();
  keyInput.close();

  // Ported from save_history()'s own `.old`-then-overwrite dance:
  // back up the previous file before writing the new one.
  history.setSavedCharset(encoding);  // so a future session with no explicit override resumes in this one's charset
  std::string historyPath = usilicDir + "history";
  std::string historyOldPath = usilicDir + "history.old";
  if (!readFileIfExists(historyPath).empty()) std::rename(historyPath.c_str(), historyOldPath.c_str());
  std::ofstream out(historyPath, std::ios::binary);
  out << history.serialize("usilic", currentTimestampText());

  return 0;
}
