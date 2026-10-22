import unittest

from usilic import help_text as H
from usilic.content_view import Line


class HelpTextTest(unittest.TestCase):
    def test_key_bytes_from_label(self):
        self.assertEqual(H.key_bytes_from_label('             ^J^H：プロンプトから HELP'), [0x0A, 0x08])
        self.assertEqual(H.key_bytes_from_label('        ␣：ページ送り'), [0x20])
        self.assertEqual(H.key_bytes_from_label('       ^␣：マーク'), [0x80])
        self.assertEqual(H.key_bytes_from_label('              ^N/^P：移る'), [0x0E])
        self.assertEqual(H.key_bytes_from_label('ファイル閲覧     ：ページャー'), [])

    def test_is_key_assignment_label(self):
        self.assertTrue(H.is_key_assignment_label('      ^[<：先頭へ'))
        self.assertTrue(H.is_key_assignment_label('        .：選択'))
        self.assertFalse(H.is_key_assignment_label('    HELP         ：マニュアル'))
        self.assertFalse(H.is_key_assignment_label('④CP/MV        ：ファイル複写/移動'))

    def test_key_source_lines_keep_continuations(self):
        lines = [Line('案内：一', 1), Line('     ^H：長い説', 2, ends_with_newline=False), Line('明', 2),
                 Line('本文', 3)]
        self.assertEqual([l.text for l in H.key_source_lines(lines)], ['案内：一', '     ^H：長い説', '明'])


if __name__ == '__main__':
    unittest.main()
