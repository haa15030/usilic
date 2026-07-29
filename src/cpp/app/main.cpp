// usilic (C++ port) -- real terminal entry point.
//
// This is the first real run loop for the Application class this
// whole port has been building toward: opens a real terminal
// (TerminalDisplay::fromEnvironment()) and a real controlling-tty
// KeyInput, constructs Application, and feeds it bytes until
// Application::wantsExit() (set by the M_exit ^X^C confirmation
// flow) becomes true.
//
// Deliberately minimal, not a literal port of tcb.c's main()/
// init_menu()/menu():
//   - No usilicrc loading, no ~/.tcb/history persistence: both
//     Configuration::loadRcText() and History::loadHistoryText()
//     take already-read text rather than doing file I/O themselves
//     (by design, see their own class comments), and this first cut
//     doesn't wire that reading up yet -- every session starts with
//     compiled-in defaults and empty history. Flagged as follow-up
//     work, not silently glossed over.
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
#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>

#include "application.hpp"
#include "configuration.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "terminal_display.hpp"

int main(int argc, char **argv) {
  std::unique_ptr<fm::TerminalDisplay> display = fm::TerminalDisplay::fromEnvironment();
  display->setOutputEnabled(true);  // fromEnvironment() leaves this off by default -- see its own class comment

  fm::KeyInput keyInput(*display);
  keyInput.open("/dev/tty");
  keyInput.installSignalHandlers();

  fm::History history;
  fm::Configuration configuration;

  fm::ShellOptions shellOptions;
  if (const char *shellEnv = std::getenv("SHELL"); shellEnv && *shellEnv) shellOptions.argv = {shellEnv};

  fm::Application app(*display, keyInput, history, configuration, fm::Charset::Encoding::Utf8, display->columns(),
                       shellOptions);

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
  return 0;
}
