import io
import os
import tarfile
import tempfile
import unittest
import zipfile

from usilic import archive_mode as A
from usilic.configuration import Configuration
from usilic.content_view import ContentView


class ArchiveTest(unittest.TestCase):
    def test_tar_listing_and_extract(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'a.tgz')
            with tarfile.open(path, 'w:gz') as tar:
                for name, data in (('._a.txt', b'mac'), ('a.txt', b'hello\n'), ('n.txt', b'1\n' * 10)):
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    tar.addfile(info, io.BytesIO(data))
            mode = A.ArchiveMode(ContentView(), Configuration())
            self.assertTrue(mode.load_archive(path))
            # macOSの「._」は出さない、行は「サイズ  名前」(右詰め)
            self.assertEqual([l.text for l in mode.view.lines], [' 6  a.txt', '20  n.txt'])
            self.assertEqual(mode.byte_count, 20)
            self.assertEqual(mode.badge, '(tar)')
            self.assertEqual(mode.extract(mode.entries[0]), b'hello\n')

    def test_zip(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'z.zip')
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr('dir/', '')
                z.writestr('dir/x.txt', 'x')
            mode = A.ArchiveMode(ContentView(), Configuration())
            self.assertTrue(mode.load_archive(path))
            self.assertEqual([e.name for e in mode.entries], ['dir/', 'dir/x.txt'])
            self.assertTrue(mode.entries[0].is_directory)
            self.assertEqual(mode.extract(mode.entries[1]), b'x')

    def test_listing_parsers(self):
        lha = ('PERMISSION  UID  GID      SIZE  RATIO     STAMP           NAME\n'
               '---------- ----------- ------- ------ ------------ ----------\n'
               '[generic]                  1234  45.9% Jan  1  2020 dir/a.txt\n')
        self.assertEqual(A.parse_lha_listing(lha), [A.ArchiveEntry('dir/a.txt', 1234, False)])
        rpm = '-rw-r--r--    1 root    root       42 Jan  1  2020 /usr/share/x.txt\n'
        self.assertEqual(A.parse_rpm_listing(rpm), [A.ArchiveEntry('/usr/share/x.txt', 42, False)])
        ar = 'rw-r--r-- 0/0      4 Jan  1 09:00 1970 debian-binary\n'
        self.assertEqual(A.parse_ar_listing(ar), [A.ArchiveEntry('debian-binary', 4)])

    def test_extract_argv(self):
        argv = A.ArchiveMode.extract_argv('/a.tar', 'Tar', ['x*.txt'], '/out')
        self.assertEqual(argv, ['tar', 'xf', '/a.tar', '-C', '/out', 'x\\*.txt'])
        self.assertIsNone(A.ArchiveMode.extract_argv('/a.rpm', 'Rpm', [], '/out'))
        self.assertEqual(A.ArchiveMode.top_level_names(['./a/b', 'a/c', 'd']), ['a', 'd'])


if __name__ == '__main__':
    unittest.main()
