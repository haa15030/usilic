#include "doctest.h"
#include "history.hpp"

using fm::Charset;
using fm::History;
using fm::NameSetCategory;
using fm::PositionCategory;
using fm::RingCategory;

TEST_CASE("cursor position memory: set/get round trip per category") {
  History h;
  History::CursorPosition pos{5, 100, 2, 42};
  h.setPosition(PositionCategory::File, "/home/user/a.txt", pos);

  auto found = h.positionFor(PositionCategory::File, "/home/user/a.txt");
  REQUIRE(found.has_value());
  CHECK(found->row == 5);
  CHECK(found->totalRows == 100);
  CHECK(found->top == 2);
  CHECK(found->lineNumber == 42);

  CHECK_FALSE(h.positionFor(PositionCategory::File, "/nope").has_value());
  CHECK_FALSE(h.positionFor(PositionCategory::Directory, "/home/user/a.txt").has_value());
}

TEST_CASE("name sets: first-occurrence-wins, no duplicates, insertion order preserved") {
  History h;
  h.rememberName(NameSetCategory::CutPattern, "TODO");
  h.rememberName(NameSetCategory::CutPattern, "FIXME");
  h.rememberName(NameSetCategory::CutPattern, "TODO");  // duplicate, ignored

  REQUIRE(h.namesIn(NameSetCategory::CutPattern).size() == 2);
  CHECK(h.namesIn(NameSetCategory::CutPattern)[0] == "TODO");
  CHECK(h.namesIn(NameSetCategory::CutPattern)[1] == "FIXME");
  CHECK(h.containsName(NameSetCategory::CutPattern, "TODO"));
  CHECK_FALSE(h.containsName(NameSetCategory::CutPattern, "nope"));
  CHECK(h.namesIn(NameSetCategory::CommandModeLabel).empty());
}

TEST_CASE("ring history: pushRecent dedups without reordering (FIFO by first insertion, not LRU)") {
  History h(10);
  h.pushRecent(RingCategory::RecentFile, "a");
  h.pushRecent(RingCategory::RecentFile, "b");
  h.pushRecent(RingCategory::RecentFile, "c");
  h.pushRecent(RingCategory::RecentFile, "a");  // revisit -- must NOT move to the back

  const auto &entries = h.recentEntries(RingCategory::RecentFile);
  REQUIRE(entries.size() == 3);
  CHECK(entries[0] == "a");
  CHECK(entries[1] == "b");
  CHECK(entries[2] == "c");
}

TEST_CASE("ring history: evicts the single oldest entry once at the retention cap") {
  History h(3);
  h.pushRecent(RingCategory::ShellCommand, "cmd1");
  h.pushRecent(RingCategory::ShellCommand, "cmd2");
  h.pushRecent(RingCategory::ShellCommand, "cmd3");
  h.pushRecent(RingCategory::ShellCommand, "cmd4");

  const auto &entries = h.recentEntries(RingCategory::ShellCommand);
  REQUIRE(entries.size() == 3);
  CHECK(entries[0] == "cmd2");  // cmd1 evicted (oldest)
  CHECK(entries[1] == "cmd3");
  CHECK(entries[2] == "cmd4");
}

TEST_CASE("retentionCount() reflects the constructor argument (no F6-sentinel dance needed)") {
  History h(42);
  CHECK(h.retentionCount() == 42);
}

TEST_CASE("session state and trailer settings round trip through explicit setters") {
  History h;
  History::SessionState state;
  state.previousDirectory = "/old";
  state.currentDirectory = "/current";
  state.modeLineArg = "arg text";
  state.previousScreenMarker = "prev-marker";
  state.lastOpenName = "/current/file.txt";
  state.pendingCutBuffer = "buffered cut text";
  state.pendingCutProgram = "grep";
  h.setSessionState(state);

  CHECK(h.sessionState().previousDirectory == "/old");
  CHECK(h.sessionState().currentDirectory == "/current");
  CHECK(h.sessionState().pendingCutProgram == "grep");

  h.setSmoothScrollEnabled(true);
  h.setLsMode(2);
  h.setSavedCharset(Charset::Encoding::Euc);
  CHECK(h.smoothScrollEnabled());
  CHECK(h.lsMode() == 2);
  REQUIRE(h.savedCharset().has_value());
  CHECK(*h.savedCharset() == Charset::Encoding::Euc);
}

TEST_CASE("serialize() then loadHistoryText() round-trips every category") {
  History h(50);
  h.setPosition(PositionCategory::File, "/a/b.txt", {3, 10, 0, 7});
  h.setPosition(PositionCategory::Directory, "/a/b/", {1, 5, 0, -1});
  h.setPosition(PositionCategory::ManPage, "ls(1)", {0, 1, 0, -1});
  h.setPosition(PositionCategory::Screen, " LOCATE", {2, 20, 1, -1});
  h.rememberName(NameSetCategory::CutPattern, "extract this");
  h.rememberName(NameSetCategory::CommandModeLabel, "some command");
  h.pushRecent(RingCategory::ShellCommand, "ls -la");
  h.pushRecent(RingCategory::RecentFile, "/a/b.txt");
  h.pushRecent(RingCategory::RecentDirectory, "/a/b/");
  h.pushRecent(RingCategory::RecentManPage, "ls(1)");
  h.pushRecent(RingCategory::RecentLocateQuery, "*.cpp");
  h.pushRecent(RingCategory::RecentFgrepPattern, "TODO");
  h.pushRecent(RingCategory::RecentEgrepPattern, "^func.*\\(");

  History::SessionState state;
  state.previousDirectory = "/old dir/with space";  // exercises sspc()/rspc() escaping
  state.currentDirectory = "/cur/";
  state.modeLineArg = "some arg";
  state.previousScreenMarker = "marker";
  state.lastOpenName = "/cur/file with space.txt";
  state.pendingCutBuffer = "";
  state.pendingCutProgram = "";
  h.setSessionState(state);
  h.setSmoothScrollEnabled(true);
  h.setLsMode(1);
  h.setSavedCharset(Charset::Encoding::Utf8);

  std::string text = h.serialize("usilic", "Mon Jan  1 00:00:00 2026");

  History loaded;
  REQUIRE(loaded.loadHistoryText(text, /*environmentCharsetExplicitlySet=*/false));
  CHECK(loaded.isComplete());
  CHECK(loaded.errors().empty());

  auto filePos = loaded.positionFor(PositionCategory::File, "/a/b.txt");
  REQUIRE(filePos.has_value());
  CHECK(filePos->row == 3);
  CHECK(filePos->totalRows == 10);
  CHECK(filePos->lineNumber == 7);

  CHECK(loaded.positionFor(PositionCategory::Directory, "/a/b/")->lineNumber == -1);
  CHECK(loaded.positionFor(PositionCategory::ManPage, "ls(1)").has_value());
  CHECK(loaded.positionFor(PositionCategory::Screen, " LOCATE").has_value());

  CHECK(loaded.containsName(NameSetCategory::CutPattern, "extract this"));
  CHECK(loaded.containsName(NameSetCategory::CommandModeLabel, "some command"));

  CHECK(loaded.recentEntries(RingCategory::ShellCommand).size() == 1);
  CHECK(loaded.recentEntries(RingCategory::ShellCommand)[0] == "ls -la");
  CHECK(loaded.recentEntries(RingCategory::RecentFile)[0] == "/a/b.txt");
  CHECK(loaded.recentEntries(RingCategory::RecentDirectory)[0] == "/a/b/");
  CHECK(loaded.recentEntries(RingCategory::RecentManPage)[0] == "ls(1)");
  CHECK(loaded.recentEntries(RingCategory::RecentLocateQuery)[0] == "*.cpp");
  CHECK(loaded.recentEntries(RingCategory::RecentFgrepPattern)[0] == "TODO");
  CHECK(loaded.recentEntries(RingCategory::RecentEgrepPattern)[0] == "^func.*\\(");

  CHECK(loaded.sessionState().previousDirectory == "/old dir/with space");
  CHECK(loaded.sessionState().currentDirectory == "/cur/");
  CHECK(loaded.sessionState().lastOpenName == "/cur/file with space.txt");

  CHECK(loaded.smoothScrollEnabled() == true);
  CHECK(loaded.lsMode() == 1);
  REQUIRE(loaded.savedCharset().has_value());
  CHECK(*loaded.savedCharset() == Charset::Encoding::Utf8);
}

TEST_CASE("loadHistoryText: environmentCharsetExplicitlySet=true suppresses the saved charset") {
  History h;
  h.setSavedCharset(Charset::Encoding::Euc);
  std::string text = h.serialize("usilic", "timestamp");

  History loaded;
  REQUIRE(loaded.loadHistoryText(text, /*environmentCharsetExplicitlySet=*/true));
  CHECK_FALSE(loaded.savedCharset().has_value());
}

TEST_CASE("loadHistoryText: a charset code of 0 (absent/predates the field) resolves to UTF-8, not EUC") {
  // Directly exercises the CLAUDE.md history item 13 fix: legacy's
  // own fallback was `(charset) ? charset : C_utf8`, but an earlier
  // bug elsewhere made THIS exact "0 means unset" case silently end
  // up EUC in practice. Build a minimal, well-formed file by hand
  // with charset field 0 and confirm the fixed fallback.
  History h;
  std::string text = h.serialize("usilic", "timestamp");  // savedCharset_ unset -> writes charset code 0

  History loaded;
  REQUIRE(loaded.loadHistoryText(text, /*environmentCharsetExplicitlySet=*/false));
  REQUIRE(loaded.savedCharset().has_value());
  CHECK(*loaded.savedCharset() == Charset::Encoding::Utf8);
}

TEST_CASE("loadHistoryText: rejects text missing the 'Current buffer' marker") {
  History h;
  CHECK_FALSE(h.loadHistoryText("not a history file at all\n", false));
  CHECK_FALSE(h.errors().empty());
}

TEST_CASE("loadHistoryText: rejects text with a missing/out-of-order section label") {
  History h;
  std::string text = h.serialize("usilic", "t");
  // Corrupt the file by deleting the " DIR" label line entirely.
  auto pos = text.find("\x0c DIR\n");
  REQUIRE(pos != std::string::npos);
  text.erase(pos, std::string("\x0c DIR\n").size());

  History loaded;
  CHECK_FALSE(loaded.loadHistoryText(text, false));
  CHECK_FALSE(loaded.errors().empty());
}

TEST_CASE("loadHistoryText: isComplete() is false when the EOF sentinel is missing (truncated file)") {
  History h;
  std::string text = h.serialize("usilic", "t");
  auto sentinelPos = text.rfind('\x1a');
  REQUIRE(sentinelPos != std::string::npos);
  text.erase(sentinelPos);  // simulate a write that got cut off

  History loaded;
  // Parsing itself still succeeds (every section was present)...
  CHECK(loaded.loadHistoryText(text, false));
  // ...but completeness is false, signaling the caller to consider a
  // `.old` backup fallback, matching legacy's own truncation check.
  CHECK_FALSE(loaded.isComplete());
}

TEST_CASE("loadHistoryText: fields containing spaces and control bytes round-trip via sspc()/rspc()") {
  History h;
  History::SessionState state;
  state.currentDirectory = "/path with spaces/and\ttabs";
  h.setSessionState(state);
  h.rememberName(NameSetCategory::CutPattern, "pattern with spaces");
  h.pushRecent(RingCategory::RecentFile, "/a file/with spaces.txt");

  std::string text = h.serialize("usilic", "t");
  History loaded;
  REQUIRE(loaded.loadHistoryText(text, false));
  CHECK(loaded.sessionState().currentDirectory == "/path with spaces/and\ttabs");
  CHECK(loaded.containsName(NameSetCategory::CutPattern, "pattern with spaces"));
  CHECK(loaded.recentEntries(RingCategory::RecentFile)[0] == "/a file/with spaces.txt");
}
