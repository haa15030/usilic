#include <fstream>
#include <sstream>

#include "configuration.hpp"
#include "doctest.h"

using fm::ArchiveKind;
using fm::Charset;
using fm::Configuration;
using fm::ConfiguredRole;

TEST_CASE("built-in defaults: role-based commands are populated without loading any rc text") {
  Configuration config;
  CHECK(config.commandFor(ConfiguredRole::Ls).argv == std::vector<std::string>{"ls", "-alF"});
  CHECK(config.commandFor(ConfiguredRole::Man).argv == std::vector<std::string>{"man"});
  // 2026-09-05変更(直接のユーザー依頼、"fgrepとegrepの代わりにgrep -F
  // とgrep -Eで代用"): configuration.cpp自身のコメント参照。
  CHECK(config.commandFor(ConfiguredRole::Egrep).argv == std::vector<std::string>{"grep", "-Ene"});
  CHECK(config.commandFor(ConfiguredRole::Fgrep).argv == std::vector<std::string>{"grep", "-Fne"});
  CHECK(config.commandFor(ConfiguredRole::Grep).argv == std::vector<std::string>{"grep", "-ne"});
  CHECK_FALSE(config.commandFor(ConfiguredRole::Du).argv.empty());
}

// 2026-08-13 (CLAUDE.md history item 296): a direct user correction --
// "updatedbの機能を果たすプログラムがあるのになぜスコープ外なの？" --
// this role runs whatever program fulfills the updatedb FUNCTION on
// the current platform, not literally the string "updatedb". macOS
// has no bare `updatedb` on PATH at all (see configuration.cpp's own
// #ifdef __APPLE__ comment).
TEST_CASE("built-in defaults: LOCATE's UpdateDb role uses the platform-appropriate program") {
  Configuration config;
#ifdef __APPLE__
  CHECK(config.commandFor(ConfiguredRole::UpdateDb).argv == std::vector<std::string>{"/usr/libexec/locate.updatedb"});
#else
  CHECK(config.commandFor(ConfiguredRole::UpdateDb).argv == std::vector<std::string>{"updatedb"});
#endif
}

// 2026-08-22 addition (docs/CHANGES.md item 406): unwired by default
// (every other test in this file constructs a bare Configuration and
// never calls this setter) -- LocateUserDb/LocateUserDbBuild simply
// stay unconfigured, matching shellArgv()'s own "caller resolves the
// environment detail" boundary.
TEST_CASE("built-in defaults: LocateUserDb/LocateUserDbBuild stay unconfigured until "
          "setUserLocateDatabasePath() is called") {
  Configuration config;
  CHECK(config.commandFor(ConfiguredRole::LocateUserDb).argv.empty());
  CHECK(config.commandFor(ConfiguredRole::LocateUserDbBuild).argv.empty());
}

TEST_CASE("setUserLocateDatabasePath: wires LocateUserDb to `locate -d <path>` and LocateUserDbBuild to a "
          "find+locate.mklocatedb pipeline writing to <path> -- macOS only") {
  Configuration config;
  config.setUserLocateDatabasePath("/tmp/example/locate.db");
#ifdef __APPLE__
  CHECK(config.commandFor(ConfiguredRole::LocateUserDb).argv ==
        std::vector<std::string>{"locate", "-d", "/tmp/example/locate.db"});
  std::vector<std::string> buildArgv = config.commandFor(ConfiguredRole::LocateUserDbBuild).argv;
  REQUIRE(buildArgv.size() == 3);
  CHECK(buildArgv[0] == "sh");
  CHECK(buildArgv[1] == "-c");
  CHECK(buildArgv[2].find("/usr/libexec/locate.mklocatedb") != std::string::npos);
  CHECK(buildArgv[2].find("/tmp/example/locate.db") != std::string::npos);
  CHECK(buildArgv[2].find("chmod 600") != std::string::npos);
#else
  CHECK(config.commandFor(ConfiguredRole::LocateUserDb).argv.empty());
  CHECK(config.commandFor(ConfiguredRole::LocateUserDbBuild).argv.empty());
#endif
}

TEST_CASE("built-in defaults: the ARCHIVE table's compiled-in suffixes resolve without any rc text") {
  Configuration config;
  ArchiveKind kind = ArchiveKind::None;
  auto tar = config.programForSuffix("backup.tar", &kind);
  REQUIRE(tar.has_value());
  CHECK(tar->argv == std::vector<std::string>{"tar", "tvf"});
  CHECK_FALSE(tar->pipedFromArgv.has_value());
  CHECK(kind == ArchiveKind::Tar);

  auto tgz = config.programForSuffix("backup.tgz", &kind);
  REQUIRE(tgz.has_value());
  CHECK(tgz->argv == std::vector<std::string>{"tar", "tvf", "-"});
  REQUIRE(tgz->pipedFromArgv.has_value());
  CHECK(*tgz->pipedFromArgv == std::vector<std::string>{"gzip", "-dc"});

  auto zip = config.programForSuffix("archive.zip", &kind);
  REQUIRE(zip.has_value());
  CHECK(kind == ArchiveKind::Zip);

  CHECK_FALSE(config.programForSuffix("plain.txt").has_value());
}

TEST_CASE("programForSuffix: prefers the longest matching suffix") {
  Configuration config;
  ArchiveKind kind = ArchiveKind::None;
  auto tarbz2 = config.programForSuffix("backup.tar.bz2", &kind);
  REQUIRE(tarbz2.has_value());
  REQUIRE(tarbz2->pipedFromArgv.has_value());
  CHECK(*tarbz2->pipedFromArgv == std::vector<std::string>{"bzip2", "-dc"});  // not the plainer ".tar" entry
}

TEST_CASE("FILE/EXEC directives: quoted and bare tokens, and EXEC sets isExec") {
  Configuration config;
  std::string rc =
      "FILE\n"
      " '.pdf' 'xpdf' '-remote'\n"
      "EXEC\n"
      " .html firefox\n";
  REQUIRE(config.loadRcText(rc, true));

  auto pdf = config.programForSuffix("report.pdf");
  REQUIRE(pdf.has_value());
  CHECK(pdf->argv == std::vector<std::string>{"xpdf", "-remote"});
  CHECK_FALSE(config.isExec("report.pdf"));

  auto html = config.programForSuffix("index.html");
  REQUIRE(html.has_value());
  CHECK(html->argv == std::vector<std::string>{"firefox"});
  CHECK(config.isExec("index.html"));
}

// 2026-08-24 fix (docs/CHANGES.md item 417): a real user's own
// ~/.usilic/usilicrc (and this repo's own shipped src/usilicrc
// template) separates directive fields with actual TAB characters,
// not spaces -- unlike every fixture string above, which (like every
// other test in this file) used plain spaces and so never exercised
// this. tokenize() only treated ' ' as a delimiter, so a tab-only
// line collapsed into ONE token (embedded tabs and all), silently
// failing parseSuffixLine()'s `size() < 2` guard -- the entire
// directive vanished with no error. Caught via a direct user report:
// an EXEC entry for `.mp4` (`open -a IINA`) never fired at all.
TEST_CASE("FILE/EXEC directives: TAB-separated fields (usilicrc's real, actual format) parse "
          "identically to space-separated ones") {
  Configuration config;
  std::string rc =
      "FILE\n"
      "\t.pdf\txpdf\t-remote\n"
      "EXEC\n"
      "\t.mp4\topen\t-a\tIINA\n";
  REQUIRE(config.loadRcText(rc, true));

  auto pdf = config.programForSuffix("report.pdf");
  REQUIRE(pdf.has_value());
  CHECK(pdf->argv == std::vector<std::string>{"xpdf", "-remote"});

  auto mp4 = config.programForSuffix("movie.mp4");
  REQUIRE(mp4.has_value());
  CHECK(mp4->argv == std::vector<std::string>{"open", "-a", "IINA"});
  CHECK(config.isExec("movie.mp4"));
}

TEST_CASE("a usilicrc suffix entry wins over a built-in one of equal length") {
  Configuration config;
  REQUIRE(config.loadRcText("FILE\n .zip myunzipper -v\n", true));
  auto zip = config.programForSuffix("archive.zip");
  REQUIRE(zip.has_value());
  CHECK(zip->argv == std::vector<std::string>{"myunzipper", "-v"});
}

TEST_CASE("ARCHIVE directive: aliases a new suffix onto an existing built-in command") {
  Configuration config;
  // Alias ".tzst" onto whatever the built-in ".tgz" entry uses. Per
  // conf_arc()'s real matching (searchText must be a PREFIX of the
  // built-in's whole "<suffix> <command...>" line, verified against
  // the extracted legacy source -- not a fragment of the command),
  // the search text is the existing suffix itself, ".tgz".
  REQUIRE(config.loadRcText("ARCHIVE\n .tzst .tgz\n", true));
  ArchiveKind kind = ArchiveKind::None;
  auto aliased = config.programForSuffix("thing.tzst", &kind);
  REQUIRE(aliased.has_value());
  CHECK(aliased->argv == std::vector<std::string>{"tar", "tvf", "-"});
  REQUIRE(aliased->pipedFromArgv.has_value());
  CHECK(*aliased->pipedFromArgv == std::vector<std::string>{"gzip", "-dc"});
  CHECK(kind == ArchiveKind::Tar);
}

TEST_CASE("ARCHIVE directive: a search text matching no built-in entry is silently ignored") {
  Configuration config;
  REQUIRE(config.loadRcText("ARCHIVE\n .foo nonexistent-tool\n", true));
  CHECK_FALSE(config.programForSuffix("thing.foo").has_value());
}

TEST_CASE("EDITOR/SHELL directives build the full launch argv") {
  Configuration config;
  REQUIRE(config.loadRcText("EDITOR\n vi\nSHELL\n bash\n", true));
  CHECK(config.editorArgv("/tmp/file.txt", 42) ==
        std::vector<std::string>{"vi", "+42", "/tmp/file.txt"});
  CHECK(config.shellArgv("") == std::vector<std::string>{"bash"});
}

// Configuration::shellArgv()自身の宣言コメント参照(2026-09-25)。
TEST_CASE("shellArgv: $SHELL if it is bash/zsh/tcsh/csh, else usilicrc's SHELL, else bash") {
  Configuration config;
  CHECK(config.shellArgv("/bin/zsh") == std::vector<std::string>{"/bin/zsh"});
  CHECK(config.shellArgv("/usr/local/bin/fish") == std::vector<std::string>{"bash"});
  CHECK(config.shellArgv("") == std::vector<std::string>{"bash"});
  REQUIRE(config.loadRcText("SHELL\n\t/bin/tcsh\n", true));
  CHECK(config.shellArgv("/bin/bash") == std::vector<std::string>{"/bin/bash"});
  CHECK(config.shellArgv("/usr/local/bin/fish") == std::vector<std::string>{"/bin/tcsh"});
}

TEST_CASE("CODE directive: only byte values under 32 are accepted, clamped to 2 digits each") {
  Configuration config;
  REQUIRE(config.loadRcText("CODE\n 27 12 999\n", true));
  const std::string &allowed = config.allowedControlBytes();
  CHECK(allowed.find(static_cast<char>(27)) != std::string::npos);
  CHECK(allowed.find(static_cast<char>(12)) != std::string::npos);
  // "999" splits into two-digit groups "99" and "9" -- both >= 32, rejected.
  CHECK(allowed.find(static_cast<char>(99)) == std::string::npos);
}

TEST_CASE("BUFFER/HISTORY/WAIT/SCROLL/LS/COLUMN: single-integer directives") {
  Configuration config;
  REQUIRE(config.loadRcText(
      "BUFFER\n 5\n"
      "HISTORY\n 300\n"
      "WAIT\n 7\n"
      "SCROLL\n 1\n"
      "LS\n 2\n"
      "COLUMN\n 12\n",
      true));
  CHECK(config.bufferSizeLimitBytes() == 5 * 1024 * 1024);
  CHECK(config.historyRetentionCount() == 300);
  CHECK(config.waitIntervalMicros() == 70000);
  CHECK(config.smoothScrollEnabled() == true);
  CHECK(config.lsMode() == 2);
  CHECK(config.columnThreshold() == 12);
}

// 2026-09-12追加、直接のユーザー依頼(columnThreshold()自身の
// コメント参照): デフォルト値8自体は暫定("今後の試用による見極めは
// 必要")なので、usilicrc自身にCOLUMNディレクティブが無い場合にこの
// 値が使われることを確認する。
TEST_CASE("COLUMN directive: defaults to 8 when absent from usilicrc") {
  Configuration config;
  REQUIRE(config.loadRcText("", true));
  CHECK(config.columnThreshold() == 8);
}

TEST_CASE("BUFFER: an out-of-range value falls back to the default 64 and is recorded as an error") {
  Configuration config;
  REQUIRE(config.loadRcText("BUFFER\n 500\n", true));
  CHECK(config.bufferSizeLimitBytes() == 64 * 1024 * 1024);
  CHECK_FALSE(config.errors().empty());
}

TEST_CASE("GREP directive: parsed as a no-op for backward-compat with existing usilicrc files; "
          "PROGRAM directive: only in file-manager mode") {
  // 2026-09-11修正: 以前このセクションはConfiguration::grepSeedPatterns()
  // (EGREP/CUT-Regexの直接入力の候補一覧の種)を検証していたが、その
  // 候補一覧自体がstartGrepPrompt()自身のコメント参照の統一により
  // 廃止されたため、grepSeedPatterns()ごと削除した。GREP:ディレクティブ
  // 自体はdirectiveKeywords()に残したまま(既存のusilicrcファイルに
  // このセクションが残っていてもエラーにしないため)なので、ここでは
  // それがエラーを出さずに読み飛ばされることだけを確認する。
  Configuration config;
  std::string rc =
      "GREP\n"
      " 'TODO'\n"
      " unquoted-pattern\n"
      "PROGRAM\n"
      " ls -la | grep foo\n";
  REQUIRE(config.loadRcText(rc, true));
  CHECK(config.errors().empty());
  REQUIRE(config.programSeedCommands().size() == 1);
  CHECK(config.programSeedCommands()[0] == "ls -la | grep foo");

  Configuration pagerConfig;
  REQUIRE(pagerConfig.loadRcText(rc, /*fileManagerMode=*/false));
  CHECK(pagerConfig.programSeedCommands().empty());
}

TEST_CASE("CP/MV/RM directives override the default command, only in file-manager mode") {
  Configuration config;
  REQUIRE(config.loadRcText("CP\n cp -vRi --reflink=auto\n", true));
  CHECK(config.commandFor(ConfiguredRole::Cp).argv ==
        std::vector<std::string>{"cp", "-vRi", "--reflink=auto"});

  Configuration pagerConfig;
  auto before = pagerConfig.commandFor(ConfiguredRole::Cp).argv;
  REQUIRE(pagerConfig.loadRcText("CP\n cp --something-else\n", /*fileManagerMode=*/false));
  CHECK(pagerConfig.commandFor(ConfiguredRole::Cp).argv == before);  // unaffected outside file-manager mode
}

TEST_CASE("KEY: legacy-format lines (no F-n/K-n head, or an F-n with no keys) are ignored without error") {
  Configuration config;
  std::string rc =
      "KEY\n"
      "F-0\n"
      "^[OP\n"
      "K-?\n"
      "\"Up\"       K-a\n"
      "CODE\n"
      " 27\n";
  REQUIRE(config.loadRcText(rc, true));
  CHECK(config.errors().empty());
  CHECK(config.allowedControlBytes().find(static_cast<char>(27)) != std::string::npos);
}

TEST_CASE("comments and blank lines are ignored between and within sections") {
  Configuration config;
  std::string rc =
      "# a leading comment\n"
      "\n"
      "FILE\n"
      "# comment inside the section\n"
      "\n"
      " .txt cat   # trailing comment\n"
      "\n"
      "EXEC\n"
      " .sh sh\n";
  REQUIRE(config.loadRcText(rc, true));
  auto txt = config.programForSuffix("readme.txt");
  REQUIRE(txt.has_value());
  CHECK(txt->argv == std::vector<std::string>{"cat"});
  CHECK(config.isExec("run.sh"));
}

TEST_CASE("an unrecognized directive keyword is a fatal parse error") {
  Configuration config;
  CHECK_FALSE(config.loadRcText("BOGUS\nsomething\n", true));
  CHECK_FALSE(config.errors().empty());
}

TEST_CASE("charset bootstrap: unset environment falls back to the documented UTF-8 default") {
  Configuration config;
  CHECK_FALSE(config.charsetExplicitlySet());
  CHECK(config.defaultCharset() == Charset::Encoding::Utf8);
}

TEST_CASE("charset bootstrap: an explicitly-exported-but-empty value is still 'explicitly set', "
          "and still resolves to UTF-8 rather than the legacy EUC fallback (CLAUDE.md history item 13)") {
  Configuration config;
  config.setEnvironmentCharset("");
  CHECK(config.charsetExplicitlySet());
  CHECK(config.defaultCharset() == Charset::Encoding::Utf8);
}

TEST_CASE("charset bootstrap: an unrecognized explicit value also falls back to UTF-8, not EUC") {
  Configuration config;
  config.setEnvironmentCharset("bogus-charset-name");
  CHECK(config.charsetExplicitlySet());
  CHECK(config.defaultCharset() == Charset::Encoding::Utf8);
}

TEST_CASE("charset bootstrap: a recognized explicit value is honored") {
  Configuration config;
  config.setEnvironmentCharset("euc");
  CHECK(config.charsetExplicitlySet());
  CHECK(config.defaultCharset() == Charset::Encoding::Euc);
}

// 2026-09-20追加、直接のユーザー報告("SHELLプロンプトでman ls[return]
// ではその場でmanページが表示され、[return]連打でスクロールするみたい
// だが、旧版のように(stdin)で表示して欲しい"): Shell::setProgramLauncher()
// (application.cpp)経由のハンドオフは、typedコマンドの最初の単語が
// Configuration::programSeedCommands()(usilicrc自身のPROGRAM節)の
// どれかと前方一致する時にだけ発動する――`less`は元々このリストに
// 含まれていたが、`man`はそのpagerとして`less`を内部で使うだけで、
// `man`自身は含まれておらず、SHELLプロンプトで直接`man ls`と打つと
// 認識されずに素のptyパススルー(Shell自身の行指向のscrollback
// パイプラインが生のフルスクリーン出力を誤って解釈する)へ落ちて
// いた――このテストは、出荷されるusilicrc自身に`man`が実際に含まれて
// いることを固定し、将来の編集で再び落とされないようにする。
TEST_CASE("shipped usilicrc's PROGRAM section includes \"man\" (SHELL: `man ls` should hand off to the real "
          "interactive-program launcher, not run inline through Shell's own line-oriented scrollback)") {
  std::ifstream in("../usilicrc");
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();

  Configuration config;
  REQUIRE(config.loadRcText(buffer.str(), true));
  bool foundMan = false;
  for (const std::string &seed : config.programSeedCommands()) {
    std::size_t end = seed.find_first_of(" \t");
    std::string firstWord = seed.substr(0, end);
    if (firstWord == "man") foundMan = true;
  }
  CHECK(foundMan);
}

// Configuration::grepPresets()自身の宣言コメント参照(2026-09-25再実装)。
TEST_CASE("GREP directive: each line's first token (or a quoted string) becomes a preset, comments dropped") {
  Configuration config;
  REQUIRE(config.loadRcText("GREP\n\t\"^  [0-9]+\\.\"   # headings\n\tm.n\t# man\n\tCP|MV|RM # ops\n", true));
  CHECK(config.grepPresets() == std::vector<std::string>{"^  [0-9]+\\.", "m.n", "CP|MV|RM"});
}

TEST_CASE("BUFFER: the default (no BUFFER directive) is 64MB") {
  Configuration config;
  CHECK(config.bufferSizeLimitBytes() == 64 * 1024 * 1024);
}

// Configuration自身のコンストラクタの2026-09-25のコメント参照(定番の別名を組み込みに)。
TEST_CASE("built-in archive aliases: .tar.gz/.taz/.tar.z/.jar/.tbz2/.txz/.tzst work without any usilicrc") {
  Configuration config;
  auto kindOf = [&](const std::string &name) {
    fm::ArchiveKind kind = fm::ArchiveKind::None;
    config.programForSuffix(name, &kind);
    return kind;
  };
  for (const char *name : {"a.tar.gz", "a.taz", "a.tar.z", "a.tbz2", "a.txz", "a.tzst"}) {
    CAPTURE(name);
    CHECK(kindOf(name) == fm::ArchiveKind::Tar);
  }
  CHECK(kindOf("a.jar") == fm::ArchiveKind::Zip);
  // .tar.gzは.gz(FILEのgzip -dc)より長い一致なのでアーカイブ扱い。
  REQUIRE(config.loadRcText("FILE\n\t.gz\tgzip\t-dc\n", true));
  CHECK(kindOf("a.tar.gz") == fm::ArchiveKind::Tar);
  CHECK(kindOf("a.gz") == fm::ArchiveKind::None);
}
