#include "mouse_sgr.hpp"
#include "doctest.h"

using fm::parseMouseSgr;

TEST_CASE("parseMouseSgr: a left-button press decodes 1-based wire coordinates to 0-based") {
  auto ev = parseMouseSgr("0;45;12M");
  REQUIRE(ev.has_value());
  CHECK(ev->column == 44);
  CHECK(ev->row == 11);
  CHECK(ev->button == 0);
  CHECK(ev->pressed == true);
  CHECK(ev->motionOnly == false);
}

TEST_CASE("parseMouseSgr: a release uses the lowercase terminator") {
  auto ev = parseMouseSgr("0;10;10m");
  REQUIRE(ev.has_value());
  CHECK(ev->pressed == false);
}

TEST_CASE("parseMouseSgr: a hover-motion report sets motionOnly") {
  auto ev = parseMouseSgr("35;5;5M");  // 32 (motion bit) | 3 (no button) == 35
  REQUIRE(ev.has_value());
  CHECK(ev->motionOnly == true);
}

TEST_CASE("parseMouseSgr: a drag-motion report keeps which button is held") {
  auto ev = parseMouseSgr("32;5;5M");  // 32 (motion bit) | 0 (left button)
  REQUIRE(ev.has_value());
  CHECK(ev->motionOnly == true);
  CHECK(ev->button == 0);
}

TEST_CASE("parseMouseSgr: rejects malformed input") {
  CHECK_FALSE(parseMouseSgr("").has_value());
  CHECK_FALSE(parseMouseSgr("not-a-report").has_value());
  CHECK_FALSE(parseMouseSgr("0;45;12").has_value());   // missing terminator
  CHECK_FALSE(parseMouseSgr("0;0;5M").has_value());    // column 0 is out of range (wire is 1-based)
}
