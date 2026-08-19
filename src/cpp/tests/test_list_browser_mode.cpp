#include "content_view.hpp"
#include "doctest.h"
#include "list_browser_mode.hpp"
#include "recording_display.hpp"

using fm::ListBrowserMode;
using fm::test::RecordingDisplay;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  fm::ContentView view{display};
  ListBrowserMode mode{view};
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "show: populates items() and the ContentView's lines, cursor at 0") {
  mode.show({"alpha", "beta", "gamma"});
  REQUIRE(mode.items().size() == 3);
  CHECK(mode.items()[1] == "beta");
  REQUIRE(view.lines().size() == 3);
  CHECK(view.lines()[0].text == "alpha");
  CHECK(view.lines()[2].text == "gamma");
  CHECK(view.cursorIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "selectedItem: tracks the ContentView cursor") {
  mode.show({"alpha", "beta", "gamma"});
  view.setCursorIndex(2);
  const auto *selected = mode.selectedItem();
  REQUIRE(selected != nullptr);
  CHECK(*selected == "gamma");
}

TEST_CASE_FIXTURE(Fixture, "selectedItem: null before any list has been shown") {
  CHECK(mode.selectedItem() == nullptr);
}

// Mode-line fields (highlighted item as name, RingCategory label as
// trailing) are no longer this class's own responsibility -- moved to
// Application's constructor 2026-08-02 (see ListBrowserMode's own
// constructor comment for why: only Application knows
// listBrowserCategory_). Covered by test_application.cpp's own "List
// Browser mode-line" tests instead; this class's own mode-line text
// is just the bare position ("0/0 END" etc) with no provider
// registered at all when tested in isolation like this.
TEST_CASE_FIXTURE(Fixture, "mode-line fields: bare position text, no provider registered by this class itself") {
  CHECK(view.modeLineText() == "0/0 END");
  mode.show({"/tmp/alpha", "/tmp/beta"});
  CHECK(view.modeLineText() == "1/2 TOP");
}

TEST_CASE_FIXTURE(Fixture, "show: calling it again replaces the previous items entirely") {
  mode.show({"one", "two"});
  mode.show({"x"});
  REQUIRE(mode.items().size() == 1);
  CHECK(mode.items()[0] == "x");
  REQUIRE(view.lines().size() == 1);
}

// 2026-08-20 addition, a direct user request ("FILE:ARCで閲覧した
// utf8.tarに「(tar)」を付けて欲しい"): badges are display-only,
// appended to the rendered Line text, but items()/selectedItem() must
// still return the ORIGINAL unmodified path for submission.
TEST_CASE_FIXTURE(Fixture, "show: an optional badges vector appends display-only suffixes, without "
                           "changing items()/selectedItem() themselves") {
  mode.show({"/tmp/plain.txt", "/tmp/archive.tar"}, {"", "(tar)"});
  REQUIRE(view.lines().size() == 2);
  CHECK(view.lines()[0].text == "/tmp/plain.txt");
  CHECK(view.lines()[1].text == "/tmp/archive.tar  (tar)");
  REQUIRE(mode.items().size() == 2);
  CHECK(mode.items()[1] == "/tmp/archive.tar");  // unmodified, no "(tar)" baked in
  view.setCursorIndex(1);
  CHECK(*mode.selectedItem() == "/tmp/archive.tar");
}

TEST_CASE_FIXTURE(Fixture, "show: a shorter (or absent) badges vector is treated as \"no badge\" for the "
                           "missing indices, not an error") {
  mode.show({"a", "b", "c"}, {"(x)"});
  REQUIRE(view.lines().size() == 3);
  CHECK(view.lines()[0].text == "a  (x)");
  CHECK(view.lines()[1].text == "b");
  CHECK(view.lines()[2].text == "c");
}
