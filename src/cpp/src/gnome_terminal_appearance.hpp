#pragma once

#include <optional>
#include <string>

namespace fm {

// Pure parsing helpers for GNOME Terminal's own `gsettings`/dconf
// output formats, plus a `fc-match`-based font NAME -> file PATH
// resolver -- the Linux counterpart to mac_font_resolver.hpp, added
// 2026-07-31 after the user found (via a web search) that GNOME
// Terminal's font/color/profile settings ARE queryable from the
// command line after all, contrary to an earlier answer in this
// project that assumed no such mechanism existed on Linux. Split out
// from app/main.cpp's own queryTerminalAppColors() (the macOS/
// AppleScript equivalent, which inlines its own numeric-only sscanf
// parsing) specifically because gsettings' output needs real STRING
// parsing (quoted strings, "rgb(r,g,b)", Pango font descriptions) --
// there is no live GNOME Terminal reachable from this project's own
// development sandbox to verify any of this end-to-end against, so
// these are pure functions precisely so doctest can be the actual
// verification instead.

struct GsettingsColor {
  unsigned char r = 0, g = 0, b = 0;
};

// Parses gsettings' own color format, e.g. "'rgb(255,255,255)'"
// (gsettings wraps string-typed values in single quotes; a trailing
// newline from the subprocess's own output is tolerated too) OR the
// "#RRGGBB" hex form a real gsettings on Ubuntu 26.04 was confirmed
// (2026-07-31, live in this project's own dev container after
// installing gnome-terminal to register its schema) to actually
// return instead -- see the .cpp for why both are handled. Returns
// nullopt if the string matches neither.
std::optional<GsettingsColor> parseGsettingsRgbColor(const std::string &raw);

struct ParsedFontDescription {
  std::string family;
  int pointSize;
};

// Parses a Pango font description string (what GNOME Terminal's own
// "font" gsettings key, and org.gnome.desktop.interface's
// "monospace-font-name" key, both store) -- e.g. "Ubuntu Mono 11" or
// "Monospace Bold 12" -- into a family name and point size. The size
// is Pango's own convention: the LAST whitespace-separated token that
// parses as an integer; everything before it (including any style
// keywords like "Bold") is kept together as `family`, since
// fc-match/resolveLinuxFontPath() below is given the family string
// as-is and lets fontconfig's own matching handle style keywords the
// same way a real Pango-based renderer would. fallbackPointSize is
// used verbatim if no trailing integer is found. Also strips
// gsettings' own wrapping single quotes if present (see
// stripGsettingsQuotedString()), so this can be fed gsettings' raw
// output directly.
ParsedFontDescription parsePangoFontDescription(const std::string &raw, int fallbackPointSize);

// Strips gsettings' own wrapping single quotes around a string-typed
// value (e.g. "'some value'\n" -> "some value") and any trailing
// whitespace/newline -- the generic shape virtually every gsettings
// string-type query returns (profile IDs, font names, ...).
std::string stripGsettingsQuotedString(const std::string &raw);

// Resolves a font family name to an actual loadable font FILE path
// via fontconfig's `fc-match` command-line tool (part of the
// `fontconfig` package, a near-universal dependency on any desktop
// Linux distro since GTK/Pango themselves depend on it -- confirmed
// present in this project's own Ubuntu 26.04 dev container even
// though gnome-terminal itself isn't installed there). This is the
// Linux equivalent of mac_font_resolver.hpp's CTFontDescriptor-based
// resolution; unlike CoreText, fc-match is a plain subprocess call,
// so this doesn't need any special linking. Returns "" if fc-match
// isn't installed, the font name doesn't resolve, or (implicitly, on
// any non-Linux platform where this would still compile and run) the
// tool simply isn't found -- ChildProcess::run() already reports that
// as `!ok` uniformly across platforms, so there's no #ifdef guard
// here the way mac_font_resolver.cpp needs one for CoreText's
// framework-only API.
std::string resolveLinuxFontPath(const std::string &fontName);

}  // namespace fm
