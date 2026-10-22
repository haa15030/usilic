import unittest

from usilic.grep_mode import GrepMode, FIXED, REGEX, join_lines_for_grep, match_spans, grep_line_indices


class GrepModeTest(unittest.TestCase):
    def test_join_keeps_line_numbers(self):
        # 行の中の改行・NULは空白に(grepの行番号がずれない)
        self.assertEqual(join_lines_for_grep(['a\nb', 'c\0d']), 'a b\nc d\n')

    def test_grep_line_indices(self):
        lines = ['line %d' % i for i in range(1, 51)]
        self.assertEqual(grep_line_indices(lines, '^line 4[0-9]$', REGEX), list(range(39, 49)))
        self.assertEqual(len(grep_line_indices(lines, 'line 2', FIXED)), 11)  # 2, 20〜29
        self.assertEqual(grep_line_indices(['a\nx', 'b', 'x'], 'x', FIXED), [2])  # 1行目の中の改行はxでない

    def test_match_spans(self):
        self.assertEqual(match_spans('abcabc', 'bc', FIXED), [(1, 3), (4, 6)])
        self.assertEqual(match_spans('line 42', '4[0-9]', REGEX), [(5, 7)])

    def test_search_starts_at_first_match(self):
        g = GrepMode()
        g.search(['x', 'y', 'x'], 'x', FIXED, origin=1)
        self.assertEqual((g.position_text(), g.current_line()), ('1/2', 0))
        g.move(1)
        self.assertEqual(g.current_line(), 2)


if __name__ == '__main__':
    unittest.main()
