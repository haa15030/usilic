import os
import tempfile
import unittest

from usilic import mouse as m
from usilic import epub_vertical as ev
from usilic.content_view import ContentView, Line
from usilic.copy_mode import CopyMode


class SgrTest(unittest.TestCase):
    def test_parse(self):
        e = m.parse_sgr('0;45;12M')
        self.assertEqual((e.row, e.column, e.button, e.motion, e.pressed, e.wheel), (11, 44, 0, False, True, 0))
        e = m.parse_sgr('35;3;4M')   # ボタン無しの動き(ホバー)
        self.assertEqual((e.button, e.motion), (3, True))
        e = m.parse_sgr('65;1;1M')   # ホイールの下
        self.assertEqual((e.button, e.wheel), (-1, 1))
        self.assertEqual(m.parse_sgr('64;1;1M').wheel, -1)
        self.assertEqual(m.parse_sgr('66;1;1M').wheel, 0)   # 横のホイールは向きを持たせない
        self.assertFalse(m.parse_sgr('2;5;5m').pressed)
        self.assertIsNone(m.parse_sgr('0;0;5M'))
        self.assertIsNone(m.parse_sgr('x'))


class SpeedTest(unittest.TestCase):
    def test_levels(self):
        self.assertEqual([m.level_for_release_distance(n) for n in (0, 1, 2, 4, 30, -40)], [0, 1, 2, 3, 10, 10])
        s = m.Speeds()
        self.assertEqual((s.max_level(), s.drag_offset()), (10, 0))
        self.assertAlmostEqual(s.seconds_per_row(1), 0.72)   # 90000usec×8ドット
        s = m.Speeds(arrow=[200000, 100000, 25000])
        self.assertEqual((s.max_level(), s.drag_offset()), (3, 2))
        self.assertAlmostEqual(s.seconds_per_row(3), 0.2)

    def test_step_by_key(self):
        self.assertEqual(m.step_level_by_key(2, 1, 1, 10), (3, 1))    # 同じ向き: 速く
        self.assertEqual(m.step_level_by_key(2, 1, -1, 10), (1, 1))   # 逆: 遅く
        self.assertEqual(m.step_level_by_key(1, 1, -1, 10), (1, -1))  # 段1で逆: 向きを変える
        self.assertEqual(m.step_level_by_key(10, 1, 1, 10), (10, 1))

    def test_dropped_paths(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, 'a b'), os.path.join(d, 'c')
            open(a, 'w').close()
            open(b, 'w').close()
            self.assertEqual(m.dropped_paths("'%s' %s" % (a, b)), [a, b])
            self.assertEqual(m.dropped_paths(a.replace(' ', '\\ ')), [a])
            self.assertEqual(m.dropped_paths(a + ' /nonexistent'), [])
            self.assertEqual(m.dropped_paths('hello'), [])


class CopyMouseTest(unittest.TestCase):
    def test_begin_and_extend(self):
        v = ContentView()
        v.set_height(5)
        v.set_lines([Line('abcdef', 1), Line('ghijkl', 2)])
        c = CopyMode(v)
        c.begin_or_extend_from_mouse(0, 1)
        c.place_cursor_at(1, 3)
        self.assertEqual(c.joined_text(), 'bcdef\nghi')
        c.begin_or_extend_from_mouse(0, 4)   # 広がりのある範囲は残して、ここから新しく
        c.place_cursor_at(0, 6)
        self.assertEqual(c.joined_text(), 'bcdef\nghi ef')


class VerticalColumnTest(unittest.TestCase):
    def test_column_at(self):
        # 桁9: 右からの1列目は桁6〜7(桁8は行間)、2列目は桁3〜4
        self.assertEqual(ev.column_at(7, 9), 0)
        self.assertEqual(ev.column_at(8, 9), -1)
        self.assertEqual(ev.column_at(4, 9), 1)
        self.assertEqual(ev.column_at(5, 9), -1)

    def test_cell_at(self):
        v = ev.VerticalBook([ev.SourceLine('あいう', 1), ev.SourceLine('え', 2)], set(), 3, 9, False)
        self.assertEqual(v.cell_at(1, 7), (0, 1))
        self.assertEqual(v.cell_at(9, 0), (1, 2))   # 行・列の外は近い所へ


if __name__ == '__main__':
    unittest.main()
