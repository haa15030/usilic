#include "epub_to_help_markup.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

namespace fm {

namespace {

// ---- XMLの簡易スキャナ -------------------------------------------------
// EPUBのcontainer.xml/OPF/nav/ncx/XHTMLは整形式XMLなので、要素の入れ子を
// 検証しない逐次のトークン化で足りる。名前は接頭辞(dc:、epub:、opf:等)を
// 落として小文字にする(ncxのnavPoint等も小文字で比較する)。

struct XmlToken {
  enum Kind { Text, Open, Close, SelfClose } kind = Text;
  std::string name;                          // Open/Close/SelfClose
  std::map<std::string, std::string> attrs;  // Open/SelfClose
  std::string text;                          // Text(実体参照は解決済み)
};

void appendUtf8(std::string &out, unsigned long cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x110000) {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string decodeEntities(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); i++) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    std::size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 12) {
      out += '&';
      continue;
    }
    std::string name = s.substr(i + 1, semi - i - 1);
    if (!name.empty() && name[0] == '#') {
      bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
      char *end = nullptr;
      unsigned long cp = std::strtoul(name.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
      if (end && *end == '\0' && cp) {
        appendUtf8(out, cp);
        i = semi;
        continue;
      }
    }
    static const std::map<std::string, std::string> kNamed = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "}};
    auto it = kNamed.find(name);
    if (it == kNamed.end()) {
      out += '&';
      continue;
    }
    out += it->second;
    i = semi;
  }
  return out;
}

std::string localName(std::string name) {
  std::size_t colon = name.find(':');
  if (colon != std::string::npos) name.erase(0, colon + 1);
  for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return name;
}

bool isXmlSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::vector<XmlToken> tokenizeXml(const std::string &s) {
  std::vector<XmlToken> tokens;
  std::size_t i = 0, n = s.size();
  auto pushText = [&](std::string text) {
    if (text.empty()) return;
    XmlToken t;
    t.text = std::move(text);
    tokens.push_back(std::move(t));
  };
  while (i < n) {
    if (s[i] != '<') {
      std::size_t lt = s.find('<', i);
      if (lt == std::string::npos) lt = n;
      pushText(decodeEntities(s.substr(i, lt - i)));
      i = lt;
      continue;
    }
    if (s.compare(i, 4, "<!--") == 0) {
      std::size_t end = s.find("-->", i + 4);
      i = (end == std::string::npos) ? n : end + 3;
      continue;
    }
    if (s.compare(i, 9, "<![CDATA[") == 0) {
      std::size_t end = s.find("]]>", i + 9);
      if (end == std::string::npos) end = n;
      pushText(s.substr(i + 9, end - i - 9));
      i = (end == n) ? n : end + 3;
      continue;
    }
    if (s.compare(i, 2, "<?") == 0) {
      std::size_t end = s.find("?>", i + 2);
      i = (end == std::string::npos) ? n : end + 2;
      continue;
    }
    if (s.compare(i, 2, "<!") == 0) {  // DOCTYPE(内部サブセット"[...]"を含み得る)
      int bracket = 0;
      std::size_t j = i + 2;
      for (; j < n; j++) {
        if (s[j] == '[') bracket++;
        else if (s[j] == ']') bracket--;
        else if (s[j] == '>' && bracket <= 0) break;
      }
      i = (j < n) ? j + 1 : n;
      continue;
    }
    std::size_t j = i + 1;
    XmlToken t;
    t.kind = XmlToken::Open;
    if (j < n && s[j] == '/') {
      t.kind = XmlToken::Close;
      j++;
    }
    std::size_t nameStart = j;
    while (j < n && !isXmlSpace(s[j]) && s[j] != '>' && s[j] != '/') j++;
    t.name = localName(s.substr(nameStart, j - nameStart));
    while (j < n) {
      while (j < n && isXmlSpace(s[j])) j++;
      if (j >= n) break;
      if (s[j] == '>') {
        j++;
        break;
      }
      if (s[j] == '/') {
        if (t.kind == XmlToken::Open) t.kind = XmlToken::SelfClose;
        j++;
        continue;
      }
      std::size_t attrStart = j;
      while (j < n && !isXmlSpace(s[j]) && s[j] != '=' && s[j] != '>' && s[j] != '/') j++;
      std::string attrName = localName(s.substr(attrStart, j - attrStart));
      while (j < n && isXmlSpace(s[j])) j++;
      std::string value;
      if (j < n && s[j] == '=') {
        j++;
        while (j < n && isXmlSpace(s[j])) j++;
        if (j < n && (s[j] == '"' || s[j] == '\'')) {
          char quote = s[j++];
          std::size_t end = s.find(quote, j);
          if (end == std::string::npos) end = n;
          value = decodeEntities(s.substr(j, end - j));
          j = (end == n) ? n : end + 1;
        } else {
          std::size_t valueStart = j;
          while (j < n && !isXmlSpace(s[j]) && s[j] != '>') j++;
          value = decodeEntities(s.substr(valueStart, j - valueStart));
        }
      }
      if (!attrName.empty()) t.attrs.emplace(attrName, value);
    }
    tokens.push_back(std::move(t));
    i = j;
  }
  return tokens;
}

std::string attr(const XmlToken &t, const std::string &name) {
  auto it = t.attrs.find(name);
  return it == t.attrs.end() ? std::string() : it->second;
}

std::string trimAsciiSpace(const std::string &s) {
  std::size_t b = 0, e = s.size();
  while (b < e && isXmlSpace(s[b])) b++;
  while (e > b && isXmlSpace(s[e - 1])) e--;
  return s.substr(b, e - b);
}

// 連続するASCII空白を1個のスペースへ(ラベル・書名用)。
std::string collapseSpace(const std::string &s) {
  std::string out;
  bool space = false;
  for (char c : trimAsciiSpace(s)) {
    if (isXmlSpace(c)) {
      space = true;
      continue;
    }
    if (space) out += ' ';
    space = false;
    out += c;
  }
  return out;
}

// ---- パス ---------------------------------------------------------------

std::string directoryOf(const std::string &path) {
  std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string percentDecode(const std::string &s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

// baseDir(末尾'/'付き、またはzipの最上位なら"")からの相対hrefを、zip内の
// 正規化したパスへ。フラグメント("#...")は*fragmentへ分ける。
std::string resolveHref(const std::string &baseDir, const std::string &href, std::string *fragment = nullptr) {
  std::string path = href;
  std::size_t hash = path.find('#');
  if (fragment) *fragment = (hash == std::string::npos) ? std::string() : percentDecode(path.substr(hash + 1));
  if (hash != std::string::npos) path.erase(hash);
  path = percentDecode(path);
  std::string joined = (!path.empty() && path[0] == '/') ? path.substr(1) : baseDir + path;
  std::vector<std::string> segments;
  std::size_t start = 0;
  while (start <= joined.size()) {
    std::size_t slash = joined.find('/', start);
    if (slash == std::string::npos) slash = joined.size();
    std::string seg = joined.substr(start, slash - start);
    if (seg == "..") {
      if (!segments.empty()) segments.pop_back();
    } else if (!seg.empty() && seg != ".") {
      segments.push_back(seg);
    }
    start = slash + 1;
  }
  std::string out;
  for (std::size_t k = 0; k < segments.size(); k++) out += (k ? "/" : "") + segments[k];
  return out;
}

// ---- 目次 ---------------------------------------------------------------

struct TocEntry {
  std::string fragment;  // 空ならファイル先頭
  std::string label;
};
using TocMap = std::map<std::string, std::vector<TocEntry>>;  // 本文ファイル(zip内のパス)→項目

// EPUB3のnav(<nav epub:type="toc">、無ければ最初の<nav>)の<a href>。ラベルの
// ruby(rt/rp)の読みは落とす(「燈ひ色の場」にならないように)。
void parseNav(const std::string &xml, const std::string &navPath, TocMap &toc) {
  std::vector<XmlToken> tokens = tokenizeXml(xml);
  bool haveTocNav = false;
  for (const auto &t : tokens)
    if (t.kind == XmlToken::Open && t.name == "nav" && attr(t, "type").find("toc") != std::string::npos)
      haveTocNav = true;
  int navDepth = 0, skipDepth = 0;
  bool done = false, inAnchor = false;
  std::string href, label;
  for (const auto &t : tokens) {
    if (done) break;
    if (t.kind == XmlToken::Open && t.name == "nav") {
      if (navDepth == 0 && haveTocNav && attr(t, "type").find("toc") == std::string::npos) continue;
      navDepth++;
      continue;
    }
    if (t.kind == XmlToken::Close && t.name == "nav" && navDepth > 0) {
      if (--navDepth == 0) done = true;
      continue;
    }
    if (navDepth == 0) continue;
    if (t.kind == XmlToken::Open && t.name == "a") {
      inAnchor = true;
      href = attr(t, "href");
      label.clear();
    } else if (t.kind == XmlToken::Close && t.name == "a" && inAnchor) {
      inAnchor = false;
      if (href.empty()) continue;
      std::string fragment;
      std::string file = resolveHref(directoryOf(navPath), href, &fragment);
      toc[file].push_back({fragment, collapseSpace(label)});
    } else if (t.kind == XmlToken::Open && (t.name == "rt" || t.name == "rp")) {
      skipDepth++;
    } else if (t.kind == XmlToken::Close && (t.name == "rt" || t.name == "rp") && skipDepth > 0) {
      skipDepth--;
    } else if (t.kind == XmlToken::Text && inAnchor && skipDepth == 0) {
      label += t.text;
    }
  }
}

// EPUB2のncx: navPointごとのnavLabel/textとcontent src。
void parseNcx(const std::string &xml, const std::string &ncxPath, TocMap &toc) {
  std::vector<XmlToken> tokens = tokenizeXml(xml);
  bool inLabel = false, inText = false;
  std::string label;
  for (const auto &t : tokens) {
    if (t.kind == XmlToken::Open && t.name == "navlabel") {
      inLabel = true;
      label.clear();
    } else if (t.kind == XmlToken::Close && t.name == "navlabel") {
      inLabel = false;
    } else if (t.kind == XmlToken::Open && t.name == "text" && inLabel) {
      inText = true;
    } else if (t.kind == XmlToken::Close && t.name == "text") {
      inText = false;
    } else if (t.kind == XmlToken::Text && inText) {
      label += t.text;
    } else if ((t.kind == XmlToken::SelfClose || t.kind == XmlToken::Open) && t.name == "content") {
      std::string src = attr(t, "src");
      if (src.empty()) continue;
      std::string fragment;
      std::string file = resolveHref(directoryOf(ncxPath), src, &fragment);
      toc[file].push_back({fragment, collapseSpace(label)});
    }
  }
}

// ---- 本文 ---------------------------------------------------------------

// 2026-09-29追加、直接のユーザー依頼(EPUBの段階2、"sans-serif(ゴシック体)とserif
// (明朝体)をページ内に混在"): CSSのfont-familyから、要素ごとに明朝体かゴシック体かを
// 決める(Display::Attribute::Serif、<serif>タグ)。読むのは「タグ」「.クラス」
// 「タグ.クラス」だけの単純なセレクタで、子孫・子などの組み合わせのセレクタは誤って
// 広く当てはまらないよう読み飛ばす。
// 2026-09-29(段階3): writing-mode(EpubLayoutHints参照)も同じ規則から読む。
struct CssFontRule {
  std::string tag;                   // 空なら任意
  std::vector<std::string> classes;  // すべて要素のclassにあること
  std::optional<bool> serif;         // font-family(明朝体ならtrue)
  std::optional<bool> vertical;      // writing-mode(縦書きならtrue)
  std::optional<bool> alignEnd;      // text-align(right/endならtrue、EpubLayoutHints::alignEnd)
  std::optional<bool> center;        // text-align(centerならtrue、EpubLayoutHints::center)
  // 字下げ(2026-09-30、EpubLayoutHints::indent): emの値だけ読む。paddingTopは縦書き、
  // paddingLeftは横書きの行頭側の余白(ブロックの中の行すべて)、textIndentは段落の最初の行の
  // ずれ(負ならぶら下げ、継承する)。
  std::optional<double> paddingTop;
  std::optional<double> paddingLeft;
  std::optional<double> textIndent;
  // display:none(2026-09-30、直接のユーザー依頼"class=\"invisible\"も実装よろしく"――
  // h2.invisible { display:none })。trueなら要素の中身を出さない。
  std::optional<bool> hidden;
};

std::optional<bool> classifyDisplayNone(const std::string &value) {
  std::string v = trimAsciiSpace(value);
  if (v.empty()) return std::nullopt;
  return v.find("none") != std::string::npos;
}

// "1em"・"-1em"・"0"の値(emか0だけ、他の単位は読まない)。
std::optional<double> parseEmLength(const std::string &value) {
  std::string v = trimAsciiSpace(value);
  if (v == "0") return 0.0;
  if (v.size() < 3 || v.compare(v.size() - 2, 2, "em") != 0) return std::nullopt;
  char *end = nullptr;
  std::string number = v.substr(0, v.size() - 2);
  double d = std::strtod(number.c_str(), &end);
  if (!end || *end != '\0' || number.empty()) return std::nullopt;
  return d;
}

// text-alignがcenterか(2026-09-30、EpubLayoutHints::center)。
std::optional<bool> classifyTextCenter(const std::string &value) {
  if (value.find("center") != std::string::npos) return true;
  for (const char *v : {"left", "start", "right", "end", "justify"})
    if (value.find(v) != std::string::npos) return false;
  return std::nullopt;
}

std::optional<bool> classifyTextAlign(const std::string &value) {
  if (value.find("right") != std::string::npos || value.find("end") != std::string::npos) return true;
  for (const char *v : {"left", "start", "center", "justify"})
    if (value.find(v) != std::string::npos) return false;
  return std::nullopt;
}

// writing-mode(-epub-/-webkit-付きも)の値。vertical-*は縦、horizontal-tbは横、
// 知らない値(Kbt.epubの"hirozontal-tb"のような綴り違いを含む)は無視。
std::optional<bool> classifyWritingMode(const std::string &value) {
  if (value.find("vertical") != std::string::npos) return true;
  if (value.find("horizontal") != std::string::npos) return false;
  return std::nullopt;
}

// font-familyの値が明朝体(true)かゴシック体(false)か。総称名(serif/sans-serif/
// monospace)を優先する(Kbt.epubのゴシックの指定にも"BIZ UD明朝"が混ざっている)。
std::optional<bool> classifyFontFamily(const std::string &value) {
  std::vector<std::string> names;
  std::string cur;
  for (char c : value + ",") {
    if (c == ',') {
      std::string name;
      for (char d : cur)
        if (d != '"' && d != '\'') name += static_cast<char>(std::tolower(static_cast<unsigned char>(d)));
      names.push_back(trimAsciiSpace(name));
      cur.clear();
    } else {
      cur += c;
    }
  }
  for (const auto &n : names) {
    if (n == "sans-serif" || n == "monospace") return false;
    if (n == "serif") return true;
  }
  for (const auto &n : names) {
    if (n.find("明朝") != std::string::npos || n.find("mincho") != std::string::npos) return true;
    if (n.find("ゴシック") != std::string::npos || n.find("角ゴ") != std::string::npos ||
        n.find("gothic") != std::string::npos || n.find("kaku") != std::string::npos ||
        n.find("sans") != std::string::npos || n.find("メイリオ") != std::string::npos ||
        n.find("meiryo") != std::string::npos)
      return false;
  }
  return std::nullopt;
}

std::vector<CssFontRule> parseCssFontRules(std::string css) {
  // コメントを除く
  for (std::size_t a; (a = css.find("/*")) != std::string::npos;) {
    std::size_t b = css.find("*/", a + 2);
    css.erase(a, b == std::string::npos ? std::string::npos : b + 2 - a);
  }
  std::vector<CssFontRule> rules;
  std::size_t pos = 0;
  while (pos < css.size()) {
    std::size_t open = css.find('{', pos);
    if (open == std::string::npos) break;
    std::string selectors = trimAsciiSpace(css.substr(pos, open - pos));
    std::size_t semicolonAt = selectors.rfind(';');  // "@charset ...;"等の文を読み飛ばす
    if (semicolonAt != std::string::npos) selectors = trimAsciiSpace(selectors.substr(semicolonAt + 1));
    // 対応する'}'(@mediaの入れ子にも耐える)
    int depth = 1;
    std::size_t close = open + 1;
    for (; close < css.size() && depth > 0; close++) {
      if (css[close] == '{') depth++;
      if (css[close] == '}') depth--;
    }
    std::string body = css.substr(open + 1, close - open - 2);
    pos = close;
    if (!selectors.empty() && selectors[0] == '@') {
      if (selectors.compare(0, 6, "@media") == 0)
        for (auto &inner : parseCssFontRules(body)) rules.push_back(std::move(inner));
      continue;
    }
    // 同じ規則の中では後の宣言が勝つ
    auto readProperty = [&](const std::string &property, auto classify) {
      std::optional<bool> result;
      std::size_t fp = 0;
      while ((fp = body.find(property, fp)) != std::string::npos) {
        std::size_t colon = body.find(':', fp);
        if (colon == std::string::npos) break;
        std::size_t end = body.find(';', colon);
        std::string value = body.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1);
        if (auto v = classify(value)) result = v;
        fp = colon;
      }
      return result;
    };
    std::optional<bool> serif = readProperty("font-family", classifyFontFamily);
    std::optional<bool> vertical = readProperty("writing-mode", classifyWritingMode);
    std::optional<bool> alignEnd = readProperty("text-align", classifyTextAlign);
    std::optional<bool> center = readProperty("text-align", classifyTextCenter);
    auto readLength = [&](const std::string &property) {
      std::optional<double> result;
      std::size_t fp = 0;
      while ((fp = body.find(property, fp)) != std::string::npos) {
        std::size_t colon = body.find(':', fp);
        if (colon == std::string::npos) break;
        // "padding-top"を探して"-webkit-padding-top"等に当たらないよう、直前が名前の字でない所だけ
        bool nameStart = fp == 0 || !(std::isalnum(static_cast<unsigned char>(body[fp - 1])) || body[fp - 1] == '-');
        bool nameEnd = trimAsciiSpace(body.substr(fp + property.size(), colon - fp - property.size())).empty();
        std::size_t end = body.find(';', colon);
        if (nameStart && nameEnd)
          if (auto v = parseEmLength(
                  body.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1)))
            result = v;
        fp = colon;
      }
      return result;
    };
    std::optional<double> paddingTop = readLength("padding-top");
    std::optional<double> paddingLeft = readLength("padding-left");
    std::optional<double> textIndent = readLength("text-indent");
    std::optional<bool> hidden;
    for (std::size_t fp = 0; (fp = body.find("display", fp)) != std::string::npos;) {
      std::size_t colon = body.find(':', fp);
      if (colon == std::string::npos) break;
      bool nameStart = fp == 0 || !(std::isalnum(static_cast<unsigned char>(body[fp - 1])) || body[fp - 1] == '-');
      bool nameEnd = trimAsciiSpace(body.substr(fp + 7, colon - fp - 7)).empty();
      std::size_t end = body.find(';', colon);
      if (nameStart && nameEnd)
        if (auto v = classifyDisplayNone(
                body.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1)))
          hidden = v;
      fp = colon;
    }
    if (!serif && !vertical && !alignEnd && !center && !paddingTop && !paddingLeft && !textIndent && !hidden) continue;
    std::string one;
    for (char c : selectors + ",") {
      if (c != ',') {
        one += c;
        continue;
      }
      std::string sel = trimAsciiSpace(one);
      one.clear();
      // 2026-09-30、直接のユーザー報告("同ファイル(src/K3t.epub)をEPUBで開くと奥付が横書きに
      // ならない"): K3t.epubの横書きの指定は"html body { writing-mode: horizontal-tb }"。先頭が
      // html/bodyだけの子孫・子のセレクタは、どの要素もその中にあるので最後の部分と同じ
      // (以前は組み合わせのセレクタとしてまとめて読み飛ばしていた)。
      for (bool stripped = true; stripped;) {
        stripped = false;
        for (const char *outer : {"html", "body"}) {
          std::size_t len = std::strlen(outer);
          if (sel.compare(0, len, outer) != 0 || sel.size() <= len) continue;
          std::size_t next = sel.find_first_not_of(" \t\n>", len);
          if (next == len || next == std::string::npos) continue;  // "html.x"・"body"だけ等はそのまま
          sel = sel.substr(next);
          stripped = true;
        }
      }
      if (sel.empty() || sel.find_first_of(" \t\n>+~:[#") != std::string::npos) continue;  // 単純なセレクタだけ
      CssFontRule rule;
      rule.serif = serif;
      rule.vertical = vertical;
      rule.alignEnd = alignEnd;
      rule.center = center;
      rule.paddingTop = paddingTop;
      rule.paddingLeft = paddingLeft;
      rule.textIndent = textIndent;
      rule.hidden = hidden;
      std::size_t dot = sel.find('.');
      rule.tag = sel.substr(0, dot);
      if (rule.tag == "*") rule.tag.clear();
      while (dot != std::string::npos) {
        std::size_t next = sel.find('.', dot + 1);
        rule.classes.push_back(sel.substr(dot + 1, next == std::string::npos ? std::string::npos : next - dot - 1));
        dot = next;
      }
      rules.push_back(std::move(rule));
    }
  }
  return rules;
}

const std::set<std::string> kBlockElements = {
    "p",       "div",     "li",     "blockquote", "dd",     "dt",      "tr",   "section", "article",
    "aside",   "header",  "footer", "nav",        "figure", "figcaption", "table", "ul", "ol",
    "dl",      "body",    "pre",    "caption",    "main",   "address"};

class BodyConverter {
 public:
  BodyConverter(std::string &out, const EpubMemberReader &readMember) : out_(out), readMember_(readMember) {}

  // 本文中のリンク(行番号→リンク先の"ファイル#フラグメント")と、各アンカーの
  // 行番号("ファイル#フラグメント"、ファイル先頭は"ファイル#")。行番号は
  // out_の1始まりの行。epubToHelpMarkup()が全ファイルの変換後に突き合わせる。
  struct LinkRecord {
    int line;
    std::string target;
  };
  const std::vector<LinkRecord> &links() const { return links_; }
  const std::map<std::string, int> &anchors() const { return anchorLines_; }
  // 出した行(out_の1始まりの行)ごとの縦横(EpubLayoutHints参照)。空行は入らない。
  enum class LineKind { Normal, Horizontal, Centered };
  const std::map<int, LineKind> &lineKinds() const { return lineKinds_; }
  const std::vector<int> &alignEndLines() const { return alignEndLines_; }
  const std::vector<int> &centerLines() const { return centerLines_; }
  const std::map<int, std::pair<int, int>> &indentLines() const { return indentLines_; }
  const std::map<int, std::string> &chapterLabels() const { return chapterLabels_; }
  void setBookVertical(bool vertical) { bookVertical_ = vertical; }

  void convert(const std::string &xhtml, const std::string &path, const std::vector<TocEntry> *tocEntries) {
    path_ = path;
    pendingAnchors_.push_back(path + "#");
    tocEntries_ = tocEntries;
    pendingLabel_.reset();
    if (tocEntries_)
      for (const auto &e : *tocEntries_)
        if (e.fragment.empty()) {
          pendingLabel_ = e.label;
          pendingDepth_ = 0;
          break;
        }
    depth_ = skipDepth_ = preDepth_ = hiddenDepth_ = 0;
    listStack_.clear();
    fontRules_.clear();  // 本文のファイルごとに、そのファイルが読み込むCSSだけ
    fontStack_.clear();
    serif_ = true;  // CSSの指定が無ければ明朝体(読書アプリの既定に合わせる)
    vertical_.reset();
    horizontalOutside_ = false;
    alignEnd_ = false;
    center_ = false;
    paddingTopEm_ = paddingLeftEm_ = textIndentEm_ = 0;
    rubyBase_ = false;
    inStyle_ = false;
    std::vector<XmlToken> tokens = tokenizeXml(xhtml);
    // CSS(fontRules_参照)は<head>の中にあり、<html>の開始より後に来るので、先に
    // まとめて読んでおく(そうしないとhtml自身に掛かる規則が当たらない――Kbt.epubの
    // 奥付はhtmlのwriting-modeだけで横書きにしている)。
    bool inStyle = false;
    for (const auto &t : tokens) {
      bool open = (t.kind == XmlToken::Open || t.kind == XmlToken::SelfClose);
      if (t.name == "link" && open && attr(t, "rel").find("stylesheet") != std::string::npos)
        loadStylesheet(attr(t, "href"));
      if (t.name == "style" && t.kind != XmlToken::Text) inStyle = (t.kind == XmlToken::Open);
      if (inStyle && t.kind == XmlToken::Text)
        for (auto &rule : parseCssFontRules(t.text)) fontRules_.push_back(std::move(rule));
    }
    for (const auto &t : tokens) handle(t);
    endLine(false);
    flushDeferredLabel();
    if (pendingLabel_) emitLabelLine();
  }

 private:
  void handle(const XmlToken &t) {
    const std::string &name = t.name;
    if (t.kind == XmlToken::Text) {
      if (inStyle_) return;  // convert()が先に読んでいる
      if (skipDepth_ == 0 && hiddenDepth_ == 0) text(t.text);
      if (skipDepth_ == 0 && hiddenDepth_ > 0 && hiddenHeading_) hiddenHeadingText_ += t.text;
      return;
    }
    bool open = (t.kind == XmlToken::Open || t.kind == XmlToken::SelfClose);
    bool close = (t.kind == XmlToken::Close || t.kind == XmlToken::SelfClose);
    if (name == "style") inStyle_ = (t.kind == XmlToken::Open);  // 中身はconvert()が先に読む
    if (open && t.kind == XmlToken::Open) depth_++;

    if (name == "head" || name == "script" || name == "style" || name == "rp" || name == "title") {
      if (t.kind == XmlToken::Open) skipDepth_++;
      if (t.kind == XmlToken::Close && skipDepth_ > 0) skipDepth_--;
      if (t.kind == XmlToken::Close) depth_--;
      return;
    }
    if (skipDepth_ > 0) {
      if (t.kind == XmlToken::Close) depth_--;
      return;
    }
    // display:none(CssFontRule::hidden)の要素の中は出さない(開いた時のdepth_より浅くなるまで)
    if (hiddenDepth_ > 0) {
      if (t.kind == XmlToken::Close && --depth_ < hiddenDepth_) {
        hiddenDepth_ = 0;
        // 2026-09-30、直接のユーザー依頼("一覧では「奥付」のままにして"): 隠した見出しの字は、
        // 次の章の行の章の一覧での名前にする(EpubLayoutHints::chapterLabel)。
        std::string label = collapseSpace(hiddenHeadingText_);
        if (hiddenHeading_ && !label.empty()) hiddenHeadingLabel_ = label;
        hiddenHeading_ = false;
      }
      return;
    }
    if (open && ruleFor(name, attr(t, "class"), &CssFontRule::hidden).value_or(false)) {
      if (t.kind == XmlToken::Open) {
        hiddenDepth_ = depth_;
        hiddenHeading_ = name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
        hiddenHeadingText_.clear();
      }
      return;
    }

    if (open) {
      std::string id = attr(t, "id");
      if (!id.empty()) pendingAnchors_.push_back(path_ + "#" + id);
      if (!id.empty() && tocEntries_)
        for (const auto &e : *tocEntries_)
          if (e.fragment == id) {
            pendingLabel_ = e.label;
            pendingDepth_ = depth_;
            break;
          }
    }

    bool heading = name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
    if (heading) {
      if (open) {
        endLine(false);
        flushDeferredLabel();
        ensureBlankLine();
        changeStyle(bold_, +1, "b");
      }
      if (close) {
        // 空の見出し(<h2 id="toc_id_1"></h2>+次の段落に画像だけ、等)は目次の
        // ラベルを章の名前にする。次の行が画像だけならその行とまとめて
        // 「表紙 [image: title.jpg]」の1行にする(endLine()参照)。
        if (!lineVisible_ && pendingLabel_) deferredLabel_ = *pendingLabel_;
        pendingLabel_.reset();
        changeStyle(bold_, -1, "b");
        endLine(true);
      }
    } else if (name == "br") {
      if (open) forceNewline();
    } else if (name == "hr") {
      if (open) {
        endLine(false);
        flushDeferredLabel();
        // 2026-09-30、直接のユーザー依頼("----------------------------------------のセンタリングも
        // よろしく"): 区切り線は中央に置く(EpubLayoutHints::center)。縦横は今の要素に従う。
        centerLines_.push_back(currentLineNumber());
        lineKinds_[currentLineNumber()] = currentKind();
        out_ += "----------------------------------------\n";
      }
    } else if (name == "img" || name == "image") {
      if (open) {
        // altではなくファイル名: 同じalt("title")の画像が何枚もある本が実際に
        // あり、CUTの一覧から[Enter]でクイックルックする時にzip内のどの
        // メンバーか特定できなくなる(findEpubImageMember()参照)。
        std::string src = attr(t, "src");
        if (src.empty()) src = attr(t, "href");
        src = percentDecode(src);
        std::size_t slash = src.find_last_of('/');
        std::string name = (slash == std::string::npos) ? src : src.substr(slash + 1);
        bool wasEmpty = !lineVisible_;
        text("[image: " + name + "]");
        if (wasEmpty) lineImageOnly_ = true;
      }
    } else if (name == "ruby") {
      // 2026-09-30追加、直接のユーザー依頼("-gでのルビを実装してみて"): 親字を<rb>、
      // 読み(《》ごと)を<rt>で囲む(Display::Attribute::RubyBase/RubyText)。-cは今までどおり
      // 「燈《ひ》」と見せ、-gは読みをマスに入れずに親字に添えて描く(ContentView::
      // renderLine()、layoutEpubVerticalColumns())。
      if (t.kind == XmlToken::Open) {
        openStylesAtLineStart();
        line_ += "<rb>";
        rubyBase_ = true;
      } else if (close && rubyBase_) {
        line_ += "</rb>";
        rubyBase_ = false;
      }
    } else if (name == "rt") {
      if (open) {
        if (rubyBase_) line_ += "</rb>";
        rubyBase_ = false;
        line_ += "<rt>";
        text("《");
      }
      if (close) {
        text("》");
        line_ += "</rt>";
      }
    } else if (name == "b" || name == "strong") {
      changeStyle(bold_, open ? +1 : -1, "b");
    } else if (name == "i" || name == "em" || name == "cite") {
      changeStyle(italic_, open ? +1 : -1, "i");
    } else if (name == "a") {
      // 2026-09-28追加、直接のユーザー提案("選択できる章はタグジャンプとしての
      // アンダーラインを付加し returnで当該行へ飛んではどうか"): 本の中へのリンク
      // だけに下線を付け、リンク先の行を記録する(外部URLは飛べないので対象外)。
      // 下線はリンク専用にするため、h1と<u>には付けない。
      if (t.kind == XmlToken::Open) {
        std::string href = attr(t, "href");
        bool external = href.find("://") != std::string::npos || href.compare(0, 7, "mailto:") == 0;
        // 2026-09-30、直接のユーザー依頼("webサイトへのリンクがわかりづらいのでアンダーライン化して
        // ほしいのと、踏んだらクイックルックへ移行してほしい"): http(s)のリンクも下線を付け、
        // "url:"を付けて記録する(EpubLayoutHints::webLinks、Application::openEpubWebLink())。
        bool web = href.compare(0, 7, "http://") == 0 || href.compare(0, 8, "https://") == 0;
        if (web) {
          linkTarget_ = "url:" + href;
          linkLineRecorded_ = false;
          changeStyle(underline_, +1, "u");
        } else if (!href.empty() && !external) {
          std::string fragment;
          std::string file = resolveHref(directoryOf(path_), href, &fragment);
          if (href[0] == '#') file = path_;
          linkTarget_ = file + "#" + fragment;
          linkLineRecorded_ = false;
          changeStyle(underline_, +1, "u");
        }
      } else if (t.kind == XmlToken::Close && linkTarget_) {
        linkTarget_.reset();
        changeStyle(underline_, -1, "u");
      }
    } else if (name == "td" || name == "th") {
      if (close) pendingSpace_ = true;
    } else if (kBlockElements.count(name)) {
      if (open) {
        endLine(false);
        if (name == "pre") preDepth_++;
        if (name == "ul" || name == "ol") listStack_.push_back(name == "ol" ? 1 : 0);
        if (name == "li" && !listStack_.empty()) {
          int &counter = listStack_.back();
          text(counter ? std::to_string(counter++) + ". " : "・");
        }
      }
      if (close) {
        if (name == "pre" && preDepth_ > 0) preDepth_--;
        if ((name == "ul" || name == "ol") && !listStack_.empty()) listStack_.pop_back();
        if (pendingLabel_ && lineVisible_) {
          pendingLabel_.reset();
          endLine(true);
        } else {
          endLine(false);
          if (pendingLabel_ && depth_ <= pendingDepth_) emitLabelLine();
        }
      }
    }
    // 明朝体/ゴシック体(fontRules_参照): 要素の中身の間だけ、その要素に当てはまる
    // 規則(無ければ親のまま)にする。ブロックの開き・閉じの改行(上のendLine())の
    // 後で切り替えるので、変わり目は行の途中か行頭になる。
    // 縦横(EpubLayoutHints参照)も同じく要素ごと。horizontalOutside_は「外側の要素が
    // 横書き」(扉の縦書きの箱の見分けに使う)。
    if (t.kind == XmlToken::Open) {
      fontStack_.push_back({serif_, vertical_, horizontalOutside_, alignEnd_, center_, paddingTopEm_, paddingLeftEm_,
                            textIndentEm_});
      std::string classes = attr(t, "class");
      setSerif(ruleFor(name, classes, &CssFontRule::serif).value_or(serif_));
      horizontalOutside_ = horizontalOutside_ || vertical_ == false;
      if (auto vertical = ruleFor(name, classes, &CssFontRule::vertical)) vertical_ = vertical;
      if (auto alignEnd = ruleFor(name, classes, &CssFontRule::alignEnd)) alignEnd_ = *alignEnd;
      if (auto center = ruleFor(name, classes, &CssFontRule::center)) center_ = *center;
      paddingTopEm_ += ruleFor(name, classes, &CssFontRule::paddingTop).value_or(0.0);
      paddingLeftEm_ += ruleFor(name, classes, &CssFontRule::paddingLeft).value_or(0.0);
      if (auto indent = ruleFor(name, classes, &CssFontRule::textIndent)) textIndentEm_ = *indent;
    } else if (t.kind == XmlToken::Close && !fontStack_.empty()) {
      setSerif(fontStack_.back().serif);
      vertical_ = fontStack_.back().vertical;
      horizontalOutside_ = fontStack_.back().horizontalOutside;
      alignEnd_ = fontStack_.back().alignEnd;
      center_ = fontStack_.back().center;
      paddingTopEm_ = fontStack_.back().paddingTop;
      paddingLeftEm_ = fontStack_.back().paddingLeft;
      textIndentEm_ = fontStack_.back().textIndent;
      fontStack_.pop_back();
    }
    if (t.kind == XmlToken::Close) depth_--;
  }

  void loadStylesheet(const std::string &href) {
    if (href.empty()) return;
    std::string path = resolveHref(directoryOf(path_), href);
    auto cached = cssCache_.find(path);
    if (cached == cssCache_.end()) {
      auto css = readMember_(path);
      cached = cssCache_.emplace(path, css ? parseCssFontRules(*css) : std::vector<CssFontRule>{}).first;
    }
    fontRules_.insert(fontRules_.end(), cached->second.begin(), cached->second.end());
  }

  // 要素に当てはまり、`field`(serif/vertical)を指定している規則のうち、詳しさ(クラスの
  // 数、次にタグの有無)が最も高いもの、同じなら後に書かれたもの。無ければnullopt(親のまま)。
  template <typename T>
  std::optional<T> ruleFor(const std::string &tag, const std::string &classAttr,
                           std::optional<T> CssFontRule::*field) const {
    std::vector<std::string> classes;
    std::string cur;
    for (char c : classAttr + " ") {
      if (c == ' ' || c == '\t' || c == '\n') {
        if (!cur.empty()) classes.push_back(cur);
        cur.clear();
      } else {
        cur += c;
      }
    }
    std::optional<T> best;
    int bestScore = -1;
    for (const auto &rule : fontRules_) {
      if (!(rule.*field)) continue;
      if (!rule.tag.empty() && rule.tag != tag) continue;
      bool all = true;
      for (const auto &c : rule.classes)
        if (std::find(classes.begin(), classes.end(), c) == classes.end()) all = false;
      if (!all) continue;
      int score = static_cast<int>(rule.classes.size()) * 10 + (rule.tag.empty() ? 0 : 1);
      if (score >= bestScore) {
        bestScore = score;
        best = rule.*field;
      }
    }
    return best;
  }

  LineKind currentKind() const {
    if (vertical_ == false) return LineKind::Horizontal;
    if (vertical_ == true && horizontalOutside_) return LineKind::Centered;
    return LineKind::Normal;
  }

  // changeStyle()の明朝体版(カウンタではなくオン/オフ)。
  void setSerif(bool serif) {
    if (serif == serif_) return;
    serif_ = serif;
    if (!line_.empty()) line_ += serif ? "<serif>" : "</serif>";
  }

  // 太字等はトグル式の制御バイトへ展開されるので(markdown_to_help_markup.cpp
  // 自身の2026-09-22の注記参照)、入れ子(見出しの中の<b>等)は外側の1組だけを
  // 出す。行をまたぐ場合は、行末で閉じて次の行頭で開き直す(endLine()参照)。
  void changeStyle(int &counter, int delta, const char *tag) {
    int before = counter;
    counter = std::max(0, counter + delta);
    if (before == 0 && counter > 0 && !line_.empty()) line_ += std::string("<") + tag + ">";
    if (before > 0 && counter == 0 && !line_.empty()) line_ += std::string("</") + tag + ">";
  }

  void openStylesAtLineStart() {
    if (!line_.empty()) return;
    if (serif_) line_ += "<serif>";
    if (underline_) line_ += "<u>";
    if (bold_) line_ += "<b>";
    if (italic_) line_ += "<i>";
  }

  void text(const std::string &s) {
    for (std::size_t i = 0; i < s.size(); i++) {
      char c = s[i];
      if (preDepth_ > 0) {
        if (c == '\n') {
          forceNewline();
          continue;
        }
        if (c == '\r') continue;
        if (c == '\t') c = ' ';  // タブは章の行の印なので本文には出さない
      } else if (isXmlSpace(c)) {
        pendingSpace_ = true;
        if (c == '\n') pendingNewline_ = true;
        continue;
      }
      resolveAnchorsHere();
      if (linkTarget_ && !linkLineRecorded_) {
        links_.push_back({currentLineNumber(), *linkTarget_});
        linkLineRecorded_ = true;
      }
      openStylesAtLineStart();
      // CSS Text 3の改行の変換と同じく、和文(非ASCII)どうしの間のソース上の
      // 改行はスペースにしない(「色の\n   場」→「色の場」)。
      bool nonAscii = static_cast<unsigned char>(c) >= 0x80;
      if (pendingSpace_ && lineVisible_ && !(pendingNewline_ && nonAscii && lastNonAscii_)) line_ += ' ';
      pendingSpace_ = pendingNewline_ = false;
      if (!lineVisible_) {
        lineKind_ = currentKind();
        lineAlignEnd_ = alignEnd_;
        lineCenter_ = center_;
        startLineIndent();
      } else if (pendingRestIndent_ && (static_cast<unsigned char>(c) & 0xC0) != 0x80) {
        line_ += "<indent" + std::to_string(lineIndentRest_ * 2) + ">";  // 2字目以降(折り返した行)
        pendingRestIndent_ = false;
      }
      line_ += c;
      lineVisible_ = true;
      lineImageOnly_ = false;
      lastNonAscii_ = nonAscii;
    }
  }

  // 2026-09-30追加、直接のユーザー依頼("同ファイル(src/K3t.epub)のSection021.xhtmlには
  // indentタグがあるので実装して"――<div class="indent_1em">、CSSは縦書きが"padding-top:1em;
  // text-indent:-1em"、横書きが"padding-left:1em; text-indent:-1em"のぶら下げ): 行の最初の字の
  // 所で、行頭側の余白(縦書きはpadding-top、横書きはpadding-left)とtext-indentから字下げを
  // 決め、EpubLayoutHints::indentに記録する。横書きの画面の行は<indentN>(半角N個、1em=2)で
  // 字下げし(PipeBufferが折り返した行にも引き継ぐ)、最初の行と続く行で違えば2字目の前で
  // 切り替える。縦書きはApplication::rebuildEpubVertical()がこの空白を取り除いて列の頭を空ける。
  void startLineIndent() {
    lineIndentRest_ = 0;
    pendingRestIndent_ = false;
    bool vertical = vertical_.value_or(bookVertical_);
    double padding = vertical ? paddingTopEm_ : paddingLeftEm_;
    int first = std::max(0, static_cast<int>(std::lround(padding + textIndentEm_)));
    int rest = std::max(0, static_cast<int>(std::lround(padding)));
    if (first == 0 && rest == 0) return;
    indentLines_[currentLineNumber()] = {first, rest};
    if (first > 0) line_ += "<indent" + std::to_string(first * 2) + ">";
    lineIndentRest_ = rest;
    pendingRestIndent_ = rest != first;
    indentOpen_ = true;
  }

  void closeStylesAtLineEnd() {
    if (indentOpen_) line_ += "</indent>";
    indentOpen_ = false;
    pendingRestIndent_ = false;
    if (italic_) line_ += "</i>";
    if (bold_) line_ += "</b>";
    if (underline_) line_ += "</u>";
    if (serif_) line_ += "</serif>";
  }

  // 文字のある行を出す(chapter: 行末にタブ)。文字が無ければ何もしない。
  void endLine(bool chapter) {
    pendingSpace_ = pendingNewline_ = false;
    if (!lineVisible_) {
      line_.clear();
      return;
    }
    if (deferredLabel_ && lineImageOnly_) {
      // この行(画像)に割り当て済みのアンカーは、まとめた後もこの行のまま
      line_ = "<b>" + *deferredLabel_ + "</b> " + line_;
      deferredLabel_.reset();
      chapter = true;
    }
    flushDeferredLabel();
    lineImageOnly_ = false;
    closeStylesAtLineEnd();
    lineKinds_[currentLineNumber()] = lineKind_;
    if (chapter && hiddenHeadingLabel_) {
      chapterLabels_[currentLineNumber()] = *hiddenHeadingLabel_;
      hiddenHeadingLabel_.reset();
    }
    if (lineAlignEnd_) alignEndLines_.push_back(currentLineNumber());
    if (lineCenter_) centerLines_.push_back(currentLineNumber());
    out_ += line_;
    if (chapter) out_ += '\t';
    out_ += '\n';
    line_.clear();
    lineVisible_ = false;
  }

  void forceNewline() {
    if (lineVisible_) {
      endLine(false);
    } else {
      line_.clear();
      out_ += '\n';
    }
  }

  void ensureBlankLine() {
    if (out_.empty() || (out_.size() >= 2 && out_.compare(out_.size() - 2, 2, "\n\n") == 0)) return;
    out_ += '\n';
  }

  // 行番号: out_の改行を前回数えた所から数え足す(毎回全体を数えない)。
  int currentLineNumber() {
    for (; counted_ < out_.size(); counted_++)
      if (out_[counted_] == '\n') newlines_++;
    return newlines_ + 1;
  }

  // 保留中のアンカー(id、ファイル先頭)を、これから文字が出る行に割り当てる。
  void resolveAnchorsHere() {
    if (pendingAnchors_.empty()) return;
    int line = currentLineNumber();
    for (const auto &a : pendingAnchors_) anchorLines_.emplace(a, line);
    pendingAnchors_.clear();
  }

  void flushDeferredLabel() {
    if (!deferredLabel_) return;
    resolveAnchorsHere();
    if (!deferredLabel_->empty()) {
      lineKinds_[currentLineNumber()] = currentKind();
      out_ += "<b>" + *deferredLabel_ + "</b>\t\n";
    }
    deferredLabel_.reset();
  }

  void emitLabelLine() {
    std::string label = *pendingLabel_;
    pendingLabel_.reset();
    if (label.empty()) return;
    resolveAnchorsHere();
    lineKinds_[currentLineNumber()] = currentKind();
    out_ += "<b>" + label + "</b>\t\n";
  }

  std::string &out_;
  const EpubMemberReader &readMember_;
  std::map<std::string, std::vector<CssFontRule>> cssCache_;  // CSSのファイルごと(本文の各ファイルで共有)
  std::vector<CssFontRule> fontRules_;                       // 今のファイルが読み込むCSSの規則
  struct Frame {
    bool serif;
    std::optional<bool> vertical;
    bool horizontalOutside;
    bool alignEnd;
    bool center;
    double paddingTop;
    double paddingLeft;
    double textIndent;
  };
  std::vector<Frame> fontStack_;  // 開いている要素ごとの、その外側の状態
  bool serif_ = true;
  std::optional<bool> vertical_;    // writing-mode(指定が無ければnullopt、本全体の向きに従う)
  bool horizontalOutside_ = false;  // 外側の要素のどれかが横書き
  LineKind lineKind_ = LineKind::Normal;
  bool alignEnd_ = false;      // text-alignがright/end(EpubLayoutHints::alignEnd)
  bool lineAlignEnd_ = false;  // 今の行の最初の字の時点のalignEnd_
  std::vector<int> alignEndLines_;
  bool center_ = false;      // text-alignがcenter(EpubLayoutHints::center)
  bool lineCenter_ = false;  // 今の行の最初の字の時点のcenter_
  std::vector<int> centerLines_;
  // ルビの親字の中(<rb>を出してから、<rt>か</ruby>で閉じるまで)。
  bool rubyBase_ = false;
  // 字下げ(CssFontRule::paddingTop等、em): 開いている要素のpaddingの和と、継承したtext-indent。
  double paddingTopEm_ = 0, paddingLeftEm_ = 0, textIndentEm_ = 0;
  bool bookVertical_ = false;       // 本全体が縦書き(epubIsRightToLeft())
  int lineIndentRest_ = 0;          // 今の行の2字目以降の字下げ(em)
  bool pendingRestIndent_ = false;  // 最初の字の後に<indent>(2字目以降の字下げ)を出す
  bool indentOpen_ = false;         // 行末で</indent>を出す
  int hiddenDepth_ = 0;             // display:noneの要素を開いた時のdepth_(0なら中ではない)
  bool hiddenHeading_ = false;      // 隠している要素が見出し(その字をhiddenHeadingText_に集める)
  std::string hiddenHeadingText_;
  std::optional<std::string> hiddenHeadingLabel_;  // 次の章の行の、章の一覧での名前
  std::map<int, std::string> chapterLabels_;        // 行→章の一覧での名前(EpubLayoutHints::chapterLabel)
  std::map<int, std::pair<int, int>> indentLines_;  // 行→(最初の字下げ, 続く字下げ)(em)
  std::map<int, LineKind> lineKinds_;
  bool inStyle_ = false;
  std::string path_;
  std::vector<std::string> pendingAnchors_;
  std::map<std::string, int> anchorLines_;
  std::vector<LinkRecord> links_;
  std::optional<std::string> linkTarget_;  // 開いている<a href>のリンク先
  bool linkLineRecorded_ = false;
  std::size_t counted_ = 0;
  int newlines_ = 0;
  const std::vector<TocEntry> *tocEntries_ = nullptr;
  std::optional<std::string> pendingLabel_;
  std::optional<std::string> deferredLabel_;  // 空の見出しの章の名前、heading closeのコメント参照
  bool lineImageOnly_ = false;                 // 今の行は画像の"[image: ...]"だけ
  int pendingDepth_ = 0;
  std::string line_;
  bool lineVisible_ = false;
  bool pendingSpace_ = false;
  bool pendingNewline_ = false;  // pendingSpace_の空白に改行が含まれていた
  bool lastNonAscii_ = false;    // 直前に出した文字(のバイト)が非ASCII
  int depth_ = 0, skipDepth_ = 0, preDepth_ = 0;
  int bold_ = 0, italic_ = 0, underline_ = 0;
  std::vector<int> listStack_;  // olは次の番号、ulは0
};

// container.xmlが指すOPFのzip内のパスと中身。
std::optional<std::string> readOpf(const EpubMemberReader &readMember, std::string *opfPath) {
  auto container = readMember("META-INF/container.xml");
  if (!container) return std::nullopt;
  opfPath->clear();
  for (const auto &t : tokenizeXml(*container))
    if ((t.kind == XmlToken::SelfClose || t.kind == XmlToken::Open) && t.name == "rootfile") {
      *opfPath = resolveHref("", attr(t, "full-path"));
      break;
    }
  if (opfPath->empty()) return std::nullopt;
  return readMember(*opfPath);
}

}  // namespace

std::optional<std::string> findEpubImageMember(const EpubMemberReader &readMember, const std::string &fileName) {
  std::string opfPath;
  auto opf = readOpf(readMember, &opfPath);
  if (!opf) return std::nullopt;
  for (const auto &t : tokenizeXml(*opf)) {
    if ((t.kind != XmlToken::SelfClose && t.kind != XmlToken::Open) || t.name != "item") continue;
    std::string path = resolveHref(directoryOf(opfPath), attr(t, "href"));
    std::size_t slash = path.find_last_of('/');
    if ((slash == std::string::npos ? path : path.substr(slash + 1)) == fileName) return path;
  }
  return std::nullopt;
}

bool epubIsRightToLeft(const EpubMemberReader &readMember) {
  std::string opfPath;
  auto opf = readOpf(readMember, &opfPath);
  if (!opf) return false;
  for (const auto &t : tokenizeXml(*opf))
    if ((t.kind == XmlToken::Open || t.kind == XmlToken::SelfClose) && t.name == "spine")
      return attr(t, "page-progression-direction") == "rtl";
  return false;
}

std::optional<std::string> epubToHelpMarkup(const EpubMemberReader &readMember, std::map<int, int> *links,
                                            EpubLayoutHints *layoutHints) {
  std::string opfPath;
  auto opf = readOpf(readMember, &opfPath);
  if (!opf) return std::nullopt;

  struct ManifestItem {
    std::string path, mediaType, properties;
  };
  std::map<std::string, ManifestItem> manifest;
  std::vector<std::string> spine;
  std::string title, creator, ncxId;
  std::string *capture = nullptr;
  std::string opfDir = directoryOf(opfPath);
  bool inMetadata = false;
  for (const auto &t : tokenizeXml(*opf)) {
    bool open = (t.kind == XmlToken::Open || t.kind == XmlToken::SelfClose);
    if (t.kind == XmlToken::Open && t.name == "metadata") inMetadata = true;
    if (t.kind == XmlToken::Close && t.name == "metadata") inMetadata = false;
    if (t.kind == XmlToken::Open && inMetadata && t.name == "title" && title.empty()) capture = &title;
    if (t.kind == XmlToken::Open && inMetadata && t.name == "creator" && creator.empty()) capture = &creator;
    if (t.kind == XmlToken::Close && (t.name == "title" || t.name == "creator")) capture = nullptr;
    if (t.kind == XmlToken::Text && capture) *capture += t.text;
    if (open && t.name == "item")
      manifest[attr(t, "id")] = {resolveHref(opfDir, attr(t, "href")), attr(t, "media-type"), attr(t, "properties")};
    if (open && t.name == "spine") ncxId = attr(t, "toc");
    if (open && t.name == "itemref" && attr(t, "linear") != "no") spine.push_back(attr(t, "idref"));
  }

  TocMap toc;
  for (const auto &[id, item] : manifest) {
    (void)id;
    std::set<std::string> props;
    std::size_t s = 0;
    while (s < item.properties.size()) {
      std::size_t e = item.properties.find(' ', s);
      if (e == std::string::npos) e = item.properties.size();
      props.insert(item.properties.substr(s, e - s));
      s = e + 1;
    }
    if (props.count("nav"))
      if (auto nav = readMember(item.path)) parseNav(*nav, item.path, toc);
  }
  if (toc.empty()) {
    auto it = manifest.find(ncxId);
    if (it == manifest.end())
      for (auto m = manifest.begin(); m != manifest.end(); ++m)
        if (m->second.mediaType == "application/x-dtbncx+xml") it = m;
    if (it != manifest.end())
      if (auto ncx = readMember(it->second.path)) parseNcx(*ncx, it->second.path, toc);
  }

  std::string out;
  title = collapseSpace(title);
  creator = collapseSpace(creator);
  // 本のタイトルは明朝体(2026-09-29、直接のユーザー指定)。下線はリンク専用(BodyConverterの"a"参照)
  if (!title.empty()) out += "<serif><b>" + title + "</b></serif>\n";
  if (!creator.empty()) out += creator + "\n";
  if (!out.empty()) out += "\n";

  BodyConverter converter(out, readMember);
  converter.setBookVertical(epubIsRightToLeft(readMember));
  for (const auto &idref : spine) {
    auto it = manifest.find(idref);
    if (it == manifest.end()) continue;
    const ManifestItem &item = it->second;
    if (item.mediaType.find("html") == std::string::npos && item.mediaType.find("xml") == std::string::npos) continue;
    auto xhtml = readMember(item.path);
    if (!xhtml) continue;
    auto tocIt = toc.find(item.path);
    converter.convert(*xhtml, item.path, tocIt == toc.end() ? nullptr : &tocIt->second);
    if (!out.empty() && out.compare(out.size() - 2, 2, "\n\n") != 0) out += '\n';
  }
  // expandHelpMarkup()は先頭の改行1個を読み飛ばすので、先頭の改行は落として
  // 行番号をその分ずらす(画面の行のrawLineNumberと一致させるため)。
  int leading = 0;
  while (leading < static_cast<int>(out.size()) && out[leading] == '\n') leading++;
  out.erase(0, leading);
  if (layoutHints) {
    // 同じ種類の行が(空行をはさんでも)続く所を1つの範囲にする。
    *layoutHints = {};
    using Kind = BodyConverter::LineKind;
    std::optional<std::pair<int, int>> run;
    Kind runKind = Kind::Normal;
    auto flush = [&]() {
      if (!run) return;
      if (run->second >= 1) {
        std::pair<int, int> range{std::max(1, run->first), run->second};
        (runKind == Kind::Horizontal ? layoutHints->horizontal : layoutHints->centered).push_back(range);
      }
      run.reset();
    };
    for (const auto &[line, kind] : converter.lineKinds()) {
      int at = line - leading;
      if (run && kind == runKind) {
        run->second = at;
        continue;
      }
      // 横書きの範囲は、後ろに続く空行等(記録の無い行)も次の行の手前まで含める(2026-09-29、
      // 直接のユーザー報告――本の最後の空行が奥付の範囲の外にあり、奥付の最下行から先へ進むと
      // 範囲の後ろへ出た扱いになって縦書きへ戻っていた)。扉は空の列の数が変わるので広げない。
      if (run && runKind == Kind::Horizontal) run->second = at - 1;
      flush();
      if (kind != Kind::Normal) {
        run = std::make_pair(at, at);
        runKind = kind;
      }
    }
    if (run && runKind == Kind::Horizontal) {  // 本の最後まで
      int total = static_cast<int>(std::count(out.begin(), out.end(), '\n'));
      run->second = std::max(run->second, total);
    }
    flush();
    for (int line : converter.alignEndLines())
      if (line - leading >= 1) layoutHints->alignEnd.push_back(line - leading);
    for (int line : converter.centerLines())
      if (line - leading >= 1) layoutHints->center.push_back(line - leading);
    for (const auto &[line, indent] : converter.indentLines())
      if (line - leading >= 1) layoutHints->indent[line - leading] = indent;
    for (const auto &[line, label] : converter.chapterLabels())
      if (line - leading >= 1) layoutHints->chapterLabel[line - leading] = label;
  }
  if (links) {
    links->clear();
    const auto &anchors = converter.anchors();
    for (const auto &link : converter.links()) {
      if (layoutHints && link.line - leading >= 1)  // 行のリンクすべて(EpubLayoutHints::lineLinks)
        layoutHints->lineLinks[link.line - leading].push_back(link.target.compare(0, 4, "url:") == 0 ? link.target
                                                                                                      : std::string());
      if (link.target.compare(0, 4, "url:") == 0) {  // Webのリンク(1行に複数あれば最初のリンク)
        int from = link.line - leading;
        if (from >= 1 && layoutHints && !links->count(from)) layoutHints->webLinks.emplace(from, link.target.substr(4));
        continue;
      }
      auto it = anchors.find(link.target);
      if (it == anchors.end()) it = anchors.find(link.target.substr(0, link.target.find('#') + 1));  // 無いフラグメントはファイル先頭へ
      if (it == anchors.end()) continue;
      int from = link.line - leading, to = std::max(1, it->second - leading);
      if (layoutHints && from >= 1) layoutHints->lineLinks[from].back() = "line:" + std::to_string(to);
      if (from >= 1 && !(layoutHints && layoutHints->webLinks.count(from))) links->emplace(from, to);
    }
  }
  return out;
}

}  // namespace fm
