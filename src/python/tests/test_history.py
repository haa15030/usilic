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

    def test_saving_twice_does_not_duplicate(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, 'history'), 'wb') as f:
                f.write(H.History().serialize('usilic', 'T').encode('latin-1'))
            session = H.load_from_disk(d)
            session.push_recent(H.RECENT_FILE, '/x/A')
            H.save_to_disk(session, d)
            session.push_recent(H.RECENT_FILE, '/x/B')
            session.push_recent(H.RECENT_FILE, '/x/A')
            H.save_to_disk(session, d)
            disk = H.load_from_disk(d)
            self.assertEqual(disk.recent_entries(H.RECENT_FILE), ['/x/A', '/x/B', '/x/A'])
            self.assertTrue(os.path.exists(os.path.join(d, 'history.old')))
            self.assertFalse(os.path.exists(os.path.join(d, 'history.lock')))


if __name__ == '__main__':
    unittest.main()
