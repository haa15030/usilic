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
// Deliberately minimal, not a literal port of usilic.c's main()/
// init_menu()/menu():
//   - usilicrc/history loading IS wired (this is what closes the last
//     of the three follow-ups this file's own history left open, see
//     CLAUDE.md): Configuration::loadRcText() and
//     History::loadHistoryText() take already-read text rather than
//     doing file I/O themselves (by design, see their own class
//     comments) -- this file does that reading, following
//     set_usilicrc()/the S_hist path-building in legacy's str.c as
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
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#ifndef _WIN32  // Windowsのネイティブ(.steering/20261015-windows-native-gui/)に無いもの
#include <poll.h>
#endif
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#endif
#include <utility>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "application.hpp"
#include "help_text.hpp"
#include "child_process.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "delegating_display.hpp"
#include "drag_scroll_speed.hpp"
#include "gnome_terminal_appearance.hpp"
#include "epub_vertical.hpp"
#include "graphical_display.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "mac_font_resolver.hpp"
#include "help_to_epub.hpp"
#include "markup_to_roff.hpp"
#include "mouse_sgr.hpp"
#include "usilicrc_file.hpp"
#include "terminal_display.hpp"
#include "posix_compat.hpp"
#include "platform.hpp"
#ifdef _WIN32
// Windowsのネイティブでは端末の段の関数の本体を外すので、引数が使われなくなる。
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

namespace {

std::string readFileIfExists(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// 2026-09-20追加、直接のユーザー依頼("history競合の件、1〜4の対策
// (read-merge-write+lock)...で実装よろしく"): ~/.usilic/history.lock
// を使った、複数usilicプロセス間の簡易排他制御。O_CREAT|O_EXCLは
// 「ファイルが既に存在すれば失敗する」ことをOSが保証する唯一の
// アトミックな生成手段なので、これで作成できた側だけがロックを
// 得る(既存の~/.usilic/pid自身の「存在確認してから書く」という
// 2段階のチェックとは違い、TOCTOUの隙が無い)。
bool tryCreateHistoryLock(const std::string &lockPath) {
  int fd = ::open(lockPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
  if (fd < 0) return false;
  std::string pidText = std::to_string(::getpid()) + "\n";
  ::write(fd, pidText.c_str(), pidText.size());
  ::close(fd);
  return true;
}

// ロック保持者の生死確認(kill(pid,0)一発、~/.usilic/pid自身のチェック
// と同じ発想――サスペンド中でも真)込みで、最大で約5秒待つ。保持者が
// kill -9等で死んだまま取り残されたロックは、生死確認がfalseになった
// 時点で削除して奪い直す。5秒待っても解放されない(生きたまま応答が
// 無い)場合も、保存自体を諦めるより奪って進む方が実害が小さい
// (最悪でも従来通りの「稀に上書きが起きる」状態に戻るだけで、保存
// そのものが永久に止まるよりはましなため)。
void acquireHistoryLock(const std::string &lockPath) {
  for (int attempt = 0; attempt < 100; attempt++) {
    if (tryCreateHistoryLock(lockPath)) return;
    long lockPid = 0;
    {
      std::ifstream in(lockPath);
      if (in) in >> lockPid;
    }
    bool holderAlive = lockPid > 0 && lockPid != static_cast<long>(::getpid()) &&
                        ::kill(static_cast<pid_t>(lockPid), 0) == 0;
    if (!holderAlive) {
      std::error_code ec;
      std::filesystem::remove(lockPath, ec);
      continue;  // 次のループでtryCreateHistoryLock()を再試行
    }
    ::usleep(50000);  // 50ms待って再試行
  }
  std::error_code ec;
  std::filesystem::remove(lockPath, ec);
  tryCreateHistoryLock(lockPath);
}

void releaseHistoryLock(const std::string &lockPath) {
  std::error_code ec;
  std::filesystem::remove(lockPath, ec);
}

// Ported from sub.c's save_time(): a plain asctime()-style timestamp
// with the trailing newline asctime() always appends stripped off.
std::string currentTimestampText() {
  std::time_t t = std::time(nullptr);
  std::string text = std::asctime(std::localtime(&t));
  if (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

// 2026-09-20追加、直接のユーザー依頼("history競合の件、1〜4の対策
// (read-merge-write+lock)&書き込み手順を「一時ファイルに書いてから
// rename」で実装よろしく"): ~/.usilic/historyへの実際の保存処理
// (排他ロック+読み直し+このセッション自身の変更だけの再生+
// 一時ファイル→rename)を1箇所に括り出したもの――終了時の保存と、
// pushRecent()毎の即時保存(History::setOnRingPushed()自身のコメント
// 参照)の両方から呼ぶ。sessionHistoryは呼び出し側が最後にpushした
// 状態のまま(このセッション自身のjournal_を保持している)必要がある
// ので、値渡しではなく参照で受け取る。
void saveHistoryToDisk(fm::History &sessionHistory, const std::string &usilicDir,
                        bool charsetExplicitlySet) {
  std::string historyPath = usilicDir + "history";
  std::string historyOldPath = usilicDir + "history.old";
  std::string historyLockPath = usilicDir + "history.lock";
  acquireHistoryLock(historyLockPath);

  // 保持数はこのセッションと同じ(usilicrcのHISTORY)――既定の200のままだと、
  // 保存のたびにファイルが200件へ切り詰められてしまう。
  fm::History diskHistory(sessionHistory.retentionCount());
  std::string currentDiskText = readFileIfExists(historyPath);
  if (!currentDiskText.empty()) {
    diskHistory.loadHistoryText(currentDiskText, charsetExplicitlySet);
    if (!diskHistory.isComplete()) {
      std::string diskOldText = readFileIfExists(historyOldPath);
      if (!diskOldText.empty()) diskHistory.loadHistoryText(diskOldText, charsetExplicitlySet);
    }
  }
  sessionHistory.replayJournalOnto(diskHistory);

  std::string historyTmpPath = historyPath + ".tmp." + std::to_string(::getpid());
  {
    std::ofstream out(historyTmpPath, std::ios::binary);
    out << diskHistory.serialize("usilic", currentTimestampText());
  }
  // fm::replaceFile(): std::rename()はWindowsでは置き換え先があると失敗し、historyが最初の1回の後ずっと
  // 更新されなかった(2026-10-16、-gの窓の大きさが保存されていなかった件)。
  if (!currentDiskText.empty()) fm::replaceFile(historyPath, historyOldPath);
  fm::replaceFile(historyTmpPath, historyPath);
  sessionHistory.clearJournal();  // 反映した分は次の保存で重ね直さない(History::clearJournal()参照)

  releaseHistoryLock(historyLockPath);
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
// 2026-10-11、直接のユーザー依頼(^J^Fで別のフォントを選んだ後、端末のフォント(Arial...)に戻したい): 起動した
// 端末(Terminal.app/iTerm2)のプロファイルのフォントのファイル(分からなければ空)。^J^Fの一覧の先頭に出す。
std::string gTerminalFontPath;

// 2026-10-20、直接のユーザー依頼(-gの既定はipagを優先、最初の起動の案内にfont-ipafontも): 入っているIPAゴシック
// (ipag.ttc・ipag.ttf、和文も等幅)のファイル。よくある場所に無ければfc-listで探す。無ければ空。
// 見つかった時だけ覚える(2026-10-20、直接のユーザー報告"何度やっても=kz.pngの繰り返し": 無いことを覚えていたので、
// 案内のbrew installで入れた後も、offerGroffInstallOnce()の確かめ直しがまだ無いとしてfont-ipafontを置き直していた)。
// よくある場所だけを見る(fc-listを使わない、インストールの途中で繰り返し確かめる用、runGuiPhase())。
std::string ipaGothicFontFile() {
  const char *home = std::getenv("HOME");
  for (const char *name : {"ipag.ttc", "ipag.ttf"}) {
    for (const std::string &dir :
         {std::string(home ? home : "") + "/Library/Fonts/", std::string("/Library/Fonts/"),
          std::string("/usr/share/fonts/opentype/ipafont-gothic/"),
          std::string("/usr/share/fonts/truetype/ipafont-gothic/")}) {
      std::string path = dir + name;
      std::ifstream probe(path, std::ios::binary);
      if (probe.good()) return path;
    }
  }
  return {};
}

std::string ipaGothicFontPath() {
  static std::string cached;
  if (!cached.empty()) return cached;
  cached = [] {
    if (std::string file = ipaGothicFontFile(); !file.empty()) return file;
    fm::ChildProcess::Result result = fm::ChildProcess::run({"fc-list", "-f", "%{file}\n", ":family=IPAGothic"});
    std::string path = result.ok ? result.output.substr(0, result.output.find('\n')) : "";
    std::ifstream probe(path, std::ios::binary);
    return !path.empty() && probe.good() ? path : std::string();
  }();
  return cached;
}

std::string resolveGuiFontPath(std::optional<TerminalAppearance> appearance = std::nullopt,
                                const std::string &explicitFontPath = {}) {
  if (const char *env = std::getenv("USILICFONT")) return env;
  // 2026-08-30、直接のユーザー依頼: "-f <path>"(下のオプション解析
  // 参照)で明示指定され、History::GuiPreferences.fontPathとして
  // セッションをまたいで記憶されているフォントパス。Finderからの
  // ダブルクリック起動は引数もUSILICFONT環境変数も渡せないため、
  // これが唯一の「白アイコン起動でもフォントを選べる」手段になる。
  // 自動検出されたappearanceより先にチェックする――ユーザーが明示的
  // に選んだ以上、実端末の自動マッチより優先されるべきため。
  if (!explicitFontPath.empty()) return explicitFontPath;
  if (appearance && !appearance->fontName.empty()) {
    std::string resolved = fm::resolveMacFontPath(appearance->fontName, appearance->fontPointSize);
    if (!resolved.empty()) return resolved;
    resolved = fm::resolveLinuxFontPath(appearance->fontName);
    if (!resolved.empty()) return resolved;
  }
  // 2026-10-20、直接のユーザー依頼("1080pのminiで見るとSFNSMonoは日本語の字間の広さが強調されて見える"): IPAゴシック
  // (半角がちょうど全角の半分で詰まって見える)が入っていれば、SF Monoより先に(brew install --cask font-ipafont、
  // offerGroffInstallOnce()が最初の起動で案内する)。
  if (std::string ipag = ipaGothicFontPath(); !ipag.empty()) return ipag;
  static const char *const candidates[] = {
      // 2026-10-20、直接のユーザー判断("mpiのSFNSMonoが既定でいい"、字間は^[{ / ^[}で詰められるので): SF Monoを先に
      // (同日にいったんIPAゴシックを先にしたのをやめた)。
      "/System/Library/Fonts/SFNSMono.ttf",
      "/System/Library/Fonts/Menlo.ttc",
      "/System/Library/Fonts/Monaco.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
      // Windowsのネイティブ(2026-10-15、.steering/20261015-windows-native-gui/): 和文も等幅のMSゴシック、無ければConsolas。
      "C:/Windows/Fonts/msgothic.ttc",
      "C:/Windows/Fonts/consola.ttf",
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

// 2026-10-15: ドラッグで始めた慣性も、矢印キーと同じ最速の段(maxLevel)まで。maxLevelはCUIではusilicrcのARROW
// (fm::arrowSpeedMaxLevel())、-gではSPEED(guiSpeedMaxLevel()、2026-10-16)。
int resolveCoastLevel(int computedLevel, int maxLevel = fm::arrowSpeedMaxLevel()) {
  return std::min(gScrollDebugLevel.value_or(computedLevel), maxLevel);
}

// 2026-10-16、直接のユーザー依頼("-g起動では明らかにARROWの設定で動いている。-gではARROWは不要でよろしく"):
// ARROWはCUIの行単位のスクロールに合わせた値(HELPの「補足 usilicrc」のARROW)なので、-gの慣性(矢印キー・
// ドラッグ、縦書きの左右も)はSPEEDの段(fm::kScrollSpeedLevels、-sで変わる)で動かす。
int guiSpeedMaxLevel() { return static_cast<int>(fm::kScrollSpeedLevels.size()) - 1; }
const fm::ScrollStep &guiSpeedStep(int level) {
  return fm::kScrollSpeedLevels[static_cast<std::size_t>(std::clamp(level, 0, guiSpeedMaxLevel()))];
}

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
#ifdef _WIN32  // .steering/20261015-windows-native-gui/
  return "";
#else
  const char *path = ttyname(STDIN_FILENO);
  return path ? path : "";
#endif
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
    case fm::Display::Key::ShiftUp:
      return fm::KeyInput::SpecialKey::ShiftUp;
    case fm::Display::Key::ShiftDown:
      return fm::KeyInput::SpecialKey::ShiftDown;
    case fm::Display::Key::ShiftLeft:
      return fm::KeyInput::SpecialKey::ShiftLeft;
    case fm::Display::Key::ShiftRight:
      return fm::KeyInput::SpecialKey::ShiftRight;
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

// ^[が続いた回数(^[^[ ^Mで即終了)。2026-10-21、直接のユーザー依頼("^[^[が予期せず入ったときは再度^[でも取り消し可能に"):
// 3回目の^[は終了の確認を取り消す(Application::feedPromptByteInner())ので、数え直す(続けて^Mで終わらないように)。
int nextConsecutiveEscapes(int count) { return count >= 2 ? 0 : count + 1; }

// 2026-10-02追加、直接のユーザー依頼("←→で開始でいい。いずれのキーも速度減少時、最低速を超えたら
// 逆向き最低速、以後は加速"): 慣性スクロールを始める/速さを変えるキー(←→、縦書きでは↓↑)の1回分。
// 止まっていれば`pressed`の向きに最低速で始め、同じ向きなら1段速く、逆向きなら1段遅く、最低速から
// さらに逆を押すと逆向きの最低速(以後同じキーで加速)。
// maxLevelは最速の段(CUIはfm::arrowSpeedMaxLevel()、usilicrcのARROWの数とずらし方による。-gはguiSpeedMaxLevel())。
void stepCoastByKey(int &level, int &direction, int pressed,
                    int maxLevel = static_cast<int>(fm::kScrollSpeedLevels.size()) - 1) {
  if (level == 0) {
    level = resolveCoastLevel(1, maxLevel);
    direction = pressed;
  } else if (pressed == direction) {
    level = resolveCoastLevel(std::min(level + 1, maxLevel), maxLevel);
  } else if (level > 1) {
    level = resolveCoastLevel(level - 1, maxLevel);
  } else {
    direction = pressed;
  }
}

// 2026-10-02追加、直接のユーザー依頼("ホイールを本来の機能（画面スクロール）に戻してほしい"):
// ホイールの刻みを、j/kと同じCommand::ScrollLineDown/Upで`lines`行スクロールする。端ではベルを
// 鳴らさずに何もしない(ホイールは続けて回されるので)。directionは+1で先へ。
void scrollByWheel(fm::Application &app, fm::Display &display, int direction, int lines) {
  if (app.wheelReversed()) direction = -direction;  // ^Od(Command::ToggleWheelDirection)
  if (app.epubVerticalScrollActive()) {  // 縦書きのEPUB: 1列(章の一覧は1章)ずつ(-gのホイールと同じ)
    for (int i = 0; i < lines; i++)
      if (!app.scrollEpubVertical(direction)) break;
    return;
  }
  fm::ContentView &view = app.activeContentView();
  for (int i = 0; i < lines; i++) {
    if (direction < 0) {
      if (view.topIndex() <= 0) break;
      app.handleCommand(fm::Command::ScrollLineUp);
    } else {
      if (view.topIndex() >= view.maxTopIndex()) app.preloadPagerAhead(view.topIndex() + display.rows() * 2);
      if (view.topIndex() >= view.maxTopIndex()) break;
      app.handleCommand(fm::Command::ScrollLineDown);
    }
  }
}

// 2026-10-02追加: readByte()がESCを返した直後に呼ぶ。素の矢印("\x1b[A"〜"\x1b[D"、"\x1bOA"〜
// "\x1bOD")なら最後の字('A'=↑、'B'=↓、'C'=→、'D'=←)を返す。それ以外、または`accept`に無い
// 矢印なら読んだ分を戻して0(peekMouseSgr()と同じ)。
int peekArrow(fm::KeyInput &keyInput, const char *accept) {
  std::string consumed;
  auto next = [&]() -> int {
    int b = keyInput.readByte();
    if (b != 0) consumed.push_back(static_cast<char>(b));
    return b;
  };
  int first = next();
  if (first == '[' || first == 'O') {
    int final = next();
    if (final != 0 && std::strchr(accept, final)) return final;
  }
  if (!consumed.empty()) keyInput.playRawMacro(consumed);
  return 0;
}

// Call immediately after readByte() returned ESC. Recognizes an SGR extended mouse report
// ("\x1b[<Cb;Cx;Cy(M|m)", only sent while runTerminalCoast() has
// enabled mouse tracking -- see setTerminalMouseTracking()) and hands
// the decoded parameter string to fm::parseMouseSgr(). A malformed or
// non-mouse sequence is queued back (playRawMacro()).
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

// 2026-10-13、直接のユーザー報告("-c起動後HELPを参照しているusilic端末にREADME.mdをドラッグしても表示されない"):
// 端末(Terminal.app・iTerm2等)へのファイルのドロップは、パスの文字列の貼り付けとして届く。括弧付き貼り付け
// (ESC [200~ ... ESC [201~)を有効にしておき(setTerminalMouseTracking())、ESCの後がそれなら中身を返す
// (違えば読んだ分を戻してnullopt)。
std::optional<std::string> peekBracketedPaste(fm::KeyInput &keyInput) {
  static const std::string kStart = "[200~", kEnd = "\x1b[201~";
  std::string consumed;
  for (char expected : kStart) {
    int b = keyInput.readByte();
    if (b != 0) consumed.push_back(static_cast<char>(b));
    if (b != static_cast<unsigned char>(expected)) {
      if (!consumed.empty()) keyInput.playRawMacro(consumed);
      return std::nullopt;
    }
  }
  std::string text;
  int idle = 0;
  while (idle < 30) {  // 1回0.1秒(VTIME)、3秒届かなければ打ち切る
    int b = keyInput.readByte();
    if (b == 0) {
      idle++;
      continue;
    }
    idle = 0;
    text.push_back(static_cast<char>(b == 0x80 ? 0 : b));
    if (text.size() >= kEnd.size() && text.compare(text.size() - kEnd.size(), kEnd.size(), kEnd) == 0) {
      text.resize(text.size() - kEnd.size());
      break;
    }
  }
  return text;
}

// ドロップされたパスの並び(`/a/b\ c.md /d/e.txt`、'...'・"..."で囲まれたものも)を分ける。どれかが実在しなければ空。
std::vector<std::string> droppedPathsFromPaste(const std::string &text) {
  std::vector<std::string> words;
  std::string word;
  bool inWord = false;
  char quote = 0;
  for (std::size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (quote) {
      if (c == quote) quote = 0;
      else word.push_back(c);
      continue;
    }
    if (c == '\\' && i + 1 < text.size()) {
      word.push_back(text[++i]);
      inWord = true;
    } else if (c == '\'' || c == '"') {
      quote = c;
      inWord = true;
    } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (inWord) words.push_back(word);
      word.clear();
      inWord = false;
    } else {
      word.push_back(c);
      inWord = true;
    }
  }
  if (inWord) words.push_back(word);
  for (auto &w : words) {
    if (w.rfind("~/", 0) == 0)
      if (const char *home = std::getenv("HOME")) w = std::string(home) + w.substr(1);
    std::error_code ec;
    if (w.empty() || w[0] != '/' || !std::filesystem::exists(w, ec)) return {};
  }
  return words;
}

void setTerminalMouseTracking(fm::Display &display, bool enabled) {
  // \x1b[?1003h/l: report ALL motion (not just while a button is
  // held), so the coast loop can highlight whatever row the pointer
  // is over even without a button down. \x1b[?1006h/l: SGR extended
  // coordinates -- no 223-row/column ceiling, and unambiguous to parse
  // (see mouse_sgr.hpp).
  // 2026-10-13: 括弧付き貼り付け(?2004、ファイルのドロップ用、peekBracketedPaste()参照)も一緒に切り替える。
  display.write(enabled ? "\x1b[?1003h\x1b[?1006h\x1b[?2004h" : "\x1b[?1003l\x1b[?1006l\x1b[?2004l");
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

// 2026-10-02追加、直接のユーザー依頼(=hw、-gのrunGuiPhase()のclickTargetsCursorと同じ): 慣性を
// 止めるだけのクリックの後、マウスを動かす・キー・ホイールのどれかが来るまでは、次の左クリックを
// ポインタの行ではなくハイライト行(cursorIndex_)へのクリックとして扱う。runCoastLoop()が立て、
// runTerminalPhase()が下ろし、runTerminalMouseDrag()のクリックが使う。
bool gTerminalClickTargetsCursor = false;

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

// 2026-10-15、直接のユーザー報告("-cで慣性スクロールを中断すると^P/^Nがk/jになる。ブロックカーソルも行方不明"):
// 慣性・ドラッグ・クリックの後(runCoastLoop()・runTerminalMouseDrag()から戻った所)は、ハイライト行の無い画面
// (FILE等)の端末のカーソルを、今の行(慣性が止めた先端の行)へ置き直す。redraw()だけではカーソルはモードラインの
// 端に残り、今の行が見えないので、端の行での^N/^P(画面が1行動く)がj/kに見えていた。プロンプトの中では触らない。
void syncTerminalCursorAfterMouse(fm::Application &app, fm::Display &display) {
  if (app.promptActive()) return;
  app.activeContentView().syncVisibleCursor();
  display.flush();
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
// Shared by both real-terminal triggers: Left/Right (2026-10-02,
// replacing Shift+Up/Down -- see runTerminalPhase()'s peekArrow()
// call, which calls this at level 1) and a mouse-drag
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
// than Left/Right (see stepCoastByKey() for how those change speed
// and direction) -- that
// key is handed back to the caller to feed through normally, exactly
// as if this function had never run.
TerminalCoastResult runCoastLoop(fm::Application &app, fm::Display &display, fm::KeyInput &keyInput,
                                  int initialLevel, int initialDirection, bool fromDrag) {
  // 2026-10-02追加、直接のユーザー報告("-cの縦書きEPUBでは↑↓で横スクロールが始まらず、←→で行移動
  // しない。-gは問題無し"): 縦書きのEPUBの本文では、-gの左右の慣性(runGuiPhase()のepubCoastLevel)
  // と同じく1列ずつ(Application::scrollEpubVertical()、+1で先へ)送り、速さは↓↑で変える。-gと
  // 同じく、横書きの範囲(奥付等)へ入ったら同じ段階の上下の慣性として続ける。
  bool columns = app.epubVerticalDragActive();
  fm::ContentView *viewPtr = &app.activeContentView();
  // マウスで始めたら段を2つ速い方へずらして始める(fm::kDragArrowSpeedOffset参照)。2026-10-09から縦書きの
  // 左右も同じ(直接のユーザー依頼"慣性スクロール機能を縦書きへも反映")。
  int level = std::min(initialLevel + (fromDrag ? fm::dragArrowSpeedOffset() : 0), fm::arrowSpeedMaxLevel());
  int direction = initialDirection;
  TerminalCoastResult result;
  gTerminalClickTargetsCursor = false;
  // 2026-10-02変更、直接のユーザー報告("-cの慣性スクロールでは...ハイライト行が最上行にある"の
  // 修正後も"ハイライト表示もスクロール方向の先端行にならない"): 以前は慣性中もマウスの行を
  // ホバーでハイライトしていた(ホバーはcursorIndex_より優先)ため、実機ではポインタの行が
  // ハイライトされ、先端行のcursorIndex_が見えなかった。-gと同じく、慣性中はホバーしない。
  viewPtr->setHoverIndex(std::nullopt);

  for (;;) {
    fm::ContentView &view = *viewPtr;
    // 2026-10-01、直接のユーザー依頼("-uと-iのパラメータがGUI向きなのか、動きが遅く感じる...
    // CUIだけ倍率上げてみて"): 1行あたりの遅延を16ドット分ではなく8ドット分にする(-gの約2倍の
    // 「1秒あたりの行数」)。
    constexpr int kTerminalDotsPerRow = 8;
    // 矢印キー用の表(usilicrcのARROW、fm::arrowSpeedStep())。2026-10-09から縦書きの左右も(以前はSPEED)。
    fm::ScrollStep step = fm::scaledForRow(fm::arrowSpeedStep(level), kTerminalDotsPerRow);
    // 2026-10-02変更、直接のユーザー報告("-cではそもそも左クリックでスクロールが止まらない"、疑似
    // 端末では止まるのに実機では止まらない): 以前は1行進むごとに入力を1件しか読まず、待ちは
    // usleep()だった。実機のTerminal.appはマウスを動かすたびに移動の報告(?1003)を送るので、最低速
    // (1行約0.7秒)では報告が溜まる一方になり、クリックがその後ろで待たされていた。待ちの間ずっと
    // poll()し、届いた入力はその場で全部処理する。
    auto deadline = std::chrono::steady_clock::now();
    if (step.busyLoop) performScrollStepDelay(step);
    else deadline += std::chrono::microseconds(step.microseconds);
    for (;;) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      int timeoutMs = remaining.count() > 0 ? static_cast<int>(remaining.count()) : 0;
      struct pollfd fd{STDIN_FILENO, POLLIN, 0};
      if (poll(&fd, 1, timeoutMs) <= 0 || !(fd.revents & POLLIN)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        continue;
      }
      int byte = keyInput.readByte();
      if (!byte) continue;  // spurious wakeup/timeout artifact
      if (byte != 0x1b) {
        clearHoverAndRedraw(view);
        result.hadLeftoverByte = true;
        result.leftoverByte = byte;
        return result;
      }
      auto mouse = peekMouseSgr(keyInput);
      if (mouse) {
        if (mouse->wheel != 0) {  // ホイール: 慣性を止めて画面スクロール(scrollByWheel()参照)
          clearHoverAndRedraw(view);
          scrollByWheel(app, display, mouse->wheel, 1);
          return result;
        }
        if (mouse->pressed && !mouse->motionOnly && mouse->button == 2) {  // 右クリック: 慣性を止めて^G
          clearHoverAndRedraw(view);
          app.feedCanonicalByte(0x07);
          return result;
        }
        if (mouse->pressed && !mouse->motionOnly && columns) {  // 縦書き: 止めるだけ
          view.redraw();
          return result;
        }
        if (mouse->pressed && !mouse->motionOnly) {
          // 左クリック: どこを押しても止める。-gのrunGuiPhase()のstopOnly(2026-09-25)・
          // clickInterruptedCoastと同じく、押した行のハイライト範囲の中なら、慣性中ずっと
          // 見えていたハイライト行(cursorIndex_)を開き、範囲外なら止めるだけ(その後、マウスを
          // 動かさずにクリックすればハイライト行を開く、gTerminalClickTargetsCursor参照)。
          int lineIndex = view.lineIndexAtRow(mouse->row);
          bool open = lineIndex >= 0 && view.isColumnWithinLineScope(lineIndex, mouse->column);
          clearHoverAndRedraw(view);
          if (open) {
            int cursorRow = view.cursorDisplayRow();
            if (cursorRow >= 0) app.handleMouseClick(cursorRow, 0);
          } else {
            gTerminalClickTargetsCursor = true;
          }
          return result;
        }
        continue;  // 移動・離す: 何もしない(慣性中はホバーしない)
      }
      // 2026-10-02追加、直接のユーザー依頼: ←→(縦書きでは↓↑)で速度/向きを変える(stepCoastByKey()参照)。
      // 2026-10-06: 慣性中は進む向きの矢印(一覧の↓↑、縦書きの←→)でも(runGuiPhase()の同じ日の追加参照)。
      if (int arrow = peekArrow(keyInput, "ABCD")) {
        // 2026-10-13: EPUBの横書き(奥付等)でも←が先へ(Application::leftArrowAdvances()参照)。
        // 2026-10-21、直接のユーザー報告("横書きの自動スクロール速度調整キーが逆(上向きスクロールなら↑で加速、が自然)"):
        // 横書きの↑↓は字の動く向き(先へ進む時は字が上へ動くので↑)。以前は↓が先へだった。縦書き(columns)はそのまま。
        bool forward = columns                    ? (arrow == 'B' || arrow == 'D')
                       : app.leftArrowAdvances() ? (arrow == 'D' || arrow == 'A')
                                                  : (arrow == 'C' || arrow == 'A');
        stepCoastByKey(level, direction, forward ? 1 : -1, fm::arrowSpeedMaxLevel());
        continue;
      }
      // Neither a Left/Right nor a mouse report -- some other ESC
      // sequence (an ordinary arrow key, ^[s, etc). Nothing beyond the
      // ESC byte itself has been consumed (both peeks above queue
      // their own bytes back on mismatch), so requeue ESC too and let
      // the normal terminal loop re-read and handle it exactly as it
      // always does.
      clearHoverAndRedraw(view);
      keyInput.playRawMacro("\x1b");
      return result;
    }

    if (columns) {
      if (!app.scrollEpubVertical(direction)) return result;  // 本の端
      if (!app.epubVerticalDragActive()) {  // 横書きの範囲へ入った: 上下の慣性として続ける
        columns = false;
        viewPtr = &app.activeContentView();
      }
      continue;
    }
    int before = view.topIndex();
    view.setTopIndex(before + direction);
    if (view.topIndex() == before) {
      // See Application::preloadPagerAhead()'s own declaration comment
      // (2026-09-06, a direct user report about the modeline's line-
      // count/percentage flickering, then "HISTORY.md:15036が復活した"
      // once a naive "always preload every tick" version of this fix
      // reintroduced the SAME symptom it had just fixed): calling
      // preloadPagerAhead() unconditionally on every single iteration
      // of this loop -- even the many iterations where setTopIndex()
      // was ALREADY going to succeed using content loaded on a
      // previous iteration -- meant a single slow coast gesture spent
      // dozens of loop iterations each independently fetching one more
      // paced line, even though the viewport itself only advanced by a
      // handful of rows: exactly the "①→②の間に総行数が25→50" growth
      // reported, now traced to a mismatch between "how many times
      // this loop ticks" and "how many times the viewport actually
      // moves." Fetching lazily -- try the move first with whatever is
      // already loaded, and only reach for more once that attempt
      // genuinely comes up short -- keeps this loop's fetch count in
      // 1:1 correspondence with actual topIndex_ advances, matching
      // Command::ScrollLineDown's own per-keypress cadence exactly.
      app.preloadPagerAhead(before + display.rows() * 2);
      app.suppressPagerRepoll(view.topIndex());
      view.setTopIndex(before + direction);
    }
    if (view.topIndex() == before) {  // still a real boundary even after trying to load more
      clearHoverAndRedraw(view);
      return result;
    }
    app.suppressPagerRepoll(view.topIndex());
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
    // 2026-10-02修正、直接のユーザー報告("-cの慣性スクロールでは下向きのスクロールでハイライト行が
    // 最上行にある（上へのスクロールなら正解）"): -gのContentView::stepTopIndex()と同じく、先へ
    // 進む時は画面の最下行に。
    view.setCursorIndex(direction > 0 ? std::min(view.topIndex() + view.height() - 1,
                                                 static_cast<int>(view.lines().size()) - 1)
                                      : view.topIndex());
    // 2026-10-10、直接のユーザー報告("奥付から上方向のスクロールを始めると、行頭で横方向にならずにそのまま
    // 「下へ」と消えていき、何も表示されないまま行数が001/382になって止まる"): 縦書きの本の横書きの範囲
    // (奥付等)の外へ出たら縦書きへ戻し(-gのrunGuiPhase()と同じくApplication::syncEpubHorizontalSection())、
    // 同じ段階・向きの列の慣性として続ける(上へ=前へは列も前へ)。以前は範囲の外の行へそのまま進んでいた。
    if (app.syncEpubHorizontalSection()) {
      if (!app.epubVerticalDragActive()) return result;
      columns = true;
      viewPtr = &app.activeContentView();
      app.activeContentView().redraw();
      continue;
    }
    view.redraw();
    // 2026-10-15、直接のユーザー報告("-c起動では選択行の無いベタファイル表示の慣性スクロール時にブロックカーソルが
    // いなくなる"): ハイライト行の無い画面(FILE等)は、端末のカーソルを先端行(上のcursorIndex_)へ置き直す。
    // redraw()だけでは、カーソルは最後に描いた所(モードライン等)に残る。
    view.syncVisibleCursor();
    display.flush();
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
// fm::kScrollSpeedLevels level via fm::levelForTerminalReleaseDistance()
// (its own fixed row-based table, not DRAG's dot thresholds), then hands off into runCoastLoop() to keep going at that
// speed -- exactly the mouse-drag equivalent of what Left/Right
// does via peekArrow(). A release with no meaningful motion (a
// plain click) isn't a drag at all: it's handled exactly like GUI
// mode's click-an-already-selected-row-opens-it, via
// Application::handleMouseClick().
TerminalCoastResult runTerminalMouseDrag(fm::Application &app, fm::Display &display, fm::KeyInput &keyInput,
                                          int startRow, int startColumn) {
  // 2026-09-08追加、直接のユーザー要望(syncMouseTracking()自身の同日
  // コメント参照): COPYモード中の左ドラッグは、この関数の残り(コースト
  // /スクロール物理、ホバー先読み等、すべてリスト系モード専用)を
  // 一切経由させず、GraphicalDisplay側のDragEventハンドリング
  // (runGuiPhase()自身の"COPYモードのスナップショットは元々静的な
  // 内容で...同じ左ドラッグのジェスチャーを、この経路の「前」で完全に
  // 横取りし、Application::copyMouseDown()/copyMouseDrag()/
  // copyMouseUp()へ委譲する"というコメント)と全く同じ理屈で、ここでも
  // 最初に横取りする。
  if (app.copyMouseActive()) {  // 縦書きのEPUBのCOPY(2026-10-13)も
    app.copyMouseDown(startRow, startColumn);
    for (;;) {
      int byte = keyInput.readByte();
      if (!byte) continue;  // waiting for the next mouse event -- VTIME polling handles the actual wait
      if (byte != 0x1b) {
        // 通常のrunTerminalMouseDrag()本体と同じ防御的な扱い――
        // ジェスチャーの途中に届いた普通のキーは、そのままリクエストせず
        // 呼び出し元へ返す。
        TerminalCoastResult result;
        result.hadLeftoverByte = true;
        result.leftoverByte = byte;
        return result;
      }
      auto mouse = peekMouseSgr(keyInput);
      if (!mouse) {
        keyInput.playRawMacro("\x1b");
        return TerminalCoastResult{};
      }
      if (mouse->wheel != 0 || mouse->button < 0) continue;  // ドラッグ中のホイールは無視
      if (mouse->motionOnly) {
        app.copyMouseDrag(mouse->row, mouse->column);
        continue;
      }
      if (mouse->pressed) continue;  // an extra press while already down -- ignore
      app.copyMouseUp();
      return TerminalCoastResult{};
    }
  }

  fm::ContentView &view = app.activeContentView();
  int lastRow = startRow;
  bool moved = false;
  bool targetsCursor = gTerminalClickTargetsCursor;  // それ自身のコメント参照
  gTerminalClickTargetsCursor = false;
  std::deque<std::pair<std::chrono::steady_clock::time_point, int>> recentMotion;
  constexpr auto kReleaseWindow = std::chrono::milliseconds(150);
  bool columns = app.epubVerticalDragActive();  // 縦書きのEPUBの本文: 左右のドラッグ(下のループ参照)
  int lastColumn = startColumn;
  int columnAccum = 0;
  if (!targetsCursor && !columns) applyHoverRowAndRedraw(view, startRow);

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

    if (mouse->wheel != 0 || mouse->button < 0) continue;  // ドラッグ中のホイールは無視
    // 2026-10-10、直接のユーザー報告("縦書きEPUBで横方向の画面ドラッグが出来ない"): 縦書きのEPUBの本文では、
    // -g(runGuiPhase()のepubDragging)と同じく左右のドラッグで1列ずつ送る(右へで先へ)。1列の幅
    // (fm::epubVerticalColumnPitch()桁)を越えるたびにApplication::scrollEpubVertical()。離した時の
    // 慣性も同じくrunCoastLoop()(縦書きでは列を送る)。
    if (columns) {
      if (mouse->motionOnly) {
        columnAccum += mouse->column - lastColumn;
        lastColumn = mouse->column;
        int pitch = std::max(1, fm::epubVerticalColumnPitch());
        while (columnAccum >= pitch || columnAccum <= -pitch) {
          int dir = columnAccum > 0 ? 1 : -1;
          columnAccum -= dir * pitch;
          moved = true;
          auto now = std::chrono::steady_clock::now();
          recentMotion.push_back({now, dir});
          while (!recentMotion.empty() && recentMotion.front().first < now - kReleaseWindow) recentMotion.pop_front();
          if (!app.scrollEpubVertical(dir)) columnAccum = 0;
        }
        continue;
      }
      if (mouse->pressed) continue;
      if (!moved) {
        app.handleMouseClick(mouse->row, mouse->column);
        return TerminalCoastResult{};
      }
      auto cutoff = std::chrono::steady_clock::now() - kReleaseWindow;
      int releaseDistance = 0;
      for (auto &sample : recentMotion)
        if (sample.first >= cutoff) releaseDistance += sample.second;
      int level = resolveCoastLevel(fm::levelForTerminalReleaseDistance(releaseDistance));
      if (level == 0 || !app.epubVerticalDragActive()) return TerminalCoastResult{};
      return runCoastLoop(app, display, keyInput, level, releaseDistance > 0 ? 1 : -1, /*fromDrag=*/true);
    }
    if (mouse->motionOnly) {
      int rowDelta = mouse->row - lastRow;
      lastRow = mouse->row;
      if (rowDelta != 0) {
        moved = true;
        // See runCoastLoop()自身の同種の訂正(直接のユーザー報告
        // "HISTORY.md:15036が復活した"、「毎ティック無条件に先読み」
        // という形が、少し前に直したはずの「①→②の間に総行数が
        // 25→50」を1行ずつの粒度で再発させていた)――ここも「まず
        // 今読み込めている範囲だけで動かしてみて、実際に動かなかった
        // 時だけ画面2枚分を先読みして再試行する」という遅延評価の形に
        // 揃える。
        int beforeDrag = view.topIndex();
        view.setTopIndex(beforeDrag - rowDelta);
        if (view.topIndex() == beforeDrag) {
          app.preloadPagerAhead(beforeDrag - rowDelta + display.rows() * 2);
          app.suppressPagerRepoll(view.topIndex());
          view.setTopIndex(beforeDrag - rowDelta);  // natural scrolling: dragging down reveals earlier content
        }
        // see runCoastLoop()'s clampViewport() note。先へ進む(上へドラッグ)時は最下行(同じく2026-10-02)。
        view.setCursorIndex(rowDelta < 0 ? std::min(view.topIndex() + view.height() - 1,
                                                    static_cast<int>(view.lines().size()) - 1)
                                         : view.topIndex());
        if (app.syncEpubHorizontalSection()) {  // 横書きの範囲の外へ出た: 縦書きの左右のドラッグとして続ける
          columns = app.epubVerticalDragActive();
          lastColumn = mouse->column;
          columnAccum = 0;
          recentMotion.clear();
          app.activeContentView().redraw();
          continue;
        }
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
      int cursorRow = targetsCursor ? view.cursorDisplayRow() : -1;
      if (cursorRow >= 0) app.handleMouseClick(cursorRow, 0);
      else app.handleMouseClick(mouse->row, mouse->column);
      return TerminalCoastResult{};
    }
    auto cutoff = std::chrono::steady_clock::now() - kReleaseWindow;
    int releaseDistance = 0;
    for (auto &sample : recentMotion)
      if (sample.first >= cutoff) releaseDistance += sample.second;
    int level = resolveCoastLevel(fm::levelForTerminalReleaseDistance(releaseDistance));
    if (level == 0) {
      clearHoverAndRedraw(view);
      return TerminalCoastResult{};
    }
    int direction = releaseDistance > 0 ? -1 : 1;  // same natural-scrolling sign as the live-drag step above
    return runCoastLoop(app, display, keyInput, level, direction, /*fromDrag=*/true);
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
// SIGTERM/SIGHUP (2026-08-20 addition, a direct user question after
// recalling that killing legacy usilic with `kill -9` used to leave the
// terminal in raw mode, needing `reset`): confirmed by reading key.c's
// own save_signal()/key_term() -- legacy DOES install a real SIGTERM/
// SIGHUP handler there (unconditionally, not just under some launch
// mode), which just sets a flag (`Key.f.term`) the main loop notices
// on its next iteration and unwinds through normal cleanup (restoring
// termios) before actually exiting. A PLAIN `kill` was always safe on
// legacy; only `kill -9` (SIGKILL, which cannot be caught by any
// process, ever -- this part is not fixable, on either version) left
// the terminal broken. This port had NO such handler at all until now
// -- a real regression versus legacy, not a `-g`-specific gap: `-c`'s
// own KeyInput::open("/dev/tty") already puts /dev/tty in raw mode
// (ISIG off) at startup exactly like legacy's own vt_init() did, so a
// plain `kill` on `usilic -c` (or a bare, no-flag launch) was ALSO
// silently leaving the terminal broken before this fix -- `-g`
// (forceGui) just makes the exposure more obvious, since it
// deliberately keeps the LAUNCHING terminal in that same raw mode for
// the GUI session's entire lifetime (so ^Og can toggle back to it
// seamlessly -- see the guiOwnsWholeSession branch's own
// keyInput.leaveRawMode() comment for the one launch mode that's
// already exempt, since there's no terminal content to toggle back
// to there). Same flag-not-direct-cleanup shape as
// gGuiSessionSigintReceived (below, runGuiPhase()'s own equivalent) --
// async-signal-safety means a signal handler can only safely touch a
// sig_atomic_t, not call into Application/KeyInput/Display directly --
// polled once per iteration of BOTH this function and runGuiPhase()
// (every GUI entry point funnels through the latter, including
// tryEnterGuiPhase()'s own call), calling the exact same
// Application::requestImmediateExit() gGuiSessionSigintReceived
// already uses so both loops' own `while (!app.wantsExit())` unwind
// through their NORMAL exit path (KeyInput::close()/Display::close()
// at the tail of main(), which already correctly restore cooked mode
// on a normal exit) rather than needing a separate cleanup path of
// their own. Declared here (before this function, the first of the
// two loops that reads it) rather than alongside
// gGuiSessionSigintReceived further down, since that one is only ever
// read from runGuiPhase() itself.
volatile std::sig_atomic_t gTerminationSignalReceived = 0;
void handleTerminationSignal(int) { gTerminationSignalReceived = 1; }

// 2026-10-10、直接のユーザー依頼("履歴の無い(ls)が健在では混乱を来すため親が消えるとすべての子も消える"):
// 親のusilicが外部起動した子(USILIC_TRANSIENT=親のPID)は、親がいなくなったら終わる。子は端末の新しいウィンドウや
// GUIの背後のプロセスとして起動され親の直接の子ではないので、実行ループの度に親のPIDの生存を確かめる(親の
// クラッシュや、単一インスタンスの決まりで別の起動に終了させられた場合にも効く)。0なら確かめない。
pid_t gTransientParentPid = 0;
// 親のusilicが-H・最初の起動で開いた子のHELP(1)・KEY(2)の番号(USILIC_WINDOW_SLOT、Application::
// setExternalWindowSlot()参照)。-gの子は、親や互いとぴったり重ならないよう、この番号の分だけウィンドウを
// 右下へずらす(tryEnterGuiPhase()、2026-10-11)。0なら何もしない。
int gWindowSlot = 0;
// 2026-10-12: ^Ouで複数のものを別々に開いた-gの子の何番目か(USILIC_CASCADE)。その分だけ右下へずらす。
int gCascade = 0;
// 2026-10-11、直接のユーザー依頼(-gの子のHELP・KEYを親と同じ大きさで、親の右隣に並べる): 親が渡すUSILIC_GUI_FRAME
// (Application::setGuiFrameProvider()参照)を読んだもの。valid=falseなら無し。
struct GuiFrame {
  bool valid = false;
  int x = 0, y = 0, width = 0, height = 0, rows = 0, columns = 0, pointSize = 0;
  int fg[3] = {0, 0, 0}, bg[3] = {0, 0, 0};
  // 2026-10-21、直接のユーザー報告("^[[で行間を広くした後、^T^Hで開いた別ウィンドウのHELPでは全てのbold書体文字の字間が
  // 狭くなる"): -gの親は、^[[ / ^[]・^[{ / ^[}で今足している行間・字間のドット数(横書き・縦書き)も渡す(spacing)。子は
  // それを使い、外枠からマスの大きさを割り出さずに(matchWindowSize())、自分のマスのまま外枠だけを合わせる。-cの親(端末の
  // 外枠)からは渡さない。
  bool spacing = false;
  int lineExtra[2] = {0, 0}, letterExtra[2] = {0, 0};
};
GuiFrame gGuiFrame;
// -z pt(2026-10-18、直接のユーザー依頼"文字サイズの小さい明朝体は見栄えが悪いので-g起動時のデフォルトフォントサイズを
// 暫定20pt...にしてほしい（起動オプションで設定可）"): -gの窓の字の大きさ。0は指定無し(端末に重ねない起動は前回の
// 大きさ、無ければkDefaultGuiPointSize。端末に重ねる-gは端末の字の大きさ)。端末に重ねる-g・^Ogでも-zがあればその大きさ
// (端末と同じ行数・桁数の窓、同日の追記)。
int gStartFontPointSize = 0;
constexpr int kDefaultGuiPointSize = 18;

// 親側: 今のGUIのウィンドウのUSILIC_GUI_FRAMEの値(GuiFrameと同じ並び、カンマ区切り)。
std::string guiFrameText(fm::GraphicalDisplay &gui) {
  int x, y, w, h;
  gui.outerFrame(x, y, w, h);
  fm::GraphicalDisplay::Color fg = gui.foregroundColor(), bg = gui.backgroundColor();
  std::ostringstream out;
  out << x << ',' << y << ',' << w << ',' << h << ',' << gui.rows() << ',' << gui.columns() << ','
      << gui.fontPointSize() << ',' << int(fg.r) << ',' << int(fg.g) << ',' << int(fg.b) << ',' << int(bg.r) << ','
      << int(bg.g) << ',' << int(bg.b);
  auto [lineH, lineV] = fm::GraphicalDisplay::lineHeightExtras();
  auto [letterH, letterV] = fm::GraphicalDisplay::letterSpacingExtras();
  out << ',' << lineH << ',' << lineV << ',' << letterH << ',' << letterV;  // GuiFrame::spacing
  return out.str();
}
bool transientParentGone() {
  return gTransientParentPid > 0 && ::kill(gTransientParentPid, 0) != 0 && errno == ESRCH;
}

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
    // 2026-09-08追加、直接のユーザー要望("-cのCOPY: ^-をトグルに
    // しなくなったのは...それらは本来-gでの話であって-cはGUIじゃ
    // ないのでマウスカーソルによる範囲指定は最初から守備範囲じゃ
    // ない。だがその結果として離れ小島不能となるのは惜しいので、
    // いっそ(画面ドラッグを含む)慣性スクロール以外のマウスオペレー
    // ションは-cでも可能、ってのは？"): forceCui(明示的な`-c`)は元々
    // 「本物の、graphics非対応な端末での動作検証」という目的のため
    // 全モードでマウス追跡を無条件に無効化していた(runTerminalPhase()
    // 自身の宣言コメント参照)が、COPYモード限定でこれを覆す――COPY
    // モードは自身のスナップショットが静的であることに変わりが無く
    // (runTerminalMouseDrag()自身の同日追加コメント参照)、慣性
    // スクロール/コーストの物理を一切必要としないため、「本物の
    // graphics非対応端末での検証」という当初の懸念にも触れない。
    // マウス無反応な環境(本当に非対応な端末)では、単にこの
    // エスケープシーケンスが無視され、以前と同じ「マウスでは何も
    // 起きない」に留まるだけで安全側に倒れる。
    // 2026-10-01訂正、直接のユーザー指摘("マウスの無い端末では、マウスの合図が無視されて何も
    // 起きないだけ、なら-cも引数無しと同じでいいんじゃない？"): `-c`でもマウスを絞らない
    // (SHELL以外で常に追跡)。マウスの無い端末では追跡の合図が無視されるだけ。
    (void)forceCui;
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
    // See gTerminationSignalReceived's own declaration comment (a
    // plain `kill`/SIGTERM or terminal-hangup SIGHUP, unlike SIGKILL,
    // is recoverable -- this lets that recovery actually happen,
    // matching legacy's own key_term()/Key.f.term).
    if (gTerminationSignalReceived || transientParentGone()) {
      app.requestImmediateExit();
      return leavePhase(true);
    }
    if (app.consumeGuiModeToggleRequest()) return leavePhase(true);  // ^Og(Command::ToggleGuiMode参照)
    if (app.takeFontListRequest()) app.showNotice("Font list: -g only");  // ^J^F(buildFontList()参照)
    if (app.takeLineHeightAdjustRequest()) app.showNotice("LINE-HEIGHT: -g only");  // ^[[ / ^[]
    if (app.takeLetterSpacingAdjustRequest()) app.showNotice("LETTER-SPACING: -g only");  // ^[{ / ^[}
    // 端末の画面には行の高さが無いが、^Ogで-gへ切り替えた時に読み直した値で開くよう、倍率だけは入れておく。
    if (auto ratios = app.takeLineHeightReloadRequest())
      fm::GraphicalDisplay::setLineHeightRatios(ratios->first, ratios->second);
    // See Application::awaitingLocateUpdateDb()'s own comment: this
    // loop's own poll() below already ticks every ~100ms regardless of
    // input, so a detached `updatedb` launched by ^Jl is noticed and
    // chained into the LOCATE prompt on its own, without the user
    // needing to press anything.
    app.pollLocateUpdateDbCompletion();
    app.pollDuCompletion();
    app.pollArchiveLoadCompletion();
    app.pollPendingCpMvRmShellRun();
    app.pollPendingArchiveExtractShellRun();
    app.pollShellOfferFollowUp();
    app.pollShellHandoff();
    app.pollChildChannel();  // -KのKEYの^M・hの受け渡し(Application::openChildChannel()参照)
    app.pollPrefixEcho();  // PREFIXキーの「C-x-」等(Application::pendingPrefixLabel()参照)
    app.pollPagerExitNotice();
    app.pollExternalPagerWindows();
    syncMouseTracking();
    // !app.promptActive() (2026-09-18 fix, a real report: LIST's
    // PromptKind::ShellCommand combobox -- see its own declaration
    // comment -- lives on Shell's own commandLineRow()/mode-line rows
    // while currentMode() is (necessarily) back to Shell already, but
    // Shell::flushNewScrollbackLines()/handlePromptDetected() call
    // Shell::redrawPromptRow() unconditionally the moment ANY buffered
    // child output is read, overwriting that row with the frozen
    // pre-Tab command line the instant an ordinary editing byte reached
    // this window -- "^Bを押した途端、モード名とプロンプトの「usilic
    // 」が戻る"). Mirrors the existing hover-guard's own !promptActive()
    // gating just above for the exact same reason: any of Shell's
    // internal display writes triggered by newly-read pty bytes are
    // blind to Application's own overlay on this row. Deferring the
    // read until editing concludes (Enter/^G, whereupon promptActive()
    // goes back to false and this resumes watching) just delays when
    // that output becomes visible by a few keystrokes' worth of time --
    // the bytes stay queued in the OS pty buffer meanwhile.
    bool watchShellPty =
        app.currentMode() == fm::ApplicationMode::Shell && app.shell().alive() && !app.promptActive();

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;  // see file comment on why this, not keyInput.fd(), is polled
    fds[0].events = POLLIN;
    fds[1].fd = watchShellPty ? app.shell().ptyFd() : -1;  // negative fd: poll() ignores this slot
    fds[1].events = POLLIN;
    int ready = poll(fds, 2, 100);  // 100ms so SIGWINCH/GUI-toggle still get noticed promptly when idle

    if (ready <= 0) {
      if (keyInput.consumeResizeSignal()) app.handleResize();  // updates Display's geometry, then reflows content
      // 2026-09-24追加、直接のユーザー報告("bash^M以後、プロンプトが
      // haa15030@mpi:~$になる。^Mを押さないとそれが出ない"):
      // Shell::flushIdleOutput()自身の宣言コメント参照。poll()が
      // 100msタイムアウトした(=PTYが真に静かになった)このまさに
      // その瞬間が、マーカー一致を待たずに未確定の出力行を仮確定
      // させるべきタイミング――watchShellPtyと同じガード(Shellモード
      // かつ生きていて、かつ他のプロンプトがこの行を専有していない
      // 時だけ)を再利用する。
      if (watchShellPty) app.shell().flushIdleOutput();
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
    if (byte != 0x1b) gTerminalClickTargetsCursor = false;  // キー(それ自身のコメント参照)
    if (byte == 0x1b) {
      // 2026-10-02、直接のユーザー依頼("←→で開始でいい"、"-cでは←→が↑↓に割り当てられているので
      // 速度調整にもスクロール開始にも使えない"): 一覧系のモードでは←→を^P/^Nとしてではなく、
      // 慣性スクロールの開始(→で先へ、最低速、runCoastLoop())に使う。以前のShift+↑/↓は廃止
      // (Terminal.appでは手動のキー割り当てが要るため)。
      if (app.currentMode() != fm::ApplicationMode::Shell && app.currentMode() != fm::ApplicationMode::Copy &&
          !app.awaitingSequenceOrPromptByte()) {
        // 縦書きのEPUBの本文では↓↑(↓で先へ)、章の一覧では矢印は横取りしない(-gと同じ、
        // runCoastLoop()参照)。
        const char *trigger = app.epubVerticalDragActive() ? "AB" : app.epubVerticalScrollActive() ? "" : "CD";
        if (int arrow = *trigger ? peekArrow(keyInput, trigger) : 0) {
          consecutiveEscapes = 0;
          app.noteUserActivity();  // 慣性の開始でも一時的な表示を消す(Application::noteUserActivity()参照)
          int delta = arrow == 'B' || arrow == (app.leftArrowAdvances() ? 'D' : 'C') ? 1 : -1;
          TerminalCoastResult coast = runCoastLoop(app, display, keyInput, resolveCoastLevel(1), delta, /*fromDrag=*/false);
          syncTerminalCursorAfterMouse(app, display);
          if (coast.hadLeftoverByte) app.feedByte(coast.leftoverByte, std::nullopt);
          continue;
        }
      }
      // A mouse report -- only actually arrives while mouseTrackingOn
      // (Terminal.app doesn't send these otherwise), but safe to
      // attempt unconditionally: peekMouseSgr() requeues via
      // playRawMacro() on any mismatch, same as peekArrow().
      if (auto pasted = peekBracketedPaste(keyInput)) {
        consecutiveEscapes = 0;
        // ドロップされたファイル(-gのGraphicalDisplay::consumeDroppedFiles()と同じくopenDroppedPaths())。
        // パスでなければ、またはプロンプトの中なら、これまで通りキーとして渡す。
        std::vector<std::string> paths = droppedPathsFromPaste(*pasted);
        if (!paths.empty() && app.currentMode() != fm::ApplicationMode::Shell && !app.promptActive()) {
          app.noteUserActivity();
          app.openDroppedPaths(paths);
          display.flush();
        } else {
          for (unsigned char c : *pasted) app.feedByte(c ? c : 0x80, std::nullopt);
        }
        continue;
      }
      auto mouse = peekMouseSgr(keyInput);
      // 左ボタンの押下/離す以外(移動・ホイール・他のボタン・キー)はgTerminalClickTargetsCursorを下ろす。
      if (!mouse || mouse->wheel != 0 || mouse->motionOnly || mouse->button != 0) gTerminalClickTargetsCursor = false;
      if (mouse) {
        consecutiveEscapes = 0;
        app.noteUserActivity();  // マウスに触れたら一時的な表示を消す(Application::noteUserActivity()参照)
        if (mouse->wheel != 0) {
          // ホイール(fm::MouseSgrEvent::wheel参照): 2026-10-02、直接のユーザー依頼("ホイールを本来の
          // 機能（画面スクロール）に戻してほしい")により、慣性スクロールではなく1刻み1行の画面
          // スクロール(scrollByWheel())。
          scrollByWheel(app, display, mouse->wheel, 1);
        } else if (mouse->pressed && !mouse->motionOnly && mouse->button == 2) {
          // 2026-10-01追加、直接のユーザー依頼("マウスの右クリックに^Gを割り当てて"): -gの右クリック
          // (runGuiPhase()のclick->rightButton、2026-09-07)と同じく、本物のキーボードの^Gと同じ経路へ。
          app.feedCanonicalByte(0x07);
        } else if (mouse->pressed && !mouse->motionOnly && mouse->button == 0) {
          // Left-button down: the mouse-driven equivalent of
          // Left/Right -- see runTerminalMouseDrag()'s own comment
          // for why this isn't decided as click-vs-drag until release.
          TerminalCoastResult coast = runTerminalMouseDrag(app, display, keyInput, mouse->row, mouse->column);
          syncTerminalCursorAfterMouse(app, display);
          if (coast.hadLeftoverByte) app.feedByte(coast.leftoverByte, std::nullopt);
        } else if (mouse->motionOnly && app.directEntryListActive()) {
          app.hoverDirectEntryList(mouse->row);  // Application::directEntryListActive()参照
        } else if (mouse->motionOnly && !app.redrawSensitivePromptOrRecallActive()) {
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
          app.directoryMode().clearEditError();  // see the GUI-side hover call site's own comment
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
      consecutiveEscapes = nextConsecutiveEscapes(consecutiveEscapes);
      app.feedByte(0x1b, std::nullopt);
      // 2026-09-07修正、直接のユーザー報告(⑬"^[^[のみ追加"、COPY
      // モードに限らず-c全体で再現): 上のコメントが言う「ride along」
      // する2バイト目そのものが、素早く連打された2発目の裸のESC
      // 自身であることがある――KeyInput::readEscapeSequence()は既知
      // シーケンス判定のため、ESCの直後を最大remaining(=矢印キー等の
      // シーケンス長-1)バイトぶん、VTIME=1(100ms)刻みでブロッキング
      // 読みするため、その待ち時間内に届いた2発目のESCが「remaining」
      // の1バイトとして誤って飲み込まれてしまう。従来はここで
      // lastEscapeBytes()の中身を無条件にapp.feedByte()だけへ流して
      // いたため、この飲み込まれた2発目のESCがconsecutiveEscapesの
      // カウントに一切反映されず、^[^[ Enterの直接終了ジェスチャーが
      // (COPYモードに限らず、素のDirectory等でも)全く機能しなかった
      // ――実機PTYテストで確認済み。ここで、飲み込まれた各バイトを
      // 「本来トップレベルのpoll()ループ自身で読まれていたら辿って
      // いたはずの状態遷移」に通し直す: 0x1bならまさに今と同じ「裸の
      // ESC」として数え直し、Enterかつ2回以上ESCが続いていればこの場
      // で即終了、それ以外の通常バイトはconsecutiveEscapesをリセット
      // しつつApplicationへそのまま渡す。
      for (unsigned char c : keyInput.lastEscapeBytes()) {
        if (c == 0x1b) {
          consecutiveEscapes = nextConsecutiveEscapes(consecutiveEscapes);
          app.feedByte(0x1b, std::nullopt);
          continue;
        }
        if (c == 0x0d && consecutiveEscapes >= 2) {
          app.requestImmediateExit();
          consecutiveEscapes = 0;
          continue;
        }
        consecutiveEscapes = 0;
        app.feedByte(c, std::nullopt);
      }
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
//   - (2026-09-14 correction: this bullet was stale and actively
//     misleading -- it describes a problem already solved, by design,
//     back on 2026-08-05/commit a73c052, well before this comment
//     block's own "still true after this rework" 2026-08-30 rework.
//     handleMouseClick() DOES fire on every button-down, on purpose:
//     the click-vs-drag distinction is spatial, not timing-based --
//     Application::handleMouseClick() no-ops unless the press column
//     falls within ContentView::isColumnWithinLineScope() (the `ls`
//     listing's own name column, or a left-aligned list's actual text
//     span), so a press outside that scope is free to also become a
//     drag start with no race. HISTORY.md's 2026-08-05 entries record
//     two earlier, REJECTED timing/motion-based attempts (a row-
//     crossing threshold, then a fixed pixel-jitter threshold) that
//     both failed real-mouse/trackpad testing for the same reason:
//     genuine stationary clicks essentially never report zero raw
//     motion, so deferring the click to ButtonUp and cancelling it on
//     any interim MOUSEMOTION makes nearly every real click misfire as
//     a drag. A third attempt at exactly this (GraphicalDisplay::
//     Impl::pendingLeftClick, added 2026-09-10) reproduced that same
//     failure and was reverted the same day it was reported broken --
//     do not re-attempt a timing/motion-based fix here again.
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
// 2026-09-25拡張、直接のユーザー報告("GUI起動で.mdファイルを`^[^M`
// すると暴走する。ついては裏ターミナルで`^C`投入でkill可を実装
// よろしく"): 1回目はこれまで通りフラグを立てるだけ(runGuiPhase()が
// 次の周回で拾い、ESC ESC Enterと同じ穏当な終了へ進む)。ただし暴走中は
// その周回自体が回ってこない(どこか別の場所で止まっている/回り続けて
// いる)ことがあり得るので、フラグが立ったままの状態でもう一度届いた
// ら、既定の動作に戻して自分自身へ送り直し、強制終了する。signal()/
// raise()はどちらもasync-signal-safe。
void handleSigintForGuiSession(int) {
  if (gGuiSessionSigintReceived) {
    std::signal(SIGINT, SIG_DFL);
    std::raise(SIGINT);
    return;
  }
  gGuiSessionSigintReceived = 1;
}

// 起動元ターミナルで外部プログラム(PROGRAM/EDIT)を動かしている間は、
// そのプログラム自身への^C(lessの検索中断等)でusilicまで終了しない
// よう、SIGINTを一時的に無視する。子プロセス側はChildProcess自身の
// resetSignalsToDefault()で既定に戻るので、^Cは子には従来通り届く。
struct IgnoreSigintWhileChildRuns {
  void (*previous)(int);
  IgnoreSigintWhileChildRuns() : previous(std::signal(SIGINT, SIG_IGN)) {}
  ~IgnoreSigintWhileChildRuns() { std::signal(SIGINT, previous); }
};

// 2026-09-25追加、offerGroffInstallOnce()用: PATHと、Finderからの
// 起動ではPATHに含まれないHomebrewの既定の場所の両方から実行ファイルを
// 探す。見つかればそのフルパス、無ければ空。
std::string findExecutableIn(const std::string &name, bool includeHomebrewDefaults) {
  std::vector<std::string> dirs;
  if (const char *path = std::getenv("PATH")) {
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':'))
      if (!dir.empty()) dirs.push_back(dir);
  }
  if (includeHomebrewDefaults) {
    dirs.push_back("/opt/homebrew/bin");
    dirs.push_back("/usr/local/bin");
  }
  for (const auto &dir : dirs) {
    std::string candidate = dir + "/" + name;
    if (::access(candidate.c_str(), X_OK) == 0) return candidate;
  }
  return "";
}
std::string findExecutable(const std::string &name) { return findExecutableIn(name, true); }
[[maybe_unused]] std::string findExecutableOnPathOnly(const std::string &name) { return findExecutableIn(name, false); }

// 2026-09-25追加、直接のユーザー要望("groff未インストールの環境では
// 最初のusilic起動時SHELLに切り替わり、プロンプトで`brew install groff`
// の return 待ちにする。brewがなければインストール用URLをプロンプトから
// 表示"、"SHELLのログはINFOで確認できるので1回で。Ubuntuはそれでいい"
// ――Ubuntuは`sudo apt install groff`): IDEAS.mdの「機能上必要な外部
// プログラムが無い場合、SHELLに切り替えて当該プログラムを自動インス
// トール」の第一弾。~/.usilic/groff-offeredを印に、生涯で1回だけ出す。
// コマンドは入力欄に置くだけで実行はしない(returnを押すかどうかは
// ユーザーが決める)。通常のディレクトリ起動の時だけ(ページャ起動には
// 割り込まない)。
// 2026-09-27拡張、直接のユーザー依頼("PROGRAM：...mg、infoを追加。最初の
// 起動時、「brew install groff mg info」でインストール"): groffに加えて、
// usilicrcのPROGRAMに載せたmg/infoのうち見つからないものもまとめて1回で
// 提示する。Homebrewではinfoはtexinfoに入っている(`brew install info`という
// formulaは無い)。macOSはmgを/usr/bin/mgとして標準で持っている。印は
// ~/.usilic/install-offered(以前のgroff-offeredで既にgroffを提示済みの
// 環境でも、mg/infoの分は1回出す)。
void offerGroffInstallOnce(fm::Application &app, const std::string &usilicDir) {
#ifdef _WIN32
  // 2026-10-20、直接のユーザー報告("Windowsには不要では？"、=kv.png、初回起動でcmdへ`sudo apt install …`が送られて
  // いた): Windowsのネイティブにはaptもgroff等も無く(MANは組んでおいた平文、ManMode::runAndWrap()参照)、案内しない。
  (void)app;
  (void)usilicDir;
  return;
#endif
  // 2026-10-05、直接のユーザー報告(WSLで"w3mが無い。apt installリストに入ってなかった？"): 印のファイルが
  // あるだけで二度と案内しなかったので、後から対象に足したもの(w3m・unzip・fonts-noto-cjk)が、既に案内
  // 済みの環境では一度も案内されなかった。印のファイルには案内したパッケージ名を1行ずつ書き、まだ案内して
  // いないもので見つからないものがあれば、それだけをもう一度案内する(以前の空の印は「何も案内していない」)。
  std::string marker = usilicDir + "install-offered";
  std::set<std::string> offered;
  {
    std::ifstream in(marker);
    for (std::string line; std::getline(in, line);)
      if (!line.empty()) offered.insert(line);
  }
  // {コマンド名, パッケージ名}
#ifdef __APPLE__
  // 2026-09-27追加、直接のユーザー提案("brew install groff mg texinfo 7zz"):
  // 7-Zip(ARCの.7z)も。Homebrewのformula名はsevenzip(コマンド名は7zz、
  // `7zz`というformulaは無い)。fm::sevenZipCommand()が7zの代わりに7zzを使う。
  // 2026-10-05: w3m(EPUBのWebのリンクのDump/Term、項目623)も。
  const std::vector<std::pair<std::string, std::string>> tools = {
      {"groff", "groff"}, {"mg", "mg"}, {"info", "texinfo"}, {"7zz", "sevenzip"}, {"w3m", "w3m"}};
#elif defined(__MSYS__) || defined(__CYGWIN__)
  // MSYS2(2026-10-05、.steering/20261005-windows-gitbash/): MSYS2のmsysのリポジトリにあるものだけ
  // (mg・w3mは無い)。infoはtexinfo、7zはp7zipに入っている。
  const std::vector<std::pair<std::string, std::string>> tools = {
      {"groff", "groff"}, {"info", "texinfo"}, {"7z", "p7zip"}, {"unzip", "unzip"}};
#else
  const std::vector<std::pair<std::string, std::string>> tools = {
      {"groff", "groff"}, {"mg", "mg"}, {"info", "info"}, {"7z", "p7zip-full"}, {"w3m", "w3m"}, {"unzip", "unzip"}};
#endif
  // 見つからないパッケージ(全部あれば空)。
  auto missingPackages = [tools]() {
    std::vector<std::string> missing;
    for (const auto &tool : tools) {
      bool found = !findExecutable(tool.first).empty();
      if (tool.first == "7zz" || tool.first == "7z")
        found = !findExecutable("7z").empty() || !findExecutable("7zz").empty();
      if (!found) missing.push_back(tool.second);
    }
#if !defined(__APPLE__) && !defined(__MSYS__) && !defined(__CYGWIN__)
    // 2026-10-05(.steering/20261005-windows-wsl2/): -gの日本語に要るNoto CJKフォント(GraphicalDisplayが
    // 探す場所)がどれも無ければfonts-noto-cjkも。WSL2のUbuntuには最初は入っていない。
    {
      std::error_code fontEc;
      bool haveCjkFont = false;
      for (const char *font : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                               "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"})
        if (std::filesystem::exists(font, fontEc)) haveCjkFont = true;
      if (!haveCjkFont) missing.push_back("fonts-noto-cjk");
    }
#endif
#ifdef __APPLE__
    // 2026-10-20、直接のユーザー依頼: -gの既定のフォントのIPAゴシック(ipaGothicFontPath())も。font-ipafontはcaskだが、
    // 同じ名前のformulaが無いので、brew installにformulaと並べて書ける。
    if (ipaGothicFontPath().empty()) missing.push_back("font-ipafont");
#endif
    return missing;
  };
  // 今回案内するもの: 見つからないもののうち、まだ案内していないもの。
  std::set<std::string> toOffer;
  for (const auto &package : missingPackages())
    if (!offered.count(package)) toOffer.insert(package);
  if (toOffer.empty()) return;
  // toOfferのうち見つからないものだけのインストールコマンド(全部あれば空、followUpでも使う)。
  auto installCommand = [missingPackages, toOffer]() -> std::string {
    std::string packages;
    for (const auto &package : missingPackages())
      if (toOffer.count(package)) packages += " " + package;
    if (packages.empty()) return "";
#ifdef __APPLE__
    std::string brew = findExecutable("brew");
    // brewがPATH上に無い(Finderからの起動でHomebrewの既定の場所だけで
    // 見つかった)場合は、SHELLでもそのまま実行できるようフルパスで書く。
    // 2026-10-05、直接のユーザー依頼("brewが無い場合の案内も入れて欲しい。Xcodeも必要"): brewが
    // 無ければ、Homebrew公式のインストールコマンド(https://brew.sh、足りなければXcodeのCommand Line
    // Toolsも入れる)を置く。終わってプロンプトが戻れば、下のfollowUpで今度はbrew installを置く。
    // 2026-10-20、直接のユーザー依頼("あとは自動で入るほうがいい"、案内は^Mを待たずに実行する): Homebrewの
    // インストーラーはNONINTERACTIVE=1で「Press RETURN」の確認を省く(sudoのパスワードは聞かれる)。brew installは
    // HOMEBREW_NO_ASK=1で"Do you want to proceed with the installation? [y/n]"を省く(-yを知らない古いbrewでも、
    // 環境変数なら無視されるだけ)。
    if (brew.empty())
      return "NONINTERACTIVE=1 /bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
             "  # Homebrew未インストール(Xcode Command Line Toolsも必要なら入る)。終わると次は brew install" +
             packages;
    // 2026-10-20、直接のユーザー依頼("リストの真っ先にipafontを持ってきて"): font-ipafontは先に別のbrew installで
    // (入りしだい-gのフォントをIPAゴシックに替える、runGuiPhase())。;でつなぎ、失敗しても残りは続ける。
    std::string program =
        "HOMEBREW_NO_ASK=1 " + (brew == findExecutableOnPathOnly("brew") ? std::string("brew") : brew);
    const std::string font = " font-ipafont";
    if (std::size_t at = packages.find(font); at != std::string::npos && packages != font) {
      packages.erase(at, font.size());
      return program + " install" + font + "; " + program + " install" + packages;
    }
    return program + " install" + packages;
#elif defined(__MSYS__) || defined(__CYGWIN__)
    // MSYS2の端末ならpacman。Git Bashにはパッケージ管理が無いので案内しない(空を返す)。
    if (findExecutable("pacman").empty()) return "";
    return "pacman -S --needed --noconfirm" + packages;
#else
    return "sudo apt install -y" + packages;  // -y: aptの確認(Y/n)を省く(2026-09-27)
#endif
  };
  std::string command = installCommand();
  if (command.empty()) return;
  // 2026-09-27追加、直接のユーザー提案("brew install groffは2回必要なので、
  // 処理が終わり、プロンプトが戻ったらインストール済みかどうか確認後、
  // 未インストールのアプリのみ再度brew実行"): 実行後にまだ足りないものが
  // あれば、その分だけのコマンドを再び入力欄に置く(最大2回。実行はやはり
  // ユーザーのreturnで)。
  // brewから入れる場合は、Homebrew自体のインストールの後にbrew installが続くので1回多く。
  // 案内済みの印は、実際に入力欄に置けた時に書く(Application::scheduleShellOffer()参照、2026-10-20)。
  app.scheduleShellOffer(command, installCommand, /*retries=*/3, [marker, offered, toOffer] {
    std::ofstream out(marker);
    for (const auto &package : offered) out << package << "\n";
    for (const auto &package : toOffer) out << package << "\n";
  });
}

bool stopBackgroundQuickLook();  // 自身のコメント参照

// ^Of(K-104、2026-10-06、.steering/20261006-gui-font-picker/、Application::takeFontListRequest()参照)
// の一覧。fc-listから、FONTは等幅(:spacing=mono)、SANS-SERIF/SERIFは日本語の入ったもの(:lang=ja、
// 直接のユーザー判断"英語用なら(a)だが暫定(b)")。名前に明朝/Mincho/Serif等を含むものはSERIF、
// 他はSANS-SERIFへ。usilicrcにはファイルしか書けず、TTF_OpenFont()はファイルの0番目の書体を開くので、
// 0番目(index 0)だけを載せる。"."で始まる名前はmacOSの内部用。今のフォントが一覧に無ければ先頭に足す。
std::vector<fm::FontListEntry> buildFontList(const fm::GraphicalDisplay &gui) {
  struct Face {
    std::string path, name;
  };
  // hidden: ファミリー名が「.」で始まるもの(macOSがアプリ向けに隠しているシステムフォント)も、先頭の「.」を除いて
  // 出す。FONT(等幅)だけ(2026-10-20、直接のユーザー報告"miniの^J^F一覧のFONT項目にSFNSMonoが出ない"、
  // SFNSMono.ttfのファミリー名は「.SF NS Mono」)。
  auto query = [](const std::string &pattern, bool hidden = false) {
    std::vector<Face> faces;
    fm::ChildProcess::Result result =
        fm::ChildProcess::run({"fc-list", "-f", "%{index}\t%{file}\t%{family[0]}\t%{style[0]}\n", pattern});
    if (!result.ok) return faces;
    std::istringstream in(result.output);
    std::set<std::string> seen;
    for (std::string line; std::getline(in, line);) {
      std::vector<std::string> fields;
      std::istringstream fieldStream(line);
      for (std::string field; std::getline(fieldStream, field, '\t');) fields.push_back(field);
      if (fields.size() < 3 || fields[0] != "0" || fields[2].empty()) continue;
      if (fields[2][0] == '.') {
        // 「.LastResort」「.Times LT MM」等の使えないものも等幅として出てくるので、名前に「Mono」のあるものだけ。
        if (!hidden || fields[2].find("Mono") == std::string::npos) continue;
        fields[2].erase(0, 1);
      }
      if (!seen.insert(fields[1]).second) continue;
      std::string name = fields[2] + (fields.size() > 3 && !fields[3].empty() ? " " + fields[3] : "");
      faces.push_back({fields[1], name});
    }
    std::sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) { return a.name < b.name; });
    return faces;
  };
  auto looksSerif = [](const std::string &name) {
    for (const char *word : {"明朝", "Mincho", "MINCHO", "Serif", "SERIF", "Ming", "Song", "教科書", "宋"})
      if (name.find(word) != std::string::npos) return true;
    return false;
  };
  std::vector<Face> mono = query(":spacing=mono", true), japanese = query(":lang=ja"), sans, serif;
  // 2026-10-20、直接のユーザー報告("miniの^J^Fで等倍フォント一覧にipag.ttcが出ない"): fontconfigは和文の等幅
  // (IPAゴシック・MSゴシック・BIZ UDゴシック・Osaka等幅等、全角が半角の2倍)を:spacing=monoではなくdual(90)とする。
  // 日本語の入ったdualも等幅に(点字等の日本語の無いdualは出さない)。
  {
    std::set<std::string> seen;
    for (const auto &face : mono) seen.insert(face.path);
    for (const auto &face : query(":spacing=dual:lang=ja"))
      if (seen.insert(face.path).second) mono.push_back(face);
    std::sort(mono.begin(), mono.end(), [](const Face &a, const Face &b) { return a.name < b.name; });
  }
  // 2026-10-06追加、直接のユーザー依頼("wsl版にメイリオが入るようにして"): WSLのfc-listにはWindows側のフォントが
  // 出ないので、/mnt/c/Windows/Fontsをfc-scanで調べて足す(等幅は%{spacing}が100、または和文の等幅でよくある
  // 90(dual、MSゴシック等)。日本語は%{lang}にja)。
  if (std::filesystem::is_directory("/mnt/c/Windows/Fonts")) {
    fm::ChildProcess::Result scan = fm::ChildProcess::run(
        {"fc-scan", "-f", "%{index}\t%{file}\t%{family[0]}\t%{style[0]}\t%{spacing}\t|%{lang}|\n", "/mnt/c/Windows/Fonts"});
    std::set<std::string> seenMono, seenJapanese;
    for (const auto &face : mono) seenMono.insert(face.path);
    for (const auto &face : japanese) seenJapanese.insert(face.path);
    std::istringstream in(scan.output);
    for (std::string line; std::getline(in, line);) {
      std::vector<std::string> fields;
      std::istringstream fieldStream(line);
      for (std::string field; std::getline(fieldStream, field, '\t');) fields.push_back(field);
      if (fields.size() < 6 || fields[0] != "0" || fields[2].empty()) continue;
      Face face{fields[1], fields[2] + (fields[3].empty() ? "" : " " + fields[3])};
      if ((fields[4] == "100" || fields[4] == "90") && seenMono.insert(face.path).second) mono.push_back(face);
      if (fields[5].find("|ja|") != std::string::npos && seenJapanese.insert(face.path).second) japanese.push_back(face);
    }
    auto byName = [](const Face &a, const Face &b) { return a.name < b.name; };
    std::sort(mono.begin(), mono.end(), byName);
    std::sort(japanese.begin(), japanese.end(), byName);
  }
#ifdef _WIN32
  // Windowsのネイティブ(2026-10-16、直接のユーザー報告"^J^Fしたけどno font"): fc-listが無いので、
  // GraphicalDisplay::scanFontFiles()でWindowsのフォントのフォルダと、ユーザーごとに入れたフォントのフォルダを調べる。
  {
    std::vector<std::string> directories;
    const char *windir = std::getenv("WINDIR");
    directories.push_back(std::string(windir && *windir ? windir : "C:/Windows") + "/Fonts");
    if (const char *local = std::getenv("LOCALAPPDATA"); local && *local)
      directories.push_back(std::string(local) + "/Microsoft/Windows/Fonts");
    for (auto &directory : directories)
      for (char &c : directory)
        if (c == '\\') c = '/';
    for (const auto &face : fm::GraphicalDisplay::scanFontFiles(directories)) {
      if (face.monospace) mono.push_back({face.path, face.name});
      if (face.japanese) japanese.push_back({face.path, face.name});
    }
    auto byName = [](const Face &a, const Face &b) { return a.name < b.name; };
    std::sort(mono.begin(), mono.end(), byName);
    std::sort(japanese.begin(), japanese.end(), byName);
  }
#endif
  if (mono.empty() && japanese.empty()) return {};
  for (const auto &face : japanese) (looksSerif(face.name) ? serif : sans).push_back(face);
  std::vector<fm::FontListEntry> entries;
  auto add = [&](const std::string &section, fm::GraphicalDisplay::FontKind kind, const std::vector<Face> &faces) {
    std::string current = gui.currentFontPath(kind);
    auto label = [&](const std::string &name, const std::string &path) {
      std::string head = section;
      head.resize(std::max<std::size_t>(head.size(), 12), ' ');
      return head + name + "  (" + std::filesystem::path(path).filename().string() + ")";
    };
    bool found = std::any_of(faces.begin(), faces.end(), [&](const Face &face) { return face.path == current; });
    if (!current.empty() && !found && !(kind == fm::GraphicalDisplay::FontKind::Base && current == gTerminalFontPath))
      entries.push_back({section, current, label(std::filesystem::path(current).stem().string(), current), true});
    for (const auto &face : faces)
      entries.push_back({section, face.path, label(face.name, face.path), face.path == current});
  };
  // 端末のフォント(gTerminalFontPath): FONTの先頭にいつも出し、選ぶとusilicrcのFONTを空にして端末のフォントへ戻す
  // (applyFontChoice()の"FONT:terminal")。今のフォントがこれなら下線。
  if (!gTerminalFontPath.empty()) {
    std::string head = "FONT";
    head.resize(12, ' ');
    entries.push_back({"FONT:terminal", gTerminalFontPath,
                       head + "端末のフォント: " + std::filesystem::path(gTerminalFontPath).stem().string() + "  (" +
                           std::filesystem::path(gTerminalFontPath).filename().string() + ")",
                       gui.currentFontPath(fm::GraphicalDisplay::FontKind::Base) == gTerminalFontPath});
  }
  add("FONT", fm::GraphicalDisplay::FontKind::Base, mono);
  add("SANS-SERIF", fm::GraphicalDisplay::FontKind::SansSerif, sans);
  add("SERIF", fm::GraphicalDisplay::FontKind::Serif, serif);
  return entries;
}

// ^Ofの一覧で選ばれたフォントを、その場で反映してusilicrcの節へ書く(-Fと同じ、applyRcOptions()参照)。
void applyFontChoice(fm::Application &app, fm::GraphicalDisplay &gui, const std::string &chosenSection,
                     const std::string &path) {
  // アイコンからの起動では、基本のフォントはICON-FONTへ(Configuration::setIconLaunch()参照、2026-10-22)。
  const bool icon = fm::Configuration::iconLaunch();
  std::string section = icon && chosenSection == "FONT" ? "ICON-FONT" : chosenSection;
  if (section == "FONT:terminal") {  // 端末のフォント(buildFontList()参照): 切り替えて、usilicrcのFONTを空に
    if (!gui.setFont(fm::GraphicalDisplay::FontKind::Base, path)) {
      app.showNotice("Cannot use this font");
      return;
    }
    const char *home = std::getenv("HOME");
    // アイコンからの起動でICON-FONTを空にするとFONTへ戻るので、そのフォントのファイルを書く。
    const char *fontSection = icon ? "ICON-FONT" : "FONT";
    if (!fm::usilicrc::setSection(home ? home : "", fontSection,
                                  icon ? std::vector<std::string>{path} : std::vector<std::string>{}))
      app.showNotice(std::string("Cannot write ") + fontSection + " to usilicrc");
    return;
  }
  auto kind = section == "FONT" || section == "ICON-FONT" ? fm::GraphicalDisplay::FontKind::Base
              : section == "SANS-SERIF" ? fm::GraphicalDisplay::FontKind::SansSerif
                                        : fm::GraphicalDisplay::FontKind::Serif;
  if (!gui.setFont(kind, path)) {
    app.showNotice("Cannot use this font");
    return;
  }
  const char *home = std::getenv("HOME");
  if (!fm::usilicrc::setSection(home ? home : "", section, {path}))
    app.showNotice("Cannot write " + section + " to usilicrc");
}

void runGuiPhase(fm::Application &app, fm::GraphicalDisplay &gui) {
  // 2026-09-25追加、Application::setInterruptPeeker()自身の宣言コメント
  // 参照: ^[^Mの読み込み中にGUIウィンドウ側で押されたキーで中断できる
  // ようにする。キーは中断のためだけに使い、捨てる(コマンドとしては
  // 実行しない)。ウィンドウが閉じられた場合も中断扱い。途中のどの
  // returnからでも確実に外すため、デストラクタで解除する。
  struct InterruptPeekerGuard {
    fm::Application &app;
    ~InterruptPeekerGuard() { app.setInterruptPeeker(nullptr); }
  } interruptPeekerGuard{app};
  app.setInterruptPeeker([&app, &gui]() {
    // pumpEvents()の「閉じられた」は一度きり(次の呼び出しでは再び
    // trueを返す)なので、ここで消費した分をループ側が見落とさないよう
    // 終了要求に変えておく(ループ先頭のwantsExit()で抜ける)。
    if (!gui.pumpEvents()) {
      app.requestImmediateExit();
      return true;
    }
    bool anyKey = false;
    while (gui.consumeKeyEvent()) anyKey = true;
    return anyKey;
  });
  bool dragging = false;
  // 2026-09-29追加、直接のユーザー依頼("左右方向の画面ドラッグを実装で"): EPUBの縦書きの
  // 本文の左右ドラッグ(Application::epubVerticalDragActive())。上下のドラッグ
  // (dragging/renderScrolled())とは別に持つ――縦書きの画面は横倒し等をマスの描画
  // (GraphicalDisplay::renderCell())で行うので、行単位のrenderScrolled()は通さず、
  // 本文をepubDragOffsetピクセルだけ横にずらして描き(setContentOffsetX())、1列の幅を
  // 越えるたびにApplication::scrollEpubVertical()で1列動かす。離したら近い列にそろえる。
  bool epubDragging = false;
  int epubDragOffset = 0;
  // 左右ドラッグの慣性(2026-09-29、直接のユーザー依頼"左右ドラッグにも慣性スクロールを付けて"):
  // 上下のcoastLevel/coastDirection/recentMotionと同じ規則(離す直前kReleaseWindowの移動量で
  // 速さの段階を決め、一定の速さで1ドットずつ流し続ける。本の端か、ボタンを押すと止まる)。
  int epubCoastLevel = 0;
  int epubCoastDirection = 0;
  std::deque<std::pair<std::chrono::steady_clock::time_point, int>> epubRecentMotion;
  // 1列の幅を越えた時に列を1列動かす(dirは+1で先へ)。2026-09-29、直接のユーザー報告
  // ("スクロール中に画面のちらつきがある。モードラインは正常"): Application::
  // scrollEpubVertical()の描き直し(ContentView::redraw())はその場で画面へ出す(flush)ので、
  // 以前のように「列を動かす→ずれを1列分戻す」の順だと、ずれを戻す前の(1列分余計にずれた)
  // 絵が一瞬出ていた。先にずれを戻してから動かす(動けなければ元に戻してfalse)。
  // 横にずらす行(モードラインの行を除く)。2026-09-30、直接のユーザー報告("^Oh後はさらに
  // カオス"): rows()-1に決め打ちしていたので、^Ohでモードラインを隠すと最下行だけずれずに
  // 残っていた。
  auto epubContentRows = [&]() { return gui.modeLineVisible() ? std::max(gui.rows() - 1, 0) : gui.rows(); };
  auto shiftEpubColumn = [&](int dir) {
    int pitch = std::max(1, gui.cellWidthPixels() * fm::epubVerticalColumnPitch());
    epubDragOffset -= dir * pitch;
    gui.setContentOffsetX(epubDragOffset, epubContentRows());
    if (app.scrollEpubVertical(dir)) return true;
    epubDragOffset += dir * pitch;
    gui.setContentOffsetX(epubDragOffset, epubContentRows());
    return false;
  };
  // 2026-08-30、直接のユーザー報告(GUI、ARCモードで再現させたスクリー
  // ンショット付き――ただし根本原因はモードを問わない): マウスボタンを
  // 押した「だけ」で(実際には全く動かしていなくても)dragging=trueに
  // なり、その間render-mode分岐(下の`if (dragging || ...)`)が
  // renderScrolled()(通常のContentView::renderLine()を一切経由しない、
  // 完全に別の描画経路)へ切り替わってしまっていた――「...」省略、
  // 濁点合成(commitGlyph())、モードラインのsplitLayoutWhenCrowded、
  // どれもrenderScrolled()側には実装が無いため、末尾切れ表示・濁点が
  // 単体グリフとして分離/消失・モードラインの位置表示が誤って
  // 「END」になり末尾フィールドが消える、という3つの症状が同時に
  // 出ていた。ボタンを押した時点では「素のクリックか本物のドラッグ
  // か」を区別できないので、実際にMotionイベントが1回でも届くまでは
  // dragging自体を立てない――buttonHeldPendingDrag(下のDragEvent::
  // ButtonDown/Motion/ButtonUp参照)。
  bool buttonHeldPendingDrag = false;
  // 2026-09-15追加、直接のユーザー提案("スクロールのさなかに見つけた
  // 目的のファイル上でクリックすればその時点で内容が表示されるって
  // のはどう？"): ContentView::stepTopIndex()自身のコメント参照
  // ――ドラッグ/コースト中は1行分の境界を越えるたびにcursorIndex_が
  // ビューポートの下端(または上端)へ明示的に再固定される。つまり
  // スクロール中ずっと「今どのエントリがハイライトされているか」は
  // 正確に追跡され続けている一方、コーストを止めるための押下位置
  // (生のマウス座標)は、ユーザーが目で追っていたそのハイライト行と
  // 一致するとは限らない――むしろ普通は一致しない(ハイライトは
  // 固定行、マウスは任意の位置)。このフラグは、コースト中に押下
  // されたことを覚えておき、実際にドラッグへ発展しなかった(=
  // 単なる「掴んで止める」)場合に限り、解放時の遅延クリックの対象
  // 行を、生のマウス位置ではなくその時点のcursorIndex_(スクロール
  // 中ずっと見えていたハイライト行)へ差し替えるために使う。
  bool clickInterruptedCoast = false;
  // 2026-10-02追加、直接のユーザー依頼(=hw、"-g→^X^Fで下向きスクロール中、左クリックで停止...
  // マウスを動かしていないのでハイライト行も変わってない。このままで左クリックした場合でも
  // [ハイライト行]を開いてほしい"): 慣性を止めるだけのクリック(stopOnly)の後、マウスを動かす
  // (ホバー)かキー/ホイールを使うまでは、次の左クリックをポインタの行ではなくハイライト行
  // (cursorIndex_)へのクリックとして扱う(clickInterruptedCoastの!stopOnlyと同じ差し替え)。
  bool clickTargetsCursor = false;
  int pixelOffset = 0;      // sub-row offset; -cellHeight < pixelOffset < cellHeight
  int coastLevel = 0;       // 0 == not coasting; otherwise an index into fm::kScrollSpeedLevels
  int coastDirection = 0;   // +1 or -1 while coastLevel > 0, matching renderScrolled()'s pixelOffsetY sign
  // 2026-09-07追加、直接のユーザー報告("バーが最下点に達した瞬間...
  // 「00023/00025 END」が一瞬出てしまう...ちらつきが復活"、さらに
  // ContentView::scrollModeLineSegments()自身の"bottomVisible>=total
  // でEND"という判定ロジックを直接確認して特定): 「境界に当たった時
  // だけ1回先読みする」という遅延評価(直前の複数回の訂正)は、実際の
  // 移動量を超えて読み込んでしまう問題は解決したが、代わりに「境界の
  // ちょうど真上でしか読み込まない」ため、毎フレームの
  // scrollModeLineSegments()自身の判定(topIndex_+height_>=lines_.size()
  // ならEND)が、この関数自身のtopIndex_更新「直後」の、まだ次の
  // 1行を読み足す「前」の一瞬だけ、正確にEND条件を満たしてしまう
  // ――legacy自身の実機`U`で確認した「current+2」のような、常に
  // わずかな余裕(0ではない)を保つ挙動とは異なり、この移植は「余裕が
  // 0になった時だけ+1補充する」形になっていたため、補充が間に合う
  // 前にちょうどその1フレームだけ描画されてしまっていた。
  // keyboard駆動のjキー(Command::ScrollLineDown)がpollPagerExitNotice()
  // の力を借りずとも常に正しいのは、1回のキー入力につき「境界に当たる
  // 前から」毎回無条件に先読みを試みる(前回の実際のtopIndex_の位置を
  // このusedForMarginTopIndex_相当の変数で記憶し、動くたびに1回だけ)
  // ためで、それと同じ「実際にtopIndex_が動くたびに1回だけ、無条件に
  // (境界に当たったかどうかに関わらず)先読みしておく」という頻度を、
  // ここでも再現する――pollPagerExitNotice()自身のlastPagerPollTopIndex_
  // と全く同じ役割の、コースト/ドラッグ専用のローカルな記憶。
  int lastMarginPreloadTopIndex = -1;
  // 2026-09-24追加、直接のユーザー報告("bash^M以後、プロンプトが
  // haa15030@mpi:~$になる。^Mを押さないとそれが出ない"):
  // Shell::flushIdleOutput()自身の宣言コメント参照(CUI側のpoll()の
  // 100msタイムアウトと同じ役割)。GUIのフレームループはpoll(&fd,1,0)
  // を毎フレーム(概ね16ms間隔)ノンブロッキングで呼ぶだけで、CUI側の
  // ような「タイムアウト=真に静かになった」という自然な合図が無い
  // ため、最後にPTYから実際にバイトを読めた時刻を自前で記録し、
  // 一定時間(CUI側と同じ100ms)新しいバイトが来ていなければ「静か」
  // とみなす。
  auto lastShellPtyActivity = std::chrono::steady_clock::now();
  // Set on any frame that renders via renderScrolled() (dragging or
  // coasting); checked on the FIRST frame back on the normal redraw()
  // path so that settle-transition frame's redraw() always happens
  // even if nothing else occurred that frame (a coast that stops
  // exactly at a list boundary, with no further key/click/drag event,
  // would otherwise leave stale renderScrolled() output on screen
  // forever -- see the render-mode branch below).
  bool wasActivelyScrolling = false;
  // 2026-09-25追加、renderScrolled()へ渡す行バッファ――下のレンダ分岐
  // 自身のコメント参照。フレームをまたいで使い回し、実際に中身を
  // 詰めたのは[scrolledFilledBegin, scrolledFilledEnd)だけ。
  std::vector<fm::Display::ScrolledLine> scrolledLines;
  int scrolledFilledBegin = 0;
  int scrolledFilledEnd = 0;
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
    if (gTerminationSignalReceived || transientParentGone()) {
      app.requestImmediateExit();
      return;
    }
    // the window's own close button was clicked。(2026-10-11〜10-20: -H/-KのHELP・KEYの子は閉じなかった。
    // 2026-10-20、直接のユーザー提案"すべての子は自身を終了可"で、どの子も閉じられるように。)
    if (!gui.pumpEvents()) return;
    if (app.takeFontListRequest()) app.showFontList(buildFontList(gui));  // ^J^F(buildFontList()参照)
    if (app.takeRaiseRequest()) gui.raise();  // 子が開いたら親が前へ(Application::requestParentRaise())
    if (auto choice = app.takeFontChoice()) applyFontChoice(app, gui, choice->first, choice->second);
    // 最初の起動のインストールを入れ終えた(Application::takeFirstStartInstallDone()): 入ったIPAゴシックへ替えてから
    // HELPへ戻る(usilicrcのFONTには書かない。次からはresolveGuiFontPath()がipagを選ぶ)。
    if (app.takeFirstStartInstallDone()) {
      if (std::string ipag = ipaGothicFontPath();
          !ipag.empty() && gui.currentFontPath(fm::GraphicalDisplay::FontKind::Base) != ipag)
        gui.setFont(fm::GraphicalDisplay::FontKind::Base, ipag);
      app.finishFirstStartInstall();
    }
    // 2026-10-20、直接のユーザー依頼("リストの真っ先にipafontを持ってきてインストールが済みしだいその場でフォント
    // 変更後、他のインストールを継続"): 表紙を出している間(最初の起動のインストール)は1秒ごとにIPAゴシックを探し、
    // 入ったらその場でフォントを替えて表紙を描き直す(残りのbrew installはそのまま続く)。
    if (app.shellBannerShowing()) {
      static auto lastProbe = std::chrono::steady_clock::time_point{};
      auto now = std::chrono::steady_clock::now();
      if (now - lastProbe >= std::chrono::seconds(1)) {
        lastProbe = now;
        if (std::string ipag = ipaGothicFontFile();
            !ipag.empty() && gui.currentFontPath(fm::GraphicalDisplay::FontKind::Base) != ipag &&
            gui.setFont(fm::GraphicalDisplay::FontKind::Base, ipag))
          app.refreshShellBanner();
      }
    }
    // ^[[ / ^[](2026-10-17、Application::takeLineHeightAdjustRequest()参照): 今の向きの行の高さをドット単位で。
    if (int delta = app.takeLineHeightAdjustRequest()) {
      auto [pixels, ratio] = gui.adjustLineHeight(delta);
      char ratioText[16];
      std::snprintf(ratioText, sizeof ratioText, "%.2f", ratio);
      app.showNotice(std::string(app.verticalLayoutShown() ? "LINE-HEIGHT (vertical): " : "LINE-HEIGHT: ") +
                     std::to_string(pixels) + " dots (" + ratioText + ")");
    }
    // ^[{ / ^[}(2026-10-20、Application::takeLetterSpacingAdjustRequest()参照): 今の向きの字間をドット単位で。
    if (int delta = app.takeLetterSpacingAdjustRequest()) {
      auto [width, extra] = gui.adjustLetterSpacing(delta);
      app.showNotice(std::string(app.verticalLayoutShown() ? "LETTER-SPACING (vertical): " : "LETTER-SPACING: ") +
                     (extra > 0 ? "+" : "") + std::to_string(extra) + " dots (" + std::to_string(width) +
                     " dots per column)");
      // 2026-10-21、直接のユーザー依頼("字間の設定項目(リアルタイム更新)をusilicrcに追加"、^T^Hで開いたHELPが前回の
      // 保存値のままだった): その場でusilicrcのLETTER-SPACINGへ書き込む(子は起動時にusilicrcを読む)。
      auto [horizontal, vertical] = fm::GraphicalDisplay::letterSpacingExtras();
      const char *home = std::getenv("HOME");
      const char *section = fm::Configuration::letterSpacingSection();  // アイコンからの起動はICON-LETTER-SPACING
      if (!fm::usilicrc::setSection(home ? home : "", section,
                                    {std::to_string(horizontal) + "  " + std::to_string(vertical)}))
        app.showNotice(std::string("Cannot write ") + section + " to usilicrc");
    }
    // usilicrcを読み直した(EDITでusilicrcを直した・^Xs)なら、LINE-HEIGHTをその場で当て直す。
    if (auto ratios = app.takeLineHeightReloadRequest()) {
      fm::GraphicalDisplay::setLineHeightRatios(ratios->first, ratios->second);
      gui.reapplyLineHeight();
      if (auto extras = app.letterSpacingFromRc()) {  // LETTER-SPACINGも(2026-10-21)
        fm::GraphicalDisplay::setLetterSpacingExtras(extras->first, extras->second);
        gui.adjustLetterSpacing(0);
      }
    }
    // See Application::awaitingLocateUpdateDb()'s own comment: this is
    // exactly the call whose ABSENCE (before this fix) let a real ^G
    // click/keypress get queued by SDL but never processed -- a
    // detached `updatedb` launched by ^Jl is now noticed and chained
    // into the LOCATE prompt every frame, same as -c's own poll loop.
    app.pollLocateUpdateDbCompletion();
    app.pollDuCompletion();
    app.pollArchiveLoadCompletion();
    app.pollPendingCpMvRmShellRun();
    app.pollPendingArchiveExtractShellRun();
    app.pollShellOfferFollowUp();
    app.pollShellHandoff();
    app.pollChildChannel();  // -KのKEYの^M・hの受け渡し(Application::openChildChannel()参照)
    app.pollPrefixEcho();  // PREFIXキーの「C-x-」等(Application::pendingPrefixLabel()参照)
    if (app.consumeGuiModeToggleRequest()) return;  // 親の^Ogの"G"(Application::toggleGuiModeFromParent())
    // `suppressDraw`(2026-09-07追加、直接のユーザー報告"わかったのは
    // ブロックカーソル位置を画面上真ん中辺に変更すると、瞬時の表示も
    // 同じ位置（真ん中辺）になされる点"): Application::pollPagerExitNotice()
    // 自身の宣言コメント参照――内部のensurePagerStreamLoaded()が新しい
    // 内容を読み込めるたびにactiveView().redraw()+flush()を即座に行う
    // ため、これを抑制せずに毎フレーム無条件で呼ぶと、下のrender-mode
    // 分岐がrenderScrolled()で描画するのと全く同じフレームの中で、
    // その「前」にこの通常のredraw()が一瞬だけ実際にSDLへ提示されて
    // しまっていた――これがモードラインの値・ハイライト位置ともに
    // 全く別の内容が一瞬だけ挟まって見える症状の真因だった(canStepTopIndex()
    // 側のタイミング解析に基づく直前2回の修正(6fd4c36、0c2e5c2)は
    // この経路には無関係で効かなかった)。このフレーム自身がこの後
    // renderScrolled()で確実に上書き提示することが分かっている間
    // (=直前のフレーム終了時点で既にdragging/coastLevel/pixelOffsetの
    // いずれかが立っていた間、下のrender-mode分岐と全く同じ条件)は
    // 抑制し、読み込みだけ行って描画はrenderScrolled()に委ねる。CUI側
    // (runTerminalPhase()自身の同名の無引数呼び出し)はこの経路を持た
    // ないため、デフォルトfalseのまま影響を受けない。
    app.pollPagerExitNotice(dragging || coastLevel > 0 || pixelOffset != 0);

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
    // GraphicalDisplay::consumeDroppedFiles()/Application::openDroppedPaths()
    // 自身の宣言コメント参照(2026-09-25追加)。
    {
      std::vector<std::string> dropped = gui.consumeDroppedFiles();
      if (!dropped.empty()) {
        app.openDroppedPaths(dropped);
        gui.raise();
        anyEventThisFrame = true;
      }
    }
    // 2026-08-24 fix (docs/CHANGES.md item 422, see GraphicalDisplay::
    // consumeNeedsRedraw()'s own declaration comment for the full
    // story): a real user report against `make app`'s new .app bundle
    // -- launched via Finder/`open`, the window sometimes stayed solid
    // black forever, no crash, no hang (confirmed via `sample`, the
    // run loop was idling normally). Root cause was this exact
    // anyEventThisFrame optimization: the very first frame's own
    // redraw can race against the window actually being mapped by
    // WindowServer (slower/more involved for a real bundle than a bare
    // executable's simpler LaunchServices bootstrap), and once that
    // first frame is lost, nothing else in this loop ever forces
    // another one without a live click/key/drag. Folded in here,
    // first, so it's just one more "did something happen" source this
    // frame -- the exact same anyEventThisFrame gate below already
    // does the right thing once this is true.
    if (gui.consumeNeedsRedraw()) anyEventThisFrame = true;
    // 2026-08-24 addition (docs/CHANGES.md item 425, a direct user
    // request: window resize/color/font weren't live-adjustable at
    // all): the window is now created SDL_WINDOW_RESIZABLE
    // (GraphicalDisplay::create()'s own comment) -- a live drag-resize
    // recomputes rows()/columns() (GraphicalDisplay::
    // handleWindowResized()) the moment pumpEvents() sees
    // SDL_WINDOWEVENT_RESIZED, same as this loop's own
    // consumeNeedsRedraw() check just above; app.handleResize() here
    // is the exact same call runTerminalPhase()'s own
    // keyInput.consumeResizeSignal() branch already makes for a real
    // terminal's SIGWINCH -- it reads the Display's current rows()/
    // columns() itself, already updated by the time this runs.
    // 2026-10-16(GraphicalDisplay::setVerticalLineSpacing()参照): 縦書きの画面では縦書きの字の送りの行の高さに。
    gui.setVerticalLineSpacing(app.verticalLayoutShown());
    if (gui.consumeSizeChanged()) {
      app.handleResize();
      anyEventThisFrame = true;
    }

    while (auto click = gui.consumeMouseClick()) {
      anyEventThisFrame = true;
      app.noteUserActivity();  // マウスに触れたら一時的な表示を消す(Application::noteUserActivity()参照)
      // 2026-09-15、直接のユーザー確認+依頼を経て現在の形になった
      // (この節自身の過去のコメントは2026-08-05のマージン方式を
      // 説明していたが、その後2回の変更で置き換わったので刷新する):
      //
      // (1) 左クリックの「実行」自体はもうここでは押下即座ではない
      //     ――GraphicalDisplay::Impl::pendingLeftClick自身のコメント
      //     参照、ボタン解放まで遅延し、mouseQueueにはその時点で
      //     初めて積まれる。
      // (2) その解放遅延だけで実際に「操作感は変わらない」と実機
      //     確認できたため、2026-08-05にDirectory/ARC/DUの一覧へ
      //     導入していた左マージン(ドラッグ専用の安全地帯)除外は
      //     不要と判明し撤去した(Application::handleMouseClick()の
      //     Directory/Archive/Du分岐、ContentView::
      //     isColumnWithinRowText()自身のコメント参照)。
      // (3) しかしマージンが消えたことで、ファイル名/パスの「真上」
      //     で押してそのままドラッグし、離した場合に「押した時点の
      //     エントリ」が離した瞬間に開いてしまう新しい問題が実機で
      //     確認された。ユーザー自身の言葉: 「実際にドラッグと確定
      //     したら、離した時のクリックは実行しない」――ただし同じ
      //     ユーザー自身が指摘した通り、この「確定」判定そのものが
      //     2026-08-05に2回失敗した領域(ジッターに弱いタイミング/
      //     モーション判定)と同じ弱点を抱え得ることは承知の上での
      //     実験。ここでは新しいヒューリスティックを発明せず、この
      //     ファイル自身が既に別の目的(renderScrolled()への切替、
      //     hover抑制、コースト開始判定)で使っている`dragging`
      //     (「実際にMotionイベントが最低1回届いた」ことだけを条件に
      //     trueになる、buttonHeldPendingDrag自身のコメント参照)を
      //     そのまま再利用する――このクリックのButtonUpとペアになる
      //     DragEvent::ButtonUpは、このmouseQueue消費より後(すぐ下)
      //     で処理されるため、ここではまだ「直前のフレームまでに
      //     本物のドラッグが確定していたかどうか」を正しく読める。
      if (!click->rightButton && !dragging && !epubDragging) {  // 左右ドラッグの離しはクリックにしない
        // clickInterruptedCoast自身の宣言コメント参照、直接のユーザー
        // 提案("スクロールのさなかに見つけた目的のファイル上で
        // クリックすればその時点で内容が表示されるってのはどう？"):
        // このクリックがコースト中の押下から来ていて、かつ実際には
        // ドラッグへ発展しなかった(=単なる「掴んで止める」)なら、
        // 生のマウス位置(click->row/column、コースト中ずっと動いて
        // いた実際のマウスの位置で、ユーザーが目で追っていたハイ
        // ライト行とは普通一致しない)ではなく、その時点の
        // cursorIndex_(ContentView::stepTopIndex()が1行境界を越える
        // たびにビューポートの下端/上端へ明示的に再固定してきた、
        // スクロール中ずっと見えていたハイライト行)を対象にする。
        // その行はcursorIndex()そのものなので、Application::
        // handleMouseClick()内のidx==cursorIndex()判定(「2回目の
        // クリック」相当)に自然に合致し、選択だけでなくその場で直接
        // オープンまで進む――ユーザーの言う「統合」そのもの。
        int row = click->row;
        int column = click->column;
        // 2026-09-25追加、直接のユーザー要望("マウスで慣性スクロールを
        // 停止「だけ」する手段がない。画面左端のようにハイライト範囲外
        // で左クリックすればスクロールが停止する機能を実装してほしい"):
        // 上の「統合」はコースト中の押下を常にハイライト行のオープンへ
        // 差し替えていたため、止めるだけの手段が無くなっていた。生の
        // 押下位置が、その位置にある行自身のハイライト範囲
        // (isColumnWithinLineScope()、lsの権限/サイズ/日付の左マージンや
        // 短いエントリ名の後ろの空白は範囲外)に入っていなければ、
        // 押下(ButtonDown)でコーストが止まった時点で役目は終わりとし、
        // クリックは実行しない。
        bool stopOnly = false;
        if (clickInterruptedCoast) {
          fm::ContentView &v = app.activeContentView();
          int rawIdx = v.lineIndexAtRow(click->row);
          stopOnly = rawIdx < 0 || !v.isColumnWithinLineScope(rawIdx, click->column);
        }
        if ((clickInterruptedCoast && !stopOnly) || (!clickInterruptedCoast && clickTargetsCursor)) {
          int cursorRow = app.activeContentView().cursorDisplayRow();
          if (cursorRow >= 0) {
            row = cursorRow;
            // 生のクリック列は元々マウスが乗っていた行のテキスト長を
            // 基準にしたものなので、行を差し替えた後もそのまま使うと
            // (cursorRow側の行がそれより短ければ)スコープ判定に
            // 無関係に落ちかねない。0は今やisColumnWithinRowText()の
            // 上限チェックだけなので、空行でない限り常に安全。
            column = 0;
          }
        }
        if (!stopOnly) app.handleMouseClick(row, column);
        clickTargetsCursor = stopOnly;
      } else if (click->rightButton && !dragging) {
        // 2026-09-07追加、直接のユーザー要望("右クリックに`^G`の機能を
        // 持たせて欲しい"): app.feedByte()経由で本物のキーボード^G
        // (kControlG、application.cpp)と全く同じ経路を通す――
        // Command::Backへ直接ディスパッチするのではなく、awaitingSequenceOrPromptByte()
        // 等、キーボード^Gが普段受ける全てのガード/前処理をそのまま
        // 受けさせるため。左クリックと同じく`!dragging`で、コースト/
        // ドラッグ中の右クリックは無視する(左クリックの同種のガードと
        // 揃える、意図しない「掴んで止める」中の暴発を避ける)。
        app.feedCanonicalByte(0x07);  // usilicrcのKEYで^Gを変えても右クリックは「戻る」のまま
      }
      clickInterruptedCoast = false;
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
    if (!dragging && coastLevel == 0 && app.directEntryListActive()) {
      // 2026-10-02追加(Application::directEntryListActive()参照): ^X^F等の直接入力の一覧の
      // ホバーは、下の通常のホバー(ContentView::redraw()でモードラインを上書きする)ではなく
      // hoverDirectEntryList()で。
      while (auto hover = gui.consumeHoverEvent()) {
        anyEventThisFrame = true;
        app.noteUserActivity();  // マウスに触れたら一時的な表示を消す(Application::noteUserActivity()参照)
        clickTargetsCursor = false;
        app.hoverDirectEntryList(hover->row);
      }
    } else if (!dragging && coastLevel == 0 && app.currentMode() != fm::ApplicationMode::Shell &&
        !app.redrawSensitivePromptOrRecallActive() && !app.awaitingLocateUpdateDb() && !app.awaitingDu() && !app.awaitingArchiveLoad()) {
      // !app.awaitingLocateUpdateDb() (2026-08-15 fix): see the render-
      // mode branch's own identical addition below -- a mouse wiggle
      // during a real, still-running `updatedb` wait would otherwise
      // clobber "Updating database..." via this same redraw()-inside-
      // applyHoverRowAndRedraw() path too.
      // !app.awaitingDu() (2026-09-19 addition): the exact same risk,
      // now that `du` also runs asynchronously with its own "Disk
      // usage..." wait message -- see Application::awaitingDu()'s own
      // comment.
      while (auto hover = gui.consumeHoverEvent()) {
        anyEventThisFrame = true;
        app.noteUserActivity();  // マウスに触れたら一時的な表示を消す(Application::noteUserActivity()参照)
        // Real mouse movement clears DirectoryMode's own transient
        // "Invalid mode" error (see DirectoryMode::clearEditError()'s
        // own comment on why THIS call site, alongside Application::
        // handleCommand()'s, is safe -- unlike the ~60Hz idle loop's
        // own incidental redraws, this only runs when SDL actually
        // reported real motion) -- a real user report: hover here also
        // moves cursorIndex_ (moveCursor=true below), so leaving a
        // stale message about the PREVIOUS selection on screen while
        // the effective selection has already moved on is confusing,
        // the same reasoning keyboard ^N/^P already gets via
        // handleCommand(). Harmless outside Directory mode.
        app.directoryMode().clearEditError();
        clickTargetsCursor = false;
        applyHoverRowAndRedraw(app.activeContentView(), hover->row, /*moveCursor=*/true);
        // 2026-09-02、直接のユーザー報告(=fg/=fh、慣性スクロールが
        // 止まった直後、ドラッグ無しでマウスを動かすだけでハイライト
        // が明滅し画面が「振動」する、j/k等キー入力で正常化する件):
        // 原因調査用の一時的なダンプ。マウスの生の行(hover->row)と、
        // それがlineIndexAtRow()経由でContentView::hoverIndex_/
        // cursorIndex_/topIndex_へどう反映されたかを、実機で
        // USILICGUIDEBUGを付けて再現してもらうことで直接確認する
        // ためのもの――このサンドボックスには本物のマウスが無く、
        // 憶測で直すと=fdの時と同じ轍を踏む(幅の不一致という誤った
        // 仮説で一度コミットしてしまった)ため、まず実データを取る。
        if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
          FILE *f = std::fopen(dbg, "a");
          if (f) {
            fm::ContentView &v = app.activeContentView();
            std::fprintf(f,
                         "---hover--- row=%d hoverIndex=%d cursorIndex=%d topIndex=%d "
                         "dragging=%d coastLevel=%d pixelOffset=%d wasActivelyScrolling=%d\n",
                         hover->row, v.hoverIndex().value_or(-1), v.cursorIndex(), v.topIndex(), dragging ? 1 : 0,
                         coastLevel, pixelOffset, wasActivelyScrolling ? 1 : 0);
            std::fclose(f);
          }
        }
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
      app.noteUserActivity();  // ←→の慣性の開始等、feedByte()を通らないキーでも一時的な表示を消す
      anyEventThisFrame = true;
      clickTargetsCursor = false;
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

      // runProgramInLaunchingTerminal()のクイックルック(2026-09-29)参照: 裏で動いている
      // qlmanageは^[/^G/^Cで閉じる(そのキーはusilicへは渡さない)。
      if (!key->special && (key->byte == 0x1b || key->byte == 0x07 || key->byte == 0x03) &&
          stopBackgroundQuickLook()) {
        consecutiveEscapes = 0;
        continue;
      }
      bool upKey = key->special == fm::Display::Key::Up;
      bool downKey = key->special == fm::Display::Key::Down;
      // 2026-10-02追加、直接のユーザー依頼("←→で開始でいい。いずれのキーも速度減少時、最低速を
      // 超えたら逆向き最低速、以後は加速"): ←→で慣性スクロールを始め/速さを変える(stepCoastByKey()、
      // →で先へ)。縦書きの本文の左右の慣性では↓(先へ)/↑。2026-08-07からの「慣性中の↑↓で加速/
      // 最低速で逆向き」はこれに置き換えた(以後↑↓は慣性を止めて1行動く、他のキーと同じ)。
      if (app.currentMode() != fm::ApplicationMode::Shell && app.currentMode() != fm::ApplicationMode::Copy &&
          !app.awaitingSequenceOrPromptByte()) {
        bool leftKey = key->special == fm::Display::Key::Left;
        bool rightKey = key->special == fm::Display::Key::Right;
        // 2026-10-06追加、直接のユーザー提案("縦横の方向が変わるとどの矢印がそれなのかわからなくなる。
        // しかもスクロール中の矢印キーは行移動としての機能が果たせず無意味なキー...スクロールが始まったら
        // 機能を速度調整に切り換えたらどうかなと。開始に限り上下左右を交換で"): 慣性中は、進む向きの
        // 矢印(一覧の↓↑、縦書きの本文の←→)でも速さを変える。開始は今まで通り交差する向き(一覧は←→、
        // 縦書きは↓↑)だけ。
        if (app.epubVerticalDragActive() && epubCoastLevel > 0 && (leftKey || rightKey)) {
          stepCoastByKey(epubCoastLevel, epubCoastDirection, leftKey ? 1 : -1, guiSpeedMaxLevel());  // ←で先へ(右から左へ読む)
          consecutiveEscapes = 0;
          continue;
        }
        if (!app.epubVerticalScrollActive() && coastLevel > 0 && (upKey || downKey)) {
          // ↑で先へ(-1、coastDirection参照)。2026-10-21、直接のユーザー報告(runTerminalCoast()の同じ日の直し参照): 以前は↓。
          stepCoastByKey(coastLevel, coastDirection, upKey ? -1 : 1, guiSpeedMaxLevel());
          consecutiveEscapes = 0;
          continue;
        }
        if (app.epubVerticalDragActive() && (upKey || downKey)) {
          stepCoastByKey(epubCoastLevel, epubCoastDirection, downKey ? 1 : -1, guiSpeedMaxLevel());
          consecutiveEscapes = 0;
          continue;
        }
        if (!app.epubVerticalScrollActive() && (leftKey || rightKey)) {
          if (coastLevel == 0) app.activeContentView().setHoverIndex(std::nullopt);  // ホイールで始めていた時と同じ(2026-09-03)
          bool forwardKey = app.leftArrowAdvances() ? leftKey : rightKey;  // Application::leftArrowAdvances()参照
          stepCoastByKey(coastLevel, coastDirection, forwardKey ? -1 : 1, guiSpeedMaxLevel());  // coastDirection自身のコメント参照: -1で先へ
          consecutiveEscapes = 0;
          continue;
        }
      }
      if (key->special) {
        consecutiveEscapes = 0;
        // Any other real key stops      } else if (key->special) {
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
        //
        // 2026-08-15 fix: this branch used to ONLY track
        // consecutiveEscapes and never call app.feedByte() at all -- a
        // real report ("^[は相変わらず反応無し", specifically in -g,
        // confirmed fine in -c) traced back to exactly this: EVERY
        // ESC-leader sequence (^[s/SHELL, ^[v/PageUp, ^[</^[>, ...)
        // was silently swallowed here before ever reaching Application,
        // in -g only -- the terminal path (runTerminalPhase(), a few
        // hundred lines up) already forwards ESC via this exact same
        // app.feedByte(0x1b, std::nullopt) call, so this was a real gap
        // specific to -g's own separate key-handling code, not a
        // property of ESC-leader sequences themselves (confirmed
        // working via direct testing in -c and via unit tests). Setting
        // escPending_ this way is harmless for the exit gesture itself:
        // a second ESC lands in dispatchLeaderByte()'s own escPending_
        // switch as an unrecognized sequence (a no-op, resetting
        // escPending_ back to false), and consecutiveEscapes/Enter's
        // own check just below is entirely independent of Application's
        // internal state.
        consecutiveEscapes = nextConsecutiveEscapes(consecutiveEscapes);
        app.feedByte(0x1b, std::nullopt);
      } else if (key->byte == 0x0d && consecutiveEscapes >= 2) {
        app.requestImmediateExit();
      } else if (key->byte == 0x0d && (coastLevel > 0 || epubCoastLevel > 0)) {
        // 2026-10-06追加、直接のユーザー依頼("Enterでも慣性スクロール停止(ハイライト上でも
        // 停止だけ)"): 慣性中のEnterは止めるだけで、ハイライト行は開かない(ハイライト範囲外
        // の左クリックのstopOnlyと同じ)。
        consecutiveEscapes = 0;
        syncCursorBeforeCoastStoppingKey(coastLevel, wasActivelyScrolling);
        coastLevel = 0;
        pixelOffset = 0;  // see the key->special branch's comment above
        if (epubCoastLevel > 0) {
          epubCoastLevel = 0;
          epubCoastDirection = 0;
          epubDragOffset = 0;
          gui.setContentOffsetX(0, 0);
        }
      } else {
        consecutiveEscapes = 0;
        syncCursorBeforeCoastStoppingKey(coastLevel, wasActivelyScrolling);
        coastLevel = 0;
        pixelOffset = 0;  // see the key->special branch's comment above
        app.feedByte(key->byte, std::nullopt);
      }
      if (app.consumeGuiModeToggleRequest()) return;  // ^Ogで端末へ(Command::ToggleGuiMode参照)
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
      app.noteUserActivity();  // マウスに触れたら一時的な表示を消す(Application::noteUserActivity()参照)
      clickTargetsCursor = false;
      if (app.currentMode() == fm::ApplicationMode::Shell || app.awaitingSequenceOrPromptByte()) continue;
      // 2026-10-02、直接のユーザー依頼("GUIともどもホイールを本来の機能（画面スクロール）に戻して
      // ほしい"): 2026-08-05からホイールは慣性スクロールの開始/速度調整だったが、普段の画面
      // スクロールの操作で慣性が始まって混乱するため、1刻みごとに画面をスクロールするだけに戻す
      // (慣性中なら止める)。慣性の開始/速さは←→(stepCoastByKey())か左ドラッグを離す速さ。
      // EPUBの縦書き(Application::scrollEpubVertical())は1列(章の一覧は1章)ずつの横スクロール。
      // 縦のホイールは下で先へ。横(トラックパッド)は実機で逆とのユーザー報告("トラップパッドは
      // 方向が逆")により、deltaXが正で先へ。
      if (app.epubVerticalScrollActive()) {
        int delta = wheel->deltaY != 0 ? (wheel->deltaY > 0 ? -1 : 1) : (wheel->deltaX > 0 ? 1 : -1);
        if (app.wheelReversed() && wheel->deltaY != 0) delta = -delta;  // ^Od(Command::ToggleWheelDirection)
        if (epubCoastLevel > 0) {
          epubCoastLevel = 0;
          epubCoastDirection = 0;
          epubDragOffset = 0;
          gui.setContentOffsetX(0, 0);
        }
        app.scrollEpubVertical(delta);
        gui.flush();
        continue;
      }
      if (wheel->deltaY == 0) continue;  // 横だけのイベント(WheelEvent::deltaX参照)
      dragging = false;
      gui.setHandCursor(false);
      gui.setPointingCursor(false);
      coastLevel = 0;
      coastDirection = 0;
      pixelOffset = 0;
      // 符号: 実機確認(2026-10-02、直接のユーザー報告"-gでのホイールの回転がスクロール方向と逆")で、
      // deltaYが正なら先へ。
      scrollByWheel(app, gui, wheel->deltaY > 0 ? 1 : -1, std::abs(wheel->deltaY));
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
      app.noteUserActivity();  // マウスに触れたら一時的な表示を消す(Application::noteUserActivity()参照)
      // 縦書きの本文(Application::epubVerticalScrollbar()参照): 上端が本の最初、下端が本の
      // 最後。draggingは立てない(立てると行単位のrenderScrolled()で描き、縦書きのマスの
      // 描画を通らない)。
      if (app.epubVerticalDragActive()) {
        auto jumpEpub = [&](int y) {
          int trackHeight = epubContentRows() * cellHeight;
          if (trackHeight <= 0) return;
          app.jumpEpubVerticalToFraction(static_cast<double>(y - gui.marginYPixels()) / trackHeight);
        };
        switch (sb->type) {
          case fm::GraphicalDisplay::ScrollbarEventType::ButtonDown:
            epubCoastLevel = 0;  // 左右の慣性を止める
            epubCoastDirection = 0;
            epubDragOffset = 0;
            gui.setContentOffsetX(0, 0);
            gui.setPointingCursor(true);
            jumpEpub(sb->y);
            break;
          case fm::GraphicalDisplay::ScrollbarEventType::Motion:
            jumpEpub(sb->y);
            break;
          case fm::GraphicalDisplay::ScrollbarEventType::ButtonUp:
            gui.setPointingCursor(false);
            break;
        }
        continue;
      }
      switch (sb->type) {
        case fm::GraphicalDisplay::ScrollbarEventType::ButtonDown:
          // 2026-09-02 fix, a direct user report (=fg/=fh): confirmed via
          // a real USILICGUIDEBUG capture that ContentView::hoverIndex_
          // stays completely frozen at its pre-drag value for the WHOLE
          // duration of a coast/drag (topIndex_ 49->56, cursorIndex_
          // 65->78, hoverIndex_ stuck at 64 the entire time) -- nothing
          // ever clears or updates it while dragging/coasting (the
          // hover-consumption gate above only drains the QUEUE, see its
          // own comment; it never touches the stored hoverIndex_ value
          // itself). redraw()'s own highlight priority
          // (hoverIndex_.has_value() ? *hoverIndex_ : cursorIndex_)
          // then keeps insisting on this stale, likely now off-screen
          // row the instant coasting settles -- no highlight visible at
          // all (its row scrolled out of view) -- and once real mouse
          // motion resumes, each new hover event's own redraw() (which
          // finally sets a fresh, correct hoverIndex_) races against
          // this same frame's later "anyEventThisFrame" redraw() lower
          // down, which is otherwise harmless UNLESS something upstream
          // keeps re-reading the old value -- exactly the flicker/
          // "screen vibrates" symptom reported. Clearing it here (as
          // Application::handleCommand()'s Move/Page/GoTo dispatch and
          // ContentView::setLines() already do for the analogous "the
          // view moved out from under the stale hover position" cases)
          // means cursorIndex_ -- which stepTopIndex()/jumpToScrollbarY()
          // DO keep continuously correct throughout -- is what redraw()
          // falls back to the instant dragging/coasting ends, with
          // nothing stale left to fight it.
          view.setHoverIndex(std::nullopt);
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
          view.markJumpOrigin();  // ^X^X用(ContentView::markJumpOrigin()参照)、クリック前の行を記録
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
      app.noteUserActivity();  // マウスに触れたら一時的な表示を消す(Application::noteUserActivity()参照)
      // 2026-09-07追加、直接のユーザー要望("スクロールに関する画面
      // ドラッグと同じマウス操作に関して、発動し得る機能としては
      // 範囲指定がある。COPYモードの際にそれは可能だろうか？"):
      // COPYモードのスナップショットは元々静的な内容で、スクロール
      // のコースト/ドラッグ物理(pixelOffset/coastLevel等)を一切
      // 必要としない――同じ左ドラッグのジェスチャーを、この経路の
      // 「前」で完全に横取りし、Application::copyMouseDown()/
      // copyMouseDrag()/copyMouseUp()(いずれもcurrentMode()!=Copyの
      // 間は無害にno-op)へ委譲する。下のスクロール専用のswitch
      // (dragging/coastLevel/pixelOffset等を書き換える)には一切
      // 触れさせない――COPYモードの描画は元々ずっと通常のredraw()
      // 経由(CopyMode::render())で、renderScrolled()へ切り替わる
      // 理由が無いため。
      if (app.copyMouseActive()) {  // 縦書きのEPUBのCOPY(2026-10-13)も
        switch (drag->type) {
          case fm::GraphicalDisplay::DragEventType::ButtonDown:
            app.copyMouseDown(drag->row, drag->column);
            break;
          case fm::GraphicalDisplay::DragEventType::Motion:
            app.copyMouseDrag(drag->row, drag->column);
            break;
          case fm::GraphicalDisplay::DragEventType::ButtonUp:
            app.copyMouseUp();
            break;
        }
        continue;
      }
      switch (drag->type) {
        case fm::GraphicalDisplay::DragEventType::ButtonDown:
          if (epubCoastLevel > 0) {  // 左右の慣性の途中でつかむと、その場で止めてドラッグを続ける
            epubCoastLevel = 0;
            epubCoastDirection = 0;
            epubDragging = true;
          }
          epubRecentMotion.clear();
          // dragging自体はまだ立てない――buttonHeldPendingDrag自身の
          // コメント参照。coastLevel/coastDirection/recentMotion/
          // setHandCursor(true)は、以前と同じくボタンが押された時点
          // で即座にリセット/表示して構わない(これらは「本物の
          // ドラッグかどうか」に関わらず安全な、押した瞬間のフィード
          // バックのため)。
          buttonHeldPendingDrag = true;
          // clickInterruptedCoast自身の宣言コメント参照: coastLevelを
          // ゼロ化する「前」に読む。
          clickInterruptedCoast = coastLevel > 0;
          coastLevel = 0;  // grabbing mid-coast stops it right where it is
          coastDirection = 0;
          recentMotion.clear();
          gui.setHandCursor(true);
          break;
        case fm::GraphicalDisplay::DragEventType::ButtonUp: {
          if (epubDragging) {  // epubDragging自身のコメント参照
            int pitch = std::max(1, gui.cellWidthPixels() * fm::epubVerticalColumnPitch());
            auto cutoff = std::chrono::steady_clock::now() - kReleaseWindow;
            int releaseDistance = 0;
            for (auto &sample : epubRecentMotion)
              if (sample.first >= cutoff) releaseDistance += sample.second;
            epubCoastLevel =
                resolveCoastLevel(fm::levelForReleaseDistance(releaseDistance, std::max(gui.cellHeightPixels(), 1), false),
                                  guiSpeedMaxLevel());
            // 2026-10-10、直接のユーザー依頼("-gではスクロールが行ではなくドット単位なので2段ズラさなくとも
            // 滑らかにつながる。なので-gではズラし無しで"): マウスで始めた慣性の段のずらし
            // (fm::kDragArrowSpeedOffset)は端末(runCoastLoop())だけにし、-gではずらさない。
            if (epubCoastLevel > 0) {  // 慣性で流す(ずれはそのまま、下のフレームごとの処理)
              epubCoastDirection = releaseDistance > 0 ? 1 : -1;
              epubDragging = false;
              buttonHeldPendingDrag = false;
              gui.setHandCursor(false);
              break;
            }
            if (epubDragOffset >= pitch / 2) app.scrollEpubVertical(1);
            else if (epubDragOffset <= -pitch / 2) app.scrollEpubVertical(-1);
            epubDragging = false;
            epubDragOffset = 0;
            buttonHeldPendingDrag = false;
            gui.setContentOffsetX(0, 0);
            gui.setHandCursor(false);
            gui.flush();
            break;
          }
          // 2026-09-02 fix, a direct user report (=fg/=fh, confirmed via
          // a real USILICGUIDEBUG capture): the comment this replaces
          // assumed "if Motion never arrived, pixelOffset/coastLevel is
          // ALREADY 0" -- true for a plain click starting from rest, but
          // FALSE for a click that grabs mid-coast (ButtonDown's own
          // "grabbing mid-coast stops it right where it is" only zeroes
          // coastLevel/coastDirection, deliberately leaving pixelOffset
          // at whatever sub-row position the coast had reached, so a
          // REAL continued drag from there feels seamless). If that grab
          // then turns out to be just a quick re-tap -- no Motion before
          // this ButtonUp -- `wasRealDrag` is false, and the old code
          // skipped the whole block below, leaving that stale nonzero
          // pixelOffset (a real capture showed -23) permanently in
          // place: nothing else ever clears it (the coast-physics block
          // is gated on `coastLevel > 0`, already false here), so the
          // render-mode check below stays wedged on renderScrolled()
          // (no DirectoryMode highlight at all, ScrolledLine::attributes
          // is FileMode-only) forever, even though nothing is actually
          // animating any more -- exactly the reported "スクロールの
          // スイッチは入っていて...画面は進まない" symptom, and each
          // subsequent hover event's own correct redraw() then got
          // immediately overwritten by that same stuck renderScrolled()
          // frame later this same iteration, reading as the reported
          // flicker/"画面が振動する". Restructured so pixelOffset is
          // unconditionally settled to 0 whenever this release does NOT
          // result in an active coast, covering both the always-worked
          // "plain click" case and this previously-broken "grabbed
          // mid-coast, released without further motion" case the same
          // way.
          bool wasRealDrag = dragging;
          dragging = false;
          buttonHeldPendingDrag = false;
          gui.setHandCursor(false);
          if (wasRealDrag) {
            auto cutoff = std::chrono::steady_clock::now() - kReleaseWindow;
            int releaseDistance = 0;
            for (auto &sample : recentMotion)
              if (sample.first >= cutoff) releaseDistance += sample.second;
            // 段のずらし(fm::kDragArrowSpeedOffset)は-gではしない(上の縦書きの同日のコメント参照)。
            coastLevel = resolveCoastLevel(fm::levelForReleaseDistance(releaseDistance, cellHeight, false), guiSpeedMaxLevel());
            coastDirection = coastLevel > 0 ? (releaseDistance > 0 ? 1 : -1) : 0;
          } else {
            coastLevel = 0;
            coastDirection = 0;
          }
          // A release too slow/short to coast (coastLevel resolves to 0
          // here), a plain click, or a mid-coast re-grab released
          // without further motion -- all three leave nothing else that
          // will ever clear a leftover fractional pixelOffset, which
          // would otherwise wedge the render-mode check on
          // renderScrolled() forever (see the key->special branch's
          // comment above, and this case's own comment just above).
          if (coastLevel == 0) pixelOffset = 0;
          break;
        }
        case fm::GraphicalDisplay::DragEventType::Motion: {
          clickTargetsCursor = false;  // 本物のドラッグ(clickTargetsCursor自身のコメント参照)
          if ((buttonHeldPendingDrag || epubDragging) && !dragging && app.epubVerticalDragActive()) {
            // epubDragging自身のコメント参照。右へのドラッグで先へ(本文が右へ動き、左から
            // 次の列が出てくる)。本の端で動けなければずらさない。
            buttonHeldPendingDrag = false;
            epubDragging = true;
            int pitch = std::max(1, gui.cellWidthPixels() * fm::epubVerticalColumnPitch());
            epubDragOffset += drag->deltaX;
            epubRecentMotion.push_back({std::chrono::steady_clock::now(), drag->deltaX});
            while (!epubRecentMotion.empty() &&
                   epubRecentMotion.front().first < std::chrono::steady_clock::now() - kReleaseWindow)
              epubRecentMotion.pop_front();
            while (epubDragOffset >= pitch) {
              if (!shiftEpubColumn(1)) {
                epubDragOffset = 0;
                break;
              }
            }
            while (epubDragOffset <= -pitch) {
              if (!shiftEpubColumn(-1)) {
                epubDragOffset = 0;
                break;
              }
            }
            gui.setContentOffsetX(epubDragOffset, epubContentRows());
            gui.flush();
            break;
          }
          // buttonHeldPendingDrag自身のコメント参照: 実際にMotionが
          // 届いた時点で初めて「本物のドラッグ」と確定させる。
          if (buttonHeldPendingDrag && !dragging) {
            // See the identical fix/comment at the scrollbar ButtonDown
            // case just above -- a real content drag/coast is exactly
            // the same "view is about to move out from under the stale
            // hover position" case, confirmed via a real USILICGUIDEBUG
            // capture (hoverIndex_ frozen at 64 for the whole coast
            // while topIndex_/cursorIndex_ raced from 49 to 56+).
            view.setHoverIndex(std::nullopt);
            dragging = true;
            buttonHeldPendingDrag = false;
          }
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
          // 2026-09-06追加、直接のユーザー報告("2つ目のjではENDになら
          // ないが、慣性スクロールが同じくらいの位置に到達するとEND")
          // ――totalLines>contentRowsチェックがまだ読み込み済みでない
          // 内容を「そもそもスクロール可能ですらない」と誤判定して
          // いた。
          // 2026-09-06再訂正×2(直接のユーザー報告"HISTORY.md:15036が
          // 復活した"): 一度は「チェックの前に無条件で先読み」という
          // 形にしたが、これはマウスの1motionイベントごとに(1行の
          // 移動につき何度も)先読みを試みることになり、runCoastLoop()
          // 自身の同種の訂正で見つかったのと同じ「実際の移動量より
          // 多く読み込んでしまう」症状を再現していた――totalLines>
          // contentRowsが最初から満たされている「通常時」は先読みを
          // 一切試みず、それが「不足している」と分かった時だけ1回だけ
          // 先読みして再判定する、遅延評価の形に直す。
          // 2026-09-06再訂正×3(直接のユーザー報告"バーが最下点に達した
          // 瞬間、行が1行増えたことによる再計算で「00023/00025 END」が
          // 一瞬出てしまう...ちらつきが復活"): suppressPagerRepoll()
          // (main.cpp自身のpollPagerExitNotice()呼び出しが同じtopIndex_
          // をもう一度「未対応」と誤認しないようにする抑制)を、ここの
          // ような「毎フレーム連続で動き続けるコースト/ドラッグ」経路
          // にまで適用していたのが誤りだった――キーボードの1回きりの
          // キー入力(Command::ScrollLineDown/PageDown)とは異なり、この
          // 経路は元々毎フレームredraw()し続けているので、抑制さえ
          // しなければpollPagerExitNotice()自身の「topIndex_が前回から
          // 動いていれば先読みする」という本来の仕組みが、次のフレーム
          // で自然に「1行先を常に確保しておく」バッファとして働き、
          // このコード自身の(境界に当たった時だけ動く)場当たり的な
          // 先読みだけに頼らずに済む――結果、境界ぎりぎりで一瞬だけ
          // 「END」を描いてしまう隙間が生まれなくなる。抑制を撤去した。
          // 2026-09-07追記: 上記はコンテンツが画面すら埋まっていない
          // 極端なケース専用で、通常のドラッグ中(既に画面は埋まって
          // いて、境界にじりじり近づいていく場合)の余裕確保にはならな
          // い――coastDirection側の同日の訂正(すぐ下、この関数内の
          // 該当コメント参照)と全く同じ理由で、ここにも「実際に
          // topIndex_が動いた時だけ2回連続で先読み」を追加する。
          if (view.topIndex() != lastMarginPreloadTopIndex) {
            app.preloadPagerAhead(view.topIndex() + gui.rows() * 2);
            app.preloadPagerAhead(view.topIndex() + gui.rows() * 2);
            lastMarginPreloadTopIndex = view.topIndex();
          }
          int totalLines = static_cast<int>(view.lines().size());
          int contentRows = std::max(gui.rows() - 1, 0);
          if (totalLines <= contentRows) {
            app.preloadPagerAhead(view.topIndex() + gui.rows() * 2);
            totalLines = static_cast<int>(view.lines().size());
          }
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
      const fm::ScrollStep &step = guiSpeedStep(coastLevel);  // SPEED(-gではARROWを使わない、guiSpeedStep()参照)
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
        // 2026-09-06追加、直接のユーザー報告("2つ目のjではENDになら
        // ないが、慣性スクロールが同じくらいの位置に到達するとEND")
        // ――canStepTopIndex()が、まだ新しい内容を読み込む機会が一度も
        // 無いまま、その時点で「たまたま」読み込み済みだった行数だけを
        // 基準に「もう進めない(本当の境界)」と誤判定していた。
        // 2026-09-06再訂正×2(直接のユーザー報告"HISTORY.md:15036が
        // 復活した"): 一度は「チェックの前に無条件で先読み」という形に
        // したが、この一連のdo-whileはコースト中「毎フレーム」実行
        // されるのに対し、実際にpixelOffsetが1行分蓄積してcanStepTopIndex()
        // が意味を持つのはその一部のフレームだけ――結果、低速コースト
        // 1回(見た目には1行しか進んでいない)の間に何十フレーム分も
        // 「現在+1」の先読みが無条件に繰り返し実行され、可視の移動量を
        // はるかに超えて総行数が育ってしまっていた(「①→②の間に総行数
        // が25→50」として再現)。keyboard駆動のjキー(Command::
        // ScrollLineDown)は1回のキー入力につき1回しか先読みを試みない
        // のと対照的――ここも「まず今読み込めている範囲だけで判定して
        // みて、実際に境界に当たった時だけ1回だけ先読みして再判定する」
        // という遅延評価の形に直し、先読みの回数を実際のtopIndex_の
        // 前進回数と1対1に揃える。
        // 2026-09-06再訂正×3(直接のユーザー報告"バーが最下点に達した
        // 瞬間、行が1行増えたことによる再計算で「00023/00025 END」が
        // 一瞬出てしまう...ちらつきが復活"): drag側の同種の訂正
        // (すぐ上のコメント参照)と同じ理由で、ここでもsuppressPagerRepoll()
        // を撤去した――抑制さえしなければ、main.cpp自身のpollPagerExitNotice()
        // 呼び出しが毎フレーム「topIndex_が前回から動いていれば先読み
        // する」を自然に働かせ続け、コースト中ずっと1行分の余裕を
        // 保ってくれる。この関数自身の先読み(すぐ上、境界に当たった
        // 時だけ)はその余裕が万一追いつかなかった時の保険としてのみ
        // 働く。
        // 2026-09-07再訂正(直接のユーザー報告"バーが最下点に達した
        // 瞬間...「00023/00025 END」が一瞬出てしまう"、ContentView::
        // scrollModeLineSegments()自身の判定ロジックを直接確認して
        // 特定): 上のsuppressPagerRepoll()撤去だけでは直らなかった
        // ――pollPagerExitNotice()はこの反復の「先頭」で(前の反復の
        // 終わり時点のtopIndex_を基準に)先読みするが、このdo-while
        // 自身がその「後」でtopIndex_をさらに1行進めてしまうため、
        // せっかく補充した余裕がレンダリングの時点までに使い切られて
        // しまっていた。lastMarginPreloadTopIndex_自身の宣言コメント
        // 参照――「境界に当たった時だけ」ではなく「実際にtopIndex_が
        // 前回の記憶から動いた時は毎回無条件に」先読みする、jキー自身
        // と同じ頻度に直す。
        // 2026-09-07さらに追記: それでも1回の先読みでは、この直後の
        // pixelOffset加算→commitRowCrossings()自身がtopIndex_をさらに
        // 1行進めてしまい、せっかく確保した「+1」がその場で使い切られ、
        // 差し引き0のまま――という全く同じ問題がここにも残っていた。
        // ensureLoadedThrough()のペーシング仕様(呼び出し1回につき
        // targetの大小に関わらず必ず「既読+1」しか読み進めない、
        // application.cpp参照)を踏まえると、jキー側がこの一瞬だけ
        // ちらつかないのは、pollPagerExitNotice()自身の(独立した)
        // +1と、Command::ScrollLineDown自身のこの+1とが同じ1回の
        // キー入力処理の中で両方効いて実質+2読み進め、移動の消費分
        // 1を差し引いても+1の余裕が残るため――ここも同じ効果を
        // 得るため、1回のtopIndex_変化につき2回連続で先読みする。
        if (view.topIndex() != lastMarginPreloadTopIndex) {
          app.preloadPagerAhead(view.topIndex() + gui.rows() * 2);
          app.preloadPagerAhead(view.topIndex() + gui.rows() * 2);
          lastMarginPreloadTopIndex = view.topIndex();
        }
        if (!view.canStepTopIndex(-coastDirection)) {
          // 2026-09-07再訂正、直接のユーザー報告("こんな感じだと判明
          // (※一瞬ブロックカーソルと共に表示される値)"、モードラインの
          // 値が一瞬全く別の(cursorIndex_基準の、総行数に近い)値へ
          // 跳ねてブロックカーソルと共に表示されるログを添えて): ここで
          // coastLevelを問答無用でゼロ化していたのは、canStepTopIndex()
          // のfalseを常に「本当の境界」として扱っていたためだった――
          // ContentView::contentMayGrow()(直前のコミットで新設)が
          // trueの間は、これは「本当の境界」ではなく「まだ先読みが
          // 追いついていないだけ」の可能性がある。それにも関わらず
          // coastLevel/coastDirection/pixelOffsetを全てゼロ化して
          // しまうと、このフレームの下のrender-mode分岐
          // (`dragging || coastLevel>0 || pixelOffset!=0`)が偽になり、
          // 通常のContentView::redraw()(cursorIndex_基準、常にカーソル
          // ハイライト付き)へ一瞬だけフォールバックしてしまう――
          // cursorIndex_はstepTopIndex()により常にビューポート下端
          // 付近(topIndex_+height_-1)へ追従しているため、そこだけ全く
          // 別の(ずっと大きい)位置がブロックカーソル付きで一瞬映る、
          // という報告のログと正確に一致する。真の境界でない間は
          // コースト自体をキャンセルせず、このフレームの前進だけを
          // 見送って(gateの対象から外して)先読みを再試行し、次の
          // フレームで改めてcanStepTopIndex()を確認する。
          //
          // `-coastDirection > 0`(2026-09-07追加): 先読みで解消し得る
          // のは「後方(まだ読み込んでいない側)へ進めない」場合だけ
          // ――canStepTopIndex(-coastDirection)への引数が正で、
          // 「LATERな内容を明かす」方向(コメント上部の`-coastDirection`
          // 自身のマッピング解説参照)の時に限る。逆方向(既に読み込み
          // 済みの側、topIndex_==0という本物の先頭境界)でcontentMayGrow()
          // がtrueだからといって同じ猶予を与えると、topIndex_は
          // どれだけ先読みしても0未満にはなり得ないため、コーストが
          // 永遠にキャンセルされない(止まって見えるのに実は毎フレーム
          // 空振りし続ける)というリグレッションになる。
          if (view.contentMayGrow() && -coastDirection > 0) {
            app.preloadPagerAhead(view.topIndex() + gui.rows() * 2);
            break;
          }
          coastLevel = 0;
          coastDirection = 0;
          pixelOffset = 0;
          wasActivelyScrolling = true;  // see the identical assignment below for why
          break;
        }
        pixelOffset += coastDirection;
        bool crossedWithoutHittingBoundary = commitRowCrossings(view, cellHeight, pixelOffset);
        if (!crossedWithoutHittingBoundary) {
          // すぐ上のcanStepTopIndex()分岐と全く同じ理由(`-coastDirection
          // > 0`の向き限定も含めて)でここにも contentMayGrow()の分岐を
          // 追加(2026-09-07): commitRowCrossings()自身がstepTopIndex()
          // 経由で内部的に複数行分クロスしようとした場合、直前の
          // 先読みだけでは足りずここで初めて境界に当たることがある
          // ――その場合も「本当の境界」でない限りコーストをキャンセル
          // しない。
          if (view.contentMayGrow() && -coastDirection > 0) {
            app.preloadPagerAhead(view.topIndex() + gui.rows() * 2);
            break;
          }
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

    // 2026-09-02、直接のユーザー報告(=fg/=fh)の原因調査用の一時的な
    // ダンプ――このコースト物理ブロック自身が境界停止(canStepTopIndex()
    // /commitRowCrossings()経由)を起こしたフレームを直接特定できる
    // よう、ブロックの直後・レンダ経路の分岐より前に置いてある。
    // 上のhoverダンプ自身のコメント参照。
    // 2026-09-29: EPUBの縦書きの本の中の横書きの範囲(奥付等)で、上下の慣性・ホイールで範囲の
    // 外へ出たら縦書きへ戻す(Application::syncEpubHorizontalSection()参照)。戻ったら上下の
    // 慣性は止める(縦書きの画面は上下には動かない)。
    if (app.syncEpubHorizontalSection()) {
      // 2026-09-29、直接のユーザー依頼("空白行のスキップと同時に方向を変えてそのまま縦方向に
      // 継続してほしい"の逆向き): 上下の慣性で横書きの範囲から出たら、同じ段階の左右の慣性
      // として続ける(前へ出たら前へ=右へ)。coastDirectionは-1で先へ(下の上下の慣性参照)。
      if (coastLevel > 0 && app.epubVerticalDragActive()) {
        epubCoastLevel = coastLevel;
        epubCoastDirection = coastDirection < 0 ? 1 : -1;
        epubDragOffset = 0;
        // 前へ出た時は、端末(runCoastLoop())と同じく、前の章の最後の列を右端に置いた所(leaveEpubHorizontalIfOutside())
        // から1列ずつ。2026-10-13、直接のユーザー報告("奥付からのスローな上向きスクロールでは縦書き最終章の冒頭から
        // 始まる"、=jd): 以前はここで最後の列を左端にそろえ直していたので、右端が最終章の扉に来て、章の冒頭が出ていた。
      }
      coastLevel = 0;
      coastDirection = 0;
      pixelOffset = 0;
      dragging = false;
    }

    // 左右ドラッグの慣性(epubCoastLevel自身のコメント参照)。上の上下の慣性と同じく、この
    // フレームの約16msの間、段階ごとの間隔で1ドットずつ横にずらし、1列の幅を越えたら列を
    // 動かす(空の列は飛ばす、Application::stepEpubVerticalFirst())。本の端で止まったら、
    // ずれを0にして列にそろえる。
    if (epubCoastLevel > 0) {
      bool stop = !app.epubVerticalDragActive();
      if (!stop) {
        const fm::ScrollStep &step = guiSpeedStep(epubCoastLevel);  // SPEED(guiSpeedStep()参照)
        int pitch = std::max(1, gui.cellWidthPixels() * fm::epubVerticalColumnPitch());
        auto frameDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
        do {
          epubDragOffset += epubCoastDirection;
          if (epubDragOffset >= pitch) stop = !shiftEpubColumn(1);
          else if (epubDragOffset <= -pitch) stop = !shiftEpubColumn(-1);
          if (!app.epubVerticalDragActive()) stop = true;  // 横書きの範囲へ入った(下で上下の慣性へ)
          if (stop) break;
          performScrollStepDelay(step);
        } while (std::chrono::steady_clock::now() < frameDeadline);
      }
      // 2026-09-29、直接のユーザー依頼("慣性スクロールについては、空白行のスキップと同時に方向を
      // 変えてそのまま縦方向に継続してほしい"): 左右の慣性で横書きの範囲(奥付等)へ入ったら
      // (scrollEpubVertical()が入る)、同じ段階の上下の慣性として続ける(先へなら下へ)。
      if (stop && app.epubHorizontalSectionActive() && app.currentMode() == fm::ApplicationMode::File) {
        coastLevel = std::min(epubCoastLevel, guiSpeedMaxLevel());
        coastDirection = epubCoastDirection > 0 ? -1 : 1;
        pixelOffset = 0;
      }
      if (stop) {
        epubCoastLevel = 0;
        epubCoastDirection = 0;
        epubDragOffset = 0;
        gui.setContentOffsetX(0, 0);
      } else {
        gui.setContentOffsetX(epubDragOffset, epubContentRows());
      }
      gui.flush();
    }

    if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
      FILE *f = std::fopen(dbg, "a");
      if (f) {
        std::fprintf(f,
                     "---coastFrame--- coastLevel=%d dragging=%d pixelOffset=%d topIndex=%d cursorIndex=%d "
                     "hoverIndex=%d wasActivelyScrolling=%d\n",
                     coastLevel, dragging ? 1 : 0, pixelOffset, view.topIndex(), view.cursorIndex(),
                     view.hoverIndex().value_or(-1), wasActivelyScrolling ? 1 : 0);
        std::fclose(f);
      }
    }

    // Checked here (before the render decision below), not after, so
    // new Shell output arriving this frame counts toward
    // anyEventThisFrame -- otherwise a live Shell session's own
    // output could go unrendered indefinitely once the redraw-skip
    // optimization below is in place, since typing into a DIFFERENT
    // program (the shell) doesn't generate any GraphicalDisplay
    // click/key/drag event of its own.
    // !app.promptActive() -- see the CUI phase's own identical fix and
    // comment for why (PromptKind::ShellCommand's live edit borrows
    // Shell's own rows while currentMode() is back to Shell).
    bool watchShellPty =
        app.currentMode() == fm::ApplicationMode::Shell && app.shell().alive() && !app.promptActive();
    if (watchShellPty) {
      // POLLHUP/POLLERR too -- see the CUI phase's own comment on why.
      // (2026-10-16) Shell::outputReady()はWindowsのネイティブ(ConPTY、ptyFd()が無い)でも使える。
      if (app.shell().outputReady(0)) {
        if (app.shell().onPtyReadable()) {
          anyEventThisFrame = true;
          lastShellPtyActivity = std::chrono::steady_clock::now();
        } else {
          app.handleShellChildExited();
        }
      } else if (std::chrono::steady_clock::now() - lastShellPtyActivity >= std::chrono::milliseconds(100)) {
        // lastShellPtyActivity自身の宣言コメント参照。
        app.shell().flushIdleOutput();
      }
    }

    // Kept current every iteration (cheap -- three int writes), not
    // just on frames that redraw() -- see updateScrollPosition()'s own
    // comment: flush() (redraw()'s eventual target) has no scroll-
    // position parameters of its own, so this is what lets it draw a
    // real, always-current thumb instead of the removed decorative
    // placeholder. Must run BEFORE the redraw() call below so flush()
    // sees this frame's values, not the previous one's.
    // 2026-09-30、直接のユーザー報告("-gの奥付でマウスホイールを一段前(右スクロール)すると上へ
    // 数ドット上がったところで右スクロールへ切り替わるさなか、一瞬...見える画面がカオスのまま"
    // =hu): このフレームの途中で見せる画面が替わった(EPUBの縦書きの本の横書きの範囲への出入り、
    // syncEpubHorizontalSection()等)時は、フレームの頭でつかんだ古いviewを描かない――縦書きへ
    // 戻った後に横書きのfileView_を縦書きの描き方で描き直していた。新しい画面は替わった所で
    // 描いてある。
    if (&view != &app.activeContentView()) {
      wasActivelyScrolling = false;
      usleep(16000);
      continue;
    }
    if (auto epubBar = app.epubVerticalScrollbar())  // 縦書きの本文(Application::epubVerticalScrollbar()参照)
      gui.updateScrollPosition(epubBar->first, epubBar->total, epubBar->perScreen, epubContentRows());
    else
      gui.updateScrollPosition(view.topIndex(), static_cast<int>(view.lines().size()), std::max(gui.rows() - 1, 0));

    if (dragging || coastLevel > 0 || pixelOffset != 0) {
      wasActivelyScrolling = true;
      // 2026-08-21 fix, a direct user report ("マウスの左クリック
      // （画面ドラッグスタンバイ状態）中だと文字装飾が利かず=eb"):
      // used to drop line.attributes entirely (just .text), which is
      // exactly why HELP's own bold/underlined words (nroff-style
      // overstrike, see PipeBuffer's own class comment) rendered plain
      // during an active drag/coast -- see Display::ScrolledLine's own
      // comment for the rest of this fix (GraphicalDisplay::
      // renderTextRun() now actually applies these).
      // 2026-09-02 fix, a direct user request ("ついでに透かしが消える
      // 件もよろしく" -- DirectoryModeのカーソルハイライトがドラッグ/
      // 慣性スクロール中は常に消え、静止すると現れる件): line.attributes
      // はFileMode専用のBold/Underline runしか運ばないので、
      // DirectoryModeの現在行ハイライト(全く別の仕組み、
      // ContentView::redraw()自身のcursorRow/markedRowロジック)は
      // これまでこのレンダ経路に一切乗らなかった。ContentView::
      // scrollHighlightForLine()(その宣言側コメント参照)がredraw()
      // と同じ判定を1行ずつ再現するので、対象の行があればそのAttributeRun
      // を足し込む――GraphicalDisplay::renderTextRun()側のStandout対応
      // (このコミットで新設)と組み合わさって、ドラッグ/コースト中も
      // 静止中と同じ見た目のハイライトが表示される。
      // 2026-09-03 fix, a direct user report ("/Users/haa15030/（ls）
      // 画面に収まらないファイル名...を Screensho...5-15-13.png で
      // 表示よろしく" -- redraw()側にContentView::renderLine()経由で
      // 追加した同じ修正、その宣言側コメント参照): ここでも同じ
      // ellipsisBudget()/truncateHighlightedSuffix()で行のテキスト
      // 自体を先に切り詰めておかないと、静止中とドラッグ中とで
      // DirectoryModeの長いファイル名の見た目がまた食い違ってしまう
      // (=fe/=ffと同じ種類の退行)。
      //
      // 2026-09-15修正、直接のユーザー報告+スクリーンショット(=fn/
      // =fo、DU一覧の"..."の位置が画面ドラッグの前後で変わる、LOCATE
      // では起きない): この分岐は、ContentView::renderLine()自身の
      // 優先順位(自身のコメント参照、ellipsisEligibleが「先」に
      // チェックされ、highlightStartColumnはellipsisEligibleがfalseの
      // 時だけのフォールバック)を一度も再現しておらず、highlight
      // StartColumnの有無「だけ」でtruncateHighlightedSuffix()へ
      // 分岐していた。DirectoryMode/ArchiveMode(highlightStartColumn
      // はあるがellipsisEligibleは無い)ではこれで結果的に一致していた
      // ため気づかれなかったが、DuMode(2026-09-14にクリックの左マー
      // ジン除外用としてhighlightStartColumnを追加、しかし元々
      // ellipsisEligible=trueも持っていた)で初めて両方を同時に持つ
      // 呼び出し元が現れ、静止中(middleEllipsisTruncateByWidth()を
      // 使うellipsisEligible優先の分岐)とドラッグ中(ここ、
      // highlightStartColumnしか見ないのでtruncateHighlightedSuffix()
      // へ流れる)とで別のアルゴリズムが使われ、"..."の挿入位置が
      // 食い違っていた。renderLine()自身と一字一句同じ優先順位へ揃えた。
      //
      // 2026-09-25修正、直接のユーザー報告("LOCATE等...下への速度を
      // 速めると最下行ファイル名は激しく切り替わっているのにハイライト
      // は最下行とその上の行をゆっくり行き来するだけ...スクロール自体
      // にも滑らかさがない。なにか処理が挟まってる？"): 以前はここで
      // 毎フレーム「全行」をコピーし、上の"..."切り詰めまで全行に
      // かけていた。renderScrolled()が実際に読むのは[topIndex-1,
      // topIndex+contentRows]の高々数十行(と、スクロールバー用の
      // lines.size())だけなので、LOCATEのように数千〜数万行ある一覧
      // ではこの全行処理が1フレームを大きく引き延ばし、その間コースト
      // 物理(16msぶんだけ進めては描く)が間引かれてカクつく。さらに
      // 間引かれたフレームがpixelOffsetのどの位相で描かれるかが
      // ほぼランダムになり、ハイライト(cursorIndex_=最下端の行、
      // pixelOffsetぶん上へずれて描かれる)が「最下行とその上の行を
      // ゆっくり行き来する」ように見えていた(行数の少ない(ls)では
      // 目立たなかった)。バッファをフレームをまたいで使い回し、画面に
      // 映る範囲だけを詰め直す。
      int ellipsisBudget = view.ellipsisBudget();
      int totalLines = static_cast<int>(view.lines().size());
      if (static_cast<int>(scrolledLines.size()) != totalLines) {
        scrolledLines.clear();
        scrolledLines.resize(totalLines);
        scrolledFilledBegin = scrolledFilledEnd = 0;
      }
      for (int i = scrolledFilledBegin; i < scrolledFilledEnd && i < totalLines; i++) scrolledLines[i] = {};
      int fillBegin = std::clamp(view.topIndex() - 1, 0, totalLines);
      int fillEnd = std::clamp(view.topIndex() + gui.rows() + 1, fillBegin, totalLines);
      for (int i = fillBegin; i < fillEnd; i++) {
        if (!view.isIndexShown(i)) continue;  // ContentView::setShownIndexRange()参照(空白のまま)
        const auto &line = view.lines()[i];
        std::vector<fm::Display::AttributeRun> attrs = line.attributes;
        if (const auto &standout = view.extraStandout(); standout && standout->index == i)  // 選んでいるリンク
          attrs.push_back({standout->start, standout->end, fm::Display::Attribute::Standout});
        if (auto highlight = view.scrollHighlightForLine(i)) attrs.push_back(*highlight);
        std::string text = line.text;
        if (line.ellipsisEligible && static_cast<int>(text.size()) > ellipsisBudget) {
          text = fm::middleEllipsisTruncateByWidth(text, ellipsisBudget);
        } else if (line.highlightStartColumn.has_value() && static_cast<int>(text.size()) > ellipsisBudget) {
          text = fm::truncateHighlightedSuffix(text, std::max(0, *line.highlightStartColumn), ellipsisBudget);
        }
        scrolledLines[i] = {std::move(text), std::move(attrs), line.ellipsisEligible, line.imageKey, line.imageRow,
                            line.imageRows};
      }
      scrolledFilledBegin = fillBegin;
      scrolledFilledEnd = fillEnd;
      const auto &lines = scrolledLines;
      // trackCursorPosition=true(2026-09-07追加): ContentView::
      // scrollModeLineSegments()自身の宣言コメント参照――GUIのコースト/
      // ドラッグはcursorIndex_を常に追従させ続けているので、キーボード
      // j/k(modeLineText())と一致する表示にする。
      // GraphicalDisplay::setScrolledCursor()参照: カーソル行のハイライトの無いビュー(FILE等)は、
      // 静止時と同じブロックカーソルをスクロール中も描く(慣性で送る向きの先頭の行にある)。
      if (!view.cursorHighlightEnabled() && !app.promptActive())
        gui.setScrolledCursor(std::make_pair(view.cursorIndex(), view.contentCursorColumn()));
      else
        gui.setScrolledCursor(std::nullopt);
      gui.renderScrolled(lines, view.topIndex(), pixelOffset, view.scrollModeLineSegments(/*trackCursorPosition=*/true));
    } else if ((wasActivelyScrolling || anyEventThisFrame) && !app.redrawSensitivePromptOrRecallActive() &&
               !app.awaitingLocateUpdateDb() && !app.awaitingDu() && !app.awaitingArchiveLoad()) {
      // !app.awaitingLocateUpdateDb() (2026-08-15 fix): the exact same
      // bug class !app.promptActive() below was already added for (a
      // real report: "EXIT (y/n) ? y" flashed then got overwritten),
      // just for a DIFFERENT non-promptKind_ status display --
      // runLocateUpdateDb() draws "Updating database..." via
      // drawPromptLabel() directly, then returns immediately (see its
      // own comment) -- the very same key event that triggered ^Jl
      // still lands here as anyEventThisFrame, and this unconditional
      // view.redraw() (which unconditionally redraws the mode-line
      // row) clobbered that message with the underlying view's own
      // normal mode line one frame later, reading exactly like
      // "flashes then reverts" (a real report: "-gの「Updating
      // database...」が一瞬表示されて元に戻る、が直ってない").
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
      //
      // !app.promptActive() (2026-08-11 fix, a real report: "EXIT
      // (y/n) ? y" flashed for an instant then got overwritten by the
      // mode line -- same for FGREP/EGREP etc, anything routed through
      // activatePrompt()). ContentView::redraw() unconditionally
      // redraws its own mode-line row, the SAME row Application's
      // prompt label/input field live on (see drawPromptLabel()'s own
      // comment) -- and this branch ran unconditionally on ANY
      // anyEventThisFrame, including the very keypress that just
      // opened the prompt (activatePrompt()'s own promptEditor_.redraw()
      // already presented a correct frame with the prompt visible;
      // this redraw() then immediately clobbered it with the normal
      // mode line before the user had a chance to answer). The
      // hover-consumption code earlier in this same loop already
      // learned this exact lesson for a different symptom (2026-08-03,
      // "^Xf's input field vanishing the moment the mouse moved") and
      // already gates on !app.promptActive() -- this call just never
      // got the same guard. Submitting or cancelling the prompt clears
      // promptKind_ synchronously (submitPrompt()), so the SAME
      // keypress that closes a prompt still reaches this branch and
      // redraws normally once promptActive() has already gone false.
      wasActivelyScrolling = false;
      view.redraw();
    }

    usleep(16000);  // ~60Hz pacing -- see file comment on why this loop can't share the terminal loop's poll()
  }
}

// Clears THIS process's own controlling terminal (a plain ANSI
// "\x1b[2J\x1b[H" written directly to this process's own inherited
// stdout -- no AppleScript/osascript needed at all, since this
// process's stdio IS literally that terminal, unlike
// raiseLaunchingTerminalWindow()'s own need to reach an EXTERNAL
// window from outside it). Called exactly once, right after the
// "実はすでにGUI" GUI window is created/shown and has already covered
// this terminal from view, so the clear itself is never visible --
// it only matters for what's THERE the next time
// raiseLaunchingTerminalWindow() brings this same terminal back to
// the front for a PROGRAM-mode command.
//
// Added (2026-08-17 follow-up) after a real user report that
// runProgramInLaunchingTerminal()'s own "clear; " command prefix (see
// its own comment) only hides the flash from the SECOND PROGRAM
// invocation onward -- the FIRST one still showed this terminal's
// genuinely original content (whatever was on screen the moment
// `usilic -g` itself was launched, e.g. the shell echoing that
// command), since nothing had ever cleared it before that point. This
// closes that gap by clearing it once up front instead.
void clearLaunchingTerminal() {
  std::cout << "\x1b[2J\x1b[H" << std::flush;
}

// Raises THIS process's own controlling-terminal window (identified
// by tty, exactly the way queryTerminalAppColors()/
// queryTerminalAppWindowBounds() already find "the specific Terminal
// window this process launched from" rather than guessing at "front
// window") to the front of Terminal.app's own window stack, via the
// `index of <window>` property (1 == frontmost). Same
// TERM_PROGRAM=Apple_Terminal gate as every other AppleScript feature
// in this file. Returns whether the AppleScript itself reported
// success -- doesn't (and can't) confirm the window is now VISUALLY
// frontmost, same limitation every
// AppleScript-based feature here already has.
bool raiseLaunchingTerminalWindow() {
  const char *termProgram = std::getenv("TERM_PROGRAM");
  if (!termProgram || std::string(termProgram) != "Apple_Terminal") return false;
  std::string tty = currentTtyPath();
  if (tty.empty()) return false;

  fm::ChildProcess::Result result = fm::ChildProcess::run(
      {"osascript", "-e",
       "tell application \"Terminal\"\n"
       "  activate\n"
       "  repeat with w in windows\n"
       "    repeat with t in tabs of w\n"
       "      if tty of t is \"" +
           tty +
           "\" then\n"
           "        set index of w to 1\n"
           "      end if\n"
           "    end repeat\n"
           "  end repeat\n"
           "end tell"});
  return result.ok;
}

// Runs `command` in this process's OWN inherited stdio -- i.e.
// exactly what ChildProcess::runInteractive() (ProgramMode::run()'s
// own ordinary, non-launcher fallback) already does -- but first
// raises the REAL terminal window that stdio actually belongs to
// above `gui`'s own GUI window, and hides `gui` for the duration.
//
// A complete redesign (2026-08-17), replacing an earlier version that
// tried to open a SEPARATE, newly-created Terminal.app window
// (bounds/colors matched to `gui`'s own, via `do script`) to run the
// command in instead. That approach never got the new window's own
// bounds/colors/z-order right across three separate fix attempts
// (`set index of theWindow to 1`; blocking->polling;
// stderr-capturing debug round) and turned out to be solving the
// wrong problem in the first place: a real user report noticed that
// `less` was ALREADY running correctly -- with fully working keyboard
// input -- in the ORIGINAL terminal this process was launched from
// (ChildProcess::runInteractive()'s own inherited-stdio fallback,
// confirmed by minimizing that terminal and finding `less` running
// there) the whole time, EVERY prior round; only its VISIBILITY was
// ever the actual problem. Their own suggestion -- "その辺の仕組みを
// 頂くというのは？" (borrow whatever mechanism clicking Terminal's own
// Dock icon uses) -- is exactly what raiseLaunchingTerminalWindow()
// above does: find and raise THIS SPECIFIC already-existing window,
// rather than creating, positioning, coloring, and eventually closing
// an entirely separate one. No `windowOuterBounds()`/colors/`do
// script`/`busy`-polling needed at all -- the command runs through
// the exact same code path (ChildProcess::runInteractive()) the
// CUI-only (`-c`) build already uses unmodified, so anything already
// proven to work there (keyboard input, signal handling, exit status)
// carries over here for free.
// 2026-09-29追加、直接のユーザー報告("-gのクイックルックでは^Cで戻れる時もあるが
// そうでないときもある模様。タイミングの問題かと。^Cに加え^[と^Gでも戻れるように
// して"、続けて"クイックルックの際、裏のCUI画面が表になりさえしなければうまく行き
// そうな気もするんだが"): `qlmanage -p`はプレビューのパネルを閉じても終わらない
// ことがあり、下のrunProgramInLaunchingTerminal()のように端末を前に出してGUIを
// 隠したまま終了を待つと、GUIへ戻れなくなっていた。クイックルックに限り端末には
// 触れず、GUIを出したまま裏で起動して待たない。GUIの窓で^[/^G/^Cを打つと
// stopBackgroundQuickLook()で止める(runGuiPhase()のキー処理)。次のクイックルックの
// 前にも前のものを止める。
pid_t gBackgroundQuickLookPid = -1;

bool stopBackgroundQuickLook() {
#ifdef _WIN32  // .steering/20261015-windows-native-gui/
  return false;  // クイックルックはmacOSだけ
#else
  if (gBackgroundQuickLookPid <= 0) return false;
  int status = 0;
  pid_t pid = gBackgroundQuickLookPid;
  gBackgroundQuickLookPid = -1;
  if (::waitpid(pid, &status, WNOHANG) == pid) return false;  // もう自分で終わっていた
  ::kill(pid, SIGTERM);
  ::waitpid(pid, &status, 0);
  return true;
#endif
}

void startBackgroundQuickLook(const std::string &command) {
#ifdef _WIN32  // .steering/20261015-windows-native-gui/
  (void)command;
#else
  stopBackgroundQuickLook();
  pid_t pid = ::fork();
  if (pid == 0) {
    int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      ::dup2(devnull, STDIN_FILENO);
      ::dup2(devnull, STDOUT_FILENO);
      ::dup2(devnull, STDERR_FILENO);
    }
    ::execl("/bin/sh", "sh", "-c", ("exec " + command).c_str(), static_cast<char *>(nullptr));
    ::_exit(127);
  }
  if (pid > 0) gBackgroundQuickLookPid = pid;
#endif
}

bool runProgramInLaunchingTerminal(const std::string &command, fm::GraphicalDisplay &gui) {
  if (command.rfind("qlmanage -p ", 0) == 0) {  // startBackgroundQuickLook()参照
    startBackgroundQuickLook(command);
    return true;
  }
  if (!raiseLaunchingTerminalWindow()) return false;  // falls back to ProgramMode::run()'s own default path
  gui.setVisible(false);
  // `clear; ` prefix (2026-08-17 follow-up): the terminal is raised
  // INSTANTLY, but `command` itself (fork/exec/its own startup, e.g.
  // `less` opening the file and entering its alternate screen) still
  // takes a moment -- during that gap, whatever this terminal last
  // had on screen (its own prompt from just before `usilic -g` itself
  // was launched, typically) is still what's visibly showing. A real
  // user follow-up ("欲を言えば...下の表示が見えてしまう") asked
  // whether that's avoidable; it isn't ELIMINATED (the fork/exec/
  // startup latency itself is unavoidable, whichever command runs
  // first), but this at least makes the BRIEF flash blank instead of
  // stale leftover content -- `clear` is a fast, near-instant builtin/
  // binary, so the perceived gap shrinks even if it can't reach zero.
  {
    IgnoreSigintWhileChildRuns ignoreSigint;  // それ自身のコメント参照
    fm::ChildProcess::runInteractive({"sh", "-c", "clear; " + command});
  }
  gui.setVisible(true);
  gui.raise();
  return true;
}

// EditMode's own counterpart to runProgramInLaunchingTerminal() above
// -- same raise/hide/run/restore shape, but takes a real argv (no
// shell needed at all, unlike PROGRAM-mode's free-form typed command
// line) and clears via clearLaunchingTerminal() (a plain stdout write
// this process already owns) instead of a `clear; ` shell prefix,
// since there's no `sh -c` step here to prepend it to.
bool runEditorInLaunchingTerminal(const std::vector<std::string> &argv, fm::GraphicalDisplay &gui) {
  if (!raiseLaunchingTerminalWindow()) return false;  // falls back to EditMode::edit()'s own default path
  // 2026-09-20追加、"-gで今度は103ではなく100に来る"追跡用の一時診断
  // ログ(既存のUSILICGUIDEBUG慣習): gui.setVisible(false)/(true)の
  // 前後でウィンドウの実際の列数(gui.columns())を記録しておけば、
  // 隠す/見せるだけでSDL側の列数計算がずれていないかがこれだけで
  // 分かる。
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) { std::fprintf(f, "runEditorInLaunchingTerminal: before hide, columns=%d rows=%d\n", gui.columns(), gui.rows()); std::fclose(f); }
  }
  gui.setVisible(false);
  clearLaunchingTerminal();
  {
    IgnoreSigintWhileChildRuns ignoreSigint;  // runProgramInLaunchingTerminal()の同じ箇所参照
    fm::ChildProcess::runInteractive(argv);
  }
  gui.setVisible(true);
  gui.raise();
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) { std::fprintf(f, "runEditorInLaunchingTerminal: after show, columns=%d rows=%d\n", gui.columns(), gui.rows()); std::fclose(f); }
  }
  return true;
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
                      fm::History &history, std::optional<TerminalAppearance> appearance = std::nullopt,
                      std::optional<TerminalWindowBounds> bounds = std::nullopt) {
  // Saved window size/font (2026-08-24, item 427) -- ONLY applied when
  // `appearance` is unset: with `appearance` set, this call is the
  // "実はすでにGUI" launch-time overlay (tryEnterGuiPhase()'s own
  // doc comment), which deliberately matches the REAL calling
  // terminal's OWN current geometry/font -- a stale saved GUI size
  // must never override that. Unset covers both remaining callers
  // (^Og manual mid-session entry, and main()'s own forceGui fallback
  // for when no real terminal exists to match at all, e.g. the .app
  // bundle launch item 423/424 fixed) -- for those, there IS no
  // competing "real terminal geometry" to prefer, so the user's own
  // last-adjusted GUI window naturally wins instead of the hardcoded
  // 16pt/current-terminal-size default.
  fm::History::GuiPreferences savedGui = history.guiPreferences();
  // savedGui.fontPathは`appearance`の有無に関わらず常に使う――
  // "-f <path>"で明示指定された以上、実端末の自動マッチや保存済み
  // rows/columns/fontPointSizeとは違い、常にユーザーの意図が最優先
  // されるべきため(resolveGuiFontPath()自身の優先順位コメント参照)。
  std::string fontPath = resolveGuiFontPath(appearance, savedGui.fontPath);
  // 2026-10-16(runGuiPhase()の後の、端末に重ねる起動の保存のコメント参照): 重ねる端末が無く、usilicrcのFONTも
  // USILICFONTも無ければ、最後に-gで重ねた端末のフォント。
  if (!appearance && savedGui.fontPath.empty() && !std::getenv("USILICFONT") && !savedGui.terminalFontPath.empty() &&
      std::filesystem::is_regular_file(savedGui.terminalFontPath))
    fontPath = savedGui.terminalFontPath;
  int rows = display.rows(), columns = display.columns();
  int fontPointSize = kDefaultGuiPointSize;  // gStartFontPointSize参照
  if (appearance) fontPointSize = appearance->fontPointSize;  // 端末の文字の大きさ(^Og、2026-10-12)
  if (appearance && gStartFontPointSize > 0) fontPointSize = gStartFontPointSize;  // ^Ogでも-z(gStartFontPointSize参照)
  if (!appearance) {
    if (savedGui.rows > 0 && savedGui.columns > 0) {
      rows = savedGui.rows;
      // 2026-10-16、直接のユーザー依頼(Windowsの-g、=jo.png"横幅が80未満ならもう少し広くていいかも"): 前回
      // 狭めた窓のままだと一覧の名前やモードラインが詰まるので、起動時は少なくとも80桁にする。
      columns = std::max(savedGui.columns, 80);
    } else {
      // 2026-10-20、直接のユーザー依頼("既定のウィンドウサイズを80x40にして"): 保存した大きさが無い(最初の起動)
      // 時は、端末の大きさ(.app起動・Windowsでは決め打ちの値)ではなく80桁x40行。
      rows = 40;
      columns = 80;
    }
    if (savedGui.fontPointSize > 0) fontPointSize = savedGui.fontPointSize;
    if (gStartFontPointSize > 0) fontPointSize = gStartFontPointSize;  // -z
    if (gGuiFrame.valid) {  // 親と同じ大きさ(gGuiFrame参照)
      rows = gGuiFrame.rows;
      columns = gGuiFrame.columns;
      fontPointSize = gGuiFrame.pointSize;
    }
  }
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {  // 2026-10-16、GraphicalDisplay::open()のログと同じ理由
    if (FILE *f = std::fopen(dbg, "a")) {
      std::fprintf(f, "tryEnterGuiPhase: saved %dx%d %dpt, frame %d, create rows %d columns %d %dpt\n", savedGui.rows,
                   savedGui.columns, savedGui.fontPointSize, gGuiFrame.valid ? 1 : 0, rows, columns, fontPointSize);
      std::fclose(f);
    }
  }
  std::unique_ptr<fm::GraphicalDisplay> gui =
      fontPath.empty() ? nullptr : fm::GraphicalDisplay::create(rows, columns, fontPath, fontPointSize);
  if (!gui) return false;  // no usable font, or window/renderer creation failed -- stay in terminal mode

  if (!appearance && gGuiFrame.valid)
    gui->setColors(fm::GraphicalDisplay::Color{static_cast<unsigned char>(gGuiFrame.fg[0]),
                                               static_cast<unsigned char>(gGuiFrame.fg[1]),
                                               static_cast<unsigned char>(gGuiFrame.fg[2])},
                   fm::GraphicalDisplay::Color{static_cast<unsigned char>(gGuiFrame.bg[0]),
                                               static_cast<unsigned char>(gGuiFrame.bg[1]),
                                               static_cast<unsigned char>(gGuiFrame.bg[2])});
  else if (appearance && appearance->hasColors) gui->setColors(appearance->foreground, appearance->background);
  else if (!appearance && savedGui.hasColors)
    gui->setColors(fm::GraphicalDisplay::Color{savedGui.fgR, savedGui.fgG, savedGui.fgB},
                    fm::GraphicalDisplay::Color{savedGui.bgR, savedGui.bgG, savedGui.bgB});
  // 2026-10-20、直接のユーザー依頼("macOS、Windows共に最初の起動のみ背景白文字黒、次回以降は前回保存で"): 重ねる
  // 端末が無く、保存した色もまだ無い(最初の起動)時は白地に黒。次回からは終わりに保存した色(上)。
  else if (!appearance)
    gui->setColors(fm::GraphicalDisplay::Color{0, 0, 0}, fm::GraphicalDisplay::Color{255, 255, 255});

  // 2026-10-12、直接のユーザー報告("[^Ogで]GUIは出たけどプロファイルが継承されてない"): ^Ogでは、起動時の
  // 「実はすでにGUI」の経路と同じく、端末のウィンドウにぴったり重ねる(色・フォントは上の`appearance`)。
  if (bounds) {
    gui->setWindowPosition(bounds->x, bounds->y);
    if (gStartFontPointSize <= 0) gui->matchWindowSize(bounds->width, bounds->height);  // -zなら端末の行数・桁数のまま
  }
  gui->open();
  // 2026-10-13、直接のユーザー報告("表紙のページで^Ogすると表紙画像のGUI画面が下になる"、=je/=jf): ^Ogでは端末が
  // 前面のアプリのままなので、開いたGUIのウィンドウが端末の後ろに隠れることがあった。前面に出す(下の描き直しの後にも)。
  if (bounds) gui->raise();
  // 子のHELP(1)・KEY(2): 親のウィンドウの右隣に、HELPは上端を揃え、KEYはその下(親と同じ大きさなので親の高さ分下)。
  // 親の外枠が無ければ(古い親等)、以前の48ずつのずらし。開いたら親に前へ出てもらう(Application::requestParentRaise())。
  if (gWindowSlot > 0 && gGuiFrame.valid) {
    // 外枠も親とぴったり同じ大きさに(-gの親なら自分のマスのまま、GuiFrame::spacing参照)
    if (gGuiFrame.spacing) gui->setWindowSize(gGuiFrame.width, gGuiFrame.height);
    else gui->matchWindowSize(gGuiFrame.width, gGuiFrame.height);
    // 2026-10-11、直接のユーザー依頼("画面の右端に近い位置ではHELP・KEYを左側、下端では上の空いている側"): 親のいる
    // 画面の使える範囲で、横は親の右隣、入らなければ左隣。縦はHELPが親と上端を揃え(はみ出すなら画面の下端に)、KEYは
    // HELPの下、入らなければHELPの上(Terminal.appのopenInNewTerminalWindow()と同じ置き方)。
    int w = gGuiFrame.width, h = gGuiFrame.height;
    int left = -100000, top = -100000, right = 100000, bottom = 100000;
    fm::GraphicalDisplay::usableBoundsContaining(gGuiFrame.x, gGuiFrame.y, left, top, right, bottom);
    int x = gGuiFrame.x + w;
    if (x + w > right) x = gGuiFrame.x - w;
    if (x < left) x = left;
    int helpY = gGuiFrame.y;
    if (helpY + h > bottom) helpY = bottom - h;
    if (helpY < top) helpY = top;
    int y = helpY;
    if (gWindowSlot > 1) {
      y = helpY + h;
      if (y + h > bottom) {
        y = helpY - h;
        if (y < top) y = bottom - h;
      }
    }
    gui->setWindowPosition(x, y);
  }
  else if (gWindowSlot > 0)
    gui->moveWindowBy(gWindowSlot * 48, gWindowSlot * 48);
  else if (gGuiFrame.valid) {
    // 2026-10-12、直接のユーザー依頼("複数外部ウィンドウがピッタリ重なっているので=il.pngくらいズラしてほしい"):
    // ^Ou・^T等の子は、親(-gのウィンドウ、-cなら端末のウィンドウ)と同じ大きさで、親から1つ目は32、2つ目は64...
    // 右下へずらして置く(USILIC_CASCADE、画面からはみ出さない範囲で)。
    int w = gGuiFrame.width, h = gGuiFrame.height;
    if (gGuiFrame.spacing) gui->setWindowSize(w, h);  // GuiFrame::spacing参照
    else gui->matchWindowSize(w, h);
    int left = -100000, top = -100000, right = 100000, bottom = 100000;
    fm::GraphicalDisplay::usableBoundsContaining(gGuiFrame.x, gGuiFrame.y, left, top, right, bottom);
    int offset = (gCascade % 10 + 1) * 32;
    int x = std::max(left, std::min(gGuiFrame.x + offset, right - w));
    int y = std::max(top, std::min(gGuiFrame.y + offset, bottom - h));
    gui->setWindowPosition(x, y);
  } else if (gCascade > 0)
    gui->moveWindowBy((gCascade % 10) * 32, (gCascade % 10) * 32);
  if (gWindowSlot > 0) app.requestParentRaise();
  // 2026-09-12追加、項目⑫: 新しく作られたGraphicalDisplayは
  // modeLineVisible_=trueで始まる(GraphicalDisplayのImpl自身の
  // フィールド既定値参照)ため、直前までterminalDisplayが保持して
  // いた「実際の希望」(history.modeLineVisible()――^Ohでの手動
  // トグルはdisplay/historyの両方を同時に更新するので、これが常に
  // 真実の値)をここで明示的に適用しておかないと、非表示設定のまま
  // ^OgでGUIへ切り替えた瞬間にモードラインが無条件に復活してしまう。
  gui->setModeLineVisible(history.modeLineVisible());
  display.setBackend(*gui);
  clearLaunchingTerminal();  // see its own comment -- covered by the GUI window now, so this never flashes
  // 2026-08-21 fix, a direct user report ("HISTORY:9023...が今は逆に
  // なっていて-cはOK。症状はかつての-cと同じ" -- the "first `less`
  // renders broken" bug had moved FROM `-c` TO `-g`'s own
  // runProgramInLaunchingTerminal() path, after item 382 fixed the
  // `-c` side): this terminal briefly ran usilic itself in terminal
  // mode before this exact swap (every launch starts in terminal mode
  // first, see this file's own comment on the startup sequence),
  // which means TerminalDisplay::open() already constrained ITS
  // scroll region to rows_-2 (reserving the last row for usilic's own
  // mode line) -- and unlike a normal PROGRAM invocation's own
  // leaveRawMode()/reenterRawMode() pairing (item 382's own fix),
  // NOTHING ever widened it back out, since this terminal is about to
  // sit idle indefinitely while GUI owns the session (see this
  // function's/main()'s own comment on that) rather than immediately
  // running another usilic redraw that would want the constrained
  // region back. runProgramInLaunchingTerminal() later re-raises THIS
  // SAME terminal for every PROGRAM-mode command via
  // ChildProcess::runInteractive() directly -- bypassing ProgramMode::
  // run() (and therefore item 382's own leave/reenterForExternalProgram()
  // pairing) entirely, since its own foregroundLauncher_ callback
  // short-circuits that path -- so `less`'s first run here inherited
  // the exact same stale constrained region `-c` used to leave behind,
  // reproducing the identical symptom on the opposite code path. Fixed
  // once, here, at the swap itself (not per-PROGRAM-invocation, unlike
  // item 382's own fix): this terminal is being handed over for
  // external-program-style use for the rest of the session, matching
  // leaveForExternalProgram()'s own meaning exactly, and there's no
  // corresponding reenterFromExternalProgram() call needed since this
  // terminal never goes back to being usilic's own active display
  // within this session.
  terminalDisplay.leaveForExternalProgram();
  app.handleResize();  // re-layout/redraw against the (same-sized, but now GUI-rendered) geometry
  if (bounds) gui->raise();

  // Wired for the duration of GUI mode only -- see
  // ProgramMode::setForegroundLauncher()'s own comment on why this is
  // the one and only place a GUI-aware launcher gets constructed.
  // Captures `gui` by reference (a raw GraphicalDisplay&, not the
  // owning unique_ptr) since the launcher only runs while gui.reset()
  // below hasn't happened yet.
  app.programMode().setForegroundLauncher(
      [&gui](const std::string &command) { return runProgramInLaunchingTerminal(command, *gui); });
  app.editMode().setForegroundLauncher(
      [&gui](const std::vector<std::string> &argv) { return runEditorInLaunchingTerminal(argv, *gui); });

  // 2026-09-25追加、handleSigintForGuiSession()自身のコメント参照:
  // これまで起動元ターミナルの^Cを拾えたのは「実はすでにGUI」起動
  // (guiOwnsWholeSession)だけで、-g/^Og経由のこの経路ではターミナルが
  // raw mode(ISIG無効)のままのため、^Cはシグナルにならず誰も読まない
  // 1バイトとして捨てられていた。GUIの間だけISIGを有効にしてSIGINTを
  // 拾う(^Z/^\は無効化――GUIウィンドウを出したまま停止/コアダンプ
  // させないため)。制御端末が無い(Finderからの起動等)場合は何もしない。
  // 2026-09-26修正、直接のユーザー依頼("-gのページャー起動でも裏ターミナル
  // で^Cによる終了実装よろしく"): 以前は標準入力(STDIN_FILENO)の端末設定
  // だけを変えていたため、`git log`のPAGERや`cmd | usilic -g`のように
  // 標準入力がパイプのページャー起動では、ISIGが一度も有効にならず^Cが
  // 効かなかった。キー入力(KeyInput::open("/dev/tty"))と同じく、標準入力が
  // 端末でなければ制御端末/dev/ttyそのものを相手にする。
#ifdef _WIN32  // 端末が無い(Ctrl+Cの扱いも無い)
  bool termiosSaved = false, closeSignalTty = false;
  int signalTtyFd = -1;
  void (*previousSigint)(int) = std::signal(SIGINT, handleSigintForGuiSession);
#else
  int signalTtyFd = STDIN_FILENO;
  bool closeSignalTty = false;
  if (!isatty(signalTtyFd)) {
    signalTtyFd = ::open("/dev/tty", O_RDWR | O_NOCTTY);
    closeSignalTty = signalTtyFd >= 0;
  }
  struct termios savedTermios{};
  bool termiosSaved = signalTtyFd >= 0 && isatty(signalTtyFd) && tcgetattr(signalTtyFd, &savedTermios) == 0;
  if (termiosSaved) {
    struct termios withSignals = savedTermios;
    withSignals.c_lflag |= ISIG;
    withSignals.c_cc[VSUSP] = _POSIX_VDISABLE;
    withSignals.c_cc[VQUIT] = _POSIX_VDISABLE;
    tcsetattr(signalTtyFd, TCSANOW, &withSignals);
  }
  gGuiSessionSigintReceived = 0;
  void (*previousSigint)(int) = std::signal(SIGINT, handleSigintForGuiSession);
#endif

  fm::GraphicalDisplay *guiForFrame = gui.get();
  app.setGuiFrameProvider([guiForFrame] { return guiFrameText(*guiForFrame); });
  app.setGuiFontProvider([guiForFrame] { return guiForFrame->currentFontPath(fm::GraphicalDisplay::FontKind::Base); });
  runGuiPhase(app, *gui);
  app.setGuiFrameProvider(nullptr);
  app.setGuiFontProvider(nullptr);

  std::signal(SIGINT, previousSigint);
#ifndef _WIN32
  if (termiosSaved) tcsetattr(signalTtyFd, TCSANOW, &savedTermios);
  if (closeSignalTty) ::close(signalTtyFd);
#else
  (void)termiosSaved;
  (void)closeSignalTty;
  (void)signalTtyFd;
#endif

  app.programMode().setForegroundLauncher(nullptr);  // back to plain ChildProcess::runInteractive() in terminal mode
  app.editMode().setForegroundLauncher(nullptr);

  // Save whatever the user actually ended up with (item 427) --
  // BEFORE gui.reset() destroys the window/font this reads from.
  // Captured unconditionally (not gated on `!appearance`): even a
  // "実はすでにGUI" overlay session can pick up a live Cmd+K/Cmd+=/
  // Cmd+- adjustment (item 426), and there's no reason THAT
  // deliberate a choice shouldn't carry forward the same as a plain
  // ^Og session's would -- only the READ side above needs the
  // appearance-match-wins-over-a-stale-save distinction, since that's
  // about what the NEXT session should start from, not what THIS one
  // ended at. Actually written to disk by main()'s own end-of-process
  // history.serialize() call, same as every other History field.
  {
    // 2026-08-30、直接のユーザー報告("usilic -fしても次のopen -aに
    // 反映されない"): 以前はここでfm::History::GuiPreferences prefs;
    // と「新規に」構築していたため、このブロックの直前でfontPathへ
    // 何を設定していようと(main()冒頭の"-f"マージ処理参照)、GUI
    // セッションが一度でも終了するたびにデフォルト値の空文字列で
    // 無条件に上書きされていた。既存のhistory.guiPreferences()から
    // 読み出すことで、このブロックが実際に更新する意図のフィールド
    // (fontPointSize/rows/columns/色)以外は保持されるようにする。
    fm::History::GuiPreferences prefs = history.guiPreferences();
    prefs.fontPointSize = gui->fontPointSize();
    prefs.rows = gui->horizontalRows();  // 縦書きの画面で終えても横書きの行数(2026-10-16)
    if (appearance && !gTerminalFontPath.empty()) prefs.terminalFontPath = gTerminalFontPath;  // ^Ogで重ねた時
    prefs.columns = gui->horizontalColumns();
    std::tie(prefs.lineHeightExtraHorizontal, prefs.lineHeightExtraVertical) = fm::GraphicalDisplay::lineHeightExtras();
    std::tie(prefs.letterSpacingExtraHorizontal, prefs.letterSpacingExtraVertical) =
        fm::GraphicalDisplay::letterSpacingExtras();
    fm::GraphicalDisplay::Color fg = gui->foregroundColor();
    fm::GraphicalDisplay::Color bg = gui->backgroundColor();
    prefs.hasColors = true;
    prefs.fgR = fg.r; prefs.fgG = fg.g; prefs.fgB = fg.b;
    prefs.bgR = bg.r; prefs.bgG = bg.g; prefs.bgB = bg.b;
    history.setGuiPreferences(prefs);
  }

  // 2026-09-12追加、項目⑫: 上のsetBackend(*gui)呼び出し時と同じ理由
  // (自身のコメント参照)――GUIセッション中に^Ohで希望が変わっていた
  // 場合も含め、history.modeLineVisible()を「真実」としてterminalDisplay
  // 側へも反映しておく。直後のapp.handleResize()がlayoutViews()+
  // activeView().redraw()を行うので、再描画自体はそちらに任せられる。
  terminalDisplay.setModeLineVisible(history.modeLineVisible());
  display.setBackend(terminalDisplay);
  gui.reset();  // closes the real window
  if (!app.wantsExit()) {
    // 2026-10-13、直接のユーザー報告("^OgでCUI→GUIはOKだが逆が暴走"): GUIへ移る時のleaveForExternalProgram()
    // (上のコメント参照)でスクロール領域を画面全体に戻したままだったので、端末へ戻った後のスクロールが
    // モードラインまで巻き込んでいた。usilic自身の範囲へ狭め直し、キーが端末へ届くよう端末のウィンドウを前に出す。
    terminalDisplay.reenterFromExternalProgram();
    raiseLaunchingTerminalWindow();
    app.handleResize();  // redraw the terminal, which SDL's window never touched
  }
  return true;
}

// 2026-09-20追加、直接のユーザー報告("SHELLプロンプトでman ls...
// 旧版のように(stdin)で表示して欲しい") + shell.cpp自身の
// buildSetupCommand()の`pagerStatements`のコメント参照: legacyの
// export PAGER='<usilic自身> -p <pid>'を再現するには、まず「今実際に
// 動いている、このプロセス自身の」実行ファイルの絶対パスが要る
// (argv[0]は素の"usilic"のような相対名/PATH解決に頼った名前のことが
// あり、$PAGERとして子のシェルへexportしてもそのままでは実行できない
// 場合がある)。macOSは_NSGetExecutablePath()(<mach-o/dyld.h>)、
// Linuxは/proc/self/exeへのシンボリックリンクを提供する――どちらも
// OSが「本当に何がexecされたか」を教えてくれる標準的な方法(argv[0]
// 自体を独自にPATH解決して回るよりずっと確実)。失敗時(取得できない
// 稀な環境)は空文字列を返し、呼び出し元(main())側でPAGERのexport
// 自体を丸ごと省略する(ShellOptions::pagerPath自身の宣言コメント
// 参照)。
std::string resolveOwnExecutablePath() {
#ifdef __APPLE__
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);  // 最初の呼び出しは常に失敗するが、必要なバッファ長をsizeへ書き戻す
  std::string buf(size, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return "";
  std::error_code ec;
  auto resolved = std::filesystem::canonical(buf.c_str(), ec);
  return ec ? std::string() : resolved.string();
#elif defined(_WIN32)
  return fm::windowsExecutablePath();
#else
  std::error_code ec;
  auto resolved = std::filesystem::canonical("/proc/self/exe", ec);
  return ec ? std::string() : resolved.string();
#endif
}

// 2026-08-25 (a direct user request): the child-process half of the
// SHELL-mode re-exec handoff -- see Shell::start()'s own
// setenv("USILIC", ...) comment for how this gets detected, and
// Application::pollShellHandoff() for the parent-process half that
// actually receives this. Ported from usilic.c's own from_shell() (a
// real legacy source investigation, 2026-08-25, confirmed against the
// real binary): a plain `usilic <path>` (or a pipe into it) typed at
// a live usilic's own SHELL-mode prompt should switch THAT already-
// running session's own display instead of opening a second,
// independent one.
//
// Protocol (deliberately simpler than legacy's own mode-byte+cwd+name
// binary format -- this port has no cross-version wire compatibility
// to preserve): the rendezvous path is
// /tmp/.usilic-shell/s.<uid>.<the shell's own pid> -- getppid() from
// HERE, since THIS process's own parent IS the shell it was typed
// into, the SAME pid the live usilic session already knows as
// app.shell().childPid() -- both ends compute this independently from
// already-shared values (uid, that one pid), no prior negotiation
// needed, exactly mirroring from_shell()'s own <uid>.<getppid()> Unix
// path.
//   - No pipe (a plain path argument, or none at all -- meaning "the
//     shell's own cwd", equivalent to a bare `usilic` typed at the
//     prompt): writes the single resolved absolute path to a temp
//     file, then rename()s it into place atomically, so the parent's
//     own poll (Application::pollShellHandoff()) only ever sees a
//     fully-written file, never a partial one.
//   - A pipe (stdin isn't a tty, no positional argument -- matching
//     legacy's own `!name && (!f||f=='p')` from_shell() branch
//     exactly): binds a real AF_UNIX/SOCK_STREAM socket at that SAME
//     path instead, listen()s, and blocks in accept() -- the parent
//     connects as the CLIENT once it notices the path exists (see
//     pollShellHandoff()'s own comment), and this process streams its
//     own stdin through to it until EOF.
// 2026-08-25 (a direct user request, ported into BOTH this binary AND
// legacy's own old/src/usilic.c's main() -- see Shell::start()'s own
// setenv("USILIC_LIVE_SHELL",...) comment for the marker-SETTING half in
// shell.cpp, and usilic.c's own identical check for legacy's side):
// getenv("USILIC") alone only ever proves "I'm inside a live USILIC
// SHELL specifically" -- typing the OTHER binary's name (legacy "U")
// inside a live USILIC SHELL sees no such env var at all and falls
// through to launching a confusing, genuinely independent full session
// instead. USILIC_LIVE_SHELL is a SECOND, shared, binary-agnostic env var
// BOTH binaries' own Shell start sets unconditionally -- present but
// USILIC specifically absent means "I'm in someone ELSE's live SHELL",
// so this prints a real error instead of ever reaching normal startup,
// exactly the "居場所がSHELLであり、かつ環境変数「USILIC」が設定されて
// いない限り...「Invalid Terminal」となる" the user asked for. (An
// earlier attempt used an unusual pty baud rate instead of a second
// env var -- reverted, see Shell::start()'s own comment on why: a
// real PTY capture found zsh's own interactive startup silently resets
// it before the first prompt even appears.) A completely ordinary
// standalone launch (a real terminal, never any usilic/U SHELL at all)
// has neither env var set and reaches neither branch here, unaffected.
void checkInvalidTerminalMarker() {
  if (std::getenv("USILIC")) return;
  if (!std::getenv("USILIC_LIVE_SHELL")) return;
  std::cerr << "usilic: Invalid Terminal\n";
  std::exit(1);
}

// Returns true if a handoff was attempted at all (regardless of
// success) -- main() returns immediately either way once
// getenv("USILIC") says "your real work belongs to the parent, not
// you": no normal Application/Display startup ever happens here.
bool tryHandoffToParentShell(int argc, char **argv) {
#ifdef _WIN32  // .steering/20261015-windows-native-gui/
  return false;  // SHELLの中のusilicはWindowsでは無い
#else
  if (!std::getenv("USILIC")) return false;
  // 2026-10-02追加、直接のユーザー報告("-m/-nが利かない"): -m/-n(マークアップの変換を標準出力へ)
  // と-h(使い方)は画面を使わずに終わるので、SHELLの中でも親へは渡さずにその場で実行する。以前は
  // "-m"とパスが「開くファイル」として親へ渡っていた。
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], "-m") == 0 || std::strcmp(argv[i], "-n") == 0 || std::strcmp(argv[i], "-e") == 0 ||
        std::strcmp(argv[i], "-h") == 0)
      return false;

  // 2026-09-20追加、直接のユーザー報告("SHELLプロンプトでman ls...
  // (stdin)で表示して欲しい") + shell.cpp自身のbuildSetupCommand()の
  // pagerStatements自身のコメント参照: この関数自身の待ち合わせパス
  // は、元々「typed直接」のケース(usilicがbashの「直接の」子である、
  // ls -l | usilicのような)だけを想定してgetppid()から導出していた
  // ――しかしPAGERとして起動される場合、この「usilic」プロセスの
  // 直接の親は`man`自身(あるいはmanが内部で起動する別の中間プロセス)
  // であり、bash自体ではない。legacyのexport PAGER='<usilic自身>
  // -p <pid>'が明示的にpid引数を渡していたのと全く同じ理由――
  // buildSetupCommand()は、PAGERのexport文自体の中でシェル自身の`$$`
  // (=bash自身のpid、この文が「セットアップ時」に実行される、つまり
  // manより先に評価される時点でのbash自身のpid)を展開させ、
  // --usilic-pager <pid>として埋め込む。ここでそれを検出し、
  // getppid()の代わりに使う――付いていなければ(typed直接のケース)
  // 以前通りgetppid()にフォールバックする。
  long rendezvousPid = ::getppid();
  // 2026-10-05、直接のユーザー報告(MSYS2で^Jp"usilic ファイル..."がページャーになる): ^Jp(PROGRAM)からの
  // 起動では、親のusilicが自分のpidをUSILIC_HANDOFFで渡す(Application::runNestedUsilicProgram())。
  // getppid()は`sh -c`がコマンドを別プロセスにするか置き換えるかで親のusilicにもshにもなり、MSYS2では
  // 親の待つ番号と食い違っていた。
  if (const char *handoff = std::getenv("USILIC_HANDOFF"); handoff && *handoff) rendezvousPid = std::atol(handoff);
  ::unsetenv("USILIC_HANDOFF");  // このusilicのSHELL等の子へ引き継がない(そこでの受け渡しの目印を取り違える)
  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-c" || arg == "-g") continue;
    if (arg == "-s" || arg == "-S" || arg == "-d" || arg == "-D") {  // RcOption参照
      while (i + 1 < argc && *argv[i + 1] &&
             std::all_of(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]),
                         [](unsigned char c) { return std::isdigit(c); }))
        i++;
      continue;
    }
    if (arg.size() == 2 && arg[0] == '-' && std::strchr("fFwWbBlL", arg[1])) {  // 値は開くファイルではない
      if (i + 1 < argc) i++;
      continue;
    }
    if (arg == "--usilic-pager") {
      if (i + 1 < argc) rendezvousPid = std::atol(argv[++i]);
      continue;
    }
    positional.push_back(arg);
  }

  std::string handoffDir = "/tmp/.usilic-shell";
  ::mkdir(handoffDir.c_str(), 0755);
  std::string path =
      handoffDir + "/s." + std::to_string(::getuid()) + "." + std::to_string(rendezvousPid);

  bool pipedStdin = positional.empty() && !::isatty(STDIN_FILENO);
  if (pipedStdin) {
    int sfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) return true;
    ::unlink(path.c_str());  // clears any stale rendezvous left by a crashed earlier attempt
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(sfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 || ::listen(sfd, 1) < 0) {
      ::close(sfd);
      ::unlink(path.c_str());
      return true;
    }
    // 2026-09-06追加、直接のユーザー報告("man bash|usilic -cで
    // 00001/00050になる...これってパイプ用のfdをcloseしてないんじゃ
    // ない？"): 実機PTY調査の結果、原因はfd未解放ではなく、この
    // 待ち合わせパス自体の奪い合いだと判明した――$PAGERが既にusilic
    // 自身にエクスポートされている間は、`man`は「自分の出力がパイプ
    // であっても」内部で$PAGERを呼び出す(`PAGER=<probe> man bash |
    // head`で実機確認済み)。つまり`man bash|usilic -c`と打つと、
    // ①manが内部で呼ぶ$PAGER(= --usilic-pager <bashのpid>付きの
    // usilic)と、②タイプした側の明示的な`usilic -c`(引数無し、
    // getppid()にフォールバックするが、これも直接の子なので結局bashの
    // 同じpidに解決される)の「2つ」のusilicが、全く同じ待ち合わせ
    // パスへ同時にbind()しようとする。すぐ上の::unlink()が
    // bind()の直前で無条件に行われるため、後から来た方が先に
    // listen()していたソケットファイルを横取りしてしまい、先着の方は
    // 以下のaccept()で永遠に取り残されていた(親からの接続はもう
    // 二度と来ない――親はこの経路を1回のShell復帰につき1回しか
    // ポーリングしないため)。
    // 修正: accept()を無期限にブロックさせず、poll()でタイムアウト
    // 付きにする――親(生きたSHELLセッション)は自分のメインループの
    // 毎回の反復でこのパスをポーリングしているので、通常の(競合の
    // 無い)場合は数十ミリ秒以内に接続されるはずであり、数秒待っても
    // 何も来なければ「自分は今回の奪い合いに負けた」と判断してこの
    // 引き渡し自体を諦め、tryHandoffToParentShell()自身が呼び出し元へ
    // falseを返すことで、main()自身の通常の起動処理(このプロセス
    // 自身のstdinを直接読む、独立したスタンドアロンページャーとしての
    // 起動)へ普通に流れ落ちる――永遠にハングする代わりに、負けた方も
    // 自分自身のstdin(それが実際に何であれ)をそのまま独立表示できる。
    // タイムアウト時にpath自体をunlink()しないのは意図的――この時点で
    // そのパスが指しているのは「自分がまだ保持している(だけ使われて
    // いない)もの」か「既に別のプロセスが上書き済みのもの」のどちらか
    // であり、後者を消してしまうとそちらの引き渡しまで壊しかねない。
    // 前者であっても、次回同じパスへの試行が始まる時の防御的unlink()
    // (この関数自身の冒頭のコメント参照)が自然に片付ける。
    struct pollfd pfd {sfd, POLLIN, 0};
    if (::poll(&pfd, 1, 2000) <= 0) {
      ::close(sfd);
      return false;
    }
    int client = ::accept(sfd, nullptr, nullptr);
    ::close(sfd);
    ::unlink(path.c_str());
    if (client < 0) return true;
    char buf[8192];
    ssize_t n;
    while ((n = ::read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
      ssize_t off = 0;
      while (off < n) {
        ssize_t w = ::write(client, buf + off, static_cast<std::size_t>(n - off));
        if (w <= 0) { n = 0; break; }
        off += w;
      }
    }
    ::close(client);
    return true;
  }

  // 2026-10-05: 引数は全部、1行に1つずつ渡す(以前は最初の1つだけ)。PROGRAMの側はそれをUSILICの一覧に
  // する(Application::tryPollProgramHandoff())。SHELLの側は今まで通り最初の行だけを読む。
  std::string resolved;
  if (!positional.empty()) {
    for (const auto &arg : positional) {
      if (!resolved.empty()) resolved += "\n";
      resolved += std::filesystem::absolute(arg).lexically_normal().string();
    }
  } else {
    std::error_code ec;
    resolved = std::filesystem::current_path(ec).string();
  }
  std::string tmpPath = path + ".tmp." + std::to_string(::getpid());
  std::ofstream out(tmpPath, std::ios::binary);
  if (out) {
    out << resolved << "\n";
    out.close();
    fm::replaceFile(tmpPath, path);  // Windowsでも置き換える(saveHistoryToDisk()参照)
  }
  // 2026-09-10追加: 元は直接のユーザー報告("^Jpusilic ...が反応しない
  // →/tmp/に戻るだけ")の調査中に見つけた、別口の潜在バグの防御。
  // (報告自体の真因は、usilicrcのPROGRAMセクションに"usilic"が
  // 登録されていなかったこと――isRecognizedProgram()に弾かれて
  // executeProgram()がそもそも何も実行せずredraw()するだけだった。
  // そちらはusilicrc側にエントリを追加して解決済み、詳細はコミット
  // メッセージ参照。)
  // ただし調査の過程で、この関数自体にも独立した潜在バグがあると
  // 判明した: `USILIC`環境変数は、Shell::start()自身のsetenv()
  // コメントの通り「生きたusilicのSHELLモードのプロンプトに直接
  // タイプされた」ことだけを証明するつもりだったが、実際には
  // ensureShellActive()(CP/MV/RM/ARC自身の、内部的な永続SHELLセッ
  // ション駆動)経由でも同じくShell::start()が呼ばれ、同じ環境変数が
  // 設定される――そのためCP/MV等を(内部的にSHELLを駆動する経路で)
  // 一度でも実行した後にPROGRAM(^Jp)から"usilic ..."を再帰的に呼ぶと、
  // このプロセスも(実際にはライブなSHELLプロンプトに直接タイプされた
  // のではないにも関わらず)無条件にここへ来てしまう。受け手である
  // Application::pollShellHandoff()自身は`currentMode_ ==
  // ApplicationMode::Shell`の間しかこのランデブーパスを一切見ないため
  // (自身のコメント参照)、呼び出し元がSHELL画面を実際に見ていない
  // ケースでは誰も拾わない――この関数は(修正前は)「引き渡しを試みた
  // かどうか」だけを見て常にtrueを返していたため、main()はApplication/
  // Displayを一切起動せずそのまま終了し、ユーザーには「何も起きて
  // いない」ように見えるはずだった。
  // 修正: 上のパイプ済みstdinの分岐(accept()のpoll()タイムアウト)
  // と同じ理屈で、このファイルが実際に「拾われた」(親のpollShell
  // Handoff()自身の::unlink()で消える)のを短時間待つ――生きたSHELL
  // プロンプトへ直接タイプされた本来のケースでは、親は自分のメイン
  // ループの毎回の反復でこのパスをポーリングしているので、通常
  // 数十ミリ秒以内に消えるはずである。タイムアウトしても消えな
  // ければ「本当は誰も受け取っていない」と判断し、このファイル自体
  // を後始末した上でfalseを返し、main()自身の通常の起動処理(独立
  // した本物のセッション)へ普通に流れ落ちる。
  for (int waited = 0; waited < 100; waited++) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return true;  // 消えた=拾われた
    ::usleep(20000);
  }
  ::unlink(path.c_str());  // 誰も拾わなかった――後始末してから通常起動へ
  return false;
#endif
}

}  // namespace

// 2026-10-02新設、直接のユーザー依頼(.steering/20261002-help-key-usilicrc-reorg/): usilicrcの設定を
// 起動時に上書きするオプション1つ分。小文字(-s/-d/-f/-w/-b/-l)はこの起動だけ、大文字
// (-S/-D/-F/-W/-B/-L)は同じ効果に加えて~/.usilic/usilicrcの該当する節へ書き込み、以後はオプション
// 無しでもusilicrcから読まれる(applyRcOptions()参照)。
struct RcOption {
  char letter;
  std::vector<std::string> values;
};

// usilicrcを読んだ後で、rcOptionsを書いた順に当てる。優先順位は「オプション > usilicrc > 組み込み」。
// 書き込みに失敗したら標準エラーへ出すだけで起動は続ける。
void applyRcOptions(fm::Configuration &configuration, const std::vector<RcOption> &rcOptions, const std::string &home,
                    std::string *fontPath) {
  if (!configuration.speedValues().empty()) fm::applyScrollSpeedOverride(configuration.speedValues());
  if (!configuration.arrowSpeedValues().empty()) fm::applyArrowSpeedOverride(configuration.arrowSpeedValues());
  if (!configuration.dragValues().empty()) fm::applyReleaseDistanceOverride(configuration.dragValues());
  *fontPath = configuration.fontPath();
  auto numbers = [](const std::vector<std::string> &values) {
    std::vector<long> out;
    for (const auto &v : values) out.push_back(std::atol(v.c_str()));
    return out;
  };
  for (const auto &option : rcOptions) {
    char lower = static_cast<char>(std::tolower(option.letter));
    bool save = option.letter != lower;
    std::string section;
    std::vector<std::string> saved;
    int value = option.values.empty() ? 0 : std::atoi(option.values.front().c_str());
    switch (lower) {
      case 's': {
        int tunable = static_cast<int>(fm::kScrollSpeedLevels.size()) - 1;
        if (static_cast<int>(option.values.size()) > tunable)
          std::cerr << "usilic: -" << option.letter << " only tunes " << tunable << " levels; the extra "
                    << (option.values.size() - tunable) << " trailing value(s) will be ignored.\n";
        fm::applyScrollSpeedOverride(numbers(option.values));
        section = "SPEED";
        // 書く値は、足りない段を今の値で埋めた全段(どの段が何μsかを一目で分かるように)。
        std::string line;
        for (int level = 1; level <= tunable; level++)
          line += (level > 1 ? " " : "") + std::to_string(fm::kScrollSpeedLevels[level].microseconds);
        saved.push_back(line);
        break;
      }
      case 'd': {
        fm::applyReleaseDistanceOverride(numbers(option.values));
        section = "DRAG";
        std::string line;
        for (long v : fm::currentReleaseDistances()) line += (line.empty() ? "" : " ") + std::to_string(v);
        saved.push_back(line);
        break;
      }
      case 'f':
        *fontPath = option.values.front();
        section = "FONT";
        saved.push_back(*fontPath);
        break;
      case 'w':
        configuration.overrideWait(value);
        section = "WAIT";
        saved.push_back(option.values.front());
        break;
      case 'b':
        if (!configuration.overrideBuffer(value)) {
          std::cerr << "usilic: -" << option.letter << ": value out of range (1-100): " << value << "\n";
          continue;
        }
        section = "BUFFER";
        saved.push_back(option.values.front());
        break;
      case 'l':
        configuration.overrideLine(value);
        section = "LINE";
        saved.push_back(option.values.front());
        break;
      default:
        continue;
    }
    if (save && !fm::usilicrc::setSection(home, section, saved))
      std::cerr << "usilic: -" << option.letter << ": cannot write " << section << " to "
                << fm::usilicrc::userPath(home) << "\n";
  }
}

int main(int argc, char **argv) {
#ifdef _WIN32
  // HOMEをUSERPROFILEで(~・~/.usilic/のため)。argvはSDL2mainがUTF-8にしてくれる。
  fm::prepareWindowsEnvironment();
#endif
#ifdef __APPLE__
  // 2026-10-20、直接のユーザー報告(".appのアイコンをクリックして起動したのち^J^Fすると「No fonts (fc-list)」"):
  // Finderからの起動ではPATHが/usr/bin:/bin:/usr/sbin:/sbinだけで、Homebrewのfc-list・groff等が見つからない。
  // 無ければHomebrewの既定の場所を後ろに足す(子のシェル・コマンドにも受け継がれる)。
  {
    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "/usr/bin:/bin:/usr/sbin:/sbin";
    for (const char *dir : {"/opt/homebrew/bin", "/usr/local/bin"})
      if ((":" + path + ":").find(std::string(":") + dir + ":") == std::string::npos) path += std::string(":") + dir;
    ::setenv("PATH", path.c_str(), 1);
  }
  // 2026-10-20、直接のユーザー報告(.usilicを消して.appから起動すると"英語モードで始まる"): FinderからはLANGも無く、
  // HELP等が英語で始まっていた(下のsetHelpEnglish()はLANGがja_JPのUTF-8の時だけ日本語)。LANGが無ければ、macOSの
  // 優先する言語(AppleLanguagesの先頭)が日本語の時にja_JP.UTF-8にする(子のシェルにも受け継がれる)。
  if (!std::getenv("LANG")) {
    fm::ChildProcess::Result languages = fm::ChildProcess::run({"/usr/bin/defaults", "read", "-g", "AppleLanguages"});
    std::size_t start = languages.ok ? languages.output.find_first_not_of("( \n\t\"") : std::string::npos;
    if (start != std::string::npos && languages.output.compare(start, 2, "ja") == 0) ::setenv("LANG", "ja_JP.UTF-8", 1);
  }
#endif
#ifndef _WIN32
  // 2026-10-20、直接のユーザー報告("^T^H、^T^Kが利かない"、".appだけが出ない"): Finderから起動した.app(デスクトップの
  // アイコンからも)のPATHには~/.local/binが無く、"usilic"は/usr/local/binの旧版(C版、-gを知らない)になっていた。
  // 自分の実行ファイルのディレクトリ(.appならContents/MacOS)がPATHに無ければ先頭に足す(SHELLで打つusilicも今のものに。
  // 外部起動の子はApplication::setSelfExecutablePath()の絶対パスで起動する)。
  if (std::string self = resolveOwnExecutablePath(); !self.empty()) {
    std::string dir = std::filesystem::path(self).parent_path().string();
    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
    if ((":" + path + ":").find(":" + dir + ":") == std::string::npos)
      ::setenv("PATH", (path.empty() ? dir : dir + ":" + path).c_str(), 1);
  }
#endif
  checkInvalidTerminalMarker();
  if (tryHandoffToParentShell(argc, argv)) return 0;
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
  bool openHelpChild = false;  // -H(下の初回起動と同じく、起動後に子のusilicでHELPを開く)
  bool openKeyAtStart = false;  // -K(裏オプション、-Hの子のKEYのウィンドウ用): 起動後にFileManagerのKEYを開く
  std::vector<RcOption> rcOptions;  // -s/-S、-d/-D、-f/-F、-w/-W、-b/-B、-l/-L(RcOption参照)
  std::vector<char *> positionalArgs;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-c") == 0) forceCui = true;
    else if (std::strcmp(argv[i], "-g") == 0) forceGui = true;
    else if (std::strcmp(argv[i], "-H") == 0) openHelpChild = true;
    else if (std::strcmp(argv[i], "-K") == 0) openKeyAtStart = true;
    else if (std::strcmp(argv[i], "-z") == 0) {  // gStartFontPointSize参照
      int size = i + 1 < argc ? std::atoi(argv[i + 1]) : 0;
      if (size < 4 || size > 200) {
        std::cerr << "usilic: -z requires a font size in points (4-200, e.g. -z 20).\n";
        return 1;
      }
      gStartFontPointSize = size;
      i++;
    }
    else if (std::strcmp(argv[i], "-h") == 0) {
      // 2026-09-15追加、直接のユーザー依頼("新たな仕様として...
      // ③-hオプションで「usilic [<file] [file] [directory]」を表示")
      // ――forceCui/forceGui宣言直前のコメント自身が2026-08-21時点で
      // 既に予告していた「隠れオプションとしては「-c -g -u -i -h」」
      // のうち、これだけが今日まで未実装のまま残っていた。以前
      // (41564e8、2026-09-14「大きめの方針転換」)は「引数無し・パイプ
      // 無し」の起動そのものがこの使い方メッセージを表示して即終了
      // していたが、その方針は同日中に撤回された(このすぐ下の
      // consultHistoryOnStart自身のコメント参照、"深く考えもせず
      // 取り消してしまって申し訳ない"というユーザー自身の言葉通り)
      // ――使い方の表示は「素の引数無し起動の既定の挙動」ではなく、
      // この明示的な-hフラグ専用にした。
      // 2026-10-03、直接のユーザー報告("-hの表示が旧来のまま"): 2026-10-02に-s/-d等へ改名・新設した
      // オプションも並べる。裏オプションの-m/-nは載せない。
      std::cout << "usage: usilic [options] [<file] [file] [directory]\n"
                   "  -c            CUI (terminal) mode\n"
                   "  -g            GUI (window) mode\n"
                   "  -H            also open HELP in a child usilic\n"
                   "  -s usec ...   scroll SPEED per level (1-10)\n"
                   "  -d dot ...    flick DRAG thresholds per level (1-10)\n"
                   "  -f file       FONT file for -g\n"
                   "  -z pt         font size for -g (default: last size, or 18)\n"
                   "  -w wait       macro WAIT (x 10 msec)\n"
                   "  -b MB         BUFFER size (1-100)\n"
                   "  -l lines      history LINE count\n"
                   "  -S -D -F -W -B -L   same as lowercase, and also write it to ~/.usilic/usilicrc\n";
      return 0;
    } else if (std::strlen(argv[i]) == 2 && std::strchr("sSdD", argv[i][1]) && argv[i][0] == '-') {
      // 2026-10-02、直接のユーザー依頼(.steering/20261002-help-key-usilicrc-reorg/): 旧-u/-iを
      // usilicrcのSPEED/DRAGに合わせて-s/-dへ改名。大文字は同じ効果に加えてusilicrcへ書き込む
      // (rcOptions参照)。値は1つ以上の整数。
      char letter = argv[i][1];
      RcOption option{letter, {}};
      while (i + 1 < argc && *argv[i + 1] && std::all_of(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]),
                                                          [](unsigned char c) { return std::isdigit(c); }))
        option.values.push_back(argv[++i]);
      if (option.values.empty()) {
        std::cerr << "usilic: -" << letter << " requires one or more "
                  << (std::tolower(letter) == 's' ? "microsecond values (e.g. -s 90000 20000 10000)"
                                                   : "pixel-distance values (e.g. -d 1 2 4 8 16)")
                  << ".\n";
        return 1;
      }
      rcOptions.push_back(option);
    } else if (std::strlen(argv[i]) == 2 && std::strchr("fFwWbBlL", argv[i][1]) && argv[i][0] == '-') {
      // -f/-F(FONT、フォントファイル)、-w/-W(WAIT、10ms単位)、-b/-B(BUFFER、MB)、-l/-L(LINE、行数)。
      char letter = argv[i][1];
      bool numeric = std::tolower(letter) != 'f';
      if (i + 1 >= argc || (numeric && (!*argv[i + 1] || !std::all_of(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]),
                                                                        [](unsigned char c) { return std::isdigit(c); })))) {
        std::cerr << "usilic: -" << letter << " requires "
                  << (numeric ? "a number" : "a font file path (e.g. -f /Library/Fonts/MS-Gothic.ttf)") << ".\n";
        return 1;
      }
      rcOptions.push_back(RcOption{letter, {argv[++i]}});
    } else if (std::strcmp(argv[i], "-m") == 0) {
      // 2026-09-20追加、直接のユーザー依頼("usilicに裏オプションを
      // 作ってほしい。具体的には、`usilic -m src/QUICK.txt` >
      // `~/.local/lib/usilic/usilic.man` でusilicのmanファイルが
      // 作成される")。-c/-g/-u/-i/-fと同じ、隠しの一回限りの
      // ユーティリティモード――対話セッションは一切起動せず、与え
      // られたQUICK.txt形式のmarkupファイルを変換して標準出力へ
      // 書き出し、即座に終了する。
      // 2026-09-20訂正、同じ依頼への直接の追記("-mオプションだが、
      // roffソースを出力する必要はない。man-file風だが公式には無い
      // 裏タグ<indentNN></indent>で作られているソース用の裏オプション
      // としてsrc/doc/HELP.jpのような形式を打ち出してほしい"): 当初は
      // groffの-manマクロソースへ変換していたが、それは差し戻し、
      // legacy自身のHELP.jpが実際に使う「本物のnroff重ね打ちバイト列」
      // 形式(convertMarkupToLegacyHelpText()自身のコメント参照)へ
      // 作り直した。
      if (i + 1 >= argc) {
        std::cerr << "usilic: -m requires a markup file path (e.g. -m src/QUICK.txt).\n";
        return 1;
      }
      std::string markupPath = argv[++i];
      std::ifstream in(markupPath, std::ios::binary);
      if (!in) {
        std::cerr << "usilic: -m: cannot read " << markupPath << ".\n";
        return 1;
      }
      std::ostringstream buffer;
      buffer << in.rdbuf();
      std::cout << fm::convertMarkupToLegacyHelpText(buffer.str());
      return 0;
    } else if (std::strcmp(argv[i], "-n") == 0) {
      // 2026-09-21追加、直接のユーザー依頼("-mオプションはmanマクロ
      // 形式なのでmsを出力するオプションが欲しい。-nとか...とりあえず、
      // manとmsで比較もしたいので-mはそのままで-nを新設してほしい")。
      // -mと対になる裏オプション――同じusilic独自markup(<b>/<u>/<i>/
      // <indentN>)を、今度は本物のroff -msマクロソースへ変換して
      // 標準出力へ書き出す(convertMarkupToMsRoffSource()自身の
      // markup_to_roff.hppコメント参照)。-mと違いこれはまだ「最終的な
      // 平文」ではなく、ユーザー自身が`groff -ms`(または-Tutf8等)へ
      // 通して初めてレンダリングされる、-mとの見比べ用の別経路。
      if (i + 1 >= argc) {
        std::cerr << "usilic: -n requires a markup file path (e.g. -n src/QUICK.txt).\n";
        return 1;
      }
      std::string markupPath = argv[++i];
      std::ifstream in(markupPath, std::ios::binary);
      if (!in) {
        std::cerr << "usilic: -n: cannot read " << markupPath << ".\n";
        return 1;
      }
      std::ostringstream buffer;
      buffer << in.rdbuf();
      std::cout << fm::convertMarkupToMsRoffSource(buffer.str());
      return 0;
    } else if (std::strcmp(argv[i], "-e") == 0) {
      // 2026-10-14、直接のユーザー依頼("HELPモードを正式にHELP.epubとしたい（注釈を見れるので）。usilic -e HELP.txt >
      // ~/.local/lib/usilic/HELP.epub の実装よろしく"): -nのroffを横書きのEPUB3にして標準出力へ
      // (convertMsRoffToHelpEpub()参照)。注釈に使うusilicrcとHELP.notesは、~/.local/lib/usilic/(make installが
      // 最新にする)、/usr/local/lib/usilic/、HELPの原稿と同じディレクトリの順に探す(原稿の隣の古い写しより先に)。
      if (i + 1 >= argc) {
        std::cerr << "usilic: -e requires a markup file path (e.g. -e HELP.txt > ~/.local/lib/usilic/HELP.epub).\n";
        return 1;
      }
      std::string markupPath = argv[++i];
      std::ifstream in(markupPath, std::ios::binary);
      if (!in) {
        std::cerr << "usilic: -e: cannot read " << markupPath << ".\n";
        return 1;
      }
      std::ostringstream buffer;
      buffer << in.rdbuf();
      std::vector<std::string> dirs;
      if (const char *home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/.local/lib/usilic");
      dirs.push_back("/usr/local/lib/usilic");
      std::string markupDir = std::filesystem::path(markupPath).parent_path().string();
      dirs.push_back(markupDir.empty() ? "." : markupDir);
      // 2026-10-18: 開発中にdoc/HELP.epubを作る時(make help-epub)は、リポジトリのusilicrcとHELP.notesを
      // $USILIC_EPUB_RC・$USILIC_EPUB_NOTESで渡す(インストール済みの古い写しを読まないように)。
      auto find = [&](const std::string &name) {
        if (const char *given = std::getenv(name == "usilicrc" ? "USILIC_EPUB_RC" : "USILIC_EPUB_NOTES")) {
          std::ifstream file(given, std::ios::binary);
          std::ostringstream text;
          if (file) text << file.rdbuf();
          return text.str();
        }
        for (const auto &dir : dirs) {
          std::ifstream file(dir + "/" + name, std::ios::binary);
          if (!file) continue;
          std::ostringstream text;
          text << file.rdbuf();
          return text.str();
        }
        return std::string();
      };
      // 2026-10-18: -nが作ったroff(doc/HELP.ja.1、"."で始まる)もそのまま受ける(make installがHELP.epubを作るため)。
      std::string source = buffer.str();
      std::string roff = !source.empty() && source[0] == '.' ? source : fm::convertMarkupToMsRoffSource(source);
      std::string summary;
      std::vector<std::string> unnoted;
      // 2026-10-20: 表紙の画像(usilic.appのアイコン、make help-epubが$USILIC_EPUB_COVERで渡す)。無ければ表紙無し。
      std::string cover;
      if (const char *given = std::getenv("USILIC_EPUB_COVER")) {
        std::ifstream file(given, std::ios::binary);
        std::ostringstream bytes;
        if (file) bytes << file.rdbuf();
        cover = bytes.str();
      }
      std::string epub = fm::convertMsRoffToHelpEpub(roff, find("usilicrc"), find("HELP.notes"), &summary, &unnoted,
                                                     cover);
      std::cout.write(epub.data(), static_cast<std::streamsize>(epub.size()));
      std::cout.flush();
      std::cerr << "usilic -e: " << summary << "\n";
      // 2026-10-18、直接のユーザー依頼: 注釈の無い斜体の名前。doc/HELP.notesに"@名前"と文を足す(要らなければ本文の
      // 無い"@名前"だけ)。
      if (!unnoted.empty()) {
        std::cerr << "usilic -e: no note:";
        for (const auto &name : unnoted) std::cerr << ' ' << name;
        std::cerr << "\n";
      }
      return 0;
    } else positionalArgs.push_back(argv[i]);
  }

  // 2026-08-25 (a direct user request, "いまだ手つかずの通常ファイル
  // 引数"): a positional argument that is a plain FILE, not a
  // directory, used to be silently mistreated as one (fed straight
  // into app.start() below, which lists it as if it were a
  // directory -- previously the only path this function had). Now
  // resolved to absolute paths up front (still lexically_normal(),
  // matching the directory case's own established convention) and
  // checked with std::filesystem::is_directory(): any positional
  // argument that ISN'T a directory (a real file, or a typo that
  // doesn't exist at all -- left for Application::startPagerFileList()
  // itself to report, same as a bad directory argument already falls
  // through to the "cannot list" error below) routes into
  // startPagerFileList() instead of Directory mode. Two or more such
  // arguments is the genuinely NEW case (legacy never supported this
  // at all): a navigable "pager file list", see Command::PagerNextFile's
  // own comment for the n/p/x/f and '['/']' navigation keys this wires
  // up.
  // 2026-09-20、直接のユーザー依頼("大きめの方針転換")によりconsultHistoryOnStart
  // (下)の判定にも使うため、この計算をここ(display/keyInputのセット
  // アップより前、引数解析の直後)へ引き上げた――positionalArgs自体の
  // 内容だけで決まる、std::filesystemだけの純粋な計算なので前倒しに
  // 副作用は無い。
  std::vector<std::string> filePaths;
  for (char *arg : positionalArgs) {
    std::string resolved = std::filesystem::absolute(arg).lexically_normal().string();
    if (!std::filesystem::is_directory(resolved)) filePaths.push_back(resolved);
  }
  bool isFileLaunch = !filePaths.empty() && filePaths.size() == positionalArgs.size();

#ifdef _WIN32
  // Windowsのネイティブ(.steering/20261015-windows-native-gui/)は-gだけ(端末の画面を持たない)。
  forceCui = false;
  forceGui = true;
#endif
  if (forceCui && forceGui) {
    std::cerr << "usilic: -c and -g are mutually exclusive.\n";
    return 1;
  }
  // 2026-10-17、直接のユーザー依頼("ssh起動かどうかはわかる？わかるなら-c固定でよろしく"): ssh越しの起動
  // (sshdが立てるSSH_CONNECTION・SSH_TTY)では、-gを付けても端末の画面(-c)で動かす。-gの窓はssh先(この
  // usilicが動いている側)の画面に開いてしまい、手元には出ないため。^Ogも同じく断る(Command::ToggleGuiMode)。
  if (std::getenv("SSH_CONNECTION") || std::getenv("SSH_TTY")) {
    forceGui = false;
    forceCui = true;
  }
#ifdef USILIC_NO_GUI
  // 2026-10-05、直接のユーザー依頼(MSYS2で-gを試したら"CUI画面上にエラーメッセージが重なっているので
  // 起動せずにメッセージだけ表示して終わりでいい"): GUI無しのビルドでは、端末の画面を作る前に終える。
  if (forceGui) {
    std::cerr << "usilic: -g is not available in this build (built without SDL2).\n";
    return 1;
  }
#endif
  // Platform-based default when NEITHER "-c" nor "-g" was given
  // explicitly (2026-08-21, a direct user request: "他の隠れオプション
  // としては「-c -g -u -i -h」、デフォルト-gで起動できるのはmacOSの
  // みそれ以外では-cとする（-g起動では別のウィンドウ上で起動）"):
  // -g's own GUI window creation needs SDL2/SDL2_ttf and a real
  // display server -- reliably available on macOS (this port's only
  // actually-tested GUI platform, see CLAUDE.md's own repeated notes
  // on Linux GUI verification being untested), not guaranteed
  // elsewhere (a headless Linux server, a bare tty console, etc. --
  // -g there would just fail/fall back anyway, per forceGui's own
  // existing "no usable GUI... falling back to terminal mode" error
  // message below, but defaulting to it there would still cost the
  // attempt every plain launch). Deliberately setting forceGui (not
  // leaving it false and relying on the separate "実はすでにGUI"
  // appearance/bounds auto-detect below, which only activates when
  // it can OVERLAY the current Terminal.app/iTerm2 window) means an
  // unadorned `usilic` on macOS reaches the exact same explicit "-g"
  // code path (tryEnterGuiPhase(), a genuinely separate GUI window)
  // a hand-typed "-g" already does, matching "別のウィンドウ上で起動"
  // -- the auto-detect overlay path, when it also happens to match,
  // still runs first and can still claim the session before that
  // point (unchanged, existing behavior for explicit "-g" too, not
  // something this default changes).
#ifdef __APPLE__
  // 2026-09-16追加、直接のユーザー提案("実行後SHELLへ戻る案、戻らずに
  // 仕様だけ頂くってのはどうかな？^Jpusilic ...はlessのように現usilic
  // から出ること無くusilic内で完結する。SHELLと違うのはPROGRAMを
  // 発動した場所へ戻る点"): ^Jp(PROGRAM)経由でChildProcess::
  // runInteractive()(新規fork/exec)された"usilic"は、
  // tryHandoffToParentShell()自身のランデブー(親がProgramMode::run()
  // のwaitpid()でブロック中のためポーリングできず、必ず2秒でタイム
  // アウトしてfalseを返す――tryHandoffToParentShell()自身のコメント
  // 参照)には失敗するが、USILIC環境変数自体は「ネストしている」こと
  // の信頼できる証拠であり続ける。この場合だけmacOSの既定をGUIから
  // CUIへ倒す――ProgramMode::run()自身の「フォアグラウンドで実行→
  // 終了を待つ→呼び出し元の画面へそのまま戻る」という既存の挙動
  // (lessと同じ)には一切手を付けない。SHELLへ注入する案とは異なり、
  // このusilicは引き続き独立したfork/execのまま、既定の起動先だけが
  // GUIからCUIへ変わる。
  if (!forceCui && !forceGui && std::getenv("USILIC")) forceCui = true;
  // 2026-09-26変更、直接のユーザー依頼("(デフォでGUI起動)はssh経由だと
  // 特に問題なので引数無しはCUIに変更した方が望ましい"): 上の
  // 2026-08-21の「macOSの既定は-g」を改め、制御端末がある限りmacOSでも
  // 既定はCUIとする(ssh越しにGUIウィンドウをローカル側へ開こうとして
  // しまうのを避ける)。制御端末が一切無い場合(Finderからの.app
  // ダブルクリック起動、下のkeyInput.fd() < 0の分岐参照)だけは、CUIでは
  // 何も表示できないので従来通りGUIを既定にする。
  if (!forceCui && !forceGui) {
    int ttyFd = ::open("/dev/tty", O_RDWR | O_NOCTTY);
    if (ttyFd >= 0) {
      ::close(ttyFd);
      forceCui = true;
    } else {
      forceGui = true;
      // 2026-10-22: アイコンからの起動は、フォント・行の高さ・字間等を-gとは別に(Configuration::setIconLaunch()参照)。
      fm::Configuration::setIconLaunch(true);
      fm::History::setIconLaunch(true);
    }
  }
#else
  // 2026-10-16、直接のユーザー依頼(Ubuntuのアイコンからの起動): Linux等でも、制御端末が一切無ければ(デスクトップの
  // ランチャー等)、上のmacOSと同じくGUIを既定にする(CUIでは何も表示できない)。GUI無しのビルドはCUIのまま。
#if !defined(USILIC_NO_GUI) && !defined(_WIN32)  // Windowsのネイティブは上で常に-g
  if (!forceCui && !forceGui) {
    int ttyFd = ::open("/dev/tty", O_RDWR | O_NOCTTY);
    if (ttyFd >= 0) {
      ::close(ttyFd);
    } else {
      forceGui = true;
      fm::Configuration::setIconLaunch(true);  // 上のmacOSと同じ
      fm::History::setIconLaunch(true);
    }
  }
#endif
  if (!forceCui && !forceGui) forceCui = true;
#endif

  // exitOnFailure=!forceGui (2026-08-24 fix, docs/CHANGES.md item 423):
  // see TerminalDisplay::fromEnvironment()'s own declaration comment
  // for the full story -- forceGui/forceCui are already fully resolved
  // by this point (the platform-default block above), so a GUI-only
  // session (no real terminal needed at all) tolerates a missing/
  // unusable $TERM instead of exiting before ever reaching
  // tryEnterGuiPhase().
  std::unique_ptr<fm::TerminalDisplay> terminalDisplay = fm::TerminalDisplay::fromEnvironment(!forceGui);
  terminalDisplay->setOutputEnabled(true);  // fromEnvironment() leaves this off by default -- see its own comment
  fm::DelegatingDisplay display(*terminalDisplay);  // see file comment's own GUI-mode section

  fm::KeyInput keyInput(*terminalDisplay);
  // 2026-10-11、直接のユーザー報告("-gで終わると端末設定が戻らずキー入力がエコーバックしない"): -gの外部起動の子は
  // 端末を使わないので/dev/ttyを開かない。開くと、親が端末を生のモードにしている時の設定を「元の設定」として
  // 覚え、親が終わって端末を元に戻した後に、親に終わらされた子がそれ(エコー無し)に戻してしまっていた。
  if (!(forceGui && std::getenv("USILIC_TRANSIENT"))) keyInput.open("/dev/tty");
  keyInput.installSignalHandlers();
  // SIGTERM/SIGHUP: see gTerminationSignalReceived's own declaration
  // comment. KeyInput::installSignalHandlers() above only covers
  // SIGWINCH/SIGTTOU/SIGCONT (legacy's key.c save_signal() handles
  // those THREE plus SIGTERM/SIGHUP together in one switch -- this
  // port's own installSignalHandlers() just never picked up the other
  // two). Installed unconditionally here, matching legacy's own
  // unconditional save_signal() -- NOT gated on forceGui/-c, since a
  // plain `kill` should be recoverable regardless of which display
  // backend ends up running.
  std::signal(SIGTERM, handleTerminationSignal);
  std::signal(SIGHUP, handleTerminationSignal);

  // 2026-09-20(「大きめの方針転換」、41564e8)は、パイプも位置引数も
  // 無い素の対話的起動を「usilic [<file] [file] [directory]」という
  // 使い方表示+即終了に変え、historyの読み書きをディレクトリ引数
  // 起動だけへ移していた――が、2026-09-15、直接のユーザーによる
  // 訂正でこの転換自体を撤回した("深く考えもせず取り消してしまって
  // 申し訳ない")。isPipedStdin自身の判定(下、pager起動の分岐)と
  // 全く同じisatty()+keyInput.fd()の組み合わせをここでも使う必要が
  // あるため、その判定自体はここに残す(usageの表示自体は-h専用の
  // 分岐(上のflagループ参照)へ移した)。
  // -K(-gの子は標準入力が/dev/null)は標準入力のページャーにしない(2026-10-11、-gのKEYが空だった)。
  bool stdinPiped = !openKeyAtStart && !::isatty(STDIN_FILENO) && keyInput.fd() >= 0;

  // Ported from HELP.jp's own EXIT section (2.18)。2026-09-15、直接の
  // ユーザー依頼("新たな仕様として①引数がディレクトリならそこから
  // 起動。②引数無しなら前回終了時の最終画面から起動。③-hオプション
  // で使い方を表示")により、41564e8の反転をさらに反転し直す形で
  // legacy自身のこの節の原則("引数無しでfile managerで起動した場合は
  // ...履歴リストを...復元"/"ディレクトリ引数で起動した時は...アクセス
  // しません")の「両方」を満たすよう作り直した――ディレクトリ引数
  // 起動(①、41564e8で追加された「何個でも並行稼働してよい」仕様は
  // 変更無し)と、素の引数無し起動(②、legacy自身の「単一インスタンス
  // 前提の、履歴による前回セッション再開」)の両方がhistoryへアクセス
  // する。ファイル引数のpager/pipe stdinのpagerだけが、legacy自身の
  // 「pagerモードはKEY-modeの機能しか使わない」という既存の制限どおり
  // historyに触れない。
  // 2026-10-09、直接のユーザー提案("履歴にはアクセスしないその場限りのディレクトリ仕様ということで^Ou経由でのみ
  // 起動可能というのは？"): 親のusilicが外部ページャーとして起動した子(USILIC_TRANSIENT=親のPID、
  // Application::runExternalUsilicPager()参照)は、ディレクトリを含んでいてもページャーと同じく履歴に触れず、
  // 下の単一インスタンスの決まりで親を終了させない。孫へは引き継がない。
  // 2026-10-10: USILIC_TRANSIENTの値は親のPID。右下の透かしを「親のPID:USILIC」にし(ContentView::
  // programWatermark()参照)、親がいなくなったら終わる(transientParentGone()参照)。
  // 親が開き直したHELPの子に、起動したら飛ぶ行(Application::forwardHelpRequest()参照)。
  std::string helpJumpOnStart;
  if (const char *jump = std::getenv("USILIC_HELP_JUMP")) {
    helpJumpOnStart = jump;
    ::unsetenv("USILIC_HELP_JUMP");
  }
  if (const char *frame = std::getenv("USILIC_GUI_FRAME")) {
    GuiFrame f;
    int n = std::sscanf(frame, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &f.x, &f.y, &f.width, &f.height,
                        &f.rows, &f.columns, &f.pointSize, &f.fg[0], &f.fg[1], &f.fg[2], &f.bg[0], &f.bg[1], &f.bg[2],
                        &f.lineExtra[0], &f.lineExtra[1], &f.letterExtra[0], &f.letterExtra[1]);
    f.valid = (n == 13 || n == 17) && f.rows > 0 && f.columns > 0 && f.pointSize > 0;
    f.spacing = f.valid && n == 17;
    gGuiFrame = f;
    ::unsetenv("USILIC_GUI_FRAME");
  }
  if (const char *cascade = std::getenv("USILIC_CASCADE")) {
    gCascade = std::max(0, std::atoi(cascade));
    ::unsetenv("USILIC_CASCADE");
  }
  if (const char *slot = std::getenv("USILIC_WINDOW_SLOT")) {
    gWindowSlot = std::max(0, std::atoi(slot));
    ::unsetenv("USILIC_WINDOW_SLOT");
  }
  const char *transientEnv = std::getenv("USILIC_TRANSIENT");
  bool transientLaunch = transientEnv != nullptr;
  long transientParent = transientEnv ? std::atol(transientEnv) : 0;
  ::unsetenv("USILIC_TRANSIENT");
  if (transientParent > 1) {
    gTransientParentPid = static_cast<pid_t>(transientParent);
    // 2026-10-11、直接のユーザー依頼("PID:は不要かなと。何かに使える値じゃないので表示する意味がない"): 「USILIC」だけ。
    fm::ContentView::programWatermark() = "USILIC";
  }
  bool consultHistoryOnStart = !isFileLaunch && !stdinPiped && !transientLaunch;

  // Ported from str.c's set_usilicrc()/S_hist path-building: both files
  // live under $HOME/.usilic/, created if it doesn't exist yet (same
  // directory legacy's own history file lived under after the
  // usilic->usilic rename, see CLAUDE.md).
  const char *homeEnv = std::getenv("HOME");
  std::string usilicDir = (homeEnv ? std::string(homeEnv) : std::string()) + "/.usilic/";
  ::mkdir(usilicDir.c_str(), S_IRWXU);  // ignored if it already exists; real failures surface later as I/O errors

  // 2026-08-18 addition (screenshot =cm): an argument-launched run
  // (positionalArgs non-empty) can have any number of OTHER instances
  // running concurrently against different directories -- unlike a
  // bare no-argument "resume last session" launch, which legacy itself
  // only ever assumed one of at a time. Application's own error/shell
  // files used to always live under usilicDir regardless, so two such
  // instances would clobber each other's (confirmed: the report showed
  // one instance's ^T screen displaying the OTHER instance's stale
  // ERROR/SHELL content). Rooting them under a directory unique to
  // THIS process's own pid instead fixes that; a bare no-argument
  // launch keeps the old shared usilicDir (single-instance assumption
  // still holds there, and it's the one case where "resume across
  // restarts" is actually the point).
  //
  // pidTempDirectory (2026-08-18 follow-up, a direct user correction):
  // ALWAYS created and used for work/arc specifically (Application's
  // own workRootDirectory, see its own comment), regardless of launch
  // mode -- unlike tempRootDirectory below (error/shell only), which
  // stays empty for a no-argument launch. A direct user request:
  // "引数無しでは...ERROR/SHELLは~/.usilic配下、終了後もWORK以外は
  // 削除しない" (WORK stays per-process/cleaned-up-on-exit either way;
  // only ERROR/SHELL get the persistent-across-restarts treatment, and
  // only for a no-argument launch). getpid()'s own reuse-after-restart
  // isn't a concern -- /tmp itself gets cleared independently (this
  // project's own already-accepted /tmp-scratch tolerance, see
  // materializeArchiveTempFile()'s own comment).
#ifdef _WIN32
  std::string pidTempDirectory = fm::windowsTempDirectory() + "/.usilic." + std::to_string(::getpid());
#else
  std::string pidTempDirectory = "/tmp/.usilic." + std::to_string(::getpid());
#endif
  ::mkdir(pidTempDirectory.c_str(), S_IRWXU);
  // 2026-08-25 (a direct user request): a piped stdin launch (no
  // positional arguments at all, checked here via isatty() the same
  // way the pager-launch branch further down does -- see
  // isPipedStdin's own declaration there) is content-bearing exactly
  // like an argument launch, even though positionalArgs itself is
  // empty ("見た目引数「無し」でも作業ファイルは「有り」と同じ...
  // ^Tで表示される作業ファイルもみな/tmp/〜"): ERROR/SHELL get the
  // SAME per-process pidTempDirectory treatment as a real file/
  // directory argument, not the persistent shared usilicDir a bare
  // interactive no-argument launch keeps (this comment block's own
  // 2026-08-18 reasoning above still applies to a genuine bare
  // launch -- only the stdin-pipe case is added here). The
  // isatty()-alone-is-insufficient Finder-double-click distinction
  // this paragraph used to spell out for hasRealContentArgument's own
  // (now-removed, see the next comment) definition lives on unchanged
  // at isPipedStdin's own declaration further down, which still needs
  // it.
  //
  // 2026-09-15復活、直接のユーザー依頼("HISTORY.md:17429を復活して
  // ほしい...前プロセスをkillしたうえでの再立ち上げが実装してあれば、
  // 複数プロセスを林立させることなく任意のディレクトリで起動し直せる
  // ことになる")。項目473で実装した「引数無しの多重起動を検知して
  // killする」仕組み(旧hasRealContentArgument判定+~/.usilic/pidベース
  // の排他)は、41564e8(「大きめの方針転換」)で「それが守っていた
  // 対象自体(引数無し起動=履歴付きfile managerセッション)が消滅した」
  // として丸ごと削除されていたが、その方針転換自体を上のconsult
  // HistoryOnStart変更で撤回したため、この排他もここで復活させる
  // ――以下、削除前の実装をstdinPiped(旧hasRealContentArgumentの
  // パイプ判定部分に相当)を使って再現したもの。legacy自身の
  // check_pl()/check_ps()(old/src/usilic.c)ではなく、docs/ideas/
  // IDEAS.md:46が元々望んでいた「警告した上で新プロセス自身が旧
  // プロセスをkillしてから起動を続ける」形。legacy自身のSIGCONT+
  // pidファイルのmtime監視による生死確認(このポートには対応する
  // 「稼働中プロセスが定期的にpidファイルへ触れる」仕組みが無い)の
  // 代わりに、kill(pid,0)一発の単純な生死確認(サスペンド中でも
  // 真――IDEAS.md自身の「サスペンド中のケースを含む」という要件を
  // 自然に満たす)に単純化し、PID再利用による誤検出を避けるため
  // `ps -p <pid> -o comm=`で実際にusilic自身(argv[0]の実行ファイル名)
  // かどうかも確認する。警告メッセージ自体は出さない(2026-09-14の
  // 既存の判断、IDEAS.md:51参照――新プロセス自身の画面がすぐ後で
  // 塗りつぶす直前に一瞬だけ表示されて消える、意味の薄いノイズだった
  // ため)。
  //
  // 2026-09-15追加、直接のユーザー依頼("⑤引数無しusilicが生きて
  // いたら、ディレクトリ引数であってもkillしたうえで起動して欲しい。
  // つまりファイルマネージャーとしてのusilicプロセスは一つだけ"):
  // 対象を「引数無し・パイプ無しの素の起動」だけから、consult
  // HistoryOnStart(=ディレクトリ引数起動も含む、pagerモード――
  // ファイル引数/pipe stdin――だけを除く)へ広げた――ファイル
  // マネージャーとして起動するusilicは(ディレクトリ引数の有無に
  // 関わらず)常に単一インスタンスであるべき、というのがこの依頼の
  // 趣旨。旧プロセスがディレクトリ引数付き/無しのどちらで起動されて
  // いたかは問わず、~/.usilic/pidに記録された(生きていれば)何であれ
  // killする。pagerモードは従来通り何個でも並行して構わない(この
  // すぐ下のtempRootDirectory自身のコメント参照)。
  if (consultHistoryOnStart) {
    std::string ownProgramName = std::filesystem::path(argv[0]).filename().string();
    std::string pidPath = usilicDir + "pid";
    long oldPid = 0;
    {
      std::ifstream pidIn(pidPath);
      if (pidIn) pidIn >> oldPid;
    }
    if (oldPid > 0 && static_cast<pid_t>(oldPid) != ::getpid() && ::kill(static_cast<pid_t>(oldPid), 0) == 0) {
      std::string comm = fm::ChildProcess::run({"ps", "-p", std::to_string(oldPid), "-o", "comm="}).output;
      while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r' || comm.back() == ' ')) comm.pop_back();
      if (std::filesystem::path(comm).filename().string() == ownProgramName) {
        ::kill(static_cast<pid_t>(oldPid), SIGTERM);
        // 2026-10-11: 4秒(古い方は外部起動の子を終わらせるのに最大1.5秒待つ、Application::terminateChildren())。
        for (int waited = 0; waited < 80 && ::kill(static_cast<pid_t>(oldPid), 0) == 0; waited++) ::usleep(50000);
        if (::kill(static_cast<pid_t>(oldPid), 0) == 0) ::kill(static_cast<pid_t>(oldPid), SIGKILL);  // 4秒待っても応答無し -- 強制終了
      }
    }
    std::ofstream pidOut(pidPath, std::ios::trunc);
    if (pidOut) pidOut << ::getpid() << "\n";
  }

  // tempRootDirectory: 2026-09-15修正(⑤、上のkill機構自身のコメント
  // 参照)――ファイルマネージャーとして起動する側(引数無し/ディレクト
  // リ引数、consultHistoryOnStart全体)は常に単一インスタンス前提に
  // なったので、どちらも永続共有のusilicDirのまま(ERROR/SHELLは
  // 前回セッションの内容を引き継ぐ)。pagerモード(ファイル引数/pipe
  // stdin、何個でも並行稼働してよい)だけがこの2026-08-18追加の
  // per-pid隔離(ERROR/SHELLログの相互汚染防止)の対象になる。
  std::string tempRootDirectory = consultHistoryOnStart ? "" : pidTempDirectory;
  // 2026-10-08、直接のユーザー依頼("ERRORは継続しなくていい。起動毎に新規で"): SHELLのログ(項目589)とは
  // 違い、ERRORは起動のたびに空にする(引数無しの~/.usilic/errorが前回までの分を溜め続けていた)。
  { std::ofstream((tempRootDirectory.empty() ? usilicDir : tempRootDirectory + "/") + "error", std::ios::trunc); }
  std::string workRootDirectory = pidTempDirectory;
  // ^T's own new "Boot options" field (see executeShowUsilicBanner()'s own
  // comment): the literal argv[1..] this process was launched with, in
  // their original order (so -c/-g/-u and the directory argument all
  // appear exactly as typed, whichever order that was) -- empty for a
  // bare no-argument launch. ~ expansion only ever applies to the
  // directory-looking token(s) (flags never start with ~).
  std::string bootOptions;
  for (int i = 1; i < argc; i++) {
    std::string token = argv[i];
    if (homeEnv && (token == "~" || (token.size() > 1 && token[0] == '~' && token[1] == '/'))) {
      token = std::string(homeEnv) + token.substr(1);
    }
    if (!bootOptions.empty()) bootOptions += " ";
    bootOptions += token;
  }
  // 2026-10-10、直接のユーザー依頼("INFOの「Boot Options」に「PID：引数」"): 外部起動した子は親のPIDを前に置く。
  // 2026-10-11、直接のユーザー依頼("INFOでは末尾でいい"、例「-c /Users/…/HELP.ja.1 (46063)」): 末尾に括弧で。
  if (gTransientParentPid > 0) bootOptions += " (" + std::to_string(gTransientParentPid) + ")";

  fm::History history;
  fm::Configuration configuration;

  // Ported from init_config()'s env_charset()/getenv(ENV_CHARSET)
  // check -- explicitly distinguishing "unset" from "set to an empty
  // string" is why this isn't just `getenv(...) ?: ""`, see
  // Configuration's own class comment on CLAUDE.md history item 13.
  const char *charsetEnv = std::getenv("USILICCHARSET");
  configuration.setEnvironmentCharset(charsetEnv ? std::optional<std::string>(charsetEnv) : std::nullopt);

  // 2026-08-25 (a direct user request): $HOME/.usilic/usilicrc (a
  // user's own customized copy) takes priority as before; if that
  // doesn't exist yet, falls back to the SHIPPED default `make
  // install` places at $(PREFIX)/lib/usilic/usilicrc -- mirrors
  // legacy's own two-tier set_usilicrc() lookup (str.c: $HOME/.usilic/
  // then LIB/usilicrc) exactly, just without a config.sh-style
  // compile-time LIB constant to bake the second tier into, so both
  // plausible PREFIX values (~/.local, the new default; /usr/local,
  // the traditional one) are tried in turn instead. Previously this
  // port had NO fallback at all -- a fresh install with no
  // hand-created ~/.usilic/usilicrc silently ran on built-in defaults
  // only, never actually reading the usilicrc `make install` just
  // copied into place.
  // 2026-10-05: 実行ファイルの場所(<PREFIX>/bin/usilic)から見た<PREFIX>/lib/usilic/。MSYS2で
  // `make install PREFIX=/c/Users/<名前>/.local`としてGit Bashで使う等、HOMEの下の~/.local以外へ
  // 入れた時にもusilicrc・HELPを見つけるため(Application::setInstallLibDirectory()参照)。
  std::string installLibDir;
  if (std::string exe = resolveOwnExecutablePath(); !exe.empty())
#ifdef _WIN32  // 配る形(windows.mkのdist)は、usilic.exeの隣のlib/usilic/
    installLibDir = std::filesystem::path(exe).parent_path().string() + "/lib/usilic/";
#else
    installLibDir = std::filesystem::path(exe).parent_path().parent_path().string() + "/lib/usilic/";
#endif
  fm::usilicrc::setInstallLibDirectory(installLibDir);
  // 2026-10-02追加(fm::usilicrc::ensureUser()参照): ~/.usilic/usilicrcが無ければ既定のものをコピーする。
  if (homeEnv) fm::usilicrc::ensureUser(homeEnv);
  // 2026-10-08: インストールされた既定のusilicrcの方が新しければ、控えを取ってから~/.usilic/usilicrcを
  // 置き換える(fm::usilicrc::updateUserIfOlder()参照)。
  if (homeEnv) fm::usilicrc::updateUserIfOlder(homeEnv);
  std::string rcText = readFileIfExists(usilicDir + "usilicrc");
  if (rcText.empty() && homeEnv) rcText = readFileIfExists(std::string(homeEnv) + "/.local/lib/usilic/usilicrc");
  if (rcText.empty() && !installLibDir.empty()) rcText = readFileIfExists(installLibDir + "usilicrc");
  if (rcText.empty()) rcText = readFileIfExists("/usr/local/lib/usilic/usilicrc");
  if (!rcText.empty()) configuration.loadRcText(rcText, /*fileManagerMode=*/true);
  std::string rcFontPath;  // FONT、-f/-F(applyRcOptions()参照)
  applyRcOptions(configuration, rcOptions, homeEnv ? std::string(homeEnv) : std::string(), &rcFontPath);
  // usilicrcのSERIF/SANS-SERIF(2026-09-29、GraphicalDisplay::setFontFamilyCandidates()参照)。
  fm::GraphicalDisplay::setFontFamilyCandidates(configuration.serifFontCandidates(),
                                                configuration.sansSerifFontCandidates());
  // usilicrcのLINE-HEIGHT(2026-10-16、GraphicalDisplay::setLineHeightRatios()参照)。
  fm::GraphicalDisplay::setLineHeightRatios(configuration.lineHeightRatios().first,
                                            configuration.lineHeightRatios().second);
  // usilicrcのHISTORY(History::setRetentionCount()自身のコメント参照、2026-09-25)。
  history.setRetentionCount(configuration.historyRetentionCount());

  // LOCATE's personal database (2026-08-22 addition, docs/CHANGES.md
  // item 406): see ConfiguredRole's own enum comment and Configuration::
  // setUserLocateDatabasePath()'s own comment for why this exists (the
  // system-wide database is built by `nobody`, which can't read INTO a
  // modern-macOS `750` home directory at all). No-op on non-Apple
  // builds -- see that same setter's #else branch.
  configuration.setUserLocateDatabasePath(usilicDir + "locate.db");

  std::string historyText;  // stays empty (see hasSavedHistory below) when consultHistoryOnStart is false
  if (consultHistoryOnStart) {
    // 2026-09-20追加、直接のユーザー依頼("大きめの方針転換...historyへの
    // その場反映は^R発動のHISTORY以外でお願いしたい"): 元々の⑦提案
    // ("FILE/DIR/PROGRAMに項目が加わるたびに同ファイルへ記録")を、
    // RingCategory::ShellCommand(" HISTORY"章、^Rで見る生の時系列
    // ログ――実行したシェルコマンド/cd/PROGRAMモードのコマンドが
    // 積まれる、他のどのカテゴリより高頻度)だけ除外する形で実装した。
    // どのカテゴリを対象外にするかはHistory自身の関心事ではなく
    // ここ(main.cpp)のポリシーなので、コールバック自体は無条件に
    // 全カテゴリの通知を受け取り、ここでShellCommandだけ弾く。
    // saveHistoryToDisk()自身がロック+読み直し+差分再生を担うので、
    // ここでは呼ぶだけでよい。
    history.setOnRingPushed([&](fm::RingCategory category) {
      if (category != fm::RingCategory::ShellCommand) {
        saveHistoryToDisk(history, usilicDir, configuration.charsetExplicitlySet());
      }
    });

    historyText = readFileIfExists(usilicDir + "history");
    if (!historyText.empty()) {
      history.loadHistoryText(historyText, configuration.charsetExplicitlySet());
      if (!history.isComplete()) {
        // Ported from load_list()'s own `.old` fallback: the primary
        // file looked truncated/corrupt (missing EOF sentinel).
        std::string oldText = readFileIfExists(usilicDir + "history.old");
        if (!oldText.empty()) history.loadHistoryText(oldText, configuration.charsetExplicitlySet());
      }
    } else {
      // 2026-09-14追加、直接のユーザー依頼("README改めTUTORIALとして
      // FILE履歴には真っ新起動の時点で~/.local/lib/usilic/TUTORIAL
      // \"^　[0-9]^H[0-9]+\\.\"が登録済み"): ~/.usilic/history自体が
      // 存在しない(=このユーザーにとって本当に初めての起動、または
      // 何らかの理由で履歴がまっさらな状態)場合に限り、FILE履歴へ
      // TUTORIAL自身の合成CUTエントリ(application.cppのparseCutFileHistoryEntry()/
      // buildCutFileHistoryEntry()自身のコメント参照)を事前登録する。
      // パターン自体はfm::tutorialCutPattern()(application.hpp/cpp)を
      // 呼ぶ――^T画面自身の「TUTORIAL」行(Application::
      // openSelectedBannerEntry())と全く同じバイト列を参照する必要が
      // あり、ここで独自に組み立てると片方だけ直す事故が起きるため
      // (そのものの宣言コメント参照)。usilic.man同様、ここでは
      // TUTORIAL自身の存在確認までは行わない(実際に選択された時点で
      // FileMode::loadFile()が自然に失敗するだけなので、ここで二重に
      // チェックする必要が無い)。
      // 2026-09-14再訂正、直接のユーザー依頼("モード名はTUTORIALだが
      // 読み込むファイルをTUTORIALとTUTORIAL.enとで切り替えを行う^Ja
      // を復活して欲しい。真っ新FILEリストには...両方を登録"):
      // TUTORIAL.en側も同じ形で1件事前登録する。Application::
      // tutorialPath_の初期値(常にTUTORIAL、日本語版)に合わせ、
      // より目立つ「最新」の位置(pushRecent()は末尾ほど新しい)に
      // TUTORIALが来るよう、TUTORIAL.enを先に積む。
      // 2026-10-02: TUTORIALはHELPへ改名し^Hで開くので、FILE履歴への事前登録はやめた
      // (.steering/20261002-help-key-usilicrc-reorg/)。
    }
  }

  // "-f <path>"(上のオプション解析参照)は、読み込んだhistoryの
  // GuiPreferences.fontPathをここで直ちに上書きする――こうすることで
  // (1) このセッション自身のGUIフォント解決(resolveGuiFontPath()の
  // 呼び出し側、savedGui.fontPath参照)にすぐ反映され、(2) 末尾の
  // consultHistoryOnStart由来のhistory書き出しにも自然に乗る(引数
  // 無し起動の場合のみ、次回のダブルクリック起動でも効き続ける)。
  //
  // 2026-10-02変更(applyRcOptions()参照): フォントはusilicrcのFONT(と-f/-F)で決め、historyに保存
  // された値は使わない――毎回の起動でここで上書きする(FONTが無ければ空=組み込みの候補)。
  {
    fm::History::GuiPreferences prefs = history.guiPreferences();
    prefs.fontPath = rcFontPath;
    history.setGuiPreferences(prefs);
    // ^[[ / ^[]で足した行の高さを前回から引き継ぐ(2026-10-18、History::GuiPreferences参照)。
    fm::GraphicalDisplay::setLineHeightExtras(prefs.lineHeightExtraHorizontal, prefs.lineHeightExtraVertical);
    fm::GraphicalDisplay::setLetterSpacingExtras(prefs.letterSpacingExtraHorizontal, prefs.letterSpacingExtraVertical);
    // usilicrcのLETTER-SPACINGに値があればそちら(^[{ / ^[}がその場で書き込むので、子のHELP・KEYも親と同じ字間、2026-10-21)。
    if (auto extras = configuration.letterSpacingExtras())
      fm::GraphicalDisplay::setLetterSpacingExtras(extras->first, extras->second);
    if (gGuiFrame.spacing) {  // -gの親から開いた子: 親の今の行間・字間(GuiFrame::spacing)
      fm::GraphicalDisplay::setLineHeightExtras(gGuiFrame.lineExtra[0], gGuiFrame.lineExtra[1]);
      fm::GraphicalDisplay::setLetterSpacingExtras(gGuiFrame.letterExtra[0], gGuiFrame.letterExtra[1]);
    }
  }

  // History's own saved charset already defers to the environment
  // when charsetExplicitlySet() is true (loadHistoryText() leaves
  // savedCharset() nullopt in that case) -- see History's own class
  // comment. Configuration::defaultCharset() itself already folds in
  // the environment/rc value, or the compiled-in Utf8 default.
  fm::Charset::Encoding encoding = history.savedCharset().value_or(configuration.defaultCharset());

  fm::ShellOptions shellOptions;
  // Configuration::shellArgv()自身の宣言コメント参照(2026-09-25): $SHELLが
  // 対応するシェルでなければbash(2026-10-18、usilicrcのSHELLは廃止)。
  {
    const char *shellEnv = std::getenv("SHELL");
    shellOptions.argv = configuration.shellArgv(shellEnv ? shellEnv : "");
  }
  // resolveOwnExecutablePath()自身のコメント参照(2026-09-20追加、
  // "SHELLプロンプトでman ls...(stdin)で表示して欲しい")――空(失敗)
  // ならShellOptions::pagerPath自身のデフォルトのまま(PAGERのexport
  // を丸ごと省略)にしておく。
  shellOptions.pagerPath = resolveOwnExecutablePath();

  fm::Application app(display, keyInput, history, configuration, encoding, display.columns(), shellOptions,
                       homeEnv ? std::string(homeEnv) : std::string(), tempRootDirectory, workRootDirectory,
                       bootOptions);
  // 2026-10-12、直接のユーザー報告(-cから^Tで開いた-gの子の見た目が端末のプロファイルと違う): -cの間に開く-gの子へ、
  // 起動した端末のウィンドウの外枠・行数・桁数・文字の大きさ・色("x,y,w,h,rows,cols,pt,r,g,b,r,g,b"、guiFrameText()と
  // 同じ並び)とフォントのファイルを渡す(Application::launchExternalUsilic())。分からなければ空(子は今まで通り)。
  app.setTerminalFrameProvider([&display]() -> std::string {
    auto appearance = queryRealTerminalAppearance();
    auto bounds = appearance ? queryRealTerminalWindowBounds() : std::nullopt;
    if (!appearance || !bounds || !appearance->hasColors) return "";
    std::ostringstream out;
    out << bounds->x << ',' << bounds->y << ',' << bounds->width << ',' << bounds->height << ',' << display.rows()
        << ',' << display.columns() << ',' << appearance->fontPointSize << ',' << int(appearance->foreground.r) << ','
        << int(appearance->foreground.g) << ',' << int(appearance->foreground.b) << ','
        << int(appearance->background.r) << ',' << int(appearance->background.g) << ','
        << int(appearance->background.b);
    return out.str();
  });
  app.setTerminalFontProvider([]() -> std::string {
    if (!gTerminalFontPath.empty()) return gTerminalFontPath;
    auto appearance = queryRealTerminalAppearance();
    if (!appearance || appearance->fontName.empty()) return "";
    std::string path = fm::resolveMacFontPath(appearance->fontName, appearance->fontPointSize);
    return path.empty() ? fm::resolveLinuxFontPath(appearance->fontName) : path;
  });
  // 2026-10-11、直接のユーザー依頼(-KのKEYの^Mのキーを親へ、hを-HのHELPの子へ): 親子の受け渡しの名前付き
  // パイプ(Application::openChildChannel()参照)。外部起動された子でなければ、自分の子から受け取る"keys"を開く。
  // 子は親のPIDを覚え、-H・最初の起動のHELPの子(gWindowSlot 1)は親の下に"help"を開く。終了時に消す。
  struct ChildChannelGuard {
    fm::Application &app;
    ~ChildChannelGuard() {
      app.terminateChildren();  // 親: 子を終わらせて、終わったのを確かめてから(Application::terminateChildren())
      app.closeChildChannel();
    }
  } childChannelGuard{app};
  if (gTransientParentPid > 0) {
    app.setParentPid(gTransientParentPid);
    // (2026-10-12〜10-20: -H・最初の起動のHELP・KEYの子は自身では終わらなかった(setEndsWithParentOnly())。
    // 2026-10-20、直接のユーザー提案"すべての子は自身を終了可"でやめた。)
    // 2026-10-12: HELPの子以外も自分の受け渡しのFIFO("w.<PID>")を持つ(親の^Tn/^Tpの"A"を受ける)。
    // openChildChannel()は登録のファイルを消すので、登録はその後(中身にFIFOの名前を書く)。
    app.openChildChannel(gTransientParentPid, gWindowSlot == 1 ? std::string("help") : "w." + std::to_string(::getpid()));
    app.registerAsChild();
  } else {
    app.openChildChannel(::getpid(), "keys");
  }

  // 2026-09-20、直接のユーザー依頼("今のKEYをHELPに置き換え...正味の
  // ：カットによるHELPとしてほしい")によりHELP自身が全面的に作り直
  // された。以前はここでhelp_text.hppに埋め込んだ日本語/英語の全文
  // (kHelpTextJa/En、+GUI専用章kHelpTextGuiJa/En、Command::
  // ToggleHelpLanguage/^Oaで切替可能)を読み込んでいたが、それらは
  // 全て削除済み(git履歴には残る)。新HELPは~/.local/lib/usilic/
  // usilic.man(usilic -m QUICK.txtで生成する、legacy自身のHELP.jpと
  // 同じnroff重ね打ちバイト列形式のファイル、markup_to_roff.hpp
  // 参照)の生の内容をそのまま読み込むだけ――expandHelpMarkup()展開も
  // GUI章の自動追加も一切行わない(そのファイルの内容がそのまま
  // 全て)。$USILICHELPが設定されている場合は、以前と全く同じように
  // そちらを優先する(手元の別ファイルで試したい場合の上書き用)。
  // 読めない場合はHELP(^H/^J^H)が単に利用不可能になるだけ
  // (Command::StartHelpが素直に失敗する)――起動時エラーにはしない。
  //
  // 2026-10-02変更(.steering/20261002-help-key-usilicrc-reorg/): usilic.manではなく、TUTORIALから改名した
  // HELP(Application::helpPath_、~/.local/lib/usilic/HELP、無ければ/usr/local/lib/usilic/HELP)を読む。
  // ^HはこれをタブでCUTし、^Kは「：」でCUTする(KEY)。
  app.setInstallLibDirectory(installLibDir);
  app.setSelfExecutablePath(resolveOwnExecutablePath());  // 外部起動の子もこれで(Application::setSelfExecutablePath())
  app.setPagerLaunch(!consultHistoryOnStart);  // KEYの検索文字列(Application::startKey()参照)
  if (const char *overridePath = std::getenv("USILICHELP")) {
    std::string text = readFileIfExists(overridePath);
    if (!text.empty()) app.loadHelpText(text);
  } else {
    // 2026-10-11、直接のユーザー依頼("LANGがja_JP.UTF-8の時はHELP.ja.1それ以外ではHELP.1を参照し、起動後も
    // 最初から英語モード"): 以前は常に日本語(HELP)で始まり、英語は^Jaで切り替えていた。LANGがja_JPのUTF-8
    // なら日本語、それ以外(未設定・C・en_US.UTF-8・ja_JP.eucJP等)は英語で始める。-Hと最初の起動で子に渡す
    // HELPもこれに従う(helpLoadedPath_)。英語版が読めなければ日本語へ戻す。
    std::string lang = std::getenv("LANG") ? std::getenv("LANG") : "";
    std::string langLower;
    for (char c : lang) langLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool japanese = langLower.rfind("ja_jp.", 0) == 0 &&
                    (langLower.find(".utf-8") != std::string::npos || langLower.find(".utf8") != std::string::npos);
    app.setHelpEnglish(!japanese);
    if (!app.reloadHelpText() && !japanese) {
      app.setHelpEnglish(false);
      app.reloadHelpText();
    }
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
  if (appearance && !appearance->fontName.empty()) {  // gTerminalFontPath参照
    gTerminalFontPath = fm::resolveMacFontPath(appearance->fontName, appearance->fontPointSize);
    if (gTerminalFontPath.empty()) gTerminalFontPath = fm::resolveLinuxFontPath(appearance->fontName);
  }
  auto bounds = (forceCui || !appearance) ? std::nullopt : queryRealTerminalWindowBounds();
  if (appearance && bounds) {
    // savedGui.fontPathをここでも尊重する理由はtryEnterGuiPhase()自身の
    // コメントと同じ: "-f <path>"で明示指定された以上、実端末の自動
    // マッチより優先されるべきため。
    std::string fontPath = resolveGuiFontPath(appearance, history.guiPreferences().fontPath);
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
                                                                  gStartFontPointSize > 0 ? gStartFontPointSize
                                                                                          : appearance->fontPointSize);
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
      // 2026-10-18、直接のユーザー報告("basic（11pt）で`-g -z 20`したけど11のまま")と続く依頼("80x24になる方が
      // いい。-gが正式オプションとなったので重ね合わせはどうでもいいかなと"): -zがあれば、その字の大きさで端末と
      // 同じ行数・桁数の窓にする(位置だけ端末に合わせ、端末より大きくなってもよい)。matchWindowSize()は字の送りを
      // 端末に合わせてしまうので使わない。
      if (gStartFontPointSize <= 0) launchGui->matchWindowSize(bounds->width, bounds->height);
      launchGui->open();
      display.setBackend(*launchGui);
      clearLaunchingTerminal();  // see its own comment -- covered by the GUI window now, so this never flashes
      // See tryEnterGuiPhase()'s own identical call's comment
      // (2026-08-21 fix): this launch path swaps to GUI the exact
      // same way, so it needs the exact same scroll-region reset --
      // otherwise a PROGRAM-mode command run later via THIS terminal
      // (runProgramInLaunchingTerminal()) would inherit the same
      // stale constrained region this process's own brief terminal-
      // mode startup left behind.
      terminalDisplay->leaveForExternalProgram();
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
  // serialized to the history file's USILIC trailer line, but nothing
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
  // 2026-10-02: usilicrcのSCROLL・LSは廃止(Configuration::overrideWait()の宣言のコメント参照)。
  // historyに保存が無い(新規)なら、どちらもオフ。
  bool hasSavedHistory = !historyText.empty();
  display.setSmoothScrollEnabled(history.smoothScrollEnabled());
  app.directoryMode().setFilenameOnlyMode(history.lsMode() != 0);
  // 2026-09-12追加、項目⑫: History::modeLineVisible_にはusilicrc側の
  // 対応するディレクティブが無い(ユーザー依頼はHistoryでの永続化のみ)
  // ため、未保存の新規インストールは単純に「表示」を既定値とする
  // (History::modeLineVisible_自身の既定値と一致)。この時点で
  // display(DelegatingDisplay)がどちらのバックエンドを指していようと
  // (この行より前のGUI起動経路自身のsetBackend()呼び出し参照)、
  // ここで一度適用しておけば両方の起動経路を一箇所でカバーできる。
  display.setModeLineVisible(hasSavedHistory ? history.modeLineVisible() : true);
  // EPUBの明朝体・ゴシック体(^Ot、Command::ToggleEpubTypeface): 前回の終了時の選択(-gへ移った後もDelegatingDisplayが渡す)。
  display.setSerifSuppressed(hasSavedHistory && history.epubSansSerif());
  // 2026-09-12追加: fm::Application appのコンストラクタ(このsetModeLineVisible()
  // 呼び出しより前の行で既に構築済み)は自身のlayoutViews()を構築時点の
  // display.modeLineVisible()(まだ既定のtrueのまま)で1回実行してしまって
  // いるため、ここでhistoryの永続化された値を適用するだけではcontentHeight
  // が古いまま――setSmoothScrollEnabled()/setFilenameOnlyMode()と違い、
  // modeLineVisible()はレイアウト(予約行数)そのものに影響するので、この
  // 1回だけは明示的な再レイアウトが必要(PTYでの実機検証で発見)。
  app.relayoutForModeLineVisibilityChange();

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
  // 2026-08-24 fix (docs/CHANGES.md item 421, found while verifying
  // the new `make app` .app-bundle packaging, not literally requested
  // but directly in service of "click-to-launch works nicely"):
  // LaunchServices spawns a double-clicked .app bundle with the
  // process's CWD at bare "/" (confirmed directly -- a real
  // screenshot of the packaged bundle's first launch showed the root
  // filesystem's own root-owned entries). A real Terminal session's
  // CWD is never meaningfully "/" in practice (nobody navigates there
  // to work), so an argument-less launch landing on literal "/" is a
  // reliable enough signal that this ISN'T a real chosen working
  // directory -- $HOME is used instead, matching what any ordinary
  // Finder-launched file browser would default to. An EXPLICIT `usilic
  // /` (or `usilic .` from an actual root-CWD shell) is untouched --
  // this only substitutes the bare, argument-less "." case.
  std::string cwdFallback = ".";
  if (positionalArgs.empty()) {
    std::error_code cwdEc;
    std::filesystem::path cwd = std::filesystem::current_path(cwdEc);
    if (!cwdEc && cwd == std::filesystem::path("/") && homeEnv) cwdFallback = homeEnv;
  }
  // 2026-08-25 (a direct user follow-up, "less同様1ページ分しか読み
  // 込まない。その点STDINと同じなのでついでにそれもサポートして"): a
  // piped stdin (no positional arguments at all, and fd 0 isn't a real
  // tty) is the OTHER pager-launch shape, sharing startPagerFileList()'s
  // own lazy-loading machinery (FileMode::loadStream()) via a separate
  // Application::startPagerStdin() -- see its own comment.
  // 2026-08-30 (直接のユーザー報告、「build/usilicをダブルクリック
  // すると内容無しのSTDIN」): Finderからの直接ダブルクリック起動は、
  // 制御端末自体が無く(LaunchServices経由、argc==1でstdin/stdout/
  // stderrが/dev/null相当)、!::isatty(STDIN_FILENO)だけで判定すると
  // 「シェルからパイプされたstdin」と誤認され、/dev/nullを読んで
  // 空のページャ画面になっていた。keyInput.fd() >= 0(上の
  // keyInput.open("/dev/tty")が制御端末を実際に開けたか)を追加の
  // 条件にすることで、本物のシェルパイプ("cat foo | ./build/usilic"、
  // 制御端末自体は健在)とダブルクリック起動(制御端末が存在しない)
  // を区別する――後者はこの下のelse節、通常のディレクトリ起動に
  // 落ちる。
  bool isPipedStdin = !openKeyAtStart && positionalArgs.empty() && !::isatty(STDIN_FILENO) && keyInput.fd() >= 0;
  // Neither pager launch shape gets a background Directory listing at
  // all (2026-08-25, a real PTY capture of the legacy binary: pressing
  // ^G in ITS OWN pager mode produces no visible change whatsoever --
  // there is no "previous mode" to fall back into, matching HELP.jp's
  // own "この時はKEYで表示されるモードのみ使用できます"). An EARLIER
  // version of this fix called app.start() first regardless, so ^G
  // would reveal the first file's own containing directory -- a
  // deliberate, reasonable-sounding improvement over legacy that
  // turned out NOT to match what was actually being asked for here;
  // reverted in favor of matching the reference exactly. See
  // Command::Back's own File-mode branch (fileMode_.hasActiveStream()
  // check) for where ^G's own no-op now lives.
  if (isFileLaunch) {
    if (!app.startPagerFileList(filePaths)) {
      display.close();
      keyInput.close();
      std::cerr << "usilic: cannot open " << filePaths.front() << "\n";
      return 1;
    }
    if (!helpJumpOnStart.empty()) app.jumpHelpToKeyLine(helpJumpOnStart);  // 親が開き直したHELPの子
  } else if (isPipedStdin) {
    if (!app.startPagerStdin()) {
      display.close();
      keyInput.close();
      std::cerr << "usilic: cannot read from stdin\n";
      return 1;
    }
  } else {
    std::string startDirectory =
        std::filesystem::absolute(!positionalArgs.empty() ? std::filesystem::path(positionalArgs[0])
                                                            : std::filesystem::path(cwdFallback))
            .lexically_normal()
            .string();
    // ファイルとディレクトリが混ざった起動は、最初のディレクトリを開く(2026-10-12に^Tn/^Tpの複数のディレクトリは
    // 廃止、外部起動の子へは親が1つずつ渡す、Application::runExternalUsilicPager()参照)。
    for (const auto &arg : positionalArgs) {
      std::error_code ec;
      if (std::filesystem::is_directory(arg, ec)) {
        startDirectory = std::filesystem::absolute(arg).lexically_normal().string();
        break;
      }
    }
    // History-based cursor restoration (see DirectoryMode::enterDirectory()'s
    // own comment) only applies to an argument-LESS launch -- legacy's
    // own "resume last session" case. An explicit path argument
    // (including ".") always starts at the top of that directory's
    // listing, regardless of any position saved from an earlier,
    // unrelated visit (a real report: launching with "." landed on a
    // stale mid-list row instead of the top). consultHistoryOnStart
    // itself is declared earlier now (see its own comment) so the
    // startup load further up can share it too.
    // 2026-08-30、直接のユーザー報告("`open -a usilic`で起動すなわち
    // 引数無しなので前回の履歴を引き継がないと。通常ファイルで終えた
    // なら次回起動時も同ファイルから"): 引数無し起動の場合のみ、
    // Application::startResumingSession()自身のコメントが説明する
    // 通り、前回終了時に開いていたもの(history.sessionState().
    // lastOpenName)がFileモードの通常ファイルであればそれをそのまま
    // 復元する。引数付き起動、またはlastOpenNameがディレクトリ/
    // 存在しない/開けない場合は、その内部で従来通りの
    // start(startDirectory, ...)へフォールバックする。
    //
    // 2026-09-15訂正、直接のユーザー報告("④ディレクトリ引数で指定
    // したディレクトリから立ち上がらない(引数無し同様前回終了時から
    // 開始)"): この分岐の条件を誤ってconsultHistoryOnStart(2026-09-15
    // の別の変更で、ディレクトリ引数起動でも真になるよう広げたばかり
    // だった)にしてしまっていたため、ディレクトリ引数付き起動まで
    // 「引数を無視してlastOpenNameを復元する」経路に巻き込まれて
    // いた――本来この分岐が問うべきは「historyへアクセスするか
    // どうか」ではなく「実際に位置引数(ディレクトリ)が与えられたか
    // どうか」であり、それは別物(consultHistoryOnStartは両方の起動
    // シェイプでtrueになるが、lastOpenName復元は引数無し起動だけの
    // 話)だった。positionalArgs.empty()自体で判定するよう戻す
    // ――consultHistoryOnStart(historyへの読み書き自体の可否)は、
    // 引数付き起動の側の分岐でも変わらず渡し続ける(このstartDirectory
    // 自身のcursor位置保存/復元等は、ディレクトリ引数起動でも引き続き
    // history経由で行いたいため)。
    bool started = positionalArgs.empty()
                       ? app.startResumingSession(history.sessionState().lastOpenName, startDirectory,
                                                   consultHistoryOnStart)
                       : app.start(startDirectory, consultHistoryOnStart);
    if (!started) {
      display.close();
      keyInput.close();
      std::cerr << "usilic: cannot list " << startDirectory << "\n";
      return 1;
    }
    offerGroffInstallOnce(app, usilicDir);
    // 2026-10-11、直接のユーザー依頼("一番最初の起動(~/.usilic/historyが空)もしくは-Hの起動オプションで
    // usilicを立ち上げた場合、^Jtusilic ~/.local/lib/usilic/HELP_ja.1^Mを実行した状態で起動"): 親は今まで
    // どおり(ls)か前回の続きから立ち上がり、HELPは子のusilic(外部ページャー、^Jtの"usilic ..."と同じ
    // runExternalUsilicPager())で開く。子へはreloadHelpText()が読んだもの(roff版のHELP.ja.1/HELP.1、
    // groffが無ければ平文のHELP)を渡す。子の側はroffをgroffで平文に組む(startPagerFileList()参照)。
    // 同日、"^Kのウィンドウも追加"(-KのKEYの子も)、"-gでは端末の大きさ・配色を受け継ぐのは親だけ"(ここではまだ
    // GUIができておらず親のウィンドウを子へ渡せなかった): 起動は実行ループの最初で(Application::launchHelpChildren())。
    // 2026-10-20、直接のユーザー提案("最初の起動では子のHELPを出さずに親がHELPで起動。次回以降は-H/-K起動または
    // 稼働中^T^H/^T^Kで子のHELP/KEYを出す、すべての子は自身を終了可"): 最初の起動は親自身がHELPを開き、-Hは
    // HELPの子、-Kは(子のKEYの窓自身でなければ)KEYの子を開く。
    bool firstStart = consultHistoryOnStart && historyText.empty();
    if (firstStart && !app.helpLoadedPath_.empty()) app.scheduleHelpInParent();
    bool keyChild = openKeyAtStart && gTransientParentPid <= 0;
    if ((openHelpChild && !app.helpLoadedPath_.empty()) || keyChild)
      app.scheduleHelpChildren(openHelpChild && !app.helpLoadedPath_.empty(), keyChild);
    // -K(子のKEYのウィンドウ自身): HELPの「：」を含む行だけのKEY(キーの発動・h・^G無し、keyListOnly()参照)を開く。
    if (openKeyAtStart && !keyChild) {
      app.setPagerLaunch(false);
      app.setKeyColonListLaunch(true);
      app.handleCommand(fm::Command::StartKey);
    }
  }


  if (guiOwnsWholeSession) {
    // Same launcher tryEnterGuiPhase() wires for the ^Og / -g-via-that-
    // path cases -- this is the THIRD, separate GUI entry point
    // (guiOwnsWholeSession's own "実はすでにGUI" launch-time-detected
    // overlay, where GUI owns the whole session from process start and
    // runTerminalPhase()/tryEnterGuiPhase() are never called at all).
    // A real user report (2026-08-17) found this branch missing the
    // wiring entirely (PROGRAM-mode `less` kept running with correct
    // keyboard input but no window ever came to front, since
    // setForegroundLauncher() had only ever been wired inside
    // tryEnterGuiPhase(), which this branch never calls).
    app.programMode().setForegroundLauncher(
        [&launchGui](const std::string &command) { return runProgramInLaunchingTerminal(command, *launchGui); });
    app.editMode().setForegroundLauncher(
        [&launchGui](const std::vector<std::string> &argv) { return runEditorInLaunchingTerminal(argv, *launchGui); });
    fm::GraphicalDisplay *guiForFrame = launchGui.get();
    app.setGuiFrameProvider([guiForFrame] { return guiFrameText(*guiForFrame); });
    app.setGuiFontProvider(
        [guiForFrame] { return guiForFrame->currentFontPath(fm::GraphicalDisplay::FontKind::Base); });
    runGuiPhase(app, *launchGui);
    // 2026-10-16、直接のユーザー依頼("最後に-gしたときの端末設定（文字・背景色）の共有は？usilic.appをクリック
    // したときもそれらの設定で起動できない？"): 端末に重ねる起動はいつも端末の大きさ・字・色に合わせる(保存した
    // 値は読まない)が、終わりにその時の字の大きさ・行数・桁数・色・端末のフォントを保存し、重ねる端末の無い起動
    // (usilic.app等、tryEnterGuiPhase()のappearance無し)がそれを使う。tryEnterGuiPhase()の終わりの保存と同じ。
    {
      fm::History::GuiPreferences prefs = history.guiPreferences();
      prefs.fontPointSize = launchGui->fontPointSize();
      prefs.rows = launchGui->horizontalRows();
      prefs.columns = launchGui->horizontalColumns();
      std::tie(prefs.lineHeightExtraHorizontal, prefs.lineHeightExtraVertical) = fm::GraphicalDisplay::lineHeightExtras();
      std::tie(prefs.letterSpacingExtraHorizontal, prefs.letterSpacingExtraVertical) =
          fm::GraphicalDisplay::letterSpacingExtras();
      fm::GraphicalDisplay::Color fg = launchGui->foregroundColor(), bg = launchGui->backgroundColor();
      prefs.hasColors = true;
      prefs.fgR = fg.r; prefs.fgG = fg.g; prefs.fgB = fg.b;
      prefs.bgR = bg.r; prefs.bgG = bg.g; prefs.bgB = bg.b;
      if (!gTerminalFontPath.empty()) prefs.terminalFontPath = gTerminalFontPath;
      history.setGuiPreferences(prefs);
    }
    app.setGuiFrameProvider(nullptr);
    app.setGuiFontProvider(nullptr);
    app.programMode().setForegroundLauncher(nullptr);
    app.editMode().setForegroundLauncher(nullptr);
  } else {
    if (forceGui && !app.wantsExit()) {
      // -g: same GUI entry point ^Og already uses mid-session, just
      // invoked once up front instead of waiting for that keypress.
      // tryEnterGuiPhase() blocks until the GUI side exits (^Og again
      // or the window's close button), so falling through to the
      // ordinary terminal-first loop below afterward is correct either
      // way -- same as if the user had pressed ^Og once by hand.
      app.setGuiToggleReturnsToTerminal(keyInput.fd() >= 0);  // ^Ogで下の端末の実行ループへ戻れる
      // 2026-10-16、直接のユーザー依頼(Ubuntu、"もう重ねなくていい。直す案で作ってみて"): 端末に重ねられない時
      // (Linuxは端末の窓の位置を調べられない)も、読み取れた端末の色・フォント(appearance)で開き、終わりに
      // 保存する(^Ogと同じ、tryEnterGuiPhase()の終わりの保存)。アイコンからの起動はその保存した設定で開く。
      if (!tryEnterGuiPhase(app, display, *terminalDisplay, history, appearance)) {
#ifdef _WIN32  // コンソールが無いので(端末の画面にも戻れない)、メッセージボックスで
        fm::showWindowsError("Cannot open the usilic window (no font found in C:/Windows/Fonts, or SDL2.dll/SDL2_ttf.dll "
                             "missing next to usilic.exe).");
#else
        std::cerr << "usilic: -g requested but no usable GUI (no font found, or no display) "
                     "-- falling back to terminal mode.\n";
#endif
      }
    }
    // 2026-08-30 (直接のユーザー報告、「usilicウィンドウの赤(終了)を
    // クリックすると暴走」): keyInput.fd() < 0は制御端末が一切無い
    // ことを意味する(Finderからのダブルクリック起動、isPipedStdin/
    // stdinPipedの同種の修正参照)。この場合、下の
    // runTerminalPhase()は自分のpoll()にSTDIN_FILENO(/dev/null相当)
    // を使い続け、常に即座にPOLLINで戻り、keyInput.readByte()も
    // fd_<0のため常に0を返す――結果、100msのはずのスリープが一度も
    // 効かない無制御のビジーループになり、CPUを食い荒らしながら
    // ウィンドウの無い状態のまま何にも反応しなくなる(赤ボタンで
    // GUIウィンドウ自体は閉じているが、その後この無限ループに落ちて
    // いた)。guiOwnsWholeSession(実はすでにGUI、上のコメント参照)が
    // 「戻る先の端末が無い」場合にrunTerminalPhase()へ一切戻らないのと
    // 同じ理由で、制御端末が無い場合はここでも同様にループへ入らず、
    // GUIウィンドウが閉じた時点で素直に終了する。forceGuiに限らず
    // 判定する(-cで強制した場合も同じ無限ビジーループに落ちることを
    // このバグ修正の動作確認中に実際に踏んだ――制御端末が無い以上、
    // -c/-g/デフォルトのどれで起動されていようと戻れる端末はどのみち
    // 無い)。
    if (keyInput.fd() < 0) {
      // 何もしない: GUIウィンドウは閉じられた(またはそもそも開けな
      // かった、あるいは-cで最初からGUIを試みてすらいない)ので、
      // 戻る先の端末が無いままこのままmain()末尾へ進み終了する。
    } else {
      while (!app.wantsExit()) {
        if (!runTerminalPhase(app, keyInput, display, forceCui)) break;
        if (app.wantsExit()) break;
        app.setGuiToggleReturnsToTerminal(true);
        // ^Og(Command::ToggleGuiMode): 同じ画面をGUIのウィンドウで。開けなければ(SSH・画面が無い等)端末のまま知らせる。
        // 端末(Terminal.app・iTerm2・GNOME Terminal等)のプロファイルの色・フォント・大きさと、ウィンドウの位置を受け継ぐ。
        auto appearance = queryRealTerminalAppearance();
        auto bounds = appearance ? queryRealTerminalWindowBounds() : std::nullopt;
        if (!tryEnterGuiPhase(app, display, *terminalDisplay, history, appearance, bounds))
          app.showNotice("No GUI available");
      }
    }
  }

  display.close();
  keyInput.close();

  // pidTempDirectory cleanup (2026-08-18 addition, a direct user
  // request): "引数付きusilic起動では終了(^X^C[return])後に/tmp/
  // .usilic.PID/を削除でいい。引数無しでは...終了後もWORK以外は削除
  // しない". For an argument-launched run, pidTempDirectory is ALSO
  // where tempRootDirectory (error/shell) lives -- see its own
  // assignment above -- so removing it here deletes everything
  // (error/shell/work/arc) as one unit, exactly as requested. For a
  // bare no-argument launch, tempRootDirectory instead falls back to
  // the persistent usilicDir (error/shell untouched by this), so
  // pidTempDirectory here only ever held work/arc -- removing it
  // cleans up WORK specifically without touching usilicDir/error or
  // usilicDir/shell. One unconditional remove_all() correctly covers
  // both cases without needing to branch on positionalArgs again here.
  {
    std::error_code ec;
    std::filesystem::remove_all(pidTempDirectory, ec);
  }

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
  app.saveShellLog();  // 2026-10-03、SHELLのログを~/.usilic/shellへ(Application::saveShellLog()参照)
  if (consultHistoryOnStart) {
    // 2026-08-30、直接のユーザー報告("通常ファイルで終えたなら次回
    // 起動時も同ファイルから"、"utf8.tarの画面（アーカイブ構成ファイル
    // 一覧）を開いていたら再起動時も同じ画面で"): SessionState::
    // lastOpenName(legacyのLINE::nameの移植)自身はHistory::
    // loadHistoryText()/serialize()経由で既に往復可能だったが、実際に
    // Applicationからこれを書くコードが一度も配線されていなかった。
    // Application::currentResumableName()自身のコメント参照(File/
    // トップレベルArchive/それ以外の判定)。他のフィールド
    // (previousDirectory等)は既存の(読み込み時の)値のまま変更しない。
    {
      fm::History::SessionState state = history.sessionState();
      state.lastOpenName = app.currentResumableName();
      state.epubVertical = false;  // 縦横は本が決める(2026-09-29、Application::openEpub()参照)
      history.setSessionState(state);
    }
    history.setSavedCharset(app.encoding());  // so a future session with no explicit override resumes in this one's charset

    // 2026-09-20追加、直接のユーザー依頼("history競合の件、1〜4の対策
    // (read-merge-write+lock)&書き込み手順を「一時ファイルに書いて
    // からrename」で実装よろしく"): 実際の保存処理(排他ロック+読み
    // 直し+このセッション自身の変更だけの再生+一時ファイル→rename)は
    // saveHistoryToDisk()に括り出してある――pushRecent()毎の即時保存
    // (この関数のもっと前、history.setOnRingPushed()自身のコメント
    // 参照)と同じ処理をここでも呼ぶだけ。sessionState/savedCharsetの
    // ようなセッション終了時にしか確定しない値は、この最後の1回でのみ
    // 反映される。
    saveHistoryToDisk(history, usilicDir, configuration.charsetExplicitlySet());
  }

  return 0;
}
