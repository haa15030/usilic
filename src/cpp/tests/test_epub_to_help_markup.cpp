#include <map>

#include "doctest.h"
#include "epub_to_help_markup.hpp"

// 2026-09-28追加(.steering/20260928-epub-viewer-stage1/参照): epubToHelpMarkup()の
// 単体テスト。zipの代わりに連想配列からメンバーを返す。

namespace {

fm::EpubMemberReader readerFor(const std::map<std::string, std::string> &files) {
  return [files](const std::string &member) -> std::optional<std::string> {
    auto it = files.find(member);
    if (it == files.end()) return std::nullopt;
    return it->second;
  };
}

const char *kContainer =
    R"(<?xml version="1.0"?><container><rootfiles><rootfile full-path="OEBPS/content.opf"/></rootfiles></container>)";

}  // namespace

TEST_CASE("epubToHelpMarkup: no container.xml or OPF means not convertible") {
  CHECK_FALSE(fm::epubToHelpMarkup(readerFor({})));
  CHECK_FALSE(fm::epubToHelpMarkup(readerFor({{"META-INF/container.xml", kContainer}})));
}

TEST_CASE("epubToHelpMarkup: EPUB3 -- title/creator header, spine order, ruby, nav chapters") {
  std::map<std::string, std::string> files = {
      {"META-INF/container.xml", kContainer},
      {"OEBPS/content.opf", R"(<package><metadata xmlns:dc="x"><dc:title>本の名前</dc:title><dc:creator>著者</dc:creator></metadata>
<manifest>
  <item id="a" href="Text/a.xhtml" media-type="application/xhtml+xml"/>
  <item id="b" href="Text/b%20c.xhtml" media-type="application/xhtml+xml"/>
  <item id="nav" href="Text/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
</manifest>
<spine><itemref idref="b"/><itemref idref="a"/></spine></package>)"},
      {"OEBPS/Text/nav.xhtml", R"(<html><body><nav epub:type="toc"><h2>目次</h2><ol>
<li><a href="../Text/a.xhtml#t1"><ruby>燈<rt>ひ</rt></ruby>の章</a></li>
<li><a href="b%20c.xhtml">表紙</a></li></ol></nav></body></html>)"},
      {"OEBPS/Text/b c.xhtml", R"(<html><head><title>x</title></head><body>
<h2 id="toc_id_1"></h2>
<p><img alt="title" src="../Image/title.jpg"/></p></body></html>)"},
      {"OEBPS/Text/a.xhtml", R"(<html><body>
<p class="title" id="t1">　燈の章</p>
<p>　<ruby>燈<rp>(</rp><rt>ひ</rt><rp>)</rp></ruby>色の
   場&amp;<b>太字</b></p>
<p><br/></p>
<p>次の行</p></body></html>)"},
  };
  auto out = fm::epubToHelpMarkup(readerFor(files));
  REQUIRE(out);
  CHECK(*out ==
        "<b>本の名前</b>\n"
        "著者\n"
        "\n"
        "<b>表紙</b> [image: title.jpg]\t\n"  // 空の見出し+画像だけの段落は1行にまとめる
        "\n"
        "　燈の章\t\n"  // 目次が指す段落そのものが章の行
        "　燈《ひ》色の場&<b>太字</b>\n"  // 和文の間の改行は詰める
        "\n"
        "次の行\n"
        "\n");
}

TEST_CASE("epubToHelpMarkup: EPUB2 ncx labels, headings are chapters, linear=no skipped") {
  std::map<std::string, std::string> files = {
      {"META-INF/container.xml", kContainer},
      {"OEBPS/content.opf", R"(<package><manifest>
  <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
  <item id="s" href="Text/s.xhtml" media-type="application/xhtml+xml"/>
  <item id="c" href="Text/c.xhtml" media-type="application/xhtml+xml"/>
</manifest><spine toc="ncx"><itemref idref="c" linear="no"/><itemref idref="s"/></spine></package>)"},
      {"OEBPS/toc.ncx", R"(<ncx><navMap><navPoint><navLabel><text>Start</text></navLabel>
<content src="Text/s.xhtml"/></navPoint></navMap></ncx>)"},
      {"OEBPS/Text/c.xhtml", "<html><body><p>cover</p></body></html>"},
      {"OEBPS/Text/s.xhtml", R"(<html><body><p>〓題〓</p><h2>　目次</h2><p>a	b</p>
<h3>第<i>一</i>章</h3><p><b>強<strong>調</strong></b></p><ul><li>x</li></ul><ol><li>y</li><li>z</li></ol></body></html>)"},
  };
  auto out = fm::epubToHelpMarkup(readerFor(files));
  REQUIRE(out);
  CHECK(*out ==
        "〓題〓\t\n"  // ncxの"Start"(フラグメント無し)はファイル先頭の段落
        "\n"
        "<b>　目次</b>\t\n"
        "a b\n"  // 本文中のタブは章の印にならない
        "\n"
        "<b>第<i>一</i>章</b>\t\n"
        "<b>強調</b>\n"  // 太字の入れ子はトグルなので外側の1組だけ
        "・x\n"
        "1. y\n"
        "2. z\n"
        "\n");
}

TEST_CASE("epubToHelpMarkup: empty heading followed by text keeps the TOC label as its own line; "
          "findEpubImageMember() maps a file name to its zip path") {
  std::map<std::string, std::string> files = {
      {"META-INF/container.xml", kContainer},
      {"OEBPS/content.opf", R"(<package><manifest>
  <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
  <item id="a" href="Text/a.xhtml" media-type="application/xhtml+xml"/>
  <item id="i" href="Image/title%201.jpg" media-type="image/jpeg"/>
</manifest><spine><itemref idref="a"/></spine></package>)"},
      {"OEBPS/nav.xhtml", R"(<nav epub:type="toc"><a href="Text/a.xhtml#x">章</a></nav>)"},
      {"OEBPS/Text/a.xhtml", R"(<body><h2 id="x"></h2><p>本文<img src="../Image/title%201.jpg"/></p></body>)"},
  };
  auto out = fm::epubToHelpMarkup(readerFor(files));
  REQUIRE(out);
  CHECK(*out == "<b>章</b>\t\n本文[image: title 1.jpg]\n\n");
  CHECK(fm::findEpubImageMember(readerFor(files), "title 1.jpg") == std::optional<std::string>("OEBPS/Image/title 1.jpg"));
  CHECK_FALSE(fm::findEpubImageMember(readerFor(files), "none.jpg"));
}

TEST_CASE("epubToHelpMarkup: in-book links are underlined and mapped to their target line; "
          "external links are plain") {
  std::map<std::string, std::string> files = {
      {"META-INF/container.xml", kContainer},
      {"OEBPS/content.opf", R"(<package><manifest>
  <item id="a" href="a.xhtml" media-type="application/xhtml+xml"/>
  <item id="b" href="b.xhtml" media-type="application/xhtml+xml"/>
</manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>)"},
      {"OEBPS/a.xhtml", R"(<body><h2>目次</h2><p><a href="b.xhtml#c1">第一章</a></p><p>第二章</p>
<p><a href="http://example.com/">外</a></p><p>　<a href="b.xhtml">先頭</a></p></body>)"},
      {"OEBPS/b.xhtml", R"(<body><p>前</p><h2 id="c1">第一章</h2><p>本文</p></body>)"},
  };
  std::map<int, int> links;
  auto out = fm::epubToHelpMarkup(readerFor(files), &links);
  REQUIRE(out);
  CHECK(*out ==
        "<b>目次</b>\t\n"
        "<u>第一章</u>\n"
        "第二章\n"
        "外\n"
        "　<u>先頭</u>\n"
        "\n"
        "前\n"
        "\n"
        "<b>第一章</b>\t\n"
        "本文\n"
        "\n");
  CHECK(links == std::map<int, int>{{2, 9}, {5, 7}});
}

// 2026-10-15(EpubLayoutHints::notes): <aside epub:type="footnote">の行の範囲を、注釈ごとに返す(HELP.epubの注釈)。
TEST_CASE("epubToHelpMarkup: footnote asides are reported as note ranges") {
  std::map<std::string, std::string> files = {
      {"META-INF/container.xml", kContainer},
      {"OEBPS/content.opf", R"(<package><metadata/><manifest>
  <item id="a" href="a.xhtml" media-type="application/xhtml+xml"/>
  <item id="n" href="n.xhtml" media-type="application/xhtml+xml"/>
</manifest><spine><itemref idref="a"/><itemref idref="n"/></spine></package>)"},
      {"OEBPS/a.xhtml", R"(<html><body><p>本文 <a href="n.xhtml#n1">BUFFER</a></p></body></html>)"},
      {"OEBPS/n.xhtml", R"(<html><body><h2>注釈</h2>
<aside id="n1" epub:type="footnote"><p>BUFFER</p><p>書式: サイズ</p></aside>
<aside id="n2" epub:type="footnote"><p>CODE</p></aside>
<aside id="x"><p>注釈ではない</p></aside></body></html>)"},
  };
  std::map<int, int> links;
  fm::EpubLayoutHints hints;
  auto out = fm::epubToHelpMarkup(readerFor(files), &links, &hints);
  REQUIRE(out);
  std::vector<std::string> lines;
  for (std::size_t p = 0, q; p < out->size(); p = q + 1) {
    q = out->find('\n', p);
    if (q == std::string::npos) q = out->size();
    lines.push_back(out->substr(p, q - p));
  }
  auto textOf = [&](int from, int until) {
    std::string s;
    for (int i = from; i <= until; i++) s += lines[i - 1] + "|";
    return s;
  };
  REQUIRE(hints.notes.size() == 2);
  CHECK(textOf(hints.notes[0].first, hints.notes[0].second) == "<serif>BUFFER</serif>|<serif>書式: サイズ</serif>|");
  CHECK(textOf(hints.notes[1].first, hints.notes[1].second) == "<serif>CODE</serif>|");
  REQUIRE(links.size() == 1);
  CHECK(links.begin()->second == hints.notes[0].first);
}

// 2026-10-15: KDP(Kindle Publishing Guidelines)の注釈の3つの書き方が、どれも注釈の区画になること――
// <aside epub:type="footnote">、<li epub:type="endnote">(<section epub:type="endnotes">の入れ物は注釈ではない)、
// epub:typeが無く双方向のリンクだけでつながった章末の段落。目次と章の見出しの行き来は注釈にしない。
TEST_CASE("epubToHelpMarkup: KDP-style notes -- footnote asides, endnote list items, bidirectional paragraphs") {
  std::map<std::string, std::string> files = {
      {"META-INF/container.xml", kContainer},
      {"OEBPS/content.opf", R"(<package><metadata/><manifest>
  <item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>
  <item id="c2" href="c2.xhtml" media-type="application/xhtml+xml"/>
  <item id="n" href="notes.xhtml" media-type="application/xhtml+xml"/>
</manifest><spine page-progression-direction="rtl"><itemref idref="c1"/><itemref idref="c2"/><itemref idref="n"/></spine></package>)"},
      {"OEBPS/c1.xhtml", R"(<html><body><h2 id="h1">第一章</h2>
<p>本文<a id="ref1" href="notes.xhtml#fn1" epub:type="noteref">注1</a></p>
<p><a href="notes.xhtml#top">注の章へ</a></p></body></html>)"},
      {"OEBPS/c2.xhtml", R"(<html><body><h2>第二章</h2>
<p>本文<a id="r3" href="#n3">※3</a>と<a id="r4" href="notes.xhtml#en4" epub:type="noteref">注4</a></p>
<p id="n3"><a href="#r3">※3</a> 章末の注</p></body></html>)"},
      {"OEBPS/notes.xhtml", R"(<html><body><h2 id="top">注</h2>
<p><a href="c1.xhtml#h1">第一章へ</a></p>
<aside id="fn1" epub:type="footnote"><p><a href="c1.xhtml#ref1">注1</a> 脚注</p><p>二段落目</p></aside>
<section epub:type="endnotes"><ol><li id="en4" epub:type="endnote"><p><a href="c2.xhtml#r4">注4</a> 後注</p></li></ol></section>
</body></html>)"},
  };
  std::map<int, int> links;
  fm::EpubLayoutHints hints;
  auto out = fm::epubToHelpMarkup(readerFor(files), &links, &hints);
  REQUIRE(out);
  std::vector<std::string> lines;
  for (std::size_t p = 0, q; p < out->size(); p = q + 1) {
    q = out->find('\n', p);
    if (q == std::string::npos) q = out->size();
    lines.push_back(out->substr(p, q - p));
  }
  std::vector<std::string> notes;
  for (const auto &[from, until] : hints.notes) {
    std::string s;
    for (int i = from; i <= until; i++) s += lines[i - 1] + "|";
    notes.push_back(s);
  }
  REQUIRE(notes.size() == 3);
  CHECK(notes[0].find("章末の注") != std::string::npos);
  CHECK(notes[0].find("本文") == std::string::npos);
  CHECK(notes[1].find("脚注") != std::string::npos);
  CHECK(notes[1].find("二段落目") != std::string::npos);
  CHECK(notes[2].find("後注") != std::string::npos);
  CHECK(notes[2].find("脚注") == std::string::npos);
}
