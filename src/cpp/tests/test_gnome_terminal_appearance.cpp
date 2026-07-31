#include "gnome_terminal_appearance.hpp"
#include "doctest.h"

using fm::parseGsettingsRgbColor;
using fm::parsePangoFontDescription;
using fm::stripGsettingsQuotedString;

TEST_CASE("stripGsettingsQuotedString: strips wrapping single quotes and trailing whitespace") {
  CHECK(stripGsettingsQuotedString("'Ubuntu Mono 11'\n") == "Ubuntu Mono 11");
  CHECK(stripGsettingsQuotedString("'b1dcc9dd-5262-4d8d-a863-c897e6d979b9'") ==
        "b1dcc9dd-5262-4d8d-a863-c897e6d979b9");
}

TEST_CASE("stripGsettingsQuotedString: an unquoted value passes through unchanged") {
  CHECK(stripGsettingsQuotedString("true\n") == "true");
}

TEST_CASE("parseGsettingsRgbColor: real gsettings output for foreground/background-color") {
  auto fg = parseGsettingsRgbColor("'rgb(255,255,255)'\n");
  REQUIRE(fg.has_value());
  CHECK(fg->r == 255);
  CHECK(fg->g == 255);
  CHECK(fg->b == 255);

  auto bg = parseGsettingsRgbColor("'rgb(44,0,30)'\n");
  REQUIRE(bg.has_value());
  CHECK(bg->r == 44);
  CHECK(bg->g == 0);
  CHECK(bg->b == 30);
}

TEST_CASE("parseGsettingsRgbColor: real gsettings output, #RRGGBB hex form") {
  // Confirmed live 2026-07-31: a real gsettings on Ubuntu 26.04
  // returns THIS form, not "rgb(r,g,b)" -- see the .cpp's own comment.
  auto bg = parseGsettingsRgbColor("'#171421'\n");
  REQUIRE(bg.has_value());
  CHECK(bg->r == 0x17);
  CHECK(bg->g == 0x14);
  CHECK(bg->b == 0x21);

  auto fg = parseGsettingsRgbColor("'#ffffff'\n");
  REQUIRE(fg.has_value());
  CHECK(fg->r == 0xff);
  CHECK(fg->g == 0xff);
  CHECK(fg->b == 0xff);
}

TEST_CASE("parseGsettingsRgbColor: unparseable input returns nullopt, not a crash") {
  CHECK_FALSE(parseGsettingsRgbColor("").has_value());
  CHECK_FALSE(parseGsettingsRgbColor("''").has_value());
  CHECK_FALSE(parseGsettingsRgbColor("'not a color'").has_value());
}

TEST_CASE("parsePangoFontDescription: family plus a trailing point size") {
  auto d = parsePangoFontDescription("'Ubuntu Mono 11'\n", 12);
  CHECK(d.family == "Ubuntu Mono");
  CHECK(d.pointSize == 11);
}

TEST_CASE("parsePangoFontDescription: a style keyword stays part of the family") {
  // Matches how fc-match/fontconfig itself expects to receive style
  // keywords bundled into the family string it's asked to match --
  // see resolveLinuxFontPath()'s own comment.
  auto d = parsePangoFontDescription("'Monospace Bold 12'", 10);
  CHECK(d.family == "Monospace Bold");
  CHECK(d.pointSize == 12);
}

TEST_CASE("parsePangoFontDescription: no trailing number falls back to the given point size") {
  auto d = parsePangoFontDescription("'Monospace'", 13);
  CHECK(d.family == "Monospace");
  CHECK(d.pointSize == 13);
}
