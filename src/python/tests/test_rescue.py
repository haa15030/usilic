import unittest

from usilic import rescue

OPTIONS = """/dev/shm : 1.2G
img-size : 3456
. U [-acdfkmnosv]
  -a(dd_strage)
  -c(hange_root) [/mnt/usb]
  -d(esktop) : on ??GB-* (8GB~, STRAGE; on a desktop host: from a text console)
  -f(ile_system) [-] : /mnt/No.
  -n(ext_host)
  -s(ave_host) name [.] or . (without STRAGE: 4GB-rescue only)
  -u(pdate_base) : TORAM (dracut)
  -v(ervose)
"""

NUMBERS = """ 0)TYPE DEVICENAME UUID (free) [mounted]
 1)ntfs /dev/sda3 1234-ABCD (120 GB)
  -)exfat /dev/sdb1 5678-EF01 (can't mount)
10)ext4 /dev/nvme0n1p5 aaaa-bbbb (50 GB) [/mnt/ext4]
  )<EXIT>
Input No:
"""


class RescueTest(unittest.TestCase):
    def test_parse_options(self):
        options = rescue.parse_options(OPTIONS)
        self.assertEqual([o for o, _ in options], ['-a', '-c', '-d', '-f', '-n', '-s', '-u', '-v'])
        self.assertEqual(options[0][1], '-a(dd_strage)')

    def test_takes_argument(self):
        by = dict((o, d) for o, d in rescue.parse_options(OPTIONS))
        self.assertTrue(rescue.takes_argument(by['-s']))
        self.assertTrue(rescue.takes_argument(by['-c']))
        self.assertFalse(rescue.takes_argument(by['-a']))
        self.assertFalse(rescue.takes_argument(by['-d']))  # 「 : 」の後は説明
        self.assertFalse(rescue.takes_argument(by['-u']))
        self.assertFalse(rescue.takes_argument(by['-v']))

    def test_parse_numbers(self):
        items = rescue.parse_numbers(NUMBERS)
        self.assertEqual([n for n, _ in items], ['0', '1', '10'])  # 「-)」「 )<EXIT>」は選べない
        self.assertEqual(rescue.number_of(items[2][1]), '10')
        self.assertEqual(rescue.number_of(rescue.UNMOUNT_ALL), '-')

    def test_answer_args(self):
        self.assertEqual(rescue.answer_args(['2', 'n']), ['--ans=2', '--ans=n'])


if __name__ == '__main__':
    unittest.main()
