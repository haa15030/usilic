import unittest

from usilic import mode_line


class ModeLineTest(unittest.TestCase):
    def test_format(self):
        self.assertEqual(mode_line.format(1, 500), '001/500 TOP')
        self.assertEqual(mode_line.format(250, 500), '250/500 50%')
        self.assertEqual(mode_line.format(5, 500), '005/500  1%')
        self.assertEqual(mode_line.format(500, 500), '500/500 END')
        self.assertEqual(mode_line.format(1, 1), '1/1 END')

    def test_format_fixed_width(self):
        self.assertEqual(mode_line.format_fixed_width(1, 40), '00001/00040 TOP')
        self.assertEqual(mode_line.format_fixed_width(300, 300), '00300/00300 END')


if __name__ == '__main__':
    unittest.main()
