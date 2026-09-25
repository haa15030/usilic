#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "configuration.hpp"
#include "content_view.hpp"
#include "doctest.h"
#include "file_mode.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Charset;
using fm::Configuration;
using fm::FileMode;
using fm::History;
using fm::Display;
using fm::isMarkdownFile;
using fm::looksLikeRoffDocument;
using fm::PositionCategory;
using fm::test::RecordingDisplay;

namespace {

fs::path makeTempDir() {
  char tmpl[] = "/tmp/filemode_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

struct Fixture {
  fs::path root;
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  History history;
  Configuration configuration;
  FileMode mode{view, history, configuration, Charset::Encoding::Ascii, 80};

  Fixture() : root(makeTempDir()) {}
  ~Fixture() { fs::remove_all(root); }

  std::string writeFile(const std::string &name, const std::string &content) {
    std::string path = (root / name).string();
    std::ofstream(path, std::ios::binary) << content;
    return path;
  }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "loadFile: a plain text file's lines are displayed") {
  std::string path = writeFile("plain.txt", "hello\nworld\n");
  REQUIRE(mode.loadFile(path));
  CHECK(mode.currentFile() == path);
  REQUIRE(view.lines().size() == 2);
  CHECK(view.lines()[0].text == "hello");
  CHECK(view.lines()[1].text == "world");
}

TEST_CASE_FIXTURE(Fixture, "loadFile: nroff-style overstrike bold (\"X\\bX\") carries through as an "
                           "AttributeRun on the resulting Line, not just de-overstruck plain text "
                           "(2026-08-05: PipeBuffer already detected this, FileMode was the one place "
                           "that discarded it before it ever reached the screen)") {
  std::string path = writeFile("bold.txt", "plain B\bBO\bOL\bLD\bD plain\n");
  REQUIRE(mode.loadFile(path));
  REQUIRE(view.lines().size() == 1);
  CHECK(view.lines()[0].text == "plain BOLD plain");
  // One AttributeRun PER overstruck character (nroff emits "X\bX" once
  // per bolded character, not once per bolded WORD -- PipeBuffer's own
  // wrapAttrs_.push_back() doesn't merge adjacent same-attribute runs,
  // confirmed by reading its source directly rather than assumed), so
  // "BOLD" is 4 separate 1-column runs at columns 6,7,8,9 -- not one
  // run spanning [6,10).
  REQUIRE(view.lines()[0].attributes.size() == 4);
  for (int i = 0; i < 4; i++) {
    CHECK(view.lines()[0].attributes[i].attribute == fm::Display::Attribute::Bold);
    CHECK(view.lines()[0].attributes[i].start == 6 + i);
    CHECK(view.lines()[0].attributes[i].end == 7 + i);
  }
}

TEST_CASE_FIXTURE(Fixture, "loadFile: a nonexistent file fails cleanly") {
  CHECK_FALSE(mode.loadFile((root / "does-not-exist.txt").string()));
}

TEST_CASE_FIXTURE(Fixture, "loadFile: shows a real display cursor at the current line, since plain file "
                            "content has no whole-line highlight of its own (2026-08-04 user request)") {
  view.setViewport(0, 10);
  std::string path = writeFile("plain.txt", "line0\nline1\nline2\n");
  REQUIRE(mode.loadFile(path));
  CHECK_FALSE(view.cursorHighlightEnabled());  // ported behavior, unchanged by this request
  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == view.cursorDisplayRow());
  CHECK(display.cursorCol() == 0);
}

TEST_CASE_FIXTURE(Fixture, "reflow: keeps the visible cursor in sync with the (possibly re-clamped) cursor "
                            "line after a width change") {
  view.setViewport(0, 10);
  std::string path = writeFile("plain.txt", "line0\nline1\nline2\n");
  REQUIRE(mode.loadFile(path));
  view.setCursorIndex(2);
  REQUIRE(mode.reflow(40));
  CHECK_FALSE(display.visibleCursorHidden());
  CHECK(display.cursorRow() == view.cursorDisplayRow());
}

TEST_CASE_FIXTURE(Fixture, "loadFile: long lines wrap at the configured width, via PipeBuffer") {
  fm::ContentView narrowView{display};
  History h2;
  FileMode narrowMode{narrowView, h2, configuration, Charset::Encoding::Ascii, 10};
  std::string path = writeFile("long.txt", "abcdefghijklmno\n");

  REQUIRE(narrowMode.loadFile(path));
  REQUIRE(narrowView.lines().size() == 2);
  CHECK(narrowView.lines()[0].text == "abcdefghij");
  CHECK(narrowView.lines()[1].text == "klmno");
}

TEST_CASE_FIXTURE(Fixture, "loadFile: UTF-8 content round-trips without corruption") {
  std::string path = writeFile("utf8.txt", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n");  // "日本語\n"
  fm::ContentView utf8View{display};
  History h2;
  FileMode utf8Mode{utf8View, h2, configuration, Charset::Encoding::Utf8, 80};

  REQUIRE(utf8Mode.loadFile(path));
  REQUIRE(utf8View.lines().size() == 1);
  CHECK(utf8View.lines()[0].text == "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
}

TEST_CASE_FIXTURE(Fixture, "saveCursorPosition / loadFile: cursor position round-trips through History") {
  std::string path = writeFile("multi.txt", "line1\nline2\nline3\nline4\n");
  REQUIRE(mode.loadFile(path));
  view.setCursorIndex(2);
  mode.saveCursorPosition();

  auto saved = history.positionFor(PositionCategory::File, path);
  REQUIRE(saved.has_value());
  CHECK(saved->row == 2);

  // Re-loading the same file (as if returning to it later) restores
  // the cursor instead of resetting to the top.
  view.setCursorIndex(0);
  REQUIRE(mode.loadFile(path));
  CHECK(view.cursorIndex() == 2);
}

TEST_CASE_FIXTURE(Fixture, "loadFile: an empty file produces no lines and doesn't crash") {
  std::string path = writeFile("empty.txt", "");
  REQUIRE(mode.loadFile(path));
  CHECK(view.lines().empty());
}

TEST_CASE_FIXTURE(Fixture, "saveCursorPosition before any loadFile is a safe no-op") {
  FileMode fresh{view, history, configuration, Charset::Encoding::Ascii, 80};
  fresh.saveCursorPosition();  // must not crash
  CHECK(history.positionFor(PositionCategory::File, "").has_value() == false);
}

TEST_CASE_FIXTURE(Fixture, "reflow: re-wraps the current file at the new width") {
  std::string path = writeFile("long.txt", "abcdefghijklmno\n");
  REQUIRE(mode.loadFile(path));
  REQUIRE(view.lines().size() == 1);  // fits in 80 columns unwrapped

  REQUIRE(mode.reflow(10));
  REQUIRE(view.lines().size() == 2);
  CHECK(view.lines()[0].text == "abcdefghij");
  CHECK(view.lines()[1].text == "klmno");

  REQUIRE(mode.reflow(80));
  REQUIRE(view.lines().size() == 1);
  CHECK(view.lines()[0].text == "abcdefghijklmno");
}

TEST_CASE_FIXTURE(Fixture, "reflow: preserves the CURRENT session cursor, not a History-saved one") {
  std::string path = writeFile("multi.txt", "line1\nline2\nline3\nline4\n");
  REQUIRE(mode.loadFile(path));
  // A stale saved position elsewhere in History that reflow() must
  // NOT jump to -- the whole point is preserving THIS session's
  // in-progress scroll position across a resize, unlike loadFile().
  History::CursorPosition stale;
  stale.row = 0;
  history.setPosition(PositionCategory::File, path, stale);

  view.setCursorIndex(3);
  REQUIRE(mode.reflow(80));
  CHECK(view.cursorIndex() == 3);
}

TEST_CASE_FIXTURE(Fixture, "reflow: clamps the cursor if the new width produces fewer lines") {
  std::string path = writeFile("wrap.txt", "abcdefghijklmno\n");
  fm::ContentView narrowView{display};
  History h2;
  FileMode narrowMode{narrowView, h2, configuration, Charset::Encoding::Ascii, 10};
  REQUIRE(narrowMode.loadFile(path));
  REQUIRE(narrowView.lines().size() == 2);
  narrowView.setCursorIndex(1);

  REQUIRE(narrowMode.reflow(80));  // now fits on one line
  REQUIRE(narrowView.lines().size() == 1);
  CHECK(narrowView.cursorIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "reflow: a no-op (false) when no file is currently loaded") {
  FileMode fresh{view, history, configuration, Charset::Encoding::Ascii, 80};
  CHECK_FALSE(fresh.reflow(40));
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: shows the bare filename, byte size, and directory") {
  std::string path = writeFile("plain.txt", "hello\nworld\n");  // 12 bytes
  REQUIRE(mode.loadFile(path));

  // Ported from disp_window()'s pager case: unlike Directory mode, the
  // name field here is the bare filename (NOT the full path). The byte
  // size and directory are two SEPARATE ModeLineFields::trailing/
  // trailing2 segments (2026-08-10, a real request/pixel comparison:
  // they need their own independent reverse-video highlight runs, like
  // every other mode-line field pair, not one unbroken run) -- as
  // plain text this still reads as "<byte size> <directory>/" with a
  // single space, the same gap width as name/position get, just now
  // via two separately-highlighted segments instead of one string with
  // extra spaces baked in. The byte size itself stays a plain,
  // ungrouped digit run (a separate, earlier misreading of this same
  // request already established and reverted that).
  std::string text = view.modeLineText();
  CHECK(text.rfind("plain.txt ", 0) == 0);
  std::string expectedTrailing = "12 " + root.string() + "/";
  CHECK(text.substr(text.size() - expectedTrailing.size()) == expectedTrailing);
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: before any loadFile, no fields are shown") {
  FileMode fresh{view, history, configuration, Charset::Encoding::Ascii, 80};
  CHECK(view.modeLineText() == "0/0 END");
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: directory trailing2 uses the full available width when it "
                           "overflows, not legacy's fixed 16-char tail cap") {
  // 2026-08-16 fix: a real report/screenshot (=bk vs =bl, ^X^F on a
  // deeply-nested real path) found this port's own tail truncated to
  // exactly 16 characters ("...39537/Libraries/") while the real CUI
  // showed a far longer tail using whatever room was actually left on
  // the row -- the 16-char cap was only ever meant for a typed search
  // STRING's own fixed-width preview area (DirectoryMode's
  // searchAnnotation_), not a PATH like this one. See
  // ContentView::TrailingKind's own comment (trailing2Kind=Path).
  fs::path deep = root / "some" / "really" / "deeply" / "nested" / "Directory-x86_64-11.1.9.1039537" / "Libraries";
  fs::create_directories(deep);
  std::string path = (deep / "BDF_Driver_License.txt").string();
  { std::ofstream(path) << "x"; }
  REQUIRE(mode.loadFile(path));
  std::string text = view.modeLineText();
  // Still overflows an 80-column row (the temp root alone plus this
  // deep chain is well over 80 characters), so it's still truncated --
  // just with the FULL available tail, not capped at 16. "Libraries/"
  // (10 chars) is well past the old cap's own reach ("39537/Libraries/",
  // the last 16 characters of the path) but comfortably within budget.
  CHECK(text.find("Directory-x86_64-11.1.9.1039537/Libraries/") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "constructing FileMode disables the cursor-line highlight on its view") {
  view.setViewport(0, 5);  // otherwise height_ defaults to 0 and nothing but the mode-line row renders
  std::string path = writeFile("plain.txt", "hello\nworld\n");
  REQUIRE(mode.loadFile(path));
  view.redraw();
  CHECK(display.standoutAt(0, 0) == false);  // cursor is on line 0 -- real CUI has no highlight here
}

// -- looksLikeRoffDocument() / auto-groff-render (2026-09-20 addition) --
//
// Direct user request ("usilic -m src/QUICK.txt > .../usilic.man...
// HELPは同ファイルを「C-j b」でCUTする"): legacy's documented behavior
// (HELP.jp: "特定のsuffixを持たなくてもマニュアルページなどのmsマクロ
// 形式で書かれている通常ファイルはgroff(1)を起動して整形します") had
// never been ported -- ConfiguredRole::Groff existed in Configuration
// but was never actually invoked anywhere. Ported from old/src/code.c's
// is_man(), scanning the first 0x1000 bytes line-by-line for a
// troff-macro-looking line.

TEST_CASE("looksLikeRoffDocument: a `.TH`-led man page is detected") {
  CHECK(looksLikeRoffDocument(".TH USILIC 1\n.SH NAME\nusilic\n"));
}

TEST_CASE("looksLikeRoffDocument: a `.\\\"` comment line alone is enough") {
  CHECK(looksLikeRoffDocument(".\\\" just a comment\nplain text follows\n"));
}

TEST_CASE("looksLikeRoffDocument: `.B `/`.I ` (single-letter macros followed by a space) count too") {
  CHECK(looksLikeRoffDocument(".B bold word\n"));
  CHECK(looksLikeRoffDocument(".I italic word\n"));
}

TEST_CASE("looksLikeRoffDocument: plain prose with no leading-dot lines is NOT roff") {
  CHECK_FALSE(looksLikeRoffDocument("hello\nworld\nno dots here\n"));
}

TEST_CASE("looksLikeRoffDocument: a dot-led line that's clearly not a 1-2 letter macro name (a real "
          "filename fragment, 3+ letters before the next boundary) is NOT mistaken for roff") {
  CHECK_FALSE(looksLikeRoffDocument("何かの説明。\n.gitignore\n"));
}

// Faithful to legacy's own is_man() (deliberately loose: ANY leading-dot
// line whose 2nd/3rd bytes are letters and 4th byte is a space or
// newline counts, false positives included -- this port doesn't try to
// out-guess it).
TEST_CASE("looksLikeRoffDocument: even a short, plausible-looking 2-letter run after the dot counts, "
          "matching legacy's own permissive heuristic") {
  CHECK(looksLikeRoffDocument(".ok\n"));
}

TEST_CASE("looksLikeRoffDocument: only scans the first 0x1000 bytes, matching legacy's N_man") {
  std::string padding(0x1000, 'x');
  CHECK_FALSE(looksLikeRoffDocument(padding + "\n.TH LATE 1\n"));
}

TEST_CASE_FIXTURE(Fixture, "loadFile: a `.TH`-led file is auto-rendered through groff before display, "
                           "instead of showing the raw troff source") {
  std::string path = writeFile("usilic.man", ".TH USILIC 1\n.SH NAME\nusilic \\- test\n");
  if (!mode.loadFile(path)) {
    MESSAGE("groff not installed here -- skipping");
    return;
  }
  bool sawRawMacro = false, sawRenderedHeading = false;
  for (const auto &line : view.lines()) {
    if (line.text.find(".TH") != std::string::npos) sawRawMacro = true;
    if (line.text.find("NAME") != std::string::npos) sawRenderedHeading = true;
  }
  CHECK_FALSE(sawRawMacro);  // the raw ".TH USILIC 1" macro line must not leak through verbatim
  CHECK(sawRenderedHeading);  // groff's own rendered "NAME" section heading should be there instead
}

// 2026-09-15追加、直接のユーザー依頼("usilic.1を表示の際のエスケープ
// シーケンスの反映もよろしく"): groffの-Tutf8出力デバイスは太字/
// 下線をANSI SGR("\x1b[1m"..."\x1b[0m"/"\x1b[4m"..."\x1b[24m")で表現
// する(legacy由来の"X\bX"/"_\bX"重ね打ち規約とは別物)。
// ConfiguredRole::Groffの起動コマンドへ`env GROFF_NO_SGR=1`を挟む
// ことで、groff自身に重ね打ち規約での出力を強制する――この修正が
// 効いていれば、(a)どの行のtextにも生のESC(0x1b)バイトが一切残ら
// ず、(b)"NAME"見出し全体が実際にBold属性のAttributeRunとして
// マークされているはず。
TEST_CASE_FIXTURE(Fixture, "loadFile: groff's own bold/underline output arrives as AttributeRuns (via "
                           "GROFF_NO_SGR-forced legacy overstrike), not raw ANSI escape bytes") {
  std::string path = writeFile("usilic.man", ".TH USILIC 1\n.SH NAME\nusilic \\- test\n");
  if (!mode.loadFile(path)) {
    MESSAGE("groff not installed here -- skipping");
    return;
  }
  bool sawEscapeByte = false;
  bool sawBoldNameRun = false;
  for (const auto &line : view.lines()) {
    if (line.text.find('\x1b') != std::string::npos) sawEscapeByte = true;
    std::size_t namePos = line.text.find("NAME");
    if (namePos == std::string::npos) continue;
    // legacyの重ね打ち規約に倣い、1文字ごとに別々のAttributeRunが
    // 積まれる(既存のnroff overstrike検出と同じ粒度)ので、"NAME"の
    // 4バイト全てが(複数の隣接するRunにまたがっていても構わず)
    // Boldで覆われているかを1バイトずつ確認する。
    bool allBold = true;
    for (int pos = static_cast<int>(namePos); pos < static_cast<int>(namePos) + 4; pos++) {
      bool covered = false;
      for (const auto &run : line.attributes) {
        if (run.attribute == fm::Display::Attribute::Bold && run.start <= pos && pos < run.end) covered = true;
      }
      if (!covered) allBold = false;
    }
    if (allBold) sawBoldNameRun = true;
  }
  CHECK_FALSE(sawEscapeByte);
  CHECK(sawBoldNameRun);
}

TEST_CASE_FIXTURE(Fixture, "loadFile: a plain (non-roff) file is unaffected -- no groff invocation, no "
                           "content change") {
  std::string path = writeFile("plain2.txt", "just some ordinary text\nwith a . in it\n");
  REQUIRE(mode.loadFile(path));
  REQUIRE(view.lines().size() == 2);
  CHECK(view.lines()[0].text == "just some ordinary text");
  CHECK(view.lines()[1].text == "with a . in it");
}

// 2026-09-15追加、直接のユーザー指摘("~/.usilic/saved/usilic.1に
// ついて、現状では^O^M/^[^M/^Mが皆おなじなのでちゃんと機能を反映
// して...^[^Mは「as is」（msマクロ）になってない"): loadFileRaw()
// (rawControlCodesMode_)は、legacy自身のM_code(^[^M、fp->cr=0が
// check_ext()自体を呼ばない)に倣い、HELP-markup展開/groff整形の
// どちらもスキップして生バイトのまま見せるべきだった――修正前は
// loadFile()と全く同じ変換済み内容を表示してしまっていた。
TEST_CASE_FIXTURE(Fixture, "loadFileRaw: a `.TH`-led file is shown completely as-is -- bypasses the "
                           "auto-groff-render loadFile() applies, matching legacy's M_code (fp->cr=0 "
                           "never reaching check_ext())") {
  std::string path = writeFile("usilic.man", ".TH USILIC 1\n.SH NAME\nusilic \\- test\n");
  REQUIRE(mode.loadFileRaw(path));
  REQUIRE(view.lines().size() == 3);
  CHECK(view.lines()[0].text == ".TH USILIC 1");
  CHECK(view.lines()[1].text == ".SH NAME");
  CHECK(view.lines()[2].text == "usilic \\- test");
}

// 2026-09-16追加、直接のユーザー報告("debian-keyring...tar.xz(75MB)
// の^[^Mが^Gで中断不能")+提案("一回の読み込み分が終わったらそこを
// 通り、^Gが投入されていたら中断"): loadFileRaw()にshouldAbortを渡すと
// readAndWrapChunked()(ファイルをkChunkBytes単位で読みPipeBuffer::
// feed()、各チャンクの後にshouldAbort()を呼ぶ)経由になる。ここでは
// shouldAbortが常にfalseを返すケースを検証し、読み込み結果が
// (呼び出し元がshouldAbortを渡さない)既存のreadAndWrap()経路と
// 完全に同じ内容になること、およびshouldAbort自体が実際に呼ばれる
// ことを確認する――実ファイルが8MBのチャンク境界を超えるケース
// (中断時に一部だけが残る場合)は、テストスイートの実行時間の都合上
// ここでは検証していない(実機での確認が必要、既知の制約)。
TEST_CASE_FIXTURE(Fixture, "loadFileRaw with a shouldAbort callback that never aborts: goes through "
                           "readAndWrapChunked() but produces identical content to the plain (no "
                           "callback) readAndWrap() path, and the callback is actually invoked") {
  std::string path = writeFile("usilic.man", ".TH USILIC 1\n.SH NAME\nusilic \\- test\n");
  int abortCheckCalls = 0;
  auto shouldAbort = [&]() {
    abortCheckCalls++;
    return false;
  };
  REQUIRE(mode.loadFileRaw(path, shouldAbort));
  REQUIRE(view.lines().size() == 3);
  CHECK(view.lines()[0].text == ".TH USILIC 1");
  CHECK(view.lines()[1].text == ".SH NAME");
  CHECK(view.lines()[2].text == "usilic \\- test");
  CHECK(abortCheckCalls >= 1);
}

// 同上のユーザー指摘、こちらは"^O^Mで(stdin)形式になっておらず"の
// 方。loadFileAsPipe()(K-23)は、legacy自身のM_fp(fp->cr=2)がplain
// Enter(fp->cr=1)と同じくcheck_ext()を通る(=groff整形はloadFile()と
// 揃う)一方、read_buff()自体はm_pipe()経由でパイプ越しに行われる、
// という2点セットの移植。
TEST_CASE_FIXTURE(Fixture, "loadFileAsPipe: a `.TH`-led file IS groff-rendered (same content as "
                           "loadFile(), unlike loadFileRaw()), but the mode-line shows \"(stdin)\" like "
                           "a real STDIN-pager session") {
  std::string path = writeFile("usilic.man", ".TH USILIC 1\n.SH NAME\nusilic \\- test\n");
  if (!mode.loadFileAsPipe(path)) {
    MESSAGE("groff not installed here -- skipping");
    return;
  }
  // loadStream()自身のコメント参照: 初回はわざと少ない行数しか読まない
  // (生きたSHELLパイプのペーシングに合わせた設計)ので、ここでは
  // ensureLoadedThrough()で全文(既にtempファイルへ書き出し済みの
  // 有限な内容)を強制的に読み切ってから中身を検証する。
  mode.ensureLoadedThrough(1000, /*bypassPacing=*/true);
  bool sawRawMacro = false, sawRenderedHeading = false;
  for (const auto &line : view.lines()) {
    if (line.text.find(".TH") != std::string::npos) sawRawMacro = true;
    if (line.text.find("NAME") != std::string::npos) sawRenderedHeading = true;
  }
  CHECK_FALSE(sawRawMacro);
  CHECK(sawRenderedHeading);
  CHECK(view.modeLineText().find("(stdin)") != std::string::npos);
}

// 2026-09-21、直接のユーザー判断: 旧版のM_fp(^O^M)は折り返しを止めない(パイプ越しに読むだけ)ので、
// 長い行は通常の表示と同様に画面幅で折り返し、全文字が見える(以前は切り詰めていた)。
TEST_CASE_FIXTURE(Fixture, "loadFileAsPipe: long lines ARE wrapped at the display width, like legacy's M_fp") {
  std::string longLine(200, 'x');
  std::string path = writeFile("long.txt", longLine + "\n");
  REQUIRE(mode.loadFileAsPipe(path));
  REQUIRE(view.lines().size() > 1);
  std::string joined;
  for (const auto &l : view.lines()) joined += l.text;
  CHECK(joined == longLine);  // 全文字が残る(欠けない)
}

// 2026-09-15追加、直接のユーザー指摘("バッファへの読み込みが、最初に
// 一括ではなく１バイトずつなのが（stdin）...「1バイト」で検索してみて"、
// HISTORY.md:14857参照): loadFileAsPipe()はloadStream()を経由するので、
// ensureLoadedThrough()自身の既存の(SHELL経由の生きたハンドオフに
// 限らず全てのストリームへ一般化済みの、そちら自身のコメント参照)
// 「現在+1」ペーシングをそのまま受け継ぐ――内容を丸ごと一括で
// view_へ流し込むわけではない。ここではファイル全体(5行)を渡した
// 直後の時点でまだ1行しか見えていないこと、その後1回のensure
// LoadedThrough()ごとにちょうど1行ずつしか増えないことを確認する。
TEST_CASE_FIXTURE(Fixture, "loadFileAsPipe: content is paced in one line at a time via "
                           "ensureLoadedThrough(), not materialized into the view all at once") {
  std::string path = writeFile("multi5.txt", "line1\nline2\nline3\nline4\nline5\n");
  REQUIRE(mode.loadFileAsPipe(path));
  REQUIRE(view.lines().size() == 1);  // not all 5 lines yet, despite the whole file already being on disk
  CHECK(view.lines()[0].text == "line1");

  REQUIRE(mode.ensureLoadedThrough(50, /*bypassPacing=*/false));
  REQUIRE(view.lines().size() == 2);  // exactly one more line, not a jump to the end
  CHECK(view.lines()[1].text == "line2");

  REQUIRE(mode.ensureLoadedThrough(50, /*bypassPacing=*/false));
  REQUIRE(view.lines().size() == 3);
  CHECK(view.lines()[2].text == "line3");
}

// -- isMarkdownFile() / auto-markdown-render (2026-09-22 addition) --
//
// Direct user request ("せっかく.mdフォーマットで書かれているので
// usilicに文字装飾機能を実装してほしい。ベタで読みたければ^[^M
// ということで"): .md/.markdownファイルは、loadFile()(通常の^M)経由では
// markdownToHelpMarkup()+expandHelpMarkup()で装飾され、loadFileRaw()
// (^[^M、rawControlCodesMode_)経由では変換をスキップして生のMarkdown
// 記法のまま見える。

TEST_CASE("isMarkdownFile: recognizes .md/.markdown (case-insensitive), not other extensions") {
  CHECK(isMarkdownFile("/tmp/README.md"));
  CHECK(isMarkdownFile("/tmp/README.MD"));
  CHECK(isMarkdownFile("/tmp/doc.markdown"));
  CHECK_FALSE(isMarkdownFile("/tmp/README.txt"));
  CHECK_FALSE(isMarkdownFile("/tmp/README"));
}

TEST_CASE_FIXTURE(Fixture, "loadFile: a .md file's headings/bold are rendered as AttributeRuns "
                           "via loadAndTransformContent()'s markdown branch") {
  std::string path = writeFile("README.md", "# Title\n\n**bold** word\n");
  REQUIRE(mode.loadFile(path));
  REQUIRE(view.lines().size() >= 2);
  CHECK(view.lines()[0].text == "Title");
  bool sawUnderline = false;
  for (const auto &run : view.lines()[0].attributes)
    if (run.attribute == Display::Attribute::Underline) sawUnderline = true;
  CHECK(sawUnderline);
  bool sawBold = false;
  for (const auto &line : view.lines())
    for (const auto &run : line.attributes)
      if (run.attribute == Display::Attribute::Bold) sawBold = true;
  CHECK(sawBold);
}

TEST_CASE_FIXTURE(Fixture, "loadFileRaw (^[^M): a .md file is shown with its literal Markdown "
                           "syntax, NOT decorated (rawControlCodesMode_ bypasses the markdown "
                           "transform, matching isHelpMarkupPagerFile()'s own established "
                           "as-is behavior)") {
  std::string path = writeFile("README.md", "# Title\n\n**bold** word\n");
  REQUIRE(mode.loadFileRaw(path));
  bool sawLiteralHash = false;
  for (const auto &line : view.lines())
    if (line.text.find("# Title") != std::string::npos) sawLiteralHash = true;
  CHECK(sawLiteralHash);
}

// FileMode::binaryTypeText()自身のコメント参照(2026-09-25、usilicrcのCODE)。
TEST_CASE_FIXTURE(Fixture, "loadFile: with CODE set, a file holding a non-allowed control byte shows \"(^X) <file type>\" "
                           "instead of its content") {
  REQUIRE(configuration.loadRcText("CODE\n 8 9 10 12 13 27\n", true));
  std::string path = writeFile("bin.dat", std::string("abc\x01" "def\n", 8));
  REQUIRE(mode.loadFile(path));
  REQUIRE(view.lines().size() == 1);
  CHECK(view.lines()[0].text.rfind("(^A) ", 0) == 0);
  CHECK_FALSE(view.lines()[0].attributes.empty());  // 太字
}

TEST_CASE_FIXTURE(Fixture, "loadFile: allowed control bytes (and an empty CODE) keep a file as text") {
  std::string path = writeFile("ff.txt", "page1\fpage2\n");
  REQUIRE(mode.loadFile(path));  // CODE未設定: 判定しない
  CHECK(view.lines()[0].text.find("page1") != std::string::npos);
  REQUIRE(configuration.loadRcText("CODE\n 8 9 10 12 13 27\n", true));
  REQUIRE(mode.loadFile(path));  // \f(12)は許可されている
  CHECK(view.lines()[0].text.find("page1") != std::string::npos);
}

// FileMode::readAndWrap()の中断のコメント参照(2026-09-25、"^Mは中断できないの？")。
TEST_CASE_FIXTURE(Fixture, "loadFile with shouldAbort: stops between chunks and keeps only the lines read so far") {
  std::string content;
  for (int i = 0; i < 60000; i++) content += "line " + std::to_string(i) + " ----------------------------------\n";
  std::string path = writeFile("big.txt", content);  // 約2.6MB = 3チャンク程度
  REQUIRE(mode.loadFile(path, [] { return true; }));
  CHECK(view.lines().size() > 0);
  CHECK(view.lines().size() < 60000);
  REQUIRE(mode.loadFile(path));
  CHECK(view.lines().size() == 60000);
}
