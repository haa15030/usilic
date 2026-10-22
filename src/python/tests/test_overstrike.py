import unittest

from usilic import overstrike as O


class OverstrikeTest(unittest.TestCase):
    def test_bold_and_underline(self):
        text, runs = O.decode('N\bNA\bAM\bME\bE  _\bl_\bs')
        self.assertEqual(text, 'NAME  ls')
        self.assertEqual(runs, [(0, 4, 'bold'), (6, 8, 'underline')])

    def test_wide_character(self):
        text, runs = O.decode('説\b説明\b明')
        self.assertEqual((text, runs), ('説明', [(0, 2, 'bold')]))

    def test_wrap_keeps_attributes(self):
        lines = O.wrap_styled('abcdef', [(2, 5, 'bold')], 3, 1)
        self.assertEqual([l.text for l in lines], ['abc', 'def'])
        self.assertEqual([l.attributes for l in lines], [[(2, 3, 'bold')], [(0, 2, 'bold')]])


if __name__ == '__main__':
    unittest.main()
