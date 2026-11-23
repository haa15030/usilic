"""U.shのPython版(usilic/ush/)を、偽の区画・マウント(FakeSystem)で。実機には触れない。"""
import unittest

from usilic.ush import fs as ush_fs
from usilic.ush import main as ush_main
from usilic.ush.env import shell_value
from usilic.ush.system import FakeSystem

ush_fs.WAIT = 0

FSTAB = ('/dev/ram0 / ext4 errors=remount-ro 0 1\n'
         'UUID=ROOT /mnt/debian ext4 noauto 0 1\n')


def fake(**extra):
    parts = {
        '/dev/nvme0n1p2': {'TYPE': 'ext4', 'UUID': 'ROOT', 'free_kb': 50000000},          # DEBIAN区画(出さない)
        '/dev/sda3': {'TYPE': 'ntfs', 'UUID': 'AAA', 'free_kb': 120000000, 'mountable': 'ro'},  # 休止したWindows
        '/dev/sdb1': {'TYPE': 'exfat', 'UUID': 'BBB', 'mountable': False},
        '/dev/sdc1': {'TYPE': 'vfat', 'UUID': 'CCC', 'free_kb': 3000000},
        '/dev/sdd1': {'TYPE': 'ext4', 'UUID': 'DDD', 'free_kb': 7000000},
    }
    files = {'/etc/fstab': FSTAB, '/etc/issue': 'Debian GNU/Linux 13 \\n \\l\n\n',
             '/opt/U.sh': 'readonly DEBIAN=debian \t# 注釈\n', '/opt/U.opt': 'STRAGE=""\nSH=bash\n',
             'fstab:/mnt/debian': '/dev/nvme0n1p2'}
    files.update(extra.pop('files', {}))
    mounted = {'/media/usilic/CCC': '/dev/sdc1'}
    mounted.update(extra.pop('mounted', {}))
    return FakeSystem(parts=parts, mounted=mounted, files=files, dirs={'/mnt'}, **extra)


def run(system, *argv):
    lines = []
    code = ush_main.run(list(argv), system=system, uname='6.12.57+8gb', out=lines.append)
    return code, '\n'.join(lines)


class UshTest(unittest.TestCase):
    def test_shell_value(self):
        self.assertEqual(shell_value('readonly DEBIAN=debian \t# x\n', 'DEBIAN'), 'debian')
        self.assertEqual(shell_value('STRAGE="/mnt/ntfs"\nUUID=AB-12\n', 'STRAGE'), '/mnt/ntfs')
        self.assertEqual(shell_value('STRAGE=""\n', 'STRAGE'), '')
        self.assertIsNone(shell_value('X=1\n', 'Y'))

    def test_not_ported(self):
        code, out = run(fake(), '-s', 'foo')
        self.assertEqual(code, 2)
        self.assertIn('not in the Python version', out)

    def test_needs_root(self):
        code, out = run(fake(euid=1000), '-f', '--list')
        self.assertEqual(code, 1)

    def test_f_list(self):
        system = fake()
        code, out = run(system, '-f', '--list')
        self.assertEqual(code, 0)
        self.assertIn(' 1)ntfs /dev/sda3 AAA (120 GB)', out)      # 読み書きでは駄目、読むだけで仮にマウントして空きを見た
        self.assertIn("  -)exfat /dev/sdb1 BBB (can't mount)", out)
        self.assertIn(' 2)ext4 /dev/sdd1 DDD (7 GB)', out)          # DEBIAN区画(ROOT)は出ない
        self.assertIn(' 3)vfat /dev/sdc1 CCC (3 GB) [/media/usilic/CCC]', out)
        self.assertIn('Input No (e.g. \'2\' or \'2 5\'):', out)
        self.assertEqual(sorted(system.mounted), ['/media/usilic/CCC'])  # 仮のマウントは外した、/mnt/番号は作らない
        self.assertFalse(any(c[:2] == ['ln', '-s'] for c in system.calls))

    def test_f_mount_and_link(self):
        system = fake()
        code, out = run(system, '-f', '--ans=1 3')
        self.assertEqual(system.mounted.get('/mnt/1'), '/dev/sda3')     # 読むだけで
        self.assertIn(['mount', '-o', 'ro', 'UUID=AAA', '/mnt/1'], system.calls)
        self.assertEqual(system.links.get('/mnt/3'), '/media/usilic/CCC')  # もうマウントされているのでリンク
        self.assertIn('ln -s /media/usilic/CCC /mnt/3', out)

    def test_f_invalid_number(self):
        system = fake()
        code, out = run(system, '-f', '--ans=9')
        self.assertIn('9: Invalid No. (1-3)', out)
        self.assertNotIn('/mnt/9', system.mounted)

    def test_f_unmount_all(self):
        system = fake(mounted={'/mnt/1': '/dev/sda3'}, links={'/mnt/3': '/media/usilic/CCC'})
        system.dirs.add('/mnt/1')
        code, out = run(system, '-f', '-', '--batch')
        self.assertNotIn('/mnt/1', system.mounted)
        self.assertNotIn('/mnt/3', system.links)
        self.assertIn(['rmdir', '/mnt/1'], system.calls)
        self.assertEqual(system.mounted.get('/media/usilic/CCC'), '/dev/sdc1')  # /mnt/番号の行き先は外さない

    def test_v(self):
        system = fake(links={'/mnt/3': '/media/usilic/CCC'})
        code, out = run(system, '-v', '--batch')
        self.assertEqual(code, 0)
        self.assertIn('DEBIAN(/mnt/debian/opt):', out)                  # RAMDISK: DEBIAN区画をマウントして
        self.assertEqual(system.mounted.get('/mnt/debian'), '/dev/nvme0n1p2')
        self.assertIn('/mnt/3: /dev/sdc1 vfat CCC (rw) -> /media/usilic/CCC', out)

    def test_v_strage(self):
        system = fake(files={'/opt/U.opt': 'STRAGE=/mnt/ext4\nUUID=DDD\n'})
        code, out = run(system, '-v', '--batch')
        self.assertIn('STRAGE(/mnt/ext4/opt):', out)
        self.assertEqual(system.mounted.get('/mnt/ext4'), '/dev/sdd1')

    def test_mount_failure_logs(self):
        system = fake(files={'fstab:/mnt/debian': '/dev/missing'})
        code, out = run(system, '-v', '--batch')
        self.assertEqual(code, 1)
        self.assertIn('mount /mnt/debian: failed.', out)
        self.assertTrue(any(c[0] == 'append' and c[1].startswith('/opt/U.log_') for c in system.calls))


if __name__ == '__main__':
    unittest.main()
