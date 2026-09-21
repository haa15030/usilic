#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "content_view.hpp"
#include "doctest.h"
#include "du_mode.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::Configuration;
using fm::DuMode;
using fm::History;
using fm::PositionCategory;
using fm::test::RecordingDisplay;

namespace {

fs::path makeTempDir() {
  char tmpl[] = "/tmp/dumode_testXXXXXX";
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
  DuMode mode{view, history, configuration};

  Fixture() : root(makeTempDir()) {
    fs::create_directory(root / "subdir");
    { std::ofstream(root / "subdir" / "inner.txt") << std::string(2048, 'x'); }
    { std::ofstream(root / "top.txt") << std::string(1024, 'y'); }
  }

  ~Fixture() { fs::remove_all(root); }

  std::string path() const { return root.string() + "/"; }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "loadUsage: lists real disk usage for every file and subdirectory") {
  REQUIRE(mode.loadUsage(path()));
  REQUIRE_FALSE(mode.entries().empty());

  bool sawTop = false, sawSubdir = false, sawInner = false;
  for (const auto &e : mode.entries()) {
    if (e.path.find("top.txt") != std::string::npos) { sawTop = true; CHECK_FALSE(e.isDirectory); CHECK(e.sizeKb > 0); }
    if (e.path.find("inner.txt") != std::string::npos) { sawInner = true; CHECK_FALSE(e.isDirectory); }
    if (e.path.find("subdir") != std::string::npos && e.path.find("inner.txt") == std::string::npos) {
      sawSubdir = true;
      CHECK(e.isDirectory);
    }
  }
  CHECK(sawTop);
  CHECK(sawInner);
  CHECK(sawSubdir);
}

TEST_CASE_FIXTURE(Fixture, "loadUsage: a nonexistent path fails cleanly") {
  CHECK_FALSE(mode.loadUsage(path() + "does-not-exist/"));
}

TEST_CASE_FIXTURE(Fixture, "selectedEntry: tracks the ContentView cursor") {
  REQUIRE(mode.loadUsage(path()));
  view.setCursorIndex(0);
  CHECK(mode.selectedEntry() != nullptr);
  view.setCursorIndex(static_cast<int>(mode.entries().size()) - 1);
  CHECK(mode.selectedEntry() != nullptr);
}

TEST_CASE_FIXTURE(Fixture, "saveCursorPosition / loadUsage: cursor position round-trips through History") {
  REQUIRE(mode.loadUsage(path()));
  int last = static_cast<int>(mode.entries().size()) - 1;
  REQUIRE(last >= 0);
  view.setCursorIndex(last);
  mode.saveCursorPosition();

  auto saved = history.positionFor(PositionCategory::Screen, path());
  REQUIRE(saved.has_value());
  CHECK(saved->row == last);

  view.setCursorIndex(0);
  REQUIRE(mode.loadUsage(path()));
  CHECK(view.cursorIndex() == last);
}

// 2026-09-21、直接のユーザー依頼: duには`ls -F`相当が無いので、表示行の末尾へ型マーカーを補う
// (ディレクトリ'/'、実行ファイル'*'、シンボリックリンク'@')。entry.path自体は変えない。
TEST_CASE_FIXTURE(Fixture, "DuMode: entries carry ls -F style type markers, entry paths stay clean") {
  { std::ofstream(root / "run.sh") << "x"; }
  fs::permissions(root / "run.sh", fs::perms::owner_exec, fs::perm_options::add);
  fs::create_symlink(root / "top.txt", root / "link");
  auto row = [&](const char *name) { return "4\t" + (root / name).string() + "\n"; };
  REQUIRE(mode.loadUsageFromOutput(root.string(), row("subdir") + row("top.txt") + row("run.sh") + row("link")));
  const auto &entries = mode.entries();
  REQUIRE(entries.size() == 4);
  CHECK(entries[0].typeMarker == '/');
  CHECK(entries[1].typeMarker == 0);
  CHECK(entries[2].typeMarker == '*');
  CHECK(entries[3].typeMarker == '@');
  CHECK(entries[0].path == (root / "subdir").string());  // pathはマーカー無し
}
