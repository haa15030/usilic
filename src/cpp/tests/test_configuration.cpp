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
  CHECK(config.commandFor(ConfiguredRole::Egrep).argv == std::vector<std::string>{"egrep", "-n"});
  CHECK_FALSE(config.commandFor(ConfiguredRole::Du).argv.empty());
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
  CHECK(config.shellArgv() == std::vector<std::string>{"bash", "-i"});
}

TEST_CASE("shellArgv: falls back to the caller-supplied shell when no SHELL directive is present") {
  Configuration config;
  CHECK(config.shellArgv().empty());
  config.setFallbackShellArgv({"/bin/tcsh"});
  CHECK(config.shellArgv() == std::vector<std::string>{"/bin/tcsh", "-i"});
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

TEST_CASE("BUFFER/HISTORY/WAIT/SCROLL/LS: single-integer directives") {
  Configuration config;
  REQUIRE(config.loadRcText(
      "BUFFER\n 5\n"
      "HISTORY\n 300\n"
      "WAIT\n 7\n"
      "SCROLL\n 1\n"
      "LS\n 2\n",
      true));
  CHECK(config.bufferSizeLimitBytes() == 5 * 1024 * 1024);
  CHECK(config.historyRetentionCount() == 300);
  CHECK(config.waitIntervalMicros() == 70000);
  CHECK(config.smoothScrollEnabled() == true);
  CHECK(config.lsMode() == 2);
}

TEST_CASE("BUFFER: an out-of-range value is clamped to 1 and recorded as an error") {
  Configuration config;
  REQUIRE(config.loadRcText("BUFFER\n 500\n", true));
  CHECK(config.bufferSizeLimitBytes() == 1 * 1024 * 1024);
  CHECK_FALSE(config.errors().empty());
}

TEST_CASE("GREP directive: strips surrounding quotes; PROGRAM directive: only in file-manager mode") {
  Configuration config;
  std::string rc =
      "GREP\n"
      " 'TODO'\n"
      " unquoted-pattern\n"
      "PROGRAM\n"
      " ls -la | grep foo\n";
  REQUIRE(config.loadRcText(rc, true));
  REQUIRE(config.grepSeedPatterns().size() == 2);
  CHECK(config.grepSeedPatterns()[0] == "TODO");
  CHECK(config.grepSeedPatterns()[1] == "unquoted-pattern");
  REQUIRE(config.programSeedCommands().size() == 1);
  CHECK(config.programSeedCommands()[0] == "ls -la | grep foo");

  Configuration pagerConfig;
  REQUIRE(pagerConfig.loadRcText(rc, /*fileManagerMode=*/false));
  CHECK(pagerConfig.programSeedCommands().empty());
}

TEST_CASE("GREP directive (2026-08-12 fix): an UNQUOTED pattern is truncated at the first space/tab, "
          "matching legacy conf_grep()'s own `while (*cp != ' ' && *cp != '\\t') cp++;` -- usilicrc's real "
          "GREP section right-aligns each pattern's own `# comment' with a run of tabs (e.g. "
          "\"\\\\/\\\\*[a-z]*\\\\*\\\\/\\t\\t\\t\\t # tcb source\"), and this port previously used an "
          "unquoted line's data wholesale (only trimCommentAndLeadingWs()'s own comment-strip + LEADING-"
          "whitespace-strip, no trailing-whitespace handling), baking the trailing tab run into the seed "
          "pattern as literal bytes -- a real report found this corrupted both the ^J^S/^J^C candidate "
          "list's own mode-line layout (real terminal tab-stop jumps threw off column alignment) and, once "
          "such a candidate seeded an actual editable prompt, rendered as reversed \"IIII\"-style control-"
          "character glyphs the moment editing began") {
  Configuration config;
  std::string rc =
      "GREP\n"
      "\t\\/\\*[a-z]*\\*\\/\t\t\t\t # tcb source\n"
      "\t^Question\t\t\t\t # Linux-FAQ\n"
      "\t\"^Subject: \"\t\t\t\t # Mailing list\n";
  REQUIRE(config.loadRcText(rc, true));
  REQUIRE(config.grepSeedPatterns().size() == 3);
  CHECK(config.grepSeedPatterns()[0] == "\\/\\*[a-z]*\\*\\/");
  CHECK(config.grepSeedPatterns()[1] == "^Question");
  // The quoted branch is untouched by this fix -- still extracts
  // exactly the quoted span, trailing tabs and all excluded by the
  // closing quote itself, not by this new truncation.
  CHECK(config.grepSeedPatterns()[2] == "^Subject: ");
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

TEST_CASE("KEY sections are skipped without error (deferred, see class comment)") {
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
