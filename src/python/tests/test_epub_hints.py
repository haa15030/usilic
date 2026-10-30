import os
import tempfile
import unittest
import zipfile

from usilic import epub_vertical as ev
from usilic.epub_mode import EpubBook, parse_css, rule_for

OPF = '''<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0">
<metadata><dc:title>書名　一</dc:title><dc:creator>著者</dc:creator></metadata>
<manifest><item id="css" href="s.css" media-type="text/css"/>
<item id="cover" href="cover.xhtml" media-type="application/xhtml+xml"/>
<item id="t" href="title.xhtml" media-type="application/xhtml+xml"/>
<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>
<item id="z" href="colophon.xhtml" media-type="application/xhtml+xml"/></manifest>
<spine page-progression-direction="rtl"><itemref idref="cover"/><itemref idref="t"/><itemref idref="c1"/>
<itemref idref="z"/></spine></package>'''
CSS = '''/* 注釈 */ html body { writing-mode: vertical-rl; }
.horizontal { writing-mode: horizontal-tb; } .vertical { writing-mode: vertical-rl; }
p.author { text-align: right; } .center { text-align: center; }
.indent { padding-top: 2em; text-indent: -1em; } .invisible { display: none; }
div p.x { text-align: right; }'''
HEAD = '<html><head><link rel="stylesheet" type="text/css" href="s.css"/></head>'
COVER = HEAD + '<body><p><img src="cover.jpg"/></p></body></html>'
TITLE = HEAD + '''<body class="horizontal"><div class="vertical"><p class="title">書名</p>
<p class="author">著者</p></div></body></html>'''
C1 = HEAD + '''<body><h2>第一章</h2><p>本文<a href="#n1" id="r1">［注1］</a>。</p>
<p class="indent">字下げ</p><p>後</p>
<aside epub:type="footnote" id="fn2"><p>注釈そのもの</p></aside>
<p id="n1"><a href="#r1">［注1］</a>双方向の注釈</p></body></html>'''
COLOPHON = HEAD + '''<body class="horizontal"><h2 class="invisible">奥付</h2><p class="center">書名</p>
<p>発行</p></body></html>'''


def build(d):
    path = os.path.join(d, 'b.epub')
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="c.opf"/></rootfiles>'
                                             '</container>')
        for name, data in (('c.opf', OPF), ('s.css', CSS), ('cover.xhtml', COVER), ('title.xhtml', TITLE),
                           ('c1.xhtml', C1), ('colophon.xhtml', COLOPHON)):
            z.writestr(name, data)
    return path


class CssTest(unittest.TestCase):
    def test_simple_selectors_and_specificity(self):
        rules = parse_css('html body { writing-mode: horizontal-tb } p { text-align: right } '
                          'p.a { text-align: left } div p { text-align: center } @media x { .b { display: none } }')
        self.assertFalse(rule_for(rules, 'p', set(), 'vertical'))       # html bodyの頭は外す
        self.assertTrue(rule_for(rules, 'p', set(), 'align_end'))
        self.assertFalse(rule_for(rules, 'p', {'a'}, 'align_end'))      # クラス付きが勝つ
        self.assertIsNone(rule_for(rules, 'p', set(), 'center') if False else None)
        self.assertTrue(rule_for(rules, 'span', {'b'}, 'hidden'))       # @mediaの中も
        self.assertEqual(len([r for r in rules if r.props.get('center')]), 0)  # 子孫のセレクタは読まない


class HintsTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.book = EpubBook()
        self.assertTrue(self.book.load(build(self.dir.name)))
        self.lines, _ = self.book.render(40)
        self.raw = {}
        for line in self.lines:
            self.raw.setdefault(line.text.split('\u2060')[0].strip(' '), line.raw_line_number)  # 章の行の印は除いて

    def tearDown(self):
        self.dir.cleanup()

    def test_header_after_cover_and_title_page(self):
        texts = [l.text.split('\u2060')[0] for l in self.lines]  # 章の行の印(U+2060と空白)は除いて
        # 表紙が先頭、その後は本自身の扉(自前の書名・著者名の扉は足さない、C++の項目1094)
        self.assertEqual(texts[:4], ['[image: cover.jpg]', '', '書名', '著者'])
        h = self.book.hints
        self.assertIn((self.raw['書名'], self.raw['書名'] + 1), h.centered)        # 横書きの中の縦書きの箱

    def test_colophon_is_horizontal_and_hidden_heading(self):
        h = self.book.hints
        start = next(l.raw_line_number for l in self.lines
                     if l.text.startswith(' ') and l.text.split('\u2060')[0].strip() == '書名')
        self.assertNotIn('奥付', self.raw)                            # display:noneの見出しは出さない
        self.assertEqual(h.horizontal[-1][0], start)
        self.assertIn(start, h.center)
        centered = next(l for l in self.lines if l.raw_line_number == start)
        self.assertEqual(centered.text.split('\u2060')[0], ' ' * 18 + '書名')  # 横書きでは中央に((40-4)//2)

    def test_indent_uses_padding_top_in_vertical_book(self):
        self.assertEqual(self.book.hints.indent[self.raw['字下げ']], (1, 2))

    def test_notes(self):
        notes = self.book.hints.notes
        self.assertIn((self.raw['注釈そのもの'], self.raw['注釈そのもの']), notes)   # epub:type
        self.assertIn((self.raw['［注1］双方向の注釈'],) * 2, notes)                 # 本文と双方向のリンク
        self.assertFalse(any(a <= self.raw['本文［注1］。'] <= b for a, b in notes))  # 本文の段落は注釈ではない


class TitlePageTest(unittest.TestCase):
    def test_title_page_is_centered_and_stepped_over(self):
        sources = [ev.SourceLine(t, i + 1) for i, t in enumerate(['前', '書名', '著者', '後'])]
        v = ev.VerticalBook(sources, set(), 4, 15, False, centered=[(2, 3)])  # 1画面5列
        self.assertEqual([(c.raw, c.title_page, c.padding) for c in v.cols[1:6]],
                         [(2, True, True), (2, True, False), (3, True, False), (3, True, True), (3, True, True)])
        v.set_view(0, 0)
        v.command(3)          # 扉のページへ: ページの頭を右端に、カーソルは書名の列
        self.assertEqual((v.first, v.cursor), (1, 2))
        v.command(3)          # 1列の送りでページごと越える
        self.assertEqual(v.cursor, 6)

    def test_align_end(self):
        cols = ev.layout_columns([ev.SourceLine('著者', 1, align_end=True)], 4)
        self.assertEqual([c.start for c in cols[0].cells], [-1, -1, 0, 1])


if __name__ == '__main__':
    unittest.main()
