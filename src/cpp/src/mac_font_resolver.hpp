#pragma once

#include <string>

namespace fm {

// Resolves a font's PostScript/family name (e.g. "MS-Gothic", exactly
// what Terminal.app's AppleScript "font name" property reports) to an
// actual loadable font FILE path, via macOS's CoreText framework
// (CTFontDescriptorCreateWithNameAndSize + kCTFontURLAttribute) --
// the only reliable way to go from a font NAME to a FILE on macOS;
// there's no filesystem naming convention that maps one to the other
// (verified directly: "MS-Gothic" -> "/Users/.../Library/Fonts/
// msgothic.ttc", a per-user-installed font, not something a fixed
// candidate path list could ever have covered). Added 2026-07-31 --
// main.cpp's own resolveGuiFontPath() comment previously documented
// font-NAME matching as deliberately out of scope for exactly this
// reason (needs a real font-lookup API, not just a string), at a real
// report that the GUI window wasn't honoring the calling terminal's
// configured font (e.g. "ＭＳゴシック14" for full Japanese coverage).
//
// Returns "" if resolution fails for any reason (font not installed,
// CoreText API failure), or unconditionally on non-macOS platforms
// (see the .cpp's #ifdef __APPLE__) -- callers must fall back to
// their own candidate-path list either way, this is never a hard
// requirement.
std::string resolveMacFontPath(const std::string &fontName, double pointSize);

}  // namespace fm
