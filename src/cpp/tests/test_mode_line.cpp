#include "mode_line.hpp"
#include "doctest.h"

using fm::ModeLine;

TEST_CASE("format: current is zero-padded to total's digit width") {
  CHECK(ModeLine::format(3, 15, 12) == "03/15 80%");
  CHECK(ModeLine::format(3, 150, 12) == "003/150  8%");
}

TEST_CASE("format: percent is the bottom-visible line's fraction of total, not the cursor's") {
  // A 24-line list, viewport showing lines 1-10 (cursor on line 1):
  // bottom-visible is 10, so 10/24 -- not cursorIndex-based.
  CHECK(ModeLine::format(1, 24, 10) == "01/24 41%");
}

TEST_CASE("format: bottom-visible reaching total shows END instead of a percentage") {
  CHECK(ModeLine::format(15, 15, 15) == "15/15 END");
  CHECK(ModeLine::format(9, 15, 15) == "09/15 END");
}

TEST_CASE("format: empty list") {
  CHECK(ModeLine::format(0, 0, 0) == "0/0 END");
}

TEST_CASE("format: single-line list is always END (one row is both top and bottom)") {
  CHECK(ModeLine::format(1, 1, 1) == "1/1 END");
}
