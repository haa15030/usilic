#include "application.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>

#include "child_process.hpp"
#include "mode_line.hpp"
#include "pipe_buffer.hpp"

namespace fm {

namespace {
constexpr int kLeaderJ = 0x0a;         // ^J
constexpr int kLeaderX = 0x18;         // ^X
constexpr int kLeaderO = 0x0f;         // ^O
constexpr int kEscape = 0x1b;          // ESC
constexpr int kControlS = 0x13;        // ^S
constexpr int kControlC = 0x03;        // ^C
constexpr int kControlG = 0x07;        // ^G
constexpr int kControlQ = 0x11;        // ^Q
constexpr int kCarriageReturn = 0x0d;  // ^M
// Trailing-directory truncation (2026-08-12 fix): shared by both
// drawPromptLabel() (the active-editing mode line) and
// listBrowserView_'s own ModeLineFieldsProvider direct-entry branch
// (the ^J^S/^J^C candidate-list BROWSING mode line, before editing
// starts) -- a real report + reference screenshot (=aa) found the
// latter still showed the destination directory in full, untruncated,
// so its own available width for the candidate preview text stretched
// and shrank depending on how long the directory path happened to be
// ("戻り値が「...〜」になっておらず、文字列の長さによって入力エリア
// が伸縮"). Factored out here (rather than duplicated) once a SECOND
// call site needed the exact same "...<last 16 chars>" rule
// drawPromptLabel() already established.
constexpr int kTrailingPathTailChars = 16;
std::string truncatedTrailingPath(const std::string &path) {
  if (static_cast<int>(path.size()) <= kTrailingPathTailChars) return path;
  return "..." + path.substr(path.size() - kTrailingPathTailChars);
}

// Byte count shown in the `trailing` field of every *results*-shaped
// mode line (CUT's segment content, and -- as of 2026-08-13, following
// a user request to consolidate this port's now-settled mode-line
// FORMAT into shared logic instead of re-deriving it per mode -- MAN/
// LOCATE/DU/archive-entry content too). Mirrors legacy disp_window()'s
// own generic print_arg()-populated buffer-size field (menu.c's main
// redraw loop calls print_arg() with h.bp->size for essentially every
// mode before calling disp_window() itself, not a mode-by-mode special
// case) -- reproduced here as a simple sum of each currently-loaded
// line's byte length (+1 for the newline every real line in the
// original buffer had) rather than tracking a separate raw byte count
// alongside each Mode class's own already-parsed std::vector<Line>,
// since the two always agree by construction (every line came FROM
// that many source bytes). Originally only cutContentView_'s own
// provider computed this (validated there via a direct user
// reproduction with exact expected mode-line text -- see its own
// comment); factored out once MAN/LOCATE/DU/archive content needed
// the identical computation instead of a fourth/fifth copy.
// 2026-08-15: now a thin wrapper -- see ContentView::totalByteCount()'s
// own comment for why the actual computation moved there (so
// DirectoryMode/ArchiveMode, different translation units with no
// access to this file's anonymous namespace, can share it too).
std::size_t contentByteCount(const ContentView &view) { return view.totalByteCount(); }
// K-2 (^H, M_key, "KEY"): startKey()'s own static binding table.
// Unlike HELP (which loads real prose text at runtime via
// loadHelpText()), this table is NOT parsed from usilicrc's own KEY
// section -- Configuration deliberately excludes that (see its own
// class comment: "its bindings target mode/command numbers that
// don't exist yet in this C++ port... deferred until Mode exists").
// That gap hasn't closed by rewriting Configuration's KEY parser
// (a separate, still-open project, since usilicrc's KEY syntax
// supports arbitrary user rebinding this table doesn't reproduce);
// instead, this table is a hand-maintained MIRROR of dispatchByte()'s
// /resolveCommand()'s own switch statements -- the single source of
// truth for which key sequences this port ACTUALLY recognizes right
// now. Each entry's bytes are fed through Application::feedByte(),
// one byte at a time, by executeSelectedKeyBinding() -- exactly
// as if a real user had typed that sequence live, so multi-byte
// leader sequences (^J c, ^X ^K l, etc.) naturally re-enter the same
// leaderXPending_/leaderJPending_/etc. states a live keypress would.
// M-0..M-9 (usilicrc's own 10 populated keyboard-macro slots, of the
// 20 available) ARE included below, each flagged isMacro=true so
// startKey() can render legacy's own "(M)" suffix (see legacy's
// print_key(), tcb/src/str.c, `if (i >= 80) printf(" (M)")`) -- this
// port has only ONE user-recordable macro slot of its own
// (StartMacroRecord/PlayMacro, "^X (" / "^X )" / "^X e" below), not
// legacy's 20 independently-bound ones, so these usilicrc-defined
// entries are reproduced here as literal replayed byte sequences
// (executeSelectedKeyBinding() already replays any table entry's
// bytes through feedByte() one at a time -- the exact mechanism a
// live macro playback would use) rather than as a real generic
// per-slot macro system. M-8 ("ESC t", search ~/.tcb/tcbrc section
// names) is left out: on inspection it's a throwaway self-referential
// DEMO macro baked into usilicrc's own example config, hardcoding the
// stale pre-rename "~/.tcb/tcbrc" path even in this already-renamed
// usilicrc (documented in CLAUDE.md), not a feature worth reproducing
// here. M-9 ("ESC l") USED to be excluded for the same reason -- see
// that same history -- but as of 2026-08-18 (executeStartLog()'s own
// comment) it's a real, distinct feature (LOG), not a demo macro
// replay, so it gets a normal entry below instead. Arrow/PageUp/
// PageDown special keys are omitted too -- every one of them already
// has a plain-byte alias in this table (j/k, ^N/^P, space/^F/^V,
// b/^B), so nothing is unreachable by leaving KeyInput::SpecialKey-
// only entries out.
struct KeyTableEntry {
  const char *keyLabel;
  const char *description;
  std::vector<int> bytes;
  bool isMacro = false;
};

const KeyTableEntry kKeyTable[] = {
    // Cursor / paging / misc (no leader)
    {"j", "Move window down", {'j'}},
    {"k", "Move window up", {'k'}},
    {"^N", "Move cursor down", {0x0e}},
    {"^P", "Move cursor up", {0x10}},
    {"space", "Page down", {' '}},
    {"^F", "Page down", {0x06}},
    {"^V", "Page down", {0x16}},
    {"b", "Page up", {'b'}},
    {"^B", "Page up", {0x02}},
    {"return", "Open selected entry", {kCarriageReturn}},
    {"^G", "Back / cancel", {kControlG}},
    {"^S", "FGREP", {kControlS}},
    {"^space", "Toggle mark (CP/MV/RM/ARC)", {0x80}},
    {"^R", "Toggle chronological HISTORY view (current list browser)", {0x12}},
    {"^L", "Redraw screen", {0x0c}},
    {"^T", "TCB banner", {0x14}},
    {"q", "Exit tcb (pager)", {'q'}},
    // Macros (usilicrc M-0..M-7, see this table's own comment above)
    {";", "Back, cursor up, select (M-0)", {kControlG, 0x10, kCarriageReturn}, true},
    {":", "Back, cursor down, select (M-1)", {kControlG, 0x0e, kCarriageReturn}, true},
    {"+", "Back twice, cursor up, select twice (M-2)", {kControlG, kControlG, 0x10, kCarriageReturn, kCarriageReturn}, true},
    {"*", "Back twice, cursor down, select twice (M-3)", {kControlG, kControlG, 0x0e, kCarriageReturn, kCarriageReturn}, true},
    {",", "Select, then cursor up (M-4)", {kCarriageReturn, 0x10}, true},
    {".", "Select, then cursor down (M-5)", {kCarriageReturn, 0x0e}, true},
    {"<", "Select, back, cursor up, select (M-6)", {kCarriageReturn, kControlG, 0x10, kCarriageReturn}, true},
    {">", "Select, back, cursor down, select (M-7)", {kCarriageReturn, kControlG, 0x0e, kCarriageReturn}, true},
    // ESC-leader: "^[" (not the spelled-out word "ESC"), matching
    // legacy's own KEY listing exactly (str.c's print_key()/tobo(),
    // confirmed via a real PTY dump of /usr/local/bin/usilic: "^[<",
    // "^[>", "^[^M") and, per the user's own 2026-08-05 correction,
    // consistent with "space"/"return" below being their real Apple
    // Keyboard keytop labels rather than "Space"/"Enter" -- "ESC" as
    // a spelled-out word sat awkwardly between the single-control-char
    // symbols (^J/^X/^O) and those keytop labels, matching neither.
    {"^[s", "SHELL", {kEscape, 's'}},
    {"^[l", "LOG", {kEscape, 'l'}},
    {"^[<", "Go to top", {kEscape, '<'}},
    {"^[>", "Go to bottom", {kEscape, '>'}},
    {"^[^M", "Read as is", {kEscape, kCarriageReturn}},
    // ^J-leader. No space between the leader and the key that
    // follows (^J^H, not "^J ^H") -- matches legacy's own KEY listing
    // (str.c's print_key(), tobo()) and keeps this table's own ^J^H
    // "HELP" entry internally consistent with everything else here;
    // the wide gap the space produced before startKey()'s fixed
    // column padding was the whole reason for this change.
    {"^J^K", "Go to file-selector", {kLeaderJ, 0x0b}},
    {"^J^S", "EGREP", {kLeaderJ, kControlS}},
    {"^Jc", "CUT (fixed string, buffer)", {kLeaderJ, 'c'}},
    {"^J^C", "CUT (regex, buffer)", {kLeaderJ, kControlC}},
    {"^Jb", "CUT (fixed string, file)", {kLeaderJ, 'b'}},
    {"^J^B", "CUT (regex, file)", {kLeaderJ, 0x02}},
    {"^Jm", "MAN", {kLeaderJ, 'm'}},
    {"^Jp", "PROGRAM", {kLeaderJ, 'p'}},
    {"^Jl", "LOCATE", {kLeaderJ, 'l'}},
    {"^Jd", "DU", {kLeaderJ, 'd'}},
    {"^J^R", "COMMAND (programs run at SHELL)", {kLeaderJ, 0x12}},
    {"^Je", "EDIT", {kLeaderJ, 'e'}},
    {"^J^H", "HELP", {kLeaderJ, 0x08}},
    {"^J.", "SHELL: cd <directory>", {kLeaderJ, '.'}},
    {"^J,", "SHELL: ls -l", {kLeaderJ, ','}},
    {"^Jw", "SHELL: insert access-name", {kLeaderJ, 'w'}},
    {"^J^W", "SHELL: insert buffer content", {kLeaderJ, 0x17}},
    // ^X-leader (same no-space rationale as ^J-leader above)
    {"^Xc", "CP", {kLeaderX, 'c'}},
    {"^Xm", "MV", {kLeaderX, 'm'}},
    {"^Xr", "RM", {kLeaderX, 'r'}},
    {"^Xf", "FILE: input file name", {kLeaderX, 'f'}},
    {"^Xd", "DIR: input directory name", {kLeaderX, 'd'}},
    {"^X^F", "FILE: select file name", {kLeaderX, 0x06}},
    {"^X^D", "DIR: select directory name", {kLeaderX, 0x04}},
    {"^X^C", "EXIT", {kLeaderX, kControlC}},
    {"^X^W", "Save buffer to file", {kLeaderX, 0x17}},
    {"^X^Y", "Mark all (CP/MV/RM/ARC)", {kLeaderX, 0x19}},
    {"^X^N", "Clear all marks (CP/MV/RM/ARC)", {kLeaderX, 0x0e}},
    {"^X(", "Begin macro recording", {kLeaderX, '('}},
    {"^X)", "End macro recording", {kLeaderX, ')'}},
    {"^Xe", "Execute recorded macro", {kLeaderX, 'e'}},
    {"^Xs", "Restart SHELL", {kLeaderX, 's'}},
    {"^Xa", "ARC: expand archive", {kLeaderX, 'a'}},
    // ^X^K-leader (same no-space rationale as ^J-leader above)
    {"^X^Kc", "CP with SHELL window", {kLeaderX, 0x0b, 'c'}},
    {"^X^Km", "MV with SHELL window", {kLeaderX, 0x0b, 'm'}},
    {"^X^Kr", "RM with SHELL window", {kLeaderX, 0x0b, 'r'}},
    {"^X^Ka", "ARC: expand with SHELL window", {kLeaderX, 0x0b, 'a'}},
    {"^X^Ke", "ERROR", {kLeaderX, 0x0b, 'e'}},
    {"^X^Kl", "LOG: SHELL output", {kLeaderX, 0x0b, 'l'}},
    // ^O-leader (same no-space rationale as ^J-leader above). ^Og
    // (ToggleGuiMode) is deliberately NOT listed here: the user found
    // by hand that once a real run (app/main.cpp) is started with -g,
    // there's no way back to -c/real-terminal state, so it isn't
    // actually a toggle in practice -- listing it as one in the KEY
    // reference would be misleading. The binding/dispatch itself is
    // untouched (still reachable by pressing ^O g directly); only the
    // reference-list row was removed.
    {"^Oh", "Switch scroll mode", {kLeaderO, 'h'}},
    {"^Ol", "Switch ls output format", {kLeaderO, 'l'}},
    {"^Oa", "Switch charset (ASCII/EUC/UTF-8)", {kLeaderO, 'a'}},
    {"^O^K", "Go to parent directory", {kLeaderO, 0x0b}},
    {"^O^M", "Read per line", {kLeaderO, kCarriageReturn}},
};

// Ported from menu.c's init_cut2() call for M_help/T_help -- the
// fixed section-header pattern HELP's own CUT split always uses (see
// class comment). Legacy's OWN pattern (HELP.jp 2.4's own worked
// example: "^  [0-9]^H[0-9]+\.") is written for RAW, unprocessed
// bytes -- HELP.jp/HELP's section numbers are nroff-style overstrike-
// bold (each character doubled as "X\bX", see PipeBuffer's own
// A_md/A_md2 handling), so legacy's pattern really just anchors on
// "two leading spaces, one bold-doubled digit, then a period" and
// relies on the SAME literal backspace still being present in the
// buffer it matches against. This port's own wrapHelpText() already
// runs the raw text through PipeBuffer BEFORE cutMode_.split() ever
// sees it, which strips every overstrike pair down to a single
// (still just plain, non-doubled) character -- so matching against
// literal "\bH" or double-digit-in-a-narrow-range bytes can never
// hit anything in THIS port's own already-clean helpLines. Found via
// a real user report (2026-08-01: "HELPのCUTでマクロ機能を試そうと
// したが...効かない") after a PTY test showed HELP loading as a
// single 1-line, unsplit CUT segment ("1/1 END") instead of a proper
// section-by-section table of contents. The previous
// "^  [1-6][1-6]+\." here (two ADJACENT digits, both artificially
// restricted to 1-6) never matched anything either, on raw OR
// stripped text -- confirmed by generating a stripped-text dump of
// the real doc/HELP.jp and checking headers span "1." (bare chapter
// titles) through "2.18." (two-digit subsections) and "5.3.": no
// header is two adjacent digits, and several use digits outside
// 1-6 (e.g. "2.18.", "2.9."). "^  [0-9]+\." (one-or-more digits then
// a period, against the CLEAN de-overstruck text) matches every one
// of them -- it doesn't need to consume a header's OWN trailing
// ".M." suffix (e.g. matching just "2." out of "2.4.") since CUT
// split only needs the match to anchor where each new section
// starts, not span the whole header.
const char *const kHelpPattern = "^  [0-9]+\\.";

// See tempRootDirectory_'s own comment (application.hpp): an explicit
// `tempRootDirectory` (main()'s own /tmp/.usilic.<pid>, an argument-
// launched run) wins; a bare no-argument run passes "" here and falls
// back to the same `<homeDirectory>/.usilic` every OTHER path in this
// class already used exclusively before 2026-08-18.
std::string resolveTempRoot(const std::string &homeDirectory, const std::string &tempRootDirectory) {
  return tempRootDirectory.empty() ? (homeDirectory + "/.usilic") : tempRootDirectory;
}
}  // namespace

Application::Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
                          Charset::Encoding encoding, int width, ShellOptions shellOptions,
                          std::string homeDirectory, std::string tempRootDirectory, std::string workRootDirectory,
                          std::string bootOptions)
    : display_(display),
      pathResolver_(homeDirectory),
      tempRootDirectory_(resolveTempRoot(homeDirectory, tempRootDirectory)),
      workRootDirectory_(resolveTempRoot(homeDirectory, workRootDirectory)),
      bootOptions_(std::move(bootOptions)),
      errorLogPath_(tempRootDirectory_ + "/error"),
      logFilePath_(tempRootDirectory_ + "/shell"),
      usilicrcPath_(homeDirectory + "/.usilic/usilicrc"),
      historyFilePath_(homeDirectory + "/.usilic/history"),
      encoding_(encoding),
      configuration_(configuration),
      history_(history),
      directoryView_(display),
      fileView_(display),
      grepView_(display),
      cutView_(display),
      cutContentView_(display),
      manView_(display),
      locateView_(display),
      archiveView_(display),
      archiveContentView_(display),
      duView_(display),
      listBrowserView_(display),
      keyView_(display),
      promptEditor_(display, encoding),
      directoryMode_(directoryView_, modeStack_, history, configuration),
      fileMode_(fileView_, history, encoding, width),
      grepMode_(grepView_, configuration),
      cutMode_(cutView_, cutModeStack_, configuration),
      manMode_(manView_, history, configuration, encoding, width),
      programMode_(keyInput, history),
      locateMode_(locateView_, configuration),
      archiveMode_(archiveView_, history, configuration),
      shell_(display, [&] {
        shellOptions.rows = display.rows();
        shellOptions.columns = display.columns();
        shellOptions.encoding = encoding;
        return shellOptions;
      }()),
      cpMvRmMode_(configuration, keyInput, &errorLogPath_),
      duMode_(duView_, history, configuration),
      listBrowserMode_(listBrowserView_),
      editMode_(keyInput, configuration),
      execMode_(keyInput) {
  layoutViews();
  // K-65 (^I): real filesystem-backed completion for Shell's own
  // command line, resolved relative to whatever directory Shell last
  // reported itself in (its own prompt-parsing tracks this already --
  // see Shell::currentDirectory()'s own comment). Previously left
  // unset entirely, meaning Tab silently did nothing in Shell mode
  // despite Shell::onTab() otherwise being fully implemented and
  // tested against a synthetic provider (see test_shell.cpp).
  shell_.setCompletionProvider(
      [this](const std::string &partial) { return completeFilesystemPath(partial, shell_.currentDirectory()); });
  // K-50 (^J^R)'s own RingCategory::ShellCommand history (2026-08-04
  // correction: NOT plain ^R -- see ToggleHistoryView's own comment),
  // plus this port's new ^P/^N command recall (see feedShellByte()'s
  // own comment). Shell::submitLine() has always called historyRecorder_
  // (if set) after successfully running a typed command -- but
  // nothing here ever SET it, discovered while building ^P/^N
  // (2026-08-02): this category was silently empty forever, meaning
  // ^J^R's own picker never had anything to show either despite being
  // fully wired otherwise -- the exact "wired but never connected"
  // pattern found repeatedly throughout this project (see
  // setCompletionProvider()'s own history just above).
  shell_.setHistoryRecorder(
      [this](const std::string &line) { history_.pushRecent(RingCategory::ShellCommand, line); });
  // 2026-08-17 fix: this hook (Shell::submitLine()'s own dispatch,
  // ported from input_shell()'s do_prog() hand-off) existed but was
  // never wired -- the exact "wired but never connected" pattern noted
  // just above for setHistoryRecorder(). A real report ("'less CLA'^I
  // →[return]→less画面でなく「cat CLAUDE.md」のような画面...[return]
  // 連打で一行ずつ進む") found that typing a recognized PROGRAM command
  // (e.g. "less CLAUDE.md") AT THE SHELL PROMPT sent it to the real,
  // pty-attached zsh child instead -- which DOES run `less`, but
  // Shell's own OUTPUT pipeline (line-oriented, prompt-detecting, see
  // the class comment) was never built to carry a full-screen program's
  // raw cursor-control/repaint escape sequences, garbling them into
  // scrollback "text", and Shell's own commandLine_ never forwards
  // per-keystroke bytes to the child either (see feedShellByte()'s ESC
  // comment) -- explaining the "bare Enter reaches less as its own
  // next-line keybinding" symptom exactly. Reuses isRecognizedProgram()/
  // cdWrap() (see executeProgram()'s own comment on why a bare command
  // needs the cd prefix: usilic never chdir()s, and this handoff runs
  // via ChildProcess::runInteractive() -- a fresh fork/exec, NOT the
  // pty-attached zsh -- so it inherits usilic's own process cwd, not
  // wherever Shell is currently browsing) resolved against
  // shell_.currentDirectory() instead of directoryMode_'s.
  // historyRecorder_ is deliberately NOT also invoked here:
  // ProgramMode::run()'s own recordedCommand parameter already pushes
  // into the exact same RingCategory::ShellCommand ring.
  shell_.setProgramLauncher([this](const std::string &programToken, const std::string &fullLine) -> bool {
    if (!isRecognizedProgram(programToken)) return false;
    display_.flush();
    shell_.setBusy(true);
    programMode_.run(cdWrap(shell_.currentDirectory(), fullLine), fullLine);
    shell_.setBusy(false);
    shell_.scrollback().redraw();
    return true;
  });
  // Mode-line fields, ported from disp_shell()'s own
  // disp_window(..., T_shell, name="SHELL", prev, arg, ...) call
  // (shell.c): "SHELL" fixed on the left; on the right, the trailing
  // field is "(program) shellReturnName_" -- "(bash)"/"(zsh)" (see
  // Shell::programName()'s own comment, ported from disp_shell()'s
  // own sprintf("%c(%s)", K_z, chp->argv[0])) followed by
  // shellReturnName_ (see its own comment -- the previous mode's own
  // name, snapshotted by ensureShellActive()/shellRestart()). A real
  // user report (2026-08-02, comparing side by side against the
  // legacy build) found the "(program)" piece missing entirely from
  // this port's own SHELL mode-line. A live callback (like
  // DirectoryMode's own provider) since both pieces can change across
  // separate Shell entries/restarts within the same session.
  // 2026-08-17 fix: `trailing`/`shellReturnName_` used to be
  // concatenated into ONE string ("(zsh) /path/") and drawn as a
  // single unbroken standout run -- a real report (screenshot =cl)
  // found no visible separator between them and no padding to the
  // row's right edge. disp_window() (legacy disp.c, read directly)
  // draws these as two SEPARATE standout runs with a genuine plain
  // gap between (arg's own `vt_endstand(); vt_putchar(' ')` before
  // `prev` starts its own `vt_stand()` run), and explicitly pads
  // `prev` with spaces out to `vt_col(0)` before printing it in
  // standout -- the exact shape trailing/trailing2/fillTrailingToEdge
  // already provide elsewhere (see DirectoryMode's own provider).
  // When there's no previous-mode name at all (shellReturnName_
  // empty -- legacy's `*prev == K_z` case), legacy falls back to a
  // bold, right-aligned program name/version instead of padding;
  // this port's existing `rightWatermark` fallback already implements
  // that same fallback, just needed wiring here too.
  shell_.scrollback().setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    std::string trailing = "(" + shell_.programName() + ")";
    bool hasPrev = !shellReturnName_.empty();
    return {"SHELL", trailing, /*fillTrailingToEdge=*/hasPrev,
            /*rightWatermark=*/hasPrev ? "" : "usilic v1.5b", /*trailing2=*/shellReturnName_};
  });
  // Mode-line fields for KEY (^H): "KEY" fixed on the left, and
  // whichever mode KEY was invoked from's own name (previousModeName(),
  // the SAME helper Shell's own trailing field above uses) as the
  // trailing field on the right -- matching a real legacy screenshot
  // (2026-08-02 user report) that showed e.g. "KEY .../HELP.rej" after
  // invoking KEY while viewing that file. A live callback since
  // keyReturnMode_ changes across separate KEY entries.
  keyView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    return {"KEY", previousModeName(keyReturnMode_), /*fillTrailingToEdge=*/false, /*rightWatermark=*/"",
            /*trailing2=*/""};
  });
  // Mode-line fields for the LIST-based history pickers (K-45..K-49,
  // ^X^F/^X^D/^R/etc): the category itself ("FILE"/"DIR"/"COMMAND"/
  // etc, matching legacy's own " FILE_HISTORY"-style naming,
  // RingCategory's own comments) as the LEFT-aligned name field, and
  // the highlighted item's own string as the trailing field -- a real
  // user report (2026-08-02: "^X^F（FILE）が置き換わってない") found no
  // indication anywhere of WHICH history category was being browsed,
  // which this still fixes, but the initial fix (2026-08-02) put the
  // two fields backwards AND wrapped the category in parentheses (the
  // parens are reserved for the trailing PROGRAM-name field elsewhere,
  // e.g. DirectoryMode's own "(ls)" -- a real side-by-side screenshot
  // comparison against legacy, 2026-08-03, showed the category name
  // belongs unparenthesized at the LEFT edge, e.g. "FILE 1/5 END
  // /path/to/file", not "(FILE)" trailing after the path). A live
  // callback since cursorIndex, listBrowserCategory_, AND (2026-08-04
  // addition) listBrowserChronological_ all change across separate
  // entries/toggles: when the user has pressed ^R to switch the
  // current list into its chronological "HISTORY" view (see
  // ToggleHistoryView's own comment), the name field shows the fixed
  // literal "HISTORY" instead of the category's own normal label,
  // matching legacy's mp[M_hist]==" HISTORY" applying uniformly to
  // T_hfile/T_hdir/etc regardless of which underlying ring is shown.
  //
  // 2026-08-05 addition, prompt-triggered recalls only
  // (promptHistoryRecallKind_ set -- see startPromptHistoryRecall()'s
  // own comment for the full story): name is ALWAYS "HISTORY" here
  // (there's no separate sorted view for these categories to fall
  // back to), and rightWatermark shows the PROMPT's own category
  // label (promptLabelText(), e.g. "CUT"/"FGREP"/"MAN") -- a real
  // user reproduction (screenshot ⑪, 2026-08-05) confirmed this exact
  // shape: "HISTORY" + position + the selected item, with "CUT"
  // right-aligned separately, disambiguating WHICH prompt's history
  // this is (unlike FILE/DIR's own ^X^F/^X^D-triggered screen, which
  // needs no such badge -- how you got there already makes the
  // category obvious).
  listBrowserView_.setModeLineFieldsProvider([this](int cursorIndex) -> ContentView::ModeLineFields {
    const auto &items = listBrowserMode_.items();
    bool inRange = cursorIndex >= 0 && cursorIndex < static_cast<int>(items.size());
    // 2026-08-05 fix: an EMPTY list (^J^S/^J^C's own new direct-entry
    // path -- see startRegexPromptList()'s own comment -- can land here
    // with items empty far more often than the old ^R-only path ever
    // did) used to bail out to a fully blank ModeLineFields here,
    // hiding the name/badge fields entirely alongside redraw()'s own
    // "--" placeholder row (ContentView's own comment) -- found via a
    // real doctest failure once EGREP started reaching this provider
    // with nothing in the ring yet. trailing is simply empty in that
    // case (nothing to show); the name/badge fields are just as
    // meaningful on an empty list as a populated one.
    std::string trailing = inRange ? items[cursorIndex] : "";
    // RecentFile/RecentDirectory (2026-08-17 addition): checked BEFORE
    // the generic promptHistoryRecallKind_ branch below on purpose --
    // these two are reached through the same direct-entry machinery
    // GREP/CUT/MAN use (for the typing capability), but their mode-line
    // shape needs to keep matching the OLD plain-picker rendering this
    // provider already had for them (name="FILE"/"DIR" via
    // ringCategoryLabel()-equivalent promptLabelText(), PathMiddle
    // truncation on the path, "HISTORY"+badge only once ^R has
    // switched to the chronological view) -- NOT GREP/CUT/MAN's own
    // "destination directory as trailing2" shape, which makes no sense
    // for a screen that opens paths directly rather than running a
    // search that returns somewhere.
    if (promptHistoryRecallKind_ &&
        (*promptHistoryRecallKind_ == PromptKind::RecentFile || *promptHistoryRecallKind_ == PromptKind::RecentDirectory)) {
      std::string name = listBrowserChronological_ ? "HISTORY" : promptLabelText(*promptHistoryRecallKind_);
      // fillTrailingToEdge=true (2026-08-18 fix, a real report+
      // screenshot =cr: "一文字も入力されていないと不良のまま" --
      // item 348's drawPromptLabel() fill-to-edge fix only covers the
      // screen AFTER editing starts; this READ-ONLY candidate-list
      // state, rendered by ContentView::renderModeLine() through THIS
      // provider instead, still left the same beige gap from the
      // candidate path's own end to the screen's right edge, unfilled,
      // whenever not a single character had been typed yet). No trailing2
      // exists for this kind (see this branch's own comment above), so
      // renderModeLine()'s own fillTrailingToEdge logic pads `trailing`
      // itself (the candidate path) with spaces, inside its own
      // Standout run, all the way to the edge -- the exact same
      // mechanism DirectoryMode's "(ls)" trailing field already uses.
      return {name, trailing, /*fillTrailingToEdge=*/true,
              /*rightWatermark=*/listBrowserChronological_ ? promptLabelText(*promptHistoryRecallKind_) : "",
              /*trailing2=*/"", /*trailingPlain=*/false, /*trailing2RightJustified=*/false,
              /*trailing2Kind=*/ContentView::TrailingKind::QueryEcho,
              /*trailingKind=*/ContentView::TrailingKind::PathMiddle};
    }
    if (promptHistoryRecallKind_) {
      // 2026-08-11 fix: a real report + legacy source read (mode.c's
      // T_hfgrep/T_hegrep vs plain M_fgrep/M_locate/M_cut2) -- direct
      // entry (^J^S/^J^C, startRegexPromptList()) is NOT the same
      // screen as a real ^R recall, and shows differently: the actual
      // mode name (e.g. "CUT", via promptLabelText() -- NOT
      // ringCategoryLabel(), which would wrongly say "EGREP" for CUT's
      // regex variant since it borrows that ring) on the left, and the
      // destination directory highlighted on the right (when this
      // search returns to Directory mode) instead of the fixed literal
      // "HISTORY" name + unhighlighted mode-name badge a real ^R uses.
      if (promptHistoryRecallIsDirectEntry_) {
        PromptKind kind = *promptHistoryRecallKind_;
        bool destinationIsDirectory =
            (kind == PromptKind::GrepPattern && grepReturnMode_ == ApplicationMode::Directory) ||
            (kind == PromptKind::CutPattern && cutReturnMode_ == ApplicationMode::Directory);
        std::string trailing2 = destinationIsDirectory ? truncatedTrailingPath(directoryMode_.currentDirectory()) : "";
        return {promptLabelText(kind), trailing, /*fillTrailingToEdge=*/!trailing2.empty(),
                /*rightWatermark=*/"", trailing2, /*trailingPlain=*/true, /*trailing2RightJustified=*/true};
      }
      return {"HISTORY", trailing, /*fillTrailingToEdge=*/false,
              /*rightWatermark=*/promptLabelText(*promptHistoryRecallKind_), /*trailing2=*/"",
              /*trailingPlain=*/false, /*trailing2RightJustified=*/false,
              /*trailing2Kind=*/ContentView::TrailingKind::QueryEcho,
              /*trailingKind=*/ContentView::TrailingKind::PathMiddle};
    }
    std::string name = listBrowserChronological_ ? "HISTORY" : ringCategoryLabel(listBrowserCategory_);
    // trailingKind=PathMiddle (2026-08-17 fix, =bw): this `trailing` is
    // the highlighted FILE/DIR/COMMAND item itself -- a full absolute
    // path (or a shell command, whose tail also tends to matter most)
    // that can easily exceed the terminal width. Before TrailingKind
    // existed, `trailing` had no truncation option at all, so a real
    // reproduction (^J^L→search→open a LOCATE result→^X^F) found it
    // vanishing ENTIRELY (mode line fell back to bare "FILE 1/1 END",
    // no path at all) instead of eliding like every other long field
    // already does.
    return {name, trailing, /*fillTrailingToEdge=*/false,
            /*rightWatermark=*/"", /*trailing2=*/"",
            /*trailingPlain=*/false, /*trailing2RightJustified=*/false,
            /*trailing2Kind=*/ContentView::TrailingKind::QueryEcho,
            /*trailingKind=*/ContentView::TrailingKind::PathMiddle};
  });
  // 2026-08-13 fix: a real report -- an empty history ring (e.g. a
  // fresh install's very first ^J^L/^Jm/etc, or a real ^R with nothing
  // recorded yet) showed ContentView's own DEFAULT empty-list
  // placeholder here, a bare "--". That default exists for CUT's own
  // content view (an actual unlabeled REGION within real split
  // content, see CutMode::split()'s own comment) -- semantically
  // wrong for listBrowserView_, which is a HISTORY/RESULTS list, not
  // content with regions. "「--」はCUTのみ" (per the user's own
  // correction) -- reuses GrepMode::search()'s own "No String"
  // convention (HISTORY.md's own record of that decision) instead,
  // just without a pattern prefix (there's no single "pattern" concept
  // for a FILE/DIR/MAN/PROGRAM/LOCATE/COMMAND ring the way there is
  // for an EGREP/FGREP search) -- set once, statically, since unlike
  // GrepMode's dynamic per-search version there's no varying text to
  // refresh here.
  {
    static const std::string kNoString = "No String";
    listBrowserView_.setEmptyPlaceholder(
        ContentView::Line{kNoString, std::nullopt, std::nullopt,
                           {AttributeRun{0, static_cast<int>(kNoString.size()), Display::Attribute::Underline}}},
        /*marked=*/false);
  }
  // CutContent (the currently-selected CUT segment's own content --
  // HELP's own screen included) never gets a whole-line reverse-video
  // cursor highlight, matching FileMode's own
  // setCursorHighlightEnabled(false) (see its constructor comment) --
  // confirmed by a real CUI/GUI side-by-side screenshot (2026-08-04):
  // legacy's HELP screen shows the current section's own heading line
  // ("1.  Introduction") in plain text at the top, not reverse-video,
  // since CutContent (like plain file content) isn't a selectable
  // list the way DirectoryMode's own listing is. Previously left at
  // the ContentView default (true), which is right for cutView_ (the
  // segment-LABEL list itself IS a selectable list, same as
  // DirectoryMode) but wrong for cutContentView_.
  cutContentView_.setCursorHighlightEnabled(false);
  // Mode-line fields for CutContent: "HELP" fixed (matching legacy's
  // own literal name for the help split, parallel to how Shell's own
  // field above is the fixed literal "SHELL") when cutOrigin_ is Help,
  // or the ORIGINATING file's own bare name (a File-origin CUT always
  // splits fileView_'s current content, see executeCut()'s own
  // comment -- matching FileMode's own "bare filename, not full path"
  // convention) otherwise, on the left; the segment's own byte size
  // (echoing FileMode's own byte-size trailing field) followed by the
  // CURRENTLY SELECTED segment's own label (the delimiter line it was
  // split on -- e.g. "1.  Introduction" for HELP's numbered headings,
  // see CutSegment's own comment) on the right.
  // A real 2026-08-04 user report/screenshot showed legacy's actual
  // HELP display as "HELP 01/48 47% 2618  1. Introduction": BOTH the
  // byte count ("2618", already ported in an earlier same-day revision
  // of this provider) AND the current chapter's own label appear on
  // the same row, not one replacing the other, confirmed by looking at
  // the screenshot directly a second time rather than trusting a first
  // read of only the numbers. mode.c's own T_cut case (sprintf(...,
  // " \042%s\042", cutp->str)) and menu.c's own init_cut(hp->bp,
  // ap->name, cutp->str, cutp->bp) call together show cutp->str is the
  // SELECTED segment-list entry's own label text -- appended here after
  // the byte count within the SAME trailing field (ModeLineFields has
  // no separate slot for this: rightWatermark is reserved specifically
  // for disp_window()'s "usilic v1.5b" program-name fallback, see its
  // own comment, not a general-purpose second trailing field, so
  // repurposing it here would be a mismatch of a different kind).
  // Scoped to CutContent only when THIS provider was first written
  // (not cutView_, the segment-LABEL list itself): disp_window()'s own
  // switch groups M_cut/M_cut2 with M_prog/M_lfile/M_ldir's "enter
  // string at mode-line" handling, a meaningfully different (editable-
  // prompt-like) mode-line shape that didn't obviously map onto this
  // port's simple name/trailing ModeLineFields the same way HELP's own
  // (name="HELP" is a fixed literal, not tied to that "enter string"
  // branch at all) does -- left as a follow-up rather than guessed at.
  // That follow-up is now below (2026-08-05): a direct user
  // reproduction (with exact expected mode-line text, not a guess)
  // confirmed cutView_'s OWN name/trailing shape independently.
  cutContentView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // CutOrigin::Directory (2026-08-12 fix): this provider only ever
    // handled Help vs. "assume File" (fileMode_.currentFile()) --
    // there was no CUT-from-Directory support at all when it was
    // written. A real report + reference screenshot (=ai) found
    // fileMode_.currentFile() genuinely empty for a Directory-origin
    // split (FileMode was never involved), so `name` came out as an
    // empty string -- "モードラインにモード名（CUT）が無く". Mirrors
    // cutView_'s own Directory-origin choice just below (literal
    // "CUT", not a derived name -- there's no single file to name).
    std::string name;
    if (cutOrigin_ == CutOrigin::Help) {
      name = "HELP";
    } else if (cutOrigin_ == CutOrigin::Directory) {
      name = "CUT";
    } else {
      name = std::filesystem::path(fileMode_.currentFile()).filename().string();
    }
    std::string trailing = std::to_string(contentByteCount(cutContentView_));
    if (const CutSegment *segment = cutMode_.selectedSegment()) {
      std::string label = segment->label;
      std::size_t start = label.find_first_not_of(' ');
      if (start != std::string::npos) trailing += "  " + label.substr(start);
    }
    return {name, trailing, /*fillTrailingToEdge=*/false, /*rightWatermark=*/"", /*trailing2=*/""};
  });
  // Mode-line fields for cutView_ itself (the segment-LABEL list, one
  // level ABOVE CutContent -- see its own class comment): "CUT" fixed
  // (or "HELP" for cutOrigin_==Help, matching CutContent's own literal-
  // name convention) on the left; on the right, either the FULL path
  // of the originating file (2026-08-05 user-confirmed example:
  // splitting doc/HELP.jp by "man" shows trailing
  // "/Users/haa15030/git/usilic/src/doc/HELP.jp" -- note this is the
  // FULL path, unlike FileMode's/CutContent's own "bare filename"
  // convention) for a fresh TOP-LEVEL split, or "CUT:<parent pattern>"
  // for a NESTED one (splitSelected()'s own re-split -- see
  // CutMode::parentPattern()'s own comment for why it's the PARENT
  // level's pattern, e.g. "CUT:man" after re-splitting a "man" match
  // by "tcb", not "CUT:tcb"). HELP's own top-level (non-nested)
  // trailing is deliberately left empty here -- unlike the File-origin
  // case, no concrete confirmed value exists for it yet (a real
  // screenshot elsewhere showed legacy uses a directory-path trailing
  // there too, but which directory wasn't independently confirmed);
  // left for its own follow-up rather than guessed at.
  cutView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // Log (2026-08-18, ESC l): a distinct mode-line identity from a
    // plain CUT search, matching HELP's own established precedent of
    // showing a name other than "CUT" for a specific, dedicated entry
    // point into the same underlying split() engine -- see
    // executeStartLog()'s own comment on why this is safe, unlike the
    // reverted 2026-08-05 attempt at the same "ESC l" key.
    std::string name = (cutOrigin_ == CutOrigin::Help)  ? "HELP"
                        : (cutOrigin_ == CutOrigin::Log) ? "LOG"
                                                          : "CUT";
    std::string trailing;
    if (auto parent = cutMode_.parentPattern()) {
      trailing = "CUT:" + *parent;
    } else if (cutOrigin_ == CutOrigin::File) {
      trailing = fileMode_.currentFile();
    } else if (cutOrigin_ == CutOrigin::Directory) {
      trailing = directoryMode_.currentDirectory();
    } else if (cutOrigin_ == CutOrigin::Log) {
      trailing = logFilePath_;
    }
    return {name, trailing, /*fillTrailingToEdge=*/false, /*rightWatermark=*/"", /*trailing2=*/""};
  });

  // 2026-08-11 fix: grepView_ never had a mode-line-fields provider at
  // all (every other excursion view above already does), so a real
  // FGREP/EGREP search's mode line fell back to ContentView::
  // renderModeLine()'s own "no provider" branch -- just the bare
  // position text, missing the "FGREP"/"(fgrep)"/source-directory
  // fields a real CUI screenshot shows. Mirrors DirectoryMode's own
  // "(ls)" convention (see its provider's comment) but for whichever
  // grep program actually ran, and shows the directory the search was
  // run FROM (grepReturnMode_ == File's own fileMode_.currentFile()'s
  // directory isn't tracked separately here, so this only fills
  // trailing2 for the common Directory-search case -- a search over an
  // open file's content just shows the name/(egrep) fields, matching
  // legacy's own T_grep case never populating a directory field for
  // that path either).
  // 2026-08-20 fix, a direct user request ("DIR/FILE/FGREP/EGREP:モード
  // ラインに「バイト数」が無いモードは反映して" -- see
  // ContentView::totalByteCount()'s own comment for the original
  // "全モードのモードラインに...バイト数を追加してほしい" request this
  // extends): `trailing` used to hold the "(fgrep)"/"(egrep)" program
  // badge, leaving this the only excursion view with NO byte-count
  // field at all (DIR/MAN/LOCATE/DU/ARC all already show
  // totalByteCount() in `trailing`). The badge itself is still useful
  // (matches every other mode's own "(<program>)" convention), so it
  // moves to trailing3 (see its own comment) instead of being dropped.
  grepView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    bool fixed = grepSyntax_ == GrepSyntax::Fixed;
    std::string name = fixed ? "FGREP" : "EGREP";
    const auto &cmd = configuration_.commandFor(fixed ? ConfiguredRole::Fgrep : ConfiguredRole::Egrep);
    std::string program = cmd.argv.empty() ? "" : cmd.argv.front();
    std::string badge = program.empty() ? "" : "(" + program + ")";
    std::string trailing2 = (grepReturnMode_ == ApplicationMode::Directory) ? directoryMode_.currentDirectory() : "";
    // trailing2 here is a PATH (the directory the search ran from), not
    // a typed search string -- see ContentView::TrailingKind's own
    // comment for why paths (Path) opt out of QueryEcho's 16-char tail
    // cap.
    ContentView::ModeLineFields fields{name,
                                        std::to_string(grepView_.totalByteCount()),
                                        /*fillTrailingToEdge=*/!trailing2.empty(),
                                        /*rightWatermark=*/"",
                                        trailing2,
                                        /*trailingPlain=*/false,
                                        /*trailing2RightJustified=*/false,
                                        /*trailing2Kind=*/ContentView::TrailingKind::Path};
    fields.trailing3 = badge;
    return fields;
  });
  // grepView_'s own empty-result placeholder ("<pattern>: No String",
  // "No String" underlined -- see ContentView::setEmptyPlaceholder()'s
  // own comment) is set dynamically, per search, by GrepMode::search()
  // itself (it's the one place that actually knows the pattern just
  // searched) rather than statically here.

  // 2026-08-13: manView_/locateView_/duView_/archiveContentView_ had NO
  // ModeLineFieldsProvider at all until now -- a user observation ("モード
  // ラインの現状を見て...フォーマットはすでに決まってる...共有ルーチン化
  // もできるのでは") prompted an audit of every ContentView this
  // Application owns against the other 8 already-registered providers,
  // which turned up these 4 as genuinely bare (position text only, no
  // name/trailing/badge) -- the same class of gap items 280-290 each
  // found one at a time in different views, just never systematically
  // checked for ALL of them at once before. `name` mirrors legacy's own
  // ap->name persistence (set to the mode's own name string by
  // m_locate()/m_man() at PROMPT-entry time, and never overwritten again
  // before the results screen renders -- confirmed by reading
  // tcb/src/menu.c's m_locate() directly, and already independently
  // established for CUT/GREP's own literal "CUT"/"EGREP" names below and
  // above) -- NOT a content-derived name the way FileMode's bare filename
  // is (File's ap->name genuinely IS the filename in legacy, a real
  // difference in how that one mode's transition populates it, not an
  // oversight). `trailing` reuses contentByteCount() -- the same
  // generic byte-count field legacy's own disp_window() shows via
  // print_arg()'s h.bp->size for essentially every mode's results screen
  // (menu.c's main redraw loop, not a per-mode special case), already
  // established and user-validated for cutContentView_ above. None of
  // these 4 have a natural trailing2/rightWatermark counterpart the way
  // Directory's "(ls)"+"usilic v1.5b" or File's byte-size+directory pair
  // do (no second concept to pair the byte count with here), so neither
  // is set -- matching cutContentView_'s own precedent, not a partial
  // implementation. Flagged as source-derived, not yet independently
  // confirmed against a real CUI screenshot the way Directory/File/CUT
  // were (this port has no legacy binary to run for direct comparison) --
  // a reasonable-confidence first pass per the user's own framing of
  // this round as a "机上の構想...実地検証必須" (desk design needing
  // field verification), not a guess made without reading the source.
  // 2026-08-16 fix: trailing2 was empty (see the now-stale comment
  // above about "no natural trailing2 counterpart") -- legacy's own
  // tcb/src/sub.c print_arg() T_man case was independently given the
  // same "%d (%s)" byte-count+label shape as T_dir's "(ls)" this same
  // day (see CLAUDE.md items 305/306), so "(man)" is the real
  // counterpart after all, matching DirectoryMode's trailing/trailing2
  // split below (also confirmed to already render with the correct
  // plain-space gap between the two reverse-video segments, unlike
  // legacy's own single-run rendering of this pair -- see disp.c's
  // disp_window() fix from the same round).
  manView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (manMode_.currentPage().empty()) return {};
    return {"MAN", std::to_string(contentByteCount(manView_)), /*fillTrailingToEdge=*/false, /*rightWatermark=*/"",
            /*trailing2=*/"(man)"};
  });
  // 2026-08-14 fix, then corrected same-day: a real report ("^J^L→
  // '.txt'[return]→戻り値「LOCATE: .txt」が無い") pointed out LOCATE's
  // own results screen had nowhere at all showing what was actually
  // searched, unlike GREP's own results screen (whose 戻り値/trailing2
  // convention shows the search string once you LAND somewhere via a
  // match, see DirectoryMode::setSearchAnnotation() callers). The
  // FIRST fix folded "LOCATE: <query>" into the `name` field itself
  // (replacing the bare literal "LOCATE") -- a direct user correction
  // (screenshot =aw) pointed out this was the wrong SLOT: 「LOCATE:
  // .txt」はモード名ではなく戻り値、モード名は「LOCATE」で. `name`
  // reverted to the bare literal; the same "MODE: query" string now
  // lives in `trailing2` instead, matching every other search-results
  // screen's own 戻り値 convention exactly (just applied to LOCATE's
  // own results screen, not only its eventual Directory/File
  // destination, see openSelectedLocateResult()'s identical strings).
  // 2026-08-16 fix: fillTrailingToEdge was false, so trailing2
  // ("LOCATE: <query>") stopped right after its own text instead of
  // being padded with spaces to the row's right edge -- a real report
  // + direct comparison against a working example ("戻り値がディレク
  // トリ名の時は左詰め、空きもちゃんとスペースで埋めるてるのに、なぜ
  // 「LOCATE: .txt」でそうならないのか") pointed out every OTHER
  // directory-shaped trailing2 (DirectoryMode's own "(ls)"/search-
  // annotation fallback, GrepMode's search-origin directory, etc.)
  // already gets this padding, and this was the one inconsistent
  // holdout. Safe regardless of `trailing` (the byte count): the
  // trailing-padding branch only fires when trailing2 is EMPTY, which
  // it never is here.
  locateView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (locateMode_.query().empty()) return {};
    return {"LOCATE", std::to_string(contentByteCount(locateView_)), /*fillTrailingToEdge=*/true,
            /*rightWatermark=*/"", "LOCATE: " + locateMode_.query()};
  });
  duView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (duMode_.currentPath().empty()) return {};
    return {"DU", std::to_string(contentByteCount(duView_)), /*fillTrailingToEdge=*/false, /*rightWatermark=*/"",
            /*trailing2=*/""};
  });
  // archiveContentView_ mirrors fileView_'s own shape (a single
  // extracted entry's raw content, name=bare filename not a literal
  // mode word -- see openSelectedArchiveEntry()'s own comment on why
  // this is genuinely "a file", just sourced from inside an archive
  // instead of the real filesystem) rather than the bare-byte-count-only
  // shape used just above: trailing=byte size (fillTrailingToEdge, same
  // convention as fileView_).
  //
  // 2026-08-19 fix, a direct user correction against a real report +
  // screenshot: "モードラインは下記でよろしく。「国際報道２０..._
  // web_fl.ts」「1/1 END」「111」「ARC: /Users/...rc/utf8.tar」".
  // `entry->name` is the archive-relative path (e.g. "utf8/国際報道
  // ..."), not actually a bare filename despite this comment's own
  // ORIGINAL claim -- true only for a flat archive with no directory
  // nesting (this port's own small test fixtures), never for a real
  // one like the reproduction archive here. basename() now strips
  // that prefix the same way FileMode's own provider already does for
  // a real filesystem path. trailing2 is now "ARC: <path>" (a single
  // highlighted run combining a fixed literal label with the archive's
  // own path) instead of the bare path alone.
  //
  // splitLayoutWhenCrowded (2026-08-19 same-day follow-up, a direct
  // user proposal: "モードラインの長過ぎモード名および戻り値対処と
  // しては...『「1/1 END」「111」』をまずは中央へ→残りのエリア半分
  // ずつを「モード名」「戻り値」で分け合った上で..."): a real CJK
  // filename inside this project's own test archive is long enough that
  // name+trailing2 together never fit alongside trailing (the byte
  // count) -- fitModeLineFields()'s usual tiers silently dropped
  // trailing entirely rather than show a truncated version of anything.
  // See ModeLineFields::splitLayoutWhenCrowded's/renderSplitModeLine()'s
  // own comments for the replacement layout.
  archiveContentView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    const ArchiveEntry *entry = archiveMode_.selectedEntry();
    if (!entry) return {};
    std::string basename = std::filesystem::path(entry->name).filename().string();
    // trailing2Kind=Path here (not QueryEcho) so this can still show a
    // reasonably long tail of the archive's own path if it's long --
    // see ContentView::TrailingKind's own comment for why paths opt
    // out of QueryEcho's fixed 16-char tail cap.
    ContentView::ModeLineFields fields{basename,
                                        std::to_string(contentByteCount(archiveContentView_)),
                                        /*fillTrailingToEdge=*/true,
                                        /*rightWatermark=*/"",
                                        "ARC: " + archiveMode_.currentArchive(),
                                        /*trailingPlain=*/false,
                                        /*trailing2RightJustified=*/false,
                                        /*trailing2Kind=*/ContentView::TrailingKind::Path};
    fields.splitLayoutWhenCrowded = true;
    return fields;
  });

  // usilicrc's own GREP section (2026-08-05 fix): a real CUI screenshot
  // and direct legacy-tcb PTY testing (fresh $HOME, no ~/.tcb/history at
  // all) confirmed that ^J^S (EGREP) and ^J^C (CUT's regex variant)
  // open directly into a non-empty candidate list even on a completely
  // fresh install -- the exact 10 patterns from usilicrc's own GREP
  // section ("The `string' that will be used in EGREP or CUT", e.g.
  // "diff \-", "^Question", "^Subject: "). Configuration::
  // grepSeedPatterns() already parsed this section (with its own
  // doctest coverage) but nothing ever consumed it -- seeding
  // RecentEgrepPattern with it here means ^R at any EGREP/CUT-regex
  // prompt shows these defaults even before the user has ever typed a
  // real pattern, matching legacy and directly addressing a user
  // report ("EGREPのデフォルト履歴リストは入れておく必要がある" --
  // CUT's regex variant sharing EGREP's own ring, per item 167/168,
  // needs that ring to actually HAVE a default list in it to make the
  // shared-spec claim visible, not just structurally true). Seeded
  // unconditionally at construction (not "only if empty"): an
  // argument-launched run's history_ is always empty at this point
  // regardless (main.cpp's own consultHistoryOnStart gate -- see its
  // comment -- skips loading ~/.usilic/history for those runs, matching
  // legacy's own ~/.tcb/history skip confirmed by the same PTY test),
  // and a no-argument run's already-loaded real history simply gets
  // these appended after it chronologically. This doesn't reproduce
  // legacy's own "^" config-line marking exactly where real history
  // gets interleaved among the seeds (this port's ring is a simpler
  // flat chronological deque) -- a known, deliberate simplification.
  for (const auto &seed : configuration_.grepSeedPatterns()) {
    history_.pushRecent(RingCategory::RecentEgrepPattern, seed);
  }
}

std::string Application::ringCategoryLabel(RingCategory category) const {
  switch (category) {
    case RingCategory::ShellCommand:
      // K-50 (^J^R, M_cmd, " COMMAND"), per usilicrc's own line 286
      // ("COMMAND : Select program executed at SHELL"). NOT "HISTORY"
      // -- that label is reserved for M_hist's own chronological-view
      // toggle (see ToggleHistoryView's comment), which is orthogonal
      // to which RingCategory is being browsed and applies to ANY of
      // them, this one included. An earlier version of this port
      // conflated the two (both being vaguely "history"-shaped),
      // corrected 2026-08-04 after a direct user clarification about
      // plain ^R's own, unrelated behavior surfaced the mix-up.
      return "COMMAND";
    case RingCategory::RecentFile:
      return "FILE";
    case RingCategory::RecentDirectory:
      return "DIR";
    case RingCategory::RecentManPage:
      return "MAN";
    case RingCategory::RecentLocateQuery:
      return "LOCATE";
    case RingCategory::RecentFgrepPattern:
      return "FGREP";
    case RingCategory::RecentEgrepPattern:
      return "EGREP";
  }
  return "";
}

// Shared by the constructor and handleResize(): sizes every owned
// ContentView's viewport and the prompt field off display_'s CURRENT
// geometry. The bottom row is reserved for prompt text entry (search/
// cut/man/locate/du patterns) -- legacy's equivalent is the mode-line
// row disp_window() left alone for exactly this (see ContentView's
// own class comment on why that row's rendering isn't ContentView's
// job either).
void Application::layoutViews() {
  int contentHeight = std::max(1, display_.rows() - 1);
  directoryView_.setViewport(0, contentHeight);
  fileView_.setViewport(0, contentHeight);
  grepView_.setViewport(0, contentHeight);
  cutView_.setViewport(0, contentHeight);
  cutContentView_.setViewport(0, contentHeight);
  manView_.setViewport(0, contentHeight);
  locateView_.setViewport(0, contentHeight);
  archiveView_.setViewport(0, contentHeight);
  archiveContentView_.setViewport(0, contentHeight);
  duView_.setViewport(0, contentHeight);
  listBrowserView_.setViewport(0, contentHeight);
  keyView_.setViewport(0, contentHeight);
  promptEditor_.setField(display_.rows() - 1, 0, std::max(1, display_.columns()));
}

bool Application::start(const std::string &directory, bool consultHistory) {
  currentMode_ = ApplicationMode::Directory;
  return directoryMode_.enterDirectory(directory, consultHistory);
}

// Ported from get_m()'s switch(m), cross-checked directly against
// usilicrc's own K-? default bindings (2026-07-31 correction -- an
// earlier version of this comment claimed 'j'/'k' aliased ^N/^P into
// the same M_dn/M_up cursor movement scroll.c's move_row() handles;
// usilicrc actually documents them as two DELIBERATELY DIFFERENT
// commands: K-8/K-9 "Move window one line down/up" for 'j'/'k' vs.
// K-10/K-11 "Move cursor one line down/up" for ^N/^P -- confirmed by
// grepping the legacy C sources: 'j'/'k' don't appear as literal key
// bindings anywhere in str.c/key.c/mode.c at all, only through
// usilicrc's configurable key table, and under a name ("move window")
// that doesn't match move_row()'s cursor-relative behavior). Left/
// Right-arrow==Up/Down is still a real legacy quirk (reads oddly for
// a single-column list, but is what move_row()'s switch does) and IS
// correctly ported below, unrelated to the j/k correction. M_forw/
// M_back/M_kforw/M_kback (space/b/^V and their arrow-adjacent
// bindings) map to m_page(); M_cr (^M only -- see class comment on
// why ^J is NOT folded in here) to m_cr(); M_bl (^G) to m_bl();
// M_fgrep (^S) starts a fgrep search prompt. ^J-leader sequences (e.g.
// ^J^S for EGREP, ^Jc/^J^C for CUT) aren't resolvable from one byte
// and are handled by feedByte() instead.
Command Application::resolveCommand(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (special) {
    switch (*special) {
      case KeyInput::SpecialKey::Up:
      case KeyInput::SpecialKey::Left:
        return Command::MoveUp;
      case KeyInput::SpecialKey::Down:
      case KeyInput::SpecialKey::Right:
        return Command::MoveDown;
      case KeyInput::SpecialKey::PageUp:
        return Command::PageUp;
      case KeyInput::SpecialKey::PageDown:
        return Command::PageDown;
    }
    return Command::None;
  }

  switch (byte) {
    case 'j':
      return Command::ScrollLineDown;
    case 'k':
      return Command::ScrollLineUp;
    case 0x0e:  // ^N
      return Command::MoveDown;
    case 0x10:  // ^P
      return Command::MoveUp;
    case ' ':
    case 0x06:  // ^F (M_forw2, usilicrc's own alias for Space/M_forw)
    case 'b':
    case 0x02:  // ^B (M_back2, alias for b/M_back)
    case 0x16:  // ^V
      return (byte == 'b' || byte == 0x02) ? Command::PageUp : Command::PageDown;
    case kCarriageReturn:
      return Command::Enter;
    case kControlG:
      return Command::Back;
    // 2026-08-16 fix: swapped -- a direct user report ("「;」「:」の
    // 機能が逆") traced back to kKeyTable's own M-0/M-1 entries (item
    // 177, taken VERBATIM from usilicrc's own M-? macro section: `;`
    // (M-0) = ^G ^P Enter = "Back, cursor up", `:` (M-1) = ^G ^N Enter
    // = "Back, cursor down") disagreeing with THIS dispatch, which the
    // 2026-08-03 comment on JumpNext/JumpPrevious's own declaration
    // claims was "confirmed directly by the user" as the opposite
    // (';'=MoveDown, ':'=MoveUp). Since kKeyTable's own mapping is
    // directly sourced from usilicrc itself (not a guess), it's the
    // legacy-verified ground truth here; the earlier "confirmed"
    // dispatch had the two backwards. JumpNext/JumpPrevious's own
    // bodies (Back+MoveDown+Enter / Back+MoveUp+Enter respectively)
    // are unchanged -- only WHICH character reaches which command is
    // swapped, so this is the minimal fix that also keeps
    // executeSelectedKeyBinding()'s own KEY-list replay (which feeds
    // ^G/^P/^N/^M directly, never through this switch at all) already
    // correct and untouched.
    case ';':
      return Command::JumpPrevious;
    case ':':
      return Command::JumpNext;
    case kControlS:
      return Command::StartFgrep;
    case 0x08:  // ^H (M_key, K-2, "KEY") -- see startKey()'s own comment
      return Command::StartKey;
    case 0x80:  // ^Space (M_mark) -- KeyInput reports a literal NUL/Ctrl-Space as 0x80, see its class comment
      return Command::ToggleMark;
    case 0x12:  // ^R (M_hist) -- see ToggleHistoryView's own comment
      return Command::ToggleHistoryView;
    case 0x0c:  // ^L (M_clear)
      return Command::Redraw;
    case 0x14:  // ^T (M_tcb, K-0, "TCB")
      return Command::ShowTcbBanner;
    case 'q':  // M_quit -- usilicrc scopes this to pager contexts; see handleCommand()'s own gate
      return Command::QuitPager;
    default:
      return Command::None;
  }
}

bool Application::dispatchByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  // See awaitingLocateUpdateDb()'s own comment: checked before even
  // promptKind_ (drawPromptLabel()'s own staticMessage path never sets
  // promptKind_ at all -- this isn't a real editable prompt). ^G always
  // wins immediately, at any point during the run, leaving updatedb
  // running detached in the background (per the user's own explicit
  // spec: "常時^G（←^Cではなく）で^J^Cへ移行"). Every other byte is
  // simply ignored while this message is showing, matching the
  // original design's own scope (no other interaction was ever
  // possible here).
  if (awaitingLocateUpdateDb()) {
    if (!special && byte == kControlG) {
      locateUpdateDbPid_ = -1;
      startLocatePrompt();
      return true;
    }
    return false;
  }
  if (promptKind_ != PromptKind::None) return feedPromptByte(byte);
  if (currentMode_ == ApplicationMode::Shell) return feedShellByte(byte, special);
  // GrepPattern/CutPattern/ManName's own "Select ..." list (see
  // startRegexPromptList()'s own comment): unlike every other
  // ListBrowser use (FILE/DIR/SHELL/PROGRAM pickers, all pure
  // navigate+Enter-to-select), legacy's own EGREP/CUT-regex/MAN list is
  // a live combobox where typing directly edits the highlighted
  // candidate -- see feedRegexListByte()'s own comment. Checked before
  // the generic resolveCommand() dispatch below so ordinary characters
  // (which resolveCommand() would otherwise resolve to page/scroll
  // commands, same table used everywhere else) reach it instead.
  // ManName added 2026-08-15: a real report ("^J^M→「No String」は
  // いいけどモードラインに入力できない") -- key.c's own input_key()
  // groups M_man2 (K-44's own list-editing case) in the exact same
  // switch case as M_cut2/M_locate/T_locate/M_fgrep (see its own `k =
  // vt_col(0) / 2` branch), confirming this isn't unique to GREP/CUT.
  // PROGRAM's own M_prog2 is grouped in a DIFFERENT case in that same
  // switch (only sharing the *width* calculation, not confirmed to
  // share this editing behavior) and wasn't part of this report, so
  // deliberately left out here rather than guessed at.
  // RecentFile/RecentDirectory added 2026-08-17 (K-46/K-48, ^X^F/^X^D):
  // a real report (screenshot =cn, "文字入力エリアが無い") + legacy
  // key.c read confirmed T_hfile/T_hdir are grouped in this exact same
  // f_enter=1 switch case too (see startListBrowser()'s own comment) --
  // this port had them on the OLD, plain "navigate only" ListBrowserMode
  // path instead of this live-editable one, unlike GREP/CUT/MAN.
  if (currentMode_ == ApplicationMode::ListBrowser && promptHistoryRecallKind_ &&
      (*promptHistoryRecallKind_ == PromptKind::GrepPattern || *promptHistoryRecallKind_ == PromptKind::CutPattern ||
       *promptHistoryRecallKind_ == PromptKind::ManName || *promptHistoryRecallKind_ == PromptKind::RecentFile ||
       *promptHistoryRecallKind_ == PromptKind::RecentDirectory)) {
    return feedRegexListByte(byte, special);
  }

  if (escPending_ || leaderJPending_ || leaderXPending_ || leaderXKPending_ || leaderOPending_ ||
      (!special && (byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape))) {
    return dispatchLeaderByte(byte, special);
  }

  return handleCommand(resolveCommand(byte, special));
}

// See dispatchByte()'s own comment for why this exists. A curated,
// deliberately small set of bytes stays list-navigation-only (^N/^P/
// arrows to move the selection, Enter to submit it as-is, ^G to cancel
// back out entirely, ^L to redraw) -- matching resolveCommand()'s own
// bindings for these so the muscle memory carries over. Every OTHER
// byte (ordinary typed characters, DEL/^H) is treated as "start
// editing": transitions into the normal editable GrepPattern/CutPattern
// prompt (activatePrompt()'s own already-correct plain, live-cursor
// rendering) pre-filled with the CURRENTLY HIGHLIGHTED candidate's own
// text (not blank -- this is what makes editing an EXISTING candidate
// possible, not just typing a fresh one from scratch), then immediately
// re-feeds this same byte through feedPromptByte() so the transition
// feels continuous rather than requiring a separate "start over" step.
//
// Leader sequences (^J/^X/^O/ESC, 2026-08-11 fix) are dispatched as
// COMMANDS here too, not treated as "start editing" -- a real report
// (^J^W, "SHELL: insert buffer content"/K-64, already fully
// implemented as shellInsertBufferContent() and reachable from every
// OTHER ContentView-backed screen) found it silently became literal
// "JW" text in the candidate's own pattern instead. Ported from
// key.c's own key_dir()/edit_line() switch: `case K_w: if (f_M !=
// T_input) { c = T_w; break; } default: c = k + N_char;` -- ^W (and by
// extension any leader-initiated command) resolves to its COMMAND
// meaning everywhere EXCEPT `T_input` (a plain text-entry field, this
// port's activatePrompt()-driven editing state) -- this list-browsing
// state is NOT T_input yet (that only starts once editing begins,
// below), so leader bytes should behave exactly like they do
// everywhere else. Routed to dispatchLeaderByte() directly rather than
// the top-level dispatchByte() (which would recurse straight back into
// THIS function, since currentMode_/promptHistoryRecallKind_ haven't
// changed) -- covers both the byte that STARTS a sequence (kLeaderJ
// itself) and any CONTINUATION byte while one of the leaderXPending_-
// style flags is already set from a previous call.
bool Application::feedRegexListByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  bool isNav = special == KeyInput::SpecialKey::Up || special == KeyInput::SpecialKey::Down;
  if (!special) {
    switch (byte) {
      case 0x0e:  // ^N
      case 0x10:  // ^P
      case kCarriageReturn:
      case kControlG:
      case 0x0c:  // ^L
        isNav = true;
        break;
      // ^R (2026-08-18 fix, a direct report + screenshots =cz/=da): NOT
      // in this whitelist originally, so pressing plain ^R while still
      // just BROWSING (not yet typed anything) fell through to the
      // "start editing" logic below -- which treats byte 0x12 as an
      // ordinary typed character, activates a REAL prompt pre-filled
      // with the highlighted candidate, then re-feeds 0x12 through
      // feedPromptByte(), where its own unrelated `if (byte == 0x12)
      // startPromptHistoryRecall();` (K-49, the legitimate "mid-typing
      // ^R re-opens the list" feature every other PromptKind here also
      // relies on) fires AGAIN, but this time via startPromptHistoryRecall()'s
      // own DEFAULT `directEntry = false` -- silently clobbering
      // promptHistoryRecallIsDirectEntry_ back to false even though
      // RecentFile/RecentDirectory are ALWAYS directEntry (see
      // startPromptHistoryRecall()'s own comment), which is exactly what
      // broke returnFromListBrowser()'s own =cx/=cy fix moments later on
      // the very next ^G (=da, "一覧が無い" -- it silently fell back to
      // the general "reopen a blank prompt" branch because
      // promptHistoryRecallIsDirectEntry_ no longer read true). It also
      // forced sortDirectEntryList=false along the way, so the resulting
      // screen showed an unconditional chronological "HISTORY" view
      // through the OLD pre-drawPromptLabel() rendering path instead of
      // the real toggle (=cz, "文字列入力エリアが無い" -- this detour
      // never goes through refreshDirectEntryModeLine() at all). Scoped
      // to RecentFile/RecentDirectory only (toggleListBrowserHistory()'s
      // own guard already no-ops for every other PromptKind here, so
      // GrepPattern/CutPattern/ManName's existing "^R while browsing
      // restarts the candidate list via the very same detour" behavior
      // -- unreported, presumably harmless, not this bug -- is left
      // completely unchanged rather than risked).
      case 0x12:
        if (promptHistoryRecallKind_ && (*promptHistoryRecallKind_ == PromptKind::RecentFile ||
                                          *promptHistoryRecallKind_ == PromptKind::RecentDirectory)) {
          isNav = true;
        }
        break;
    }
  }
  if (isNav) return handleCommand(resolveCommand(byte, special));
  if (escPending_ || leaderJPending_ || leaderXPending_ || leaderXKPending_ || leaderOPending_ ||
      (!special && (byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape))) {
    return dispatchLeaderByte(byte, special);
  }
  if (special) return false;  // an unhandled special key -- nothing meaningful to feed as text

  PromptKind kind = *promptHistoryRecallKind_;
  const std::string *selected = listBrowserMode_.selectedItem();
  std::string seed = selected ? *selected : promptHistoryRecallText_;
  // Captured BEFORE promptHistoryRecallKind_/etc. below stop this being
  // "the candidate list" at all -- see activatePrompt()'s own comment
  // (a direct user correction: "[bs]→「1/10 TOP」が消滅", i.e. this
  // position indicator, visible the whole time the list was just being
  // browsed, shouldn't vanish the instant editing starts).
  std::string positionText;
  int candidateCount = static_cast<int>(listBrowserMode_.items().size());
  if (candidateCount > 0) positionText = ModeLine::format(listBrowserView_.cursorIndex() + 1, candidateCount);
  promptHistoryRecallKind_ = std::nullopt;
  // See pendingSearchFromDirectEntryList_'s own comment: captured
  // BEFORE clearing, so executeGrep()/executeCut() can still see it
  // once this edit actually gets submitted.
  pendingSearchFromDirectEntryList_ = promptHistoryRecallIsDirectEntry_;
  promptHistoryRecallIsDirectEntry_ = false;
  promptHistoryRecallText_.clear();
  currentMode_ = promptHistoryRecallReturnMode_;
  // 2026-08-11 fix: this used to redraw activeView() (the DESTINATION
  // Directory/File view), replacing the candidate list on screen with
  // the full underlying listing the instant editing started -- a real
  // report + reference screenshot found this reads exactly like ^Jc's
  // own plain fgrep prompt (a blank prompt over the full listing), not
  // the live-editable COMBOBOX legacy's own M_egrep/M_cut screen is:
  // the candidate list stays fully visible, with only the mode line's
  // own row turning into an editable field over the currently-selected
  // candidate. currentMode_ is still switched to promptHistoryRecallReturnMode_
  // right above (executeCut()/executeGrep() need it correct at submit
  // time), but nothing redraws that mode's own view until the prompt
  // resolves (promptActive() gates every other redraw() call -- see
  // e.g. runGuiPhase()'s own !app.promptActive() check), so leaving
  // listBrowserView_'s own content on screen here is safe.
  listBrowserView_.redraw();
  activatePrompt(kind, seed, positionText);
  return feedPromptByte(byte);
}

// See this function's own declaration comment (application.hpp).
// Unchanged from dispatchByte()'s own former body -- only the callers
// changed (dispatchByte() itself, plus feedShellByte() now).
bool Application::dispatchLeaderByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (escPending_) {
    escPending_ = false;
    if (special) return false;
    switch (byte) {
      case 's':  // ESC s : SHELL
        return handleCommand(Command::StartShell);
      case '<':  // ESC < : go to beginning of buffer (M_head)
        return handleCommand(Command::GoToTop);
      case '>':  // ESC > : go to end of buffer (M_tail)
        return handleCommand(Command::GoToBottom);
      case kCarriageReturn:  // ESC ^M : "Read as is" (K-24, M_code)
        return handleCommand(Command::Enter);
      // ESC v : page back one page (2026-08-14, per direct user
      // instruction: "旧版のKEYには収録し忘れだが「1ページ戻る」は
      // ^[wではなくて、emacs同様^[vで反映してほしい"). A previous round
      // had found tcb/src/key.c's OWN real binding for this is bare
      // ESC-w (M_kback), not ESC-v -- reported back to the user as a
      // discrepancy rather than assumed -- and this is the user's
      // explicit, informed decision to deliberately deviate from that
      // legacy binding (an acknowledged omission in legacy's own KEY
      // list) in favor of Emacs' own M-v convention instead. Reuses the
      // same Command::PageUp already bound to plain 'b'/^B (see
      // resolveCommand()'s own space/'b' switch a few lines below) --
      // legacy's M_kback and M_back both ultimately reach the same
      // m_page() page-back behavior, so there's no separate distinct
      // command to invent here, just another key reaching the existing
      // one.
      case 'v':
        return handleCommand(Command::PageUp);
      // ESC l : LOG -- see executeStartLog()'s own comment (why this
      // is safe to add here, unlike the reverted 2026-08-05 attempt at
      // the same key -- CLAUDE.md history item 176).
      case 'l':
        return handleCommand(Command::StartLog);
      default:
        return false;  // unrecognized ESC-sequence in this slice
    }
  }

  if (leaderJPending_) {
    leaderJPending_ = false;
    if (special) return false;
    switch (byte) {
      case 0x0b:  // ^J ^K : Go to file-selector (K-18)
        return handleCommand(Command::GoToFileSelector);
      case kControlS:  // ^J ^S : EGREP
        return handleCommand(Command::StartEgrep);
      case 'c':  // ^J c : CUT (fixed string, buffer)
        return handleCommand(Command::StartCutFixed);
      case kControlC:  // ^J ^C : CUT (regex, buffer)
        return handleCommand(Command::StartCutRegex);
      case 'b':  // ^J b : CUT (fixed string, regular file)
        return handleCommand(Command::StartCutFileFixed);
      case 0x02:  // ^J ^B : CUT (regex, regular file)
        return handleCommand(Command::StartCutFileRegex);
      // K-43/K-44, K-41/K-42 (CLAUDE.md history item 289's follow-up):
      // unlike LOCATE/DU, usilicrc's own wording for these two pairs
      // really is "Input"/"Select" -- same shape as EGREP's ^J^S -- so
      // the second key of each pair now opens directly into a
      // candidate list (startPromptHistoryRecall(directEntry=true)
      // via startManList()/startProgramList()), reusing the exact same
      // machinery startRegexPromptList() already established for
      // EGREP/CUT-regex rather than duplicating it.
      case 'm':  // ^J m : MAN (K-43, "Input manual page")
        return handleCommand(Command::StartMan);
      case kCarriageReturn:  // ^J ^M : MAN (K-44, "Select manual page"; the second byte here is ^M, unrelated to a bare Enter)
        return handleCommand(Command::StartManList);
      case 'p':  // ^J p : PROGRAM (K-41, "Input program name")
        return handleCommand(Command::StartProgram);
      case 0x10:  // ^J ^P : PROGRAM (K-42, "Select program name"; the second byte here is ^P, unrelated to plain ^P/MoveUp)
        return handleCommand(Command::StartProgramList);
      // K-39/K-40 (docs/ideas/IDEAS.md LOCATE section): the two keys
      // are deliberately NOT the same command here, unlike DU's own
      // still-undifferentiated 'd'/0x04 pair a few lines below --
      // legacy's own m_locate() gave them genuinely different
      // behavior too (see CLAUDE.md history item 288's investigation),
      // just not the "Input"/"Select" shape this port originally
      // assumed. ^Jl now runs `updatedb` (a feature legacy never had
      // at all); ^J^L keeps the "open a prompt" role, now seeded with
      // the previous query (startLocatePrompt()'s own comment).
      case 'l':       // ^J l : LOCATE (K-39, "Execute `locate'" -> updatedb)
        return handleCommand(Command::LocateUpdateDb);
      case 0x0c:      // ^J ^L : LOCATE (K-40, "Redisplay window")
        return handleCommand(Command::StartLocate);
      case 'd':       // ^J d : DU
      case 0x04:      // ^J ^D : DU
        return handleCommand(Command::StartDu);
      case 0x12:      // ^J ^R : command history (M_cmd)
        return handleCommand(Command::StartCommandHistory);
      case 'e':  // ^J e : EDIT
        return handleCommand(Command::StartEdit);
      case 0x08:  // ^J ^H : HELP
        return handleCommand(Command::StartHelp);
      case '.':  // ^J . : SHELL, change current directory
        return handleCommand(Command::ShellChangeDirectory);
      case ',':  // ^J , : SHELL, execute `ls -l`
        return handleCommand(Command::ShellRunLsL);
      case 'w':  // ^J w : SHELL, insert access-name
        return handleCommand(Command::ShellInsertName);
      case 0x17:  // ^J ^W : SHELL, insert buffer content
        return handleCommand(Command::ShellInsertBufferContent);
      default:
        return false;  // unrecognized ^J-sequence in this slice
    }
  }

  if (leaderXPending_) {
    leaderXPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'c':  // ^X c : CP
        return handleCommand(Command::StartCopy);
      case 'm':  // ^X m : MV
        return handleCommand(Command::StartMove);
      case 'r':  // ^X r : RM
        return handleCommand(Command::StartRemove);
      case 'f':  // ^X f : FILE (input file name)
        return handleCommand(Command::StartFileInput);
      case 'd':  // ^X d : DIR (input directory name)
        return handleCommand(Command::StartDirectoryInput);
      case 0x06:  // ^X ^F : FILE (select file name, K-46)
        return handleCommand(Command::StartFileHistory);
      case 0x04:  // ^X ^D : DIR (select directory name, K-48)
        return handleCommand(Command::StartDirectoryHistory);
      case kControlC:  // ^X ^C : exit
        return handleCommand(Command::StartExit);
      case 0x17:  // ^X ^W : save buffer to wFilePath_
        return handleCommand(Command::SaveBufferToFile);
      case 0x19:  // ^X ^Y : mark all (CP, MV, RM, ARC)
        return handleCommand(Command::MarkAll);
      case 0x0e:  // ^X ^N : clear all (CP, MV, RM, ARC)
        return handleCommand(Command::ClearAllMarks);
      case '(':  // ^X ( : begin macro recording
        return handleCommand(Command::StartMacroRecord);
      case ')':  // ^X ) : end macro recording
        return handleCommand(Command::StopMacroRecord);
      case 'e':  // ^X e : execute recorded macro
        return handleCommand(Command::PlayMacro);
      case 0x0b:  // ^X ^K : leader for CP/MV/RM's "with SHELL window" variants (F-2 in usilicrc's own terms)
        leaderXKPending_ = true;
        return true;  // sequence in progress, not a no-op
      case 's':  // ^X s : restart SHELL (K-60)
        return handleCommand(Command::RestartShell);
      case 'a':  // ^X a : ARC expand, without SHELL window (K-58)
        return handleCommand(Command::StartArchiveExtract);
      default:
        return false;  // unrecognized ^X-sequence in this slice (^X^Kd is still future work)
    }
  }

  if (leaderXKPending_) {
    leaderXKPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'c':  // ^X ^K c : CP with SHELL window (K-51)
        return handleCommand(Command::StartCopyInteractive);
      case 'm':  // ^X ^K m : MV with SHELL window (K-53)
        return handleCommand(Command::StartMoveInteractive);
      case 'r':  // ^X ^K r : RM with SHELL window (K-55)
        return handleCommand(Command::StartRemoveInteractive);
      case 'a':  // ^X ^K a : ARC expand, with SHELL window (K-57)
        return handleCommand(Command::StartArchiveExtractInteractive);
      case 'e':  // ^X ^K e : ERROR (K-67)
        return handleCommand(Command::ShowErrorLog);
      case 'l':  // ^X ^K l : LOG (K-68)
        return handleCommand(Command::ShowShellLog);
      default:
        return false;  // ^X^Kd (kanji switch) is still future work
    }
  }

  if (leaderOPending_) {
    leaderOPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'g':  // ^O g : toggle GUI mode (this port's own addition, not a usilicrc K-? binding)
        return handleCommand(Command::ToggleGuiMode);
      case 'h':  // ^O h : switch scroll mode (K-25)
        return handleCommand(Command::ToggleScrollMode);
      case 'l':  // ^O l : switch format of `ls' output (K-26)
        return handleCommand(Command::ToggleLsMode);
      case 'a':  // ^O a : switch ascii mode (K-27)
        return handleCommand(Command::CycleCharset);
      case 0x0b:  // ^O ^K : go to parent directory (K-19)
        return handleCommand(Command::GoToParentDirectory);
      case kCarriageReturn:  // ^O ^M : "Read per line" (K-23, M_fp)
        return handleCommand(Command::Enter);
      default:
        return false;  // unrecognized ^O-sequence in this slice
    }
  }

  if (!special && byte == kLeaderJ) {
    leaderJPending_ = true;
    return true;  // sequence in progress, not a no-op
  }
  if (!special && byte == kLeaderX) {
    leaderXPending_ = true;
    return true;  // sequence in progress, not a no-op
  }
  if (!special && byte == kLeaderO) {
    leaderOPending_ = true;
    return true;  // sequence in progress, not a no-op
  }
  if (!special && byte == kEscape) {
    escPending_ = true;
    return true;  // sequence in progress, not a no-op
  }

  return false;  // unreachable given dispatchByte()'s/feedShellByte()'s own guard, kept total for safety
}

// Wraps dispatchByte() (the real logic, see its own body) with p_km
// recording bookkeeping -- see class comment's own p_km section for
// why this holds a leaderX sequence's leading byte back in
// pendingLeaderXByte_ rather than recording it immediately.
bool Application::feedByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  bool wasLeaderXPending = leaderXPending_;
  bool wasRecording = recordingMacro_;

  bool result = dispatchByte(byte, special);

  if (wasRecording) {
    if (wasLeaderXPending) {
      if (recordingMacro_) {
        // still recording -- this byte did NOT resolve to
        // StartMacroRecord/StopMacroRecord/PlayMacro, so commit both
        // the held-back leading ^X byte and this one.
        if (pendingLeaderXByte_) recordedMacro_.push_back(*pendingLeaderXByte_);
        recordedMacro_.push_back({byte, special});
      }
      // else: recording just turned off (StopMacroRecord resolved) --
      // discard both bytes, they're the control sequence itself.
      pendingLeaderXByte_.reset();
    } else if (!special && byte == kLeaderX) {
      pendingLeaderXByte_ = RecordedStep{byte, special};  // hold back until the second byte resolves
    } else {
      recordedMacro_.push_back({byte, special});
    }
  }

  return result;
}

bool Application::startMacroRecording() {
  recordingMacro_ = true;
  recordedMacro_.clear();
  pendingLeaderXByte_.reset();
  return true;
}

bool Application::stopMacroRecording() {
  if (!recordingMacro_) return false;
  recordingMacro_ = false;
  return true;
}

// Replays each recorded byte through dispatchByte() directly (NOT
// feedByte()), so a macro played back from inside an active recording
// session is never accidentally self-recorded -- see class comment.
bool Application::playRecordedMacro() {
  if (recordedMacro_.empty()) return false;
  for (const auto &step : recordedMacro_) dispatchByte(step.byte, step.special);
  return true;
}

bool Application::isRecordingMacro() const { return recordingMacro_; }
std::size_t Application::recordedMacroLength() const { return recordedMacro_.size(); }

// See class comment for the scope of this routing (and the KeyInput/
// escape-sequence gap in wiring ^[s to a real terminal). Left/Right
// move the command-line cursor; Up/Down/PageUp/PageDown (scrollback
// paging / command history) are deliberately not handled here yet.
bool Application::feedShellByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  // Global leader-key access from within Shell mode: ^J/^X/^X^K/^O/ESC.
  // A real user report (2026-08-02, comparing against the mature legacy
  // build side by side) found ^X^F/^X^C etc. work directly from within
  // SHELL there, which silently did nothing here before this existed:
  // currentMode_==Shell routed EVERY byte straight to this function with
  // no leader-key access at all.
  //
  // ESC included (2026-08-17 fix, reversing an earlier deliberate
  // exclusion): a real user report ("^[は今も利かない...いずれも他の
  // 文字列入力ルーチンでは直っていたのでこれも似たようなケースなの？")
  // pointed out this was the SAME "fixed generically in feedPromptByte()
  // but never mirrored here" gap as the ^A/^E/^B/^F/^D fix just above.
  // The earlier exclusion's own rationale -- "a real interactive shell
  // needs raw ESC constantly for its own line editor/Meta-keys/vi-mode"
  // -- turns out not to hold for THIS port: shell_.commandLine() is a
  // fully local LineEditor (matching legacy shell.c's own non-passthrough
  // design, see the architecture note in CLAUDE.md), and nothing here
  // ever forwards a raw per-keystroke byte to the child pty except
  // sendControlByte() (^C only, below) and submitLine() (the WHOLE
  // composed line, at Enter). The child shell never sees an ESC typed
  // while composing a command in usilic's own editor either way, so
  // treating it as a leader byte here (matching ^J/^X/^O, and matching
  // feedPromptByte()'s escPending_ handling) only adds the missing
  // global command access -- it doesn't take anything away from real
  // shell usage. (A genuinely raw, interactive child like `vim`/`less`
  // launched via do_prog() is a completely separate, real pty-passthrough
  // code path that this doesn't touch.)
  bool shellLeaderPending = leaderJPending_ || leaderXPending_ || leaderXKPending_ || leaderOPending_ || escPending_;
  if (!special && (shellLeaderPending || byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape)) {
    if (shellLeaderPending) {
      // Completing a sequence -- switch back to whichever mode SHELL
      // was entered from BEFORE dispatching, exactly as if the user
      // had pressed ^G first (matching legacy, and matching KEY
      // mode's own executeSelectedKeyBinding() precedent): several
      // downstream commands (startListBrowser() for ^X^F, etc.) have
      // their own currentMode_==Directory-style guards that would
      // otherwise reject them outright while still Shell. An
      // unrecognized sequence (a genuine typo) still leaves Shell as
      // a side effect -- accepted as a rare, minor edge case rather
      // than adding rollback machinery for it.
      currentMode_ = shellReturnMode_;
      activeView().redraw();
    }
    return dispatchLeaderByte(byte, special);
  }

  if (special) {
    switch (*special) {
      case KeyInput::SpecialKey::Left:
        shell_.commandLine().moveLeft();
        shell_.commandLine().redraw();
        return true;
      case KeyInput::SpecialKey::Right:
        shell_.commandLine().moveRight();
        shell_.commandLine().redraw();
        return true;
      default:
        return false;
    }
  }

  if (byte == kControlG) return returnFromShell();
  // K-0 (^T, ShowTcbBanner): 2026-08-18 fix, a direct user report
  // ("SHELLで^Tが利かない"). ^T isn't a leader byte (kLeaderJ/X/O/ESC
  // above) -- resolveCommand() maps it directly, unprefixed, for every
  // OTHER mode -- so it fell all the way through this function to the
  // plain command-line-editing fallback at the bottom, silently
  // inserted (or dropped, being a control byte) into the shell command
  // line instead of doing anything banner-related. Mirrors the leader-
  // sequence handling above: switch back to whichever mode SHELL was
  // entered from FIRST (executeShowTcbBanner() unconditionally sets
  // fileReturnMode_ to Directory afterward regardless of where ^T was
  // pressed from, matching every other entry point -- SHELL isn't a
  // special case there, see its own comment), then redraw before
  // dispatching in case it fails and leaves this screen showing.
  if (byte == 0x14) {
    currentMode_ = shellReturnMode_;
    activeView().redraw();
    return handleCommand(Command::ShowTcbBanner);
  }
  // ^P/^N and Enter-while-browsing below are ported from legacy's
  // shell.c M_kup/M_kdn: a cursor moves directly through scrollback_'s
  // OWN lines_ (the same unified prompt+output history real content
  // lives in, not a separate array) -- see Shell::freezePromptLine()'s
  // own comment for the data-model half of this. A real user report
  // (2026-08-02) pointed out that an earlier version of this (a
  // synthetic RingCategory::ShellCommand-backed index, entirely
  // disconnected from what's actually on screen) was "vibe coding":
  // reaching for whatever was easiest to bolt on on top of the
  // existing split scrollback_/commandLine_ design instead of reading
  // legacy's actual mechanism first. ContentView::moveUp()/moveDown()
  // already implement the exact cursor-move-with-boundary-bell
  // semantics needed here (used elsewhere for plain list navigation),
  // so ^P/^N end up needing no bespoke state of their own at all.
  if (byte == 0x10) {  // ^P
    shell_.scrollback().moveUp();
    shell_.redrawPromptRow();  // moveUp() can blank commandLineRow() -- see its own comment
    moveDisplayCursorToShellHistoryLine();
    return true;
  }
  if (byte == 0x0e) {  // ^N
    shell_.scrollback().moveDown();
    shell_.redrawPromptRow();
    moveDisplayCursorToShellHistoryLine();
    return true;
  }
  // !empty() guard: with no lines at all yet, lines().size()-1
  // underflows to -1, which cursorIndex()'s default of 0 never equals
  // -- falsely reading as "browsing" and indexing lines()[0] on an
  // empty vector below.
  bool browsingHistory = !shell_.scrollback().lines().empty() &&
                          shell_.scrollback().cursorIndex() != static_cast<int>(shell_.scrollback().lines().size()) - 1;
  if (byte == kCarriageReturn) {
    if (browsingHistory) {
      // Ported from shell.c's own '\r' case: re-run the highlighted
      // line's command if it's a real, previously-submitted prompt
      // line (Line::shellCommand set by freezePromptLine()); anything
      // else (plain output text) is legacy's own "not a prompt line"
      // fallthrough -- simplified here to just snap back rather than
      // also replicating legacy's own attempt to submit raw output
      // text as a command (an edge case legacy itself handles
      // inconsistently, not worth reproducing exactly).
      const auto &line = shell_.scrollback().lines()[shell_.scrollback().cursorIndex()];
      if (line.shellCommand) {
        shell_.commandLine().setText(*line.shellCommand);
        shell_.submitLine();
      } else {
        shell_.scrollback().goToBottom();
        shell_.redrawPromptRow();  // see the browsingHistory guard below's own comment
      }
      return true;
    }
    shell_.submitLine();
    return true;
  }
  // Any content-modifying key while browsing snaps the cursor back to
  // the live prompt FIRST, matching shell.c's own default case
  // ("if (pos != M_tail) { write_comm(pv[1], M_tail, ...); ... }"
  // before falling through to handle the actual keystroke). ^C is the
  // only byte past this point that does NOT touch commandLine_ (it
  // just forwards the interrupt to the child), matching legacy's own
  // K_c case, which has no such guard either. redrawPromptRow() (not
  // just commandLine_.redraw(), which Tab/backspace/insertByte below
  // already call on their own) is needed here specifically because
  // goToBottom() itself just ran a full scrollback_ redraw that can
  // blank the prompt PREFIX text -- see redrawPromptRow()'s own
  // comment for the full story (a real user report, 2026-08-02).
  if (browsingHistory && byte != kControlC) {
    shell_.scrollback().goToBottom();
    shell_.redrawPromptRow();
  }
  if (byte == 0x12) {  // ^R -- see startShellHistoryRecall()'s own comment
    return startShellHistoryRecall();
  }
  if (byte == 0x09) {  // Tab
    shell_.onTab();
    return true;
  }
  if (byte == 0x7f || byte == 0x08) {  // DEL / ^H
    shell_.commandLine().backspace();
    shell_.commandLine().redraw();
    return true;
  }
  // ^A/^E/^B/^F/^D (2026-08-16 fix): feedPromptByte() got these wired
  // in 2026-08-12 (see its own comment) for the GENERIC prompt editor
  // (promptEditor_, used by LOCATE/CUT/GREP/etc.), but SHELL mode has
  // its own separate commandLine() LineEditor that this fix never got
  // mirrored into -- a real report ("^A^DDDD'cat'...を打とうとしたら
  // ハイライトのADDDDだった") found the exact same symptom the 2026-08-12
  // fix already describes (each control byte falling through to
  // insertByte() below and rendering as its own reversed "@"-offset
  // glyph) still happening here. Same byte values, same LineEditor
  // class, just a different instance (shell_.commandLine() instead of
  // promptEditor_) and redraw() call (commandLine().redraw(), matching
  // backspace's own pairing just above) since SHELL's own prompt row
  // isn't drawn by the generic prompt-label path.
  if (byte == 0x01) {  // ^A: beginning of line
    shell_.commandLine().moveHome();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x05) {  // ^E: end of line
    shell_.commandLine().moveEnd();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x02) {  // ^B: back one character
    shell_.commandLine().moveLeft();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x06) {  // ^F: forward one character
    shell_.commandLine().moveRight();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x04) {  // ^D: delete forward
    shell_.commandLine().deleteForward();
    shell_.commandLine().redraw();
    return true;
  }
  // ^U/^K/^Y (2026-08-17 fix): the exact same "fixed for promptEditor_,
  // never mirrored into shell_.commandLine()" gap as ^A/^E/^B/^F/^D
  // just above (see that fix's own 2026-08-16 comment) -- these three
  // were simply missed at the time. A real report: "less CLAUDE.md"
  // with the cursor on "U", ^K expected to kill to end-of-line ("less
  // CLA") or ^U expected to kill to start-of-line ("UDE.md"), either
  // way recoverable with ^Y -- none of the three did anything (the
  // bytes fell through to insertByte() below and rendered as their own
  // reversed "@"-offset glyphs instead of editing the line).
  if (byte == 0x15) {  // ^U: kill from field start to cursor
    shell_.commandLine().killToStart();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x0b) {  // ^K: kill from cursor to field end
    shell_.commandLine().killToEnd();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x19) {  // ^Y: re-insert the ^U/^K kill buffer
    shell_.commandLine().yank();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == kControlC) {
    shell_.sendControlByte(static_cast<unsigned char>(byte));
    return true;
  }
  shell_.commandLine().insertByte(static_cast<unsigned char>(byte));
  shell_.commandLine().redraw();
  return true;
}

// ^P/^N's own plain-text-cursor replacement for the whole-line
// highlight scrollback_.setCursorHighlightEnabled(false) turns off
// (see Shell's own constructor comment for why: legacy's stand_str()
// never draws that highlight for SHELL mode at all). A no-op while on
// the live prompt line itself (cursorDisplayRow() there coincides with
// redrawPromptRow()'s own commandLine_.redraw() cursor position, which
// already ran immediately before this is called at both ^P/^N call
// sites, so there's nothing to override) -- only moves the display's
// visible cursor when actually browsing scrollback_ history, so the
// live-prompt case keeps editing normally. -1 (scrolled out of view,
// shouldn't be reachable given moveUp()/moveDown() always keep
// cursorIndex_ within the viewport, but checked defensively) is simply
// skipped rather than showing the cursor somewhere meaningless.
//
// setVisibleCursor(), not moveCursor() (2026-08-04 revision -- see
// Display::setVisibleCursor()'s own comment): an earlier version used
// plain moveCursor(), which worked for -c (nothing else touches the
// screen between this call and the terminal going idle again) but not
// -g, where runGuiPhase()'s own steady ~60Hz polling could re-run a
// full, unrelated ContentView::redraw() within the SAME frame this ran
// in, silently dragging the visible cursor back to wherever that
// redraw()'s OWN incidental moveCursor() calls last landed (a real
// user report: "-gのSHELLでは...一瞬出て消えるパターン").
void Application::moveDisplayCursorToShellHistoryLine() {
  const auto &lines = shell_.scrollback().lines();
  bool browsingHistory =
      !lines.empty() && shell_.scrollback().cursorIndex() != static_cast<int>(lines.size()) - 1;
  if (!browsingHistory) return;
  int row = shell_.scrollback().cursorDisplayRow();
  if (row >= 0) display_.setVisibleCursor(row, 0);
}

ContentView &Application::activeView() { return viewForMode(currentMode_); }

// Factored out of activeView() (2026-08-18) so any code that needs the
// view for a mode OTHER than currentMode_ -- e.g. executeGrep() reading
// grepReturnMode_'s buffer -- shares the exact same mode->view mapping
// instead of re-deriving a narrower, easily-stale copy of it (see
// executeGrep()'s own comment for the bug this fixes: it used to
// hardcode File-vs-everything-else-as-Directory, silently reading the
// wrong buffer -- and redrawing the wrong ContentView on failure --
// for every OTHER mode grep can actually be launched from, Locate
// included, since startGrepPrompt() only blocks re-entry from Grep
// itself and is otherwise reachable from any mode).
ContentView &Application::viewForMode(ApplicationMode mode) {
  switch (mode) {
    case ApplicationMode::File:
      return fileView_;
    case ApplicationMode::Grep:
      return grepView_;
    case ApplicationMode::Cut:
      return cutView_;
    case ApplicationMode::CutContent:
      return cutContentView_;
    case ApplicationMode::Man:
      return manView_;
    case ApplicationMode::Locate:
      return locateView_;
    case ApplicationMode::Archive:
      return archiveView_;
    case ApplicationMode::ArchiveContent:
      return archiveContentView_;
    case ApplicationMode::Shell:
      return shell_.scrollback();
    case ApplicationMode::Du:
      return duView_;
    case ApplicationMode::ListBrowser:
      return listBrowserView_;
    case ApplicationMode::Key:
      return keyView_;
    case ApplicationMode::Directory:
    default:
      return directoryView_;
  }
}

bool Application::handleCommand(Command command) {
  // See DirectoryMode::clearEditError()'s own comment on why this
  // lives HERE (once per dispatched command) rather than inside the
  // mode-line provider itself, and why handleCommand() specifically
  // (not dispatchByte(), which most real keypresses funnel through
  // but which incidental hover/idle-loop redraws never call at all,
  // and which some callers/tests bypass by calling handleCommand()
  // directly) -- this is the one place both real keyboard dispatch
  // AND those direct callers agree passes through. Harmless outside
  // Directory mode or when nothing is currently showing. Executed
  // BEFORE the switch below, so StartEdit's own case (which may set
  // this again via executeEdit()/reportEditError()) still shows
  // its OWN message afterward, unaffected.
  directoryMode_.clearEditError();
  switch (command) {
    // setHoverIndex(nullopt) BEFORE each move, not after (2026-08-05
    // fix): a real user report -- after resting the mouse over a row
    // (setting hoverIndex_, see ContentView::setHoverIndex()'s own
    // comment) and then switching to keyboard navigation, "モードライン
    // 行数は変わり内容も正しいがハイライトが追随しない". moveUp()/
    // moveDown()/etc. below all redraw() internally, and redraw()'s own
    // highlight pick is `hoverIndex_.has_value() ? *hoverIndex_ :
    // cursorIndex_` -- cursorIndex_ WAS moving correctly the whole
    // time (hence the mode-line/content being right), but a stale
    // hoverIndex_ left over from the last mouse hover kept outranking
    // it in that redraw, so the highlighted row never visibly followed
    // along. Clearing it here, before the move itself runs its own
    // internal redraw(), means that redraw() already sees cursorIndex_
    // as the (correct, up to date) highlight source -- the same
    // "keyboard input always wins over stale mouse-driven UI state"
    // principle the user's own OWN diagnosis already applied to the
    // -g `dragging` bug.
    case Command::MoveUp:
      activeView().setHoverIndex(std::nullopt);
      activeView().moveUp();
      activeView().syncVisibleCursor();
      refreshDirectEntryModeLine();  // no-op outside RecentFile/RecentDirectory's own read-only browsing
      return true;
    case Command::MoveDown:
      activeView().setHoverIndex(std::nullopt);
      activeView().moveDown();
      activeView().syncVisibleCursor();
      refreshDirectEntryModeLine();
      return true;
    case Command::ScrollLineUp:
      activeView().setHoverIndex(std::nullopt);
      activeView().scrollLineUp();
      activeView().syncVisibleCursor();
      return true;
    case Command::ScrollLineDown:
      activeView().setHoverIndex(std::nullopt);
      activeView().scrollLineDown();
      activeView().syncVisibleCursor();
      return true;
    case Command::PageUp:
      activeView().setHoverIndex(std::nullopt);
      activeView().pageUp();
      activeView().syncVisibleCursor();
      return true;
    case Command::PageDown:
      activeView().setHoverIndex(std::nullopt);
      activeView().pageDown();
      activeView().syncVisibleCursor();
      return true;
    case Command::Enter:
      if (currentMode_ == ApplicationMode::Directory) return openSelectedEntry();
      if (currentMode_ == ApplicationMode::Cut) return openSelectedSegment();
      if (currentMode_ == ApplicationMode::Archive) return openSelectedArchiveEntry();
      if (currentMode_ == ApplicationMode::Du) return openSelectedDuEntry();
      if (currentMode_ == ApplicationMode::ListBrowser) return openSelectedListEntry();
      if (currentMode_ == ApplicationMode::Key) return executeSelectedKeyBinding();
      if (currentMode_ == ApplicationMode::Grep) return openSelectedGrepMatch();
      if (currentMode_ == ApplicationMode::Locate) return openSelectedLocateResult();
      // 2026-08-18: File mode has no OTHER Enter behavior (plain
      // pager) -- openSelectedBannerEntry() itself no-ops (returns
      // false) unless the file actually being viewed is the ^T
      // banner, so this is a no-op for ordinary file viewing.
      if (currentMode_ == ApplicationMode::File) return openSelectedBannerEntry();
      return false;
    case Command::Back:
      if (currentMode_ == ApplicationMode::Key) return returnFromKey();
      if (currentMode_ == ApplicationMode::ListBrowser) return returnFromListBrowser();
      if (currentMode_ == ApplicationMode::Du) return returnFromDu();
      if (currentMode_ == ApplicationMode::Shell) return returnFromShell();
      if (currentMode_ == ApplicationMode::ArchiveContent) return returnFromArchiveContent();
      if (currentMode_ == ApplicationMode::Archive) return returnFromArchive();
      if (currentMode_ == ApplicationMode::Locate) return returnFromLocate();
      if (currentMode_ == ApplicationMode::Man) return returnFromMan();
      if (currentMode_ == ApplicationMode::CutContent) return returnFromCutContent();
      if (currentMode_ == ApplicationMode::Cut) {
        if (cutMode_.canGoBack()) return cutMode_.goBack();
        return returnFromCut();
      }
      if (currentMode_ == ApplicationMode::Grep) return returnFromGrep();
      if (currentMode_ == ApplicationMode::File) {
        // 2026-08-14 fix: see fileReturnMode_'s own declaration
        // comment -- a file reached via LOCATE returns to LOCATE's
        // own results list first (matching the user's own explicit
        // spec: "^G連打で最終的にLOCATEを起動した(ls)へ戻るのは
        // かまわない" -- a SECOND ^G, now from Locate mode, already
        // correctly reaches locateReturnMode_ via returnFromLocate()'s
        // own existing, untouched logic).
        if (fileReturnMode_ == ApplicationMode::Locate) return returnFromFileToLocate();
        // 2026-08-18: a w.N entry opened via openSelectedBannerEntry()
        // (K-0/^T's own candidate-list selection) -- routes straight
        // back to the banner itself rather than returnToDirectory()'s
        // "follow the file's real parent directory" logic, which would
        // otherwise land on tempRootDirectory_ (see that function's own
        // 2026-08-18 comment on why the banner file itself is already
        // excluded from that logic -- a freshly reloaded w.N is a
        // DIFFERENT file from the banner and isn't covered by that
        // exclusion, so it needs this separate branch here instead).
        if (fileReturnMode_ == ApplicationMode::File) return executeShowTcbBanner();
        return returnToDirectory();
      }
      // 2026-08-11 fix: a real report -- after openSelectedGrepMatch()
      // lands back in Directory mode with a "FGREP: pattern" mode-line
      // annotation (see DirectoryMode::setSearchAnnotation()'s own
      // comment), pressing Back (^G) did nothing at all whenever
      // modeStack_ was empty (the common case: the search was run
      // directly from the top-level listing, never having descended
      // into a subdirectory, so there was no ModeStack frame to pop).
      // The annotation exists specifically to mark "you can get back
      // to this," so Back should honor it -- checked BEFORE the
      // ModeStack-based goBack() fallback, matching this same
      // ordering's existing precedent (File/Cut/etc. all get their own
      // dedicated check ahead of the generic directory case). Extended
      // (same day) to also cover CUT run directly from Directory mode
      // (returnFromCut() sets the SAME annotation, see its own
      // comment) -- lastDirectorySearchKind_ (set alongside whichever
      // of these two set the annotation) picks which results screen to
      // route back to, since the annotation text alone doesn't
      // structurally distinguish them.
      if (currentMode_ == ApplicationMode::Directory && directoryMode_.hasSearchAnnotation()) {
        return lastDirectorySearchKind_ == DirectorySearchKind::Cut ? returnToCutResults() : returnToGrepResults();
      }
      return directoryMode_.goBack();
    // ':'/';' -- literally Back+MoveDown+Enter / Back+MoveUp+Enter.
    // 2026-08-16: a real user report ("「;」「:」の機能が逆") showed the
    // 2026-08-03 "confirmed directly by the user" mapping had these two
    // backwards relative to kKeyTable's own M-0/M-1 entries (item 177,
    // sourced VERBATIM from usilicrc's own macro definitions); see
    // resolveCommand()'s own comment and Command::JumpNext's declaration
    // comment for the full account. Each step
    // re-enters handleCommand() so it sees currentMode_ as it stands
    // AFTER the previous step (e.g. Back may switch File->Directory
    // before MoveDown/Enter act on the now-current Directory view) --
    // the same as if the three underlying keys had actually been typed
    // one at a time. Intermediate steps' own success/failure is
    // intentionally not checked (matching a real 3-keystroke replay,
    // which doesn't stop partway through just because one keystroke
    // was a no-op); only the final step's result is returned.
    case Command::JumpNext:
      handleCommand(Command::Back);
      handleCommand(Command::MoveDown);
      return handleCommand(Command::Enter);
    case Command::JumpPrevious:
      handleCommand(Command::Back);
      handleCommand(Command::MoveUp);
      return handleCommand(Command::Enter);
    case Command::StartFgrep:
      return startGrepPrompt(GrepSyntax::Fixed);
    case Command::StartEgrep:
      return startGrepPrompt(GrepSyntax::Regex);
    case Command::StartCutFixed:
      return startCutPrompt(GrepSyntax::Fixed);
    case Command::StartCutRegex:
      return startCutPrompt(GrepSyntax::Regex);
    case Command::StartCutFileFixed:
      return startCutFilePrompt(GrepSyntax::Fixed);
    case Command::StartCutFileRegex:
      return startCutFilePrompt(GrepSyntax::Regex);
    case Command::StartMan:
      return startManPrompt();
    case Command::StartManList:
      return startManList();
    case Command::StartProgram:
      return startProgramPrompt();
    case Command::StartProgramList:
      return startProgramList();
    case Command::StartLocate:
      return startLocatePrompt();
    case Command::LocateUpdateDb:
      return runLocateUpdateDb();
    case Command::StartShell:
      return startShell();
    case Command::ToggleMark:
      return toggleMark();
    case Command::StartCopy:
      return startCopyPrompt();
    case Command::StartMove:
      return startMovePrompt();
    case Command::StartRemove:
      return startRemovePrompt();
    case Command::StartCopyInteractive:
      return startCopyPrompt(/*interactive=*/true);
    case Command::StartMoveInteractive:
      return startMovePrompt(/*interactive=*/true);
    case Command::StartRemoveInteractive:
      return startRemovePrompt(/*interactive=*/true);
    case Command::SaveBufferToFile:
      return saveBufferToFile();
    case Command::ShellChangeDirectory:
      return shellChangeDirectory();
    case Command::ShellRunLsL:
      return shellRunLsL();
    case Command::ShellInsertName:
      return shellInsertName();
    case Command::ShellInsertBufferContent:
      return shellInsertBufferContent();
    case Command::GoToParentDirectory:
      if (currentMode_ != ApplicationMode::Directory) return false;
      return directoryMode_.goToParent();
    case Command::GoToFileSelector:
      return goToFileSelector();
    case Command::RestartShell:
      return shellRestart();
    case Command::StartArchiveExtract:
      return startArchivePrompt(/*interactive=*/false);
    case Command::StartArchiveExtractInteractive:
      return startArchivePrompt(/*interactive=*/true);
    case Command::ShowErrorLog:
      return executeShowErrorLog();
    case Command::ShowShellLog:
      return executeShowShellLog();
    case Command::StartLog:
      return executeStartLog();
    case Command::ShowTcbBanner:
      return executeShowTcbBanner();
    case Command::CycleCharset:
      return cycleCharset();
    // 2026-08-17 fix (=cn): K-46 (^X^F, "Select file name") -- see
    // startListBrowser()'s own comment for why this no longer calls it
    // directly. startRegexPromptList() is generic enough already
    // (dispatches purely on the PromptKind value) despite its name.
    case Command::StartFileHistory:
      return startRegexPromptList(PromptKind::RecentFile);
    case Command::StartKey:
      return startKey();
    case Command::StartExit:
      return startExitPrompt();
    case Command::StartDu:
      return startDuPrompt();
    // 2026-08-17 fix (=cn): K-48 (^X^D, "Select directory name") --
    // same reasoning as StartFileHistory just above.
    case Command::StartDirectoryHistory:
      return startRegexPromptList(PromptKind::RecentDirectory);
    case Command::StartCommandHistory:
      return startListBrowser(RingCategory::ShellCommand);
    // Plain ^R from ordinary Directory/File browsing (2026-08-18
    // addition, a direct user request): legacy's own bare ^R (M_hist)
    // is dispatched from literally any mode, but was empirically found
    // (a real freshly-built legacy binary, see item 353's own
    // investigation) to open a functionally useless "HISTORY 0/0 END
    // No string" screen when pressed cold from plain "(ls)" browsing --
    // the user's own theory: ^R was FIRST implemented for COMMAND
    // (SHELL execution history) only, and never rewired once DIR/FILE/
    // FGREP/etc were added later as their own dedicated entry points
    // (^X^D/^X^F/^J^S/...). Rather than reproduce that dead-end
    // faithfully, this is a genuinely NEW directory-name/file-name
    // history shortcut: plain ^R from Directory mode opens the SAME
    // DIR picker ^X^D does (startRegexPromptList(RecentDirectory)),
    // and from File mode (excluding the ^T banner screen -- see
    // bannerFilePath()'s own comment; a real user file is regular
    // reading material, the banner is a synthetic index screen, not
    // something "file name history" makes sense for) opens the SAME
    // FILE picker ^X^F does. Deliberately scoped to ONLY these two
    // modes -- not "reachable from anywhere" the way legacy's own
    // (non-functional) dispatch-table entry technically was ("機能的には
    // DIR（FILE）とダブるけれども、どこからでもOKではない点が違う").
    // Every other mode (GREP/CUT/MAN/LOCATE/SHELL/...) falls through to
    // toggleListBrowserHistory()'s own existing behavior unchanged (a
    // real sorted<->chronological toggle inside an already-open
    // ListBrowser screen, a no-op everywhere else).
    case Command::ToggleHistoryView:
      if (currentMode_ == ApplicationMode::Directory) return startRegexPromptList(PromptKind::RecentDirectory);
      if (currentMode_ == ApplicationMode::File && fileMode_.currentFile() != bannerFilePath()) {
        return startRegexPromptList(PromptKind::RecentFile);
      }
      return toggleListBrowserHistory();
    case Command::StartEdit:
      return executeEdit();
    case Command::StartHelp:
      return startHelp();
    case Command::StartMacroRecord:
      return startMacroRecording();
    case Command::StopMacroRecord:
      return stopMacroRecording();
    case Command::PlayMacro:
      return playRecordedMacro();
    case Command::ToggleGuiMode:
      guiModeToggleRequested_ = true;
      return true;
    case Command::Redraw:
      activeView().setHoverIndex(std::nullopt);  // see the Move*/Page* cases' own comment
      activeView().redraw();
      activeView().syncVisibleCursor();
      return true;
    case Command::GoToTop:
      activeView().setHoverIndex(std::nullopt);
      activeView().goToTop();
      activeView().syncVisibleCursor();
      return true;
    case Command::GoToBottom:
      activeView().setHoverIndex(std::nullopt);
      activeView().goToBottom();
      activeView().syncVisibleCursor();
      return true;
    case Command::QuitPager:
      // usilicrc documents this as pager-only ("Exit tcb (pager)");
      // Directory mode has its own confirmed-exit path (StartExit,
      // ^X^C) and doesn't want a bare 'q' to bypass it.
      if (currentMode_ == ApplicationMode::Directory) return false;
      requestImmediateExit();
      return true;
    case Command::ToggleScrollMode: {
      bool enabled = !display_.smoothScrollEnabled();
      display_.setSmoothScrollEnabled(enabled);
      history_.setSmoothScrollEnabled(enabled);  // persisted across runs, see History's own field comment
      return true;
    }
    case Command::StartFileInput:
      return startPathInputPrompt(/*isDirectory=*/false);
    case Command::StartDirectoryInput:
      return startPathInputPrompt(/*isDirectory=*/true);
    case Command::ToggleLsMode: {
      if (currentMode_ != ApplicationMode::Directory) return false;
      bool enabled = !directoryMode_.filenameOnlyMode();
      directoryMode_.setFilenameOnlyMode(enabled);
      history_.setLsMode(enabled ? 1 : 0);  // persisted across runs, see History's own field comment
      return true;
    }
    case Command::MarkAll:
      if (currentMode_ == ApplicationMode::Archive) {
        archiveMode_.markAll();
        return true;
      }
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.markAll();
      return true;
    case Command::ClearAllMarks:
      if (currentMode_ == ApplicationMode::Archive) {
        archiveMode_.clearMarks();
        return true;
      }
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.clearMarks();
      return true;
    case Command::None:
      return false;
  }
  return false;
}

// Ported from mode.c's T_dir case reaching T_tar/T_lha/T_unzip/T_rpm/
// T_deb vs T_file: legacy dispatches on the selection's suffix before
// ever trying to view it as plain text. Here that's just "try
// ArchiveMode first, fall back to FileMode" -- see class comment for
// the one fidelity gap this simplification introduces (a real archive
// suffix whose listing command fails falls through to plain-text
// viewing instead of a specific error).
bool Application::openSelectedEntry() {
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry) return false;
  if (entry->isDirectory) return directoryMode_.openSelected();

  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;

  // EXEC (a suffix-mapped external viewer) takes priority over
  // archive detection -- see class comment on is_child()'s own
  // f.exec-checked-first precedence (though Configuration's unified
  // suffix table already makes the check order mostly moot in
  // practice). Hands off entirely and returns to Directory mode
  // unchanged, same shape as PROGRAM/EDIT.
  if (configuration_.isExec(path)) {
    if (auto command = configuration_.programForSuffix(path)) execMode_.run(*command, path);
    activeView().redraw();
    return true;
  }

  if (archiveMode_.loadArchive(path)) {
    archiveParentStack_.clear();  // a brand new top-level archive -- any earlier nested-descent chain no longer applies
    currentMode_ = ApplicationMode::Archive;
    // 2026-08-20 fix, a real report ("ARC:^X^F→「utf8.tar」が履歴に
    // 無い"): FileMode::loadFile() records every opened file into
    // RecentFile history (see its own pushRecent() call), but an
    // archive never reaches loadFile() at all -- this branch returns
    // BEFORE the `fileMode_.loadFile(path)` call below, so opening a
    // .tar/.zip/... never got recorded, making it permanently
    // unreachable from ^X^F regardless of how recently it was opened.
    // Only the TOP-LEVEL archive the user directly selected (this call
    // site) is recorded -- descending into a NESTED archive
    // (materializeArchiveTempFile()'s own loadArchive() call further
    // down) uses a throwaway mkdtemp() temp path with no lasting
    // meaning to record, and navigating back UP to an already-recorded
    // parent (goToParentArchive()'s own loadArchive() call) would just
    // duplicate an existing entry.
    history_.pushRecent(RingCategory::RecentFile, path);
    return true;
  }
  if (!fileMode_.loadFile(path)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // see its own declaration comment
  return true;
}

// Ported from mode.c's T_tar/etc Enter handling, PLUS new nested-
// archive support (not a port of anything -- legacy never had this,
// see descendToNestedArchive()'s own comment): if the selected entry
// is itself a recognized archive suffix, recurse into it instead of
// showing its raw bytes, the same suffix-dispatch openSelectedEntry()
// already does at the TOP level. Otherwise: extracts the selected
// entry's raw bytes and displays them as plain text lines (no
// PipeBuffer charset/overstrike processing -- ArchiveMode hands back
// raw bytes only, see class comment). False for a directory entry, a
// format that doesn't support extraction (RPM/.deb), or a failed
// extraction -- ArchiveMode::extractSelected() already distinguishes
// none of these from the others, so neither does this.
bool Application::openSelectedArchiveEntry() {
  if (const ArchiveEntry *entry = archiveMode_.selectedEntry(); entry && !entry->isDirectory) {
    ArchiveKind nestedKind = ArchiveKind::None;
    if (configuration_.programForSuffix(entry->name, &nestedKind) && nestedKind != ArchiveKind::None)
      return descendToNestedArchive(*entry);
  }

  auto content = archiveMode_.extractSelected();
  if (!content) return false;

  std::string text(content->begin(), content->end());
  std::istringstream stream(text);
  std::vector<ContentView::Line> lines;
  std::string line;
  while (std::getline(stream, line)) lines.push_back({line, std::nullopt, std::nullopt, {}});
  archiveContentView_.setLines(std::move(lines));
  archiveContentView_.setCursorIndex(0);
  archiveContentView_.redraw();
  currentMode_ = ApplicationMode::ArchiveContent;
  return true;
}

// See the class's own declaration comment on this feature.
bool Application::descendToNestedArchive(const ArchiveEntry &entry) {
  auto content = archiveMode_.extractSelected();
  if (!content) return false;

  std::string tempPath = materializeArchiveTempFile(allocateArcDirectory(), entry.name, *content);
  if (tempPath.empty()) return false;

  std::string parentPath = archiveMode_.currentArchive();
  archiveMode_.saveCursorPosition();
  if (!archiveMode_.loadArchive(tempPath)) return false;

  archiveParentStack_.push_back(parentPath);
  currentMode_ = ApplicationMode::Archive;
  return true;
}

std::string Application::materializeArchiveTempFile(const std::string &dir, const std::string &entryName,
                                                      const std::vector<unsigned char> &bytes) {
  // 2026-08-18: used to mkdtemp() a bare `/tmp/usilic-arcXXXXXX` itself
  // -- `dir` is now the caller's own already-allocated directory (an
  // arc.N slot for archive descent, or tempRootDirectory_ directly for
  // the banner's single fixed file -- see this function's own
  // declaration comment), so this just needs to ensure it exists.
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return "";

  // entry.name may itself contain '/' (a path within the archive) --
  // only the basename matters for programForSuffix()'s own suffix
  // dispatch, and keeping the temp file directly in `dir` (rather than
  // trying to recreate the archive's internal directory structure)
  // sidesteps needing to mkdir -p anything else here.
  std::string baseName = entryName;
  if (std::size_t slash = baseName.find_last_of('/'); slash != std::string::npos)
    baseName = baseName.substr(slash + 1);
  if (baseName.empty()) baseName = "entry";

  std::string path = dir + "/" + baseName;
  std::ofstream out(path, std::ios::binary);
  if (!out) return "";
  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!out) return "";
  return path;
}

// Ported from mode.c's T_du Enter handling (see class comment on why
// this, unlike every other prompt-driven result screen, drills
// straight into DirectoryMode/FileMode instead of stopping at a
// no-op like GREP/LOCATE do): DuMode::loadUsage() already invokes
// `du` with an absolute path, so `du`'s own output entries are
// already absolute -- no path-joining needed here, unlike Cut/Archive
// segments/entries.
bool Application::openSelectedDuEntry() {
  const DuEntry *entry = duMode_.selectedEntry();
  if (!entry) return false;
  if (entry->isDirectory) {
    if (!directoryMode_.enterDirectory(entry->path)) return false;
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  if (!fileMode_.loadFile(entry->path)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // see its own declaration comment
  return true;
}

// 2026-08-14: a direct user report ("ハイライト行で[return]が無反応")
// -- LocateMode's own class comment had explicitly deferred this
// ("Deliberately excluded (future work, not this slice): deciding
// what to do with a selected result"), and that "future work" moment
// arrived. Mirrors openSelectedDuEntry() just above exactly (DU's own
// results are ALSO arbitrary filesystem paths that can be either) --
// the one difference is DuEntry already carries its own isDirectory
// flag (parsed from `du`'s own output), while LocateMode::results()
// is just raw paths from `locate`, so this checks the filesystem
// directly instead. Matches DU's own precedent of NOT resetting the
// directory-navigation floor here (see DirectoryMode::
// resetNavigationToFloor()'s own comment -- that's specifically for
// the ^X^D history PICKER's "discard a stale prior chain" case, not
// every non-linear way of arriving at a directory).
bool Application::openSelectedLocateResult() {
  const std::string *path = locateMode_.selectedResult();
  if (!path) return false;
  std::error_code ec;
  if (std::filesystem::is_directory(*path, ec) && !ec) {
    if (!directoryMode_.enterDirectory(*path)) return false;
    currentMode_ = ApplicationMode::Directory;
    // 2026-08-14 fix: a real report ("モードラインにバイト数...および
    // 戻り値「LOCATE: .../HKSCS.txt」が無い（旧版と違うので注意）") --
    // landing here via a LOCATE match previously left no trace of the
    // search that led here, unlike GREP's own identical convention
    // (setSearchAnnotation() calls right above/below this function).
    // Query (not the matched path) here, mirroring EGREP/CUT's own
    // "MODE: pattern" convention exactly -- the destination directory
    // itself is already the whole of DirectoryMode's own name field,
    // so there's nothing further to add beyond which search found it.
    directoryMode_.setSearchAnnotation("LOCATE: " + locateMode_.query());
    // enterDirectory() already redrew ONCE internally, before the
    // annotation above was set -- unlike openSelectedGrepMatch() (which
    // never calls enterDirectory() at all, since a GREP match is always
    // inside the ALREADY-listed current directory), LOCATE can jump to
    // an arbitrary not-yet-listed directory and has to call it. Without
    // this second redraw, the screen keeps showing that first, stale,
    // annotation-less mode line until the next unrelated redraw
    // happens to occur -- caught by a live PTY run (a real `locate`
    // search + Enter), not by the unit tests above, which call
    // modeLineText() directly (recomputes fresh on demand) rather than
    // checking what actually reached the RecordingDisplay's own screen
    // buffer.
    directoryView_.redraw();
    return true;
  }
  if (!fileMode_.loadFile(*path)) return false;
  currentMode_ = ApplicationMode::File;
  // 2026-08-14 fix (two direct user corrections against the previous
  // version here): (1) "^Gで(ls)へ戻るのはおかしい、LOCATEの検索結果
  // 一覧へ戻るわけだし" -- a file reached via LOCATE could be anywhere
  // on the filesystem, unrelated to whatever directory was active
  // before the search, so ^G should return to LOCATE's own results
  // list first (see fileReturnMode_'s own declaration comment), not
  // straight to some unrelated directory. (2) "モード名はLOCATE、
  // 戻り値も検索一覧同様「/Applications/Adobe Acrob...source/.../
  // HKSCS.txt」形式で" -- setSearchOrigin()'s label ("LOCATE", not the
  // bare filename) and detail (the full path, not prefixed with
  // "LOCATE: " -- see its own comment for why that duplication no
  // longer makes sense once the label itself already says LOCATE).
  fileMode_.setSearchOrigin("LOCATE", *path);
  fileReturnMode_ = ApplicationMode::Locate;
  fileView_.redraw();  // see the Directory branch's identical comment just above
  return true;
}

// K-18 (^J^K, "Go to file-selector", per usilicrc's own line -- this
// port had never implemented it before now). A direct user request
// ("検索ファイル所在地へ降りるためのキーアサインも実はあるので反映
// してほしい（^J^K）") made specifically in the context of a file
// reached via LOCATE, but the binding itself is a general File-mode
// feature, not LOCATE-specific -- jumps straight to a real `ls`
// listing of the CURRENT file's own containing directory. Legacy's own
// "file-selector" naming (disp.c) is tied to deeper K_z-sentinel
// bookkeeping (menu.c) this port doesn't have a legacy binary handy to
// verify the exact cursor-positioning behavior of, so deliberately
// scoped to just the navigation itself -- matching the user's own
// explicit spec: "^J^Kなら「.../Adobe/」の(ls)になる（LOCATEへは戻ら
// ない）", a plain, ONE-WAY jump (LOCATE's own results list is never
// touched, so ^G from the new directory does NOT return there).
// Untouched by fileReturnMode_, same as openSelectedLocateResult()'s
// own Directory branch (see its own comment on NOT calling
// resetNavigationToFloor()) -- this function DOES call it, though (see
// its own 2026-08-15 addition just below), for a different reason:
// ^X^D/^R's own RecentDirectory branch already establishes this exact
// "toggle back and forth with wherever you were" relationship for
// every OTHER way of landing in a new directory, and a direct user
// report ("(ls)での^Gは一つ前と行き来" -- confirmed missing here
// specifically, screenshots =bd/=bf/=bg) found this was the one
// directory-jumping entry point that hadn't been given the same
// treatment.
bool Application::goToFileSelector() {
  // 2026-08-16 fix: usilicrc's own K-? section (item 177/CLAUDE.md)
  // groups K-18 alongside plainly-universal bindings (K-2 "Return to
  // previous mode", K-21 "Go to previous regular file", K-22 "Read
  // content of file to buffer") -- a real user report ("CUTでは？と
  // 思い試そうとしたら^J^K不能だった...本来どこでも使えるキーアサイン
  // なので") confirmed this was meant to work from CUT too, not just
  // File. CutContent/Cut both operate on a file's content the same way
  // File itself does, so they get the same treatment here. Reads
  // fileMode_.currentFile() regardless of mode -- correct for the
  // buffer-variant CUT (^Jc from File mode, which never touches
  // FileMode's own state, so it's still holding the source file) --
  // NOT extended to the Directory-variant CUT (K-35/36, whose own
  // cutFileSourcePath_ is deliberately cleared the moment executeCut()
  // consumes it, so nothing retains that path once inside Cut mode;
  // out of scope for this fix, matching the user's own tested
  // scenario which was the buffer variant reached via LOCATE->File).
  // 2026-08-16(続き): also usable straight from ^X^F's own RecentFile
  // candidate list (ApplicationMode::ListBrowser) without first pressing
  // Enter to open the highlighted candidate -- a real report
  // ("同画面で^J^Kが利かない=bo", =bo being that exact candidate-list
  // screen). Reads the highlighted candidate string itself (items()[
  // cursorIndex]) rather than fileMode_.currentFile(), since nothing has
  // been opened yet at this point.
  // 2026-08-16(続き): also usable straight from LOCATE's own results
  // list (ApplicationMode::Locate) -- a real report ("新^J^K: どこでも
  // 使えない。特にLOCATE一覧=buでの^J^Kを試したい"), same shape as the
  // ListBrowser/RecentFile case just above: reads the highlighted
  // result's own path, nothing needs to have been opened first.
  std::string sourceFile;
  if (currentMode_ == ApplicationMode::ListBrowser) {
    if (listBrowserCategory_ != RingCategory::RecentFile) return false;
    const std::string *selected = listBrowserMode_.selectedItem();
    if (!selected) return false;
    sourceFile = *selected;
  } else if (currentMode_ == ApplicationMode::Locate) {
    const std::string *selected = locateMode_.selectedResult();
    if (!selected) return false;
    sourceFile = *selected;
  } else if (currentMode_ == ApplicationMode::File || currentMode_ == ApplicationMode::Cut ||
             currentMode_ == ApplicationMode::CutContent) {
    sourceFile = fileMode_.currentFile();
  } else {
    return false;
  }
  std::filesystem::path path(sourceFile);
  std::string parent = path.parent_path().string();
  if (parent.empty()) return false;
  // Captured BEFORE enterDirectory() overwrites currentDirectory_ --
  // see openSelectedListEntry()'s own identical RecentDirectory-branch
  // comment. Genuinely empty (not just unset) whenever the whole
  // session started directly on a file (`usilic somefile.txt`, no
  // Directory session ever active) -- resetNavigationToFloor() isn't
  // called in that case, matching every other floor-establishing call
  // site's own implicit assumption that there IS a real "previous
  // directory" to toggle back to.
  std::string previousDirectory = directoryMode_.currentDirectory();
  if (!directoryMode_.enterDirectory(parent)) return false;
  if (!previousDirectory.empty()) directoryMode_.resetNavigationToFloor(previousDirectory);
  currentMode_ = ApplicationMode::Directory;
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

// Ported from m_list()'s dispatch (see class comment for the 5 other
// categories not wired here): RecentDirectory (K-48, ^X^D/^R) drills
// into DirectoryMode, RecentFile (K-46, ^X^F) into FileMode; every
// other category (just ShellCommand in this slice) is a no-op, since
// deciding what "open" means for a shell command (pre-fill Shell's
// command line? require Shell already started?) is deliberately left
// open rather than guessed at.
bool Application::openSelectedListEntry() {
  const std::string *item = listBrowserMode_.selectedItem();
  if (!item) return false;
  if (promptHistoryRecallKind_) {
    // Restore the ORIGINAL underlying mode first (prompts overlay
    // whatever was active when they started, and the corresponding
    // execute*() functions -- see e.g. executeGrep()'s own
    // grepReturnMode_ read -- assume that's still current, exactly as
    // it is for a live-typed submitPrompt() call), THEN dispatch the
    // picked text through the exact same path a live Enter would.
    PromptKind kind = *promptHistoryRecallKind_;
    std::string text = *item;
    promptHistoryRecallKind_ = std::nullopt;
    // See pendingSearchFromDirectEntryList_'s own comment.
    pendingSearchFromDirectEntryList_ = promptHistoryRecallIsDirectEntry_;
    promptHistoryRecallIsDirectEntry_ = false;
    currentMode_ = promptHistoryRecallReturnMode_;
    return executePromptSubmission(kind, text);
  }
  if (shellHistoryRecallActive_) {
    // Insert only -- does NOT submitLine(). See startShellHistoryRecall()'s
    // own comment for why this deliberately differs from MAN/LOCATE/
    // FGREP's auto-execute-on-Enter.
    shellHistoryRecallActive_ = false;
    std::string text = *item;
    currentMode_ = ApplicationMode::Shell;
    shell_.commandLine().setText(text);
    shell_.scrollback().redraw();
    shell_.redrawPromptRow();
    return true;
  }
  if (listBrowserCategory_ == RingCategory::RecentDirectory) {
    // 2026-08-13: resetNavigationToFloor() -- see its own declaration
    // comment (directory_mode.hpp) -- captured BEFORE enterDirectory()
    // overwrites currentDirectory_, so it's genuinely "where you were
    // before picking this," not the newly-entered directory itself.
    std::string previousDirectory = directoryMode_.currentDirectory();
    if (!directoryMode_.enterDirectory(*item)) return false;
    directoryMode_.resetNavigationToFloor(previousDirectory);
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  if (listBrowserCategory_ == RingCategory::RecentFile) {
    if (!fileMode_.loadFile(*item)) return false;
    currentMode_ = ApplicationMode::File;
    fileReturnMode_ = ApplicationMode::Directory;  // see its own declaration comment
    return true;
  }
  return false;
}

// Ported from M_mark (^Space): toggles the mark at the cursor.
// Directory-only, matching every other CP/MV/RM-related action added
// in this slice.
// K-57/K-58's own "mark specific entries to extract" (HELP.jp 2.11)
// reuses this same ^Space binding within Archive mode -- matching
// this port's established simplification of legacy's real per-mode
// marking mechanism into one shared ^Space/ToggleMark command (see
// DirectoryMode's own class comment on that choice).
bool Application::toggleMark() {
  if (currentMode_ == ApplicationMode::Archive) {
    if (!archiveMode_.selectedEntry()) return false;
    archiveMode_.toggleMark(archiveView_.cursorIndex());
    return true;
  }
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (!directoryMode_.selectedEntry()) return false;
  directoryMode_.toggleMark(directoryView_.cursorIndex());
  return true;
}

std::vector<std::string> Application::collectTargetPaths() const {
  std::vector<std::string> paths;
  std::string dir = directoryMode_.currentDirectory();
  for (const auto &entry : directoryMode_.targetEntries()) {
    std::string path = dir;
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry.name;
    paths.push_back(std::move(path));
  }
  return paths;
}

bool Application::openSelectedSegment() {
  const CutSegment *segment = cutMode_.selectedSegment();
  if (!segment) return false;

  std::vector<StyledLine> content = cutMode_.segmentContent(*segment);
  std::vector<ContentView::Line> lines;
  lines.reserve(content.size());
  // l.attributes (2026-08-05): this is the actual screen content HELP.jp/
  // CUT display -- see ContentView::Line::attributes' own comment for why
  // dropping this here specifically hid HELP.jp's own bold section
  // headings even after FileMode/CutMode started carrying it through.
  for (auto &l : content) lines.push_back({l.text, std::nullopt, std::nullopt, l.attributes});
  cutContentView_.setLines(std::move(lines));
  cutContentView_.setCursorIndex(0);
  cutContentView_.redraw();
  currentMode_ = ApplicationMode::CutContent;
  return true;
}

bool Application::returnToDirectory() {
  fileMode_.saveCursorPosition();
  // 2026-08-16 fix: this used to just flip currentMode_ and redraw
  // directoryView_ AS-IS, trusting directoryMode_'s own currentDirectory_
  // to already be the file's containing directory -- true for a normal
  // Directory-descent-then-Enter session, but NOT for a file reached via
  // ^X^F/RecentFile (openSelectedListEntry()'s own RecentFile branch
  // never calls directoryMode_.enterDirectory() at all, so whatever
  // directory happened to be showing BEFORE ^X^F stays showing after ^G
  // too). A real user report + own diagnosis nailed this exactly:
  // "=bo→[return]=bq→^G...結果は起動直後の（ls）なので「それ/
  // BDF_Driver...」を表示しようとしてんじゃないの？" -- confirmed
  // against legacy's own real behavior (=br: lands on the file's actual
  // Libraries/ directory), and goToFileSelector()'s own identical
  // enterDirectory()+resetNavigationToFloor() pairing already established
  // this exact fix for the ^J^K case; just missing here for plain ^G.
  // A no-op for the normal-descent case (parent already matches
  // currentDirectory_), so this doesn't touch existing cursor-position
  // behavior there.
  // 2026-08-18 fix: skip the "follow the file's real parent directory"
  // fix just below when the currently-loaded file IS the ^T banner's
  // own synthetic file (bannerFilePath(), see executeShowTcbBanner()'s
  // own comment) -- that fix is correct for ^X^F/RecentFile (a real
  // file the user deliberately picked), wrong for a throwaway status
  // screen whose own directory is essentially never where the user
  // actually wants to end up (this is exactly WHY leaving
  // directoryMode_ untouched here matters: it's what makes ^G from the
  // banner land back wherever ^T was actually invoked FROM, not
  // wherever the banner file itself happens to live -- a real report,
  // "戻り値も...^Tを起動したモードのはず", confirmed this was already
  // the intended behavior once bannerFilePath() stopped pointing at
  // tempRootDirectory_, see that function's own comment). A direct
  // path comparison rather than a separate stateful flag:
  // self-correcting the moment ANY other file gets loaded (of which
  // there are ~10 call sites), rather than needing every one of them
  // to remember to clear a flag this one alone sets.
  if (fileMode_.currentFile() != bannerFilePath()) {
    std::filesystem::path filePath(fileMode_.currentFile());
    std::string parent = filePath.parent_path().string();
    if (!parent.empty() && parent.back() != '/') parent += '/';
    if (!parent.empty() && parent != directoryMode_.currentDirectory()) {
      std::string previousDirectory = directoryMode_.currentDirectory();
      if (directoryMode_.enterDirectory(parent) && !previousDirectory.empty())
        directoryMode_.resetNavigationToFloor(previousDirectory);
    }
  }
  currentMode_ = ApplicationMode::Directory;
  // File mode's own view now sets a real visible cursor (see
  // ContentView::syncVisibleCursor()'s own comment) -- without this,
  // it would keep floating over the directory listing that replaces
  // it, since directoryView_.redraw() deliberately never touches
  // Display's visible-cursor state itself (same reasoning as
  // Application::returnFromShell()'s own hideVisibleCursor() call).
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

// 2026-08-11 fix: Command::Enter's own switch never had a Grep case at
// all (a real report: pressing Enter on an FGREP/EGREP result did
// nothing, "反応無し"). Ported from mode.c's T_grep case: jump back to
// wherever the search was run FROM (grepReturnMode_, same target
// returnFromGrep()'s ^G/Back path already uses), landing the cursor on
// the SOURCE line the selected match came from (GrepMatch::
// sourceLineIndex, parsed from fgrep/egrep's own `-n` line-number
// output in GrepMode::search()) rather than leaving it wherever it was
// before the search started -- confirmed against a real CUI screenshot
// showing the matched entry highlighted in the full listing afterward.
// Also records the "FGREP: pattern" mode-line annotation (see
// DirectoryMode::setSearchAnnotation()'s own comment) for the
// Directory case specifically, matching legacy's own lp->prev
// behavior.
bool Application::openSelectedGrepMatch() {
  const GrepMatch *match = grepMode_.selectedMatch();
  if (!match) return false;
  currentMode_ = grepReturnMode_;
  ContentView &target = activeView();
  target.setCursorIndex(match->sourceLineIndex);
  if (grepReturnMode_ == ApplicationMode::Directory) {
    std::string label = (grepSyntax_ == GrepSyntax::Fixed) ? "FGREP" : "EGREP";
    directoryMode_.setSearchAnnotation(label + ": " + grepMode_.pattern());
    lastDirectorySearchKind_ = DirectorySearchKind::Grep;
  }
  display_.hideVisibleCursor();
  target.redraw();
  return true;
}

bool Application::returnFromGrep() {
  // 2026-08-11 fix: a direct user clarification -- "検索への入口が
  // CUTなら出るときもCUTから出るはず" (if the search's own entrance
  // was the candidate list, Back should exit through it too). See
  // grepReturnsToCandidateList_'s own comment (pendingSearchFromDirectEntryList_):
  // re-enters the candidate list instead of jumping straight to
  // grepReturnMode_, one level at a time, matching a proper LIFO
  // stack. currentMode_ is set to grepReturnMode_ FIRST so
  // startRegexPromptList()'s own startPromptHistoryRecall() captures
  // the right promptHistoryRecallReturnMode_ (it reads currentMode_
  // at call time), even though this function never actually SHOWS
  // grepReturnMode_'s own view in this branch.
  if (grepReturnsToCandidateList_) {
    grepReturnsToCandidateList_ = false;
    currentMode_ = grepReturnMode_;
    return startRegexPromptList(PromptKind::GrepPattern);
  }
  currentMode_ = grepReturnMode_;
  // ^G from the Directory listing (with a "FGREP: pattern" annotation
  // already showing, from a prior openSelectedGrepMatch()) goes back
  // to Grep mode (returnToGrepResults(), just above), and a SECOND ^G
  // from there comes back through HERE -- but the mode line should
  // read the plain "(ls)" a fresh Back-out-of-grep shows, not the
  // stale "FGREP: .json" from before. openSelectedGrepMatch() is the
  // only caller that means "I picked THIS match" -- this function
  // means "cancel, I'm backing out without picking anything (new),"
  // so whatever annotation was left over from a previous selection no
  // longer describes what just happened and needs to be cleared, not
  // carried forward a second time.
  if (grepReturnMode_ == ApplicationMode::Directory) directoryMode_.setSearchAnnotation("");
  activeView().redraw();
  return true;
}

// 2026-08-11 fix: the symmetric counterpart to openSelectedGrepMatch()
// -- Back (^G) from Directory mode while its "FGREP: pattern"
// annotation is showing re-enters Grep mode over the SAME still-intact
// results (grepView_'s lines/matches_ are never touched by
// openSelectedGrepMatch(), only the Directory/File view it lands on),
// so this doesn't need to re-run the search at all.
bool Application::returnToGrepResults() {
  currentMode_ = ApplicationMode::Grep;
  grepView_.redraw();
  return true;
}

bool Application::returnFromCutContent() {
  currentMode_ = ApplicationMode::Cut;
  cutView_.redraw();
  return true;
}

// 2026-08-11: unlike GREP (where Enter on a match is the ONLY path
// back to Directory with an annotation, and plain Back is a separate
// "cancel" path that clears it -- see openSelectedGrepMatch()/
// returnFromGrep()'s own comments), CUT run directly from Directory
// mode has no such split: Back is the ONLY way out of Cut mode back
// to Directory (there's no "select a segment and jump to Directory"
// action the way GREP's Enter works), so this single function both
// leaves Cut mode AND records the "CUT: pattern" annotation. Always
// freshly written with the CURRENT pattern on every call, so there's
// no stale-annotation risk the way returnFromGrep() had to guard
// against.
bool Application::returnFromCut() {
  // 2026-08-11 fix: same LIFO-stack reasoning as returnFromGrep()'s
  // own grepReturnsToCandidateList_ check -- see its comment. Checked
  // before the "Nothing between the candidate list and Cut mode"
  // comment above's own always-fresh annotation write, since re-
  // entering the candidate list means we haven't actually left the
  // search yet.
  if (cutReturnsToCandidateList_) {
    cutReturnsToCandidateList_ = false;
    currentMode_ = cutReturnMode_;
    return startRegexPromptList(PromptKind::CutPattern);
  }
  currentMode_ = cutReturnMode_;
  if (cutReturnMode_ == ApplicationMode::Directory) {
    directoryMode_.setSearchAnnotation("CUT: " + cutMode_.pattern());
    lastDirectorySearchKind_ = DirectorySearchKind::Cut;
  }
  activeView().redraw();
  return true;
}

// The Cut/CUT-from-Directory counterpart to returnToGrepResults() --
// see Command::Back's own comment on why lastDirectorySearchKind_
// picks between the two. cutView_'s own lines/segments are untouched
// by returning to Directory, so (like the Grep case) this doesn't
// need to re-run the split.
bool Application::returnToCutResults() {
  currentMode_ = ApplicationMode::Cut;
  cutView_.redraw();
  return true;
}

bool Application::returnFromMan() {
  manMode_.saveCursorPosition();
  currentMode_ = manReturnMode_;
  activeView().redraw();
  return true;
}

bool Application::returnFromLocate() {
  currentMode_ = locateReturnMode_;
  activeView().redraw();
  return true;
}

// See fileReturnMode_'s own declaration comment. Mirrors
// returnToDirectory() exactly (saveCursorPosition() + hideVisibleCursor()
// + redraw the destination view), just landing on locateView_ instead
// of directoryView_.
bool Application::returnFromFileToLocate() {
  fileMode_.saveCursorPosition();
  currentMode_ = ApplicationMode::Locate;
  display_.hideVisibleCursor();
  locateView_.redraw();
  return true;
}

bool Application::returnFromArchiveContent() {
  currentMode_ = ApplicationMode::Archive;
  archiveView_.redraw();
  return true;
}

// Climbs back out of a nested-archive descent one level at a time
// (see descendToNestedArchive()'s own comment) before finally
// returning to Directory once archiveParentStack_ is empty again --
// the original, still-unchanged behavior for the non-nested case.
bool Application::returnFromArchive() {
  archiveMode_.saveCursorPosition();
  if (!archiveParentStack_.empty()) {
    std::string parentPath = archiveParentStack_.back();
    archiveParentStack_.pop_back();
    if (archiveMode_.loadArchive(parentPath)) return true;
    // The remembered path should always still be valid (a real file
    // or our own still-on-disk temp file) -- but if something removed
    // it out from under us, fail safe by falling through to Directory
    // below rather than leaving the screen showing stale content.
  }
  currentMode_ = ApplicationMode::Directory;
  directoryView_.redraw();
  return true;
}

bool Application::returnFromDu() {
  duMode_.saveCursorPosition();
  currentMode_ = ApplicationMode::Directory;
  directoryView_.redraw();
  return true;
}

bool Application::returnFromShell() {
  currentMode_ = shellReturnMode_;
  // The mode being returned to (typically a plain directory listing)
  // relies on ContentView's own row-highlight, not a visible cursor,
  // and never calls setVisibleCursor() itself -- without this, -g
  // would keep showing SHELL's last cursor position (a history
  // browsing line, or the live prompt caret) floating over the new
  // screen's content, since ContentView::redraw() deliberately never
  // touches it (see Display::setVisibleCursor()'s own comment on why
  // it must persist across incidental redraws to begin with).
  display_.hideVisibleCursor();
  activeView().redraw();
  return true;
}

// Ported from do_shell()'s own "if (kill(spid, 0)) { pv = NULL; m =
// -M_bl; break; }" -- legacy's input_shell() checks whether the
// relay/shell process is still alive on EVERY loop iteration, and the
// moment it isn't (typically because the user just typed `exit`),
// automatically returns -M_bl (^G's own code) and unwinds straight
// back to whichever mode SHELL was entered from -- no separate ^G
// needed. Called whenever Shell::onPtyReadable() reports EOF (see its
// own comment). shellStarted_ resets to false so the NEXT entry into
// Shell mode spawns a genuinely fresh child via ensureShellActive()'s
// own shellStarted_ guard, rather than trying to reuse a dead one --
// Shell::start()'s own resetSessionState() clears scrollback_/
// pipeBuffer_/etc at that point, matching legacy's fresh PP chain on
// restart. A real user request (2026-08-02) confirmed this -- see
// resetSessionState()'s own comment for the full exchange.
void Application::handleShellChildExited() {
  shell_.terminate();
  shellStarted_ = false;
  if (currentMode_ == ApplicationMode::Shell) returnFromShell();
}

bool Application::returnFromListBrowser() {
  if (promptHistoryRecallKind_) {
    PromptKind kind = *promptHistoryRecallKind_;
    // EGREP's own candidate list (2026-08-12 fix, direct user
    // instruction, scoped to GrepPattern only -- CUT's regex variant
    // keeps the old behavior below "for now", per that same
    // instruction: "まだCUTの試用まではしていないので不具合があれば
    // 対処すればいい"): ^G here must NOT reopen a blank editable
    // prompt. Per docs/HISTORY.md:1538 -- the entire reason ^J^S opens
    // DIRECTLY into the candidate list instead of a blank prompt in
    // the first place (startRegexPromptList()'s own history) is that
    // EGREP has no FGREP-vs-EGREP-style naming cue the way CUT's
    // ^Jc/^J^C do share one name with no visual distinction otherwise
    // -- so the list itself IS the "this is regex mode" signal, shown
    // deliberately to prevent misuse. Silently falling through to a
    // blank prompt on ^G (this function's own pre-existing behavior,
    // inherited wholesale from the unrelated real-^R-recall case this
    // function ALSO handles, see the general branch below) undermines
    // that exact signal by letting a user slip back into typing
    // without ever seeing the list again. Only applies when
    // promptHistoryRecallIsDirectEntry_ (i.e. reached via ^J^S itself,
    // or item 275's own "return to candidate list" LIFO mechanism) --
    // a REAL ^R recall for EGREP (mid-typing, directEntry==false)
    // keeps resuming whatever was already typed, unaffected, since
    // that's a completely different scenario (recall while editing,
    // not "the list IS the entry point").
    // RecentFile/RecentDirectory (2026-08-18 fix, a direct user report +
    // screenshots =cx/=cy): these two are ALWAYS directEntry (see
    // startPromptHistoryRecall()'s own comment -- there's no separate
    // "real ^R mid-typing recall" entry point for either), and unlike
    // GrepPattern/CutPattern they're pure navigation with no "typed
    // pattern to resume editing" concept at all, so reopening a blank
    // activatePrompt() editor on ^G makes even less sense here than it
    // did for GrepPattern -- it left the ^X^D/^X^F prompt's own "DIR"/
    // "FILE" mode-line label and empty editable field drawn OVER the
    // real DirectoryMode/FileMode content underneath (activeView().
    // redraw() had already painted the correct screen, then
    // activatePrompt() drew its own row 24 right on top of it), instead
    // of the plain "(ls)"-showing screen the excursion actually started
    // from ("(ls)から^X^Dしてるんで(ls)へ戻らないと"). Folded into the
    // same early-return branch as GrepPattern's own identical fix.
    if ((kind == PromptKind::GrepPattern || kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory) &&
        promptHistoryRecallIsDirectEntry_) {
      // RecentFile/RecentDirectory + listBrowserChronological_
      // (2026-08-18, a direct user correction backed by a real,
      // freshly-built legacy binary check -- "^X^D→^R→^G(DIR一覧から
      // 来たんだからそこへ戻るのも当たり前)...現状そうなっていない
      // （^GでDIR一覧、じゃなくて(ls)へ戻る）"): ^R's chronological
      // "HISTORY" toggle is its own NESTED screen stacked on top of the
      // ordinary sorted "DIR"/"FILE" list, not a side note on the same
      // screen -- confirmed by running `^X^D`→`^R`→`^G` against
      // tcb/src's own legacy binary (built fresh in this same
      // investigation): the first ^G lands back on "DIR 1/1 END ...",
      // the SECOND ^G is what actually reaches "(ls)". The early-return
      // branch below (unconditionally jumping straight to
      // promptHistoryRecallReturnMode_) skipped that intermediate step
      // entirely. Popping the toggle back to sorted first -- via the
      // exact same toggleListBrowserHistory() a real ^R press uses --
      // and returning early (staying in ListBrowser, NOT touching
      // promptHistoryRecallKind_/currentMode_ at all) reproduces the
      // legacy two-^G shape; a second ^G then falls through to this
      // same branch again with listBrowserChronological_ already false,
      // taking the normal exit path below.
      if ((kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory) && listBrowserChronological_) {
        return toggleListBrowserHistory();
      }
      promptHistoryRecallKind_ = std::nullopt;
      promptHistoryRecallIsDirectEntry_ = false;
      promptHistoryRecallText_.clear();
      currentMode_ = promptHistoryRecallReturnMode_;
      // 2026-08-12 fix, a direct user correction: "戻り値が(ls)なら
      // ちゃんとそこ（(ls)でEGREPしたので当然(ls)のはず）へ戻って
      // ほしい" -- landing back on promptHistoryRecallReturnMode_
      // alone isn't enough when this candidate list was reached via
      // item 275's own LIFO return from Grep results: THAT return
      // path leaves currentMode_ as Directory but does NOT clear
      // directoryMode_'s own stale "EGREP: pattern" annotation (only
      // openSelectedGrepMatch() sets it, nothing along that path
      // clears it), so landing here still showed the annotated screen
      // (indistinguishable from having just picked a match again)
      // instead of the TRUE plain "(ls)" the whole excursion actually
      // started from. Mirrors returnFromGrep()'s own identical
      // clearing for the exact same reason -- see its own comment.
      if (promptHistoryRecallReturnMode_ == ApplicationMode::Directory) directoryMode_.setSearchAnnotation("");
      activeView().redraw();
      return true;
    }
    std::string text = promptHistoryRecallText_;
    promptHistoryRecallKind_ = std::nullopt;
    promptHistoryRecallIsDirectEntry_ = false;
    promptHistoryRecallText_.clear();
    currentMode_ = promptHistoryRecallReturnMode_;
    activeView().redraw();
    activatePrompt(kind, text);  // resumes with whatever had already been typed before ^R
    return true;
  }
  if (shellHistoryRecallActive_) {
    shellHistoryRecallActive_ = false;
    currentMode_ = ApplicationMode::Shell;
    shell_.commandLine().setText(shellHistoryRecallText_);  // restores whatever had already been typed before ^R
    shellHistoryRecallText_.clear();
    shell_.scrollback().redraw();
    shell_.redrawPromptRow();
    return true;
  }
  currentMode_ = listBrowserReturnMode_;
  activeView().redraw();
  return true;
}

// Ported from the M_shell (^[s) entry point: starts the pty-backed
// child on first use only (shellStarted_), and just switches the
// active screen on every subsequent entry -- the child keeps running
// in the background across Back/^G in between (see class comment).
// Refuses re-entry from within Shell mode itself (nothing to do).
bool Application::startShell() {
  if (currentMode_ == ApplicationMode::Shell) return false;
  return ensureShellActive();
}

bool Application::ensureShellActive() {
  if (currentMode_ == ApplicationMode::Shell) return true;
  // 2026-08-12 fix: a real report -- start usilic, ^J^S (EGREP
  // candidate list), ^J^W (now reachable from there thanks to today's
  // earlier feedRegexListByte()/feedPromptByte() leader-dispatch fix),
  // type "|sort" and run it, then ^R at the live SHELL prompt -- showed
  // the ListBrowser's own mode line reading "EGREP" instead of
  // "PROGRAM", and selecting an entry ran an EGREP search using the
  // shell command text as the pattern ("cat .../w.usilic|sort") instead
  // of inserting it into the command line. Root cause: promptHistoryRecallKind_
  // (set by the ORIGINAL ^J^S's startPromptHistoryRecall(), meant to be
  // consumed exactly once -- either by feedRegexListByte()'s own "start
  // editing" transition or by openSelectedListEntry()'s submission) was
  // never cleared by shellInsertBufferContent() jumping straight into
  // Shell mode instead of going through either of those normal
  // consumption paths. It stayed set (still == GrepPattern) all the way
  // through running the shell command, so when startShellHistoryRecall()
  // later opened its OWN, unrelated ListBrowser session (for
  // RingCategory::ShellCommand), dispatchByte()'s top-level check
  // (`currentMode_==ListBrowser && promptHistoryRecallKind_ && (...==
  // GrepPattern || ...==CutPattern)`) still matched on the STALE flag
  // and routed every byte through feedRegexListByte() -- the EGREP/CUT
  // candidate-list combobox logic -- instead of ordinary ListBrowser
  // navigation, hence both symptoms at once (wrong mode-line label, and
  // Enter submitting the selection as a grep pattern via
  // feedRegexListByte()'s own edit-then-submit path).
  //
  // Cleared here (not inside shellInsertBufferContent() specifically)
  // because ensureShellActive() is the one common choke point EVERY
  // Shell-entering command funnels through (shellInsertBufferContent(),
  // shellChangeDirectory(), shellRunLsL(), shellInsertName(), plain
  // startShell()) -- any of them jumping to Shell while a prompt-recall
  // ListBrowser session is still logically "in progress" (abandoned via
  // a leader command rather than its own ^G/Enter/edit-start
  // consumption path) has the identical staleness risk, not just ^J^W.
  // A no-op in the ordinary case (no recall was in progress, this is
  // already std::nullopt) -- so safe to run unconditionally.
  promptHistoryRecallKind_ = std::nullopt;
  promptHistoryRecallText_.clear();
  promptHistoryRecallIsDirectEntry_ = false;
  if (!shellStarted_) {
    if (!shell_.start()) return false;
    shellStarted_ = true;
  }
  shellReturnName_ = previousModeName(currentMode_);
  shellReturnMode_ = currentMode_;
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.redrawPromptRow();
  return true;
}

// K-60 (^Xs, M_hup): see the class's own declaration comment. Unlike
// ensureShellActive(), does NOT short-circuit when already in Shell
// mode -- that's the whole point of "restart" -- so it manages
// shellStarted_/currentMode_/shellReturnMode_ directly rather than
// delegating to ensureShellActive().
bool Application::shellRestart() {
  if (shell_.busy()) return false;
  shell_.terminate();
  shellStarted_ = false;
  if (!shell_.start()) return false;
  shellStarted_ = true;
  if (currentMode_ != ApplicationMode::Shell) {
    shellReturnName_ = previousModeName(currentMode_);
    shellReturnMode_ = currentMode_;
  }
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.redrawPromptRow();
  return true;
}

std::string Application::previousModeName(ApplicationMode mode) const {
  switch (mode) {
    case ApplicationMode::Directory:
      return directoryMode_.currentDirectory();
    case ApplicationMode::File:
      return fileMode_.currentFile();
    case ApplicationMode::Man:
      return manMode_.currentPage();
    case ApplicationMode::Archive:
    case ApplicationMode::ArchiveContent:
      return archiveMode_.currentArchive();
    case ApplicationMode::Du:
      return duMode_.currentPath();
    // Shell (2026-08-13): a direct user correction -- drawPromptLabel()'s
    // own destination-trailing field previously only ever showed
    // something for `mode == Directory`, on the reasoning that only
    // GrepPattern/CutPattern (the only two PromptKinds it was wired
    // for at all) had an established "returns to Directory" case to
    // mirror. The user pointed out this reasoning didn't hold: EVERY
    // prompt is launched FROM some mode, and that mode's own display
    // name IS the destination, independent of whether launching the
    // prompt itself happens to change currentMode_ or not ("PROGRAMを
    // 起動したモードは必ず存在する...SHELLならSHELL等"). This literal
    // "SHELL" fills in the one case previousModeName() itself hadn't
    // covered yet (existing callers -- Shell's own shellReturnName_,
    // K-2's own KEY trailing field -- never needed it: shellReturnName_
    // captures the mode BEFORE Shell, never Shell itself, and nothing
    // returns FROM a prompt directly into Shell without going through
    // ensureShellActive() first).
    case ApplicationMode::Shell:
      return "SHELL";
    default:
      return "";
  }
}

// K-17 (^X^W, M_w4): ported from menu.c's own `send_pp(hp->bp->pp,
// "a")` (append mode) -- writes activeView()'s current lines to
// currentWorkFilePath_, allocating a fresh w.N the FIRST time this (or
// K-64) is used this session and reusing it on every subsequent ^X^W
// press (2026-08-18: see allocateWorkFilePath()'s own comment for why
// K-64 instead always advances to a new number). Available from any
// mode (legacy's own M_w4 case has no mode guard either). False if
// the file couldn't be opened for writing (e.g. Application was
// constructed with an empty homeDirectory, see the constructor's own
// comment).
bool Application::saveBufferToFile() {
  if (currentWorkFilePath_.empty()) currentWorkFilePath_ = allocateWorkFilePath();
  std::ofstream out(currentWorkFilePath_, std::ios::app);
  if (!out) return false;
  for (const auto &line : activeView().lines()) out << line.text << "\n";
  return true;
}

// K-61 (^J., M_chdir2): ported from menu.c's own m_w() M_chdir2 case
// ("cd '<dir>'\r", auto-submitted). Uses DirectoryMode's own current
// directory as the closest match to legacy's lp->dir (a generic
// "current directory" field this port doesn't have an equivalent of
// outside DirectoryMode).
bool Application::shellChangeDirectory() {
  if (shell_.busy()) return false;
  std::string dir = directoryMode_.currentDirectory();
  if (dir.empty()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("cd '" + dir + "'");
  shell_.submitLine();
  return true;
}

// K-62 (^J,, M_chdir): ported from menu.c's own m_w() M_chdir case,
// which forwards a caller-supplied command string -- usilicrc's own
// K-62 description ("Execute `ls -l \cM`") names the literal command
// directly, so that's what's sent here rather than re-deriving it
// from wherever legacy's own ap->comm was populated.
bool Application::shellRunLsL() {
  if (shell_.busy()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("ls -l");
  shell_.submitLine();
  return true;
}

// K-63 (^Jw, M_w): ported from menu.c's own m_w() M_w case in
// SPIRIT, not literally -- legacy writes `name` to a temp file then
// builds "echo `cat <file>`" purely to sidestep 1990s shell-quoting
// concerns around embedding the name directly in a generated command
// string; this port's LineEditor::setText() just takes the string
// directly (no shell parsing happens until the user actually submits
// it), so inserting "echo <name>" straight into the command line has
// the same visible effect without the temp-file indirection. Uses
// the selected DirectoryMode entry's name, or FileMode's own current
// file's basename if already viewing one -- other modes aren't
// covered yet (a real "current access name" tracked generically
// across every mode, matching legacy's own ap->name, doesn't exist
// in this port).
//
// FULL path, not the bare entry/file name (2026-08-18 fix, a direct
// user report: "新では「echo ./」とか「echo CLAUDE.md」とかになる...
// SHELLのカレントディレクトリが(ls)のそれとは違う場合もあるので
// 「./」「CLAUDE.md」のフルパスへの変更は必要"): SHELL's own current
// directory isn't guaranteed to match DirectoryMode's/FileMode's own
// (e.g. after a `cd` typed directly into SHELL), so a bare relative
// name inserted there can resolve against the WRONG directory or fail
// outright. Directory mode's own self entry ("./") is special-cased to
// just the directory path itself (dropping the redundant "./" --
// concatenating it as-is would still work, `cat`/`echo` handle a
// trailing "/./" fine, but it reads oddly).
bool Application::shellInsertName() {
  if (shell_.busy()) return false;
  std::string name;
  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (entry) name = (entry->name == "./") ? directoryMode_.currentDirectory()
                                             : directoryMode_.currentDirectory() + entry->name;
  } else if (currentMode_ == ApplicationMode::File || shellReturnMode_ == ApplicationMode::File) {
    name = fileMode_.currentFile();  // already a full path
  }
  if (name.empty()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("echo " + name);
  shell_.commandLine().redraw();
  return true;
}

// K-64 (^J^W, M_w2): 2026-08-18 REDESIGN, a direct user request -- for
// Directory/File specifically, no longer allocates a numbered w.N
// under workRootDirectory_ at all. Instead saves under
// `<homeDirectory>/.usilic/<name>`, where `<name>` is the CURRENT
// mode's own basename -- DirectoryMode's own current directory's
// basename (e.g. ".../git/usilic/" -> "usilic") or FileMode's own
// current file's basename (e.g. ".../CLAUDE.md" -> "CLAUDE.md") --
// overwriting on every press rather than accumulating numbered slots,
// since the point is now "a persistent, human-readable snapshot of
// THIS mode", not a disposable scratch buffer. 2026-08-19 addition:
// ArchiveContent joined this group too (see its own branch below), a
// direct user report/screenshot (=di) that expected exactly this
// naming/location for a file opened from inside an archive.
//
// w.N fallback for every OTHER mode (2026-08-18, kept deliberately):
// Grep/Cut/CutContent/Man/Locate/Archive/Shell/Du/ListBrowser/Key have
// no established "mode name" concept the way Directory/File/
// ArchiveContent now do (matching shellInsertName()'s -- K-63's --
// own identical, already-accepted "other modes aren't covered yet"
// limitation right above) -- but UNLIKE K-63 (a convenience insert
// with no other way to reach the same effect), this key is also the
// only way several existing, already-fixed regressions reach Shell
// mode in the first place (e.g. the ensureShellActive() test right
// below this one: "start, ^J^S, ^J^W, run a shell command..." reached
// straight from the ^J^S/^J^C candidate list). Making it a silent
// no-op there would have quietly broken that reproduction path rather
// than just "not modernizing" it, so it keeps working exactly as
// before for every mode besides Directory/File/ArchiveContent.
//
// Writes activeView()'s CURRENT lines (the view active BEFORE
// switching to Shell mode, so this must run before ensureShellActive()
// changes currentMode_), matching the previous version's own identical
// ordering.
bool Application::shellInsertBufferContent() {
  if (shell_.busy()) return false;
  std::string modeName;
  if (currentMode_ == ApplicationMode::Directory) {
    std::string dir = directoryMode_.currentDirectory();
    if (!dir.empty() && dir.back() == '/') dir.pop_back();  // filename() of a "/"-terminated path is empty otherwise
    modeName = std::filesystem::path(dir).filename().string();
  } else if (currentMode_ == ApplicationMode::File) {
    modeName = std::filesystem::path(fileMode_.currentFile()).filename().string();
  } else if (currentMode_ == ApplicationMode::ArchiveContent) {
    // 2026-08-19 addition, a direct user report/screenshot (=di):
    // "=di→^J^W→/Users/haa15030/.usilic/saved/国際報道２０２６...ts" --
    // ArchiveContent was still falling through to the w.N fallback
    // described in this function's own class comment above ("no
    // established mode name concept... Archive/ArchiveContent...")
    // despite the archive content view being just as much "a file"
    // (see openSelectedArchiveEntry()'s own comment) as FileMode's own
    // real-filesystem case just above. Uses the selected entry's own
    // basename, same as archiveContentView_'s own mode-line provider
    // (application.cpp's setModeLineFieldsProvider() for this view).
    if (const ArchiveEntry *entry = archiveMode_.selectedEntry()) {
      modeName = std::filesystem::path(entry->name).filename().string();
    }
  }
  ContentView &source = activeView();
  std::string savePath;
  if (!modeName.empty()) {
    // <homeDirectory>/.usilic/saved/ (2026-08-19 fix, a direct user
    // request: "^J^Wの保存先は「~/.usilic/saved/」（無ければ自動
    // 作成）でよろしく") -- a dedicated subdirectory rather than
    // dropping named snapshots directly into .usilic/ alongside
    // usilicrc/history/error/shell, auto-created on first use since
    // (unlike .usilic/ itself, created unconditionally at startup --
    // see main()'s own usilicDir mkdir) nothing else guarantees it
    // exists yet.
    std::string savedDir = std::filesystem::path(usilicrcPath_).parent_path().string() + "/saved";
    std::error_code ec;
    std::filesystem::create_directories(savedDir, ec);
    savePath = savedDir + "/" + modeName;
  } else {
    currentWorkFilePath_ = allocateWorkFilePath();
    savePath = currentWorkFilePath_;
  }
  std::ofstream out(savePath, std::ios::trunc);
  if (!out) return false;
  for (const auto &line : source.lines()) out << line.text << "\n";
  out.close();
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("cat " + savePath);
  shell_.commandLine().redraw();
  return true;
}

// See allocateWorkFilePath()'s/allocateArcDirectory()'s own
// declaration comments (application.hpp).
std::string Application::allocateWorkFilePath() {
  std::string path = workRootDirectory_ + "/w." + std::to_string(++nextWorkFileNumber_);
  allocatedWorkFiles_.push_back(path);
  return path;
}

std::string Application::allocateArcDirectory() {
  std::string path = workRootDirectory_ + "/arc." + std::to_string(++nextArchiveNumber_);
  allocatedArcDirs_.push_back(path);
  return path;
}

// K-67: see the class's own declaration comment.
bool Application::executeShowErrorLog() {
  if (!std::filesystem::exists(errorLogPath_)) return false;
  if (!fileMode_.loadFile(errorLogPath_)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // see its own declaration comment
  return true;
}

// K-68: see the class's own declaration comment.
// Shared by executeShowShellLog() (K-68, ^X^Kl) and executeStartLog()
// (ESC l, LOG -- 2026-08-18 addition, see its own comment): writes
// Shell::scrollback()'s current lines to logFilePath_ (already
// with/without-args-aware, see tempRootDirectory_'s own comment), so
// both features always see the SAME up-to-date snapshot. False if
// Shell was never started (nothing to snapshot) or the file couldn't
// be opened for writing.
bool Application::refreshShellLogFile() {
  if (!shellStarted_) return false;
  std::ofstream out(logFilePath_, std::ios::trunc);
  if (!out) return false;
  for (const auto &line : shell_.scrollback().lines()) out << line.text << "\n";
  return static_cast<bool>(out);
}

bool Application::executeShowShellLog() {
  if (!refreshShellLogFile()) return false;
  if (!fileMode_.loadFile(logFilePath_)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // see its own declaration comment
  return true;
}

// ESC l (LOG): a real, direct-entry-point version of the "actually
// working manual procedure" CLAUDE.md history item 176 verified after
// a first attempt (Command::StartShellLog, intercepting raw ESC WHILE
// INSIDE Shell mode) turned out to be structurally impossible --
// feedShellByte() deliberately never lets a lone ESC reach
// dispatchLeaderByte() at all (vi-mode/Meta-key compatibility, the
// real shell needs it), so that attempt could never actually fire and
// was reverted. This one is different in the way that matters: it's
// reached the SAME way every other ESC-leader command is (this
// switch, dispatchLeaderByte()'s escPending_ case), which -- per that
// same investigation -- is only ever reachable OUTSIDE Shell mode
// (Directory/File/etc, exactly where the validated manual procedure
// itself starts from after its own ^G). What used to be 6 manual
// steps (^X^Kl snapshot -> ^G back to Directory -> select the log file
// -> ^Jb split-without-opening -> type a pattern -> Enter) collapses
// to this single automatic one, using "usilic-1.5b:" (item 335's own
// new bold SHELL-prompt marker) as the fixed split pattern instead of
// that old attempt's literal demo string "tcb v1.5b".
bool Application::executeStartLog() {
  if (!refreshShellLogFile()) return false;
  if (!fileMode_.loadFile(logFilePath_)) return false;
  std::vector<StyledLine> sourceLines;
  sourceLines.reserve(fileView_.lines().size());
  for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
  cutSyntax_ = GrepSyntax::Fixed;
  // Set BEFORE calling split(), not after checking its return value --
  // discovered via a real PTY test: CutMode::split() redraws cutView_
  // INTERNALLY (its own last line), before this function ever gets a
  // chance to run, so the mode line's very first frame showed "CUT"
  // (whatever cutOrigin_ happened to already be) instead of "LOG",
  // self-correcting only on some LATER, unrelated redraw. Turned out to
  // be a pre-existing ordering bug affecting every OTHER cutOrigin_
  // assignment in this file too (executeCut()'s File/Directory
  // branches, startHelp()) -- fixed alongside this one. Harmless to set
  // even on eventual failure: every failure path inside split() returns
  // before its own redraw(), so nothing ever renders using a "wasted"
  // assignment here.
  cutOrigin_ = CutOrigin::Log;
  if (!cutMode_.split(sourceLines, "usilic-1.5b:", cutSyntax_)) return false;
  currentMode_ = ApplicationMode::Cut;
  return true;
}

// K-0 (^T): redesigned 2026-08-18 in response to a real report
// (screenshot =cm) that two concurrently-running argument-launched
// instances showed each other's stale ERROR/SHELL content here, since
// every path shown used to be one fixed location under
// `<homeDirectory>/.usilic` regardless of how many instances were
// running. Field layout is the user's own explicit design (not ported
// from anything in legacy, which has no per-instance concept to begin
// with): "Boot options"/"Startup file" are single-line fields right-
// justified to a common label width; "ERROR"/"SHELL"/"WORK" are
// shorter labels right-justified to that SAME width (so every colon
// lines up); WORK lists every w.N ever allocated this session
// (allocatedWorkFiles_, oldest first -- empty if K-17/K-64 haven't
// been used yet), continuation lines indented to the path column with
// no repeated label; arc.N (nested-archive descent, allocatedArcDirs_)
// follows the same way, entirely omitted if empty (unlike WORK, which
// always shows its own label even with nothing after it) since unlike
// WORK there's no single always-relevant "the" ARC concept to anchor
// an empty label to. "History file"/"User name" (both present in the
// OLD layout) are gone entirely: historyFilePath_ is a genuinely
// cross-session accumulating ring, not a per-instance artifact this
// redesign's "here's what THIS instance is using" framing is about,
// and User name was never referenced by any of this round's own
// report.
std::string Application::bannerFilePath() const {
  return std::filesystem::path(usilicrcPath_).parent_path().string() + "/usilic";
}

bool Application::executeShowTcbBanner() {
  constexpr int kLabelWidth = 12;  // "Boot options"/"Startup file" own width -- every other label right-justifies to this
  auto field = [](const std::string &label) {
    std::string padded = label;
    if (static_cast<int>(padded.size()) < kLabelWidth) padded = std::string(kLabelWidth - padded.size(), ' ') + padded;
    return padded + " : ";
  };
  std::string continuationIndent(kLabelWidth + 3, ' ');  // aligns under field()'s own " : "-following column

  std::ostringstream banner;
  banner << field("Boot options") << bootOptions_ << "\n";
  banner << field("Startup file") << usilicrcPath_ << "\n";
  banner << field("ERROR") << errorLogPath_ << "\n";
  banner << field("SHELL") << logFilePath_ << "\n";
  banner << field("WORK");
  for (std::size_t i = 0; i < allocatedWorkFiles_.size(); i++) {
    if (i) banner << continuationIndent;
    banner << allocatedWorkFiles_[i] << "\n";
  }
  if (allocatedWorkFiles_.empty()) banner << "\n";
  for (std::size_t i = 0; i < allocatedArcDirs_.size(); i++) banner << continuationIndent << allocatedArcDirs_[i] << "\n";

  std::string text = banner.str();
  std::vector<unsigned char> bytes(text.begin(), text.end());
  // "usilic", NOT "tcb" (2026-08-18): FileMode's own mode-line provider
  // derives its `name` field from the loaded file's bare basename (see
  // its own comment) -- this is the entire mechanism behind the
  // requested "mode name shown here is usilic" without needing a
  // dedicated ContentView/mode of its own. bannerFilePath()'s own
  // directory (NOT tempRootDirectory_/allocateArcDirectory()) -- see
  // its own declaration comment.
  std::filesystem::path bannerPath(bannerFilePath());
  std::string tempPath =
      materializeArchiveTempFile(bannerPath.parent_path().string(), bannerPath.filename().string(), bytes);
  if (tempPath.empty() || !fileMode_.loadFile(tempPath)) return false;
  // WORK/arc.N candidate-list selection (2026-08-18 fix, =cq): a real
  // report found these lines impossible to pick out at all -- FileMode
  // itself disables cursorHighlightEnabled_ by default (correct for
  // plain pager reading, see its own constructor comment), which this
  // screen specifically needs turned back ON. loadFile() itself resets
  // it to that same "off" default on every call (see its own comment)
  // so this doesn't leak into whatever gets loaded next.
  fileView_.setCursorHighlightEnabled(true);
  // Highlight only from after the "<label> : " separator (2026-08-18
  // fix, a direct user report: "一覧表示のハイライトを「: 」の後から
  // にしてほしい"): every line loadFile()/readAndWrap() just produced
  // for this synthetic banner has the exact same shape -- either
  // `field(label) + value` or `continuationIndent + value` -- both of
  // which put the VALUE at this same fixed column (continuationIndent's
  // own width), so a single uniform highlightStartColumn works for
  // every line without needing to distinguish which field it belongs
  // to (mirrors DirectoryMode's own nameStartColumn precedent for `ls
  // -l` rows). NOTE: a value long enough to WRAP onto a second physical
  // row (readAndWrap()'s own doing, before this code ever sees the
  // result) would apply this same column to that continuation row too,
  // slightly misplacing the highlight there -- accepted as a rare edge
  // case (this port's own $HOME/usilicrc paths are normally far short
  // of the ~65-column budget available before wrapping), not solved
  // here.
  {
    std::vector<ContentView::Line> lines = fileView_.lines();
    for (auto &line : lines) line.highlightStartColumn = static_cast<int>(continuationIndent.size());
    fileView_.setLines(std::move(lines));
  }
  currentMode_ = ApplicationMode::File;
  // fileReturnMode_ = Directory as usual, but see returnToDirectory()'s
  // own 2026-08-18 comment: unlike every other File-mode entry point,
  // this one deliberately does NOT get the "follow the file's real
  // parent directory" treatment on the way back out (this file's own
  // directory, tempRootDirectory_, is essentially never where the user
  // actually wants to end up).
  fileReturnMode_ = ApplicationMode::Directory;  // see its own declaration comment
  // Return-destination display (2026-08-18 fix, a direct user report:
  // launched via "-c ~/git/usilic" then ^T showed "/Users/haa15030/
  // .usilic" as the trailing "戻り値" field instead of "~/git/usilic").
  // FileMode's own default trailing2 (see its constructor's own
  // comment) is the loaded file's OWN containing directory -- correct
  // for a real file, but this file's real parent (bannerFilePath()'s
  // own directory) is never where ^G above actually returns to (see
  // the comment just above); setSearchOrigin() already exists exactly
  // for "override the destination shown here" (LOCATE's own File
  // results use it identically), reused here with the literal name
  // this screen already shows ("usilic") so only trailing2 visibly
  // changes. directoryMode_.currentDirectory() is right regardless of
  // which mode ^T was actually pressed from, since fileReturnMode_
  // above is unconditionally Directory and nothing along that path
  // touches DirectoryMode's own state.
  fileMode_.setSearchOrigin("usilic", directoryMode_.currentDirectory());
  return true;
}

// K-0/^T's own candidate-list selection (2026-08-18): the original
// mockup annotated the WORK/arc.N lines with "[↑ ハイライト文字列
// 選択後は][戻り値「usilic」画面へ]" -- select one of those lines and
// Enter opens it, ^G comes back to this banner specifically. Reuses
// the SAME generic cursorIndex_/moveUp()/moveDown() every other
// ContentView already has (see ContentView::redraw()'s own
// highlighted-line rendering, mode-agnostic by construction) rather
// than rebuilding this as a dedicated ListBrowserMode-style screen --
// fileView_ never opted OUT of cursor highlighting the way
// cutContentView_/scrollback_ do, so up/down + a highlighted row
// already worked here for free; only Enter needed new plumbing.
// Matches by SUFFIX rather than exact-line-equals: the "WORK" line
// itself carries a "WORK : " label prefix (field()'s own format) that
// continuation/arc.N lines don't, so suffix-matching handles both
// shapes uniformly without needing to special-case the first row.
// w.N entries open as a FILE (fileReturnMode_ = File, a new case
// added to Command::Back's own File branch, routes straight back to
// executeShowTcbBanner() instead of returnToDirectory() -- see that
// switch's own comment); arc.N entries open as a DIRECTORY via a
// plain enterDirectory() with no stack frame pushed (mirrors how
// jumping to an arbitrary path via ^Xd already behaves when reached
// outside normal descent: ^G is a harmless no-op once the stack is
// empty, rather than risking a stateful "return to banner" flag going
// stale across however many further directories the user might
// descend into from here).
bool Application::openSelectedBannerEntry() {
  if (fileMode_.currentFile() != bannerFilePath()) return false;
  const auto &lines = fileView_.lines();
  int idx = fileView_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(lines.size())) return false;
  const std::string &text = lines[idx].text;
  auto endsWith = [&text](const std::string &suffix) {
    return !suffix.empty() && text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  // Startup file/ERROR/SHELL (2026-08-18 fix, a direct user report:
  // "ハイライト上の[return]が反応しない" -- every line here is now
  // highlighted the same way (see executeShowTcbBanner()'s own
  // highlightStartColumn comment), so a user landing the cursor on
  // ANY of these three reasonably expects Enter to open the file it
  // names, not silently do nothing (previously only WORK/arc.N were
  // wired up at all). "Boot options" is deliberately excluded -- it's
  // literal argv text, not a path to any real file. Opens as a FILE,
  // same fileReturnMode_ = File convention as WORK below, so ^G comes
  // straight back to this banner. loadFile() itself already fails
  // gracefully (false) for a path that doesn't exist yet (ERROR/SHELL
  // before anything's ever been logged there) -- Enter simply stays a
  // no-op in that case, same as it already was for an empty WORK slot.
  // saveCursorPosition() BEFORE loadFile() overwrites currentFile_/
  // resets the view (2026-08-18 fix, a direct user report: "^Gで同じ
  // 行に戻らない" -- every OTHER File-mode-to-File-mode-ish transition
  // in this class saves the position it's LEAVING first, matching
  // returnFromFileToLocate()'s own identical two-line shape, but this
  // one never did -- loadFile()'s own History::positionFor() lookup
  // (keyed on bannerFilePath(), a fixed path) had nothing to restore
  // from, so ^G back to the banner always landed on row 0 regardless
  // of which line was actually selected).
  //
  // setSearchOrigin("usilic") AFTER loadFile() (same report: "戻り値
  // が不良"): FileMode's own default trailing2 for a real file is its
  // containing directory (see the constructor's own comment) -- true
  // but misleading here, since ^G from any of these doesn't lead to a
  // directory listing of `/tmp/.usilic.<pid>/` at all, it goes straight
  // back to the ^T banner screen, exactly the same "destination shown
  // here should be where ^G ACTUALLY goes" fix already applied to the
  // banner's own trailing2 above. `label` is passed as the file's own
  // bare basename (unchanged from what FileMode would already show) so
  // only the destination half of the mode line actually changes.
  for (const std::string &fixedPath : {usilicrcPath_, errorLogPath_, logFilePath_}) {
    if (!endsWith(fixedPath)) continue;
    fileMode_.saveCursorPosition();
    if (!fileMode_.loadFile(fixedPath)) return false;
    fileMode_.setSearchOrigin(std::filesystem::path(fixedPath).filename().string(), "usilic");
    fileReturnMode_ = ApplicationMode::File;
    // loadFile() already redrew once internally, using the DEFAULT mode
    // line (searchOrigin was still empty at that point -- it's only set
    // just above, AFTER loadFile() clears it) -- an explicit second
    // redraw() is what actually gets the corrected trailing2 onto the
    // real screen, matching openSelectedLocateResult()'s own identical
    // ordering/comment for the exact same reason.
    fileView_.redraw();
    return true;
  }
  for (const auto &workPath : allocatedWorkFiles_) {
    if (!endsWith(workPath)) continue;
    fileMode_.saveCursorPosition();
    if (!fileMode_.loadFile(workPath)) return false;
    fileMode_.setSearchOrigin(std::filesystem::path(workPath).filename().string(), "usilic");
    fileReturnMode_ = ApplicationMode::File;
    fileView_.redraw();  // see the fixedPath loop's own identical comment above
    return true;
  }
  for (const auto &arcPath : allocatedArcDirs_) {
    if (!endsWith(arcPath)) continue;
    if (!directoryMode_.enterDirectory(arcPath)) return false;
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  return false;
}

// K-27: see the class's own declaration comment.
bool Application::cycleCharset() {
  encoding_ = (encoding_ == Charset::Encoding::Ascii)  ? Charset::Encoding::Euc
              : (encoding_ == Charset::Encoding::Euc) ? Charset::Encoding::Utf8
                                                       : Charset::Encoding::Ascii;

  fileMode_.setEncoding(encoding_);
  manMode_.setEncoding(encoding_);
  shell_.setEncoding(encoding_);
  promptEditor_.setEncoding(encoding_);

  if (currentMode_ == ApplicationMode::File) fileMode_.reflow(display_.columns());
  else if (currentMode_ == ApplicationMode::Man) manMode_.reflow(display_.columns());

  return true;
}

// Ported from M_cp2/M_mv2 (^Xc/^Xm) entering a destination-path
// prompt: Directory-only, and only when there's something to act on
// (marked entries, or the current selection -- see
// DirectoryMode::targetEntries()).
bool Application::startCopyPrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  activatePrompt(PromptKind::CopyDestination, "");
  return true;
}

bool Application::startMovePrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  activatePrompt(PromptKind::MoveDestination, "");
  return true;
}

// Ported from M_rm2 (^Xr) entering a confirmation prompt -- a
// deliberately simplified stand-in for m_dest()'s T_rm branch (which
// checks a single typed 'y' the same way), see class comment.
bool Application::startRemovePrompt(bool interactive) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (directoryMode_.targetEntries().empty()) return false;
  cpMvRmInteractive_ = interactive;
  activatePrompt(PromptKind::RemoveConfirm, "");
  return true;
}

// Ported from M_exit (^X^C) entering a confirmation prompt -- see
// class comment for why this is reachable from any mode and doesn't
// need a targetEntries()-style guard the way CP/MV/RM do.
bool Application::startExitPrompt() {
  // "y" pre-filled (2026-08-05 fix, a real CUI screenshot's "y" was
  // barely visible next to "(y/n) ? " because it's ALREADY there when
  // the prompt first appears, not typed by the user afterward) -- the
  // default answer is "yes, exit", so pressing Enter alone confirms;
  // backspacing it away and typing "n" (then Enter) still cancels, via
  // executeExit()'s own `confirmation != "y"` check, unchanged.
  activatePrompt(PromptKind::ExitConfirm, "y");
  return true;
}

// Ported from m_du()/m_cr()'s T_du case: unlike every other prompt
// above, the field is PRE-FILLED with the current directory
// (`strcpy(rvp->str, ap->du)`) rather than starting empty -- ^M alone
// accepts the default, or the user can edit first. Directory-only,
// see class comment.
bool Application::startDuPrompt() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  activatePrompt(PromptKind::DuPath, directoryMode_.currentDirectory());
  return true;
}

// K-57/K-58: see the class's own declaration comment.
bool Application::startArchivePrompt(bool interactive) {
  if (currentMode_ == ApplicationMode::Archive) {
    if (archiveMode_.targetEntries().empty()) return false;
  } else if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry || entry->isDirectory) return false;
    std::string path = directoryMode_.currentDirectory();
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry->name;
    ArchiveKind kind = ArchiveKind::None;
    if (!configuration_.programForSuffix(path, &kind) || kind == ArchiveKind::None) return false;
  } else {
    return false;
  }
  archiveExtractInteractive_ = interactive;
  activatePrompt(PromptKind::ArchiveDestination, "");
  return true;
}

std::vector<std::string> Application::buildArchiveExtractArgv(const std::string &archivePath, ArchiveKind kind,
                                                                const std::vector<std::string> &members,
                                                                const std::string &destination) const {
  if (kind == ArchiveKind::Tar) {
    std::vector<std::string> argv = {"tar", "xf", archivePath, "-C", destination};
    for (const auto &m : members) argv.push_back(m);
    return argv;
  }
  if (kind == ArchiveKind::Zip) {
    std::vector<std::string> argv = {"unzip", "-o", archivePath};
    for (const auto &m : members) argv.push_back(m);
    argv.push_back("-d");
    argv.push_back(destination);
    return argv;
  }
  return {};  // Lha/Rpm/Deb/Bzip2 -- not supported by this first slice, see the class's own declaration comment
}

// K-45/K-47 (^Xf/^Xd, "Input file/directory name") -- unlike
// startDuPrompt() and friends, deliberately NOT gated to Directory
// mode: legacy's own M_lfile2/M_ldir2 are general "type any path,
// land wherever it goes" navigation, usable from File/Man/Grep/etc
// just as much as from a directory listing. Both keys share this one
// prompt/execute pair since PathResolver already decides file-vs-
// directory from the path itself, not from which key was pressed --
// `isDirectory` is stashed in pathInputIsDirectory_ purely for
// promptLabelText()'s benefit (legacy shows a distinct " FILE"/" DIR"
// mode-line label per key even though the two share this one
// execution path; a real side-by-side screenshot comparison,
// 2026-08-03, found this port always showing "FILE/DIR" regardless of
// which key was pressed).
bool Application::startPathInputPrompt(bool isDirectory) {
  pathInputIsDirectory_ = isDirectory;
  activatePrompt(PromptKind::PathInput, "");
  return true;
}

// Shared by startListBrowser() and toggleListBrowserHistory()'s own
// non-chronological branch: History::recentEntries() is now the RAW
// access log (see History::pushRecent()'s own comment, 2026-08-04
// correction) and CAN contain non-consecutive duplicates -- e.g.
// accessing A, then B, then A again leaves [A, B, A] in the ring. The
// sorted "unique recent items" view needs its own de-duplication over
// that raw log rather than relying on the ring never repeating, which
// a std::set does in one step (sorted AND unique).
std::vector<std::string> Application::sortedUniqueRecentEntries(RingCategory category) const {
  const std::deque<std::string> &recent = history_.recentEntries(category);
  std::set<std::string> unique(recent.begin(), recent.end());
  return std::vector<std::string>(unique.begin(), unique.end());
}

// Ported from m_list()'s dispatch, narrowed to the two least
// ambiguous categories (see class comment). Not prompt-driven -- this
// is an immediate action, like archive-browsing's auto-detection,
// not a text-entry flow.
//
// NOT gated to Directory mode (2026-08-12 fix -- an earlier version of
// this comment claimed "Directory-only entry, matching every other
// history-adjacent action in this slice", which was never actually
// true of the whole K-45..K-48 family: startPathInputPrompt() (K-45/
// K-47, "Input file/directory name", just above) was ALREADY
// deliberately unrestricted -- "usable from File/Man/Grep/etc just as
// much as from a directory listing" -- and usilicrc documents K-46/
// K-48 ("Select file/directory name") and K-50/^J^R (COMMAND, which
// also calls this function) with the exact same "reachable from any
// mode" intent, no Directory-only caveat anywhere in its own K-?
// table. A real user report + direct clarification: "^X^F/^X^D自体、
// どのモードからでも（^Rも含め）移行可能な仕様が目的" (the whole POINT
// of ^X^F/^X^D's own design is to be reachable from ANY mode) --
// caught while trying ^J^C (CUT) and finding these didn't work from
// there either. This function's own body needs nothing from
// Directory-specific state to run (listBrowserReturnMode_ just
// captures whatever currentMode_ already was, for ANY mode, exactly
// the same way startPromptHistoryRecall() does for its own MAN/
// LOCATE/GREP/CUT/PROGRAM recall categories -- this Directory-only
// guard was the ONE inconsistency, not a structural need).
//
// Shown sorted alphabetically, de-duplicated -- confirmed directly by
// the user (2026-08-04): "FILE（DIR）は今までアクセスしたファイル
// （ディレクトリ）をソートしたのち重複を排除した一覧" ("FILE/DIR is
// a list of accessed files/directories, sorted then de-duplicated").
// De-dup is done HERE at display time (sortedUniqueRecentEntries()),
// not by History::pushRecent() itself -- a same-day correction: an
// earlier version of this comment claimed pushRecent()'s own
// first-occurrence-wins policy already handled it, which was true of
// an EARLIER (also-since-corrected) version of pushRecent() but not
// the real legacy behavior it was meant to port (see pushRecent()'s
// own comment). An even earlier version of this function showed
// reverse-chronological order (recent.rbegin()/rend()) instead of
// sorted order, which the user's clarification revealed was never
// actually what legacy shows for this normal (non-^R) view either.
// The raw chronological order (duplicates included) is real, but
// belongs to the SEPARATE "HISTORY" toggle instead -- see
// toggleListBrowserHistory().
bool Application::startListBrowser(RingCategory category) {
  // 2026-08-15 fix: a real report + screenshot (=bf/=bg, ^X^D showing
  // a garbled trailing2 fragment from an unrelated earlier LOCATE
  // path) traced back to promptHistoryRecallKind_/
  // promptHistoryRecallIsDirectEntry_ being left set by an EARLIER,
  // unrelated GREP/CUT/MAN direct-entry candidate-list session that
  // never fully resolved (e.g. backed out via ^G partway through) --
  // listBrowserView_'s own ModeLineFieldsProvider checks
  // `if (promptHistoryRecallKind_)` to decide whether THIS is a
  // direct-entry regex list (name/trailing2 shaped very differently)
  // or a plain MAN/PROGRAM/COMMAND picker, and a stale
  // leftover value from a completely different feature made it take
  // the wrong branch. startListBrowser() is the single shared entry
  // point for every PLAIN picker (never itself a direct-entry regex
  // list), so clearing both here guarantees a fresh, correct state
  // regardless of what an earlier, unrelated session left behind.
  // (2026-08-17: RecentFile/RecentDirectory -- ^X^F/^X^D -- no longer
  // reach this function at all, see Command::StartFileHistory/
  // StartDirectoryHistory's own call sites -- they now go through
  // startRegexPromptList(PromptKind::RecentFile/RecentDirectory)
  // instead, since legacy's own T_hfile/T_hdir turned out to be a
  // live-editable combobox too, not a plain picker. Only
  // Command::StartCommandHistory -- RingCategory::ShellCommand -- still
  // calls this.)
  promptHistoryRecallKind_ = std::nullopt;
  promptHistoryRecallIsDirectEntry_ = false;
  listBrowserCategory_ = category;
  listBrowserReturnMode_ = currentMode_;
  listBrowserChronological_ = false;
  // See ContentView::setCursorAtModeLineTrailingEnd()'s own comment --
  // a plain FILE/DIR picker, unlike the GrepPattern/CutPattern
  // candidate list, so the caret belongs at the highlighted row.
  listBrowserView_.setCursorAtModeLineTrailingEnd(false);
  listBrowserMode_.show(sortedUniqueRecentEntries(category));
  currentMode_ = ApplicationMode::ListBrowser;
  return true;
}

// K-49 (plain ^R, M_hist): see ToggleHistoryView's own comment for the
// full story. Re-shows listBrowserCategory_'s own ring, but toggled
// between startListBrowser()'s own sorted/de-duplicated order and raw
// chronological order (History::recentEntries() already returns
// oldest-first, i.e. exactly "newest at the bottom row" with no
// reversal needed) -- entering chronological order additionally moves
// the cursor to the LAST entry, so the newest item is what's actually
// visible/selected first, matching the user's own description
// ("最下行が最新", confirmed directly, 2026-08-04) and legacy's own
// mode.c (M_hist/T_hist group: `pnp->row = hp->bp->nrow - 1;`).
// A no-op outside ListBrowser mode, matching m_list()'s own
// `echo_mess(S_errf)` for a context ^R doesn't apply to.
bool Application::toggleListBrowserHistory() {
  if (currentMode_ != ApplicationMode::ListBrowser) return false;
  // A prompt-triggered recall (startPromptHistoryRecall()) has no
  // separate "sorted" view to toggle INTO in the first place -- see
  // its own comment on why (no non-h ring exists in legacy for any of
  // these categories) -- so a second ^R here is simply a no-op,
  // rather than incorrectly re-sorting a screen whose mode-line would
  // then still misleadingly read "HISTORY". RecentFile/RecentDirectory
  // (2026-08-17 addition) are the one exception among promptHistoryRecallKind_
  // holders: unlike GREP/CUT/MAN/LOCATE/PROGRAM, legacy's own T_hfile/
  // T_hdir DO have a separate sorted ring (that same comment's own
  // parenthetical), now reached via this same direct-entry machinery
  // for the typing capability, but still expected to support the
  // ordinary sorted<->chronological ^R toggle underneath.
  if (promptHistoryRecallKind_ && *promptHistoryRecallKind_ != PromptKind::RecentFile &&
      *promptHistoryRecallKind_ != PromptKind::RecentDirectory) {
    return false;
  }
  listBrowserChronological_ = !listBrowserChronological_;
  std::vector<std::string> items;
  if (listBrowserChronological_) {
    // Raw log, duplicates included -- see History::pushRecent()'s own
    // comment for why this ring can legitimately repeat an entry.
    const std::deque<std::string> &recent = history_.recentEntries(listBrowserCategory_);
    items.assign(recent.begin(), recent.end());
  } else {
    items = sortedUniqueRecentEntries(listBrowserCategory_);
  }
  listBrowserMode_.show(items);
  if (listBrowserChronological_ && !items.empty()) {
    // show() itself already redrew once with cursorIndex_ freshly
    // reset to 0 -- redraw() again so the screen (not just internal
    // state) actually reflects the moved-to-the-end cursor below.
    listBrowserView_.setCursorIndex(static_cast<int>(items.size()) - 1);
    listBrowserView_.redraw();
  }
  // RecentFile/RecentDirectory (2026-08-18 fix, =cz report -- "文字列
  // 入力エリアが無い"): this function is the ONE other place besides
  // startPromptHistoryRecall()/Command::MoveUp/MoveDown that redraws
  // the read-only candidate screen for these two PromptKinds, and it
  // was never wired to refreshDirectEntryModeLine() (added one round
  // ago) -- so a ^R toggle here left row 24 on the OLD, pre-item-351
  // rendering (listBrowserView_'s own plain ModeLineFieldsProvider,
  // fully reversed with no gaps and no non-highlighted candidate) even
  // though every other redraw of this same screen already looks right.
  // A no-op for every other PromptKind (the early-return guard above),
  // matching every other call site's own guard.
  refreshDirectEntryModeLine();
  return true;
}

// K-2 (^H, M_key, "KEY"): see kKeyTable's own comment for why this is
// a hand-maintained mirror of dispatchByte()'s switch statements
// rather than a real usilicrc KEY-section parse. Reachable from any
// mode (matching HELP's own no-mode-guard precedent, and legacy's own
// init_bp() default case) -- re-entry from within Key mode itself is
// refused, same shape as every other Start*() in this class.
bool Application::startKey() {
  if (currentMode_ == ApplicationMode::Key) return false;
  std::vector<ContentView::Line> lines;
  lines.reserve(std::size(kKeyTable));
  // "<label> : <description>", front-padded (leading spaces, i.e.
  // right-justified) to the longest label actually IN this table --
  // not a fixed constant -- so every row's " : " lands in the same
  // column, matching legacy's own print_key() column alignment
  // (though legacy computes its column width differently). The
  // user's own 2026-08-05 reports iterated this twice: first the
  // previous fixed-10-column-then-two-spaces scheme was too wide for
  // short labels (e.g. "^X^C" padded out to 10 columns before "  "
  // even started), which the first fix corrected by dropping padding
  // entirely -- but that left the ":" characters NOT lined up between
  // rows of different label lengths, which this front-padding now
  // fixes without reintroducing the original wide-gap problem (the
  // padding amount is now the minimum needed to align, not a
  // generous fixed constant).
  std::size_t maxLabelWidth = 0;
  for (const auto &entry : kKeyTable) maxLabelWidth = std::max(maxLabelWidth, std::string(entry.keyLabel).size());
  for (const auto &entry : kKeyTable) {
    std::string label = entry.keyLabel;
    if (label.size() < maxLabelWidth) label.insert(0, maxLabelWidth - label.size(), ' ');
    std::string text = label + " : " + entry.description;
    if (entry.isMacro) text += " (M)";
    lines.push_back({text, std::nullopt, std::nullopt, {}});
  }
  keyView_.setLines(std::move(lines));
  keyView_.setCursorIndex(0);
  keyReturnMode_ = currentMode_;
  currentMode_ = ApplicationMode::Key;
  keyView_.redraw();
  return true;
}

bool Application::returnFromKey() {
  currentMode_ = keyReturnMode_;
  activeView().redraw();
  return true;
}

// See this class's own declaration comment (application.hpp) for the
// "return first, THEN execute" order HELP.jp 2.2 documents.
bool Application::executeSelectedKeyBinding() {
  int idx = keyView_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(std::size(kKeyTable))) return false;
  std::vector<int> bytes = kKeyTable[idx].bytes;  // copy: returnFromKey() below may redraw/mutate state first

  currentMode_ = keyReturnMode_;
  activeView().redraw();
  bool result = false;
  for (int byte : bytes) result = feedByte(byte, std::nullopt);
  return result;
}

// Ported from m_grep(): entering FGREP/EGREP just remembers where to
// come back to (legacy: save_stack()) and switches to a prompt state
// so the next bytes are the search pattern, not commands. Refuses to
// start a nested search from within grep results, matching m_grep()'s
// own is_name(ap->name, M_fgrep/M_egrep) guard.
//
// EGREP (Regex) is a deliberate exception (2026-08-05): see
// startRegexPromptList()'s own comment for why it skips the blank
// prompt and opens directly into the candidate list instead -- FGREP
// (Fixed) is unaffected.
bool Application::startGrepPrompt(GrepSyntax syntax) {
  if (currentMode_ == ApplicationMode::Grep) return false;
  grepSyntax_ = syntax;
  grepReturnMode_ = currentMode_;
  if (syntax == GrepSyntax::Regex) return startRegexPromptList(PromptKind::GrepPattern);
  activatePrompt(PromptKind::GrepPattern, "");
  return true;
}

// K-32/K-34 (^J^S EGREP, ^J^C CUT's regex variant, "Select string" per
// usilicrc's own K-31..K-34 documentation -- unlike K-31/K-33's
// "Input string" FGREP/CUT-fixed): direct legacy-tcb PTY testing
// (fresh $HOME, no ~/.tcb/history at all -- see item 170's own ⑫
// investigation) confirmed this isn't just a documentation nuance:
// ^J^S/^J^C open DIRECTLY into the candidate-pattern list (usilicrc's
// own GREP section, see Configuration::grepSeedPatterns()'s comment
// and the seeding this port now does at construction) rather than a
// blank prompt -- ^R is never even pressed to get there. A 2026-08-05
// user report asked for this specifically for CUT: unlike FGREP/EGREP
// (visually distinguishable by their own mode-line names), CUT's two
// variants both show identically as "CUT" -- showing the list upfront
// is the only visible signal that regex/"Select" semantics are in
// effect for THIS invocation, preventing a user from typing a literal
// string into what they assume is CUT's fixed-string variant.
//
// Reuses startPromptHistoryRecall() wholesale rather than duplicating
// its RingCategory-selection switch: it already knows how to (a) pick
// the right ring from promptKind_/grepSyntax_/cutSyntax_ (both already
// set by the caller before this runs), (b) restore an editable BLANK
// prompt on ^G (returnFromListBrowser()'s own promptHistoryRecallKind_
// branch) -- this doubles as the "type a fresh pattern instead of
// picking one" affordance, so no new UI had to be built for that -- and
// (c) dispatch Enter on a selected item through the exact same
// executeGrep()/executeCut() path a live Enter would take.
// promptEditor_.setText("") first: without it, startPromptHistoryRecall()
// would capture whatever STALE text happened to be left over from the
// last time any prompt was used, and restore that (not a fresh blank
// prompt) if the user backs out via ^G.
//
// 2026-08-13 (CLAUDE.md history item 289's follow-up): despite the
// name, none of this is actually regex-specific -- it's just "open a
// prompt's history ring as a candidate list instead of a blank
// editor", which is exactly what MAN's/PROGRAM's own "Select" keys
// (K-44/K-42) need too (startManList()/startProgramList()), matching
// usilicrc's "Select" wording for those two the same way EGREP/
// CUT-regex's "Select string" wording is handled here. Not renamed to
// avoid an unrelated diff across every existing EGREP/CUT call site.
bool Application::startRegexPromptList(PromptKind kind) {
  promptKind_ = kind;
  promptEditor_.setText("");
  return startPromptHistoryRecall(/*directEntry=*/true);
}

// Ported from m_cut(): reachable from File mode (a fresh split of the
// whole file), CutContent/Cut (resplitting, see the 2026-08-05 comment
// just below), or -- as of 2026-08-11 -- Directory mode (a fresh split
// of the current `ls` listing itself, mirroring startGrepPrompt()'s
// own unrestricted Directory support; see executeCut()'s own Directory
// branch for where the listing's lines actually get read). Starting
// from Grep isn't part of this slice.
bool Application::startCutPrompt(GrepSyntax syntax) {
  // ApplicationMode::Cut (2026-08-05 fix): re-splitting was already
  // correctly wired for CutContent (having ENTERED a specific
  // segment's own content first), but a direct user reproduction
  // (select HELP.jp -> ^Jc "man" -> now viewing the label LIST itself,
  // ApplicationMode::Cut, cursor on one entry -> ^Jc "tcb" AGAIN)
  // showed this guard rejecting the second ^Jc outright, silently
  // dropping the whole "tcb" keystroke sequence into ordinary list
  // navigation instead. CutMode::selectedSegment()/splitSelected()
  // already read view_'s (== cutView_'s) OWN cursorIndex_ regardless
  // of which of cutView_/cutContentView_ happens to be the currently
  // DISPLAYED view -- nothing about them actually required already
  // being inside CutContent, this guard was just stricter than it
  // needed to be.
  // 2026-08-11: ApplicationMode::Directory added -- a real report
  // found ^J^C from a plain directory listing was a silent no-op ("反応
  // 無し"), unlike ^J^S (EGREP, startGrepPrompt() has no such
  // restriction at all). CUT was never wired to read directoryView_'s
  // OWN lines the way GrepMode::search() already branches on
  // grepReturnMode_ to do -- see executeCut()'s own new Directory
  // branch, added alongside this.
  if (currentMode_ != ApplicationMode::File && currentMode_ != ApplicationMode::CutContent &&
      currentMode_ != ApplicationMode::Cut && currentMode_ != ApplicationMode::Directory)
    return false;
  cutSyntax_ = syntax;
  if (currentMode_ == ApplicationMode::File) cutReturnMode_ = ApplicationMode::File;
  // cutReturnMode_ is left untouched for Directory (its class default
  // is already ApplicationMode::Directory, matching startCutFilePrompt()'s
  // own identical reasoning, see its comment).
  // Regex ("^J^C", "Select string"): opens directly into the candidate
  // list, same as EGREP -- see startGrepPrompt()/startRegexPromptList()'s
  // own comment for why (a 2026-08-05 user report specifically named
  // CUT here, since its two variants -- unlike FGREP/EGREP -- share one
  // identical "CUT" mode-line name with no other visible cue which is
  // active).
  if (syntax == GrepSyntax::Regex) return startRegexPromptList(PromptKind::CutPattern);
  activatePrompt(PromptKind::CutPattern, "");
  return true;
}

// K-35/K-36's "regular file" CUT variant -- CutMode's own class
// comment already flagged this exact shortcut (split a file's content
// straight from a directory listing, without a separate "open it
// first" step) as unwired future work; wiring it here is just:
// resolve the selected entry's path, stash it, and reuse the SAME
// CutPattern prompt/executeCut() the buffer variant already has --
// executeCut() loads cutFileSourcePath_ via FileMode before splitting
// when it's non-empty, instead of reading the already-displayed
// buffer. cutReturnMode_ is deliberately left untouched here: its
// class default is already ApplicationMode::Directory, exactly where
// this variant is entered from and should return to.
bool Application::startCutFilePrompt(GrepSyntax syntax) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry || entry->isDirectory) return false;
  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;
  cutFileSourcePath_ = path;
  cutSyntax_ = syntax;
  // Regex ("^J^B"): same "opens directly into the candidate list"
  // treatment as the buffer variant's own ^J^C -- see
  // startCutPrompt()/startRegexPromptList()'s own comment. executeCut()
  // reads cutFileSourcePath_ (set above) regardless of whether the
  // pattern came from live typing or a picked list item, so nothing
  // else here needs to change for this path.
  if (syntax == GrepSyntax::Regex) return startRegexPromptList(PromptKind::CutPattern);
  activatePrompt(PromptKind::CutPattern, "");
  return true;
}

// Ported from m_man(): unlike GREP/CUT, m_man() has no is_name()
// nested-entry guard and doesn't read anything from the currently
// active view (see class comment) -- so this is reachable from any
// mode, including Man itself (viewing a different page without
// leaving Man mode first). manReturnMode_ is only updated the FIRST
// time (i.e. when not already in Man mode), so ^G after browsing
// several pages in a row still returns to wherever the excursion
// originally started, not to the page viewed just before it.
bool Application::startManPrompt() {
  if (currentMode_ != ApplicationMode::Man) manReturnMode_ = currentMode_;
  activatePrompt(PromptKind::ManName, "");
  return true;
}

// K-44 (^J^M, "MAN: Select manual page"): unlike LOCATE/DU's "Execute"/
// "Redisplay window" pair (see CLAUDE.md history item 288's
// investigation, and item 289's follow-up), usilicrc's own wording
// here really is "Input"/"Select" -- the same shape as EGREP's ^J^S --
// so this opens directly into a candidate list of recent MAN pages via
// startRegexPromptList()'s shared machinery (its own doc comment
// covers the mechanics; it's not actually regex-specific despite the
// name). manReturnMode_ is set with the exact same "only when not
// already in Man" rule startManPrompt() uses just above -- needed
// because executeMan() (reached once a candidate is picked, via
// openSelectedListEntry()'s promptHistoryRecallKind_ branch) doesn't
// touch manReturnMode_ itself, only currentMode_.
bool Application::startManList() {
  if (currentMode_ != ApplicationMode::Man) manReturnMode_ = currentMode_;
  return startRegexPromptList(PromptKind::ManName);
}

// Ported from m_prog()'s M_prog/M_prog2 entry (see class comment for
// why, unlike GREP/CUT/MAN, this never switches ApplicationMode at
// all -- it runs a foreground command and returns to whatever was
// already showing).
//
// 2026-08-17 fix (docs/ideas/IDEAS.md:218, a NEW feature -- legacy
// never had this, confirmed by the user directly: "IDEAS.mdだから旧
// では動かない"): a real report + hand-drawn mockup (=cb, "お手本は
// 手入力なので念のため" -- the reference was typed by hand, not a real
// screenshot of working legacy or C++ behavior) specified that ^Jp
// should pre-fill the prompt with the highlighted/open file's own
// BARE filename, cursor on the FIRST character, so typing a program
// name ("less ") prepends naturally into "less CLAUDE.md" rather than
// requiring the file's full path to be typed by hand. Previously
// always started with an empty prompt regardless of context, which is
// what this now falls back to whenever no file context exists (e.g.
// invoked from a screen with no current file). sourceFile mirrors
// goToFileSelector()'s own currentMode_ branches (Directory's
// selectedEntry, File/Cut/CutContent's fileMode_.currentFile()) --
// deliberately narrower than that function's full set (ListBrowser/
// Locate) since those weren't part of what was tested/requested here.
bool Application::startProgramPrompt() {
  std::string sourceFile;
  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (entry && !entry->isDirectory) {
      sourceFile = directoryMode_.currentDirectory();
      if (!sourceFile.empty() && sourceFile.back() != '/') sourceFile += '/';
      sourceFile += entry->name;
    }
  } else if (currentMode_ == ApplicationMode::File || currentMode_ == ApplicationMode::Cut ||
             currentMode_ == ApplicationMode::CutContent) {
    sourceFile = fileMode_.currentFile();
  }

  std::string seed;
  bool cursorAtStart = false;
  programPromptDirectory_.clear();
  if (!sourceFile.empty()) {
    std::filesystem::path path(sourceFile);
    programPromptDirectory_ = path.parent_path().string();
    seed = path.filename().string();
    cursorAtStart = true;
  }
  activatePrompt(PromptKind::ProgramCommand, seed, "", cursorAtStart);
  return true;
}

// K-42 (^J^P, "PROGRAM: Select program name"): same "Input"/"Select"
// shape as MAN's K-43/K-44 just above -- see startManList()'s own
// comment. Unlike MAN, no return-mode bookkeeping is needed here:
// executeProgram() never changes ApplicationMode at all (this
// function's own class comment), so there's nothing for a later ^G to
// need to unwind beyond what startPromptHistoryRecall() already
// captures in promptHistoryRecallReturnMode_.
bool Application::startProgramList() { return startRegexPromptList(PromptKind::ProgramCommand); }

// Ported from m_locate()'s M_locate (^J^L) path: unlike MAN/PROGRAM,
// entering LOCATE from within its own results screen is refused
// (m_locate()'s own is_name(ap->name, M_locate) guard) -- the one
// property LOCATE shares with GREP rather than MAN/PROGRAM.
//
// 2026-08-13 update (docs/ideas/IDEAS.md's LOCATE section, CLAUDE.md
// history item 288): legacy's own ^Jl "reuse the previous result set
// without re-prompting" shortcut (M_locate2) is NOT replicated --
// ^Jl now runs updatedb instead (LocateUpdateDb/runLocateUpdateDb()),
// a feature legacy never had. What IS replicated, in spirit, is the
// requested UX: the prompt opened here is seeded with the MOST
// RECENT locate query (rather than blank) so that pressing Enter
// unchanged re-shows the previous results without re-running `locate`
// (see executeLocate()'s own comment for the other half of this).
bool Application::startLocatePrompt() {
  if (currentMode_ == ApplicationMode::Locate) return false;
  locateReturnMode_ = currentMode_;
  const std::deque<std::string> &recent = history_.recentEntries(RingCategory::RecentLocateQuery);
  activatePrompt(PromptKind::LocateQuery, recent.empty() ? std::string() : recent.back());
  return true;
}

// K-39 (^Jl, "LOCATE: Execute `locate'"), per docs/ideas/IDEAS.md's
// LOCATE section: refreshes the locate database. Doesn't touch
// currentMode_/any view -- stays wherever the user was, matching how
// this is a maintenance action rather than a navigation one.
//
// 2026-08-13 update, a direct user request: (1) chain straight into
// K-40's own seeded prompt (startLocatePrompt()) once updatedb()
// finishes, rather than just silently returning to wherever the user
// was -- IDEAS.md's own spec ("^Jl -> updatedbを実行") only covered
// running the command, this closes the loop into an actual next
// action; (2) show an underlined "Updating database..." message at
// the LOCATE prompt's own field position WHILE updatedb() is running
// (this port stays single-threaded/synchronous throughout -- "機能
// 自体は完了でいい" (the function itself doesn't need to change) --
// so this doesn't need real async plumbing, just draw-then-block-
// then-draw-again in this one call).
//
// 2026-08-13 follow-up: the first version of this hand-rolled its own
// copy of drawPromptLabel()'s label-drawing logic directly here -- a
// real follow-up report ("モードライン共有ルーチンはできない？")
// pointed out that was exactly the kind of one-off duplication item
// 291's own mode-line audit had just finished eliminating elsewhere.
// Now goes through drawPromptLabel() itself (its own staticMessage
// parameter, added specifically for this case) instead of
// re-deriving the label/column math independently.
// 2026-08-14 update: "^Jl直後に「Updating database...」がモードライン
// 左端に出てくれないと意味がない。フリーズ中ではないメッセージなので"
// -- draws the message immediately now (see below), never hidden
// behind any wait.
//
// 2026-08-14 follow-up (a real report: "④の^[不具合は-gで起きる模様。
// ^Jl後の^Gも受け付けない。-cでは問題なし"): TWO earlier same-day
// attempts at "常時^Gで...移行" each had a real problem. The first
// simply reverted to item 294's original BLOCKING locateMode_.
// updateDb() call, reasoning ^G would already work once
// startLocatePrompt() was reached -- but a real updatedb run turned
// out to take MINUTES, during which that blocking call never returns,
// so ^G is never read at all. The second fixed THAT by polling
// keyboard input AND ChildProcess::hasExited() in an unbounded loop
// inside this same call -- which worked in -c (confirmed by direct PTY
// testing) but NOT in -g: that loop blocked INSIDE this one
// Application call, so app/main.cpp's own GUI event pump
// (runGuiPhase()'s gui.pumpEvents()) never ran again until the loop
// returned, meaning a real ^G click/keypress in the SDL window was
// queued by SDL but never actually processed. -c's own run loop
// happened to still work only because STDIN_FILENO IS the real
// keyboard there -- incidental, not by design, and broke this class's
// own "backend-agnostic" property (see its class comment) the moment a
// second backend was tried against it.
//
// Redesigned a third time to not block AT ALL: launches the detached
// updatedb (LocateMode::startUpdateDb()) and returns immediately,
// leaving locateUpdateDbPid_ set. awaitingLocateUpdateDb()/
// pollLocateUpdateDbCompletion() (see their own comments) let EITHER
// run loop's own existing per-iteration tick drive the wait instead --
// -c's poll(STDIN_FILENO,...,100) loop and -g's while(!wantsExit())
// frame loop both already iterate on their own regardless of backend,
// so neither needs new backend-specific plumbing. ^G itself needs no
// special polling at all: it arrives as an ordinary byte through
// feedByte()/dispatchByte()'s own new top-level
// awaitingLocateUpdateDb() gate, the exact same call both run loops
// already make for every other keystroke.
bool Application::runLocateUpdateDb() {
  static const std::string kUpdatingMessage = "Updating database...";
  drawPromptLabel(PromptKind::LocateQuery, /*positionText=*/"", kUpdatingMessage);
  display_.flush();

  locateUpdateDbPid_ = locateMode_.startUpdateDb();
  if (locateUpdateDbPid_ <= 0) {
    locateUpdateDbPid_ = -1;
    startLocatePrompt();
    return false;
  }
  return true;  // now awaiting -- see awaitingLocateUpdateDb()/pollLocateUpdateDbCompletion()
}

bool Application::awaitingLocateUpdateDb() const { return locateUpdateDbPid_ > 0; }

void Application::pollLocateUpdateDbCompletion() {
  if (locateUpdateDbPid_ <= 0) return;
  if (!ChildProcess::hasExited(locateUpdateDbPid_)) return;
  locateUpdateDbPid_ = -1;
  startLocatePrompt();
}

bool Application::feedPromptByte(int byte) {
  // ^Q (2026-08-11 fix): a real user recollection -- "文字列入力の際の
  // コントロールコードは^Qの前置で入力できたような" (control codes
  // during string entry used to be enterable by prefixing with ^Q).
  // LineEditor already implements toggleQuotedInsert()/
  // quotedInsertPending()/clearQuotedInsert() (ported from key.c's own
  // K_q case, `Key.f.ctrl = 1;`) but nothing anywhere ever called them
  // -- same "existing but unwired" pattern as killToStart()/killToEnd()
  // before item 273's fix. Checked FIRST, before every other special
  // byte below: once armed, the VERY NEXT byte is inserted literally
  // regardless of what it would otherwise mean (even kCarriageReturn/
  // kControlG/^R/^U/^K/DEL), matching usilicrc's own GREP seed
  // patterns that embed literal control bytes mid-pattern (confirmed
  // by reading the raw file: line 64's "^  [0-9]\x08[0-9]+\." has a
  // literal ^H, line 71's "^\x0c" a literal ^L -- ^Q^H/^Q^L is
  // presumably how those were originally typed in).
  if (promptEditor_.quotedInsertPending()) {
    promptEditor_.clearQuotedInsert();
    promptEditor_.insertByte(static_cast<unsigned char>(byte));
    promptEditor_.redraw();
    return true;
  }
  if (byte == kControlQ) {
    promptEditor_.toggleQuotedInsert();
    return true;
  }
  // Leader sequences (^J/^X/^O/ESC, 2026-08-12 fix): a direct user
  // complaint that the string-entry routines look "ad-hoc" (場当たり的)
  // because ^J^W ("SHELL: insert buffer content"/K-64) worked from the
  // ^J^S candidate list (feedRegexListByte()'s own 2026-08-11 fix,
  // right above) but NOT from FGREP/CUT's own plain prompt entry --
  // still showed literal reversed "JW" text there. Root cause: THIS
  // function -- the ONE shared prompt-editing routine already used by
  // every PromptKind (GrepPattern covers both FGREP's ^Jc AND EGREP's
  // direct-entry path, CutPattern, ManName, ProgramCommand, LocateQuery,
  // Copy/Move/RemoveDestination, ExitConfirm, DuPath, PathInput,
  // ArchiveDestination -- see the enum's own comment) -- never checked
  // for leader bytes at all, unlike feedRegexListByte(). Fixing it HERE
  // instead of per-mode is exactly what was asked: "全モードが同ルーチン
  // を共有化する形に作り直して欲しい" -- and since every one of those
  // prompt kinds already funnels through this single function, one
  // check here covers all of them at once, matching legacy key.c's own
  // `case K_w: if (f_M != T_input) { c = T_w; break; }` design (leader
  // bytes resolve to their COMMAND meaning in every context except
  // T_input; ^Q's quoted-insert above remains the one escape hatch for
  // typing a literal leader byte into the pattern itself, e.g. usilicrc's
  // own ^Q^H-embedded GREP patterns). shellInsertBufferContent() itself
  // already reads activeView().lines() generically (not gated on
  // currentMode_ beyond that), so it naturally captures whatever screen
  // is actually behind the active prompt -- FGREP/CUT's directory
  // listing, a Grep/Cut results screen mid re-search, etc. -- matching
  // "画面表示のある場面ならどこでも有効" (anywhere with an on-screen
  // view). SHELL itself is excluded automatically: feedShellByte() has
  // its own separate input path entirely (dispatchByte() routes there
  // before promptKind_ is ever consulted for a SHELL-originated prompt),
  // so this change can't reach in and reinterpret SHELL's own live
  // command line. Routed to dispatchLeaderByte() directly, same
  // reasoning as feedRegexListByte(): calling dispatchByte() here would
  // just re-enter feedPromptByte() through its own promptKind_ check
  // (promptKind_ hasn't changed), so go straight to the leader resolver.
  if (escPending_ || leaderJPending_ || leaderXPending_ || leaderXKPending_ || leaderOPending_ ||
      byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape) {
    return dispatchLeaderByte(byte, std::nullopt);
  }
  if (byte == kCarriageReturn) return submitPrompt();
  if (byte == kControlG) {
    cancelPrompt();
    return true;
  }
  // K-49 (^R): see startPromptHistoryRecall()'s own comment. Swallowed
  // either way (not inserted as literal text) -- 0x12 isn't printable,
  // and legacy's own m_list() dispatch never falls through to typing
  // it literally for a context ^R doesn't apply to either.
  if (byte == 0x12) {
    startPromptHistoryRecall();
    return true;
  }
  // K-65 (^I): PathInput's own buffer IS a path in full, so the whole
  // text gets completed. PROGRAM (2026-08-17 addition, screenshot =ce:
  // "'less '^Iでは反応無し") is different -- its buffer is a full shell
  // command line (e.g. "less CLAUDE.md"), of which only the LAST
  // WORD is typically a filename argument worth completing, matching
  // exactly how Shell::onTab() already completes the SHELL command
  // line's own last word (same split-on-space-before-cursor logic,
  // copied here rather than shared since promptEditor_ and
  // shell_.commandLine() are separate LineEditor instances with no
  // common owner to hang a shared helper off of). MAN/LOCATE/GREP/etc
  // still have nothing meaningful to complete (a page name/query/
  // pattern isn't a filesystem path at all), so still excluded.
  if (byte == 0x09 && (promptKind_ == PromptKind::PathInput || promptKind_ == PromptKind::ProgramCommand)) {
    const std::string &text = promptEditor_.text();
    int cursor = promptEditor_.cursorBytePos();
    std::string partial = text;
    std::size_t wordStart = 0;
    if (promptKind_ == PromptKind::ProgramCommand) {
      std::size_t searchFrom = (cursor > 0) ? static_cast<std::size_t>(cursor - 1) : 0;
      wordStart = text.find_last_of(" \t", searchFrom);
      wordStart = (wordStart == std::string::npos) ? 0 : wordStart + 1;
      if (static_cast<int>(wordStart) > cursor) wordStart = static_cast<std::size_t>(cursor);
      partial = text.substr(wordStart, static_cast<std::size_t>(cursor) - wordStart);
    }
    auto candidates = completeFilesystemPath(partial, directoryMode_.currentDirectory());
    if (candidates.size() == 1 && candidates[0].size() > partial.size()) {
      promptEditor_.paste(candidates[0].substr(partial.size()));
    }
    return true;
  }
  // DEL / ^H (2026-08-05): every prompt kind (MAN/LOCATE/FGREP/EGREP/
  // CUT/PROGRAM/PathInput/Copy/Move/Remove/.../DuPath/ArchiveDestination
  // -- anything routed through this shared feedPromptByte()) fell
  // through to insertByte() below, which has no concept of "erase" and
  // just inserts the raw 0x7f/0x08 byte as an unprintable literal
  // (rendered as a reversed "?" by print_buff()-equivalent rendering).
  // feedShellByte() already has this same backspace()/redraw() pairing
  // for the SHELL command line's own LineEditor (see its own comment
  // just above, ~line 907) -- promptEditor_ is the same LineEditor
  // class and already exposes backspace(), this generic prompt path
  // was just never wired to call it.
  if (byte == 0x7f || byte == 0x08) {
    promptEditor_.backspace();
    promptEditor_.redraw();
    return true;
  }
  // ^A/^E/^B/^F (2026-08-12 fix): LineEditor already implements
  // moveHome()/moveEnd()/moveLeft()/moveRight() (used by feedShellByte()
  // for the SHELL command line's own arrow-key handling) but nothing
  // ever wired the readline-style CONTROL-KEY equivalents (^A/^E/^B/^F)
  // into this shared prompt path -- a real report: "^A^E^B^F等文字列
  //編集のためのキーが利かずハイライトAEBFとなる" (^A/^E/^B/^F, the
  // keys for editing the string, don't work -- they show up as reversed
  // "AEBF" instead), i.e. these fell all the way through to the literal
  // insertByte() fallback below, each control byte rendered as its own
  // reversed "@"-offset glyph (LineEditor::renderVisible()'s own
  // convention: byte b < ' ' renders as reversed chr(b+'@'), so 0x01+
  // 0x40='A', 0x05+0x40='E', 0x02+0x40='B', 0x06+0x40='F' -- exactly
  // "AEBF"). Note this port's OWN plain ^B/^F command bindings
  // (resolveCommand()'s PageUp/PageDown, usilicrc's K-8/K-9) never
  // apply here regardless: dispatchByte() already routes every byte to
  // feedPromptByte() FIRST whenever a prompt is active, before
  // resolveCommand() is ever consulted.
  if (byte == 0x01) {  // ^A: beginning of line
    promptEditor_.moveHome();
    return true;
  }
  if (byte == 0x05) {  // ^E: end of line
    promptEditor_.moveEnd();
    return true;
  }
  if (byte == 0x02) {  // ^B: back one character
    promptEditor_.moveLeft();
    return true;
  }
  if (byte == 0x06) {  // ^F: forward one character
    promptEditor_.moveRight();
    return true;
  }
  if (byte == 0x04) {  // ^D: delete forward (the readline-family counterpart to ^A/^E/^B/^F above)
    promptEditor_.deleteForward();
    return true;
  }
  // ^U/^K/^Y (2026-08-11 fix): LineEditor already implements
  // killToStart()/killToEnd()/yank() (ported from key.c's K_u/K_k/K_y
  // cases -- see their own doc comments) but nothing anywhere ever
  // called them, so these bytes fell through to insertByte() below and
  // got inserted as literal control characters instead of editing the
  // line. A real report: after startRegexPromptList()'s candidate list
  // (^J^C) transitions into editable-prompt mode pre-filled with the
  // highlighted candidate's text (feedRegexListByte()'s own "start
  // editing" fallback), ^U -- meant to clear that pre-filled text
  // before typing a fresh pattern -- silently corrupted the prompt
  // text instead (a literal 0x15 byte prepended), so the subsequently
  // typed pattern never matched anything and the search silently
  // failed ("何も起きない").
  if (byte == 0x15) {  // ^U: kill from field start to cursor
    promptEditor_.killToStart();
    promptEditor_.redraw();
    return true;
  }
  if (byte == 0x0b) {  // ^K: kill from cursor to field end
    promptEditor_.killToEnd();
    promptEditor_.redraw();
    return true;
  }
  if (byte == 0x19) {  // ^Y: re-insert the ^U/^K kill buffer
    promptEditor_.yank();
    promptEditor_.redraw();
    return true;
  }
  // ^N/^P (2026-08-12 fix): a direct user report -- typing even one
  // character while browsing the ^J^S/^J^C candidate list correctly
  // transitions into editing (feedRegexListByte()'s own "start
  // editing" fallback), but that used to permanently give up ^N/^P's
  // OWN meaning there ("さらに^N^Pが利かなくなる（他のハイライト
  // 文字列へ移れない）") -- they fell through to the generic
  // control-byte-rejection catch-all just below, silently beeping
  // instead of cycling. The candidate LIST itself stays fully visible
  // on screen the whole time editing is active (activatePrompt()'s own
  // "start editing" comment), so there's no reason ^N/^P shouldn't
  // keep moving through it even mid-edit -- this re-seeds the prompt
  // with whichever candidate ^N/^P lands on next, discarding whatever
  // had been typed so far (matching a combobox's own "cycle through
  // the ORIGINAL suggestions" semantics, not an attempt to preserve
  // in-progress edits across a jump to a different entry). Only active
  // when promptCandidateListActive_ (see its own comment) -- a plain
  // FGREP/MAN/LOCATE/etc prompt never came from a list to cycle
  // through in the first place, so ^N/^P there still falls through to
  // the ordinary rejection below.
  if ((byte == 0x0e || byte == 0x10) && promptCandidateListActive_) {
    if (byte == 0x0e) listBrowserView_.moveDown();
    else listBrowserView_.moveUp();
    listBrowserView_.redraw();
    int count = static_cast<int>(listBrowserMode_.items().size());
    int idx = listBrowserView_.cursorIndex();
    const std::string *selected = listBrowserMode_.selectedItem();
    drawPromptLabel(promptKind_, count > 0 ? ModeLine::format(idx + 1, count) : "");
    promptEditor_.setText(selected ? *selected : "");
    return true;
  }
  // Reject any OTHER unrecognized control byte outright rather than
  // inserting it literally (2026-08-12 fix, a direct user instruction:
  // "コントロールコード入力は^Qの前置が必須、を反映して" -- control-code
  // entry should require the ^Q prefix). Before this, e.g. a stray Tab
  // (0x09, meaningful only for PathInput's own completion above) typed
  // into a GrepPattern/CutPattern/etc. prompt fell all the way through
  // to the plain insertByte() fallback below, silently corrupting the
  // pattern with a literal control byte -- rendered as a reversed
  // control-character glyph (see the ^A/^E/^B/^F comment above for the
  // "byte+'@'" convention) that reads as a display glitch rather than
  // what it actually is. ^Q's own quoted-insert check at the very top
  // of this function remains the ONE way to insert a literal control
  // byte on purpose (already true before this change -- this only
  // closes off the UNQUOTED path for bytes that used to slip through).
  if (byte < 0x20) {
    display_.putChar(7);  // BEL, matching LineEditor's own boundary-reject convention
    return true;
  }
  promptEditor_.insertByte(static_cast<unsigned char>(byte));
  promptEditor_.redraw();
  return true;
}

bool Application::submitPrompt() {
  PromptKind kind = promptKind_;
  std::string text = promptEditor_.text();
  promptKind_ = PromptKind::None;
  return executePromptSubmission(kind, text);
}

bool Application::executePromptSubmission(PromptKind kind, const std::string &text) {
  switch (kind) {
    case PromptKind::GrepPattern:
      return executeGrep(text);
    case PromptKind::CutPattern:
      return executeCut(text);
    case PromptKind::ManName:
      return executeMan(text);
    case PromptKind::ProgramCommand:
      return executeProgram(text);
    case PromptKind::LocateQuery:
      return executeLocate(text);
    case PromptKind::CopyDestination:
      return executeCopy(text);
    case PromptKind::MoveDestination:
      return executeMove(text);
    case PromptKind::RemoveConfirm:
      return executeRemove(text);
    case PromptKind::ExitConfirm:
      return executeExit(text);
    case PromptKind::DuPath:
      return executeDu(text);
    case PromptKind::ArchiveDestination:
      return executeArchiveExtract(text);
    case PromptKind::PathInput:
      return executePathInput(text);
    // K-46/K-48's own submission: identical path validation to
    // PathInput (K-45/K-47) -- legacy's own key.c groups T_hfile/T_hdir
    // in the SAME buff[] tilde-expand+access()-check block as
    // M_lfile/M_ldir (the Input variant), just reached via the history
    // list instead of a blank prompt. See startListBrowser()'s own
    // comment for the rest of this feature.
    case PromptKind::RecentFile:
      return executePathInput(text);
    case PromptKind::RecentDirectory: {
      // Unlike PathInput, K-48 also needs openSelectedListEntry()'s OLD
      // RecentDirectory branch's resetNavigationToFloor() call (see its
      // own declaration comment: ^X^D is explicitly NOT ordinary
      // Enter-into-subdirectory descent, so ^G afterward should toggle
      // between here and the previous directory, not unwind a
      // multi-level descent chain from before the picker opened) --
      // that branch is now unreachable for RecentDirectory specifically
      // (promptHistoryRecallKind_ takes priority, see
      // openSelectedListEntry()'s own comment) but its behavior still
      // needs to survive the move to this shared submission path,
      // whether reached by picking a highlighted entry OR typing a
      // fresh path.
      std::string previousDirectory = directoryMode_.currentDirectory();
      if (!executePathInput(text)) return false;
      if (currentMode_ == ApplicationMode::Directory) directoryMode_.resetNavigationToFloor(previousDirectory);
      return true;
    }
    case PromptKind::None:
      return false;
  }
  return false;
}

void Application::cancelPrompt() {
  promptKind_ = PromptKind::None;
  activeView().redraw();
}

// K-49 (^R) pressed while a prompt is active: ported from menu.c's
// m_list(), which picks a RingCategory purely from the CURRENT
// prompt's own context (`is_name(ap->name, M_man) -> T_hman`, etc) --
// see the class's own investigation notes (CLAUDE.md item 161) for
// the full source read this is based on, including why FILE/DIR
// (ToggleHistoryView, an already-open-list TOGGLE) and this (a
// prompt-time RECALL that opens a list for the first time) are
// different integration points even though they end up sharing the
// same ListBrowser rendering/RingCategory machinery underneath.
// MAN/LOCATE/FGREP/EGREP (2026-08-04) plus PROGRAM (2026-08-05,
// integration point 4 -- see CLAUDE.md item 162's own 4-point plan):
// ProgramMode::run() already pushes every executed invocation's FULL
// command string (e.g. "less /path/to/HELP") into
// RingCategory::ShellCommand -- ported from do_prog()'s own
// `*histp = save_history(*histp, str, 1);`, which records into the
// SAME shared ss.p.hist ring do_shell() uses for SHELL commands (see
// mode.c's `do_prog(rvp->str, &hp->ss.p.cmd, &hp->ss.p.hist, 1);` call
// site). Selecting a recalled entry here auto-EXECUTES it (matching
// MAN/LOCATE/FGREP, unlike SHELL's own ^R -- confirmed directly by
// the user, 2026-08-05: "あくまで旧版にしたがうと(B)" -- since
// PROGRAM invocations are already restricted to
// Configuration::programSeedCommands()'s allow-list, unlike SHELL's
// unrestricted command line).
// CutPattern (2026-08-05, plugging the one deliberate gap left in the
// batch above): legacy reuses ss.p.hegrep for CUT patterns (menu.c's
// `case M_cut: case M_cut2: hp->ss.p.hegrep = save_history(...)`, the
// same source read cited above) -- reused here as
// RingCategory::RecentEgrepPattern rather than inventing a new ring,
// matching that exact quirk instead of guessing at a cleaner design.
// A real user reproduction (2026-08-05, screenshot with ⑪) showing 11
// mixed "man"/"manager"/"tcb" entries in one CUT-triggered recall is
// consistent with this: "manager" is very plausibly a real EGREP
// search the user ran at some point, sharing the ring with their CUT
// patterns "man"/"tcb" -- flagged to the user as this function's own
// working assumption, not independently confirmed beyond the source
// read plus this one screenshot's circumstantial match.
//
// That same screenshot also corrected TWO things this function
// originally got wrong (an earlier same-day revision showed a
// SORTED, de-duplicated list here, name=category, e.g. "FGREP"):
//   - The list itself is the RAW chronological log, duplicates
//     included (matching History::pushRecent()'s own ring -- item
//     161's correction), not sortedUniqueRecentEntries(). Unlike
//     FILE/DIR's own ^X^F/^X^D (which DO have a separate sorted ring
//     to show normally, toggled to chronological only on ^R --
//     ToggleHistoryView), these prompt-triggered categories have no
//     such separate sorted ring in legacy at all (confirmed during
//     the same source read that produced this function originally:
//     ss.p.hman/hlocate/hfgrep/hegrep are ALL that exist for those
//     four, no non-h counterpart) -- so there is nothing to be
//     "sorted" INTO here; pressing ^R at a prompt always means "show
//     me the raw history", full stop.
//   - The mode-line's name field is the fixed literal "HISTORY" (not
//     the category), matching mp[M_hist]==" HISTORY" applying
//     uniformly across T_hfile/T_hdir/T_hman/T_hlocate/T_hfgrep/
//     T_hegrep regardless of which ring backs the current screen
//     (menu.c's m_list(), read early in this whole investigation but
//     not fully connected to THIS function's own rendering choice
//     until now) -- WHICH category is shown instead as a separate
//     right-aligned badge, via listBrowserView_'s own
//     ModeLineFieldsProvider reading promptHistoryRecallKind_ (see
//     its own comment).
bool Application::startPromptHistoryRecall(bool directEntry) {
  RingCategory category;
  switch (promptKind_) {
    case PromptKind::ManName:
      category = RingCategory::RecentManPage;
      break;
    case PromptKind::LocateQuery:
      category = RingCategory::RecentLocateQuery;
      break;
    case PromptKind::GrepPattern:
      category = (grepSyntax_ == GrepSyntax::Fixed) ? RingCategory::RecentFgrepPattern : RingCategory::RecentEgrepPattern;
      break;
    case PromptKind::ProgramCommand:
      category = RingCategory::ShellCommand;
      break;
    case PromptKind::CutPattern:
      // See executeCut()'s own 2026-08-05 correction comment: FIXED
      // (^Jc) shares FGREP's own ring, not always EGREP's.
      category = (cutSyntax_ == GrepSyntax::Fixed) ? RingCategory::RecentFgrepPattern : RingCategory::RecentEgrepPattern;
      break;
    case PromptKind::RecentFile:
      category = RingCategory::RecentFile;
      break;
    case PromptKind::RecentDirectory:
      category = RingCategory::RecentDirectory;
      break;
    default:
      return false;
  }
  promptHistoryRecallKind_ = promptKind_;
  promptHistoryRecallText_ = promptEditor_.text();
  promptHistoryRecallReturnMode_ = currentMode_;
  promptHistoryRecallIsDirectEntry_ = directEntry;
  promptKind_ = PromptKind::None;
  listBrowserCategory_ = category;
  // 2026-08-12 fix: see ContentView::setCursorAtModeLineTrailingEnd()'s
  // own comment. Both GrepPattern's and CutPattern's rings share this
  // (RecentFgrepPattern/RecentEgrepPattern) -- true whether reached via
  // direct entry (^J^S/^J^C) or a real ^R mid-typing recall, since
  // BOTH are the same live-editable combobox (feedRegexListByte()'s
  // dispatch check is keyed on promptHistoryRecallKind_'s VALUE alone,
  // not directEntry). Every other category (MAN/LOCATE/PROGRAM) stays
  // false -- Enter simply selects there, no live-editing paradigm.
  listBrowserView_.setCursorAtModeLineTrailingEnd(category == RingCategory::RecentFgrepPattern ||
                                                   category == RingCategory::RecentEgrepPattern);
  // ^J^S/^J^C's own DIRECT-ENTRY candidate list (2026-08-12 fix,
  // scoped narrowly to directEntry -- see below for why): sorted +
  // de-duplicated, NOT the raw chronological log every other
  // prompt-triggered recall uses (the very policy the big comment above
  // this function documents being corrected TO, from an earlier
  // same-day revision that got this wrong the other way). A real user
  // request: "GREPの文字列一覧だが、同ファイルを読み込んだ後もソート
  // してほしい...のちのち文字列数が多くなってから起きる重複等回避
  // するためにもやはりソート後重複削除は必要" (please also sort the
  // GREP string list after loading the file, not just keep the file
  // itself pre-sorted -- sort-then-dedup is needed to avoid duplicates
  // once the pattern count grows). Deliberately NOT applied to a real
  // ^R recall (directEntry==false, e.g. mid-typing a pattern and
  // pressing ^R): that case is exactly what the comment above already
  // settled, backed by a real screenshot (⑪) of legacy's own raw,
  // duplicate-including recall list -- this fix only concerns the
  // separate direct-entry candidate list ^J^S/^J^C open into BEFORE any
  // typing happens (matching legacy's own referenced screenshots ㊳/㊴
  // showing THAT list sorted), and only for GrepPattern/CutPattern
  // (MAN/LOCATE/PROGRAM's own direct-entry-less recall is untouched).
  // RecentFile/RecentDirectory (2026-08-17 addition, K-46/K-48) are
  // ALWAYS reached via directEntry (there's no separate "real ^R mid-
  // typing recall" entry point for these two -- startListBrowser() is
  // the only caller) and were already shown sorted+de-duplicated even
  // before this whole PromptKind-based rewrite (the plain
  // ListBrowserMode path this replaces called sortedUniqueRecentEntries()
  // directly), so keeping that same ordering here is a straight carry-
  // forward, not a new behavior change.
  bool sortDirectEntryList = directEntry && (category == RingCategory::RecentFgrepPattern ||
                                              category == RingCategory::RecentEgrepPattern ||
                                              category == RingCategory::RecentFile ||
                                              category == RingCategory::RecentDirectory);
  listBrowserChronological_ = !sortDirectEntryList;
  std::vector<std::string> items;
  if (sortDirectEntryList) {
    items = sortedUniqueRecentEntries(category);
  } else {
    const std::deque<std::string> &recent = history_.recentEntries(category);
    items.assign(recent.begin(), recent.end());
  }
  listBrowserMode_.show(items);
  currentMode_ = ApplicationMode::ListBrowser;
  if (!items.empty()) {
    if (directEntry) {
      // 2026-08-11: legacy's mode.c distinguishes real ^R recall
      // (T_hfgrep/T_hegrep, `row = nrow - 1`) from direct entry
      // (M_fgrep/M_locate/M_cut2, `i = -2` -- no explicit row, falling
      // back to the top) -- see promptHistoryRecallIsDirectEntry_'s
      // own comment. A real report found this landing on the LAST
      // (11th of 11) seed pattern instead of the first.
      listBrowserView_.setCursorIndex(0);
    } else {
      // Newest (the most recently pushed entry, at the back) is what's
      // actually visible/selected first -- matching the same "最下行が
      // 最新" convention ToggleHistoryView's own chronological mode
      // uses, and legacy's own mode.c (M_hist/T_hist group:
      // `pnp->row = hp->bp->nrow - 1;`).
      listBrowserView_.setCursorIndex(static_cast<int>(items.size()) - 1);
    }
    listBrowserView_.redraw();
  }
  // Outside the `!items.empty()` block on purpose: RecentFile/
  // RecentDirectory need their own drawPromptLabel()-style mode line
  // (see refreshDirectEntryModeLine()'s own comment) even with a
  // genuinely empty ring (no prior file/directory ever visited), not
  // just when there's a candidate to seed the field with.
  refreshDirectEntryModeLine();
  return true;
}

bool Application::startShellHistoryRecall() {
  if (currentMode_ != ApplicationMode::Shell) return false;
  shellHistoryRecallActive_ = true;
  shellHistoryRecallText_ = shell_.commandLine().text();
  listBrowserCategory_ = RingCategory::ShellCommand;
  listBrowserChronological_ = false;
  // See ContentView::setCursorAtModeLineTrailingEnd()'s own comment --
  // Enter simply INSERTS the selection into the shell command line
  // here (see openSelectedListEntry()'s own shellHistoryRecallActive_
  // branch), not a live-edit-the-highlighted-row paradigm, so the
  // caret belongs at the highlighted row like any other plain picker.
  listBrowserView_.setCursorAtModeLineTrailingEnd(false);
  listBrowserMode_.show(sortedUniqueRecentEntries(RingCategory::ShellCommand));
  currentMode_ = ApplicationMode::ListBrowser;
  return true;
}

std::string Application::promptLabelText(PromptKind kind) const {
  switch (kind) {
    case PromptKind::GrepPattern:
      return grepSyntax_ == GrepSyntax::Fixed ? "FGREP" : "EGREP";
    case PromptKind::CutPattern:
      return "CUT";
    case PromptKind::ManName:
      return "MAN";
    case PromptKind::ProgramCommand:
      return "PROGRAM";
    case PromptKind::LocateQuery:
      return "LOCATE";
    case PromptKind::CopyDestination:
      return "CP";
    case PromptKind::MoveDestination:
      return "MV";
    case PromptKind::RemoveConfirm:
      return "RM";
    case PromptKind::ExitConfirm:
      return "EXIT (y/n)?";
    case PromptKind::DuPath:
      return "DU";
    case PromptKind::PathInput:
      return pathInputIsDirectory_ ? "DIR" : "FILE";
    case PromptKind::ArchiveDestination:
      return "ARC";
    case PromptKind::RecentFile:
      return "FILE";
    case PromptKind::RecentDirectory:
      return "DIR";
    case PromptKind::None:
      return "";
  }
  return "";
}

void Application::drawPromptLabel(PromptKind kind, const std::string &positionText, const std::string &staticMessage) {
  int row = std::max(0, display_.rows() - 1);
  display_.moveCursor(row, 0);
  // Fill the WHOLE row with reverse video first (2026-08-18 fix, a
  // real report + screenshots =bn/=cq: "基本的に右端まで埋まって
  // いないモードラインは全部不良"). Every other already-correct
  // mode line in this port fills its own row edge-to-edge (see
  // ContentView::renderModeLine()'s own fillTrailingToEdge), but this
  // function only ever drew individual reversed RUNS (label/position/
  // trailing) separated by plain, unfilled gaps from the old
  // clearToEndOfLine() -- visible as beige "holes" on a real terminal,
  // easy to misread as broken/missing content. Filling the base layer
  // here, then drawing label/position/trailing on top (already
  // reversed, no visual change to them), guarantees no gap is ever
  // left unfilled by construction, without needing to hand-track every
  // individual gap. promptEditor_'s own redraw() (called separately,
  // right after this function returns) explicitly turns standout OFF
  // before drawing its own field (LineEditor::redraw(), ported from
  // key_eeol()'s vt_endstand()) -- it "punches" the editable field's
  // own region back to plain on top of this fill, which is exactly the
  // desired look: the live input area itself stays non-highlighted,
  // everything else on the row stays filled.
  // columns()-1, NOT columns() (2026-08-18 follow-up fix, a real
  // report: "-c起動なのに何が違うんだろう？直ってないので", root-
  // caused via a `script`-captured raw session showing byte-for-byte
  // CORRECT output that Terminal.app still didn't repaint until an
  // unrelated keypress). Same root cause as renderLine()'s own
  // 2026-08-13 fix and ContentView::renderModeLine()'s own matching
  // 2026-08-18 fix (see its own long comment): writing a Standout-
  // marked character to the terminal's true LAST column triggers
  // auto-wrap, silently moving the cursor off a row that's also
  // OUTSIDE the DECSTBM scroll region (rows 1-23) -- real terminals
  // handle that combination inconsistently, and evidently defer
  // repainting until a later, unrelated write. Leaving the true last
  // column unfilled is the same pragmatic tradeoff renderLine() and
  // renderModeLine() already made.
  display_.setAttribute(Display::Attribute::Standout);
  display_.write(std::string(std::max(0, display_.columns() - 1), ' '));
  display_.setAttribute(Display::Attribute::StandoutOff);
  // The true last column itself: erase it via clearToEndOfLine()
  // (termcap `ce`), not a printed character -- an erase operation
  // doesn't trigger auto-wrap the way WRITING a character at the last
  // column does (that's the exact mechanism this whole cap exists to
  // avoid), so this safely clears any stale content left there by a
  // previous screen without reintroducing the bug.
  if (display_.columns() > 0) {
    display_.moveCursor(row, display_.columns() - 1);
    display_.clearToEndOfLine();
  }
  display_.moveCursor(row, 0);

  if (kind == PromptKind::ExitConfirm) {
    // Ported from disp.c's own M_exit case (verified against a real
    // CUI screenshot, 2026-08-05 user report): the mode-line NAME
    // field is just "EXIT" (str.c's P_mode table: " EXIT"), reversed
    // -- but the "Exit (y/n) ? " MESSAGE text after it is NOT reversed
    // (print_line() calls vt_endstand() specifically because the
    // message starts with S_errg, "Exit"). This port previously
    // combined both into a single "EXIT (y/n)?" string, drawn entirely
    // reversed -- neither the wording nor the styling matched the CUI
    // original. The "prev" field (mode-line[6], the mode being
    // returned to) is also ported here: reversed, filled to the
    // screen's right edge, starting a couple of columns after the
    // message to leave room for the typed y/n answer.
    static const std::string kName = "EXIT";
    static const std::string kMessage = "Exit (y/n) ? ";
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : kName) display_.putChar(c);
    display_.setAttribute(Display::Attribute::StandoutOff);
    display_.putChar(' ');
    for (unsigned char c : kMessage) display_.putChar(c);
    int labelWidth = static_cast<int>(kName.size()) + 1 + static_cast<int>(kMessage.size());

    std::string prev = previousModeName(currentMode_);
    int editableWidth = std::max(1, display_.columns() - labelWidth);
    constexpr int kAnswerGap = 2;  // room for the typed y/n before the trailing field starts
    int trailingCol = labelWidth + kAnswerGap;
    if (!prev.empty() && trailingCol < display_.columns()) {
      int available = display_.columns() - trailingCol;
      std::string padded = prev;
      if (static_cast<int>(padded.size()) > available) padded = padded.substr(0, available);
      else padded += std::string(available - padded.size(), ' ');
      display_.moveCursor(row, trailingCol);
      display_.setAttribute(Display::Attribute::Standout);
      for (unsigned char c : padded) display_.putChar(c);
      display_.setAttribute(Display::Attribute::StandoutOff);
      // promptEditor_'s own redraw() clears its full field width with
      // spaces every keystroke (LineEditor::redraw()'s own comment) --
      // capping this at kAnswerGap keeps it from wiping out the
      // trailing field just drawn above.
      editableWidth = kAnswerGap;
    }
    promptEditor_.setField(row, labelWidth, editableWidth);
    return;
  }

  // RecentFile/RecentDirectory + listBrowserChronological_ (2026-08-18
  // fix, part of the same =cz/=da round): the OLD renderer this screen
  // used before refreshDirectEntryModeLine() existed (listBrowserView_'s
  // own ModeLineFieldsProvider, see its own comment) already showed
  // "HISTORY" in place of "DIR"/"FILE" while toggled to chronological
  // order (item 160's own established convention, `^R` = ToggleHistoryView).
  // drawPromptLabel() is generic across every PromptKind and has no
  // concept of listBrowserChronological_ at all, so without this it
  // silently dropped that cue the moment refreshDirectEntryModeLine()
  // took over this screen's rendering -- label stayed "DIR"/"FILE"
  // regardless of which order was actually showing.
  std::string label = ((kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory) &&
                        listBrowserChronological_)
                           ? "HISTORY"
                           : promptLabelText(kind);
  display_.setAttribute(Display::Attribute::Standout);
  for (unsigned char c : label) display_.putChar(c);
  display_.setAttribute(Display::Attribute::StandoutOff);
  int labelWidth = static_cast<int>(label.size());
  // 2026-08-18 fix: this gap (and every other one-column gap in this
  // function) used to be PURELY a `labelWidth` arithmetic bump, never
  // actually WRITTEN -- harmless before the 2026-08-18 fill-to-edge
  // fix above (clearToEndOfLine() had already left the whole row
  // plain, so an untouched column just stayed plain by default), but
  // that fill now leaves every UNTOUCHED column REVERSED instead (a
  // real report caught this: the gap between "DIR" and "1/1" was
  // itself reversed, reading as one continuous block). Explicitly
  // writing a plain space (Standout already off here) is what
  // actually punches a true gap through that fill now.
  if (!label.empty()) {
    display_.putChar(' ');
    labelWidth += 1;
  }
  // positionText (2026-08-12 fix): see this function's own declaration
  // comment. Reversed, matching the SAME "01/10 TOP" styling the
  // candidate list's own ModeLineFieldsProvider already used for it
  // one frame earlier (feedRegexListByte()'s edit-transition is the
  // only caller that passes this non-empty). moveCursor() to
  // labelWidth explicitly (rather than relying on wherever the cursor
  // happened to land after drawing `label`) so the gap-before-this-
  // field math below can't silently double-count a column the way an
  // earlier version of this fix did -- a real report + reference
  // screenshot found this list's own candidate text (e.g. "^Question")
  // landing ONE COLUMN further right once editing started than it sat
  // at during plain list-browsing (listBrowserView_'s own
  // ModeLineFieldsProvider, which draws name+" "+position+" "+trailing
  // with exactly ONE gap column after each field, not two).
  if (!positionText.empty()) {
    display_.moveCursor(row, labelWidth);
    // Split at the internal space -- ModeLine::format()'s own "1/1
    // END"/"01/10 TOP"/"42%" shape always has the fraction ("1/1")
    // and the position word ("END"/"TOP"/a percentage) as two pieces
    // joined by exactly one space (2026-08-18 fix, a real report:
    // "「DIR」「1/1」「END」の間にも隙間を入れて欲しい" -- drawing
    // positionText as a single Standout run left that internal space
    // ALSO reversed, reading as one solid block rather than two
    // separate fields the way "DIR" and "1/1 END" already are).
    // Scoped to drawPromptLabel() only (not ModeLine::format() itself,
    // which every OTHER mode line's renderModeLine() call also
    // shares) -- this doesn't touch any other screen's positionText
    // rendering. Total column math is unchanged: fraction.size() + 1
    // (this gap) + suffix.size() + 1 (gap before the next field) ==
    // positionText.size() + 1, the same total the single-run version
    // below used to advance labelWidth by.
    std::size_t splitAt = positionText.find(' ');
    std::string fraction = splitAt == std::string::npos ? positionText : positionText.substr(0, splitAt);
    std::string suffix = splitAt == std::string::npos ? std::string() : positionText.substr(splitAt + 1);
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : fraction) display_.putChar(c);
    display_.setAttribute(Display::Attribute::StandoutOff);
    labelWidth += static_cast<int>(fraction.size());
    if (!suffix.empty()) {
      display_.putChar(' ');  // explicit plain gap -- see this function's own 2026-08-18 comment above
      labelWidth += 1;
      display_.setAttribute(Display::Attribute::Standout);
      for (unsigned char c : suffix) display_.putChar(c);
      display_.setAttribute(Display::Attribute::StandoutOff);
      labelWidth += static_cast<int>(suffix.size());
    }
    display_.putChar(' ');  // explicit plain gap before the next field -- see the same comment above
    labelWidth += 1;
  }
  // staticMessage (2026-08-13): see this function's own declaration
  // comment -- draws a plain UNDERLINED status message at exactly the
  // column an editable field would otherwise start at, then returns
  // WITHOUT touching promptEditor_ at all (no field to reserve/set --
  // this isn't a real editable prompt).
  if (!staticMessage.empty()) {
    display_.moveCursor(row, labelWidth);
    display_.setAttribute(Display::Attribute::Underline);
    for (unsigned char c : staticMessage) display_.putChar(c);
    display_.setAttribute(Display::Attribute::UnderlineOff);
    return;
  }
  // Trailing reserve (2026-08-12 fix): a direct user formula --
  // "文字入力エリアの幅は、「モード名（EGREPなら5＋1）」＋「...右詰
  // 16文字（/Users/haa15030/git/usilicなら「...5030/git/usilic/」＋
  // 1）」をCOLUMNSから引いた値でいい" (the input area's width should be
  // COLUMNS minus the mode name (+1 gap) minus a right-justified,
  // "..."-truncated 16-character tail (+1 gap)) -- mirroring
  // grepView_'s own ModeLineFieldsProvider trailing2 field (the CWD
  // shown on a GREP/CUT RESULTS screen, see its own comment) but
  // reserved here uniformly for every prompt kind's own EDITING screen,
  // not just GREP/CUT's. Deliberately reserving the WIDTH only for now
  // -- nothing is actually drawn into this space yet, matching the
  // user's own explicit "それを超える文字幅入力への対処はのちほど"
  // (handling input that exceeds this width is for later): without
  // this reservation, a prompt could currently be typed all the way out
  // to the screen's actual right edge, wider than legacy's own
  // equivalent field ever was (legacy's own field always left this
  // trailing area alone for a directory-path field), so narrowing it
  // now keeps this port's own line-wrap/`need_scroll()`-equivalent
  // logic consistent with legacy's real available width even before
  // that trailing field's own CONTENT gets drawn in a later round.
  // ExitConfirm is excluded (own branch above, already reserves ITS
  // actual "prev mode" trailing field correctly).
  constexpr int kTrailingPathEllipsis = 3;  // "..."
  constexpr int kTrailingPathGap = 1;
  int trailingReserve = kTrailingPathTailChars + kTrailingPathEllipsis + kTrailingPathGap;
  int editableWidth = std::max(1, display_.columns() - labelWidth - trailingReserve);

  // Return-destination content (2026-08-12 fix; generalized 2026-08-13
  // TWICE in the same day -- see below): the reservation above used to
  // be width-only, deliberately deferring what actually gets drawn
  // there -- a real report ("FGREPで戻り先が無い", and for EGREP,
  // "モードラインでは戻り先の消える" the instant editing starts) found
  // that left the space blank, reading as "no destination at all"
  // rather than "reserved but not yet implemented".
  //
  // First generalization (still wrong, corrected below): scoped to
  // "every PromptKind with its OWN dedicated *ReturnMode_ variable,
  // shown only when that mode == Directory" -- which excluded
  // ProgramCommand on the reasoning that executeProgram() never
  // changes ApplicationMode, so "there's no return mode concept". A
  // direct user correction: that reasoning doesn't hold at all --
  // "PROGRAMを起動したモードは必ず存在するわけだから...それが戻り値に
  // なるのであってモード自身を変更するかどうかなんて何の関係もない"
  // (whatever mode PROGRAM was launched FROM necessarily still exists,
  // and IS the destination -- whether launching the prompt itself
  // happens to change currentMode_ is irrelevant). Restricting to
  // "only when the destination is Directory" was equally arbitrary --
  // legacy's own lp->prev (disp_window()'s mode-line[6]) is populated
  // identically by EVERY mode-transition function via
  // `strcpy(lp->prev, &ap->name[...]);` (m_prog()/m_grep()/m_cut()/
  // m_locate()/m_man() all share this one line), i.e. "whatever
  // ap->name (the PREVIOUS screen's own display name) was" -- not a
  // Directory-only special case.
  //
  // previousModeName(ApplicationMode) already exists and IS this
  // port's own equivalent of that generic "display name for mode X"
  // resolver (used by Shell's own shellReturnName_ and KEY's own
  // trailing field already) -- reusing it here, for the mode this
  // prompt will actually return to (or, for PromptKinds with no
  // dedicated *ReturnMode_ variable -- ProgramCommand included --
  // simply currentMode_ itself, since nothing changes it), rather than
  // hand-rolling a second, narrower Directory-only version.
  ApplicationMode destinationMode = currentMode_;
  switch (kind) {
    case PromptKind::GrepPattern: destinationMode = grepReturnMode_; break;
    case PromptKind::CutPattern: destinationMode = cutReturnMode_; break;
    case PromptKind::ManName: destinationMode = manReturnMode_; break;
    case PromptKind::LocateQuery: destinationMode = locateReturnMode_; break;
    // RecentFile/RecentDirectory (2026-08-18 fix, =cx report -- "戻り値
    // が惜しい"): unlike GrepPattern/CutPattern/ManName/LocateQuery,
    // these two have no dedicated *ReturnMode_ of their own -- so
    // destinationMode fell through to the `= currentMode_` default
    // above. That's correct once real editing has started (currentMode_
    // really is ListBrowser then, deliberately -- see this function's
    // own callers), but refreshDirectEntryModeLine() calls this function
    // WHILE STILL showing the read-only pre-typing candidate list, where
    // currentMode_ is ALREADY ListBrowser (set by startPromptHistoryRecall()
    // before its own refreshDirectEntryModeLine() call). previousModeName()
    // has no case for ListBrowser (falls to its own `default: return ""`),
    // so destinationDirectory came back empty and the whole trailing
    // field's fill-to-edge box was drawn with no text in it at all.
    // promptHistoryRecallReturnMode_ (set to currentMode_ BEFORE the
    // switch to ListBrowser, see startPromptHistoryRecall()) is exactly
    // the mode this prompt will actually return to, the same value
    // returnFromListBrowser() itself already relies on -- reusing it here
    // instead of hand-rolling a separate variable.
    case PromptKind::RecentFile:
    case PromptKind::RecentDirectory: destinationMode = promptHistoryRecallReturnMode_; break;
    default: break;
  }
  // 2026-08-18: a same-day attempt excluded RecentFile/RecentDirectory
  // here entirely (reasoning: showing the current directory here reads
  // as a near-duplicate of the same path being typed in the editable
  // field a few columns to the left). A direct user correction: the
  // actual problem was never this field's CONTENT, it was that the
  // ROW WASN'T FILLED TO THE EDGE around it ("=bn参照。基本的に右端
  // まで埋まっていないモードラインは全部不良" -- see the fill-to-edge
  // block below, which is the real fix). Reverted back to the
  // unconditional previousModeName(destinationMode) every other
  // PromptKind already uses.
  // RecentFile/RecentDirectory + listBrowserChronological_ (2026-08-18
  // fix, a direct user correction: "^X^D→^Rは戻り値DIRでないと。旧版は
  // そうなってるんで"): this trailing field's own "destination path"
  // content (the fix right above) is correct for the ORDINARY sorted
  // "DIR"/"FILE" screen, but legacy's own T_hfile/T_hdir -- and this
  // port's own OLD renderer for this exact screen before
  // refreshDirectEntryModeLine() took over (listBrowserView_'s own
  // ModeLineFieldsProvider, see its own comment: `rightWatermark =
  // listBrowserChronological_ ? promptLabelText(...) : ""`) -- show the
  // ORIGINAL category badge ("DIR"/"FILE") in this same slot instead,
  // once toggled into the chronological "HISTORY" view, disambiguating
  // WHICH ring's history this is (the left label itself just says the
  // fixed literal "HISTORY" at that point, same as every other
  // promptHistoryRecallKind_ holder's own chronological view). Restoring
  // that same badge here, in the one slot this function actually has,
  // rather than the destination path.
  bool showsBadge =
      (kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory) && listBrowserChronological_;
  std::string destinationDirectory = showsBadge ? promptLabelText(kind) : previousModeName(destinationMode);
  if (!destinationDirectory.empty()) {
    std::string tail = truncatedTrailingPath(destinationDirectory);
    // Badge (2026-08-18 fix, a direct user correction + screenshot =db:
    // "戻り値のDIRは左詰めでよろしく"): the DIR/FILE badge above is a
    // short, fixed-width category marker, not a path -- right-
    // justifying it (the destination-PATH convention every other
    // PromptKind here uses, where the tail end of a long path is the
    // meaningful part) just stranded it alone at the screen's right
    // edge with a wide gap in front of it. Left-justified instead,
    // starting at the same column promptEditor_'s own trailing reserve
    // begins (labelWidth + editableWidth, computed below) -- i.e. the
    // left edge of the reserved box, not the path-tail convention's
    // right edge.
    int col = showsBadge ? labelWidth + editableWidth
                          // columns()-1, NOT columns() (2026-08-18 fix): same
                          // auto-wrap avoidance as the base fill just above --
                          // ending this Standout run exactly at the true last
                          // column has the identical real-terminal repaint
                          // problem. The true last column itself is left to
                          // the base fill's own trailing plain-space write
                          // (see above), matching renderLine()'s/
                          // renderModeLine()'s own established "leave the
                          // very last column unmarked" tradeoff.
                          : display_.columns() - 1 - static_cast<int>(tail.size());
    if (col > labelWidth) {
      display_.moveCursor(row, col);
      display_.setAttribute(Display::Attribute::Standout);
      for (unsigned char c : tail) display_.putChar(c);
      display_.setAttribute(Display::Attribute::StandoutOff);
    }
  }

  promptEditor_.setField(row, labelWidth, editableWidth);
}

void Application::refreshDirectEntryModeLine() {
  if (currentMode_ != ApplicationMode::ListBrowser) return;
  if (!promptHistoryRecallKind_ || !promptHistoryRecallIsDirectEntry_) return;
  PromptKind kind = *promptHistoryRecallKind_;
  if (kind != PromptKind::RecentFile && kind != PromptKind::RecentDirectory) return;
  int candidateCount = static_cast<int>(listBrowserMode_.items().size());
  std::string positionText = candidateCount > 0 ? ModeLine::format(listBrowserView_.cursorIndex() + 1, candidateCount) : "";
  drawPromptLabel(kind, positionText);
  const std::string *selected = listBrowserMode_.selectedItem();
  promptEditor_.setText(selected ? *selected : "");
  promptEditor_.redraw();
}

void Application::activatePrompt(PromptKind kind, const std::string &initialText, const std::string &positionText,
                                  bool cursorAtStart) {
  promptKind_ = kind;
  // See feedPromptByte()'s own ^N/^P handling -- a non-empty
  // positionText is passed ONLY by feedRegexListByte()'s edit-
  // transition (every other activatePrompt() call site leaves it at
  // the "" default), so it doubles as "this prompt is editing a
  // candidate FROM the still-visible ^J^S/^J^C list, so ^N/^P should
  // keep cycling through it" without a separate parameter.
  promptCandidateListActive_ = !positionText.empty();
  drawPromptLabel(kind, positionText);
  promptEditor_.setText(initialText);
  if (cursorAtStart) promptEditor_.moveHome();
  promptEditor_.redraw();
}

bool Application::executeGrep(const std::string &pattern) {
  // Recorded regardless of whether the search itself finds anything
  // below -- see startPromptHistoryRecall()'s own comment on why FGREP
  // and EGREP get separate rings (matching legacy's own p.hfgrep/
  // p.hegrep split, keyed off grepSyntax_ the same way promptLabelText()
  // already picks the FGREP/EGREP mode-line label).
  history_.pushRecent(grepSyntax_ == GrepSyntax::Fixed ? RingCategory::RecentFgrepPattern
                                                        : RingCategory::RecentEgrepPattern,
                       pattern);
  // 2026-08-18 fix: was a hardcoded File-vs-Directory ternary, silently
  // reading (and, on failure, redrawing) the WRONG ContentView for
  // every other mode grep can be launched from -- a real report
  // ("^J^L'.txt'→=co→^S'.txt'→モードライン不良かつ検索してない")
  // showed FGREP started from LOCATE results actually searched
  // directoryView_'s stale content while locateView_ stayed on screen,
  // then redrew directoryView_ over it on failure, garbling the mode
  // line. viewForMode() (factored out of activeView() for exactly this)
  // picks the right view for ANY grepReturnMode_, not just File/Directory.
  ContentView &source = viewForMode(grepReturnMode_);
  std::vector<std::string> sourceLines;
  sourceLines.reserve(source.lines().size());
  for (const auto &line : source.lines()) sourceLines.push_back(line.text);

  if (!grepMode_.search(sourceLines, pattern, grepSyntax_)) {
    source.redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Grep;
  // See pendingSearchFromDirectEntryList_'s own comment -- consumed
  // (one-shot) here regardless of its value, so a later resplit/
  // typed-from-blank-prompt search doesn't inherit a stale true.
  grepReturnsToCandidateList_ = pendingSearchFromDirectEntryList_;
  pendingSearchFromDirectEntryList_ = false;
  return true;
}

bool Application::executeCut(const std::string &pattern) {
  // Recorded regardless of outcome/nesting depth below -- see
  // startPromptHistoryRecall()'s own comment for why this reuses
  // RecentFgrepPattern/RecentEgrepPattern (matching legacy's own
  // ss.p.hfgrep/hegrep reuse for CUT) rather than a dedicated ring.
  // 2026-08-05 correction: an earlier same-day revision used
  // RecentEgrepPattern UNCONDITIONALLY regardless of cutSyntax_ -- a
  // real legacy bug the user directly pointed out (menu.c's own
  // `case M_cut: case M_cut2: hp->ss.p.hegrep = save_history(...)`
  // does the exact same thing, never branching on M_cut2 (^Jc, FIXED)
  // vs M_cut (^J^C, REGEX) here despite the FGREP/EGREP block just
  // above it in the same switch demonstrating the correct pattern):
  // ^Jc's own CUT should behave like FGREP (and share ITS history),
  // ^J^C's like EGREP, otherwise a user who already knows FGREP/EGREP
  // reasonably expects CUT to follow the same fixed-vs-regex split
  // and gets confused when it doesn't. Ported here to the C++ side
  // AND back into the legacy C source itself (menu.c) -- see that
  // fix's own comment.
  history_.pushRecent(cutSyntax_ == GrepSyntax::Fixed ? RingCategory::RecentFgrepPattern
                                                       : RingCategory::RecentEgrepPattern,
                       pattern);
  bool ok;
  std::string fileSourcePath = std::move(cutFileSourcePath_);
  cutFileSourcePath_.clear();  // consumed exactly once, regardless of which branch below runs or its outcome

  if (currentMode_ == ApplicationMode::CutContent || currentMode_ == ApplicationMode::Cut) {
    // Nested resplit -- cutOrigin_ untouched, see class comment. Both
    // "already viewing a selected segment's own content" (CutContent)
    // and "still on the label LIST itself, cursor on one entry"
    // (Cut) land here identically, see startCutPrompt()'s own
    // 2026-08-05 comment for why the latter was missing.
    ok = cutMode_.splitSelected(pattern, cutSyntax_);
  } else if (!fileSourcePath.empty()) {
    // K-35/K-36's "regular file" variant: load the file first (same
    // as opening it normally would), THEN split its now-current
    // buffer -- see startCutFilePrompt()'s own comment. Checked BEFORE
    // the plain Directory branch just below: startCutFilePrompt() is
    // ALSO invoked with currentMode_ still Directory (its own prompt
    // never changes currentMode_ either), so fileSourcePath being
    // non-empty is what actually distinguishes "split THIS selected
    // file" from "split the listing itself" -- both are reachable from
    // the exact same currentMode_.
    if (!fileMode_.loadFile(fileSourcePath)) {
      activeView().redraw();
      return false;
    }
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
    // 2026-08-18 fix: set BEFORE calling split(), not after checking
    // its return value -- see executeStartLog()'s own discovery
    // comment for why (CutMode::split() redraws cutView_ INTERNALLY,
    // before this function ever gets a chance to run; setting
    // cutOrigin_ afterward left the mode line showing whatever the
    // PREVIOUS cutOrigin_ happened to be for this entire redraw,
    // self-correcting only on some LATER, unrelated redraw). Harmless
    // to set even on eventual failure -- every failure path inside
    // split() returns before its own redraw(), so nothing ever renders
    // using a "wasted" assignment here.
    cutOrigin_ = CutOrigin::File;
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  } else if (currentMode_ == ApplicationMode::Directory) {
    // 2026-08-11: CUT run directly over the directory listing (^J^C
    // from Directory mode, see startCutPrompt()'s own comment) --
    // mirrors GrepMode::search() reading directoryView_.lines() when
    // grepReturnMode_ == Directory, the same "the listing itself is
    // the buffer" idea. Directory listing lines never carry
    // overstrike attributes (see ContentView::Line::attributes' own
    // comment), so `.attributes` here is always empty -- passed
    // through anyway only for structural symmetry with the other
    // branches.
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(directoryView_.lines().size());
    for (const auto &line : directoryView_.lines()) sourceLines.push_back({line.text, line.attributes});
    cutOrigin_ = CutOrigin::Directory;  // see the File branch's own 2026-08-18 comment above
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  } else {
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
    // A fresh top-level split -- eligible for reflow on resize. See the
    // File branch's own 2026-08-18 comment above for the ordering.
    cutOrigin_ = CutOrigin::File;
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  }

  if (!ok) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Cut;
  // See pendingSearchFromDirectEntryList_'s own comment -- consumed
  // (one-shot) here regardless of its value, so a later resplit/
  // typed-from-blank-prompt search doesn't inherit a stale true.
  cutReturnsToCandidateList_ = pendingSearchFromDirectEntryList_;
  pendingSearchFromDirectEntryList_ = false;
  return true;
}

bool Application::executeMan(const std::string &name) {
  history_.pushRecent(RingCategory::RecentManPage, name);  // recorded regardless of loadPage()'s own outcome below
  if (!manMode_.loadPage(name)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Man;
  return true;
}

// Shared by executeProgram() and setProgramLauncher()'s own lambda
// (2026-08-17 addition, factored out when the latter was added): a
// strncmp-prefix match of some PROGRAM directive entry's own first
// word (find_prog(), sub.c's own semantics).
bool Application::isRecognizedProgram(const std::string &firstWord) const {
  for (const auto &seed : configuration_.programSeedCommands()) {
    std::size_t seedEnd = seed.find_first_of(" \t");
    std::string seedFirstWord = seed.substr(0, seedEnd);
    if (!seedFirstWord.empty() && firstWord.compare(0, seedFirstWord.size(), seedFirstWord) == 0) return true;
  }
  return false;
}

// Shared by executeProgram() and setProgramLauncher()'s own lambda:
// usilic itself never chdir()s (see executePathInput()'s own comment),
// so a command referencing a bare/relative filename only resolves
// correctly if the actual child process is `cd`'d into the right
// directory first -- neither ChildProcess::runInteractive() (a fresh
// fork/exec) nor sh -c inherits anything from directoryMode_'s or
// Shell's own purely-logical "current directory" bookkeeping. POSIX
// single-quote escaping (close the quote, emit an escaped literal
// quote, reopen) handles a directory path containing an embedded "'"
// correctly; a plain '<dir>' wrap alone already handles the far more
// common case (spaces). `dir` empty is a no-op (command unchanged) --
// callers with no directory context to resolve against (e.g.
// startProgramPrompt() found no file) leave the process cwd as-is,
// the pre-existing behavior.
std::string Application::cdWrap(const std::string &dir, const std::string &command) {
  if (dir.empty()) return command;
  std::string quoted = "'";
  for (char c : dir) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return "cd " + quoted + " && " + command;
}

// Ported from mode.c's M_prog case (see isRecognizedProgram()'s own
// comment). Runs the whole typed line via ProgramMode::run() when
// recognized; ApplicationMode never changes either way (see class
// comment).
bool Application::executeProgram(const std::string &command) {
  std::size_t start = command.find_first_not_of(" \t");
  if (start == std::string::npos) {
    activeView().redraw();
    return false;
  }
  std::size_t end = command.find_first_of(" \t", start);
  std::string firstWord = command.substr(start, end == std::string::npos ? std::string::npos : end - start);

  if (!isRecognizedProgram(firstWord)) {
    activeView().redraw();
    return false;
  }

  // programPromptDirectory_: see its own declaration comment --
  // startProgramPrompt()'s bare-filename pre-fill (e.g. "CLAUDE.md")
  // needs this cd wrap; empty (a no-op) whenever no file context was
  // found, matching the pre-existing behavior for a hand-typed command.
  std::string toRun = cdWrap(programPromptDirectory_, command);
  programPromptDirectory_.clear();  // consumed exactly once, regardless of outcome

  display_.flush();
  programMode_.run(toRun, command);
  activeView().redraw();
  return true;
}

// 2026-08-13 (docs/ideas/IDEAS.md's LOCATE section, CLAUDE.md history
// item 288): if `query` is exactly the previous entry in the ring
// (i.e. the user submitted startLocatePrompt()'s seed unchanged),
// `locate` is NOT re-run -- LocateMode's own results_/view_ from the
// prior search are simply still there, so switching currentMode_
// back to Locate redisplays them as-is. Checked BEFORE pushRecent()
// (which would otherwise move the ring's back() to `query` first and
// make this always look "unchanged"). A changed query runs a normal
// fresh search, same as before this change.
bool Application::executeLocate(const std::string &query) {
  const std::deque<std::string> &recent = history_.recentEntries(RingCategory::RecentLocateQuery);
  bool unchanged = !recent.empty() && recent.back() == query;
  history_.pushRecent(RingCategory::RecentLocateQuery, query);  // recorded regardless of search()'s own outcome below
  if (unchanged) {
    currentMode_ = ApplicationMode::Locate;
    // search() normally redraws locateView_ itself at the end of a
    // real run (locate_mode.cpp); skipping search() here means this
    // is the only redraw the newly-active view gets.
    locateView_.redraw();
    return true;
  }
  if (!locateMode_.search(query)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Locate;
  return true;
}

// Ported from m_dest()'s T_cp/T_mv branches (destination validation
// simplified away -- CpMvRmMode's own cp/mv invocation surfaces
// failures via its ChildProcess::Result instead, see class comment).
// Reloads the directory listing afterward either way, so the screen
// reflects what actually happened on disk (this also clears marks,
// via DirectoryMode::enterDirectory()'s own documented behavior).
bool Application::executeCopy(const std::string &destination) {
  bool interactive = cpMvRmInteractive_;
  cpMvRmInteractive_ = false;  // consumed exactly once, regardless of outcome below

  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    directoryView_.redraw();
    return false;
  }
  // K-51 ("with SHELL window"): runs interactively, with the real
  // command's own output visible live on the real terminal -- see
  // CpMvRmMode::copyToInteractive()'s own comment. K-52 (the default):
  // captured silently, matching the pre-existing behavior.
  bool ok = interactive ? cpMvRmMode_.copyToInteractive(targets, destination) == 0
                        : cpMvRmMode_.copyTo(targets, destination).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

bool Application::executeMove(const std::string &destination) {
  bool interactive = cpMvRmInteractive_;
  cpMvRmInteractive_ = false;

  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    directoryView_.redraw();
    return false;
  }
  bool ok = interactive ? cpMvRmMode_.moveToInteractive(targets, destination) == 0
                        : cpMvRmMode_.moveTo(targets, destination).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

// Ported from m_dest()'s T_rm branch: `confirmation` must be exactly
// "y" (matching legacy's `*ap->mess != 'y'` check) or nothing is
// removed.
bool Application::executeRemove(const std::string &confirmation) {
  bool interactive = cpMvRmInteractive_;
  cpMvRmInteractive_ = false;

  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || confirmation != "y") {
    directoryView_.redraw();
    return false;
  }
  bool ok = interactive ? cpMvRmMode_.removeInteractive(targets) == 0 : cpMvRmMode_.remove(targets).ok;
  directoryMode_.enterDirectory(directoryMode_.currentDirectory());
  return ok;
}

// Ported from m_cr()'s M_exit case: `if (*rvp->str == 'y') return
// -T_exit;` -- confirmation must be exactly "y" (same simplification
// as executeRemove()'s first-character-only legacy check). Sets
// wantsExit_ rather than doing anything itself; nothing about
// currentMode_/the screen changes either way.
bool Application::executeExit(const std::string &confirmation) {
  if (confirmation != "y") {
    activeView().redraw();
    return false;
  }
  wantsExit_ = true;
  return true;
}

// Ported from mode.c's T_du case: runs `du` on `path`. False for an
// empty path or a failed `du` invocation.
bool Application::executeDu(const std::string &path) {
  if (path.empty() || !duMode_.loadUsage(path)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Du;
  return true;
}

// K-57/K-58: see startArchivePrompt()'s own comment for the two entry
// points this re-derives at submit time (currentMode_ can't have
// changed while the prompt was up).
bool Application::executeArchiveExtract(const std::string &destination) {
  bool interactive = archiveExtractInteractive_;
  archiveExtractInteractive_ = false;  // consumed exactly once, regardless of outcome below

  std::string archivePath;
  ArchiveKind kind = ArchiveKind::None;
  std::vector<std::string> members;

  if (currentMode_ == ApplicationMode::Archive) {
    archivePath = archiveMode_.currentArchive();
    kind = archiveMode_.archiveKind();
    for (const auto &entry : archiveMode_.targetEntries()) members.push_back(entry.name);
  } else if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry) {
      activeView().redraw();
      return false;
    }
    archivePath = directoryMode_.currentDirectory();
    if (!archivePath.empty() && archivePath.back() != '/') archivePath += '/';
    archivePath += entry->name;
    configuration_.programForSuffix(archivePath, &kind);
    // members left empty -- the whole archive, per HELP.jp 2.11's own
    // File-selector example.
  }

  std::vector<std::string> argv = buildArchiveExtractArgv(archivePath, kind, members, destination);
  if (argv.empty() || destination.empty()) {
    activeView().redraw();
    return false;
  }

  // K-57 ("with SHELL window"): runs interactively, with the real
  // tar/unzip output visible live on the real terminal -- same
  // ChildProcess::runInteractive() shape CP/MV/RM's own ^X^Kc/^X^Km/
  // ^X^Kr already use (see CpMvRmMode::runInteractive()'s own
  // comment on why this class, despite its name, is what actually
  // owns the KeyInput reference this needs). K-58 (the default):
  // captured silently.
  bool ok = interactive ? cpMvRmMode_.runInteractive(argv) == 0 : cpMvRmMode_.run(argv).ok;
  activeView().redraw();
  return ok;
}

// Ported from mode.c's M_lfile/M_ldir handling: resolves the typed
// path (~ expansion, ./ and ../ cleanup, see PathResolver's own class
// comment) and lands in Directory or File mode depending on what it
// actually is. Deliberately simpler than openSelectedEntry(): no
// EXEC-suffix or archive-format auto-detection here (legacy's own
// M_lfile2 doesn't do that either -- typing a path is "load this as
// plain text/a listing," not "open it the smart way an existing
// listing ENTRY would be," which is Enter's own, separate job). False
// for an empty, nonexistent, or unreadable path.
bool Application::executePathInput(const std::string &path) {
  if (path.empty()) {
    activeView().redraw();
    return false;
  }
  // Resolved relative to directoryMode_'s own currentDirectory() (the
  // directory last BROWSED, which stays put even while looking at a
  // File/Man/etc screen opened from it -- see PathResolver::resolve()'s
  // own baseDirectory comment for why this can't be the process's own
  // cwd: usilic never chdir()s). Matches Shell's own Tab-completion,
  // which already resolves against shell_.currentDirectory() for the
  // same reason.
  ResolvedPath resolved = pathResolver_.resolve(path, directoryMode_.currentDirectory());
  if (!resolved.exists) {
    activeView().redraw();
    return false;
  }
  if (resolved.isDirectory) {
    if (!directoryMode_.enterDirectory(resolved.path)) {
      activeView().redraw();
      return false;
    }
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  if (!fileMode_.loadFile(resolved.path)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // see its own declaration comment
  return true;
}

// K-65 (^I, M_list): see the class's own declaration comment.
std::vector<std::string> Application::completeFilesystemPath(const std::string &partial,
                                                               const std::string &baseDirectory) const {
  std::string dirPart, prefix;
  std::size_t slash = partial.find_last_of('/');
  if (slash == std::string::npos) {
    prefix = partial;
  } else {
    dirPart = partial.substr(0, slash + 1);
    prefix = partial.substr(slash + 1);
  }

  std::string searchDir;
  if (!dirPart.empty() && (dirPart.front() == '/' || dirPart.front() == '~')) {
    searchDir = pathResolver_.resolve(dirPart).path;
  } else {
    searchDir = baseDirectory;
    if (!searchDir.empty() && searchDir.back() != '/') searchDir += '/';
    searchDir += dirPart;
  }

  std::vector<std::string> results;
  std::error_code ec;
  std::filesystem::directory_iterator it(searchDir, ec);
  if (ec) return results;
  for (const auto &entry : it) {
    std::string name = entry.path().filename().string();
    if (name.compare(0, prefix.size(), prefix) != 0) continue;
    bool isDir = entry.is_directory(ec);
    results.push_back(dirPart + name + (isDir ? "/" : ""));
  }
  return results;
}

// Ported from menu.c's M_edit case (^Je) -- see class comment for why
// this is neither prompt-driven nor mode-switching, and for the
// deliberate simplification on line-number: Directory-mode edits
// always open at line 1 (do_edit()'s own `pnp->line` argument is the
// cursor's row within the DIRECTORY LISTING in that context, not a
// meaningful line number inside the file about to be opened -- not
// carried over here), File-mode edits use the real cursor line.
bool Application::executeEdit() {
  std::string path;
  int lineNumber = 1;

  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry) return false;
    if (entry->isDirectory) {
      // Ported from legacy do_edit()'s S_errv ("Invalid mode") case --
      // a real user request specifically asked for this, rather than
      // the previous silent no-op, when ^Je is pressed on a directory
      // entry. See DirectoryMode::reportEditError()'s own comment.
      directoryMode_.reportEditError();
      directoryView_.redraw();
      return true;
    }
    path = directoryMode_.currentDirectory();
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry->name;
  } else if (currentMode_ == ApplicationMode::File) {
    path = fileMode_.currentFile();
    lineNumber = fileView_.cursorIndex() + 1;
  } else {
    return false;  // out of scope for this slice, see class comment
  }

  bool changed = editMode_.edit(path, lineNumber);
  if (changed && currentMode_ == ApplicationMode::File) fileMode_.loadFile(path);
  activeView().redraw();
  return true;
}

std::vector<StyledLine> Application::wrapHelpText(int width) const {
  PipeBuffer pipe(encoding_, width, 16L * 1024 * 1024);
  pipe.feed(helpText_);
  pipe.flush();
  return pipe.lines();  // .attributes carried straight through -- see cutMode_.split()'s own comment
}

bool Application::loadHelpText(const std::string &text) {
  helpText_ = text;
  return !helpText_.empty();
}

// Ported from menu.c's M_help/T_help case -- see class comment for
// why this is a CutMode split (real CUT and HELP share one instance)
// rather than its own display mechanism. Reachable from any mode
// (legacy's init_bp() default case has no mode-context guard for
// M_help/T_help either).
bool Application::startHelp() {
  if (helpText_.empty()) return false;
  std::vector<StyledLine> helpLines = wrapHelpText(display_.columns());

  bool preserveExistingCut = (currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent);
  bool ok;
  if (preserveExistingCut) {
    // A real CUT session is already showing -- push it (the SAME
    // mechanism a nested splitSelected() resplit uses) so ^G out of
    // HELP resumes it via Back's existing canGoBack()/goBack() check.
    // Deliberately does NOT touch cutSyntax_/cutOrigin_ here: both are
    // single shared fields (not saved/restored per ModeStack level),
    // so leaving them alone preserves whatever the real cut being
    // preserved already had -- setting them for HELP's own benefit
    // would corrupt that real cut's own state once ^G restores it
    // (e.g. a Fixed-string real cut resuming with cutSyntax_ left at
    // Regex from HELP would re-split wrong on a later resize).
    ok = cutMode_.pushSplit(helpLines, kHelpPattern, GrepSyntax::Regex);
  } else {
    cutReturnMode_ = currentMode_;  // nothing to preserve -- same "leave Cut back to..." anchor a real CUT uses
    // A fresh top-level split -- eligible for reflow on resize. Set
    // BEFORE calling split(), not after checking its return value --
    // see executeCut()'s own File-branch 2026-08-18 comment for why
    // (CutMode::split() redraws cutView_ INTERNALLY, before this
    // function ever gets a chance to run; a real report -- LOG's own
    // mode line briefly reading "CUT" instead of "LOG" -- traced back
    // to this exact ordering bug, which turned out to affect every
    // OTHER cutOrigin_ assignment in this file too, HELP's own
    // included).
    cutOrigin_ = CutOrigin::Help;
    cutSyntax_ = GrepSyntax::Regex;  // safe to set here: nothing real depends on the old value in this branch
    ok = cutMode_.split(helpLines, kHelpPattern, GrepSyntax::Regex);
  }
  if (!ok) return false;

  currentMode_ = ApplicationMode::Cut;
  return true;
}

// Ported from menu.c's T_winch handling -- see class comment for the
// three-way split this preserves (plain viewport resize for Directory/
// Grep/Locate/Archive/Du/ListBrowser, reflow() for File/Man and a
// TOP-LEVEL Cut/CutContent split, Shell's pty resized unconditionally)
// and why File/Man's own reflow() isn't a literal alloc_winch() port.
void Application::handleResize() {
  layoutViews();
  if (currentMode_ == ApplicationMode::File) fileMode_.reflow(display_.columns());
  if (currentMode_ == ApplicationMode::Man) manMode_.reflow(display_.columns());
  if ((currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent) &&
      !cutMode_.canGoBack()) {
    std::vector<StyledLine> freshSource;
    if (cutOrigin_ == CutOrigin::Help) {
      freshSource = wrapHelpText(display_.columns());
    } else if (cutOrigin_ == CutOrigin::Directory) {
      // 2026-08-11: directory listing lines don't reflow/word-wrap
      // with width the way File/Man text does (DirectoryMode has no
      // reflow() of its own -- `ls -alF` output is truncated at
      // render time, not re-fetched), so there's nothing to call here
      // analogous to fileMode_.reflow() below; directoryView_'s
      // already-current lines are simply re-split as-is.
      freshSource.reserve(directoryView_.lines().size());
      for (const auto &line : directoryView_.lines()) freshSource.push_back({line.text, line.attributes});
    } else {
      fileMode_.reflow(display_.columns());
      freshSource.reserve(fileView_.lines().size());
      for (const auto &line : fileView_.lines()) freshSource.push_back({line.text, line.attributes});
    }
    int savedIndex = cutView_.cursorIndex();
    bool wasContent = (currentMode_ == ApplicationMode::CutContent);
    if (cutMode_.split(freshSource, cutMode_.pattern(), cutSyntax_)) {
      int last = static_cast<int>(cutMode_.segments().size()) - 1;
      if (savedIndex < 0) savedIndex = 0;
      if (savedIndex > last) savedIndex = (last < 0) ? 0 : last;
      cutView_.setCursorIndex(savedIndex);
      if (wasContent) openSelectedSegment();  // re-populate cutContentView_ with the (hopefully same) segment
    }
  }
  shell_.onResize(display_.rows(), display_.columns());
  activeView().redraw();
  if (promptActive()) promptEditor_.redraw();
}

bool Application::wantsExit() const { return wantsExit_; }
void Application::requestImmediateExit() { wantsExit_ = true; }

bool Application::consumeGuiModeToggleRequest() {
  bool requested = guiModeToggleRequested_;
  guiModeToggleRequested_ = false;
  return requested;
}

// Ported from IDEAS.md's own GUI-mode click description (not legacy) --
// see class comment for the click-to-select/click-again-to-open
// convention. Directory-only for a long time (see class comment) until
// 2026-08-14: a direct user request ("マウスの左クリックでも選択可
// （←(ls)のように）を導入すれば解消") to also cover LOCATE results the
// same way, offered specifically as the fix for a separate report (a
// hovered-but-unclicked row's middle-ellipsis truncation width flicker,
// see ContentView::renderLine()'s own ellipsisBudget comment) --
// letting a click commit to a real selection immediately, the same way
// Directory's own listing already does, rather than the pointer
// merely hovering (and un-hovering) rows as it passes over them.
bool Application::handleMouseClick(int row, int column) {
  if (currentMode_ == ApplicationMode::Directory) {
    int idx = directoryView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    // Outside this entry's own highlighted text -- DirectoryMode's `ls`
    // listing has a fixed left margin (permissions/size/date) before the
    // highlight starts, and a short filename can leave a variable-width
    // empty tail after it -- so this press was never actually "on" the
    // entry visually. Ignored here (returns false, as if nothing were
    // under the pointer at all) so it's free to also serve as a "cat's
    // paw" screen-drag starting point (see ContentView::
    // isColumnWithinLineScope()'s own comment for the full 2026-08-05
    // user report this replaces a timing/motion-based heuristic with).
    if (!directoryView_.isColumnWithinLineScope(idx, column)) return false;
    if (idx == directoryView_.cursorIndex()) return openSelectedEntry();
    directoryView_.setCursorIndex(idx);
    directoryView_.redraw();
    return true;
  }
  if (currentMode_ == ApplicationMode::Locate) {
    int idx = locateView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    // LOCATE's own results are plain, unattributed lines (no
    // highlightStartColumn -- see LocateMode::search()'s own Line
    // construction), so isColumnWithinLineScope()'s left-margin skip
    // is a no-op here (start defaults to 0) -- the whole rendered row
    // is clickable, matching how the entire path is the "entry", not
    // just a filename substring within a fixed-format `ls -l` row.
    if (!locateView_.isColumnWithinLineScope(idx, column)) return false;
    if (idx == locateView_.cursorIndex()) return openSelectedLocateResult();
    locateView_.setCursorIndex(idx);
    locateView_.redraw();
    return true;
  }
  return false;
}

bool Application::promptActive() const { return promptKind_ != PromptKind::None; }
const std::string &Application::promptText() const { return promptEditor_.text(); }

ApplicationMode Application::currentMode() const { return currentMode_; }
Charset::Encoding Application::encoding() const { return encoding_; }

bool Application::awaitingSequenceOrPromptByte() const {
  return escPending_ || leaderJPending_ || promptKind_ != PromptKind::None;
}
DirectoryMode &Application::directoryMode() { return directoryMode_; }
FileMode &Application::fileMode() { return fileMode_; }
GrepMode &Application::grepMode() { return grepMode_; }
CutMode &Application::cutMode() { return cutMode_; }
ManMode &Application::manMode() { return manMode_; }
ProgramMode &Application::programMode() { return programMode_; }
LocateMode &Application::locateMode() { return locateMode_; }
ArchiveMode &Application::archiveMode() { return archiveMode_; }
DuMode &Application::duMode() { return duMode_; }
ListBrowserMode &Application::listBrowserMode() { return listBrowserMode_; }
Shell &Application::shell() { return shell_; }
CpMvRmMode &Application::cpMvRmMode() { return cpMvRmMode_; }
EditMode &Application::editMode() { return editMode_; }
ExecMode &Application::execMode() { return execMode_; }
ContentView &Application::activeContentView() { return activeView(); }

}  // namespace fm
