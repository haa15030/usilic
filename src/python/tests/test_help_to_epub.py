import io
import unittest
import zipfile

from usilic import help_to_epub as h


class HelpToEpubTest(unittest.TestCase):
    def test_markup_to_roff_lines(self):
        roff = h.convert_markup_to_ms_roff_source('名称\n<indent4>あ<b>い</b>う\t\n</indent>\n')
        self.assertIn('ユー\\f[JPM]ザ', roff.split('\n')[0])
        self.assertIn('.in 4n\n', roff)
        self.assertIn('\\fBい\\fR', roff)

    def test_english_title(self):
        roff = h.convert_markup_to_ms_roff_source('NAME\n<indent4>usilic</indent>\n')
        self.assertTrue(h.is_english_roff(roff))

    def test_epub_has_chapters_and_notes(self):
        markup = 'NAME\n<indent4>see <u>URL</u> here</indent>\nMORE\n<indent4>text</indent>\n'
        roff = h.convert_markup_to_ms_roff_source(markup)
        data, summary, missing = h.convert_ms_roff_to_help_epub(roff, 'URL\n# the url setting\n', '')
        names = zipfile.ZipFile(io.BytesIO(data)).namelist()
        self.assertEqual(names[0], 'mimetype')
        self.assertIn('OEBPS/Text/ch01.xhtml', names)
        self.assertIn('OEBPS/Text/notes.xhtml', names)
        self.assertTrue(summary.startswith('3 chapters'))
        self.assertEqual(missing, [])


if __name__ == '__main__':
    unittest.main()
