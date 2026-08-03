#include "mac_font_resolver.hpp"
#include "doctest.h"

#include <fstream>

using fm::resolveMacFontPath;

#ifdef __APPLE__

TEST_CASE("resolveMacFontPath: a real system font name resolves to an existing file") {
  // Helvetica ships on every macOS version this project targets --
  // unlike MS-Gothic (verified manually during development, see
  // CLAUDE.md's 2026-07-31 note), it doesn't depend on the user
  // having a specific font installed, so it's safe to assert on here.
  std::string path = resolveMacFontPath("Helvetica", 14.0);
  CHECK_FALSE(path.empty());
  if (!path.empty()) {
    std::ifstream probe(path, std::ios::binary);
    CHECK(probe.good());
  }
}

TEST_CASE("resolveMacFontPath: an unknown font name fails gracefully, not a crash") {
  std::string path = resolveMacFontPath("ThisFontDoesNotExist12345", 14.0);
  CHECK(path.empty());
}

// SF Mono (Terminal.app's own default font on any reasonably recent
// macOS) can NEVER be resolved through the ordinary CoreText name
// lookup this function otherwise uses -- macOS deliberately blocks it
// (confirmed live, 2026-08-03: a bare "SFMono-Regular" lookup silently
// returns nothing, and the dot-prefixed private-name form
// ".SFMono-Regular" returns an unrelated substitute, Times New Roman,
// with a Console warning explaining the API misuse). See this
// function's own comment for why the fix is a direct, fixed-path
// check rather than a different CoreText call. A real user report
// (2026-08-03): the GUI window silently fell back to Menlo (visually
// similar enough that "the font TYPE looks right") whose glyph metrics
// don't actually match SF Mono's, producing a wrong-sized window.
TEST_CASE("resolveMacFontPath: SF Mono resolves to its known fixed system path, not Menlo") {
  std::string path = resolveMacFontPath("SFMono-Regular", 14.0);
  CHECK_FALSE(path.empty());
  if (!path.empty()) {
    CHECK(path == "/System/Library/Fonts/SFNSMono.ttf");
    std::ifstream probe(path, std::ios::binary);
    CHECK(probe.good());
  }
}

#else

TEST_CASE("resolveMacFontPath: always empty on non-Apple platforms") {
  CHECK(resolveMacFontPath("Helvetica", 14.0).empty());
}

#endif
