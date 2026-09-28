#include "epub_vertical.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

#include "utf8.hpp"

namespace fm {

namespace {

constexpr int kColumnPitch = 3;  // 全角2セル+行間1セル

using Cell = EpubVerticalCell;

void appendUtf8(std::string &out, char32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

char32_t decode(const std::string &s, std::size_t pos, int len) {
  unsigned char c = static_cast<unsigned char>(s[pos]);
  if (len == 1) return c;
  char32_t cp = c & (0xFF >> (len + 1));
  for (int k = 1; k < len && pos + k < s.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(s[pos + k]) & 0x3F);
  return cp;
}

// 縦書き用字形への置き換え。
const std::map<char32_t, char32_t> &verticalForms() {
  static const std::map<char32_t, char32_t> table = {
      {0x3001, 0xFE11}, {0x3002, 0xFE12}, {0xFF0C, 0xFE10}, {0x2026, 0xFE19}, {0x2025, 0xFE30},
      {0x2014, 0xFE31},
      {0x2015, 0xFE31}, {0x300C, 0xFE41}, {0x300D, 0xFE42}, {0x300E, 0xFE43}, {0x300F, 0xFE44},
      {0xFF08, 0xFE35}, {0xFF09, 0xFE36}, {0xFF5B, 0xFE37}, {0xFF5D, 0xFE38}, {0x3014, 0xFE39},
      {0x3015, 0xFE3A}, {0x3010, 0xFE3B}, {0x3011, 0xFE3C}, {0x300A, 0xFE3D}, {0x300B, 0xFE3E},
      {0x3008, 0xFE3F}, {0x3009, 0xFE40}, {0xFF3B, 0xFE47}, {0xFF3D, 0xFE48}, {0x30FC, 0xFF5C},
      {0x2500, 0xFF5C}, {'(', 0xFE35},   {')', 0xFE36},   {'[', 0xFE47},   {']', 0xFE48},
  };
  return table;
}

// 行頭禁則(列の先頭に来てはいけない字)。
bool isNoStart(char32_t cp) {
  static const std::set<char32_t> set = {
      0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF09, 0x300D, 0x300F, 0x3011, 0x3015, 0x3009, 0x300B, 0xFF3D,
      0xFF5D, 0x2026, 0x2025, 0x30FC, 0x3005, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083,
      0x3085, 0x3087, 0x308E, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7,
      0x30EE, 0x30F5, 0x30F6, 0xFF01, 0xFF1F, 0xFF1A, 0xFF1B, 0x30FB, ')', ']', '!', '?', ',', '.'};
  return set.count(cp) > 0;
}

// 行末禁則(列の最後に来てはいけない字)。
bool isNoEnd(char32_t cp) {
  static const std::set<char32_t> set = {0xFF08, 0x300C, 0x300E, 0x3010, 0x3014, 0x3008, 0x300A, 0xFF3B, 0xFF5B,
                                         '(', '['};
  return set.count(cp) > 0;
}

std::optional<Display::Attribute> attributeAt(const std::vector<AttributeRun> &runs, int byte) {
  for (const auto &run : runs)
    if (byte >= run.start && byte < run.end) return run.attribute;
  return std::nullopt;
}

bool isAsciiDigit(char32_t cp) { return cp >= '0' && cp <= '9'; }

// 論理行を縦書きのマスへ。
std::vector<Cell> toCells(const EpubVerticalSourceLine &line) {
  const std::string &s = line.text;
  struct Glyph {
    char32_t cp;
    std::string bytes;
    int width;
    int byte;
  };
  std::vector<Glyph> glyphs;
  for (std::size_t pos = 0; pos < s.size();) {
    int len = Utf8::sequence_length(static_cast<unsigned char>(s[pos]));
    if (pos + len > s.size()) len = static_cast<int>(s.size() - pos);
    char32_t cp = decode(s, pos, len);
    if (cp == 0x2060) break;  // 章の印(行末のタブ、PipeBufferがU+2060+空白にしたもの)以降は捨てる
    int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(s.data() + pos), len);
    if (width > 0) glyphs.push_back({cp, s.substr(pos, len), width, static_cast<int>(pos)});
    pos += len;
  }
  while (!glyphs.empty() && glyphs.back().cp == ' ') glyphs.pop_back();

  std::vector<Cell> cells;
  for (std::size_t i = 0; i < glyphs.size(); i++) {
    const Glyph &g = glyphs[i];
    Cell cell;
    cell.original = g.cp;
    cell.attribute = attributeAt(line.attributes, g.byte);
    // 縦中横: 半角数字2桁まで、"!!" "!?" "?!" "??"
    if (i + 1 < glyphs.size()) {
      const Glyph &h = glyphs[i + 1];
      bool digits = isAsciiDigit(g.cp) && isAsciiDigit(h.cp) &&
                    (i + 2 >= glyphs.size() || !isAsciiDigit(glyphs[i + 2].cp)) &&
                    (i == 0 || !isAsciiDigit(glyphs[i - 1].cp));
      bool marks = (g.cp == '!' || g.cp == '?') && (h.cp == '!' || h.cp == '?');
      if (digits || marks) {
        cell.glyph = g.bytes + h.bytes;
        cells.push_back(cell);
        i++;
        continue;
      }
    }
    auto it = verticalForms().find(g.cp);
    if (it != verticalForms().end()) {
      appendUtf8(cell.glyph, it->second);
    } else if (g.cp == ' ') {
      cell.glyph = "  ";
    } else {
      cell.glyph = g.bytes;
      if (g.width < 2) cell.glyph += ' ';
    }
    cells.push_back(cell);
  }
  return cells;
}

}  // namespace

std::vector<EpubVerticalColumn> layoutEpubVerticalColumns(const std::vector<EpubVerticalSourceLine> &lines, int rows) {
  int height = std::max(1, rows);
  std::vector<EpubVerticalColumn> cols;
  for (const auto &line : lines) {
    std::vector<Cell> cells = toCells(line);
    if (cells.empty()) {
      cols.push_back({{}, line.rawLineNumber});
      continue;
    }
    std::size_t i = 0;
    while (i < cells.size()) {
      std::size_t count = std::min<std::size_t>(height, cells.size() - i);
      if (i + count < cells.size()) {
        // 追い出し: 次の列の頭が行頭禁則の字なら、この列の最後の字も次の列へ(2字まで)
        for (int k = 0; k < 2 && count > 1 && isNoStart(cells[i + count].original); k++) count--;
        if (count > 1 && isNoEnd(cells[i + count - 1].original)) count--;
      }
      cols.push_back({std::vector<Cell>(cells.begin() + i, cells.begin() + i + count), line.rawLineNumber});
      i += count;
    }
  }
  return cols;
}

int epubVerticalColumnsPerScreen(int columns) { return std::max(1, (columns + 1) / kColumnPitch); }

std::vector<ContentView::Line> renderEpubVerticalWindow(const std::vector<EpubVerticalColumn> &cols, int first,
                                                        int rows, int columns) {
  int perScreen = epubVerticalColumnsPerScreen(columns);
  std::vector<ContentView::Line> out;
  for (int r = 0; r < std::max(1, rows); r++) {
    // 画面の桁ごとの中身(2セル幅の字の右半分は空文字)
    std::vector<std::string> screen(std::max(0, columns), " ");
    std::vector<std::optional<Display::Attribute>> attrs(std::max(0, columns));
    for (int k = 0; k < perScreen; k++) {
      long c = static_cast<long>(first) + k;
      if (c < 0 || c >= static_cast<long>(cols.size()) || r >= static_cast<int>(cols[c].cells.size())) continue;
      int x = columns - 2 - k * kColumnPitch;
      if (x < 0) continue;
      const Cell &cell = cols[c].cells[r];
      std::size_t firstLen = Utf8::sequence_length(static_cast<unsigned char>(cell.glyph[0]));
      bool wide = Utf8::display_width(reinterpret_cast<const unsigned char *>(cell.glyph.data()),
                                      static_cast<int>(firstLen)) >= 2;
      if (wide) {
        screen[x] = cell.glyph;
        if (x + 1 < columns) screen[x + 1] = "";
      } else {
        screen[x] = cell.glyph.substr(0, firstLen);
        if (x + 1 < columns) screen[x + 1] = cell.glyph.substr(firstLen);
      }
      attrs[x] = cell.attribute;
      if (x + 1 < columns) attrs[x + 1] = cell.attribute;
    }
    ContentView::Line line;
    for (int x = 0; x < columns; x++) {
      int start = static_cast<int>(line.text.size());
      line.text += screen[x];
      int end = static_cast<int>(line.text.size());
      if (attrs[x] && end > start) {
        if (!line.attributes.empty() && line.attributes.back().end == start &&
            line.attributes.back().attribute == *attrs[x])
          line.attributes.back().end = end;
        else
          line.attributes.push_back({start, end, *attrs[x]});
      }
    }
    while (!line.text.empty() && line.text.back() == ' ') line.text.pop_back();
    for (auto &run : line.attributes) run.end = std::min<int>(run.end, static_cast<int>(line.text.size()));
    line.attributes.erase(std::remove_if(line.attributes.begin(), line.attributes.end(),
                                         [](const AttributeRun &run) { return run.end <= run.start; }),
                          line.attributes.end());
    line.neverHighlighted = true;
    out.push_back(std::move(line));
  }
  return out;
}

int epubVerticalColumnAt(int screenColumn, int columns) {
  // 右からk番目の列は桁[columns-2-k*pitch, columns-1-k*pitch]
  int fromRight = columns - 1 - screenColumn;
  if (fromRight < 0) return -1;
  if (fromRight % kColumnPitch == 2) return -1;  // 行間
  int k = fromRight / kColumnPitch;
  return k < epubVerticalColumnsPerScreen(columns) ? k : -1;
}

}  // namespace fm
