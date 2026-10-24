import unittest

from usilic import macro as M


class MacroTest(unittest.TestCase):
    def test_parse(self):
        macros, errors = M.parse_macro_lines(['\tM-6  l\tK-86 K-53 "^usilic-1.5b:" K-28\t# 注釈', '\t--',
                                              '\tM-7  x\tK-1 bad'])
        self.assertEqual(len(macros), 1)
        self.assertEqual((macros[0].number, macros[0].key_spec), (6, 'l'))
        self.assertEqual(macros[0].items, [('k', 86), ('k', 53), ('s', '^usilic-1.5b:'), ('k', 28)])
        self.assertEqual(len(errors), 1)

    def test_items_from_bytes(self):
        # ^X^F(K-62)の後に打った字は"文字列"、^M(K-28)
        self.assertEqual(M.items_from_bytes([0x18, 0x06, ord('a'), ord('b'), 0x0D]), 'K-62 "ab" K-28')
        self.assertEqual(M.items_from_bytes([ord('j'), 0x0E]), '"j" K-3')

    def test_marker_and_lines(self):
        text = 'BUFFER\n\t64\nMACRO\n\tM-1  ;\tK-20 K-4 K-28\t\t# ^G^P^M\t前\n\t--\nURL\n'
        self.assertEqual(M.next_macro_number(text), 2)
        marked = M.replace_macro_marker(text, ['\t--\tK-3 K-3'])
        lines = M.macro_lines(marked)
        self.assertEqual([(m.number, m.key, m.items, m.comment) for m in lines],
                         [(1, ';', 'K-20 K-4 K-28', '^G^P^M\t前'), (0, '', 'K-3 K-3', '')])
        replaced = M.replace_macro_line(marked, 1, '\tM-1  q\tK-3')
        self.assertIn('\tM-1  q\tK-3\n', replaced)

    def test_notation(self):
        self.assertEqual(M.notation('K-20 K-4 K-28'), '^G^P^M')
        self.assertEqual(M.notation('K-86 "a b" K-28'), '^J^Osa\u2423b^M')
        self.assertEqual(M.items_from_notation('^G^P^M'), 'K-20 K-4 K-28')
        self.assertEqual(M.items_from_notation('j^N'), '"j" K-3')
        self.assertIsNone(M.items_from_notation('^1'))


if __name__ == '__main__':
    unittest.main()
