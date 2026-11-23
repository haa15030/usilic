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

    def test_uopt_one_line(self):
        from usilic.ush.env import assignments
        self.assertEqual(assignments('STRAGE=/mnt/ntfs UUID=AAA\nSH=bash\nNEXT8=8GB-KDE\n'),
                         {'STRAGE': '/mnt/ntfs', 'UUID': 'AAA', 'SH': 'bash', 'NEXT8': '8GB-KDE'})
        self.assertEqual(assignments('STRAGE=""\n'), {'STRAGE': ''})

    def test_a_choose(self):
        system = fake(files={'/opt/U.opt': 'STRAGE=""\nSH=bash\n', '/mnt/debian/opt/BUILD_DEBIAN.img': 'x'})
        code, out = run(system, '-a', '--ans=2')
        self.assertEqual(code, 0)
        self.assertIn(' 2)ext4 /dev/sdd1 DDD (7 GB)', out)
        self.assertNotIn('vfat', out)                                   # -aはntfs・exfat・ext4だけ
        self.assertEqual(system.files['/opt/U.opt'], 'SH=bash\nSTRAGE=/mnt/ext4 UUID=DDD\n')
        self.assertEqual(system.files['/mnt/debian/opt/U.opt'], system.files['/opt/U.opt'])  # RAMDISK: DEBIAN区画へも
        self.assertIn('UUID=DDD /mnt/ext4 ext4 rw,noauto 0 1', system.files['/etc/fstab'])
        self.assertIn('UUID=ROOT /mnt/debian ext4 noauto 0 1', system.files['/etc/fstab'])
        self.assertEqual(system.mounted.get('/mnt/ext4'), '/dev/sdd1')
        self.assertNotIn(['cp', '-uv', '/mnt/debian/opt/BUILD_DEBIAN.img', '/mnt/ext4/opt'], system.calls)  # 7GBは10GB以下

    def test_a_copies_build_image(self):
        system = fake(files={'/mnt/debian/opt/BUILD_DEBIAN.img': 'x'})
        system.parts['/dev/sdd1']['free_kb'] = 70000000
        run(system, '-a', '--ans=2')
        self.assertIn(['cp', '-uv', '/mnt/debian/opt/BUILD_DEBIAN.img', '/mnt/ext4/opt'], system.calls)

    def test_a_zero(self):
        system = fake(files={'/opt/U.opt': 'STRAGE=/mnt/ext4 UUID=DDD\nSH=bash\n'})
        code, out = run(system, '-a', '--ans=0')
        self.assertEqual(system.files['/opt/U.opt'], 'SH=bash\nSTRAGE=""\n')
        self.assertIn('-a: STRAGE="" -> /opt/U.opt', out)

    def test_n(self):
        custom = ("menuentry 'RAM-6.12.57+4gb-base' {\n}\nmenuentry 'RAM-6.12.57+8gb-base' {\n}\n"
                  "menuentry 'macOS Sonoma' {\n}\n")
        system = fake(files={'/mnt/debian/etc/grub.d/40_custom': custom, '/proc/meminfo': 'MemTotal: 16000000 kB\n',
                             '/mnt/debian/opt/4GB-rescue.img': 'x', '/mnt/debian/opt/8GB-KDE.img': 'x',
                             '/mnt/debian/opt/16GB-big.img': 'x',
                             '/mnt/debian/etc/default/grub': 'GRUB_DEFAULT=saved\n'})
        code, out = run(system, '-n', '--ans=4', '--ans=n')
        lines = [l for l in out.splitlines() if l[:3].strip().rstrip(')').isdigit()]
        self.assertEqual(lines, [' 1)4GB-base', ' 2)4GB-rescue', ' 3)8GB-KDE', ' 4)8GB-base', ' 5)macOS Sonoma'])
        self.assertIn('NEXT8=8GB-base', system.files['/opt/U.opt'])     # 16GBはカーネルもメモリも無い
        self.assertIn(['grub-editenv', '/mnt/debian/boot/grub/grubenv', 'set', 'saved_entry=RAM-6.12.57+8gb-base'],
                      system.calls)
        self.assertNotIn(['reboot'], system.calls)

    def test_n_grub_entry_and_reboot(self):
        custom = "menuentry 'RAM-6.12.57+4gb-base' {\n}\nmenuentry 'Windows 11' {\n}\n"
        system = fake(files={'/mnt/debian/etc/grub.d/40_custom': custom, '/proc/meminfo': 'MemTotal: 8000000 kB\n'})
        code, out = run(system, '-n', '--ans=2', '--ans=y')
        self.assertIn(['grub-editenv', '/mnt/debian/boot/grub/grubenv', 'set', 'saved_entry=Windows 11'], system.calls)
        self.assertIn("GRUB_DEFAULT is not 'saved'", out)
        self.assertIn(['reboot'], system.calls)
        self.assertEqual(system.calls[system.calls.index(['reboot']) - 1], ['umount', '/mnt/debian'])

    def test_o_record_and_purge(self):
        base = 'acl\t\t\t\t\nbash\t\t\t\t\n'
        system = fake(files={'/opt/BASE.u': base},
                      packages=['acl', 'bash', 'grub-pc', 'linux-image-6.12', 'python3', 'sddm', 'kde-a', 'kde-b'],
                      held=['kde-b'])
        code, out = run(system, '-o', 'BASE.u', '-p')
        self.assertIn('dpkg --purge sddm kde-a kde-b', out)               # grub・カーネル・python3は外さない
        self.assertIn('-o BASE.u -p: not purged: kde-b', out)
        self.assertIn('x depends on kde-b.', out)
        self.assertEqual(system.packages, ['acl', 'bash', 'grub-pc', 'linux-image-6.12', 'python3', 'kde-b'])
        self.assertEqual(system.files['/opt/current.u'], 'acl\t\t\t\nbash\t\t\t\nkde-b\t\t\t\n')
        self.assertEqual(system.files['/mnt/debian/opt/current.u'], system.files['/opt/current.u'])

    def test_o_install_and_names(self):
        system = fake(files={'/opt/JP.u': 'acl\t\t\t\nmozc\t\t\t\n'}, packages=['acl'])
        run(system, '-o', 'JP.u', '-i')
        self.assertIn(['apt-get', 'install', '-y', 'mozc'], system.calls)
        run(system, '-o', '.')
        self.assertIn('/opt/8GB-base.u', system.files)
        run(system, '-o', 'foo')
        self.assertIn('/opt/foo.u', system.files)
        code, out = run(system, '-o', 'nothing.u', '-p')
        self.assertIn('/opt/nothing.u: not exist.', out)
        system.files['/opt/BASE.u'] = 'x\n'
        code, out = run(system, '-o', 'BASE.u')
        self.assertIn('/opt/BASE.u: already exist.', out)

    def test_c(self):
        system = fake()
        code, out = run(system, '-c', '--batch')
        self.assertIn(['arch-chroot', '/mnt/debian'], system.calls)
        self.assertIn(['umount', '-R', '/mnt/debian'], system.calls)
        code, out = run(fake(), '-c', '/mnt/usb', '--batch')
        self.assertIn('/mnt/usb: No such directory.', out)


if __name__ == '__main__':
    unittest.main()
