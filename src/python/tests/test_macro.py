import os
import tempfile
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

    def test_save_macro_before_marker(self):
        with tempfile.TemporaryDirectory() as d:
            rc = os.path.join(d, 'usilicrc')
            with open(rc, 'w') as f:
                f.write('BUFFER\n\t64\nMACRO\n\tM-1  ;\tK-20 K-4 K-28\n\t--\nURL\n')
            self.assertEqual(M.save_macro(rc, 'z', 'K-3 K-3'), 2)
            with open(rc) as f:
                lines = f.read().split('\n')
            self.assertEqual(lines[4:6], ['\tM-2  z\tK-3 K-3', '\t--'])
            self.assertTrue(any(n.endswith('.bak') for n in os.listdir(d)))


if __name__ == '__main__':
    unittest.main()
