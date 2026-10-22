import os
import tempfile
import unittest

from usilic import bookmarks as B


class BookmarksTest(unittest.TestCase):
    def test_round_trip_and_order(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'bookmarks')
            B.set_bookmark('a', '/x/a.txt', 12, path)
            B.set_bookmark('dir', '/x/', 0, path)
            with open(path) as f:
                self.assertEqual(f.read(), 'dir\t0\t/x/\na\t12\t/x/a.txt\n')  # 新しい順(C++と同じ書式)
            self.assertEqual(B.take_bookmark('a', path), ('a', '/x/a.txt', 12))
            self.assertEqual([e[0] for e in B.read_bookmarks(path)], ['a', 'dir'])  # 使った物が先頭へ
            self.assertTrue(B.delete_bookmark('dir', path))
            self.assertFalse(B.delete_bookmark('dir', path))
            self.assertEqual(B.display_lines(B.read_bookmarks(path)), ['a     /x/a.txt:12'])


if __name__ == '__main__':
    unittest.main()
