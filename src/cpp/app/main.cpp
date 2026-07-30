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
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
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
#include "child_process.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "delegating_display.hpp"
#include "drag_scroll_speed.hpp"
#include "graphical_display.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "mac_font_resolver.hpp"
#include "mouse_sgr.hpp"
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

struct TerminalAppearance {
  fm::GraphicalDisplay::Color background;
  fm::GraphicalDisplay::Color foreground;
  int fontPointSize = 16;  // matches GraphicalDisplay::create()'s own previous hardcoded default
  // The calling terminal's own font NAME (Terminal.app's AppleScript
  // "font name" property, e.g. "MS-Gothic"), empty if unavailable --
  // see resolveGuiFontPath()'s own comment on how this gets turned
  // into an actual loadable file path.
  std::string fontName;
};

// USILICFONT; then (macOS, when `appearance` carries a real font
// NAME from the calling terminal) that name resolved to a real file
// via CoreText (see mac_font_resolver.hpp); then a short best-effort
// list of common monospace system font paths (Mac, then common Linux
// distros) -- see file comment on why this mirrors USILICHELP's own
// resolution shape. Empty if none found, meaning GUI mode simply
// stays unavailable.
//
// The CoreText step was added 2026-07-31 at a real report that the
// GUI window wasn't honoring the calling terminal's configured font
// (e.g. "ＭＳゴシック14", needed for full Japanese glyph coverage
// without falling back to the separate CJK-fallback-font mechanism at
// all) -- until then this function only ever matched the terminal's
// font SIZE (see TerminalAppearance's own history), never its NAME,
// because resolving a name to a file needs a real font-lookup API.
std::string resolveGuiFontPath(std::optional<TerminalAppearance> appearance = std::nullopt) {
  if (const char *env = std::getenv("USILICFONT")) return env;
  if (appearance && !appearance->fontName.empty()) {
    std::string resolved = fm::resolveMacFontPath(appearance->fontName, appearance->fontPointSize);
    if (!resolved.empty()) return resolved;
  }
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

// See loadScrollDebugLevelFromEnvironment()'s own comment.
std::optional<int> gScrollDebugLevel;

// Parses USILICSCROLLDEBUG once at startup: if set to an integer, ALL
// coast-speed selection (mouse-release distance, Shift+arrow start/
// accelerate -- both the GUI's real dot-level path and the real-
// terminal row-level fallback) uses this level directly instead of
// its normal release-distance/repeat-count-based computation. Added
// after a real-machine report that fm::kScrollSpeedLevels' speed
// curve "changes abruptly a few levels up from the minimum" (see its
// own comment on why the busy-loop steps are hand-picked, not
// measured against real hardware) -- this lets each level be tried
// directly, one at a time, without needing an actual mouse flick or
// enough repeated keypresses to reliably land on the level under
// test. Clamped to the valid range; unset or unparseable simply means
// "no override," the normal behavior.
void loadScrollDebugLevelFromEnvironment() {
  const char *env = std::getenv("USILICSCROLLDEBUG");
  if (!env || !*env) return;
  int level = std::atoi(env);
  int maxLevel = static_cast<int>(fm::kScrollSpeedLevels.size()) - 1;
  gScrollDebugLevel = std::clamp(level, 1, maxLevel);
}

int resolveCoastLevel(int computedLevel) { return gScrollDebugLevel.value_or(computedLevel); }

// Queries the calling terminal's own background/text colors, font
// size, AND font name via AppleScript, for the "実はすでにGUI" launch
// (docs/ideas/IDEAS.md, see CLAUDE.md's 2026-07-30 notes): a
// GraphicalDisplay window that matches the REAL terminal it's
// replacing is what makes starting in GUI mode from process launch
// unnoticeable. Font size matters as much as color here:
// GraphicalDisplay::create() used to hardcode 16pt regardless of the
// real terminal's own font size, which made the GUI window visibly
// LARGER (bigger cells at the same row/column count) than the
// terminal it was supposed to look identical to -- found from a real
// screenshot comparison. Font NAME (added 2026-07-31, see
// resolveGuiFontPath()'s own comment on why this was deliberately
// left out until now) is resolved to an actual file by the caller via
// mac_font_resolver.hpp -- this function only ever returns the raw
// name AppleScript reports (e.g. "MS-Gothic"), it doesn't touch the
// filesystem itself.
//
// Gated on TERM_PROGRAM=Apple_Terminal BEFORE spawning osascript at
// all -- checked earlier in this whole project (font/color
// feasibility research) to be the only terminal this query is known
// to work for, and osascript is slow enough that skipping it entirely
// elsewhere matters for startup latency (every other terminal, and
// every non-macOS platform, just falls through to GraphicalDisplay's
// own hardcoded defaults -- see its own class comment on
// setColors()). Returns nullopt on any failure (wrong TERM_PROGRAM,
// osascript not found/fails, or output that doesn't parse) -- never a
// hard requirement for GUI mode to work.
std::optional<TerminalAppearance> queryTerminalAppColors() {
  const char *termProgram = std::getenv("TERM_PROGRAM");
  if (!termProgram || std::string(termProgram) != "Apple_Terminal") return std::nullopt;

  fm::ChildProcess::Result result = fm::ChildProcess::run(
      {"osascript", "-e",
       "tell application \"Terminal\" to get {background color, normal text color, font size, font name} of front "
       "window"});
  if (!result.ok) return std::nullopt;

  // osascript prints an AppleScript list as "bgR, bgG, bgB, fgR, fgG,
  // fgB, size, name" -- the two RGB triples 0-65535 (AppleScript's
  // own RGB color range, not 0-255), the font size a plain point
  // size, then the font name as a bare (unquoted) string, which is
  // why it's placed LAST and parsed by taking everything past where
  // the 7 numeric fields ended (a %n-recorded byte offset) rather than
  // with another %-conversion -- sscanf has no "the rest of the
  // string" specifier, and the name can't be quoted-and-scanned since
  // AppleScript's own list-to-text coercion never quotes it.
  int bgR, bgG, bgB, fgR, fgG, fgB, fontSize;
  int consumed = 0;
  if (std::sscanf(result.output.c_str(), "%d, %d, %d, %d, %d, %d, %d, %n", &bgR, &bgG, &bgB, &fgR, &fgG, &fgB,
                   &fontSize, &consumed) != 7)
    return std::nullopt;

  auto to255 = [](int channel) -> unsigned char {
    return static_cast<unsigned char>(std::clamp(channel / 257, 0, 255));  // 65535 / 255 == 257
  };
  TerminalAppearance appearance;
  appearance.background = {to255(bgR), to255(bgG), to255(bgB)};
  appearance.foreground = {to255(fgR), to255(fgG), to255(fgB)};
  if (fontSize > 0) appearance.fontPointSize = fontSize;
  if (consumed > 0 && consumed <= static_cast<int>(result.output.size())) {
    std::string fontName = result.output.substr(consumed);
    while (!fontName.empty() && std::isspace(static_cast<unsigned char>(fontName.back()))) fontName.pop_back();
    appearance.fontName = fontName;
  }
  return appearance;
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

// -- 2026-07-30 real-terminal drag-scroll trigger (see CLAUDE.md) --
//
// Both peek functions below share the same shape: read raw bytes via
// keyInput.readByte() (the same source readEscapeSequence() itself
// reads from) looking for a pattern Display's own 6-key vocabulary
// (readEscapeSequence()) has no way to represent, and if the bytes
// DON'T end up matching, queue them back via playRawMacro() so the
// normal ESC path sees exactly what it would have seen had this peek
// never run. KeyInput has no putback of its own, but its macro queue
// (checked before the real tty on every readByte(), see its own class
// comment) doubles as one perfectly well here -- this is why these
// live here instead of on KeyInput itself: KeyInput's job is "raw
// bytes in, decide what they are"; this is the caller doing its own
// additional decoding on top, the same split KeyInput's class comment
// already draws for ESC-leader bindings via lastEscapeBytes().

// Executes one usleep()/busy-loop-spin delay from `step` (see
// drag_scroll_speed.hpp) -- the actual "wait" half of one dot/row of
// scroll. Kept as its own function so the busy-loop's spin can't be
// optimized away and so its cost is visible separately from the
// bookkeeping around it. Shared by runTerminalCoast() (one step per
// ROW) and runGuiPhase() (one step per PIXEL) below.
void performScrollStepDelay(const fm::ScrollStep &step) {
  if (step.busyLoop) {
    volatile long sink = 0;
    for (long i = 0; i < step.busyIterations; i++) sink += i;
  } else if (step.microseconds > 0) {
    usleep(static_cast<useconds_t>(step.microseconds));
  }
}

enum class ShiftArrow { None, Up, Down };

// Call immediately after readByte() returned ESC. Recognizes the
// xterm CSI-with-modifier form of Shift+Up/Down ("\x1b[1;2A" /
// "\x1b[1;2B") -- Up/Down's own *unmodified* sequences are shorter and
// diverge from this one already at the 3rd byte ('1' here vs. a final
// letter there), so a mismatch here can only mean "not a Shift-arrow,"
// never "this was actually a plain arrow key that got misread."
ShiftArrow peekShiftArrow(fm::KeyInput &keyInput) {
  std::string consumed;
  auto next = [&]() -> int {
    int b = keyInput.readByte();
    if (b != 0) consumed.push_back(static_cast<char>(b));
    return b;
  };
  static const char kExpected[] = "[1;2";
  for (const char *p = kExpected; *p; p++) {
    if (next() != static_cast<unsigned char>(*p)) {
      if (!consumed.empty()) keyInput.playRawMacro(consumed);
      return ShiftArrow::None;
    }
  }
  int final = next();
  if (final == 'A') return ShiftArrow::Up;
  if (final == 'B') return ShiftArrow::Down;
  if (!consumed.empty()) keyInput.playRawMacro(consumed);
  return ShiftArrow::None;
}

// Call immediately after readByte() returned ESC and peekShiftArrow()
// already returned None. Recognizes an SGR extended mouse report
// ("\x1b[<Cb;Cx;Cy(M|m)", only sent while runTerminalCoast() has
// enabled mouse tracking -- see setTerminalMouseTracking()) and hands
// the decoded parameter string to fm::parseMouseSgr(). A malformed or
// non-mouse sequence is queued back exactly like peekShiftArrow()
// above.
std::optional<fm::MouseSgrEvent> peekMouseSgr(fm::KeyInput &keyInput) {
  std::string consumed;
  auto next = [&]() -> int {
    int b = keyInput.readByte();
    if (b != 0) consumed.push_back(static_cast<char>(b));
    return b;
  };
  if (next() != '[' || next() != '<') {
    if (!consumed.empty()) keyInput.playRawMacro(consumed);
    return std::nullopt;
  }
  std::string params;
  for (;;) {
    int b = next();
    if (b == 'M' || b == 'm') {
      params.push_back(static_cast<char>(b));
      break;
    }
    if (b == 0 || !((b >= '0' && b <= '9') || b == ';')) {
      keyInput.playRawMacro(consumed);
      return std::nullopt;
    }
    params.push_back(static_cast<char>(b));
  }
  auto event = fm::parseMouseSgr(params);
  if (!event) keyInput.playRawMacro(consumed);  // well-formed-looking but rejected -- don't swallow it
  return event;
}

void setTerminalMouseTracking(fm::Display &display, bool enabled) {
  // \x1b[?1003h/l: report ALL motion (not just while a button is
  // held), so the coast loop can highlight whatever row the pointer
  // is over even without a button down. \x1b[?1006h/l: SGR extended
  // coordinates -- no 223-row/column ceiling, and unambiguous to parse
  // (see mouse_sgr.hpp).
  display.write(enabled ? "\x1b[?1003h\x1b[?1006h" : "\x1b[?1003l\x1b[?1006l");
  // write() alone doesn't flush (TerminalDisplay::write() is a plain
  // fputs(), see its own comment) -- without an explicit flush here,
  // this sequence would just sit buffered until some unrelated
  // redraw() happened to flush it, meaning a mouse gesture attempted
  // right after entering a mode could still hit Terminal.app's native
  // text-selection instead of us, if nothing had flushed yet (found
  // via a real PTY run: the enable sequence wasn't observed in the
  // output at all until well after other activity forced a flush).
  display.flush();
}

struct TerminalCoastResult {
  bool hadLeftoverByte = false;
  int leftoverByte = 0;
};

void clearHoverAndRedraw(fm::ContentView &view) {
  view.setHoverIndex(std::nullopt);
  view.redraw();
}

void applyHoverRowAndRedraw(fm::ContentView &view, int mouseRow) {
  if (mouseRow >= 0) {
    int lineIndex = view.lineIndexAtRow(mouseRow);
    view.setHoverIndex(lineIndex >= 0 ? std::optional<int>(lineIndex) : std::nullopt);
  }
  view.redraw();
}

// Runs the real-terminal equivalent of runGuiPhase()'s drag-scroll
// coasting -- see CLAUDE.md's 2026-07-30 note on why this exists
// alongside the SDL path rather than instead of it: it trades true
// sub-row pixel compositing (impossible here -- a real terminal only
// exposes character-cell escape sequences, never raw framebuffer
// access) for landing EXACTLY on the real terminal session the user
// is already looking at, no separate window, colors/font always
// exactly right because it never stopped being the real terminal.
//
// Shared by both real-terminal triggers: Shift+Up/Shift+Down (see
// peekShiftArrow(), which calls this at level 1) and a mouse-drag
// release (see runTerminalMouseDrag(), which calls this at whatever
// level the release speed worked out to). Reuses the SAME
// fm::kScrollSpeedLevels timing table runGuiPhase() does, scaled up
// per row via fm::scaledForRow() (there's no sub-row unit on a real
// terminal -- see its own comment for why the scale factor exists at
// all: the user found the unscaled version far too fast compared to
// the GUI mouse-drag's own minimum speed). Assumes mouse tracking is
// already enabled by the caller (runTerminalPhase() manages that
// ambiently based on mode now, not this function -- see its own
// comment) -- this function only ever reads/writes hover state, never
// toggles tracking itself. Stops on: a list boundary; a click (which
// also opens whatever row is under the pointer); or any key other
// than Shift+Up/Down (repeating the SAME direction accelerates; the
// OPPOSITE direction reverses and resets to the base speed) -- that
// key is handed back to the caller to feed through normally, exactly
// as if this function had never run.
TerminalCoastResult runCoastLoop(fm::Application &app, fm::Display &display, fm::KeyInput &keyInput,
                                  int initialLevel, int initialDirection) {
  (void)display;  // kept in the signature for symmetry with runTerminalMouseDrag(); tracking is ambient now
  fm::ContentView &view = app.activeContentView();
  int level = initialLevel;
  int direction = initialDirection;
  // The most recently reported mouse row (display-row space, i.e.
  // moveCursor()'s coordinate space), or -1 before any mouse event has
  // arrived. Deliberately a ROW, not a resolved line index: content
  // keeps moving underneath a stationary mouse while coasting, so
  // which LINE is under this row has to be recomputed against the
  // current topIndex_ every step (see applyHoverRowAndRedraw()) -- a
  // fixed line index would instead have the highlight visibly drift
  // away from the actual pointer position as the list scrolled past
  // it (found via a real PTY run: a hover event correctly highlighted
  // row 9, but by the time of the next screen capture -- several
  // scroll steps later, mouse untouched -- the highlight had already
  // scrolled off to a different row entirely).
  int lastMouseRow = -1;
  TerminalCoastResult result;

  for (;;) {
    performScrollStepDelay(fm::scaledForRow(fm::kScrollSpeedLevels[level]));

    int before = view.topIndex();
    view.setTopIndex(before + direction);
    if (view.topIndex() == before) {  // hit a boundary
      clearHoverAndRedraw(view);
      return result;
    }
    // redraw() calls clampViewport(), which enforces the normal
    // moveUp()/moveDown() invariant "topIndex_ never leads
    // cursorIndex_" -- since setTopIndex() deliberately does NOT move
    // cursorIndex_ (see its own comment), that clamp would otherwise
    // snap topIndex_ straight back to 0 on every single redraw() call
    // here (found via a real PTY run: topIndex_ kept reporting 0->1
    // and back to 0 every iteration). Riding cursorIndex_ along with
    // topIndex_ during coast keeps the clamp a no-op; it's harmless
    // because the hover row (not the cursor) is what's actually
    // highlighted while coasting (see setHoverIndex()), and where the
    // cursor lands once coasting stops is a reasonable resting
    // position, the same way pageDown() leaves it wherever the page
    // landed.
    view.setCursorIndex(view.topIndex());
    applyHoverRowAndRedraw(view, lastMouseRow);

    struct pollfd fd{STDIN_FILENO, POLLIN, 0};
    if (poll(&fd, 1, 0) <= 0 || !(fd.revents & POLLIN)) continue;  // nothing pending -- keep coasting

    int byte = keyInput.readByte();
    if (!byte) continue;  // spurious wakeup/timeout artifact

    if (byte == 0x1b) {
      ShiftArrow arrow = peekShiftArrow(keyInput);
      if (arrow != ShiftArrow::None) {
        int pressedDirection = (arrow == ShiftArrow::Up) ? -1 : 1;
        if (pressedDirection == direction) {
          level = resolveCoastLevel(std::min(level + 1, static_cast<int>(fm::kScrollSpeedLevels.size()) - 1));
        } else {
          direction = pressedDirection;
          level = resolveCoastLevel(1);
        }
        continue;
      }
      auto mouse = peekMouseSgr(keyInput);
      if (mouse) {
        lastMouseRow = mouse->row;
        if (mouse->pressed && !mouse->motionOnly) {
          int lineIndex = view.lineIndexAtRow(mouse->row);
          if (lineIndex >= 0) {
            view.setCursorIndex(lineIndex);  // handleMouseClick() opens an already-selected row
            int row = mouse->row, col = mouse->column;
            clearHoverAndRedraw(view);
            app.handleMouseClick(row, col);
            return result;
          }
        }
        applyHoverRowAndRedraw(view, lastMouseRow);
        continue;
      }
      // Neither a Shift-arrow nor a mouse report -- some other ESC
      // sequence (an ordinary arrow key, ^[s, etc). Nothing beyond the
      // ESC byte itself has been consumed (both peeks above queue
      // their own bytes back on mismatch), so requeue ESC too and let
      // the normal terminal loop re-read and handle it exactly as it
      // always does.
      clearHoverAndRedraw(view);
      keyInput.playRawMacro("\x1b");
      return result;
    }

    clearHoverAndRedraw(view);
    result.hadLeftoverByte = true;
    result.leftoverByte = byte;
    return result;
  }
}

// Runs a real-terminal drag-scroll GESTURE from the moment a left-
// button SGR press is detected (see runTerminalPhase()'s own ESC
// branch) through its release. While the button stays down, the list
// follows the pointer 1:1 (row for row, natural-scrolling direction --
// a downward drag reveals earlier content, matching
// GraphicalDisplay::renderScrolled()'s own convention). On release,
// the same recent-motion-window release-speed calculation
// runGuiPhase() uses (see its own kReleaseWindow) picks a
// fm::kScrollSpeedLevels level via fm::levelForReleaseDistance()
// (passing cellHeightPixels=1, since a "pixel" there is already a row
// here), then hands off into runCoastLoop() to keep going at that
// speed -- exactly the mouse-drag equivalent of what Shift+Up/Down
// does via peekShiftArrow(). A release with no meaningful motion (a
// plain click) isn't a drag at all: it's handled exactly like GUI
// mode's click-an-already-selected-row-opens-it, via
// Application::handleMouseClick().
TerminalCoastResult runTerminalMouseDrag(fm::Application &app, fm::Display &display, fm::KeyInput &keyInput,
                                          int startRow) {
  fm::ContentView &view = app.activeContentView();
  int lastRow = startRow;
  bool moved = false;
  std::deque<std::pair<std::chrono::steady_clock::time_point, int>> recentMotion;
  constexpr auto kReleaseWindow = std::chrono::milliseconds(150);
  applyHoverRowAndRedraw(view, startRow);

  for (;;) {
    int byte = keyInput.readByte();
    if (!byte) continue;  // waiting for the next mouse event -- VTIME polling handles the actual wait

    if (byte != 0x1b) {
      // An ordinary key arrived mid-gesture -- shouldn't normally
      // happen (a real button-down blocks normal typing on any real
      // terminal), but handled the same defensive way runCoastLoop()
      // handles its own equivalent case: abandon cleanly, hand it
      // back for normal processing.
      clearHoverAndRedraw(view);
      TerminalCoastResult result;
      result.hadLeftoverByte = true;
      result.leftoverByte = byte;
      return result;
    }
    auto mouse = peekMouseSgr(keyInput);
    if (!mouse) {
      // Not a mouse report -- some other ESC sequence arrived
      // mid-gesture. Requeue ESC and bail, same as runCoastLoop().
      clearHoverAndRedraw(view);
      keyInput.playRawMacro("\x1b");
      return TerminalCoastResult{};
    }

    if (mouse->motionOnly) {
      int rowDelta = mouse->row - lastRow;
      lastRow = mouse->row;
      if (rowDelta != 0) {
        moved = true;
        view.setTopIndex(view.topIndex() - rowDelta);  // natural scrolling: dragging down reveals earlier content
        view.setCursorIndex(view.topIndex());           // see runCoastLoop()'s clampViewport() note
        auto now = std::chrono::steady_clock::now();
        recentMotion.push_back({now, rowDelta});
        while (!recentMotion.empty() && recentMotion.front().first < now - kReleaseWindow) recentMotion.pop_front();
      }
      applyHoverRowAndRedraw(view, lastRow);
      continue;
    }

    if (mouse->pressed) continue;  // an extra press while already down -- ignore

    // Release.
    if (!moved) {
      int lineIndex = view.lineIndexAtRow(mouse->row);
      if (lineIndex >= 0) view.setCursorIndex(lineIndex);
      clearHoverAndRedraw(view);
      app.handleMouseClick(mouse->row, mouse->column);
      return TerminalCoastResult{};
    }
    auto cutoff = std::chrono::steady_clock::now() - kReleaseWindow;
    int releaseDistance = 0;
    for (auto &sample : recentMotion)
      if (sample.first >= cutoff) releaseDistance += sample.second;
    int level = resolveCoastLevel(fm::levelForReleaseDistance(releaseDistance, 1));
    if (level == 0) {
      clearHoverAndRedraw(view);
      return TerminalCoastResult{};
    }
    int direction = releaseDistance > 0 ? -1 : 1;  // same natural-scrolling sign as the live-drag step above
    return runCoastLoop(app, display, keyInput, level, direction);
  }
}

// Runs until wantsExit() or a GUI-mode toggle is requested. Returns
// true if it should be followed by a GUI phase (false means the whole
// program is exiting).
//
// Owns xterm mouse tracking's on/off lifecycle for the whole terminal
// phase (see setTerminalMouseTracking()): ON whenever the current mode
// isn't Shell (so a drag on the ls screen etc. is ours to interpret,
// rather than falling through to Terminal.app's own native text-
// selection -- exactly the "drag becomes a copy-range selection
// instead" the user reported before this was wired up at the
// runTerminalPhase level), OFF in Shell mode (native copy/paste from
// real shell output matters there, and mouse-drag-as-scroll doesn't
// apply to a live pty session the same way). Also force-disabled on
// every way out of this function -- including the final program exit
// -- so a real shell prompt afterward is never left with broken mouse
// click/selection behavior.
bool runTerminalPhase(fm::Application &app, fm::KeyInput &keyInput, fm::Display &display) {
  bool mouseTrackingOn = false;
  auto syncMouseTracking = [&]() {
    bool want = app.currentMode() != fm::ApplicationMode::Shell;
    if (want != mouseTrackingOn) {
      setTerminalMouseTracking(display, want);
      mouseTrackingOn = want;
    }
  };
  auto leavePhase = [&](bool result) {
    if (mouseTrackingOn) {
      setTerminalMouseTracking(display, false);
      mouseTrackingOn = false;
    }
    return result;
  };

  while (!app.wantsExit()) {
    if (app.consumeGuiModeToggleRequest()) return leavePhase(true);
    syncMouseTracking();
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
      // Shift+Up/Down (see peekShiftArrow()'s own comment): the
      // keyboard-driven real-terminal drag-scroll trigger. Checked
      // first because its sequence is longer than Up/Down's own
      // unmodified ones, which readEscapeSequence() reads a fixed,
      // shorter length for -- a mismatch here safely requeues
      // whatever was read (via playRawMacro()) rather than losing
      // bytes.
      ShiftArrow arrow = peekShiftArrow(keyInput);
      if (arrow != ShiftArrow::None) {
        TerminalCoastResult coast =
            runCoastLoop(app, display, keyInput, resolveCoastLevel(1), arrow == ShiftArrow::Up ? -1 : 1);
        if (coast.hadLeftoverByte) app.feedByte(coast.leftoverByte, std::nullopt);
        continue;
      }
      // A mouse report -- only actually arrives while mouseTrackingOn
      // (Terminal.app doesn't send these otherwise), but safe to
      // attempt unconditionally: peekMouseSgr() requeues via
      // playRawMacro() on any mismatch, same as peekShiftArrow().
      auto mouse = peekMouseSgr(keyInput);
      if (mouse) {
        if (mouse->pressed && !mouse->motionOnly && mouse->button == 0) {
          // Left-button down: the mouse-driven equivalent of
          // Shift+Up/Down -- see runTerminalMouseDrag()'s own comment
          // for why this isn't decided as click-vs-drag until release.
          TerminalCoastResult coast = runTerminalMouseDrag(app, display, keyInput, mouse->row);
          if (coast.hadLeftoverByte) app.feedByte(coast.leftoverByte, std::nullopt);
        } else if (mouse->motionOnly) {
          // Plain hover (no button held), outside any active drag or
          // coast: just keep the highlight live, matching how a
          // pointer resting over a normal file-manager list usually
          // previews what a click would hit.
          applyHoverRowAndRedraw(app.activeContentView(), mouse->row);
        }
        // Any other combination (right-click, an orphan release,
        // etc.): nothing wired to it yet, silently ignored.
        continue;
      }
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
  return leavePhase(false);
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

// Syncs cursorIndex_ to the active view's current topIndex() right
// before a key that's about to stop an active coast is forwarded to
// the app -- otherwise the very next ContentView::moveUp()/moveDown()
// call (from THIS SAME key) runs its own clampViewport(), which snaps
// topIndex_ straight back to cursorIndex_'s stale, pre-coast value
// (cursorIndex_ deliberately never moves while coasting, see
// setTopIndex()'s own comment), discarding the whole scroll. Found via
// a real report ("current" in the mode-line looked frozen during a
// coast) that led to tracing exactly this path: pressing a plain key
// to stop a long coast was silently snapping the view back near the
// top instead of continuing from wherever the coast had scrolled to --
// a more serious variant of the same clampViewport()-desync class of
// bug items 68-69's wasActivelyScrolling fix addressed, just via a
// different call path (an app.feedByte()-triggered redraw() from
// INSIDE this same key-consumption loop, not the outer loop's own
// settled-branch redraw() wasActivelyScrolling guards). Also clears
// wasActivelyScrolling so the settled branch's own sync doesn't run a
// second time immediately after and undo the cursor move this key was
// actually supposed to make.
void syncCursorBeforeCoastStoppingKey(fm::Application &app, int coastLevel, bool &wasActivelyScrolling) {
  if (coastLevel <= 0) return;
  fm::ContentView &activeView = app.activeContentView();
  activeView.setCursorIndex(activeView.topIndex());
  wasActivelyScrolling = false;
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
// Set by handleSigintForGuiSession() (a real signal handler, so it can
// only touch a sig_atomic_t, not call into Application directly) and
// polled once per runGuiPhase() iteration. Only relevant when
// guiOwnsWholeSession -- see main()'s own comment: that's the one
// case where the real terminal that launched usilic is left sitting
// idle (SDL owns keyboard input instead, via GraphicalDisplay's own
// event queue), so a Ctrl-C typed into THAT terminal has nothing to
// reach it through unless something re-enables ISIG there and catches
// the resulting SIGINT -- ported from nothing in legacy (this
// "waiting terminal" scenario doesn't exist without a separate GUI
// window at all).
volatile std::sig_atomic_t gGuiSessionSigintReceived = 0;
void handleSigintForGuiSession(int) { gGuiSessionSigintReceived = 1; }

void runGuiPhase(fm::Application &app, fm::GraphicalDisplay &gui) {
  bool dragging = false;
  int pixelOffset = 0;      // sub-row offset; -cellHeight < pixelOffset < cellHeight
  int coastLevel = 0;       // 0 == not coasting; otherwise an index into fm::kScrollSpeedLevels
  int coastDirection = 0;   // +1 or -1 while coastLevel > 0, matching renderScrolled()'s pixelOffsetY sign
  // Set on any frame that renders via renderScrolled() (dragging or
  // coasting); checked on the FIRST frame back on the normal redraw()
  // path so the clampViewport()-desync fix below only ever fires once,
  // right at that transition -- see its own comment for why firing it
  // unconditionally on every settled frame was itself a bug (found
  // from a real report: normal, non-Shift arrow-key navigation
  // stopped working AFTER ever using Shift+arrow/mouse-drag once,
  // because this correction was overwriting cursorIndex_ on every
  // single ordinary redraw() too, not just the one right after a
  // coast/drag).
  bool wasActivelyScrolling = false;
  // Rolling window of recent Motion samples, trimmed to the last
  // ~150ms -- summed at ButtonUp to get the "cursor movement at the
  // moment you let go" release distance cb's own speed control used.
  std::deque<std::pair<std::chrono::steady_clock::time_point, int>> recentMotion;
  constexpr auto kReleaseWindow = std::chrono::milliseconds(150);
  // Counts consecutive bare ESC presses, for the ESC-ESC-Enter direct-
  // exit gesture below -- reset by any other key.
  int consecutiveEscapes = 0;

  while (!app.wantsExit()) {
    if (gGuiSessionSigintReceived) {
      app.requestImmediateExit();
      return;
    }
    if (!gui.pumpEvents()) return;  // the window's own close button was clicked

    // Set true below whenever something was actually consumed this
    // iteration (a click, a key, a drag/coast step) -- used to skip
    // the redraw() call entirely on iterations where NOTHING changed,
    // see its own check further down. Added after real profiling
    // (macOS `sample`) showed this loop was calling
    // GraphicalDisplay::flush() -- which re-renders every non-blank
    // cell via TTF_RenderUTF8_Blended -- unconditionally 60 times a
    // second even while the window just sat idle showing a static
    // listing, sustaining 100%+ CPU for no visible reason. This
    // doesn't change what gets rendered, only how often a genuinely
    // unchanged frame gets re-rendered.
    bool anyEventThisFrame = false;

    while (auto click = gui.consumeMouseClick()) {
      anyEventThisFrame = true;
      if (!click->rightButton) app.handleMouseClick(click->row, click->column);
      // right-click: captured but not acted on yet, see class comments
    }
    while (auto key = gui.consumeKeyEvent()) {
      anyEventThisFrame = true;
      // w/s: the keyboard-driven equivalent of a mouse-drag release
      // (see fm::kScrollSpeedLevels' own comment) -- pressing the
      // SAME direction again while already coasting accelerates (one
      // level up); the OPPOSITE direction reverses and resets to the
      // base speed. Direction sign matches coastDirection's existing
      // convention (see its own comment above): w (reveal earlier
      // content, topIndex decreases) is +1, s is -1.
      //
      // Originally bound to Shift+Up/Down; switched to plain w/s
      // (2026-07-31, matching the same up/down sense as a game's WASD
      // scheme) at the user's request -- not because Shift+arrow
      // needed any user-side terminal configuration in GUI mode
      // (unlike the real-terminal fallback's own Shift+arrow trigger,
      // Terminal.app's own keyboard remapping was never involved
      // here: GUI mode reads SDL's native KMOD_SHIFT flag directly),
      // but because a plain letter is one keystroke instead of a
      // held-modifier combo, and the user found that more comfortable
      // to reach for repeatedly. Gated on currentMode() != Shell and
      // !awaitingSequenceOrPromptByte() so it never steals a literal
      // "w"/"s" that should reach the shell's command line, an
      // FGREP/path prompt, or a pending ESC/^J-leader sequence (ESC s
      // == StartShell, in particular) -- Shift+Up/Down needed no such
      // gate since holding Shift never produces a plain byte those
      // paths could otherwise want.
      bool wKey = !key->special && (key->byte == 'w' || key->byte == 'W');
      bool sKey = !key->special && (key->byte == 's' || key->byte == 'S');
      if ((wKey || sKey) && app.currentMode() != fm::ApplicationMode::Shell &&
          !app.awaitingSequenceOrPromptByte()) {
        consecutiveEscapes = 0;
        int pressedDirection = wKey ? 1 : -1;
        if (coastLevel > 0 && pressedDirection == coastDirection) {
          coastLevel = resolveCoastLevel(std::min(coastLevel + 1, static_cast<int>(fm::kScrollSpeedLevels.size()) - 1));
        } else {
          coastDirection = pressedDirection;
          coastLevel = resolveCoastLevel(1);
        }
        dragging = false;  // a keyboard trigger always wins over a stale drag flag
      } else if (key->special) {
        consecutiveEscapes = 0;
        // Any other real key stops an active coast, matching
        // runTerminalCoast()'s own rule -- pixelOffset must be reset
        // together with coastLevel here (see pixelOffset's own
        // "stopping a coast" comment below the render-mode branch):
        // leaving a nonzero fractional remainder keeps the loop stuck
        // in the expensive renderScrolled() full-frame path forever.
        syncCursorBeforeCoastStoppingKey(app, coastLevel, wasActivelyScrolling);
        coastLevel = 0;
        pixelOffset = 0;
        app.feedByte(0, translateDisplayKey(*key->special));
      } else if (key->byte == 0x1b) {
        // ESC-ESC-Enter: a direct exit gesture requested in place of
        // the normal ^X^C + y/n confirmation prompt for GUI-launched
        // sessions (see main()'s own comment on why). A lone ESC
        // doesn't touch coastLevel -- typing ESC ESC Enter mid-coast
        // is still meant to exit, not just stop scrolling.
        consecutiveEscapes = std::min(consecutiveEscapes + 1, 2);
      } else if (key->byte == 0x0d && consecutiveEscapes >= 2) {
        app.requestImmediateExit();
      } else {
        consecutiveEscapes = 0;
        syncCursorBeforeCoastStoppingKey(app, coastLevel, wasActivelyScrolling);
        coastLevel = 0;
        pixelOffset = 0;  // see the key->special branch's comment above
        app.feedByte(key->byte, std::nullopt);
      }
      if (app.consumeGuiModeToggleRequest()) return;
    }

    fm::ContentView &view = app.activeContentView();
    int cellHeight = std::max(gui.cellHeightPixels(), 1);

    while (auto drag = gui.consumeDragEvent()) {
      anyEventThisFrame = true;
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
          coastLevel = resolveCoastLevel(fm::levelForReleaseDistance(releaseDistance, cellHeight));
          coastDirection = coastLevel > 0 ? (releaseDistance > 0 ? 1 : -1) : 0;
          // A release too slow/short to coast (coastLevel resolves to
          // 0 here) still needs pixelOffset cleared -- see the
          // key->special branch's comment above for why a leftover
          // fractional remainder wedges the render-mode check forever.
          if (coastLevel == 0) pixelOffset = 0;
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
          // Hit a boundary -- stop rather than pin against the edge.
          // commitRowCrossings() itself left pixelOffset clamped to
          // +-(cellHeight-1) (its "pinned" value, meant for the
          // active-drag case where the finger is still held down and
          // the content should visibly push up against the edge);
          // since coasting is ending here entirely, that residual
          // must be discarded too, or the render-mode check below
          // stays wedged on the expensive per-frame renderScrolled()
          // path forever even though nothing is moving anymore (this
          // is what a real report of CPU usage jumping UP right as
          // scrolling visibly stopped traced back to).
          coastLevel = 0;
          coastDirection = 0;
          pixelOffset = 0;
          break;
        }
      } while (std::chrono::steady_clock::now() < frameDeadline);
    }

    // Checked here (before the render decision below), not after, so
    // new Shell output arriving this frame counts toward
    // anyEventThisFrame -- otherwise a live Shell session's own
    // output could go unrendered indefinitely once the redraw-skip
    // optimization below is in place, since typing into a DIFFERENT
    // program (the shell) doesn't generate any GraphicalDisplay
    // click/key/drag event of its own.
    bool watchShellPty = app.currentMode() == fm::ApplicationMode::Shell && app.shell().alive();
    if (watchShellPty) {
      struct pollfd fd{app.shell().ptyFd(), POLLIN, 0};
      if (poll(&fd, 1, 0) > 0 && (fd.revents & POLLIN)) anyEventThisFrame |= app.shell().onPtyReadable();
    }

    if (dragging || coastLevel > 0 || pixelOffset != 0) {
      wasActivelyScrolling = true;
      std::vector<std::string> lines;
      lines.reserve(view.lines().size());
      for (const auto &line : view.lines()) lines.push_back(line.text);
      gui.renderScrolled(lines, view.topIndex(), pixelOffset, view.scrollModeLineText());
    } else if (wasActivelyScrolling || anyEventThisFrame) {
      // Settled on a row boundary -- back to the normal Display-
      // interface rendering path. redraw() calls clampViewport(),
      // which enforces the normal moveUp()/moveDown() invariant
      // "topIndex_ never leads cursorIndex_"; renderScrolled() (used
      // for every frame while actively dragging/coasting) never
      // touches cursorIndex_ at all, so immediately after coasting
      // settles, cursorIndex_ can still be wherever it was BEFORE the
      // scroll even started while topIndex_ has moved far away -- the
      // FIRST redraw() back on this path would otherwise silently
      // snap topIndex_ straight back to cursorIndex_, discarding the
      // entire scroll (found from a real report: a plain, non-Shift
      // arrow key pressed right after a coast appeared to "flicker one
      // row then snap back to the top," which was actually this exact
      // reset firing on the very next redraw()). Same fix already
      // applied to the real-terminal path's runCoastLoop().
      //
      // Gated on wasActivelyScrolling so this only fires on that ONE
      // transition frame, not every ordinary redraw(): doing it
      // unconditionally (an earlier version of this fix) broke plain
      // keyboard navigation entirely -- every redraw() from a normal
      // Up/Down/Enter keypress was ALSO overwriting cursorIndex_ with
      // topIndex_, which is only correct immediately after a coast,
      // not in general (found from a real report: after using
      // Shift+arrow/drag even once, ordinary arrow-key navigation
      // stopped moving the selection at all).
      if (wasActivelyScrolling) view.setCursorIndex(view.topIndex());
      wasActivelyScrolling = false;
      view.redraw();
    }

    usleep(16000);  // ~60Hz pacing -- see file comment on why this loop can't share the terminal loop's poll()
  }
}

// Resolves a font, creates a GraphicalDisplay sized to match
// `display`'s current geometry, applies `appearance` (if given) so
// the window matches the calling terminal's own colors instead of
// GraphicalDisplay's hardcoded defaults, swaps it in via
// setBackend(), runs runGuiPhase() until it exits (^Og again or the
// window's own close button), then swaps back to `terminalDisplay`.
// Returns false without doing anything if no usable font was found or
// GraphicalDisplay::create() itself failed (headless/no display
// server, the common case over SSH or in CI) -- the caller should
// just continue in terminal mode in that case, never a hard failure.
// Shared by both entry points: the ^Og-triggered mid-session toggle
// (main()'s own loop, `appearance` left unset -- an explicit user
// request, so showing up with default colors is fine, unchanged from
// before) and the "実はすでにGUI" launch-time attempt (main(), called
// once before ever entering runTerminalPhase() -- see its own call
// site comment for why THAT one insists on a match first).
bool tryEnterGuiPhase(fm::Application &app, fm::DelegatingDisplay &display, fm::TerminalDisplay &terminalDisplay,
                      std::optional<TerminalAppearance> appearance = std::nullopt) {
  std::string fontPath = resolveGuiFontPath(appearance);
  std::unique_ptr<fm::GraphicalDisplay> gui =
      fontPath.empty() ? nullptr : fm::GraphicalDisplay::create(display.rows(), display.columns(), fontPath);
  if (!gui) return false;  // no usable font, or window/renderer creation failed -- stay in terminal mode

  if (appearance) gui->setColors(appearance->foreground, appearance->background);

  gui->open();
  display.setBackend(*gui);
  app.handleResize();  // re-layout/redraw against the (same-sized, but now GUI-rendered) geometry

  runGuiPhase(app, *gui);

  display.setBackend(terminalDisplay);
  gui.reset();  // closes the real window
  if (!app.wantsExit()) app.handleResize();  // redraw the terminal, which SDL's window never touched
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  loadScrollDebugLevelFromEnvironment();

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

  // "実はすでにGUI" (docs/ideas/IDEAS.md, see CLAUDE.md's 2026-07-30
  // notes): if the calling terminal's own colors AND font size can be
  // matched (currently only macOS Terminal.app, via
  // queryTerminalAppColors()), swap the backend to a matching
  // GraphicalDisplay window BEFORE display.open()/app.start() ever
  // render anything -- true dot-level scrolling
  // (GraphicalDisplay::renderScrolled()'s real per-pixel compositing)
  // is only possible with real pixel access, which a real terminal
  // fundamentally doesn't have (see runTerminalMouseDrag()/
  // runCoastLoop()'s own comments on why THAT path is row-granular
  // instead), and doing the swap before the first render means the
  // real terminal never shows so much as the initial directory
  // listing -- a brief flash of `ls` output in the real terminal
  // before the GUI window took over was an unwanted seam, not
  // something worth preserving (found from a real screenshot: the
  // real terminal stayed showing its own shell prompt the whole time
  // once this was fixed). Deliberately gated on a successful color
  // match, not attempted unconditionally: showing up in a mismatched-
  // color window nobody asked for is the exact "配色が逆" complaint
  // this whole feature exists to avoid -- on any other terminal/
  // platform this is simply skipped, leaving the normal terminal-
  // first flow below completely unchanged. guiOwnsWholeSession being
  // true also means the tail of main() never falls back into
  // runTerminalPhase() at all: once GUI mode starts the session, it
  // runs it start to finish (no ^Og-triggered return to terminal mode
  // -- there's no terminal-mode content to return TO in this case,
  // since the real terminal was never used for rendering).
  std::unique_ptr<fm::GraphicalDisplay> launchGui;
  bool guiOwnsWholeSession = false;
  if (auto appearance = queryTerminalAppColors()) {
    std::string fontPath = resolveGuiFontPath(appearance);
    launchGui = fontPath.empty() ? nullptr
                                  : fm::GraphicalDisplay::create(display.rows(), display.columns(), fontPath,
                                                                  appearance->fontPointSize);
    if (launchGui) {
      launchGui->setColors(appearance->foreground, appearance->background);
      launchGui->open();
      display.setBackend(*launchGui);
      guiOwnsWholeSession = true;
      // The real terminal that launched usilic is left sitting idle
      // for the rest of the session (see main()'s own comment) --
      // keyInput.open() above already put it in raw mode (ISIG off),
      // which means the INTR character (Ctrl-C) never reaches the
      // kernel's SIGINT delivery at all, it just becomes an unread
      // byte nobody consumes. Restoring cooked mode there lets a real
      // Ctrl-C typed into that terminal behave normally again, and the
      // handler below turns the resulting SIGINT into the same
      // graceful requestImmediateExit() path ESC-ESC-Enter uses inside
      // the GUI window, rather than the default "kill the process
      // without running GraphicalDisplay's destructor" behavior (which
      // is exactly the abrupt-termination problem the ESC-ESC-Enter
      // gesture was added to avoid in the first place). Deliberately
      // scoped to guiOwnsWholeSession only -- the terminal phase's own
      // raw mode (ISIG off) is intentional there, so ^C can be part of
      // its own keybindings (^X^C) instead of generating a real SIGINT.
      keyInput.leaveRawMode();
      std::signal(SIGINT, handleSigintForGuiSession);
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

  if (guiOwnsWholeSession) {
    runGuiPhase(app, *launchGui);
  } else {
    while (!app.wantsExit()) {
      if (!runTerminalPhase(app, keyInput, display)) break;
      if (app.wantsExit()) break;
      tryEnterGuiPhase(app, display, *terminalDisplay);  // ^Og fallback -- unmatched terminal or no display server
    }
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
