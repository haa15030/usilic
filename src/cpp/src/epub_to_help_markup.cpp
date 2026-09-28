#include "epub_to_help_markup.hpp"

#include <algorithm>
#include <cctype>
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

const std::set<std::string> kBlockElements = {
    "p",       "div",     "li",     "blockquote", "dd",     "dt",      "tr",   "section", "article",
    "aside",   "header",  "footer", "nav",        "figure", "figcaption", "table", "ul", "ol",
    "dl",      "body",    "pre",    "caption",    "main",   "address"};

class BodyConverter {
 public:
  explicit BodyConverter(std::string &out) : out_(out) {}

  void convert(const std::string &xhtml, const std::vector<TocEntry> *tocEntries) {
    tocEntries_ = tocEntries;
    pendingLabel_.reset();
    if (tocEntries_)
      for (const auto &e : *tocEntries_)
        if (e.fragment.empty()) {
          pendingLabel_ = e.label;
          pendingDepth_ = 0;
          break;
        }
    depth_ = skipDepth_ = preDepth_ = 0;
    listStack_.clear();
    for (const auto &t : tokenizeXml(xhtml)) handle(t);
    endLine(false);
    flushDeferredLabel();
    if (pendingLabel_) emitLabelLine();
  }

 private:
  void handle(const XmlToken &t) {
    const std::string &name = t.name;
    if (t.kind == XmlToken::Text) {
      if (skipDepth_ == 0) text(t.text);
      return;
    }
    bool open = (t.kind == XmlToken::Open || t.kind == XmlToken::SelfClose);
    bool close = (t.kind == XmlToken::Close || t.kind == XmlToken::SelfClose);
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

    if (open) {
      std::string id = attr(t, "id");
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
        if (name == "h1") changeStyle(underline_, +1, "u");
        changeStyle(bold_, +1, "b");
      }
      if (close) {
        // 空の見出し(<h2 id="toc_id_1"></h2>+次の段落に画像だけ、等)は目次の
        // ラベルを章の名前にする。次の行が画像だけならその行とまとめて
        // 「表紙 [image: title.jpg]」の1行にする(endLine()参照)。
        if (!lineVisible_ && pendingLabel_) deferredLabel_ = *pendingLabel_;
        pendingLabel_.reset();
        changeStyle(bold_, -1, "b");
        if (name == "h1") changeStyle(underline_, -1, "u");
        endLine(true);
      }
    } else if (name == "br") {
      if (open) forceNewline();
    } else if (name == "hr") {
      if (open) {
        endLine(false);
        flushDeferredLabel();
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
    } else if (name == "rt") {
      if (open) text("《");
      if (close) text("》");
    } else if (name == "b" || name == "strong") {
      changeStyle(bold_, open ? +1 : -1, "b");
    } else if (name == "i" || name == "em" || name == "cite") {
      changeStyle(italic_, open ? +1 : -1, "i");
    } else if (name == "u") {
      changeStyle(underline_, open ? +1 : -1, "u");
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
    if (t.kind == XmlToken::Close) depth_--;
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
      openStylesAtLineStart();
      // CSS Text 3の改行の変換と同じく、和文(非ASCII)どうしの間のソース上の
      // 改行はスペースにしない(「色の\n   場」→「色の場」)。
      bool nonAscii = static_cast<unsigned char>(c) >= 0x80;
      if (pendingSpace_ && lineVisible_ && !(pendingNewline_ && nonAscii && lastNonAscii_)) line_ += ' ';
      pendingSpace_ = pendingNewline_ = false;
      line_ += c;
      lineVisible_ = true;
      lineImageOnly_ = false;
      lastNonAscii_ = nonAscii;
    }
  }

  void closeStylesAtLineEnd() {
    if (italic_) line_ += "</i>";
    if (bold_) line_ += "</b>";
    if (underline_) line_ += "</u>";
  }

  // 文字のある行を出す(chapter: 行末にタブ)。文字が無ければ何もしない。
  void endLine(bool chapter) {
    pendingSpace_ = pendingNewline_ = false;
    if (!lineVisible_) {
      line_.clear();
      return;
    }
    if (deferredLabel_ && lineImageOnly_) {
      line_ = "<b>" + *deferredLabel_ + "</b> " + line_;
      deferredLabel_.reset();
      chapter = true;
    }
    flushDeferredLabel();
    lineImageOnly_ = false;
    closeStylesAtLineEnd();
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

  void flushDeferredLabel() {
    if (!deferredLabel_) return;
    if (!deferredLabel_->empty()) out_ += "<b>" + *deferredLabel_ + "</b>\t\n";
    deferredLabel_.reset();
  }

  void emitLabelLine() {
    std::string label = *pendingLabel_;
    pendingLabel_.reset();
    if (label.empty()) return;
    out_ += "<b>" + label + "</b>\t\n";
  }

  std::string &out_;
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

std::optional<std::string> epubToHelpMarkup(const EpubMemberReader &readMember) {
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
  if (!title.empty()) out += "<u><b>" + title + "</b></u>\n";
  if (!creator.empty()) out += creator + "\n";
  if (!out.empty()) out += "\n";

  BodyConverter converter(out);
  for (const auto &idref : spine) {
    auto it = manifest.find(idref);
    if (it == manifest.end()) continue;
    const ManifestItem &item = it->second;
    if (item.mediaType.find("html") == std::string::npos && item.mediaType.find("xml") == std::string::npos) continue;
    auto xhtml = readMember(item.path);
    if (!xhtml) continue;
    auto tocIt = toc.find(item.path);
    converter.convert(*xhtml, tocIt == toc.end() ? nullptr : &tocIt->second);
    if (!out.empty() && out.compare(out.size() - 2, 2, "\n\n") != 0) out += '\n';
  }
  return out;
}

}  // namespace fm
