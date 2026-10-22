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

TEST_CASE("ring history: pushRecent suppresses only an IMMEDIATE repeat of the single most-recent entry "
          "(2026-08-04 correction: an earlier version of this test/implementation claimed whole-ring "
          "first-occurrence-wins dedup, but a closer re-read of sub.c's find_hist() -- which only ever "
          "compares against the LAST node -- plus a direct user clarification (\"アクセスした順なので"
          "重複もあり得る\") showed re-visiting an OLDER entry genuinely re-appends it)") {
  History h(10);
  h.pushRecent(RingCategory::RecentFile, "a");
  h.pushRecent(RingCategory::RecentFile, "b");
  h.pushRecent(RingCategory::RecentFile, "c");
  h.pushRecent(RingCategory::RecentFile, "a");  // revisit of an OLDER entry -- re-appended, not suppressed

  const auto &entries = h.recentEntries(RingCategory::RecentFile);
  REQUIRE(entries.size() == 4);
  CHECK(entries[0] == "a");
  CHECK(entries[1] == "b");
  CHECK(entries[2] == "c");
  CHECK(entries[3] == "a");
}

TEST_CASE("ring history: pushRecent DOES suppress a back-to-back repeat of the immediately-previous entry") {
  History h(10);
  h.pushRecent(RingCategory::RecentFile, "a");
  h.pushRecent(RingCategory::RecentFile, "a");  // immediate repeat -- suppressed
  h.pushRecent(RingCategory::RecentFile, "a");  // still the same immediately-previous entry -- suppressed

  const auto &entries = h.recentEntries(RingCategory::RecentFile);
  REQUIRE(entries.size() == 1);
  CHECK(entries[0] == "a");
}

// 2026-08-27 addition, a direct user report ("旧では同時にDIR履歴にも
// 所在地記載がなされる...^Tに限らずLOCATE、DUその他（ls）経由でない
// ファイルアクセスであっても在籍値のDIR履歴同時記帳は反映して欲し
// い"): pushRecent(RecentFile, ...) now also derives and pushes the
// file's own containing directory into RecentDirectory, matching
// legacy's simultaneous recording -- see pushRecent()'s own comment.
TEST_CASE("ring history: pushRecent(RecentFile, ...) also pushes the containing directory into "
          "RecentDirectory") {
  History h(10);
  h.pushRecent(RingCategory::RecentFile, "/a/b/file.txt");

  const auto &dirs = h.recentEntries(RingCategory::RecentDirectory);
  REQUIRE(dirs.size() == 1);
  CHECK(dirs.back() == "/a/b/");
}

// The archive case (a RecentFile entry that's itself an archive, not a
// plain file) works identically -- pushRecent() doesn't distinguish.
TEST_CASE("ring history: pushRecent(RecentFile, ...) for an archive path also pushes its containing "
          "directory") {
  History h(10);
  h.pushRecent(RingCategory::RecentFile, "/a/archive.tar");

  const auto &dirs = h.recentEntries(RingCategory::RecentDirectory);
  REQUIRE(dirs.size() == 1);
  CHECK(dirs.back() == "/a/");
}

// Ordinary `ls`-then-Enter browsing already reaches RecentDirectory
// separately (DirectoryMode::enterDirectory()'s own pushRecent() call,
// which fires BEFORE a subsequently-opened file's own RecentFile push)
// -- this derived push must not create a duplicate back-to-back entry
// in that already-common case (pushRecent()'s own existing dedup, "if
// the new entry equals the ring's own last entry, don't push," handles
// this for free, but this locks in that it actually does).
TEST_CASE("ring history: pushRecent(RecentFile, ...)'s derived directory push doesn't duplicate an "
          "already-current RecentDirectory entry") {
  History h(10);
  h.pushRecent(RingCategory::RecentDirectory, "/a/b/");  // ordinary `ls` navigation already recorded this
  h.pushRecent(RingCategory::RecentFile, "/a/b/file.txt");  // opening a file there

  const auto &dirs = h.recentEntries(RingCategory::RecentDirectory);
  REQUIRE(dirs.size() == 1);  // NOT duplicated
  CHECK(dirs.back() == "/a/b/");
}

// 2026-09-14修正、実際の再現(TUTORIALの真っ新起動時の事前登録エント
// リをusilicを実際に起動して検証中に発覚): application.cppのbuildCutFileHistoryEntry()
// が積む「パス /パターン/」(GrepSyntax::Regex)という合成形は、この
// 導出ロジックが素朴に`name`全体の最後の'/'を探すと、その閉じ
// デリミタ自身にヒットしてしまい、導出される「ディレクトリ」が
// 合成文字列まるごとになっていた(引用符区切りのFixedはパターン自身
// にスラッシュが無い限り無事だった、下の2ケース目参照)。
TEST_CASE("ring history: pushRecent(RecentFile, ...) with a slash-delimited composite CUT entry "
          "(GrepSyntax::Regex) derives the REAL containing directory, not the whole composite string") {
  History h(10);
  h.pushRecent(RingCategory::RecentFile, "/a/b/file.txt /pattern [a-z]+/");

  const auto &dirs = h.recentEntries(RingCategory::RecentDirectory);
  REQUIRE(dirs.size() == 1);
  CHECK(dirs.back() == "/a/b/");
}

// 同じ導出ロジックの引用符区切り(GrepSyntax::Fixed)側 -- 参考として
// 明示的に固定する(上のRegexケースが壊れる直前まで、これは`name`
// 自身がたまたま普通のパスと同じ形をしていたので無事だった)。
TEST_CASE("ring history: pushRecent(RecentFile, ...) with a quote-delimited composite CUT entry "
          "(GrepSyntax::Fixed) derives the REAL containing directory too") {
  History h(10);
  h.pushRecent(RingCategory::RecentFile, "/a/b/file.txt \"marker\"");

  const auto &dirs = h.recentEntries(RingCategory::RecentDirectory);
  REQUIRE(dirs.size() == 1);
  CHECK(dirs.back() == "/a/b/");
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

// 2026-10-17(^J^T^Rの実行順の一覧に同じコマンドを並べない、History::pushRecentImpl()参照)。
TEST_CASE("ring history: a command run again moves to the end instead of being listed twice") {
  History h(10);
  h.pushRecent(RingCategory::ShellCommand, "ls");
  h.pushRecent(RingCategory::ShellCommand, "make");
  h.pushRecent(RingCategory::ShellCommand, "ls");
  const auto &entries = h.recentEntries(RingCategory::ShellCommand);
  REQUIRE(entries.size() == 2);
  CHECK(entries[0] == "make");
  CHECK(entries[1] == "ls");
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

  CHECK(h.modeLineVisible() == true);  // 既定は「表示」
  h.setModeLineVisible(false);
  CHECK(h.modeLineVisible() == false);
}

// 2026-10-17(^Ot、EPUBの明朝体・ゴシック体、History::epubSansSerif()参照)。
TEST_CASE("serialize() then loadHistoryText() keeps the EPUB sans-serif choice") {
  History h(10);
  CHECK_FALSE(h.epubSansSerif());
  h.setEpubSansSerif(true);
  History loaded(10);
  REQUIRE(loaded.loadHistoryText(h.serialize("usilic", "Mon Jan  1 00:00:00 2026"),
                                 /*environmentCharsetExplicitlySet=*/false));
  CHECK(loaded.epubSansSerif());
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
  h.setModeLineVisible(false);

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
  // 2026-08-27: pushRecent(RecentFile, ...) now also derives and pushes
  // the file's own containing directory into RecentDirectory (see its
  // own comment) -- "/a/b.txt"'s parent "/a/" lands here FIRST, then
  // this test's own explicit "/a/b/" push right after it, so the ring
  // now legitimately holds two entries; checking .back() (the most
  // recent, explicit one) rather than [0] keeps this test about what it
  // was actually testing (RecentDirectory's own serialization
  // round-trip), not this unrelated derived side effect.
  CHECK(loaded.recentEntries(RingCategory::RecentDirectory).back() == "/a/b/");
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
  CHECK(loaded.modeLineVisible() == false);
}

TEST_CASE("loadHistoryText: USILIC trailer without the modeLineVisible token defaults to visible") {
  // 2026-09-12追加、項目⑫: modeLineVisible_はUSILICトレーラーの末尾に
  // 新規追加されたトークンなので、それが「無い」古い履歴ファイルでも
  // 正しく既定値(表示)へフォールバックすることを検証する
  // ――history.cpp自身のloadHistoryText()コメント(前方互換の慣習)
  // 参照。この1行のトークンだけを人為的に切り落として「古い」形式を
  // 再現する。
  History h(50);
  h.setSmoothScrollEnabled(true);
  std::string text = h.serialize("usilic", "timestamp");

  const std::string label = " USILIC\n";
  size_t labelPos = text.find(label);
  REQUIRE(labelPos != std::string::npos);
  size_t lineStart = labelPos + label.size();
  size_t lineEnd = text.find('\n', lineStart);
  REQUIRE(lineEnd != std::string::npos);
  size_t lastSpace = text.rfind(' ', lineEnd - 1);
  REQUIRE(lastSpace != std::string::npos);
  REQUIRE(lastSpace > lineStart);
  text.erase(lastSpace, lineEnd - lastSpace);  // 末尾トークン(modeLineVisible_)だけを除去

  History loaded;
  REQUIRE(loaded.loadHistoryText(text, /*environmentCharsetExplicitlySet=*/false));
  CHECK(loaded.isComplete());
  CHECK(loaded.modeLineVisible() == true);
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

// History::setRetentionCount()自身のコメント参照(2026-09-25、usilicrcのHISTORY)。
TEST_CASE("setRetentionCount: usilicrc's HISTORY caps each ring; lowering it trims the oldest; 0 is unlimited") {
  fm::History history;
  history.setRetentionCount(500);
  for (int i = 0; i < 300; i++) history.pushRecent(fm::RingCategory::RecentEgrepPattern, "p" + std::to_string(i));
  CHECK(history.recentEntries(fm::RingCategory::RecentEgrepPattern).size() == 300);

  // 500件分で書き出したものを500件の保持数で読み直しても、全て残る。
  fm::History reloaded(500);
  reloaded.loadHistoryText(history.serialize("usilic", "now"), false);
  CHECK(reloaded.recentEntries(fm::RingCategory::RecentEgrepPattern).size() == 300);

  history.setRetentionCount(100);
  const auto &ring = history.recentEntries(fm::RingCategory::RecentEgrepPattern);
  REQUIRE(ring.size() == 100);
  CHECK(ring.front() == "p200");
  CHECK(ring.back() == "p299");
  history.setRetentionCount(-5);
  CHECK(history.retentionCount() == 0);
}

// History自身のコンストラクタのコメント参照(2026-09-25、既定は無制限)。
TEST_CASE("History: the default (0) keeps every entry -- no count limit") {
  fm::History history;
  CHECK(history.retentionCount() == 0);
  for (int i = 0; i < 1000; i++) history.pushRecent(fm::RingCategory::RecentFile, "/f" + std::to_string(i));
  CHECK(history.recentEntries(fm::RingCategory::RecentFile).size() == 1000);
  fm::History reloaded;
  reloaded.loadHistoryText(history.serialize("usilic", "now"), false);
  CHECK(reloaded.recentEntries(fm::RingCategory::RecentFile).size() == 1000);
}

// 2026-10-22(アイコンからの起動の組、History::setIconLaunch()参照)。
TEST_CASE("an icon launch keeps its own font size and letter spacing apart from -g") {
  History h(10);
  History::GuiPreferences g;
  g.fontPointSize = 14;
  g.letterSpacingExtraHorizontal = 0;
  g.hasColors = true;
  g.fgR = 200;
  h.setGuiPreferences(g);
  History::setIconLaunch(true);
  CHECK(h.guiPreferences().fontPointSize == 14);  // 最初のアイコン起動は-gの値から
  History::GuiPreferences icon = h.guiPreferences();
  icon.fontPointSize = 20;
  icon.letterSpacingExtraHorizontal = 3;
  h.setGuiPreferences(icon);
  History::setIconLaunch(false);
  CHECK(h.guiPreferences().fontPointSize == 14);
  CHECK(h.guiPreferences().letterSpacingExtraHorizontal == 0);
  History loaded(10);
  REQUIRE(loaded.loadHistoryText(h.serialize("usilic", "Mon Jan  1 00:00:00 2026"),
                                 /*environmentCharsetExplicitlySet=*/false));
  CHECK(loaded.guiPreferences().fontPointSize == 14);
  History::setIconLaunch(true);
  CHECK(loaded.guiPreferences().fontPointSize == 20);
  CHECK(loaded.guiPreferences().letterSpacingExtraHorizontal == 3);
  CHECK(loaded.guiPreferences().fgR == 200);  // 色は共通
  History::setIconLaunch(false);
}

// 2026-10-22(History::clearJournal()参照): 保存(replayJournalOnto→clearJournal)を2回しても、A→B→Aは
// ディスク上でA,B,Aのまま(以前はA,B,A,B,Aに増えていた)。
TEST_CASE("saving twice does not replay the already-saved pushes again") {
  History session(0);
  History disk(0);
  session.pushRecent(RingCategory::RecentFile, "/x/A");
  session.replayJournalOnto(disk);
  session.clearJournal();
  session.pushRecent(RingCategory::RecentFile, "/x/B");
  session.pushRecent(RingCategory::RecentFile, "/x/A");
  session.replayJournalOnto(disk);
  session.clearJournal();
  const auto &ring = disk.recentEntries(RingCategory::RecentFile);
  CHECK(std::vector<std::string>(ring.begin(), ring.end()) == std::vector<std::string>{"/x/A", "/x/B", "/x/A"});
}
