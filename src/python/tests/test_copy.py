import unittest

from usilic.content_view import ContentView, Line
from usilic.copy_mode import CopyMode


def view(texts):
    v = ContentView()
    v.set_height(10)
    v.set_lines([Line(t, i + 1) for i, t in enumerate(texts)])
    return v


class CopyTest(unittest.TestCase):
    def test_region_over_lines(self):
        c = CopyMode(view(['abc', 'def', 'ghi']))
        c.move_col(1)
        c.toggle(False)
        c.move_row(1)
        c.toggle(False)
        self.assertEqual(c.joined_text(), 'bc\nd')

    def test_rectangle_uses_columns(self):
        c = CopyMode(view(['日本語', 'abcdef']))
        c.move_col(1)          # 「本」(桁2)
        c.toggle(True)
        c.move_row(1)          # 2行目の桁2(c)
        c.move_col(2)          # 桁4(e)
        c.toggle(True)
        self.assertEqual(c.joined_text(), '本\ncd')

    def test_several_regions(self):
        c = CopyMode(view(['one two']))
        c.toggle(False)
        c.move_col(3)
        c.toggle(False)
        c.move_col(1)
        c.toggle(False)
        c.line_end()
        c.toggle(False)
        self.assertEqual(c.joined_text(), 'one two')  # 範囲が2つ以上なら空白でつなぐ(C++のconfirmMark())
        self.assertEqual(c.confirmed_text(), 'one two')
        self.assertEqual(c.all_regions(), [])


if __name__ == '__main__':
    unittest.main()
