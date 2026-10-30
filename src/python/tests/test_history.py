import os
import tempfile
import unittest

from usilic import history as H


def sample():
    h = H.History()
    h._set_position_impl(H.DIRECTORY, '/a b/', H.CursorPosition(3, 9, 0, -1))
    h._push_recent_impl(H.RECENT_FILE, '/a b/ã\u0081\u0082.txt')  # UTF-8の「あ」をlatin-1で
    h._remember_name_impl(H.CUT_PATTERN, 'x y')
    h.trailer_tokens = ['0', '1', '310', '14', 'future-token']
    return h


class HistoryTest(unittest.TestCase):
    def test_round_trip(self):
        text = sample().serialize('usilic', 'T')
        h = H.History()
        self.assertTrue(h.load_history_text(text))
        self.assertTrue(h.complete)
        self.assertEqual(h.serialize('usilic', 'T'), text)
        self.assertEqual(h.trailer_tokens[-1], 'future-token')  # 解釈しない欄もそのまま
        self.assertEqual(h.position_for(H.DIRECTORY, '/a b/').row, 3)

    def test_split_only_on_c_whitespace(self):
        # latin-1の\x85・\xa0(UTF-8の字のバイト)では切らない
        self.assertEqual(H.split_whitespace('a\x85b \xa0c'), ['a\x85b', '\xa0c'])

    def test_field_encoding(self):
        for s in ['', 'a b', 'x\ty', '\x7f']:
            self.assertEqual(H.decode_field(H.encode_field(s)), s)

    def test_file_push_also_records_directory(self):
        h = H.History()
        h.push_recent(H.RECENT_FILE, '/x/y/z.txt')
        self.assertEqual(h.recent_entries(H.RECENT_DIRECTORY), ['/x/y/'])
        self.assertEqual(h.recent_entries(H.USILIC), ['/x/y/z.txt'])

    def test_japanese_names_round_trip(self):
        # 日本語のパス(UTF-8)はバイトのままファイルへ、読み返すと同じstr(第2段では保存で落ちていた)
        with tempfile.TemporaryDirectory() as d:
            session = H.load_from_disk(d)
            session.push_recent(H.RECENT_DIRECTORY, '/tmp/日本語/')
            session.set_position(H.DIRECTORY, '/tmp/日本語/', H.CursorPosition(2, 5, 0, -1))
            H.save_to_disk(session, d)
            with open(os.path.join(d, 'history'), 'rb') as f:
                self.assertIn('/tmp/日本語/'.encode('utf-8'), f.read())
            disk = H.load_from_disk(d)
            self.assertEqual(disk.recent_entries(H.RECENT_DIRECTORY), ['/tmp/日本語/'])
            self.assertEqual(disk.position_for(H.DIRECTORY, '/tmp/日本語/').row, 2)

    def test_saving_twice_does_not_duplicate(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, 'history'), 'wb') as f:
                f.write(H.History().serialize('usilic', 'T').encode('latin-1'))
            session = H.load_from_disk(d)
            session.push_recent(H.RECENT_FILE, '/x/A')
            H.save_to_disk(session, d)
            session.push_recent(H.RECENT_FILE, '/x/B')
            session.push_recent(H.RECENT_FILE, '/x/C')
            session.push_recent(H.RECENT_FILE, '/x/A')
            H.save_to_disk(session, d)
            disk = H.load_from_disk(d)
            self.assertEqual(disk.recent_entries(H.RECENT_FILE), ['/x/A', '/x/B', '/x/C', '/x/A'])
            self.assertTrue(os.path.exists(os.path.join(d, 'history.old')))
            self.assertFalse(os.path.exists(os.path.join(d, 'history.lock')))

    def test_back_and_forth_swaps(self):
        """1つ前との行き来(^J^J)は積まずに入れ替える(C++の項目1122)。"""
        h = H.History()
        for name in ('/x/A', '/x/B', '/x/A', '/x/B', '/x/A'):
            h.push_recent(H.RECENT_FILE, name)
        self.assertEqual(h.recent_entries(H.RECENT_FILE), ['/x/B', '/x/A'])


if __name__ == '__main__':
    unittest.main()
