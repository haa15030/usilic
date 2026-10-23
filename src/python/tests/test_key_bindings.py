import unittest

from usilic import key_bindings as K


class KeyBindingsTest(unittest.TestCase):
    def test_parse_spec(self):
        prefixes = [[0x18], [0x0A], [0x0F]]
        self.assertEqual(K.parse_spec('F-1 C-s', prefixes), ([0x0A, 0x13], None))
        self.assertEqual(K.parse_spec('ESC <', prefixes), ([0x1B, ord('<')], None))
        self.assertEqual(K.parse_spec('C-SPC', prefixes), ([K.CTRL_SPACE], None))
        self.assertEqual(K.parse_spec('F-1 N', prefixes), ([0x0A, K.DIGIT], None))
        self.assertIsNone(K.parse_spec('N x', prefixes)[0])  # Nは最後だけ
        self.assertIsNone(K.parse_spec('F-9 x', prefixes)[0])

    def test_default_table_has_no_conflicts(self):
        b = K.KeyBindings()
        self.assertEqual(b.errors, [])
        self.assertEqual(b.sequence_for(62), [0x18, 0x06])          # ^X^F
        self.assertEqual(b.sequence_for(84), [0x0A, 0x0F, ord('u')])  # ^J^Ou(F-4はF-1を使う)

    def test_match(self):
        b = K.KeyBindings()
        self.assertEqual(b.match([0x18]), ('more', None))
        self.assertEqual(b.match([0x18, 0x06]), ('done', 62))
        self.assertEqual(b.match([0x1B, ord('7')]), ('done', 13))  # ^[N
        self.assertEqual(b.match([0x0A, ord('2')]), ('done', 27))  # ^JN
        self.assertEqual(b.match([0x18, ord('Z')]), ('none', None))

    def test_key_section_overrides(self):
        b = K.KeyBindings()
        b.load_key_section(['    F-0   C-t     # ^Xの代わりに^T', '    K-1   C-y', '    K-2'])
        pending = [[], None]
        self.assertEqual(b.feed(0x14, 'all', pending) + b.feed(0x06, 'all', pending), [0x18, 0x06])  # ^T^Fは既定の^X^Fへ
        self.assertEqual(b.feed(0x19, 'all', [[], None]), [ord('j')])   # K-1は^Yで、既定のjへ
        self.assertEqual(b.feed(ord('k'), 'all', [[], None]), [])         # 空は割り当て無し(kは捨てる)
        self.assertEqual(b.feed(ord('J'), 'prompt', [[], None]), [ord('J')])  # プロンプトでは打った字のまま

    def test_prefix_label(self):
        self.assertEqual(K.prefix_label([0x18]), 'C-x-')
        self.assertEqual(K.prefix_label([0x1B]), 'ESC-')
        self.assertEqual(K.prefix_label([0x0A, 0x0F]), 'C-j C-o-')


if __name__ == '__main__':
    unittest.main()
