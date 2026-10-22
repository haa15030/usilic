import unittest

from usilic.content_view import ContentView
from usilic.list_browser_mode import ListBrowserMode


class ListBrowserTest(unittest.TestCase):
    def test_toggle_puts_cursor_at_newest_or_top(self):
        view = ContentView()
        view.set_height(3)
        mode = ListBrowserMode(view)
        mode.show(['b', 'a', 'c', 'a'], True, 'FILE', None)     # ^X^F: ソート済み、先頭に
        self.assertEqual((mode.items, view.cursor_index), (['a', 'b', 'c'], 0))
        mode.toggle_sorted()                                      # ^R: 入力順、最後(最新)に(C++と同じ)
        self.assertEqual((view.cursor_index, mode.selected_item()), (3, 'a'))
        view.cursor_index = 1
        mode.toggle_sorted()
        self.assertEqual(view.cursor_index, 0)


if __name__ == '__main__':
    unittest.main()
