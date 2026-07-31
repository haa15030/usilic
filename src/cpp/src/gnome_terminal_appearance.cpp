#include "gnome_terminal_appearance.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "child_process.hpp"

namespace fm {

std::string stripGsettingsQuotedString(const std::string &raw) {
  std::string s = raw;
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
  return s;
}

std::optional<GsettingsColor> parseGsettingsRgbColor(const std::string &raw) {
  std::string s = stripGsettingsQuotedString(raw);
  auto clampByte = [](int v) -> unsigned char { return static_cast<unsigned char>(std::clamp(v, 0, 255)); };

  int r, g, b;
  if (std::sscanf(s.c_str(), "rgb(%d,%d,%d)", &r, &g, &b) == 3)
    return GsettingsColor{clampByte(r), clampByte(g), clampByte(b)};

  // Confirmed live 2026-07-31 (see queryGnomeTerminalColors()'s own
  // verification note): a real gsettings on Ubuntu 26.04 returns
  // "#RRGGBB" hex, not the "rgb(r,g,b)" decimal form a web search
  // (Gemini) had suggested -- GLib/GTK's own color-serialization
  // format apparently varies by version, so both are handled here
  // rather than trusting the one that was merely reported secondhand.
  if (s.size() == 7 && s[0] == '#') {
    unsigned int hex = 0;
    if (std::sscanf(s.c_str() + 1, "%6x", &hex) == 1)
      return GsettingsColor{static_cast<unsigned char>((hex >> 16) & 0xFF), static_cast<unsigned char>((hex >> 8) & 0xFF),
                             static_cast<unsigned char>(hex & 0xFF)};
  }
  return std::nullopt;
}

ParsedFontDescription parsePangoFontDescription(const std::string &raw, int fallbackPointSize) {
  std::string s = stripGsettingsQuotedString(raw);
  std::size_t lastSpace = s.find_last_of(' ');
  if (lastSpace != std::string::npos) {
    std::string trailing = s.substr(lastSpace + 1);
    if (!trailing.empty() &&
        std::all_of(trailing.begin(), trailing.end(), [](unsigned char c) { return std::isdigit(c); })) {
      return ParsedFontDescription{s.substr(0, lastSpace), std::atoi(trailing.c_str())};
    }
  }
  return ParsedFontDescription{s, fallbackPointSize};
}

std::string resolveLinuxFontPath(const std::string &fontName) {
  if (fontName.empty()) return "";
  ChildProcess::Result result = ChildProcess::run({"fc-match", fontName, "--format=%{file}"});
  if (!result.ok) return "";
  std::string path = result.output;
  while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back()))) path.pop_back();
  return path;
}

}  // namespace fm
