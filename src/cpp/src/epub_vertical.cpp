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
      // 矢印は90度回した向きに(2026-09-29、直接のユーザー報告"本文の→が横向き"、=hl)
      {0x2192, 0x2193}, {0x2190, 0x2191}, {0x2193, 0x2190}, {0x2191, 0x2192},
      {0x21D2, 0x21D3}, {0x21D0, 0x21D1}, {0x21D3, 0x21D0}, {0x21D1, 0x21D2},
      // ＜＞は〈〉の縦書きの字形(2026-09-30、直接のユーザー報告"＜＞が横倒し"、Kat.epubのあとがき)
      {0xFF1C, 0xFE3F}, {0xFF1E, 0xFE40},
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
    if (byte >= run.start && byte < run.end && run.attribute != Display::Attribute::Serif &&
        run.attribute != Display::Attribute::RubyBase && run.attribute != Display::Attribute::RubyText)
      return run.attribute;
  return std::nullopt;
}

bool serifAt(const std::vector<AttributeRun> &runs, int byte) {
  for (const auto &run : runs)
    if (byte >= run.start && byte < run.end && run.attribute == Display::Attribute::Serif) return true;
  return false;
}

bool hasRun(const std::vector<AttributeRun> &runs, int byte, Display::Attribute attribute) {
  for (const auto &run : runs)
    if (byte >= run.start && byte < run.end && run.attribute == attribute) return true;
  return false;
}

bool isAsciiDigit(char32_t cp) { return cp >= '0' && cp <= '9'; }
bool isAsciiAlnum(char32_t cp) { return isAsciiDigit(cp) || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'); }
bool isAsciiGraphic(char32_t cp) { return cp > 0x20 && cp < 0x7F; }

// 論理行を縦書きのマスへ。
std::vector<Cell> toCells(const EpubVerticalSourceLine &line, bool sideways) {
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
  // ルビ(2026-09-30、-gのみ、Display::Attribute::RubyBase参照): 読み(《》を除く)の字を
  // 並びから抜き、親字の最初の字に読みと親字の字数を持たせる(-gが列の右の行間に描く)。
  // -cは今までどおり「燈《ひ》」をマスに並べる。
  std::map<std::size_t, std::pair<std::string, int>> rubyOf;  // 抜いた後のglyphsの添字→(読み, 親字の字数)
  if (sideways) {
    std::vector<Glyph> kept;
    std::optional<std::size_t> baseStart;
    bool prevBase = false;
    for (const auto &g : glyphs) {
      if (hasRun(line.attributes, g.byte, Display::Attribute::RubyText)) {
        if (baseStart && g.cp != 0x300A && g.cp != 0x300B) rubyOf[*baseStart].first += g.bytes;
        prevBase = false;
        continue;
      }
      bool base = hasRun(line.attributes, g.byte, Display::Attribute::RubyBase);
      if (base && !prevBase) {
        baseStart = kept.size();
        rubyOf[*baseStart] = {std::string(), 0};
      }
      if (base) rubyOf[*baseStart].second++;
      prevBase = base;
      kept.push_back(g);
    }
    glyphs = std::move(kept);
    for (auto it = rubyOf.begin(); it != rubyOf.end();) it = it->second.first.empty() ? rubyOf.erase(it) : std::next(it);
  }
  while (!glyphs.empty() && glyphs.back().cp == ' ') glyphs.pop_back();

  // 半角の続く長さ。runLength[i]はglyphs[i]を含む並びの長さ。並びは空白以外の半角で
  // 始まって終わり、途中の半角の空白を含む(2026-09-29、直接のユーザー報告"「image:
  // title4.jpg」の文字間隔が不揃いで閉じブラケットの向きがそのまま"――空白で並びが切れて、
  // 別々に横倒しになっていた)。
  std::vector<std::size_t> runLength(glyphs.size(), 0);
  for (std::size_t i = 0; i < glyphs.size();) {
    if (!isAsciiGraphic(glyphs[i].cp)) {
      i++;
      continue;
    }
    std::size_t j = i, end = i;
    while (j < glyphs.size() && (isAsciiGraphic(glyphs[j].cp) || glyphs[j].cp == ' ')) {
      if (isAsciiGraphic(glyphs[j].cp)) end = j + 1;
      j++;
    }
    for (std::size_t k = i; k < end; k++) runLength[k] = end - i;
    i = end;
  }

  std::vector<Cell> cells;
  int linkRun = -1;
  bool previousUnderline = false;
  for (std::size_t i = 0; i < glyphs.size(); i++) {
    const Glyph &g = glyphs[i];
    Cell cell;
    cell.original = g.cp;
    cell.attribute = attributeAt(line.attributes, g.byte);
    // リンク(下線、EpubVerticalCell::link): 太字等と重なっていても下線を優先し(傍線を描く)、
    // 下線の並びごとに番号を振る。
    bool underline = hasRun(line.attributes, g.byte, Display::Attribute::Underline);
    if (underline) {
      if (!previousUnderline) linkRun++;
      cell.attribute = Display::Attribute::Underline;
      cell.link = linkRun;
    }
    previousUnderline = underline;
    cell.serif = serifAt(line.attributes, g.byte);
    if (auto ruby = rubyOf.find(i); ruby != rubyOf.end()) {
      cell.ruby = ruby->second.first;
      cell.rubySpan = ruby->second.second;
    }
    // 横倒し(sideways、-gのみ、2026-09-29): 3字以上続く半角は2字ずつ1マスに詰める
    // (GraphicalDisplayが90度回して描く、layoutEpubVerticalColumns()の宣言参照)
    if (sideways && runLength[i] > 2) {
      cell.glyph = g.bytes;
      if (i + 1 < glyphs.size() && runLength[i + 1] == runLength[i]) {  // 同じ並びの次の字(空白を含む)
        cell.glyph += glyphs[++i].bytes;
      } else {
        cell.glyph += ' ';
      }
      cells.push_back(cell);
      continue;
    }
    // 縦中横: 半角英数2字まで(2026-09-29、数字だけから英字にも広げた)、"!!" "!?" "?!" "??"
    if (i + 1 < glyphs.size()) {
      const Glyph &h = glyphs[i + 1];
      bool digits = isAsciiAlnum(g.cp) && isAsciiAlnum(h.cp) && runLength[i] == 2;
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

std::vector<EpubVerticalColumn> layoutEpubVerticalColumns(const std::vector<EpubVerticalSourceLine> &lines, int rows,
                                                         bool sideways) {
  int height = std::max(1, rows);
  std::vector<EpubVerticalColumn> cols;
  for (const auto &line : lines) {
    std::vector<Cell> cells = toCells(line, sideways);
    if (cells.empty()) {
      cols.push_back({{}, line.rawLineNumber});
      continue;
    }
    if (line.alignEnd && static_cast<int>(cells.size()) < height) {  // 下端に寄せる(著者名等)
      Cell blank;
      blank.glyph = "  ";
      blank.original = ' ';
      cells.insert(cells.begin(), height - cells.size(), blank);
    }
    std::size_t i = 0;
    Cell blank;
    blank.glyph = "  ";
    blank.original = ' ';
    while (i < cells.size()) {
      // 字下げ(EpubVerticalSourceLine::indentFirst/indentRest): 列の頭を空けて、その分だけ短く
      int indent = std::clamp(i == 0 ? line.indentFirst : line.indentRest, 0, height - 1);
      std::size_t count = std::min<std::size_t>(height - indent, cells.size() - i);
      if (i + count < cells.size()) {
        // 追い出し: 次の列の頭が行頭禁則の字なら、この列の最後の字も次の列へ(2字まで)
        for (int k = 0; k < 2 && count > 1 && isNoStart(cells[i + count].original); k++) count--;
        if (count > 1 && isNoEnd(cells[i + count - 1].original)) count--;
      }
      std::vector<Cell> column(static_cast<std::size_t>(indent), blank);
      column.insert(column.end(), cells.begin() + i, cells.begin() + i + count);
      cols.push_back({std::move(column), line.rawLineNumber});
      i += count;
    }
  }
  return cols;
}

int epubVerticalColumnsPerScreen(int columns) { return std::max(1, columns / kColumnPitch); }

int epubVerticalColumnPitch() { return kColumnPitch; }

int epubVerticalScreenColumn(int k, int columns) { return columns - 3 - k * kColumnPitch; }

std::vector<ContentView::Line> renderEpubVerticalWindow(const std::vector<EpubVerticalColumn> &cols, int first,
                                                        int rows, int columns, bool sideLineMarker,
                                                        int highlightColumn, int shownFrom, int shownUntil,
                                                        int highlightLink) {
  int highlightRaw = (highlightLink >= 0 && highlightColumn >= 0 && highlightColumn < static_cast<int>(cols.size()))
                         ? cols[highlightColumn].rawLineNumber
                         : -1;
  int perScreen = epubVerticalColumnsPerScreen(columns);
  std::vector<ContentView::Line> out;
  for (int r = 0; r < std::max(1, rows); r++) {
    // 画面の桁ごとの中身(2セル幅の字の右半分は空文字)
    std::vector<std::string> screen(std::max(0, columns), " ");
    std::vector<std::optional<Display::Attribute>> attrs(std::max(0, columns));
    std::vector<bool> serifs(std::max(0, columns), false);
    std::vector<std::pair<std::string, int>> rubies(std::max(0, columns));  // 桁ごとのルビ(読み, 親字の行数)
    for (int k = 0; k < perScreen; k++) {
      long c = static_cast<long>(first) + k;
      int x = epubVerticalScreenColumn(k, columns);
      if (x < 0) continue;
      bool highlight = c == highlightColumn && highlightLink < 0;
      if (c < shownFrom || c >= shownUntil) continue;
      if (c < 0 || c >= static_cast<long>(cols.size()) || r >= static_cast<int>(cols[c].cells.size())) continue;
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
      std::optional<Display::Attribute> attribute = cell.attribute;
      if (sideLineMarker && attribute == Display::Attribute::Underline) {
        attribute.reset();
        if (x + 2 < columns) screen[x + 2] = "|";
      }
      // 2026-09-30、直接のユーザー報告("リンクを含む行全体がハイライト表示。アンダーラインのみで
      // よろしく"): リンクは選んでいるリンクの字だけ(highlightLink)、画像の列は列ごと。
      if (highlight || (highlightRaw >= 0 && cols[c].rawLineNumber == highlightRaw && cell.link == highlightLink))
        attribute = Display::Attribute::Standout;
      attrs[x] = attribute;
      if (x + 1 < columns) attrs[x + 1] = attribute;
      serifs[x] = cell.serif;
      if (!cell.ruby.empty()) rubies[x] = {cell.ruby, std::min(cell.rubySpan, std::max(1, rows) - r)};
      if (x + 1 < columns) serifs[x + 1] = cell.serif;
    }
    ContentView::Line line;
    for (int x = 0; x < columns; x++) {
      int start = static_cast<int>(line.text.size());
      if (!rubies[x].first.empty()) line.rubyMarks.push_back({start, rubies[x].first, rubies[x].second, true});
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
    // 明朝体(EpubVerticalCell::serif)は他の装飾と別のランにする(重ねて持てるように)
    for (int x = 0, pos = 0; x < columns; pos += static_cast<int>(screen[x].size()), x++) {
      int end = pos + static_cast<int>(screen[x].size());
      if (!serifs[x] || end <= pos) continue;
      bool extend = false;
      for (auto it = line.attributes.rbegin(); it != line.attributes.rend(); ++it)
        if (it->attribute == Display::Attribute::Serif) {
          if (it->end == pos) {
            it->end = end;
            extend = true;
          }
          break;
        }
      if (!extend) line.attributes.push_back({pos, end, Display::Attribute::Serif});
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
  // 右からk番目の列は桁[columns-3-k*pitch, columns-2-k*pitch](renderEpubVerticalWindow()の宣言参照)
  int fromRight = columns - 1 - screenColumn;
  if (fromRight < 0) return -1;
  if (fromRight % kColumnPitch == 0) return -1;  // 行間
  int k = fromRight / kColumnPitch;
  return k < epubVerticalColumnsPerScreen(columns) ? k : -1;
}

}  // namespace fm
