import unittest

from usilic.content_view import ContentView, Line, wrap_text, expand_tabs


def view(n, height):
    v = ContentView()
    v.set_lines([Line('line %d' % i, i) for i in range(1, n + 1)])
    v.set_height(height)
    return v


class ContentViewTest(unittest.TestCase):
    def test_wrap_text_by_display_width(self):
        self.assertEqual(wrap_text('あいうえお', 4), ['あい', 'うえ', 'お'])
        self.assertEqual(wrap_text('abcdef', 4), ['abcd', 'ef'])
        self.assertEqual(wrap_text('', 4), [''])

    def test_wrap_text_kinsoku(self):
        # 「。」が行頭に来るなら、前の行の最後の「す」を次の行へ追い出す
        self.assertEqual(wrap_text('ですす。あ', 6), ['です', 'す。あ'])

    def test_expand_tabs(self):
        self.assertEqual(expand_tabs('a\tb'), 'a       b')
        self.assertEqual(expand_tabs('あ\tb'), 'あ      b')

    def test_moves(self):
        v = view(500, 39)
        v.move_down()
        self.assertEqual(v.cursor_index, 1)
        v.scroll_line_down()
        self.assertEqual(v.top_index, 1)
        v.go_to_bottom()
        self.assertEqual((v.cursor_index, v.top_index), (499, 461))
        v.go_to_top()
        v.page_down()
        self.assertEqual(v.top_index, 39)
        v.page_up()
        self.assertEqual(v.top_index, 0)
        v.go_to_line(100)
        self.assertEqual(v.cursor_index, 99)

    def test_short_list_does_not_scroll(self):
        v = view(3, 39)
        v.page_down()
        self.assertEqual((v.top_index, v.cursor_index), (0, 2))


if __name__ == '__main__':
    unittest.main()
