import os
import tempfile
import unittest
import zipfile

from usilic.epub_mode import EpubBook

OPF = '''<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0">
<metadata><dc:title>本</dc:title></metadata>
<manifest><item id="nav" href="nav.xhtml" properties="nav" media-type="application/xhtml+xml"/>
<item id="css" href="s.css" media-type="text/css"/>
<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/></manifest>
<spine><itemref idref="c1"/></spine></package>'''
NAV = '''<html><body><nav epub:type="toc"><ol><li><a href="c1.xhtml">第一章</a>
<ol><li><a href="c1.xhtml#s1">節</a></li></ol></li></ol></nav></body></html>'''
C1 = '''<html><head><title>x</title><link rel="stylesheet" type="text/css" href="s.css"/></head><body><h2>第一章</h2>
<p class="ind">本文の<b>太字</b>と<ruby>漢字<rt>かんじ</rt></ruby>。</p>
<p class="blank"><br/></p><h3 id="s1">節</h3><p><img src="i/pic.png"/></p></body></html>'''


class EpubTest(unittest.TestCase):
    def test_render(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'b.epub')
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OEBPS/c.opf"/>'
                                                     '</rootfiles></container>')
                z.writestr('OEBPS/c.opf', OPF)
                z.writestr('OEBPS/nav.xhtml', NAV)
                z.writestr('OEBPS/s.css', 'p.ind { padding-left: 1em; text-indent: 0em; }')
                z.writestr('OEBPS/c1.xhtml', C1)
            book = EpubBook()
            self.assertTrue(book.load(path))
            self.assertEqual(book.title, '本')
            lines, toc = book.render(40)
            texts = [l.text for l in lines]
            # 冒頭に書名と空の行(C++のepubToHelpMarkup()のheader)。CSSは本文のファイルが読み込む物だけ
            self.assertEqual(texts, ['本', '', '第一章', '  本文の太字と漢字《かんじ》。', '', '節', '[image: pic.png]'])
            self.assertEqual(lines[2].attributes, [(0, 3, 'bold')])
            self.assertIn((5, 7, 'bold'), lines[3].attributes)   # 字下げの分ずれる
            self.assertEqual([(t, depth) for _, t, depth in book.toc], [('第一章', 0), ('節', 1)])
            # 章の一覧(C++のCUT): 章の行の字、最初の章より前(書名)は「--」
            self.assertEqual(toc, [0, 2, 5])
            self.assertEqual(book.chapter_titles, ['--', '第一章', '節'])


if __name__ == '__main__':
    unittest.main()
