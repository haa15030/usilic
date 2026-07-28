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

TEST_CASE_FIXTURE(Fixture, "loadArchive: lists a real .zip file's contents") {
  REQUIRE(mode.loadArchive(path("archive.zip")));
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
