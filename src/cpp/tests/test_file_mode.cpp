#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "content_view.hpp"
#include "doctest.h"
#include "file_mode.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Charset;
using fm::FileMode;
using fm::History;
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
  FileMode mode{view, history, Charset::Encoding::Ascii, 80};

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

TEST_CASE_FIXTURE(Fixture, "loadFile: a nonexistent file fails cleanly") {
  CHECK_FALSE(mode.loadFile((root / "does-not-exist.txt").string()));
}

TEST_CASE_FIXTURE(Fixture, "loadFile: long lines wrap at the configured width, via PipeBuffer") {
  fm::ContentView narrowView{display};
  History h2;
  FileMode narrowMode{narrowView, h2, Charset::Encoding::Ascii, 10};
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
  FileMode utf8Mode{utf8View, h2, Charset::Encoding::Utf8, 80};

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
  FileMode fresh{view, history, Charset::Encoding::Ascii, 80};
  fresh.saveCursorPosition();  // must not crash
  CHECK(history.positionFor(PositionCategory::File, "").has_value() == false);
}
