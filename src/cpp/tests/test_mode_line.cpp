#include "mode_line.hpp"
#include "doctest.h"

using fm::ModeLine;

TEST_CASE("format: current is zero-padded to total's digit width") {
  CHECK(ModeLine::format(3, 15) == "03/15 20%");
  CHECK(ModeLine::format(3, 150) == "003/150  2%");
}

TEST_CASE("format: percent is based on currentLine's own position, not the viewport") {
  CHECK(ModeLine::format(50, 100) == "050/100 50%");
}

TEST_CASE("format: the very first line shows TOP instead of a percentage") {
  CHECK(ModeLine::format(1, 24) == "01/24 TOP");
  CHECK(ModeLine::format(1, 100) == "001/100 TOP");
}

TEST_CASE("format: the very last line shows END instead of a percentage") {
  CHECK(ModeLine::format(15, 15) == "15/15 END");
  CHECK(ModeLine::format(24, 24) == "24/24 END");
}

TEST_CASE("format: empty list") {
  CHECK(ModeLine::format(0, 0) == "0/0 END");
}

TEST_CASE("format: single-line list shows END, not TOP (END takes priority when both apply)") {
  CHECK(ModeLine::format(1, 1) == "1/1 END");
}
