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
//   - SIGWINCH/live-resize handling IS wired: KeyInput::
//     consumeResizeSignal() updates Display's own geometry and
//     returns true when it did, and this loop then calls
//     Application::handleResize() (see its own comment for exactly
//     which modes get a content reflow vs. just a viewport resize,
//     and why Shell's pty is always resized regardless of which mode
//     is currently visible). Only meaningful in the terminal phase --
//     see the GUI-mode section below for why.
//   - HELP (^J^H) IS wired: USILICHELP names the ASCII/English help
//     file's path (no installed-package LIB-path convention exists
//     yet, same gap usilicrc/history's own LIB fallback has -- see
//     above); unset, or an unreadable file, just leaves HELP
//     unavailable rather than failing startup. Application::
//     loadHelpText() takes already-read text, same "caller does the
//     file I/O" rule as usilicrc/history.
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
//   - GUI mode (^Og, docs/ideas/IDEAS.md) IS wired, as a genuinely
//     separate SECOND run loop (runGuiPhase()) rather than folded into
//     the terminal loop above -- see Application's own class comment
//     for why ^Og/handleMouseClick() are deliberately backend-agnostic
//     there. This file is where the two concrete backends actually
//     meet:
//       - `display` (the Application-facing Display&) is a
//         DelegatingDisplay wrapping `terminalDisplay` initially;
//         Application/KeyInput/every *Mode/ContentView/LineEditor are
//         all constructed ONCE against `display` and never rebuilt --
//         see DelegatingDisplay's own class comment for why toggling
//         GUI mode is just one setBackend() call, not a teardown/
//         rebuild of the whole Application object.
//       - Entering GUI mode: resolves a monospace font path
//         (USILICFONT, or a short list of common system font paths --
//         same "env var override, then a best-effort guess, then
//         simply unavailable" shape USILICHELP already uses) and
//         constructs a GraphicalDisplay sized to match the terminal's
//         CURRENT rows/columns (so switching is visually seamless --
//         the content doesn't reflow just from toggling). If either
//         step fails (no font found, or GraphicalDisplay::create()
//         itself fails -- e.g. no real display server available, the
//         common case when running headless/over SSH/in CI), the
//         toggle is simply refused and the terminal loop continues
//         uninterrupted -- never a hard failure.
//       - The GUI loop polls GraphicalDisplay::pumpEvents() (which
//         internally does SDL's own event pump -- kept entirely
//         inside that class, this file never touches SDL directly)
//         instead of stdin, translating queued mouse clicks into
//         Application::handleMouseClick() and queued key events into
//         Application::feedByte() calls (GraphicalDisplay::KeyEvent's
//         own `special` field reuses Display::Key -- translated to
//         KeyInput::SpecialKey here, the one place both enums are in
//         scope, see GraphicalDisplay's own class comment on why it
//         doesn't depend on KeyInput itself to do this translation).
//         Shell's pty is still drained the same way as the terminal
//         loop, via a second, non-blocking poll() alongside the SDL
//         event pump, so a live Shell session keeps working
//         seamlessly across the switch too.
//       - Leaving GUI mode (^Og again, or the window's own close
//         button -- pumpEvents() returning false): the DelegatingDisplay
//         is pointed back at terminalDisplay and the GraphicalDisplay
//         instance is destroyed outright (closing the real window) --
//         re-entering GUI mode later constructs a fresh one rather
//         than trying to hide/reshow an existing window.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <cmath>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "application.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "delegating_display.hpp"
#include "drag_scroll_speed.hpp"
#include "graphical_display.hpp"
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

// USILICFONT, or a short best-effort list of common monospace system
// font paths (Mac, then common Linux distros) -- see file comment on
// why this mirrors USILICHELP's own resolution shape. Empty if none
// found, meaning GUI mode simply stays unavailable.
std::string resolveGuiFontPath() {
  if (const char *env = std::getenv("USILICFONT")) return env;
  static const char *const candidates[] = {
      "/System/Library/Fonts/Menlo.ttc",
      "/System/Library/Fonts/Monaco.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
  };
  for (const char *path : candidates) {
    std::ifstream probe(path, std::ios::binary);
    if (probe.good()) return path;
  }
  return {};
}

std::optional<fm::KeyInput::SpecialKey> translateDisplayKey(fm::Display::Key key) {
  switch (key) {
    case fm::Display::Key::Up:
      return fm::KeyInput::SpecialKey::Up;
    case fm::Display::Key::Down:
      return fm::KeyInput::SpecialKey::Down;
    case fm::Display::Key::Left:
      return fm::KeyInput::SpecialKey::Left;
    case fm::Display::Key::Right:
      return fm::KeyInput::SpecialKey::Right;
    case fm::Display::Key::PageUp:
      return fm::KeyInput::SpecialKey::PageUp;
    case fm::Display::Key::PageDown:
      return fm::KeyInput::SpecialKey::PageDown;
  }
  return std::nullopt;
}

// Runs until wantsExit() or a GUI-mode toggle is requested. Returns
// true if it should be followed by a GUI phase (false means the whole
// program is exiting).
bool runTerminalPhase(fm::Application &app, fm::KeyInput &keyInput) {
  while (!app.wantsExit()) {
    if (app.consumeGuiModeToggleRequest()) return true;
    bool watchShellPty = app.currentMode() == fm::ApplicationMode::Shell && app.shell().alive();

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;  // see file comment on why this, not keyInput.fd(), is polled
    fds[0].events = POLLIN;
    fds[1].fd = watchShellPty ? app.shell().ptyFd() : -1;  // negative fd: poll() ignores this slot
    fds[1].events = POLLIN;
    int ready = poll(fds, 2, 100);  // 100ms so SIGWINCH/GUI-toggle still get noticed promptly when idle

    if (ready <= 0) {
      if (keyInput.consumeResizeSignal()) app.handleResize();  // updates Display's geometry, then reflows content
      continue;
    }
    if (watchShellPty && (fds[1].revents & POLLIN)) app.shell().onPtyReadable();
    if (!(fds[0].revents & POLLIN)) continue;

    int byte = keyInput.readByte();
    if (!byte) {
      if (keyInput.consumeResizeSignal()) app.handleResize();
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
      // own ESC-leader state machine (e.g. ^[s for SHELL, ^Og for
      // GUI mode is a PLAIN ^O, not ESC-prefixed, so it never reaches
      // here at all). See KeyInput::lastEscapeBytes()'s own comment
      // for the one remaining inherited quirk (a fast-typed subsequent
      // keystroke can occasionally ride along as a second byte here).
      app.feedByte(0x1b, std::nullopt);
      for (unsigned char c : keyInput.lastEscapeBytes()) app.feedByte(c, std::nullopt);
      continue;
    }
    app.feedByte(byte, std::nullopt);
  }
  return false;
}

// Executes one usleep()/busy-loop-spin delay from `step` (see
// drag_scroll_speed.hpp) -- the actual "wait" half of one dot of
// scroll. Kept as its own function so the busy-loop's spin can't be
// optimized away and so its cost is visible separately from the
// pixel-offset bookkeeping around it.
void performScrollStepDelay(const fm::ScrollStep &step) {
  if (step.busyLoop) {
    volatile long sink = 0;
    for (long i = 0; i < step.busyIterations; i++) sink += i;
  } else if (step.microseconds > 0) {
    usleep(static_cast<useconds_t>(step.microseconds));
  }
}

// Commits whole-row crossings of pixelOffset into view.setTopIndex(),
// keeping pixelOffset within (-cellHeight, cellHeight) between
// renders -- the same odometer-style carry the original friction-based
// version used, just factored out so both the active-drag path and
// the coasting path (which can cross several rows in one call) share
// it. Returns false if a boundary (top or bottom of the list) was
// hit, so a coasting caller knows to stop rather than pin uselessly
// against the edge.
bool commitRowCrossings(fm::ContentView &view, int cellHeight, int &pixelOffset) {
  while (pixelOffset >= cellHeight) {
    int before = view.topIndex();
    view.setTopIndex(before - 1);
    if (view.topIndex() == before) {
      pixelOffset = cellHeight - 1;
      return false;
    }
    pixelOffset -= cellHeight;
  }
  while (pixelOffset <= -cellHeight) {
    int before = view.topIndex();
    view.setTopIndex(before + 1);
    if (view.topIndex() == before) {
      pixelOffset = -(cellHeight - 1);
      return false;
    }
    pixelOffset += cellHeight;
  }
  return true;
}

// Runs until wantsExit(), the GUI window is closed, or ^Og toggles
// back to terminal mode. See file comment for the overall shape.
//
// Also drives the drag-scroll inertial physics (docs/ideas/IDEAS.md's
// cat's-paw scroll request; see GraphicalDisplay's own class comment
// on renderScrolled()/consumeDragEvent()): while the left mouse button
// is held and dragged, or while "coasting" after release, this renders
// via GraphicalDisplay::renderScrolled() (a smooth, sub-row pixel-
// offset path -- the real per-dot mechanism, not a row-jump
// approximation, per CLAUDE.md's 2026-07-30 note) instead of the
// normal ContentView::redraw(); once fully settled on a row boundary
// with nothing coasting, it hands rendering back to the normal
// Display-interface path (a single redraw() call).
//
// Physics, corrected 2026-07-30 from an earlier friction-based
// attempt the user rejected on two counts:
//   - No deceleration: once released with enough speed to coast at
//     all, scrolling continues at a CONSTANT rate (one of
//     fm::kScrollSpeedLevels) until something stops it -- reaching a
//     list boundary, or the button going down again to grab it --
//     rather than decaying toward zero. "Inertia" here means
//     "maintains its current state," not "gradually loses energy to
//     friction" (friction was never part of the request).
//   - Release speed, not in-drag velocity: the level is chosen once,
//     at ButtonUp, from the summed drag distance in the ~150ms just
//     before release (see recentMotion below) -- not a continuously
//     smoothed during-drag velocity. This matches the author's own
//     earlier "cb" app: the amount of cursor movement at the moment
//     you let go of the button sets the scroll speed.
//   - Speed is realized as a per-dot delay (fm::kScrollSpeedLevels):
//     usleep() for the two slowest levels, an actual busy-loop spin
//     for everything faster, exactly as cb did and for the same
//     reason (OS sleep granularity can't hit the faster levels'
//     sub-millisecond per-dot timing). At the fastest levels this
//     spins through many dots (sometimes several full rows) within a
//     single ~16ms outer-loop slice below -- text becoming too fast
//     to read at top speed is an honest side effect of that, not
//     something faked by jumping rows or scaling opacity.
//
// Known, deliberate gaps (still true after this rework -- next steps,
// not oversights):
//   - renderScrolled() draws plain glyphs only -- no cursor/standout
//     highlight during an active drag or while coasting
//     (ContentView::renderLine()'s own reverse-video marking isn't
//     replicated here at all).
//   - Click-vs-drag isn't disambiguated: handleMouseClick() still
//     fires on every button-down regardless of whether a drag
//     follows, so a "click that turns into a drag" also selects
//     whatever row was under the cursor at press time.
//   - The cursor shape is SDL's built-in hand pointer
//     (SDL_SYSTEM_CURSOR_HAND), not a custom cat's-paw bitmap.
//   - No mode-line/status-bar row-count indicator yet (legacy's
//     disp_window() territory, never ported -- see CLAUDE.md's
//     2026-07-30 feasibility note); nothing here updates one.
//   - There is no keyboard-driven trigger for coasting yet (^Og itself
//     only toggles GUI mode) -- IDEAS.md's original spec also wanted a
//     key+arrow combination to start inertial scroll without a mouse;
//     not wired here.
//   - Horizontal drag, right-click-during-drag, and File/Man mode
//     (vs. just whichever mode is active) aren't specifically
//     exercised, though the mechanism itself isn't Directory-specific.
void runGuiPhase(fm::Application &app, fm::GraphicalDisplay &gui) {
  bool dragging = false;
  int pixelOffset = 0;      // sub-row offset; -cellHeight < pixelOffset < cellHeight
  int coastLevel = 0;       // 0 == not coasting; otherwise an index into fm::kScrollSpeedLevels
  int coastDirection = 0;   // +1 or -1 while coastLevel > 0, matching renderScrolled()'s pixelOffsetY sign
  // Rolling window of recent Motion samples, trimmed to the last
  // ~150ms -- summed at ButtonUp to get the "cursor movement at the
  // moment you let go" release distance cb's own speed control used.
  std::deque<std::pair<std::chrono::steady_clock::time_point, int>> recentMotion;
  constexpr auto kReleaseWindow = std::chrono::milliseconds(150);

  while (!app.wantsExit()) {
    if (!gui.pumpEvents()) return;  // the window's own close button was clicked

    while (auto click = gui.consumeMouseClick()) {
      if (!click->rightButton) app.handleMouseClick(click->row, click->column);
      // right-click: captured but not acted on yet, see class comments
    }
    while (auto key = gui.consumeKeyEvent()) {
      if (key->special) {
        app.feedByte(0, translateDisplayKey(*key->special));
      } else {
        app.feedByte(key->byte, std::nullopt);
      }
      if (app.consumeGuiModeToggleRequest()) return;
    }

    fm::ContentView &view = app.activeContentView();
    int cellHeight = std::max(gui.cellHeightPixels(), 1);

    while (auto drag = gui.consumeDragEvent()) {
      switch (drag->type) {
        case fm::GraphicalDisplay::DragEventType::ButtonDown:
          dragging = true;
          coastLevel = 0;  // grabbing mid-coast stops it right where it is
          coastDirection = 0;
          recentMotion.clear();
          gui.setHandCursor(true);
          break;
        case fm::GraphicalDisplay::DragEventType::ButtonUp: {
          dragging = false;
          gui.setHandCursor(false);
          auto cutoff = std::chrono::steady_clock::now() - kReleaseWindow;
          int releaseDistance = 0;
          for (auto &sample : recentMotion)
            if (sample.first >= cutoff) releaseDistance += sample.second;
          coastLevel = fm::levelForReleaseDistance(releaseDistance, cellHeight);
          coastDirection = coastLevel > 0 ? (releaseDistance > 0 ? 1 : -1) : 0;
          break;
        }
        case fm::GraphicalDisplay::DragEventType::Motion:
          pixelOffset += drag->deltaY;
          recentMotion.push_back({std::chrono::steady_clock::now(), drag->deltaY});
          while (!recentMotion.empty() && recentMotion.front().first < std::chrono::steady_clock::now() - kReleaseWindow)
            recentMotion.pop_front();
          commitRowCrossings(view, cellHeight, pixelOffset);
          break;
      }
    }

    if (coastLevel > 0 && !dragging) {
      const fm::ScrollStep &step = fm::kScrollSpeedLevels[coastLevel];
      auto frameDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
      do {
        performScrollStepDelay(step);
        pixelOffset += coastDirection;
        if (!commitRowCrossings(view, cellHeight, pixelOffset)) {
          coastLevel = 0;  // hit a boundary -- stop rather than pin against the edge
          coastDirection = 0;
          break;
        }
      } while (std::chrono::steady_clock::now() < frameDeadline);
    }

    if (dragging || coastLevel > 0 || pixelOffset != 0) {
      std::vector<std::string> lines;
      lines.reserve(view.lines().size());
      for (const auto &line : view.lines()) lines.push_back(line.text);
      gui.renderScrolled(lines, view.topIndex(), pixelOffset);
    } else {
      view.redraw();  // settled on a row boundary -- back to the normal Display-interface rendering path
    }

    bool watchShellPty = app.currentMode() == fm::ApplicationMode::Shell && app.shell().alive();
    if (watchShellPty) {
      struct pollfd fd{app.shell().ptyFd(), POLLIN, 0};
      if (poll(&fd, 1, 0) > 0 && (fd.revents & POLLIN)) app.shell().onPtyReadable();
    }

    usleep(16000);  // ~60Hz pacing -- see file comment on why this loop can't share the terminal loop's poll()
  }
}

}  // namespace

int main(int argc, char **argv) {
  std::unique_ptr<fm::TerminalDisplay> terminalDisplay = fm::TerminalDisplay::fromEnvironment();
  terminalDisplay->setOutputEnabled(true);  // fromEnvironment() leaves this off by default -- see its own comment
  fm::DelegatingDisplay display(*terminalDisplay);  // see file comment's own GUI-mode section

  fm::KeyInput keyInput(*terminalDisplay);
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

  fm::Application app(display, keyInput, history, configuration, encoding, display.columns(), shellOptions);

  // Ported from str.c's set_help(): English (HELP) vs a genuine
  // Japanese translation (HELP.jp), chosen by charset the same way
  // legacy does. No installed-package LIB-path convention exists yet
  // for this port (same gap usilicrc's own LIB fallback has, see
  // above) -- USILICHELP names the ASCII/English file directly; if
  // unset, or if the file can't be read, HELP (^J^H) is simply
  // unavailable (Command::StartHelp fails cleanly) rather than this
  // being a hard startup error.
  if (const char *helpPathEnv = std::getenv("USILICHELP")) {
    std::string helpPath = helpPathEnv;
    if (encoding != fm::Charset::Encoding::Ascii) {
      std::string jpText = readFileIfExists(helpPath + ".jp");
      if (!jpText.empty()) {
        app.loadHelpText(jpText);
      } else {
        app.loadHelpText(readFileIfExists(helpPath));
      }
    } else {
      app.loadHelpText(readFileIfExists(helpPath));
    }
  }

  display.open();

  std::string startDirectory = (argc > 1) ? argv[1] : ".";
  if (!app.start(startDirectory)) {
    display.close();
    keyInput.close();
    std::cerr << "usilic: cannot list " << startDirectory << "\n";
    return 1;
  }

  while (!app.wantsExit()) {
    if (!runTerminalPhase(app, keyInput)) break;
    if (app.wantsExit()) break;

    std::string fontPath = resolveGuiFontPath();
    std::unique_ptr<fm::GraphicalDisplay> gui =
        fontPath.empty() ? nullptr : fm::GraphicalDisplay::create(display.rows(), display.columns(), fontPath);
    if (!gui) continue;  // no usable font, or window/renderer creation failed -- stay in terminal mode

    gui->open();
    display.setBackend(*gui);
    app.handleResize();  // re-layout/redraw against the (same-sized, but now GUI-rendered) geometry

    runGuiPhase(app, *gui);

    display.setBackend(*terminalDisplay);
    gui.reset();  // closes the real window
    if (!app.wantsExit()) app.handleResize();  // redraw the terminal, which SDL's window never touched
  }

  display.close();
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
