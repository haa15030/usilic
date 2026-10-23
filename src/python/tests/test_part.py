import unittest

from usilic.content_view import ContentView, Line
from usilic.grep_mode import FIXED
from usilic.part_mode import PartMode, occur_lines, split_lines


class PartTest(unittest.TestCase):
    def test_split_with_leading_lines(self):
        texts = ['intro', '# A', 'a', '# B', 'b1', 'b2']
        segments = split_lines(texts, '^#')
        self.assertEqual([(s.start, s.end, s.label) for s in segments],
                         [(0, 1, '--'), (1, 3, '# A'), (3, 6, '# B')])  # 一致の前は「--」(C++のCutMode::split())

    def test_part_mode_open(self):
        mode = PartMode(ContentView(), ContentView())
        lines = [Line(t, i + 1) for i, t in enumerate(['# A', 'a', '# B', 'b'])]
        self.assertTrue(mode.split(lines, '^#', 10, 'x'))
        self.assertEqual([l.text for l in mode.toc_view.lines], ['# A', '# B'])
        mode.toc_view.cursor_index = 1
        self.assertTrue(mode.open_selected())
        self.assertEqual([l.text for l in mode.content_view.lines], ['# B', 'b'])

    def test_occur(self):
        lines = [Line(t) for t in ['x', 'ab', 'y', 'cab']]
        self.assertEqual([(i, l.text) for i, l in occur_lines(lines, 'ab', FIXED)], [(1, 'ab'), (3, 'cab')])


if __name__ == '__main__':
    unittest.main()
