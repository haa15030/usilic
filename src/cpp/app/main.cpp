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
//     file's path; unset falls back to /usr/local/lib/usilic/HELP
//     (legacy's own config.sh LIB convention for a PROGNAME=usilic
//     build -- see the HELP-loading block's own comment). Unset with
//     no fallback file present either, or an unreadable file, just
//     leaves HELP unavailable rather than failing startup.
//     Application::loadHelpText() takes already-read text, same
//     "caller does the file I/O" rule as usilicrc/history.
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "application.hpp"
#include "child_process.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "delegating_display.hpp"
#include "drag_scroll_speed.hpp"
#include "gnome_terminal_appearance.hpp"
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
  // False when this query could only determine FONT, not color (see
  // queryPtyxisAppearance()'s own comment on why background/foreground
  // above are just left default-zeroed/meaningless in that case) --
  // callers must check this before calling GraphicalDisplay::
  // setColors(), rather than assuming every successful query carries
  // real color data the way every OTHER terminal's query in this file
  // (Terminal.app, iTerm2, GNOME Terminal) always has.
  bool hasColors = true;
  int fontPointSize = 16;  // matches GraphicalDisplay::create()'s own previous hardcoded default
  // The calling terminal's own font NAME (Terminal.app's AppleScript
  // "font name" property, e.g. "MS-Gothic"), empty if unavailable --
  // see resolveGuiFontPath()'s own comment on how this gets turned
  // into an actual loadable file path.
  std::string fontName;
};

struct TerminalWindowBounds {
  int x = 0, y = 0, width = 0, height = 0;
};

// USILICFONT; then (when `appearance` carries a real font NAME from
// the calling terminal) that name resolved to a real file via
// CoreText on macOS (mac_font_resolver.hpp) or fontconfig's
// `fc-match` on Linux (gnome_terminal_appearance.hpp); then a short
// best-effort list of common monospace system font paths (Mac, then
// common Linux distros) -- see file comment on why this mirrors
// USILICHELP's own resolution shape. Empty if none found, meaning GUI
// mode simply stays unavailable.
//
// The name-resolution step was added 2026-07-31 (macOS/CoreText
// first, Linux/fc-match shortly after) at a real report that the GUI
// window wasn't honoring the calling terminal's configured font (e.g.
// "ＭＳゴシック14", needed for full Japanese glyph coverage without
// falling back to the separate CJK-fallback-font mechanism at all) --
// until then this function only ever matched the terminal's font SIZE
// (see TerminalAppearance's own history), never its NAME, because
// resolving a name to a file needs a real font-lookup mechanism.
// Trying BOTH resolvers unconditionally (rather than branching on
// platform) is deliberate and harmless: whichever tool isn't
// installed just fails its ChildProcess::run() cleanly (see each
// resolver's own comment), so this works unchanged if e.g. a Linux
// box somehow has CoreText-alike tooling or a Mac has fontconfig
// installed via Homebrew, without needing a compile-time platform
// branch here at all.
std::string resolveGuiFontPath(std::optional<TerminalAppearance> appearance = std::nullopt) {
  if (const char *env = std::getenv("USILICFONT")) return env;
  if (appearance && !appearance->fontName.empty()) {
    std::string resolved = fm::resolveMacFontPath(appearance->fontName, appearance->fontPointSize);
    if (!resolved.empty()) return resolved;
    resolved = fm::resolveLinuxFontPath(appearance->fontName);
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
// This process's own controlling terminal device's CANONICAL path
// (e.g. "/dev/ttys016"), used to find the EXACT Terminal.app/iTerm2
// window/tab this process was actually launched from, by matching
// against each window's own reported `tty` property -- see
// queryTerminalAppColors()/queryTerminalAppWindowBounds()'s own
// comment for why "front window" alone isn't reliable enough for
// this. Empty string on any failure (no controlling terminal at all,
// e.g. launched from a background job) -- every caller already treats
// "couldn't identify a real terminal" as an ordinary, non-fatal
// fallback case.
//
// ttyname(STDIN_FILENO), NOT ttyname() on an fd freshly opened via
// the generic "/dev/tty" alias path (what an earlier version of this
// function did, matching the pattern keyInput.open("/dev/tty") uses
// elsewhere in this file for a DIFFERENT purpose -- reliably reaching
// the controlling terminal regardless of what stdin happens to be
// redirected to, not identifying its canonical path) -- confirmed
// live on this machine that ttyname() on an fd opened that way
// reports back the LITERAL STRING "/dev/tty" itself, not the
// underlying specific device (e.g. "/dev/ttys016") Terminal.app's own
// `tty` property actually reports, which meant this function's own
// first version could never match anything at all. A real user report
// (2026-08-03): after the tty-matching fix below was first written
// using that broken version, "変化無し" (no change) -- confirmed via
// a live end-to-end test on this machine (opening a genuinely new,
// distinctly-positioned Terminal.app window and launching usilic
// inside it) that this was why: not merely "still picks the wrong
// window sometimes" but "never matches ANY window at all," an even
// more silent failure than the original "front window" bug this was
// meant to fix.
std::string currentTtyPath() {
  const char *path = ttyname(STDIN_FILENO);
  return path ? path : "";
}

// AppleScript's own RGB color type is always a {r, g, b} list in the
// 0-65535 range (not 0-255) regardless of which scriptable app
// returns it -- shared by every AppleScript-based color query below
// (Terminal.app, iTerm2) rather than each keeping its own local copy.
unsigned char appleScriptChannelTo255(int channel) {
  return static_cast<unsigned char>(std::clamp(channel / 257, 0, 255));  // 65535 / 255 == 257
}

// AppleScript's "bounds of <window>" is always {left, top, right,
// bottom} (a rectangle's two corners) regardless of which scriptable
// app returns it -- shared by every AppleScript-based window-bounds
// query below. Converts to {x, y, width, height}, which is what
// GraphicalDisplay itself already thinks in.
std::optional<TerminalWindowBounds> parseAppleScriptBounds(const std::string &output) {
  int left, top, right, bottom;
  if (std::sscanf(output.c_str(), "%d, %d, %d, %d", &left, &top, &right, &bottom) != 4) return std::nullopt;
  if (right <= left || bottom <= top) return std::nullopt;
  TerminalWindowBounds bounds;
  bounds.x = left;
  bounds.y = top;
  bounds.width = right - left;
  bounds.height = bottom - top;
  return bounds;
}

std::optional<TerminalAppearance> queryTerminalAppColors() {
  const char *termProgram = std::getenv("TERM_PROGRAM");
  if (!termProgram || std::string(termProgram) != "Apple_Terminal") return std::nullopt;
  std::string tty = currentTtyPath();
  if (tty.empty()) return std::nullopt;

  // Matches the SPECIFIC tab this process's own controlling terminal
  // (`tty`) belongs to, rather than querying "front window" -- a real
  // user report (2026-08-03): switching to a DIFFERENT profile
  // ("Red Sands", one of Terminal.app's own bundled presets, tested
  // alongside another preset called "Homebrew" -- initially and
  // reasonably mistaken by both the user and this assistant for a
  // wholly separate terminal application before being corrected) in a
  // NEW window/tab silently broke color/position detection, because
  // AppleScript's "front window" is whichever Terminal.app window was
  // last active/frontmost AMONG ALL open Terminal windows -- not
  // necessarily the specific one this usilic process actually launched
  // from, once more than one exists (an ordinary, common situation,
  // not an edge case: this project's own dev machine had 6 Terminal.app
  // windows open when this was investigated). Terminal.app's own
  // AppleScript exposes a `tty` property per tab (confirmed live on
  // this machine, `tell application "Terminal" to get tty of every tab
  // of every window`), which is exactly what's needed to identify the
  // one true match deterministically instead of guessing at "front".
  fm::ChildProcess::Result result = fm::ChildProcess::run(
      {"osascript", "-e",
       "tell application \"Terminal\"\n"
       "  repeat with w in windows\n"
       "    repeat with t in tabs of w\n"
       "      if tty of t is \"" +
           tty +
           "\" then\n"
           "        return {background color of t, normal text color of t, font size of t, font name of t}\n"
           "      end if\n"
           "    end repeat\n"
           "  end repeat\n"
           "end tell"});
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

  TerminalAppearance appearance;
  appearance.background = {appleScriptChannelTo255(bgR), appleScriptChannelTo255(bgG), appleScriptChannelTo255(bgB)};
  appearance.foreground = {appleScriptChannelTo255(fgR), appleScriptChannelTo255(fgG), appleScriptChannelTo255(fgB)};
  if (fontSize > 0) appearance.fontPointSize = fontSize;
  if (consumed > 0 && consumed <= static_cast<int>(result.output.size())) {
    std::string fontName = result.output.substr(consumed);
    while (!fontName.empty() && std::isspace(static_cast<unsigned char>(fontName.back()))) fontName.pop_back();
    appearance.fontName = fontName;
  }
  return appearance;
}

// Queries the calling Terminal.app window's own SCREEN position/size
// via AppleScript, so the "実はすでにGUI" launch (see
// queryTerminalAppColors()'s own comment on that feature) can overlay
// the GraphicalDisplay window exactly where the terminal window
// already sits instead of wherever SDL_WINDOWPOS_CENTERED happens to
// place it -- a real user follow-up request (2026-08-03), after
// confirming the window-margin fix looked right, specifically asking
// "テキスト表示ウィンドウに重ねる形でGUIウィンドウ版を表示したい"
// and forwarding a Gemini-drafted technical answer to implement.
//
// That answer's own SDL2 code sample (SDL_GetWindowPosition() on an
// SDL_Window*) assumes the SAME PROCESS created both windows -- not
// what's actually true here (Terminal.app and this usilic process are
// two entirely separate OS processes; this class has no SDL_Window*
// for Terminal.app's window to query in the first place). Gemini's
// own answer anticipated exactly this branch ("別々に起動する2つの
// プログラムでしょうか...OS固有のAPI...動作させているOSを教えて
// いただければ") and asked which case applied -- this project's own
// answer for that question is macOS, so this follows the SAME
// AppleScript approach queryTerminalAppColors() already established
// for exactly this situation (querying a real running Terminal.app
// process's own state from outside it), rather than the same-process
// SDL API the sample code showed.
//
// Same TERM_PROGRAM=Apple_Terminal gate as queryTerminalAppColors()
// (see its own comment on why); returns nullopt on any failure, never
// a hard requirement -- a failed query just leaves the window wherever
// GraphicalDisplay::create() already puts it (SDL_WINDOWPOS_CENTERED),
// same fallback queryTerminalAppColors() itself has.
//
// Matches by `tty` rather than querying "front window" directly, for
// the exact same reason queryTerminalAppColors() now does -- see its
// own comment (a real user report, 2026-08-03: "front window" answered
// with the WRONG one of several open Terminal.app windows once more
// than one existed, silently misplacing the overlay).
//
// AppleScript's "bounds of <window>" reports {left, top, right,
// bottom} (a rectangle's two corners), NOT {x, y, width, height} --
// converted here (parseAppleScriptBounds()) so the caller gets the
// width/height GraphicalDisplay itself already thinks in. This is the
// window's OUTER frame (title bar included) -- NOT the same
// coordinate SDL2's own window position uses (SDL's position/size
// describe the CLIENT area only, title bar added OUTSIDE/ABOVE that;
// confirmed the hard way, 2026-08-07 real report/screenshots ⑮⑯,
// after an earlier version of this comment assumed otherwise and
// left this window's title bar floating title-bar-height above the
// real terminal's own). GraphicalDisplay::setWindowPosition() is
// responsible for that conversion now, using the real OS-reported
// border size rather than this function guessing at it. Not provably
// pixel-perfect even so (a real Terminal.app tab bar, if more than
// one tab is open, adds height this query has no way to detect) --
// this sandbox has no screen-capture access to verify final on-screen
// alignment against a live Terminal.app window beyond what a real
// user screenshot shows.
std::optional<TerminalWindowBounds> queryTerminalAppWindowBounds() {
  const char *termProgram = std::getenv("TERM_PROGRAM");
  if (!termProgram || std::string(termProgram) != "Apple_Terminal") return std::nullopt;
  std::string tty = currentTtyPath();
  if (tty.empty()) return std::nullopt;

  fm::ChildProcess::Result result = fm::ChildProcess::run(
      {"osascript", "-e",
       "tell application \"Terminal\"\n"
       "  repeat with w in windows\n"
       "    repeat with t in tabs of w\n"
       "      if tty of t is \"" +
           tty +
           "\" then\n"
           "        return bounds of w\n"
           "      end if\n"
           "    end repeat\n"
           "  end repeat\n"
           "end tell"});
  if (!result.ok) return std::nullopt;
  return parseAppleScriptBounds(result.output);
}

// iTerm2 counterpart to queryTerminalAppColors()/
// queryTerminalAppWindowBounds() above -- a real user report
// (2026-08-03, two screenshots): the overlay feature those two power
// worked perfectly in Terminal.app but silently did nothing in
// iTerm2 (tested with its built-in "Red Sands" color scheme),
// because TERM_PROGRAM=iTerm.app never matched either function's own
// Apple_Terminal gate, so both silently returned nullopt and the GUI
// window fell back to its own defaults (SDL_WINDOWPOS_CENTERED,
// hardcoded fg/bg).
//
// NOT end-to-end verified against a real running iTerm2 from this
// project's own development sandbox -- iTerm2 isn't installed here
// (confirmed: `osascript -e 'tell application "iTerm2" to version'`
// fails outright, "アプリケーションを取り出すことはできません").
// Follows iTerm2's own documented AppleScript dictionary conventions
// (a "current window" containing a "current session", "background
// color"/"foreground color" as session properties, "bounds" as a
// standard window property) as closely as this project's own
// knowledge allows without live access to check the exact property
// names -- same category of limitation queryGnomeTerminalColors()
// already has for GNOME Terminal (see its own comment), not a new
// kind of gap for this file. If this doesn't actually work, the fix
// is almost certainly a property-name mismatch here, not anything
// structural -- worth checking with a direct `osascript` probe on a
// machine that actually has iTerm2 before changing anything else.
//
// iTerm2's classic AppleScript session object has no font name/size
// property the way Terminal.app's window does (font control moved to
// iTerm2's newer Python API instead) -- TerminalAppearance's
// fontPointSize/fontName are deliberately left at their own defaults
// here rather than guessed at, matching queryGnomeTerminalColors()'s
// own "don't claim more than what's actually queryable" restraint for
// its use-system-font branch.
std::optional<TerminalAppearance> queryITermColors() {
  const char *termProgram = std::getenv("TERM_PROGRAM");
  if (!termProgram || std::string(termProgram) != "iTerm.app") return std::nullopt;
  std::string tty = currentTtyPath();
  if (tty.empty()) return std::nullopt;

  // Matches by `tty` rather than "current session of current window",
  // for the same reason queryTerminalAppColors() now does -- see its
  // own comment (a real user report, 2026-08-03, traced to Terminal.app
  // specifically, but the identical "which window is actually mine"
  // ambiguity applies just as much here once more than one iTerm2
  // window is open). iTerm2's own scripting dictionary is a three-level
  // window > tab > session hierarchy (unlike Terminal.app's flatter
  // window > tab, no separate session level) -- NOT verified against a
  // real running iTerm2 (see this function's own class-level comment).
  fm::ChildProcess::Result result = fm::ChildProcess::run(
      {"osascript", "-e",
       "tell application \"iTerm2\"\n"
       "  repeat with w in windows\n"
       "    repeat with tb in tabs of w\n"
       "      repeat with s in sessions of tb\n"
       "        if tty of s is \"" +
           tty +
           "\" then\n"
           "          return {background color of s, foreground color of s}\n"
           "        end if\n"
           "      end repeat\n"
           "    end repeat\n"
           "  end repeat\n"
           "end tell"});
  if (!result.ok) return std::nullopt;

  int bgR, bgG, bgB, fgR, fgG, fgB;
  if (std::sscanf(result.output.c_str(), "%d, %d, %d, %d, %d, %d", &bgR, &bgG, &bgB, &fgR, &fgG, &fgB) != 6)
    return std::nullopt;

  TerminalAppearance appearance;
  appearance.background = {appleScriptChannelTo255(bgR), appleScriptChannelTo255(bgG), appleScriptChannelTo255(bgB)};
  appearance.foreground = {appleScriptChannelTo255(fgR), appleScriptChannelTo255(fgG), appleScriptChannelTo255(fgB)};
  return appearance;
}

// See queryITermColors()'s own comment on why this isn't end-to-end
// verified. Mirrors queryTerminalAppWindowBounds() exactly, just
// against iTerm2's "current window" instead of Terminal.app's "front
// window".
std::optional<TerminalWindowBounds> queryITermWindowBounds() {
  const char *termProgram = std::getenv("TERM_PROGRAM");
  if (!termProgram || std::string(termProgram) != "iTerm.app") return std::nullopt;
  std::string tty = currentTtyPath();
  if (tty.empty()) return std::nullopt;

  // Matches by `tty` for the same reason queryITermColors() does --
  // see its own comment (also not end-to-end verified).
  fm::ChildProcess::Result result = fm::ChildProcess::run(
      {"osascript", "-e",
       "tell application \"iTerm2\"\n"
       "  repeat with w in windows\n"
       "    repeat with tb in tabs of w\n"
       "      repeat with s in sessions of tb\n"
       "        if tty of s is \"" +
           tty +
           "\" then\n"
           "          return bounds of w\n"
           "        end if\n"
           "      end repeat\n"
           "    end repeat\n"
           "  end repeat\n"
           "end tell"});
  if (!result.ok) return std::nullopt;
  return parseAppleScriptBounds(result.output);
}

// Queries GNOME Terminal's own gsettings-stored profile (background/
// foreground colors, font) -- the Linux counterpart to
// queryTerminalAppColors() above. Added 2026-07-31 after the user
// found (via their own web search) that GNOME Terminal's settings
// ARE queryable from the command line after all, contrary to an
// earlier answer in this project that assumed no such mechanism
// existed on Linux at all.
//
// Gated on GNOME_TERMINAL_SCREEN being set BEFORE spawning any
// gsettings calls -- set specifically by gnome-terminal-server's own
// D-Bus activation, unlike $VTE_VERSION (which every VTE-based
// terminal sets -- Tilix, Terminator, xfce4-terminal, ... -- and
// would misidentify all of those as gnome-terminal, whose gsettings
// schema they don't use). Returns nullopt on any failure (env var
// unset, gsettings or its gnome-terminal schema not installed, no
// default profile, output that doesn't parse) -- exactly the same
// "never a hard requirement" contract queryTerminalAppColors() has.
//
// Verification note: this could NOT be exercised end-to-end against a
// real running GNOME Terminal from this project's own development
// sandbox (a macOS machine; the only Linux environment reachable is a
// headless Docker dev container with no gnome-terminal or D-Bus
// session at all). The gsettings schema/key names and command shapes
// below follow GNOME's own documented conventions; what WAS verified
// directly in that container: gsettings and fc-match are genuinely
// present, gsettings fails cleanly (a plain non-zero exit, not a
// crash) when the gnome-terminal schema isn't installed there
// (confirming the !result.ok early-return path is exercised for
// real), fc-match resolves a real font name to a real file, and every
// STRING PARSING step (gnome_terminal_appearance.hpp's pure
// functions) is doctest-covered directly against real sample output
// strings.
std::optional<TerminalAppearance> queryGnomeTerminalColors() {
  const char *screen = std::getenv("GNOME_TERMINAL_SCREEN");
  if (!screen || !*screen) return std::nullopt;

  fm::ChildProcess::Result profileResult =
      fm::ChildProcess::run({"gsettings", "get", "org.gnome.Terminal.ProfilesList", "default"});
  if (!profileResult.ok) return std::nullopt;
  std::string profileId = fm::stripGsettingsQuotedString(profileResult.output);
  if (profileId.empty()) return std::nullopt;

  std::string profileSchema =
      "org.gnome.Terminal.Legacy.Profile:/org/gnome/terminal/legacy/profiles/:" + profileId + "/";

  auto get = [](const std::string &schema, const std::string &key) -> std::optional<std::string> {
    fm::ChildProcess::Result r = fm::ChildProcess::run({"gsettings", "get", schema, key});
    if (!r.ok) return std::nullopt;
    return r.output;
  };

  auto fgRaw = get(profileSchema, "foreground-color");
  auto bgRaw = get(profileSchema, "background-color");
  if (!fgRaw || !bgRaw) return std::nullopt;
  auto fg = fm::parseGsettingsRgbColor(*fgRaw);
  auto bg = fm::parseGsettingsRgbColor(*bgRaw);
  if (!fg || !bg) return std::nullopt;

  TerminalAppearance appearance;
  appearance.foreground = {fg->r, fg->g, fg->b};
  appearance.background = {bg->r, bg->g, bg->b};

  // use-system-font (a boolean) decides whether the profile's own
  // "font" key applies at all, or whether GNOME's desktop-wide
  // monospace font (org.gnome.desktop.interface's own, unrelated,
  // non-relocatable schema -- no profile path needed) is what
  // actually renders -- exactly the distinction GNOME Terminal's own
  // preferences dialog draws between "System fixed width font" and a
  // custom font choice.
  auto useSystemFont = get(profileSchema, "use-system-font");
  std::optional<std::string> fontRaw =
      (useSystemFont && fm::stripGsettingsQuotedString(*useSystemFont) == "true")
          ? get("org.gnome.desktop.interface", "monospace-font-name")
          : get(profileSchema, "font");
  if (fontRaw) {
    fm::ParsedFontDescription desc = fm::parsePangoFontDescription(*fontRaw, appearance.fontPointSize);
    appearance.fontName = desc.family;
    if (desc.pointSize > 0) appearance.fontPointSize = desc.pointSize;
  }
  return appearance;
}

// Ptyxis (docs.gtk.org/Ptyxis) counterpart to queryGnomeTerminalColors()
// above -- Ubuntu 26.04 (this port's own dev/test machine, confirmed
// live 2026-08-03) ships Ptyxis as its DEFAULT terminal, not GNOME
// Terminal at all (gnome-terminal isn't even installed: `gsettings get
// org.gnome.Terminal.ProfilesList default` fails outright with "no such
// schema"), so queryGnomeTerminalColors()'s own GNOME_TERMINAL_SCREEN
// gate never fires there and Ptyxis was previously entirely
// undetected -- a real user report (2026-08-03: "ubuntuでは背景黒・
// 文字白・大きめウィンドウ", i.e. GraphicalDisplay's own hardcoded
// fallback defaults, confirming NO appearance query was matching at
// all) traced to exactly this.
//
// Gated on PTYXIS_VERSION (confirmed live, run directly inside an
// actual Ptyxis window -- unlike the OTHER env vars checked first,
// SSH'd-in commands don't inherit it, only genuinely running inside
// Ptyxis itself does; that mismatch is what made this take several
// rounds to pin down). Ptyxis's own gsettings schema (org.gnome.Ptyxis,
// confirmed live via `gsettings list-recursively`) turned out to be
// structured very differently from GNOME Terminal's: font-name/
// use-system-font are GLOBAL keys (not per-profile, so no relocatable-
// schema path or profile-UUID lookup is even needed for font alone --
// PTYXIS_PROFILE is confirmed available in the environment too, in
// case a future per-profile need arises, but isn't used here), while
// color is controlled by a "palette" key that turned out (confirmed
// live) to hold a NAMED PRESET STRING ('Ubuntu', matching the maroon
// scheme in every screenshot throughout this investigation) rather
// than actual RGB values -- Ptyxis apparently keeps named-palette RGB
// definitions compiled into the application itself, not exposed via
// gsettings/dconf at all (confirmed no override exists in dconf either
// -- `dconf dump /org/gnome/ptyxis/` came back completely empty, and
// gsettings only ever returns compiled-in schema defaults for anything
// dconf has no override for). Reproducing Ptyxis's own named-palette
// RGB tables without being able to inspect its source or test against
// it further was judged not worth the risk of silently getting colors
// wrong -- the user's own explicit call (2026-08-03, asked directly):
// font detection only, leave color at GraphicalDisplay's existing
// hardcoded default rather than guess. See TerminalAppearance::
// hasColors's own comment for how a caller is expected to respect
// that.
std::optional<TerminalAppearance> queryPtyxisAppearance() {
  if (!std::getenv("PTYXIS_VERSION")) return std::nullopt;

  auto get = [](const std::string &schema, const std::string &key) -> std::optional<std::string> {
    fm::ChildProcess::Result r = fm::ChildProcess::run({"gsettings", "get", schema, key});
    if (!r.ok) return std::nullopt;
    return r.output;
  };

  auto useSystemFont = get("org.gnome.Ptyxis", "use-system-font");
  std::optional<std::string> fontRaw =
      (useSystemFont && fm::stripGsettingsQuotedString(*useSystemFont) == "true")
          ? get("org.gnome.desktop.interface", "monospace-font-name")
          : get("org.gnome.Ptyxis", "font-name");
  if (!fontRaw) return std::nullopt;

  TerminalAppearance appearance;
  appearance.hasColors = false;
  fm::ParsedFontDescription desc = fm::parsePangoFontDescription(*fontRaw, appearance.fontPointSize);
  appearance.fontName = desc.family;
  if (desc.pointSize > 0) appearance.fontPointSize = desc.pointSize;
  return appearance;
}

// Tries every platform-specific "real terminal appearance" query this
// project knows, in order, returning the first successful one --
// queryTerminalAppColors()/queryGnomeTerminalColors() themselves each
// self-gate on their own platform/environment signal and return
// nullopt immediately when that signal is absent, so trying both
// unconditionally costs nothing extra on whichever platform doesn't
// apply.
std::optional<TerminalAppearance> queryRealTerminalAppearance() {
  if (auto appearance = queryTerminalAppColors()) return appearance;
  if (auto appearance = queryITermColors()) return appearance;
  if (auto appearance = queryPtyxisAppearance()) return appearance;
  return queryGnomeTerminalColors();
}

// See queryTerminalAppWindowBounds()/queryITermWindowBounds() -- same
// "try every platform-specific query, first success wins" shape as
// queryRealTerminalAppearance() above, kept separate from it since
// window-bounds and color/font queries have different callers (see
// the "実はすでにGUI" launch site below).
std::optional<TerminalWindowBounds> queryRealTerminalWindowBounds() {
  if (auto bounds = queryTerminalAppWindowBounds()) return bounds;
  return queryITermWindowBounds();
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

// moveCursor (default false, unchanged for every EXISTING caller --
// see each call site's own comment) additionally moves cursorIndex_
// itself to match the hovered row, not just hoverIndex_'s purely
// visual highlight. Added 2026-08-05 for a real user report: opening
// README.jp, returning to Directory mode (^G), then hovering the mouse
// over HELP.jp (which correctly highlighted it) and pressing [Enter]
// re-opened README.jp instead -- because Enter (openSelectedEntry())
// has always acted on cursorIndex_, and hovering alone had never
// touched it (by original design, see setHoverIndex()'s own comment:
// specifically so an active COAST/DRAG's fast-moving highlight
// wouldn't drag the keyboard cursor along for the ride). That
// rationale doesn't apply to a plain, idle hover with nothing coasting
// or dragging -- there, a highlighted row that DOESN'T match what
// Enter/^N/^P will actually act on next is just a confusing trap, not
// a deliberate design tradeoff. Callers representing that idle case
// (the two "plain hover, no button held" sites in both the terminal
// and GUI mouse-tracking paths) pass true; the coast/drag callers
// (runCoastLoop(), runTerminalMouseDrag()) deliberately keep the
// default false, preserving their own documented "hover row, not
// cursor, is what's highlighted while the list races past" behavior.
void applyHoverRowAndRedraw(fm::ContentView &view, int mouseRow, bool moveCursor = false) {
  if (mouseRow >= 0) {
    int lineIndex = view.lineIndexAtRow(mouseRow);
    view.setHoverIndex(lineIndex >= 0 ? std::optional<int>(lineIndex) : std::nullopt);
    if (moveCursor && lineIndex >= 0) {
      view.setCursorIndex(lineIndex);
      // Keeps File mode's own real cursor (see ContentView::
      // syncVisibleCursor()'s own comment) following the hover too,
      // the same way keyboard navigation already does via
      // Application::handleCommand()'s Move*/Page*/GoTo* dispatch --
      // a no-op for highlighted selector modes like Directory (whose
      // highlighting already reflects cursorIndex_ directly).
      view.syncVisibleCursor();
    }
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
      // No longer pre-selecting the row directly here (2026-08-05
      // fix): this used to call view.setCursorIndex(lineIndex)
      // unconditionally BEFORE handleMouseClick() ever ran, bypassing
      // its own isColumnWithinLineScope() gate entirely -- a click
      // landing in DirectoryMode's left margin (or past a short
      // entry's own text) would still select that row via THIS direct
      // mutation, even though handleMouseClick() itself would
      // correctly refuse to act on it. handleMouseClick() already
      // does its own select-then-open sequencing internally (checking
      // idx against cursorIndex() and calling setCursorIndex() itself
      // when appropriate), so this was purely redundant -- letting it
      // be the sole authority here matches how -g's own click handling
      // already works (see Application::handleMouseClick()'s own
      // comment for the full user report this addresses).
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
bool runTerminalPhase(fm::Application &app, fm::KeyInput &keyInput, fm::Display &display, bool forceCui) {
  bool mouseTrackingOn = false;
  // Counts consecutive bare ESC presses, for the ESC-ESC-Enter direct-
  // exit gesture -- see runGuiPhase()'s own comment on why this
  // gesture exists (avoiding an abrupt, cleanup-skipping termination
  // in the "実は既にGUI" auto-swap case). A real user report
  // (2026-08-01) found ESC-ESC-Enter worked under `-g` but not under
  // `-c`, making iterative hand-testing tedious (the normal ^X^C+y/n
  // confirmation prompt still works in both, this is purely an EXTRA
  // shortcut, mirroring GUI mode's own parity rather than replacing
  // anything). Reset by any other key/sequence (see each branch
  // below); a lone ESC that doesn't turn out to be part of a known
  // sequence (Shift+Arrow, mouse SGR, or a special key) increments
  // it instead of reaching Application's own ESC-leader state machine
  // immediately below.
  int consecutiveEscapes = 0;
  // `-c`'s whole purpose is verifying behavior on a plain, graphics-
  // incapable terminal -- a real Linux tty console, or this session's
  // own display-less environment (see -c's own comment near main()'s
  // argv parsing) -- so mouse tracking is never enabled under it,
  // regardless of mode. A real user report (2026-08-02) pointed out
  // that a real bare tty has no mouse reporting capability at all,
  // making mouse-follow under `-c` misleading even though it happened
  // to work when `-c` was actually run inside a real, mouse-capable
  // terminal emulator (Terminal.app) rather than a genuine bare tty.
  // Without `-c`, this is unchanged: real-terminal mouse tracking
  // stays on whenever NOT in Shell mode, same as before.
  auto syncMouseTracking = [&]() {
    bool want = !forceCui && app.currentMode() != fm::ApplicationMode::Shell;
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
    // POLLHUP/POLLERR, not just POLLIN: a real Ubuntu/bash test found
    // that once the child fully closes its end of the pty, Linux's
    // poll() can report POLLHUP alone (no POLLIN bit at all) -- unlike
    // macOS/zsh, where POLLIN still fires and the subsequent read()
    // returning 0 is what onPtyReadable() actually detects EOF from
    // (see its own comment). Checking POLLIN only meant this whole
    // path silently never ran on Linux, so `exit` never got noticed at
    // all there.
    if (watchShellPty && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) && !app.shell().onPtyReadable())
      app.handleShellChildExited();
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
        consecutiveEscapes = 0;
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
        consecutiveEscapes = 0;
        if (mouse->pressed && !mouse->motionOnly && mouse->button == 0) {
          // Left-button down: the mouse-driven equivalent of
          // Shift+Up/Down -- see runTerminalMouseDrag()'s own comment
          // for why this isn't decided as click-vs-drag until release.
          TerminalCoastResult coast = runTerminalMouseDrag(app, display, keyInput, mouse->row);
          if (coast.hadLeftoverByte) app.feedByte(coast.leftoverByte, std::nullopt);
        } else if (mouse->motionOnly && !app.promptActive()) {
          // Plain hover (no button held), outside any active drag or
          // coast: just keep the highlight live, matching how a
          // pointer resting over a normal file-manager list usually
          // previews what a click would hit. Guarded on !promptActive()
          // (2026-08-03 fix, see the GUI-side hover guard below's own
          // comment for the real bug report this addresses) --
          // ContentView::redraw() (called via applyHoverRowAndRedraw())
          // unconditionally redraws its OWN mode-line row, which is the
          // exact row Application's prompt label/input field also live
          // on while a prompt (^Xf, FGREP, ...) is active; without this
          // guard, so much as wiggling the mouse mid-prompt silently
          // wipes out the prompt the user was in the middle of typing
          // into.
          applyHoverRowAndRedraw(app.activeContentView(), mouse->row, /*moveCursor=*/true);
        }
        // Any other combination (right-click, an orphan release,
        // etc.): nothing wired to it yet, silently ignored.
        continue;
      }
      auto special = keyInput.readEscapeSequence();
      if (special) {
        consecutiveEscapes = 0;
        app.feedByte(0, special);
        continue;
      }
      // Not one of Display's known terminal sequences -- a bare ESC.
      // Counts toward the ESC-ESC-Enter direct-exit gesture (see this
      // function's own comment above) before falling through to
      // Application's own ESC-leader state machine (e.g. ^[s for
      // SHELL, ^Og for GUI mode is a PLAIN ^O, not ESC-prefixed, so it
      // never reaches here at all). See KeyInput::lastEscapeBytes()'s
      // own comment for the one remaining inherited quirk (a fast-
      // typed subsequent keystroke can occasionally ride along as a
      // second byte here).
      consecutiveEscapes = std::min(consecutiveEscapes + 1, 2);
      app.feedByte(0x1b, std::nullopt);
      for (unsigned char c : keyInput.lastEscapeBytes()) app.feedByte(c, std::nullopt);
      continue;
    }
    if (byte == 0x0d && consecutiveEscapes >= 2) {
      app.requestImmediateExit();
      continue;
    }
    consecutiveEscapes = 0;
    app.feedByte(byte, std::nullopt);
  }
  return leavePhase(false);
}

// Commits whole-row crossings of pixelOffset into view.stepTopIndex()
// (which moves topIndex_ AND keeps cursorIndex_ pinned to the newly-
// revealed edge, see its own comment), keeping pixelOffset within
// (-cellHeight, cellHeight) between renders -- the same odometer-style
// carry the original friction-based version used, just factored out
// so both the active-drag path and the coasting path (which can cross
// several rows in one call) share it. Returns false if a boundary (top
// or bottom of the list) was hit, so a coasting caller knows to stop
// rather than pin uselessly against the edge.
bool commitRowCrossings(fm::ContentView &view, int cellHeight, int &pixelOffset) {
  while (pixelOffset >= cellHeight) {
    if (!view.stepTopIndex(-1)) {  // revealing EARLIER content
      pixelOffset = cellHeight - 1;
      return false;
    }
    pixelOffset -= cellHeight;
  }
  while (pixelOffset <= -cellHeight) {
    if (!view.stepTopIndex(1)) {  // revealing LATER content
      pixelOffset = -(cellHeight - 1);
      return false;
    }
    pixelOffset += cellHeight;
  }
  return true;
}

// Clears wasActivelyScrolling right before a key that's about to stop
// an active coast is forwarded to the app, so the settled branch's own
// redraw() further down this same frame doesn't treat it as "just
// finished coasting" a second time and interfere with whatever this
// key's own app.feedByte() call is about to do.
//
// This used to ALSO force cursorIndex_ to topIndex_ here, to stop the
// very next ContentView::moveUp()/moveDown() call's own clampViewport()
// from snapping topIndex_ back to a stale pre-coast cursorIndex_ (see
// git history for the original comment) -- no longer needed now that
// ContentView::stepTopIndex() (used by commitRowCrossings(), below)
// keeps cursorIndex_ continuously pinned to the coast's leading edge
// throughout, instead of leaving it stale until an end-of-scroll sync.
// That old sync itself became its own bug once w/s coasting could
// reach a list boundary: it set cursorIndex_ = topIndex_ unconditionally,
// which is the TOP of the viewport regardless of which direction the
// coast had been going -- coasting all the way down to the end of a
// long directory listing left the cursor sitting at the top row of the
// now-bottom-scrolled screen instead of following the bottom edge the
// way scrollLineDown() ('j') does (a real report).
void syncCursorBeforeCoastStoppingKey(int coastLevel, bool &wasActivelyScrolling) {
  if (coastLevel <= 0) return;
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
  // path so that settle-transition frame's redraw() always happens
  // even if nothing else occurred that frame (a coast that stops
  // exactly at a list boundary, with no further key/click/drag event,
  // would otherwise leave stale renderScrolled() output on screen
  // forever -- see the render-mode branch below).
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
      // Acted on immediately, right on press -- a same-day (2026-08-05)
      // revision of two prior attempts at deferring this to release
      // instead (first via a motion flag, then a pixel-jitter
      // threshold), both trying to distinguish "this press is a click"
      // from "this press is the start of a cat's-paw screen drag" by
      // TIMING. The user's own alternative proposal, adopted here
      // instead: distinguish by WHERE the press landed, not when it's
      // acted on. Application::handleMouseClick() now checks whether
      // the column is actually within the clicked row's own
      // highlighted span (ContentView::isColumnWithinLineScope()) and
      // simply no-ops if not -- a press in DirectoryMode's `ls` left
      // margin, or past the end of a short entry name, was never "on"
      // the entry to begin with, so it's free to also double as a drag
      // start without any race against a still-in-flight ButtonUp.
      if (!click->rightButton) app.handleMouseClick(click->row, click->column);
      // right-click: captured but not acted on yet, see class comments
    }
    // Plain hover-follow (no button held) -- matching the real-
    // terminal mouse-tracking path's own applyHoverRowAndRedraw()/
    // motionOnly handling above, which GUI mode never had at all
    // before this (a real user report, 2026-08-02: "-g起動なのに
    // マウスでカーソルが動かないなんてあり得ない"). Skipped while a
    // drag or coast is already in progress, Shell mode is active
    // (same guards the terminal path's own hover handling uses --
    // hover highlighting during an active drag/coast would fight with
    // that gesture's own redraw() calls, and Shell has no
    // ContentView-backed row selection to hover in the first place),
    // or a prompt is active (2026-08-03 fix -- a real side-by-side
    // screenshot comparison against legacy found ^Xf's input field
    // vanishing the moment the mouse moved even slightly: SDL reports
    // SDL_MOUSEMOTION continuously for every pixel of movement, far
    // denser than a real terminal's own SGR mouse reports, so this hit
    // far more reliably in GUI mode than the equivalent gap in the
    // terminal path above -- both are now guarded the same way).
    // ContentView::redraw() (called via applyHoverRowAndRedraw())
    // unconditionally redraws its own mode-line row, the exact row
    // Application's prompt label/input field also live on.
    if (!dragging && coastLevel == 0 && app.currentMode() != fm::ApplicationMode::Shell && !app.promptActive()) {
      while (auto hover = gui.consumeHoverEvent()) {
        anyEventThisFrame = true;
        applyHoverRowAndRedraw(app.activeContentView(), hover->row, /*moveCursor=*/true);
      }
    } else {
      // Drain without acting on any of it (2026-08-05 fix): a real
      // user report -- right after releasing a "cat's paw" flick and
      // letting it coast to a stop, the block cursor visibly jumped up
      // and down "as if replaying" the mouse motion from during the
      // drag/coast itself. GraphicalDisplay::pumpEvents() keeps
      // queuing every SDL_MOUSEMOTION sample as a HoverEvent whenever
      // no button is held (see its own SDL_MOUSEMOTION case) --
      // including motion sampled WHILE coasting (button already
      // released, but coastLevel > 0 the whole time it animates), and
      // this gate above skips consuming (hence draining) that queue
      // for the entire duration of a drag/coast. Every one of those
      // samples piled up UNDRAINED, then all got processed back-to-
      // back the instant this gate reopened once the coast settled --
      // and since applyHoverRowAndRedraw()'s own redraw() ends in a
      // real flush()/SDL_RenderPresent() each time, that wasn't one
      // instant snap to the final position but a rapid-fire sequence
      // of real, visible frames -- exactly the "replaying past mouse
      // operations" look reported. Actively discarding the backlog
      // here (rather than just skipping the block that would have
      // consumed it) means there's nothing stale left queued by the
      // time hovering becomes meaningful again.
      while (gui.consumeHoverEvent()) {
      }
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
      // A keypress always wins over lingering mouse-gesture state --
      // unconditionally, for every key branch below, not just w/s
      // (2026-08-04 fix; see this loop's own further-down comments for
      // the previous, narrower version). A real user report: keyboard
      // navigation (^N/^P, arrows, Enter) would work right after
      // launch but stop responding to anything visible the moment ANY
      // mouse operation had happened even once -- their own diagnosis
      // ("キーボードが押されたらマウス操作ルーチンを離脱してキー操作
      // へ移行では？") was exactly right. Root cause: `dragging` was
      // only ever cleared in the w/s branch below; every OTHER key
      // branch (key->special for arrows/PageUp/PageDown, and the final
      // `else` for ordinary bytes like ^N/^P/Enter) already cleared
      // coastLevel/pixelOffset but left `dragging` untouched. If a
      // click's SDL_MOUSEBUTTONUP is ever missed or delayed (this
      // sandbox couldn't confirm exactly why -- a platform/SDL
      // quirk around SDL_CaptureMouse() on macOS is the leading
      // suspect, but real-machine testing is what actually surfaces
      // it), `dragging` stays stuck true forever afterward. Every
      // subsequent frame then takes the `if (dragging || ...)` branch
      // below and renders via renderScrolled() instead of the normal
      // redraw() -- and renderScrolled() "draws plain glyphs only --
      // no cursor/standout" (see its own class comment): keys were
      // still being fed to Application and updating its internal
      // state correctly the whole time, but the screen kept re-
      // rendering the exact same static frame regardless, which reads
      // exactly like "keyboard stopped working" even though nothing
      // was actually broken past the rendering-mode decision itself.
      dragging = false;
      gui.setHandCursor(false);  // don't leave a stale hand cursor showing either
      gui.setPointingCursor(false);  // ditto, for a scrollbar-thumb drag this key press interrupted

      // Up/Down double as coast-speed control once a coast is already
      // running -- 2026-08-07, replacing the original w/s keys
      // entirely (the user's own follow-up request: w/s removed
      // outright, partly because plain 's' could race against the
      // ESC-leader's own "ESC s" == StartShell sequence). Arrows keep
      // their normal single-line-move role (^P/^N equivalent, via the
      // key->special branch below) whenever nothing is coasting, but
      // once coastLevel > 0 they accelerate/reset instead of stopping
      // the coast and moving one line -- gated "not Shell, not
      // mid-prompt" so a literal arrow keypress reaching the shell or
      // a prompt is never hijacked. Direction: Down is +1 (matches
      // the OLD 'w' -- confirmed backwards on the first real-hardware
      // try with the naive Up=+1 mapping, so this is intentionally
      // the opposite of what "Up should obviously mean +1" reasoning
      // alone would suggest), Up is -1.
      bool upKey = key->special == fm::Display::Key::Up;
      bool downKey = key->special == fm::Display::Key::Down;
      bool arrowSpeedControl = (upKey || downKey) && coastLevel > 0;
      if (arrowSpeedControl && app.currentMode() != fm::ApplicationMode::Shell &&
          !app.awaitingSequenceOrPromptByte()) {
        consecutiveEscapes = 0;
        int pressedDirection = downKey ? 1 : -1;
        if (coastLevel > 0 && pressedDirection == coastDirection) {
          coastLevel = resolveCoastLevel(std::min(coastLevel + 1, static_cast<int>(fm::kScrollSpeedLevels.size()) - 1));
        } else {
          coastDirection = pressedDirection;
          coastLevel = resolveCoastLevel(1);
        }
      } else if (key->special) {
        consecutiveEscapes = 0;
        // Any other real key stops an active coast, matching
        // runTerminalCoast()'s own rule -- pixelOffset must be reset
        // together with coastLevel here (see pixelOffset's own
        // "stopping a coast" comment below the render-mode branch):
        // leaving a nonzero fractional remainder keeps the loop stuck
        // in the expensive renderScrolled() full-frame path forever.
        syncCursorBeforeCoastStoppingKey(coastLevel, wasActivelyScrolling);
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
        syncCursorBeforeCoastStoppingKey(coastLevel, wasActivelyScrolling);
        coastLevel = 0;
        pixelOffset = 0;  // see the key->special branch's comment above
        app.feedByte(key->byte, std::nullopt);
      }
      if (app.consumeGuiModeToggleRequest()) return;
    }

    // Mouse wheel: a per-tick alternative to the coast-speed keys just
    // above, added at the user's own 2026-08-05 request specifically
    // because those keys' own "opposite direction resets straight to
    // coastLevel 1" is an abrupt floor-speed jump, not a decel --
    // there was no way to ease down from a fast coast with the
    // keyboard alone (originally w/s, later replaced by the Up/Down
    // arrows just above -- see their own comment; this wheel logic
    // and its rationale predate that swap and were never tied to
    // which specific keys drove the keyboard side of it). Same
    // acceleration rule (same direction again: +1 level), but the
    // OPPOSITE-direction case here decrements one level per tick
    // instead of jumping, so spinning the wheel the other way eases
    // the current coast down through zero before it actually reverses
    // (matching a real trackpad's own feel far better than a keyboard
    // ever could, which is the whole point of adding wheel support at
    // all -- see WheelEvent's own comment). Same currentMode()/
    // awaitingSequenceOrPromptByte() gate, for the same reason
    // (wheel events firing during Shell output or a pending prompt
    // shouldn't hijack scrolling there).
    while (auto wheel = gui.consumeWheelEvent()) {
      anyEventThisFrame = true;
      if (app.currentMode() == fm::ApplicationMode::Shell || app.awaitingSequenceOrPromptByte()) continue;
      dragging = false;
      gui.setHandCursor(false);
      gui.setPointingCursor(false);
      // Negated relative to a "naive" reading of deltaY (positive ==
      // 'w'/reveal-earlier) -- confirmed backwards via real hardware
      // testing (2026-08-06 user report, macOS's own default "natural
      // scrolling" -- content follows the physical gesture direction,
      // the opposite sense from the old pre-natural-scrolling wheel
      // convention). GraphicalDisplay::consumeWheelEvent() already
      // normalizes SDL_MOUSEWHEEL_FLIPPED away to a FIXED physical-
      // direction sign (see its own comment) -- this negation is a
      // SEPARATE, deliberate choice about which physical direction
      // means which app-semantic direction, picked to match natural
      // scrolling since that's this project's real test machine's own
      // default and the only case actually verified on real hardware.
      // Not re-derived from SDL's own docs a second time here on
      // purpose, after getting the direction wrong once already by
      // reasoning it out instead of testing it.
      int wheelDirection = wheel->deltaY > 0 ? -1 : 1;
      if (coastLevel == 0) {
        coastDirection = wheelDirection;
        coastLevel = resolveCoastLevel(1);
      } else if (wheelDirection == coastDirection) {
        coastLevel = resolveCoastLevel(std::min(coastLevel + 1, static_cast<int>(fm::kScrollSpeedLevels.size()) - 1));
      } else {
        int decayedLevel = coastLevel - 1;
        if (decayedLevel <= 0) {
          coastLevel = 0;
          coastDirection = 0;
          pixelOffset = 0;
        } else {
          coastLevel = resolveCoastLevel(decayedLevel);
        }
      }
    }

    fm::ContentView &view = app.activeContentView();
    int cellHeight = std::max(gui.cellHeightPixels(), 1);

    // Scrollbar thumb drag: an ABSOLUTE "go here" gesture (unlike
    // DragEvent's relative deltaY for content dragging), added
    // 2026-08-07 now that the scrollbar is visible and the user wants
    // it to actually DO something. Maps the drag's current Y straight
    // to a proportional topIndex and jumps there immediately -- no
    // pixelOffset/coast physics involved, since a thumb drag is a
    // direct positional gesture, not a flick with momentum. Reuses
    // the `dragging` flag/render-mode lifecycle content-dragging
    // already has (renderScrolled() every frame while true, one
    // settle redraw() on release) since both are "an active mouse-
    // drag in progress," just driven by different math.
    auto jumpToScrollbarY = [&](int y) {
      int contentRows = std::max(gui.rows() - 1, 0);
      int trackHeight = contentRows * cellHeight;
      int totalLines = static_cast<int>(view.lines().size());
      int scrollableLines = std::max(totalLines - contentRows, 0);
      if (trackHeight <= 0 || scrollableLines <= 0) return;
      double fraction = std::clamp(static_cast<double>(y - gui.marginYPixels()) / trackHeight, 0.0, 1.0);
      view.setTopIndex(static_cast<int>(fraction * scrollableLines));
      // clampViewport() (called by the settle-time redraw() once
      // `dragging` goes false again) would otherwise snap topIndex_
      // straight back to wherever cursorIndex_ still is -- undoing
      // the jump the instant the drag ends -- unless cursorIndex_
      // moves along with it, the same reason commitRowCrossings()
      // keeps it pinned to the coast's own leading edge.
      view.setCursorIndex(view.topIndex());
    };
    while (auto sb = gui.consumeScrollbarEvent()) {
      anyEventThisFrame = true;
      switch (sb->type) {
        case fm::GraphicalDisplay::ScrollbarEventType::ButtonDown:
          dragging = true;
          coastLevel = 0;
          coastDirection = 0;
          pixelOffset = 0;
          // Distinct gesture from grabbing content -- the system
          // pointing-hand (index finger), not the open-hand/grab
          // cursor setHandCursor() shows for content dragging (a real
          // request, 2026-08-10, asked for exactly this split -- see
          // setHandCursor()/setPointingCursor()'s own comments).
          gui.setPointingCursor(true);
          jumpToScrollbarY(sb->y);
          break;
        case fm::GraphicalDisplay::ScrollbarEventType::Motion:
          jumpToScrollbarY(sb->y);
          break;
        case fm::GraphicalDisplay::ScrollbarEventType::ButtonUp:
          dragging = false;
          gui.setPointingCursor(false);
          break;
      }
    }

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
        case fm::GraphicalDisplay::DragEventType::Motion: {
          // No-op when the content already fits entirely on screen (no
          // row can ever scroll either direction) -- a real report
          // (2026-08-09: "-g git/usilic起動でBasic端末でも一画面に収
          // まるためj/kは利かない。ところがマウスによる画面ドラッグ
          // はできてしまう") found j/k already correctly do nothing in
          // this case (ContentView::stepTopIndex()'s own maxTop==0
          // check refuses to move topIndex_ either direction), but
          // dragging still visibly shifted content by up to
          // (cellHeight-1) pixels: commitRowCrossings() pins
          // pixelOffset at that value specifically so a drag that hits
          // a REAL scroll boundary still visibly "pushes up against
          // the edge" for tactile feedback (see its own call site
          // comment) -- but with topIndex_ permanently stuck at 0
          // (nothing scrollable at all), EVERY drag motion hits that
          // same boundary immediately, so the intentional rubber-band
          // effect fired on every drag instead of only at a genuine
          // edge. Matches jumpToScrollbarY()'s own identical
          // scrollableLines<=0 guard just above.
          int totalLines = static_cast<int>(view.lines().size());
          int contentRows = std::max(gui.rows() - 1, 0);
          if (totalLines > contentRows) {
            pixelOffset += drag->deltaY;
            recentMotion.push_back({std::chrono::steady_clock::now(), drag->deltaY});
            while (!recentMotion.empty() &&
                   recentMotion.front().first < std::chrono::steady_clock::now() - kReleaseWindow)
              recentMotion.pop_front();
            commitRowCrossings(view, cellHeight, pixelOffset);
          }
          break;
        }
      }
    }

    if (coastLevel > 0 && !dragging) {
      const fm::ScrollStep &step = fm::kScrollSpeedLevels[coastLevel];
      auto frameDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
      // Move-THEN-delay, not delay-then-move (this used to be the
      // other way around) -- a real user report (2026-08-06): the
      // screen visibly "drops" speed right at the instant the mouse
      // is released, persisting even after -u was used to make every
      // level's own per-dot delay much faster, which pointed away
      // from "the chosen level is just slow" and toward the ORDERING
      // itself: the old shape ran performScrollStepDelay() BEFORE the
      // very first pixelOffset move of a freshly-started coast, so
      // there was a real, visible dead pause (up to that level's own
      // delay -- 90ms at the slowest default level) between the last
      // drag frame and the first coast frame, no matter how fast
      // subsequent dots were configured to be. Moving first means the
      // coast's first dot renders in the SAME frame the drag stopped,
      // with the per-step delay now only ever separating dot N from
      // dot N+1 -- never gating the first one -- which is the more
      // intuitive shape for a physics loop anyway (delay is what
      // happens BETWEEN steps, not before the first one).
      do {
        // Checked BEFORE moving, not just after (commitRowCrossings()'s
        // own check below) -- a real report (2026-08-10, screenshots,
        // "上への動きでは最上行の表示を終えた時点でスクロールバーは
        // 停止するのに画面は止まらず空行を表示したのち一行分戻って
        // 正規画面になる") found that waiting for pixelOffset to
        // accumulate a FULL cellHeight before discovering the boundary
        // (the only way commitRowCrossings() itself finds out) let
        // several frames render with topIndex_ already at 0 (or the
        // bottom equivalent) and a growing positive (or negative)
        // pixelOffset in between -- renderScrolled()'s own firstRow=
        // topIndex-1 reveals a row that doesn't exist there, drawn as
        // blank (see its own comment), while drawScrollbar()'s thumb
        // position is separately clamped to [0,maxThumbY] and so
        // stayed visually pinned at the edge the whole time -- exactly
        // the "scrollbar already stopped but the screen kept revealing
        // blank space" mismatch reported. Checking canStepTopIndex()
        // up front stops the coast the INSTANT topIndex_ is already at
        // its limit, before pixelOffset is ever allowed to move past 0
        // in that direction at all.
        //
        // `-coastDirection`, NOT `coastDirection` -- a real report
        // (2026-08-10, "スクロールは直ってない") caught that the first
        // version of this fix passed the wrong sign and so never
        // actually triggered. commitRowCrossings() just above shows
        // the real mapping: pixelOffset growing POSITIVE (i.e.
        // coastDirection == +1) is what eventually calls
        // stepTopIndex(-1) once it crosses a full cellHeight, and
        // pixelOffset growing NEGATIVE (coastDirection == -1) calls
        // stepTopIndex(+1) -- the direction actually about to be
        // stepped is the NEGATION of coastDirection, not coastDirection
        // itself. Passing coastDirection unflipped checked room in the
        // opposite direction from the one about to be taken, which is
        // almost always available, so the guard was a near no-op.
        if (!view.canStepTopIndex(-coastDirection)) {
          coastLevel = 0;
          coastDirection = 0;
          pixelOffset = 0;
          wasActivelyScrolling = true;  // see the identical assignment below for why
          break;
        }
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
          // A coast that starts AND hits this boundary within the
          // SAME frame (easy to trigger: each queued 's'/'w' repeat
          // the key-consumption loop already drained this frame
          // accelerates coastLevel before this physics block even
          // runs, and a high enough level can cross the whole
          // remaining distance to a boundary in one 16ms budget) never
          // takes the render-mode branch's "actively scrolling" arm
          // below at all -- topIndex_ (and, via stepTopIndex(),
          // cursorIndex_ too) moved, but wasActivelyScrolling was
          // never set true to say so, so the settled branch's own
          // "did we just finish scrolling" check below would
          // otherwise skip this frame's redraw() entirely, leaving
          // stale renderScrolled() output on screen at the boundary.
          wasActivelyScrolling = true;
          break;
        }
        // Delay AFTER moving, and only if there's actually a next dot
        // coming this frame -- no point paying the delay just to have
        // the do-while condition immediately end the loop anyway.
        performScrollStepDelay(step);
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
      // POLLHUP/POLLERR too -- see the CUI phase's own comment on why.
      if (poll(&fd, 1, 0) > 0 && (fd.revents & (POLLIN | POLLHUP | POLLERR))) {
        if (app.shell().onPtyReadable())
          anyEventThisFrame = true;
        else
          app.handleShellChildExited();
      }
    }

    // Kept current every iteration (cheap -- three int writes), not
    // just on frames that redraw() -- see updateScrollPosition()'s own
    // comment: flush() (redraw()'s eventual target) has no scroll-
    // position parameters of its own, so this is what lets it draw a
    // real, always-current thumb instead of the removed decorative
    // placeholder. Must run BEFORE the redraw() call below so flush()
    // sees this frame's values, not the previous one's.
    gui.updateScrollPosition(view.topIndex(), static_cast<int>(view.lines().size()), std::max(gui.rows() - 1, 0));

    if (dragging || coastLevel > 0 || pixelOffset != 0) {
      wasActivelyScrolling = true;
      std::vector<std::string> lines;
      lines.reserve(view.lines().size());
      for (const auto &line : view.lines()) lines.push_back(line.text);
      gui.renderScrolled(lines, view.topIndex(), pixelOffset, view.scrollModeLineSegments());
    } else if (wasActivelyScrolling || anyEventThisFrame) {
      // Settled on a row boundary -- back to the normal Display-
      // interface rendering path. redraw() calls clampViewport(),
      // which enforces the normal moveUp()/moveDown() invariant
      // "topIndex_ never leads cursorIndex_"; this used to need an
      // explicit cursorIndex_ = topIndex_ sync right here, since
      // renderScrolled() (used for every frame while actively
      // dragging/coasting) never touched cursorIndex_ at all, leaving
      // it stale from before the scroll started. commitRowCrossings()
      // now keeps cursorIndex_ continuously pinned to the coast's
      // leading edge via ContentView::stepTopIndex() (see its own
      // comment), so clampViewport() here is already a no-op by the
      // time this runs -- nothing left to sync.
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

  if (appearance && appearance->hasColors) gui->setColors(appearance->foreground, appearance->background);

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

  // -c/-g: temporary, hand-debugging convenience (see CLAUDE.md) --
  // force CUI or GUI startup regardless of the automatic "実は既に
  // GUI" terminal-appearance detection below (queryRealTerminalAppearance()).
  // Without this, testing CUI-only behavior by hand from a real
  // GUI-capable terminal (iTerm2/Terminal.app/GNOME Terminal) would
  // otherwise always auto-launch the GUI window, exactly the terminal
  // this flag exists to let a developer bypass. Not meant as a
  // permanent, documented user-facing flag (no usilicrc entry, no
  // mention in HELP) -- just scaffolding for hands-on verification
  // during this port's development.
  bool forceCui = false;
  bool forceGui = false;
  std::vector<char *> positionalArgs;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-c") == 0) forceCui = true;
    else if (std::strcmp(argv[i], "-g") == 0) forceGui = true;
    else if (std::strcmp(argv[i], "-u") == 0) {
      // User-tunable drag-scroll "inertia" speed, in usleep()
      // microseconds -- see fm::applyScrollSpeedOverride()'s own
      // comment for exactly which levels this reaches: levels
      // 1..N (N = however many values are given here, capped at
      // fm::kScrollSpeedLevels.size()-1 -- 10 levels total, since
      // index 0 is the inert "not coasting" placeholder), assigned in
      // order starting at the slowest, EACH forced to usleep-driven
      // timing even if it defaulted to an uncalibrated busy-loop
      // (levels 5+) -- e.g. "-u 90000 70000 50000 30000 10000" (5
      // values) makes level 5 precisely-timed too, not just 1..4; all
      // 10 levels can be given precisely-timed values right up to the
      // fastest. Consumes every following plain-digit argument,
      // stopping at the next flag or the end of argv -- "-u 90000"
      // overrides only the slowest level.
      std::vector<long> scrollValues;
      while (i + 1 < argc && *argv[i + 1] && std::all_of(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]),
                                                          [](unsigned char c) { return std::isdigit(c); })) {
        scrollValues.push_back(std::atol(argv[++i]));
      }
      if (scrollValues.empty()) {
        std::cerr << "usilic: -u requires one or more microsecond values (e.g. -u 90000 55000 30000 15000).\n";
        return 1;
      }
      int tunableLevels = static_cast<int>(fm::kScrollSpeedLevels.size()) - 1;
      if (static_cast<int>(scrollValues.size()) > tunableLevels) {
        std::cerr << "usilic: -u only tunes " << tunableLevels << " levels; the extra "
                   << (scrollValues.size() - tunableLevels) << " trailing value(s) will be ignored.\n";
      }
      fm::applyScrollSpeedOverride(scrollValues);
    } else positionalArgs.push_back(argv[i]);
  }
  if (forceCui && forceGui) {
    std::cerr << "usilic: -c and -g are mutually exclusive.\n";
    return 1;
  }

  // Ported from HELP.jp's own EXIT section (2.18): "引数無しで file
  // manager で起動した場合は...アクセス履歴のリストを~/.tcb/history
  // に保存し、次回の起動時にはこのファイルにより、履歴リストを復元
  // します" / "(ディレクトリ引数で起動した時...は、以上のファイルには
  // アクセスしません)" -- an argument-launched run neither reads NOR
  // writes ~/.usilic/history at all, matching legacy's "resume last
  // session" feature being exclusive to the no-argument case (see
  // startDirectory/consultHistoryOnStart's own comment below for the
  // narrower, already-fixed half of this: which directory ROW to land
  // on). Declared here (rather than where it's used below) so both the
  // startup load AND the exit-time save can share one flag -- a real
  // side-by-side screenshot comparison against legacy (2026-08-03)
  // found an argument-launched run's ^X^F/^X^D lists polluted with
  // unrelated entries from past unrelated sessions, because loading
  // was previously unconditional.
  bool consultHistoryOnStart = positionalArgs.empty();

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

  std::string historyText;  // stays empty (see hasSavedHistory below) when consultHistoryOnStart is false
  if (consultHistoryOnStart) {
    historyText = readFileIfExists(usilicDir + "history");
    if (!historyText.empty()) {
      history.loadHistoryText(historyText, configuration.charsetExplicitlySet());
      if (!history.isComplete()) {
        // Ported from load_list()'s own `.old` fallback: the primary
        // file looked truncated/corrupt (missing EOF sentinel).
        std::string oldText = readFileIfExists(usilicDir + "history.old");
        if (!oldText.empty()) history.loadHistoryText(oldText, configuration.charsetExplicitlySet());
      }
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

  fm::Application app(display, keyInput, history, configuration, encoding, display.columns(), shellOptions,
                       homeEnv ? std::string(homeEnv) : std::string());

  // Ported from str.c's set_help(): English (HELP) vs a genuine
  // Japanese translation (HELP.jp), chosen by charset the same way
  // legacy does. USILICHELP names the ASCII/English file directly, no
  // installed-package LIB-path convention for THIS PORT's own build
  // exists yet (same gap usilicrc's own LIB fallback has, see above).
  // When unset, fall back to /usr/local/lib/usilic/HELP -- the exact
  // path legacy's own config.sh (`LIB=$USR_DIR/lib/$PROGNAME`) already
  // installs to for a PROGNAME=usilic build (confirmed present via a
  // real user's own machine, 2026-08-02: this port's own project name
  // IS "usilic", so re-using that exact convention -- rather than
  // inventing a new one -- is the natural default, and happens to
  // already be populated wherever the legacy C side of this same
  // project was ever installed under that name). If unset AND that
  // fallback path doesn't exist either, or the file can't be read,
  // HELP (^J^H) is simply unavailable (Command::StartHelp fails
  // cleanly) rather than this being a hard startup error.
  std::string helpPath;
  if (const char *helpPathEnv = std::getenv("USILICHELP")) {
    helpPath = helpPathEnv;
  } else {
    helpPath = "/usr/local/lib/usilic/HELP";
  }
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

  // "実はすでにGUI" (docs/ideas/IDEAS.md, see CLAUDE.md's 2026-07-30
  // notes): if the calling terminal's own colors AND font size can be
  // matched (macOS Terminal.app or GNOME Terminal, via
  // queryRealTerminalAppearance()), swap the backend to a matching
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
  // Window-bounds match is now a HARD requirement alongside the color/
  // font appearance match, not an optional nicety (changed 2026-08-03,
  // "-gは隠れ機能なのでウィンドウの重ね打ち可を必須としたい"): this
  // launch path is an undocumented auto-detect that swaps the real
  // terminal out for a GUI window before the user ever asked for one,
  // so a GUI window that shows up in the wrong PLACE (e.g. centered,
  // unrelated to where the real terminal sits) is exactly the same
  // class of unrequested surprise as one that shows up with the wrong
  // COLORS -- the whole reason colors are gated on a successful query
  // instead of guessing. Currently only queryTerminalAppWindowBounds()/
  // queryITermWindowBounds() (macOS Terminal.app/iTerm2) can answer
  // this at all; Ptyxis's queryPtyxisAppearance() (Ubuntu) only ever
  // supplies font info, never bounds, so this requirement is *why*
  // "実はすでにGUI" naturally stays off on Ptyxis for now rather than
  // needing its own separate platform check -- see CLAUDE.md's
  // 2026-08-03 note on why matching a Linux terminal's own window
  // position was set aside (Wayland has no general cross-app window-
  // geometry query, and X11 support would need per-distro detection
  // work not yet done).
  auto appearance = forceCui ? std::nullopt : queryRealTerminalAppearance();
  auto bounds = (forceCui || !appearance) ? std::nullopt : queryRealTerminalWindowBounds();
  if (appearance && bounds) {
    std::string fontPath = resolveGuiFontPath(appearance);
    // 2026-08-11: a real report of FOUR separate rendering-quality
    // experiments (forced bold, TTF_HINTING_LIGHT, TTF_HINTING_NONE,
    // alpha-channel gamma boost) all landing as "変化無し" (no visible
    // change) for one specific real-terminal profile, despite each one
    // measurably changing SOMETHING about how narrow glyphs render, is
    // suspicious enough to doubt the premise that the intended font is
    // even being used at all -- resolveMacFontPath()'s own comment
    // documents that CoreText's name-based lookup can fail outright
    // (returning an empty string, or worse a bizarre unrelated
    // substitute) for exactly this kind of system font depending on
    // precisely which name string AppleScript reports, silently
    // falling through to resolveGuiFontPath()'s own generic Menlo/
    // Monaco candidate list with nothing on screen indicating it
    // happened. Dumped here (not inside GraphicalDisplay, which has no
    // visibility into `appearance` at all) so a real run can confirm
    // or rule this out directly, rather than continuing to guess at
    // rendering techniques against a font that might not be the one
    // actually loaded.
    if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
      FILE *f = std::fopen(dbg, "a");
      if (f) {
        std::fprintf(f, "---resolveGuiFontPath--- appearance.fontName=%s appearance.fontPointSize=%d resolvedPath=%s\n",
                     appearance->fontName.c_str(), appearance->fontPointSize, fontPath.c_str());
        std::fclose(f);
      }
    }
    launchGui = fontPath.empty() ? nullptr
                                  : fm::GraphicalDisplay::create(display.rows(), display.columns(), fontPath,
                                                                  appearance->fontPointSize);
    if (launchGui) {
      if (appearance->hasColors) launchGui->setColors(appearance->foreground, appearance->background);
      // Overlay this window exactly where the calling terminal's own
      // window sits -- see queryTerminalAppWindowBounds()'s own
      // comment. Deliberately left BORDERED (a real title bar, with
      // the traffic-light buttons): a real side-by-side screenshot
      // comparison (2026-08-03 follow-up, "操作部が無い") found a
      // borderless window looked visibly wrong next to a real
      // Terminal.app window with its own title bar. `bounds` is
      // guaranteed present here (see the hard requirement above), so
      // both calls below are unconditional.
      //
      // A previous version of this comment claimed "SDL2's window
      // position/size parameters already describe the CLIENT area
      // only... so leaving this window bordered needs no extra math
      // here at all" -- that first half was correct (confirmed
      // against SDL's own documented position/size semantics) but the
      // conclusion didn't follow: `bounds->y` (from AppleScript's
      // "bounds of window", queryTerminalAppWindowBounds()'s own
      // comment) is the window's OUTER frame top (title bar
      // included), a DIFFERENT coordinate than SDL's own CLIENT-area
      // top -- passing it straight through left this window's title
      // bar floating title-bar-height above where the real terminal's
      // title bar actually sits. setWindowPosition() itself now
      // corrects for this (see its own comment) using the real
      // terminal window's own OS-reported border size, so this call
      // site doesn't need to know the conversion. matchWindowSize()
      // separately forces an exact size match to the real terminal's
      // own measured pixels, deriving cellWidth/cellHeight directly
      // from that known size rather than trusting this class's own
      // font-metric-based computation to land on the same line
      // spacing a real terminal uses (a real report, 2026-08-08: the
      // needed correction varied by font size across 5 different
      // real terminal profiles in a way no font-metric heuristic
      // reliably captured -- see matchWindowSize()'s own comment).
      launchGui->setWindowPosition(bounds->x, bounds->y);
      launchGui->matchWindowSize(bounds->width, bounds->height);
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

  // Restores the SCROLL/LS settings saved by a previous session (see
  // usilicrc's own SCROLL/LS sections and Command::ToggleScrollMode/
  // ToggleLsMode's own comments on ^Oh/^Ol) -- history.
  // smoothScrollEnabled()/lsMode() were already parsed from/
  // serialized to the history file's TCB trailer line, but nothing
  // ever actually applied them anywhere until now (a real gap: the
  // hardware-scroll fast paths ^Oh gates were fully built and tested,
  // but every real run defaulted to false regardless of what a
  // previous session had saved).
  //
  // A history file existing at all (hasSavedHistory) is what decides
  // precedence here, the same way encoding's own history.savedCharset()
  // .value_or(configuration.defaultCharset()) pattern above does --
  // unlike savedCharset() though, smoothScrollEnabled()/lsMode() are
  // plain bool/int (not std::optional), so they can't themselves
  // distinguish "never saved" from "explicitly saved as off/0"; using
  // historyText's own emptiness as that signal instead means a truly
  // fresh install (no history file yet) correctly falls back to
  // usilicrc's own SCROLL/LS defaults instead of silently always
  // starting both off regardless of what the user configured.
  bool hasSavedHistory = !historyText.empty();
  display.setSmoothScrollEnabled(hasSavedHistory ? history.smoothScrollEnabled()
                                                  : configuration.smoothScrollEnabled());
  app.directoryMode().setFilenameOnlyMode(hasSavedHistory ? history.lsMode() != 0 : configuration.lsMode() != 0);

  // Resolved to an absolute path (textually only -- lexically_normal(),
  // no symlink resolution, matching PathResolver's own established
  // approach) before ever reaching DirectoryMode: a bare "." argument
  // (the common no-argument-equivalent case) would otherwise show up
  // verbatim as "." in the mode-line's directory-path field, unlike
  // the CUI's own always-absolute display (confirmed against a real
  // CUI screenshot). DirectoryMode::enterDirectory() itself does NOT
  // do this resolution -- it only normalizes the trailing slash -- so
  // relative-path callers elsewhere (tests, CP/MV/RM path prompts)
  // keep working with whatever they already pass.
  std::string startDirectory =
      std::filesystem::absolute(!positionalArgs.empty() ? positionalArgs[0] : ".").lexically_normal().string();
  // History-based cursor restoration (see DirectoryMode::enterDirectory()'s
  // own comment) only applies to an argument-LESS launch -- legacy's
  // own "resume last session" case. An explicit path argument
  // (including ".") always starts at the top of that directory's
  // listing, regardless of any position saved from an earlier,
  // unrelated visit (a real report: launching with "." landed on a
  // stale mid-list row instead of the top). consultHistoryOnStart
  // itself is declared earlier now (see its own comment) so the
  // startup load further up can share it too.
  if (!app.start(startDirectory, consultHistoryOnStart)) {
    display.close();
    keyInput.close();
    std::cerr << "usilic: cannot list " << startDirectory << "\n";
    return 1;
  }

  if (guiOwnsWholeSession) {
    runGuiPhase(app, *launchGui);
  } else {
    if (forceGui && !app.wantsExit()) {
      // -g: same GUI entry point ^Og already uses mid-session, just
      // invoked once up front instead of waiting for that keypress.
      // tryEnterGuiPhase() blocks until the GUI side exits (^Og again
      // or the window's close button), so falling through to the
      // ordinary terminal-first loop below afterward is correct either
      // way -- same as if the user had pressed ^Og once by hand.
      if (!tryEnterGuiPhase(app, display, *terminalDisplay)) {
        std::cerr << "usilic: -g requested but no usable GUI (no font found, or no display) "
                     "-- falling back to terminal mode.\n";
      }
    }
    while (!app.wantsExit()) {
      if (!runTerminalPhase(app, keyInput, display, forceCui)) break;
      if (app.wantsExit()) break;
      tryEnterGuiPhase(app, display, *terminalDisplay);  // ^Og fallback -- unmatched terminal or no display server
    }
  }

  display.close();
  keyInput.close();

  // Ported from save_history()'s own `.old`-then-overwrite dance:
  // back up the previous file before writing the new one.
  // app.encoding() rather than the local `encoding` variable computed
  // at startup: K-27 (^Oa) can change it live mid-session
  // (Application::cycleCharset()), and a switch should actually
  // persist into the next run, not be silently overwritten by
  // whatever this session started with. Gated on consultHistoryOnStart
  // (see its own comment) -- an argument-launched run doesn't touch
  // ~/.usilic/history on exit either, matching legacy exactly (not
  // just "load skipped, save unconditional"): saving would otherwise
  // silently overwrite the real no-argument session's own saved state
  // with this unrelated run's in-memory rings.
  if (consultHistoryOnStart) {
    history.setSavedCharset(app.encoding());  // so a future session with no explicit override resumes in this one's charset
    std::string historyPath = usilicDir + "history";
    std::string historyOldPath = usilicDir + "history.old";
    if (!readFileIfExists(historyPath).empty()) std::rename(historyPath.c_str(), historyOldPath.c_str());
    std::ofstream out(historyPath, std::ios::binary);
    out << history.serialize("usilic", currentTimestampText());
  }

  return 0;
}
