#include "help_to_epub.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

#include "utf8.hpp"

namespace fm {

namespace {

const char *const kNbsp = "\xC2\xA0";
const char *const kColon = "\xEF\xBC\x9A";  // ：

// Pythonのstr.isspace()の主なもの(ASCIIの空白、U+00A0、U+3000)。空白ならそのバイト数、でなければ0。
int spaceLength(const std::string &s, std::size_t i) {
  if (i >= s.size()) return 0;
  unsigned char c = s[i];
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') return 1;
  if (s.compare(i, 2, kNbsp) == 0) return 2;
  if (s.compare(i, 3, "\xE3\x80\x80") == 0) return 3;
  return 0;
}

int charLength(const std::string &s, std::size_t i) {
  int n = Utf8::sequence_length(static_cast<unsigned char>(s[i]));
  return static_cast<int>(std::min<std::size_t>(n, s.size() - i));
}

std::string strip(const std::string &s) {  // str.strip()
  std::size_t b = 0;
  while (int n = spaceLength(s, b)) b += n;
  std::size_t e = s.size();
  while (e > b) {
    std::size_t p = e - 1;
    while (p > b && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) p--;
    if (!spaceLength(s, p) || p + spaceLength(s, p) != e) break;
    e = p;
  }
  return s.substr(b, e - b);
}

std::string stripNbspAndSpace(std::string s) {  // str.strip('  ')
  for (;;) {
    if (!s.empty() && s[0] == ' ') s.erase(0, 1);
    else if (s.compare(0, 2, kNbsp) == 0) s.erase(0, 2);
    else break;
  }
  for (;;) {
    if (!s.empty() && s.back() == ' ') s.pop_back();
    else if (s.size() >= 2 && s.compare(s.size() - 2, 2, kNbsp) == 0) s.resize(s.size() - 2);
    else break;
  }
  return s;
}

std::string collapseSpaces(const std::string &s) {  // re.sub(r'\s+', ' ', s).strip()
  std::string out;
  for (std::size_t i = 0; i < s.size();) {
    if (int n = spaceLength(s, i)) {
      while (int m = spaceLength(s, i)) i += m;
      out += ' ';
      (void)n;
    } else {
      out += s[i++];
    }
  }
  return strip(out);
}

std::string escapeHtml(const std::string &s) {  // html.escape(quote=False)
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

std::string encodeUtf8(unsigned cp) {
  std::string out;
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
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
  return out;
}

int number(const std::string &arg) {  // "19n" "0" → 19
  int n = 0;
  for (char c : arg) {
    if (c < '0' || c > '9') break;
    n = n * 10 + (c - '0');
  }
  return n;
}

bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }

// roffの1行の切れ端: 字、書体の切り替え(\f[B]・\fB等)、エスケープを外した字(\e・\[u3000]等)。
struct Piece {
  enum Kind { Text, Font, Literal } kind;
  std::string text;
  bool bracket = false;  // \f[...]の形
};

std::vector<Piece> tokenize(const std::string &s) {
  std::vector<Piece> pieces;
  std::string text;
  auto flush = [&] {
    if (!text.empty()) pieces.push_back({Piece::Text, text});
    text.clear();
  };
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] != '\\' || i + 1 >= s.size()) {
      text += s[i++];
      continue;
    }
    char c = s[i + 1];
    if (c == 'f' && i + 2 < s.size() && s[i + 2] == '[') {
      std::size_t close = s.find(']', i + 3);
      if (close != std::string::npos && close > i + 3) {
        std::string name = s.substr(i + 3, close - i - 3);
        bool upper = true;
        for (char ch : name) upper = upper && isUpper(ch);
        if (upper) {
          flush();
          pieces.push_back({Piece::Font, name, true});
          i = close + 1;
          continue;
        }
      }
    }
    if (c == 'f' && i + 2 < s.size() && std::string("BIRP").find(s[i + 2]) != std::string::npos) {
      flush();
      pieces.push_back({Piece::Font, std::string(1, s[i + 2])});
      i += 3;
      continue;
    }
    if (s.compare(i, 14, "\\*[usilic-tab]") == 0) {
      flush();
      pieces.push_back({Piece::Literal, ""});
      i += 14;
      continue;
    }
    if (c == 'e' || c == '&' || c == '-') {
      flush();
      pieces.push_back({Piece::Literal, c == 'e' ? "\\" : c == '-' ? "-" : ""});
      i += 2;
      continue;
    }
    if (c == '[' && i + 2 < s.size() && s[i + 2] == 'u') {
      std::size_t j = i + 3;
      while (j < s.size() && j - (i + 3) < 5 && std::isxdigit(static_cast<unsigned char>(s[j]))) j++;
      std::size_t digits = j - (i + 3);
      if (digits >= 4 && j < s.size() && s[j] == ']') {
        flush();
        pieces.push_back({Piece::Literal, encodeUtf8(static_cast<unsigned>(std::stoul(s.substr(i + 3, digits), nullptr, 16)))});
        i = j + 1;
        continue;
      }
    }
    if (c == '\n') {
      text += s[i++];
      continue;
    }
    flush();  // \\(.): 次の1字そのもの
    int n = charLength(s, i + 1);
    pieces.push_back({Piece::Literal, s.substr(i + 1, n)});
    i += 1 + n;
  }
  flush();
  return pieces;
}

std::string plain(const std::string &s) {  // エスケープを外した文字列(見出し名用)
  std::string out;
  for (const auto &p : tokenize(s))
    if (p.kind != Piece::Font) out += p.text;
  return out;
}

// 1行をXHTMLへ。タブは8桁ごと(.ta T 8n)、行頭の空白・2つ以上続く空白はnbsp。
std::string inlineHtml(const std::vector<Piece> &pieces, int col0) {
  std::string out;
  std::string font = "R", prev = "R", opened;
  int col = col0;
  auto emitText = [&](const std::string &t) {
    std::string buf;
    for (std::size_t i = 0; i < t.size();) {
      if (t[i] == '\t') {
        int n = 8 - (col % 8);
        buf.append(n, ' ');
        col += n;
        i++;
        continue;
      }
      int len = charLength(t, i);
      buf.append(t, i, len);
      col += Utf8::display_width(reinterpret_cast<const unsigned char *>(t.data() + i), len);
      i += len;
    }
    return escapeHtml(buf);
  };
  for (const auto &p : pieces) {
    if (p.kind != Piece::Font) {
      out += emitText(p.text);
      continue;
    }
    std::string f = p.text;
    if (f == "JPM") {  // 濁点の字の書体(groff用)。見た目の太字/斜体は変えない
      prev = font;
      continue;
    }
    if (f == "R" && p.bracket) continue;  // \f[R]は\f[JPM]の後始末
    if (f == "P") f = prev;
    if (!opened.empty()) {
      out += "</" + opened + ">";
      opened.clear();
    }
    font = f;
    if (f == "B") opened = "b";
    else if (f == "I") opened = "i";
    if (!opened.empty()) out += "<" + opened + ">";
  }
  if (!opened.empty()) out += "</" + opened + ">";
  // 行頭の空白と、桁をそろえるための2つ以上の空白はnbspにする(詰められないように)
  std::string result;
  std::size_t i = 0;
  while (i < out.size() && out[i] == ' ') {
    result += kNbsp;
    i++;
  }
  while (i < out.size()) {
    if (out[i] == ' ') {
      std::size_t j = i;
      while (j < out.size() && out[j] == ' ') j++;
      if (j - i >= 2)
        for (std::size_t k = i; k < j; k++) result += kNbsp;
      else
        result += ' ';
      i = j;
    } else {
      result += out[i++];
    }
  }
  return result;
}

std::string inlineHtml(const std::string &s, int col0) { return inlineHtml(tokenize(s), col0); }
std::string inlineText(const std::string &s) { return inlineHtml({{Piece::Text, s}}, 0); }

std::string removeAll(std::string s, const std::string &what) {
  for (std::size_t p; (p = s.find(what)) != std::string::npos;) s.erase(p, what.size());
  return s;
}

// 段落の行が等幅で組むべきものか(行頭が空白、空白でない字の後に空白2つ以上、タブ)。
bool looksPreformatted(const std::string &raw) {
  if (spaceLength(raw, 0)) return true;
  if (raw.find('\t') != std::string::npos) return true;
  bool prevNonSpace = false;
  for (std::size_t i = 0; i < raw.size();) {
    if (prevNonSpace && raw.compare(i, 2, "  ") == 0) return true;
    int n = spaceLength(raw, i);
    prevNonSpace = (n == 0);
    i += n ? n : charLength(raw, i);
  }
  return false;
}

struct Block {
  enum Kind { Paragraph, Space, Heading } kind;
  Block(Kind k) : kind(k) {}  // NOLINT(google-explicit-constructor)
  int level = 0;     // Heading: 2(章) / 3(字下げのある見出し)
  int indent = 0;    // Paragraph
  int first = 0;     // Paragraph: 1行目の字下げ
  std::string title;  // Heading: 目次の文字列
  std::string html;
  std::string raw;   // Paragraph: エスケープを外した文字列
};

struct Chapter {
  std::string title;
  std::vector<Block> blocks;
  std::vector<std::string> subs;
  std::string file;
};

std::string xhtml(const std::string &title, const std::string &body) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" xml:lang=\"ja\" "
         "lang=\"ja\">\n<head><meta charset=\"UTF-8\"/><title>" +
         escapeHtml(title) +
         "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"../Style/style.css\"/></head>\n<body>\n" + body +
         "</body>\n</html>\n";
}

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  for (;;) {
    std::size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

std::string formatG(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%g", v);
  return buf;
}

// zip(無圧縮)。EPUBはmimetypeを先頭に無圧縮で置く決まり、他も無圧縮で読める。
class ZipWriter {
 public:
  void add(const std::string &name, const std::string &data) {
    Entry e{name, crc32(data), static_cast<std::uint32_t>(data.size()), static_cast<std::uint32_t>(out_.size())};
    put32(0x04034b50);
    header(e);
    out_ += name;
    out_ += data;
    entries_.push_back(e);
  }
  std::string finish() {
    std::uint32_t central = static_cast<std::uint32_t>(out_.size());
    for (const auto &e : entries_) {
      put32(0x02014b50);
      put16(20);  // made by
      header(e);
      put16(0);   // comment
      put16(0);   // disk
      put16(0);   // internal attributes
      put32(0);   // external attributes
      put32(e.offset);
      out_ += e.name;
    }
    std::uint32_t size = static_cast<std::uint32_t>(out_.size()) - central;
    put32(0x06054b50);
    put16(0);
    put16(0);
    put16(static_cast<std::uint16_t>(entries_.size()));
    put16(static_cast<std::uint16_t>(entries_.size()));
    put32(size);
    put32(central);
    put16(0);
    return out_;
  }
  ZipWriter() {
    std::time_t now = std::time(nullptr);
    std::tm t = *std::localtime(&now);
    dosTime_ = static_cast<std::uint16_t>((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
    dosDate_ = static_cast<std::uint16_t>(((t.tm_year - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
  }

 private:
  struct Entry {
    std::string name;
    std::uint32_t crc, size, offset;
  };
  void put16(std::uint16_t v) {
    out_ += static_cast<char>(v & 0xFF);
    out_ += static_cast<char>(v >> 8);
  }
  void put32(std::uint32_t v) {
    put16(static_cast<std::uint16_t>(v & 0xFFFF));
    put16(static_cast<std::uint16_t>(v >> 16));
  }
  // ローカルヘッダと中央ディレクトリで共通の部分(version needed〜extra length)。
  void header(const Entry &e) {
    put16(20);      // version needed
    put16(0x0800);  // UTF-8の名前
    put16(0);       // 無圧縮
    put16(dosTime_);
    put16(dosDate_);
    put32(e.crc);
    put32(e.size);
    put32(e.size);
    put16(static_cast<std::uint16_t>(e.name.size()));
    put16(0);
  }
  static std::uint32_t crc32(const std::string &data) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
      for (std::uint32_t i = 0; i < 256; i++) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
      }
      ready = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
  }
  std::string out_;
  std::vector<Entry> entries_;
  std::uint16_t dosTime_ = 0, dosDate_ = 0;
};

}  // namespace

std::string convertMsRoffToHelpEpub(const std::string &roff, const std::string &usilicrcText,
                                    const std::string &notesText, std::string *summary,
                                    std::vector<std::string> *unnoted) {
  std::vector<std::string> lines = splitLines(roff);

  // 本文の始まり(前置きの後の最初の文字の行)
  std::size_t start = 0;
  while (start < lines.size() && (lines[start].empty() || lines[start][0] == '.')) start++;
  int ind = 0;
  std::optional<int> ti;
  std::vector<Block> blocks;
  std::string lastName;
  const std::string tab = "\\*[usilic-tab]";
  for (std::size_t li = start; li < lines.size(); li++) {
    const std::string &l = lines[li];
    if (!l.empty() && l[0] == '.') {
      std::istringstream in(l);
      std::string req, arg;
      in >> req >> arg;
      if (req == ".in") ind = number(arg.empty() ? "0" : arg);
      else if (req == ".ti") ti = number(arg.empty() ? "0" : arg);
      else if (req == ".sp") blocks.push_back({Block::Space});
      continue;
    }
    if (strip(l).empty()) {
      blocks.push_back({Block::Space});
      continue;
    }
    int first = ti ? *ti : ind;
    bool tabHead = l.size() >= tab.size() && l.compare(l.size() - tab.size(), tab.size(), tab) == 0 &&
                   l.compare(0, tab.size(), tab) != 0;
    bool startsIndented = l[0] == ' ' || l[0] == '\t' || l.compare(0, 2, "\\*") == 0;
    if (tabHead || (first == 0 && ind == 0 && !startsIndented)) {
      std::string p = strip(plain(l));
      Block h{Block::Heading};
      std::size_t colon = p.find(kColon);
      if (colon != std::string::npos) {
        std::string name = collapseSpaces(p.substr(0, colon));
        std::string desc = p.substr(colon + 3);
        if (name.empty()) {  // 2行に分けた見出し(F/EGREP)は、前の行の名前を使い、前の行は見出しへまとめる
          if (!blocks.empty() && blocks.back().kind == Block::Paragraph &&
              blocks.back().raw.find(kColon) != std::string::npos) {
            const std::string &raw = blocks.back().raw;
            name = collapseSpaces(raw.substr(0, raw.find(kColon)));
            blocks.pop_back();
          } else {
            name = lastName;
          }
        }
        h.title = name + kColon + strip(desc);
        lastName = name;
        std::string body = inlineHtml(removeAll(l, tab), 0);
        // 「：」の前後のnbspを外す
        std::string fixed;
        for (std::size_t i = 0; i < body.size();) {
          if (body.compare(i, 3, kColon) == 0) {
            while (fixed.size() >= 2 && fixed.compare(fixed.size() - 2, 2, kNbsp) == 0) fixed.resize(fixed.size() - 2);
            fixed += kColon;
            i += 3;
            while (body.compare(i, 2, kNbsp) == 0) i += 2;
          } else {
            fixed += body[i++];
          }
        }
        body = stripNbspAndSpace(fixed);
        if (body.compare(0, 3, kColon) == 0) body = "<b>" + escapeHtml(name) + "</b>" + body;
        h.html = body;
      } else {
        h.title = p;
        h.html = stripNbspAndSpace(inlineHtml(removeAll(l, tab), 0));
      }
      h.level = first == 0 ? 2 : 3;
      blocks.push_back(h);
    } else {
      Block b{Block::Paragraph};
      b.indent = ind;
      b.first = first;
      b.html = inlineHtml(l, first);
      b.raw = plain(l);
      blocks.push_back(b);
    }
    ti.reset();
  }

  // 章(行頭の見出し)ごとにファイルを分ける
  std::vector<Chapter> chapters;
  for (const auto &b : blocks) {
    if (b.kind == Block::Heading && b.level == 2) {
      chapters.push_back({b.title, {b}, {}, ""});
    } else if (!chapters.empty()) {
      chapters.back().blocks.push_back(b);
      if (b.kind == Block::Heading) chapters.back().subs.push_back(b.title);
    }
  }

  // usilicrcの設定項目(行頭が英大文字の見出し)ごとの、見出しの直後に続く「#」の行(注釈の中身)。
  std::map<std::string, std::vector<std::string>> rcComments;
  {
    std::string item;
    for (const auto &l : splitLines(usilicrcText)) {
      std::size_t n = 0;
      while (n < l.size() && (isUpper(l[n]) || (n > 0 && l[n] == '-'))) n++;
      bool heading = n > 0 && strip(l.substr(n)).empty();
      if (heading) {
        item = strip(l);
        rcComments[item].clear();
      } else if (!item.empty() && !l.empty() && l[0] == '#') {
        std::string c = l.substr(1);
        if (!c.empty() && c[0] == ' ') c.erase(0, 1);
        rcComments[item].push_back(c);
      } else {
        item.clear();
      }
    }
  }
  // HELP.notes: "@設定項目"(例 "@URL")の行の後に注釈の文を書くと、usilicrcのコメントの代わりにそれを使う。
  std::map<std::string, std::vector<std::string>> written;
  {
    std::optional<std::string> key;
    for (const auto &l : splitLines(notesText)) {
      if (!l.empty() && l[0] == '#') continue;
      if (!l.empty() && l[0] == '@') {
        key = strip(l.substr(1));
        written[*key].clear();
      } else if (key) {
        written[*key].push_back(l);
      }
    }
  }
  // 2026-10-18、直接のユーザー依頼(斜体に注釈が無ければ、その場でHELP.notesへ追記するため): HELP.notesの本文の無い
  // "@名前"は「注釈は要らない」の印(アンダーラインの代わりの斜体のman(1)等)。usilicrcにコメントがあっても注釈にしない。
  auto optedOut = [&](const std::string &item) {
    auto w = written.find(item);
    if (w == written.end()) return false;
    for (const auto &l : w->second)
      if (!strip(l).empty()) return false;
    return true;
  };
  auto hasNote = [&](const std::string &item) {
    if (optedOut(item)) return false;
    auto r = rcComments.find(item);
    auto w = written.find(item);
    return (r != rcComments.end() && !r->second.empty()) || (w != written.end() && !w->second.empty());
  };
  std::vector<std::string> missing;  // 注釈の無い斜体の名前(unnoted参照)
  // 全角の英数字の名前(<i>ＥＧＲＥＰ</i>)は半角にして探す。
  auto halfWidth = [](const std::string &t) {
    std::string r;
    for (std::size_t k = 0; k < t.size(); k++) {
      auto c = static_cast<unsigned char>(t[k]);
      if (c == 0xEF && k + 2 < t.size()) {
        auto b1 = static_cast<unsigned char>(t[k + 1]), b2 = static_cast<unsigned char>(t[k + 2]);
        if (b1 == 0xBC && b2 >= 0x90 && b2 <= 0xBF) {  // ０-９、Ａ-Ｚ等(U+FF10..FF3F)
          r += static_cast<char>(b2 - 0x90 + 0x30);
          k += 2;
          continue;
        }
        if (b1 == 0xBD && b2 >= 0x80 && b2 <= 0x9A) {  // ｀、ａ-ｚ(U+FF40..FF5A)
          r += static_cast<char>(b2 - 0x80 + 0x60);
          k += 2;
          continue;
        }
      }
      r += t[k];
    }
    return r;
  };
  // 一覧(unnoted)に出すのは名前らしい斜体(英数字・-・_だけ)。パス・man(1)・拡張子等のアンダーラインの代わりは出さない。
  auto nameLike = [](const std::string &t) {
    bool letter = false;
    for (char c : t) {
      if (std::isalpha(static_cast<unsigned char>(c))) letter = true;
      else if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    }
    return letter;
  };

  std::vector<std::string> noteOrder;                 // 設定項目(最初に参照した順)
  std::map<std::string, std::string> noteIds;         // 設定項目 -> 注釈のid
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> backrefs;  // 注釈のid -> (章のファイル, 参照のid)
  int refCount = 0;
  // 「usilicrc の URL」「usilicrc の <b>EGREP</b>」の設定項目の名前を注釈へのリンクにする。
  auto linkRefs = [&](const std::string &h, const std::string &fname) {
    const std::string lead = "<i>usilicrc</i>";
    std::string out;
    std::size_t pos = 0;
    for (;;) {
      std::size_t m = h.find(lead, pos);
      if (m == std::string::npos) break;
      std::size_t i = m + lead.size();
      while (int n = spaceLength(h, i)) i += n;
      bool ok = h.compare(i, 3, "\xE3\x81\xAE") == 0;  // の
      std::size_t afterNo = i + 3;
      if (ok) {
        i = afterNo;
        while (int n = spaceLength(h, i)) i += n;
      }
      std::size_t group1End = i;
      bool bold = ok && h.compare(i, 3, "<b>") == 0;
      if (bold) i += 3;
      std::size_t itemStart = i;
      std::size_t j = i;
      if (ok && j < h.size() && isUpper(h[j]))
        while (j < h.size() && (isUpper(h[j]) || h[j] == '-')) j++;
      while (j > itemStart && h[j - 1] == '-') j--;
      if (!ok || j - itemStart < 2) {
        out += h.substr(pos, m + lead.size() - pos);
        pos = m + lead.size();
        continue;
      }
      std::string item = h.substr(itemStart, j - itemStart);
      bool closeBold = h.compare(j, 4, "</b>") == 0;
      std::size_t end = j + (closeBold ? 4 : 0);
      if (!hasNote(item)) {
        out += h.substr(pos, end - pos);
        pos = end;
        continue;
      }
      if (!noteIds.count(item)) {
        noteIds[item] = "n" + std::to_string(noteIds.size() + 1);
        noteOrder.push_back(item);
      }
      std::string nid = noteIds[item];
      std::string rid = "r" + std::to_string(++refCount);
      backrefs[nid].push_back({fname, rid});
      out += h.substr(pos, group1End - pos);
      out += "<a id=\"" + rid + "\" href=\"notes.xhtml#" + nid + "\" epub:type=\"noteref\">" + (bold ? "<b>" : "") +
             item + (closeBold ? "</b>" : "") + "</a>";
      pos = end;
    }
    out += h.substr(pos);
    // 2026-10-17、直接のユーザー依頼("斜体箇所が注釈だがそれにそってnoteを更新してみて。narou2epub も入れてみたけど
    // noteにできる？"): HELPの原稿は「usilicrc の」を付けずに、設定項目の名前を斜体(全角の英字)で書くようになった。
    // 斜体の名前が注釈のある項目(usilicrcのコメントかHELP.notesの文、narou2epub等のusilicrcに無い名前はHELP.notesの
    // @の行だけ)なら注釈へのリンクにする。ただし「」の中(「SHELL busy」等の出力するメッセージ)と、名前の後に「 :」が
    // 続く所(冒頭の表記の一覧の「SHELL : 仮想端末」等)は名前の説明なのでリンクにしない。
    std::string linked;
    pos = 0;
    for (;;) {
      std::size_t m = out.find("<i>", pos);
      if (m == std::string::npos) break;
      std::size_t close = out.find("</i>", m + 3);
      if (close == std::string::npos) break;
      std::string shown = out.substr(m + 3, close - m - 3);
      std::string item = halfWidth(shown);
      std::size_t end = close + 4;
      bool inQuote = m >= 3 && out.compare(m - 3, 3, "\xE3\x80\x8C") == 0;  // 「
      // 表記の一覧の「SHELL : 仮想端末」(名前の後に「 :」)
      std::size_t after = end;
      while (int n = spaceLength(out, after)) after += n;
      bool legend = out.compare(after, 1, ":") == 0;
      bool inLink = out.rfind("<a ", m) != std::string::npos &&
                    (out.rfind("</a>", m) == std::string::npos || out.rfind("</a>", m) < out.rfind("<a ", m));
      bool candidate = !item.empty() && item.find('<') == std::string::npos && !inQuote && !legend && !inLink;
      if (candidate && nameLike(item) && !hasNote(item) && !optedOut(item) &&
          std::find(missing.begin(), missing.end(), item) == missing.end())
        missing.push_back(item);
      if (!candidate || !hasNote(item)) {
        linked += out.substr(pos, end - pos);
        pos = end;
        continue;
      }
      if (!noteIds.count(item)) {
        noteIds[item] = "n" + std::to_string(noteIds.size() + 1);
        noteOrder.push_back(item);
      }
      std::string nid = noteIds[item];
      std::string rid = "r" + std::to_string(++refCount);
      backrefs[nid].push_back({fname, rid});
      linked += out.substr(pos, m - pos);
      linked += "<a id=\"" + rid + "\" href=\"notes.xhtml#" + nid + "\" epub:type=\"noteref\"><i>" + shown + "</i></a>";
      pos = end;
    }
    linked += out.substr(pos);
    return linked;
  };

  std::map<std::pair<int, int>, std::string> classes;
  std::vector<std::pair<std::string, std::string>> files;
  int paragraphCount = 0;
  for (std::size_t n = 0; n < chapters.size(); n++) {
    char fname[32];
    std::snprintf(fname, sizeof fname, "ch%02zu.xhtml", n + 1);
    std::string body;
    int sub = 0;
    for (const auto &b : chapters[n].blocks) {
      if (b.kind == Block::Space) {
        // 空行(HELPの「#」の行、.sp)は、余白(margin)ではなく空の行にする(usilicのEPUB表示は余白を読まない)
        body += "<p class=\"blank\"><br/></p>\n";
        continue;
      }
      if (b.kind == Block::Heading) {
        std::string tag = "h" + std::to_string(b.level);
        std::string id = b.level == 3 ? "s" + std::to_string(sub++) : "top";
        body += "<" + tag + " id=\"" + id + "\">" + b.html + "</" + tag + ">\n";
        continue;
      }
      paragraphCount++;
      std::string cls = "i" + std::to_string(b.indent) + "t" + std::to_string(b.first);
      classes[{b.indent, b.first}] = cls;
      if (looksPreformatted(b.raw)) cls += " pre";
      std::string h = linkRefs(b.html, fname);
      body += "<p class=\"" + cls + "\">" + (stripNbspAndSpace(h).empty() ? std::string(kNbsp) : h) + "</p>\n";
    }
    files.push_back({std::string("Text/") + fname, xhtml(chapters[n].title, body)});
  }

  // 巻末の注釈。注釈の頭に、参照した所へ戻るリンク(複数なら↩1 ↩2…)。中身はusilicrcのその項目の
  // コメント(字下げ・桁そろえを保つので等幅で1行ずつ)、HELP.notesに書いてあればその文。
  std::string asides;
  for (const auto &item : noteOrder) {
    const std::string &nid = noteIds[item];
    const auto &refs = backrefs[nid];
    std::string back;
    for (std::size_t k = 0; k < refs.size(); k++) {
      if (k) back += ' ';
      back += "<a epub:type=\"noteref\" href=\"" + refs[k].first + "#" + refs[k].second + "\">\xE2\x86\xA9" +
              (refs.size() > 1 ? std::to_string(k + 1) : "") + "</a>";
    }
    std::string inner;
    auto w = written.find(item);
    if (w != written.end()) {
      std::vector<std::string> paragraph;
      auto flushParagraph = [&] {
        std::string t;
        for (std::size_t k = 0; k < paragraph.size(); k++) t += (k ? "<br/>" : "") + escapeHtml(paragraph[k]);
        if (!strip(t).empty()) inner += "<p>" + t + "</p>\n";
        paragraph.clear();
      };
      std::vector<std::string> body = w->second;
      while (!body.empty() && strip(body.front()).empty()) body.erase(body.begin());
      while (!body.empty() && strip(body.back()).empty()) body.pop_back();
      for (const auto &l : body) {
        if (strip(l).empty()) flushParagraph();
        else paragraph.push_back(l);
      }
      flushParagraph();
    } else {
      std::vector<std::string> rc = rcComments[item];
      while (!rc.empty() && strip(rc.back()).empty()) rc.pop_back();
      for (const auto &l : rc) {
        std::string t = inlineText(l);
        inner += "<p class=\"rc\">" + (t.empty() ? std::string(kNbsp) : t) + "</p>\n";
      }
    }
    // usilicrcに無い名前(narou2epub等、HELP.notesの文だけ)は、見出しに「usilicrc の」を付けない。
    std::string title = rcComments.count(item) ? " <i>usilicrc</i> \xE3\x81\xAE <b>" + escapeHtml(item) + "</b>"
                                               : " <b>" + escapeHtml(item) + "</b>";
    asides += "<aside id=\"" + nid + "\" epub:type=\"footnote\">\n<p class=\"nh\">" + back + title + "</p>\n" + inner +
              "</aside>\n";
  }
  if (!asides.empty()) {
    chapters.push_back({"\xE6\xB3\xA8\xE9\x87\x88", {}, {}, "notes"});  // 注釈
    files.push_back({"Text/notes.xhtml", xhtml("\xE6\xB3\xA8\xE9\x87\x88",
                                               "<h2 id=\"top\">\xE6\xB3\xA8\xE9\x87\x88</h2>\n" + asides)});
  }

  std::string css =
      "html { writing-mode: horizontal-tb; }\n"
      "body { font-family: serif; line-height: 1.7; margin: 0 1em; }\n"
      "h2 { font-size: 140%; font-family: sans-serif; margin: 1em 0 0.5em; }\n"
      "h3 { font-size: 115%; font-family: sans-serif; margin: 1em 0 0.3em; padding-left: 2em; }\n"
      "p { margin: 0; }\n"
      "p.nh { margin-top: 1.2em; }\n"
      "aside { margin: 0 0 1em; }\n"
      "p.rc { font-family: \"Osaka-Mono\", \"MS Gothic\", \"Noto Sans Mono CJK JP\", monospace; padding-left: 1em; }\n"
      "p.pre { font-family: \"Osaka-Mono\", \"MS Gothic\", \"Noto Sans Mono CJK JP\", monospace; }\n";
  for (const auto &kv : classes)
    css += "p." + kv.second + " { padding-left: " + formatG(kv.first.first * 0.5) +
           "em; text-indent: " + formatG((kv.first.second - kv.first.first) * 0.5) + "em; }\n";
  files.push_back({"Style/style.css", css});

  auto chapterFile = [](const Chapter &ch, std::size_t n) {
    if (!ch.file.empty()) return ch.file;
    char buf[16];
    std::snprintf(buf, sizeof buf, "ch%02zu", n + 1);
    return std::string(buf);
  };
  std::string navItems;
  for (std::size_t n = 0; n < chapters.size(); n++) {
    std::string item = "<li><a href=\"" + chapterFile(chapters[n], n) + ".xhtml#top\">" + escapeHtml(chapters[n].title) + "</a>";
    if (!chapters[n].subs.empty()) {
      item += "\n<ol>";
      for (std::size_t k = 0; k < chapters[n].subs.size(); k++)
        item += "<li><a href=\"" + chapterFile(chapters[n], n) + ".xhtml#s" + std::to_string(k) + "\">" +
                escapeHtml(chapters[n].subs[k]) + "</a></li>";
      item += "</ol>\n";
    }
    navItems += item + "</li>\n";
  }
  files.push_back({"Text/nav.xhtml", xhtml("\xE7\x9B\xAE\xE6\xAC\xA1",  // 目次
                                           "<nav epub:type=\"toc\" id=\"toc\">\n<h2>\xE7\x9B\xAE\xE6\xAC\xA1</h2>\n<ol>\n" +
                                               navItems + "</ol>\n</nav>\n")});

  // .TH USILIC 1 "日付" "版" ...
  std::string date, version = "usilic";
  for (const auto &l : lines) {
    if (l.compare(0, 4, ".TH ") != 0) continue;
    std::vector<std::string> quoted;
    for (std::size_t q = l.find('"'); q != std::string::npos && quoted.size() < 2;) {
      std::size_t e = l.find('"', q + 1);
      if (e == std::string::npos) break;
      quoted.push_back(l.substr(q + 1, e - q - 1));
      q = l.find('"', e + 1);
    }
    if (quoted.size() == 2) {
      date = plain(quoted[0]);
      version = plain(quoted[1]);
    }
    break;
  }
  char modified[32];
  std::time_t now = std::time(nullptr);
  std::strftime(modified, sizeof modified, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  std::string manifest =
      "<item id=\"nav\" href=\"Text/nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n"
      "<item id=\"css\" href=\"Style/style.css\" media-type=\"text/css\"/>";
  std::string spine;  // navは入れない(目次のページは出さない)
  for (std::size_t n = 0; n < chapters.size(); n++) {
    std::string f = chapterFile(chapters[n], n);
    manifest += "\n<item id=\"" + f + "\" href=\"Text/" + f + ".xhtml\" media-type=\"application/xhtml+xml\"/>";
    spine += (n ? "\n" : "") + std::string("<itemref idref=\"") + f + "\"/>";
  }
  std::string opf =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"id\" xml:lang=\"ja\">\n"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
      "<dc:identifier id=\"id\">urn:uuid:b9e22b63-e1df-51a5-aced-609cf3c8f37c</dc:identifier>\n"
      "<dc:title>usilic HELP</dc:title>\n"
      "<dc:creator>\xE8\xAA\x8C\xE7\x92\x83\xE7\xA9\xBA\xE3\x83\xA6\xE3\x82\xA6</dc:creator>\n"  // 誌璃空ユウ
      "<dc:language>ja</dc:language>\n"
      "<dc:description>" + escapeHtml(version + " " + date) + "(usilic -e)</dc:description>\n"
      "<meta property=\"dcterms:modified\">" + modified + "</meta>\n"
      "</metadata>\n<manifest>\n" + manifest + "\n</manifest>\n"
      "<spine page-progression-direction=\"ltr\">\n" + spine + "\n</spine>\n</package>\n";
  files.push_back({"content.opf", opf});

  ZipWriter zip;
  zip.add("mimetype", "application/epub+zip");
  zip.add("META-INF/container.xml",
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
          "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
          "</rootfiles>\n</container>\n");
  for (const auto &f : files) zip.add("OEBPS/" + f.first, f.second);
  if (summary)
    *summary = std::to_string(chapters.size()) + " chapters, " + std::to_string(paragraphCount) + " paragraphs, " +
               std::to_string(noteOrder.size()) + " notes (" + std::to_string(refCount) + " refs)";
  if (unnoted) *unnoted = missing;
  return zip.finish();
}

}  // namespace fm
