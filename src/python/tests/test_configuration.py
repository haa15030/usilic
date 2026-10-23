import unittest

from usilic import configuration as C

RC = '''# usilicrc
ARCHIVE
\t.cbz\t.zip
FILTER
\t.gz\tgzip\t-dc
LAUNCH
\t.info\tinfo\t-f
GREP
\t"^  [0-9]+\\."            # 章の見出し
\t"a # b"
BUFFER
\t8
CODE
# ^A ^G
   1  7  8  9  10
LINE
\t100
COPY
\tcp -vRi
KEY
    K-1   C-y
'''


class ConfigurationTest(unittest.TestCase):
    def setUp(self):
        self.c = C.Configuration()
        self.assertTrue(self.c.load_rc_text(RC))

    def test_sections(self):
        self.assertEqual(self.c.filter_for('/x/a.tar.gz'), ['gzip', '-dc'])
        self.assertEqual(self.c.launch_for('a.info'), ['info', '-f'])
        self.assertEqual(self.c.grep_examples, ['^  [0-9]+\\.', 'a # b'])  # ""の中の#は字
        self.assertEqual(self.c.buffer_limit_bytes(), 8 * 1024 * 1024)
        self.assertEqual(self.c.retention_count, 100)
        self.assertEqual(self.c.command_for('Cp'), ['cp', '-vRi'])
        self.assertEqual(self.c.command_for('Mv'), ['mv', '-v'])  # 書かなければ既定
        self.assertEqual(self.c.key_bindings.feed(0x19, 'all', [[], None]), [ord('j')])  # 既定のjへ読み替える

    def test_archive_suffixes(self):
        self.assertEqual(self.c.archive_for('a.TGZ')[:1], (C.TAR,))
        self.assertEqual(self.c.archive_for('a.tar.gz')[3], '(tar)')
        self.assertEqual(self.c.archive_for('a.cbz')[3], '(unzip)')   # ARCHIVEの別名
        self.assertEqual(self.c.archive_for('b.epub')[0], C.ZIP)     # .epubは組み込みで.zipの別名
        self.assertIsNone(self.c.archive_for('a.txt'))

    def test_binary(self):
        self.assertIsNone(self.c.looks_binary(b'abc\n\t\x07'))
        self.assertEqual(self.c.looks_binary(b'abc\x00'), 0)
        self.assertEqual(self.c.looks_binary(b'ab\x1bc'), 0x1B)  # CODEに無い

    def test_unknown_section_stops(self):
        c = C.Configuration()
        self.assertFalse(c.load_rc_text('BUFFER\n\t9\nNOSUCH\nLINE\n\t5\n'))
        self.assertEqual(c.buffer_megabytes, 9)
        self.assertEqual(c.retention_count, 0)


if __name__ == '__main__':
    unittest.main()
