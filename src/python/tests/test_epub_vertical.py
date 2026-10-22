import unittest

from usilic import epub_vertical as ev


def book(texts, rows=4, columns=9, chapters=(), breaks=False, links=None):
    sources = [ev.SourceLine(t, i + 1, links=(links or {}).get(i + 1, [])) for i, t in enumerate(texts)]
    return ev.VerticalBook(sources, set(chapters), rows, columns, breaks)


class LayoutTest(unittest.TestCase):
    def test_vertical_forms_and_tate_chu_yoko(self):
        cells = ev.to_cells(ev.SourceLine('「12」、x', 1))
        self.assertEqual([c.glyph for c in cells], ['﹁', '12', '﹂', '︑', 'x '])
        self.assertEqual([(c.start, c.end) for c in cells], [(0, 1), (1, 3), (3, 4), (4, 5), (5, 6)])

    def test_emphasis_reading_is_dropped(self):
        cells = ev.to_cells(ev.SourceLine('傍《﹅》点', 1))
        self.assertEqual([c.glyph for c in cells], ['傍', '点'])

    def test_kinsoku_pushes_last_char(self):
        # 高さ3: 「あいう」の次の列が「。」で始まるので、「う」も次の列へ
        cols = ev.layout_columns([ev.SourceLine('あいう。え', 1)], 3)
        self.assertEqual([''.join(c.glyph for c in col.cells) for col in cols], ['あい', 'う︒え'])

    def test_indent(self):
        cols = ev.layout_columns([ev.SourceLine('あいうえ', 1, indent_first=1)], 3)
        self.assertEqual([[c.start for c in col.cells] for col in cols], [[-1, 0, 1], [2, 3]])

    def test_render_right_to_left_with_side_line(self):
        v = book(['あい', 'う'], links={2: [(0, 1, 'x.xhtml')]})
        rows = v.render()
        # 桁9: 右からの1列目は桁6〜7、2列目は桁3〜4、リンクの字の右の行間に「|」
        self.assertEqual(rows[0][0], '   う|あ')
        self.assertEqual(rows[1][0], '      い')


class MovementTest(unittest.TestCase):
    def test_chapter_starts_at_right_edge(self):
        v = book(['あ', 'い', 'う', 'え'], columns=9, chapters={3}, breaks=True)  # 1画面3列
        self.assertEqual([c.raw for c in v.cols], [1, 2, 2, 3, 4, 4])
        self.assertEqual([c.padding for c in v.cols], [False, False, True, False, False, True])
        v.command(3)
        v.command(3)  # 空の列を飛ばして次の章の頭へ、その列を右端に
        self.assertEqual((v.first, v.cursor), (3, 3))
        self.assertEqual(v.shown_range(v.first), (3, 6))

    def test_goto_ends_are_reversed_and_exchange(self):
        v = book(['あ'] * 10)
        v.command(11)  # ^[<は末尾へ
        self.assertEqual(v.cursor, 9)
        v.command(12)  # ^[>は先頭へ
        self.assertEqual(v.cursor, 0)
        v.command(14)  # ^X^Xで^[>の前の所へ
        self.assertEqual(v.cursor, 9)

    def test_select_adjacent_link(self):
        v = book(['あい', 'う'], links={1: [(0, 1, 'p'), (1, 2, 'q')]})
        v.set_view(0, 0)
        self.assertEqual(v.selected_href(), 'p')
        v.command(3)  # 同じ段落の次のリンク
        self.assertEqual((v.cursor, v.selected_href()), (0, 'q'))
        v.command(3)
        self.assertEqual(v.cursor, 1)


class SelectionTest(unittest.TestCase):
    def test_select_across_columns_and_record_offset(self):
        v = book(['あいうえお', 'か　き'], rows=3)
        v.set_view(0, 0)
        v.start_select()
        v.move_point(cells=1)  # 「い」
        v.toggle_anchor()
        v.move_point(cells=5)  # 「う」→次の列「え」「お」→次の段落の「か」「　」
        self.assertTrue(v.toggle_anchor())
        self.assertEqual(v.selection_ranges(), [(1, 1, 5), (2, 0, 2)])
        self.assertEqual(v.selection_text(), 'いうえお か　')  # 全角の空白は字のまま
        self.assertEqual(v.offset_in_chapter(1, 2, 1), 6)

    def test_place_in_chapter_finds_text(self):
        order, texts = [1, 2], {1: 'あいう', 2: 'えお'}
        self.assertEqual(ev.place_in_chapter(order, texts, 1, 3, 'えお'), (2, 0))
        self.assertEqual(ev.place_in_chapter(order, texts, 1, 4, 'えお'), (2, 0))  # 数え方がずれても字で探す


if __name__ == '__main__':
    unittest.main()
