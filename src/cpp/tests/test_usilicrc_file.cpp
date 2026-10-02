#include "doctest.h"
#include "usilicrc_file.hpp"

using fm::usilicrc::replaceSection;

TEST_CASE("usilicrc::replaceSection: replaces the value lines and keeps the comments") {
  std::string text =
      "WAIT\n"
      "# 書式: wait\n"
      "\n"
      "\t1\n"
      "\n"
      "KEY\n"
      "\tK-1\tC-g\n";
  CHECK(replaceSection(text, "WAIT", {"5"}, "") ==
        "WAIT\n"
        "# 書式: wait\n"
        "\n"
        "\t5\n"
        "\n"
        "KEY\n"
        "\tK-1\tC-g\n");
}

TEST_CASE("usilicrc::replaceSection: a commented-out value is not replaced, the value goes after the comments") {
  std::string text =
      "FONT\n"
      "# 書式: フォントファイル\n"
      "\n"
      "#\t/a.ttf\n"
      "\n"
      "KEY\n";
  CHECK(replaceSection(text, "FONT", {"/b.ttf"}, "") ==
        "FONT\n"
        "# 書式: フォントファイル\n"
        "\n"
        "#\t/a.ttf\n"
        "\n"
        "\t/b.ttf\n"
        "\n"
        "KEY\n");
}

TEST_CASE("usilicrc::replaceSection: a missing section is copied from the template in front of KEY") {
  std::string text =
      "WAIT\n"
      "\t1\n"
      "\n"
      "KEY\n"
      "\tK-1\tC-g\n";
  std::string templateText =
      "SPEED\n"
      "# 書式: usec ...\n"
      "\n"
      "\t90000 20000\n"
      "\n"
      "KEY\n";
  CHECK(replaceSection(text, "SPEED", {"1 2 3"}, templateText) ==
        "WAIT\n"
        "\t1\n"
        "\n"
        "SPEED\n"
        "# 書式: usec ...\n"
        "\n"
        "\t1 2 3\n"
        "\n"
        "KEY\n"
        "\tK-1\tC-g\n");
}

TEST_CASE("usilicrc::replaceSection: several value lines collapse into the new ones") {
  std::string text =
      "SPEED\n"
      "\t1\n"
      "\t2\n"
      "\n"
      "KEY\n";
  CHECK(replaceSection(text, "SPEED", {"9"}, "") ==
        "SPEED\n"
        "\t9\n"
        "\n"
        "KEY\n");
}

TEST_CASE("usilicrc::replaceMacroMarker / nextMacroNumber: ^X^S writes at the -- marker") {
  std::string text =
      "MACRO\n"
      "\tM-1\t;\tK-1 K-2 K-14\n"
      "\t--\n";
  CHECK(fm::usilicrc::nextMacroNumber(text) == 2);
  CHECK(fm::usilicrc::replaceMacroMarker(text, {"\tM-2\tx\tK-3", "\t--"}) ==
        "MACRO\n"
        "\tM-1\t;\tK-1 K-2 K-14\n"
        "\tM-2\tx\tK-3\n"
        "\t--\n");
}
