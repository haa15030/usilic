#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "archive_mode.hpp"
#include "content_view.hpp"
#include "doctest.h"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::ArchiveMode;
using fm::Configuration;
using fm::History;
using fm::test::RecordingDisplay;

namespace {

fs::path makeTempDir() {
  char tmpl[] = "/tmp/archivemode_testXXXXXX";
  char *result = mkdtemp(tmpl);
  REQUIRE(result != nullptr);
  return fs::path(result);
}

// Real archives (built via the actual `tar`/`zip` CLI, matching this
// project's established preference for real data over synthetic
// fixtures), matching ArchiveReader's own test precedent.
struct Fixture {
  fs::path root;
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  History history;
  Configuration configuration;
  ArchiveMode mode{view, history, configuration};

  Fixture() : root(makeTempDir()) {
    fs::create_directory(root / "src");
    { std::ofstream(root / "src" / "hello.txt") << "hello\n"; }
    { std::ofstream(root / "src" / "world.txt") << "world\n"; }

    std::string tarPath = (root / "archive.tar").string();
    std::string cmd = "cd '" + root.string() + "' && tar cf archive.tar src";
    REQUIRE(std::system(cmd.c_str()) == 0);

    std::string zipPath = (root / "archive.zip").string();
    std::string zipCmd = "cd '" + root.string() + "' && zip -qr archive.zip src";
    REQUIRE(std::system(zipCmd.c_str()) == 0);

    // 2026-08-26 addition: unlike RAR (no creation tool available, only
    // the extraction side -- see RarReader's own test file for why that
    // format's tests stop at the ArchiveReader level, matching the
    // established Rpm/Deb precedent of no ArchiveMode-level fixture at
    // all), `7z` itself can BOTH create and extract, so a real .7z
    // fixture here follows the exact same "build it live with the real
    // CLI" convention as tar/zip just above.
    std::string sevenZipPath = (root / "archive.7z").string();
    std::string sevenZipCmd = "cd '" + root.string() + "' && 7z a -bd -bb0 archive.7z src >/dev/null";
    REQUIRE(std::system(sevenZipCmd.c_str()) == 0);
  }

  ~Fixture() { fs::remove_all(root); }

  std::string path(const std::string &name) const { return (root / name).string(); }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "loadArchive: lists a real .tar file's contents") {
  REQUIRE(mode.loadArchive(path("archive.tar")));
  CHECK(mode.currentArchive() == path("archive.tar"));
  REQUIRE_FALSE(mode.entries().empty());

  bool sawHello = false, sawWorld = false;
  for (const auto &e : mode.entries()) {
    if (e.name.find("hello.txt") != std::string::npos) sawHello = true;
    if (e.name.find("world.txt") != std::string::npos) sawWorld = true;
  }
  CHECK(sawHello);
  CHECK(sawWorld);
}

// 2026-08-15 addition: a direct user request ("全モードのモードライン
// に「画面TOPからENDまでのバイト数」を追加してほしい") -- unlike
// DirectoryMode, nothing else was already using this slot here, so no
// restructuring was needed (see the provider's own comment).
TEST_CASE_FIXTURE(Fixture, "loadArchive: mode line shows the listing's own total byte count") {
  REQUIRE(mode.loadArchive(path("archive.tar")));
  CHECK(view.modeLineText().find(std::to_string(view.totalByteCount())) != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "loadArchive: lists a real .zip file's contents") {
  REQUIRE(mode.loadArchive(path("archive.zip")));
  bool sawHello = false;
  for (const auto &e : mode.entries())
    if (e.name.find("hello.txt") != std::string::npos) sawHello = true;
  CHECK(sawHello);
}

TEST_CASE_FIXTURE(Fixture, "loadArchive: lists a real .7z file's contents") {
  REQUIRE(mode.loadArchive(path("archive.7z")));
  bool sawHello = false;
  for (const auto &e : mode.entries())
    if (e.name.find("hello.txt") != std::string::npos) sawHello = true;
  CHECK(sawHello);
}

TEST_CASE_FIXTURE(Fixture, "loadArchive: a non-archive suffix fails cleanly") {
  std::string plain = path("plain.txt");
  { std::ofstream(plain) << "not an archive\n"; }
  CHECK_FALSE(mode.loadArchive(plain));
}

TEST_CASE_FIXTURE(Fixture, "loadArchive: a missing archive file fails cleanly") {
  CHECK_FALSE(mode.loadArchive(path("does-not-exist.tar")));
}

// 2026-08-19 REDESIGN, a direct user correction: "旧ARCのモードラインは
// お手本じゃなく、正しくは「ARC」「01/14 TOP」「1774」「(tar)」
// 「/Users/haa15030/git/usilic/src/utf8.tar」でよろしく" -- name is now
// the fixed literal "ARC" (not the archive's own basename, which this
// test originally asserted), trailing2 is a "(<command>)" badge (e.g.
// "(tar)").
//
// 2026-08-19 SAME-DAY follow-up (report =dh: "「(tar)」と「/Users/.../
// utf8.tar」の間に空白が無い"): the archive's own full path moved from
// rightWatermark (non-highlighted, an earlier attempt at this fix) to
// trailing3 (see its own comment) -- a genuinely separate, independently
// highlighted field with its own real plain-mode gap before it, instead
// of being glued onto trailing2 as one string whose embedded space
// rendered invisibly inside a single reverse-video run.
TEST_CASE_FIXTURE(Fixture, "mode-line fields: \"ARC\" name, \"(tar)\" command badge, and the archive's own "
                           "full path as a genuinely separate, gapped field (not glued onto the badge)") {
  REQUIRE(mode.loadArchive(path("archive.tar")));
  // modeLineText() composes name/position/trailing/trailing2/trailing3
  // (see its own comment).
  std::string text = view.modeLineText();
  CHECK(text.rfind("ARC ", 0) == 0);
  CHECK(text.find("(tar)") != std::string::npos);
  CHECK(text.find(path("archive.tar")) != std::string::npos);

  view.setViewport(0, 23);
  view.redraw();
  const std::string &row = display.rowText(23);
  std::size_t badgeEnd = row.find("(tar)") + 5;  // just past the ')'
  CHECK(display.standoutAt(23, static_cast<int>(badgeEnd) - 1) == true);   // ')' of the badge itself
  CHECK(display.standoutAt(23, static_cast<int>(badgeEnd)) == false);      // the real gap this fix adds
  CHECK(row.find(path("archive.tar")) != std::string::npos);
  CHECK(display.standoutAt(23, static_cast<int>(row.find(path("archive.tar")))) == true);  // path itself, highlighted
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: no name field before any archive has been loaded") {
  CHECK(view.modeLineText() == "0/0 END");
}

TEST_CASE_FIXTURE(Fixture, "extractSelected: recovers a real file's exact content from a .tar") {
  REQUIRE(mode.loadArchive(path("archive.tar")));
  bool found = false;
  for (std::size_t i = 0; i < mode.entries().size(); i++) {
    if (mode.entries()[i].name.find("hello.txt") != std::string::npos &&
        !mode.entries()[i].isDirectory) {
      view.setCursorIndex(static_cast<int>(i));
      found = true;
      break;
    }
  }
  REQUIRE(found);

  auto content = mode.extractSelected();
  REQUIRE(content.has_value());
  std::string text(content->begin(), content->end());
  CHECK(text == "hello\n");
}

TEST_CASE_FIXTURE(Fixture, "extractSelected: recovers a real file's exact content from a .zip") {
  REQUIRE(mode.loadArchive(path("archive.zip")));
  bool found = false;
  for (std::size_t i = 0; i < mode.entries().size(); i++) {
    if (mode.entries()[i].name.find("world.txt") != std::string::npos &&
        !mode.entries()[i].isDirectory) {
      view.setCursorIndex(static_cast<int>(i));
      found = true;
      break;
    }
  }
  REQUIRE(found);

  auto content = mode.extractSelected();
  REQUIRE(content.has_value());
  std::string text(content->begin(), content->end());
  CHECK(text == "world\n");
}

TEST_CASE_FIXTURE(Fixture, "extractSelected: recovers a real file's exact content from a .7z") {
  REQUIRE(mode.loadArchive(path("archive.7z")));
  bool found = false;
  for (std::size_t i = 0; i < mode.entries().size(); i++) {
    if (mode.entries()[i].name.find("hello.txt") != std::string::npos &&
        !mode.entries()[i].isDirectory) {
      view.setCursorIndex(static_cast<int>(i));
      found = true;
      break;
    }
  }
  REQUIRE(found);

  auto content = mode.extractSelected();
  REQUIRE(content.has_value());
  std::string text(content->begin(), content->end());
  CHECK(text == "hello\n");
}

TEST_CASE_FIXTURE(Fixture, "extractSelected: a directory entry has no extractable content") {
  REQUIRE(mode.loadArchive(path("archive.tar")));
  bool found = false;
  for (std::size_t i = 0; i < mode.entries().size(); i++) {
    if (mode.entries()[i].isDirectory) {
      view.setCursorIndex(static_cast<int>(i));
      found = true;
      break;
    }
  }
  REQUIRE(found);
  CHECK_FALSE(mode.extractSelected().has_value());
}

TEST_CASE_FIXTURE(Fixture, "saveCursorPosition / loadArchive: cursor position round-trips through History") {
  REQUIRE(mode.loadArchive(path("archive.tar")));
  int last = static_cast<int>(mode.entries().size()) - 1;
  REQUIRE(last > 0);
  view.setCursorIndex(last);
  mode.saveCursorPosition();

  view.setCursorIndex(0);
  REQUIRE(mode.loadArchive(path("archive.tar")));
  CHECK(view.cursorIndex() == last);
}
