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

#else

TEST_CASE("resolveMacFontPath: always empty on non-Apple platforms") {
  CHECK(resolveMacFontPath("Helvetica", 14.0).empty());
}

#endif
