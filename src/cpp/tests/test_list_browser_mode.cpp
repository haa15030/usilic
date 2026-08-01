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

TEST_CASE_FIXTURE(Fixture, "mode-line fields: shows the highlighted item's own string as the name field (2026-08-01 user report)") {
  mode.show({"/tmp/alpha", "/tmp/beta", "/tmp/gamma"});
  CHECK(view.modeLineText().rfind("/tmp/alpha ", 0) == 0);

  view.setCursorIndex(2);
  CHECK(view.modeLineText().rfind("/tmp/gamma ", 0) == 0);
}

TEST_CASE_FIXTURE(Fixture, "mode-line fields: no name field before any list has been shown") {
  CHECK(view.modeLineText() == "0/0 END");
}

TEST_CASE_FIXTURE(Fixture, "show: calling it again replaces the previous items entirely") {
  mode.show({"one", "two"});
  mode.show({"x"});
  REQUIRE(mode.items().size() == 1);
  CHECK(mode.items()[0] == "x");
  REQUIRE(view.lines().size() == 1);
}
