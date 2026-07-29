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
//   - No ESC-leader bindings (SHELL's ^[s): Application's own class
//     comment already documents why -- KeyInput::readEscapeSequence()
//     consumes bytes trying to match one of Display's 6 known
//     terminal-capability sequences and has no defined way to hand
//     back an unmatched trailing byte, so an ESC followed by anything
//     that isn't an arrow/page key is simply dropped below. Every
//     other mode (GREP/CUT/MAN/PROGRAM/LOCATE/CP/MV/RM/exit) uses a
//     ^J/^X/^Space-style leader instead and works over a real
//     terminal today; SHELL does not yet.
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

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
    int byte = keyInput.readByte();
    if (!byte) {
      keyInput.consumeResizeSignal();  // updates Display's geometry; not yet propagated to ContentViews, see above
      continue;
    }
    if (byte == 0x1b) {
      auto special = keyInput.readEscapeSequence();
      if (special) app.feedByte(0, special);
      // else: an app-level ESC-leader sequence (e.g. ^[s for SHELL) -- not resolvable here, see file comment.
      continue;
    }
    app.feedByte(byte, std::nullopt);
  }

  display->close();
  keyInput.close();
  return 0;
}
