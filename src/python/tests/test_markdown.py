import unittest

from usilic.grep_mode import FIXED, GrepMode
from usilic.markdown import markdown_to_markup, markup_lines, render_markdown


class MarkdownTest(unittest.TestCase):
    def test_markup(self):
        source = '# 題\n## 節\n### 小節\n本文の**太字**と*斜体*と`code`、[名](u)。\n> 引用\n- 項目\n  - 入れ子\n---\n'
        self.assertEqual(markdown_to_markup(source).split('\n'), [
            '<u><b>題</b></u>', '<b>節</b>', '<indent02><b>小節</b></indent>',
            '本文の<b>太字</b>と<i>斜体</i>と<u>code</u>、<u>名</u> (u)。',
            '<indent02><i>引用</i></indent>', '<indent00>- 項目</indent>', '<indent02>- 入れ子</indent>', '-' * 60])

    def test_fence_table_and_less_than(self):
        source = '```py\na < b\n```\n| x | yy |\n|---|---|\n| 1 | 2 |\n'
        self.assertEqual(markdown_to_markup(source).split('\n'), [
            '<indent04>a <lt> b</indent>', '<indent02><b>x</b>  <b>yy</b></indent>', '<indent02>-  --</indent>',
            '<indent02>1  2</indent>'])

    def test_emphasis_rules(self):
        self.assertEqual(markdown_to_markup('snake_case_name と __強__'), 'snake_case_name と <b>強</b>')
        self.assertEqual(markdown_to_markup('全角の空白　\n次'), '全角の空白　 次')  # 全角の空白は字のまま

    def test_lines(self):
        lines = markup_lines('<u><b>題</b></u>\n<indent04>a <lt> b</indent>\n<b>続\nく</b>')
        self.assertEqual(lines[0], ('題', [(0, 1, 'bold'), (0, 1, 'underline')], 0))
        self.assertEqual(lines[1], ('a < b', [], 4))
        self.assertEqual(lines[3], ('く', [(0, 1, 'bold')], 0))   # 行をまたぐ太字
        shown = render_markdown('    x\n```\n' + 'y' * 30 + '\n```\n', 20)
        self.assertEqual([l.text for l in shown], ['    x', '    ' + 'y' * 16, '    ' + 'y' * 14])


class GrepWrapTest(unittest.TestCase):
    def test_occurrences_wrap(self):
        g = GrepMode()
        g.search(['a22b22', 'x', 'c22'], '22', FIXED, 0)
        self.assertEqual(g.position_text(), '1/3')       # 出現で数える(1行に2つあれば2つ)
        g.move(1)
        g.move(1)
        self.assertEqual((g.position_text(), g.current_line()), ('3/3', 2))
        g.move(1)                                         # 最後の次は最初へ巡回
        self.assertEqual(g.position_text(), '1/3')
        g.move(-1)
        self.assertEqual(g.position_text(), '3/3')


if __name__ == '__main__':
    unittest.main()
