import unittest

from usilic import utf8


class Utf8Test(unittest.TestCase):
    def test_sequence_length(self):
        self.assertEqual(utf8.sequence_length(ord('a')), 1)
        self.assertEqual(utf8.sequence_length('é'.encode()[0]), 2)
        self.assertEqual(utf8.sequence_length('あ'.encode()[0]), 3)
        self.assertEqual(utf8.sequence_length('😀'.encode()[0]), 4)
        self.assertEqual(utf8.sequence_length(0x80), 1)  # 迷子の継続バイト

    def test_display_width(self):
        self.assertEqual(utf8.display_width(b'a'), 1)
        self.assertEqual(utf8.display_width('あ'.encode()), 2)
        self.assertEqual(utf8.display_width('Ａ'.encode()), 2)
        self.assertEqual(utf8.display_width('⁠'.encode()), 0)
        self.assertEqual(utf8.display_width('゙'.encode()), 0)
        self.assertEqual(utf8.display_width('★'.encode()), 2)
        self.assertEqual(utf8.display_width('é'.encode()), 1)

    def test_text_width(self):
        self.assertEqual(utf8.text_width('abcあいう'), 9)


if __name__ == '__main__':
    unittest.main()
