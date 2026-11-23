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


def system_tmp(system):
    return [c[2] for c in system.calls if c[:2] == ['mkdir', '-p'] and c[2].startswith('/tmp/ush.')][0]


def run(system, *argv):
    lines = []
    code = ush_main.run(list(argv), system=system, uname='6.12.57+8gb', out=lines.append, prepare=False)
    return code, '\n'.join(lines)


class UshTest(unittest.TestCase):
    def test_shell_value(self):
        self.assertEqual(shell_value('readonly DEBIAN=debian \t# x\n', 'DEBIAN'), 'debian')
        self.assertEqual(shell_value('STRAGE="/mnt/ntfs"\nUUID=AB-12\n', 'STRAGE'), '/mnt/ntfs')
        self.assertEqual(shell_value('STRAGE=""\n', 'STRAGE'), '')
        self.assertIsNone(shell_value('X=1\n', 'Y'))

    def test_not_ported(self):
        code, out = run(fake(), '-z', 'foo')
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

    def save_fake(self, **kw):
        system = fake(files={'/opt/U.opt': 'STRAGE=/mnt/ext4 UUID=DDD\n',
                             '/etc/fstab': FSTAB + 'UUID=DDD /mnt/ext4 ext4 rw,noauto 0 1\n',
                             '/proc/meminfo': 'MemTotal: 16000000 kB\n'}, hostname='8GB-KDE', **kw)
        system.parts['/dev/sdd1']['free_kb'] = 70000000
        system.parts['/dev/nvme0n1p2']['free_kb'] = 20000000
        system.results['df'] = (0, 'Filesystem 1K-blocks Used Available Use% Mounted on\n'
                                   '/dev/ram0 8000000 3000000 5000000 40% /\n'
                                   'tmpfs 16000000 1000 16000000 1% /dev/shm\n'
                                   'tmpfs 16000000 500000 15500000 4% /home\n')
        return system

    def test_s_new_and_exist(self):
        system = self.save_fake()
        code, out = run(system, '-s', 'GNOME')
        self.assertIn('HOST:8GB-GNOME', out)
        self.assertIn('free: 70000000KB (/mnt/ext4/opt)', out)
        self.assertTrue(any(c[:2] == ['sh', '-c'] and c[2].startswith("tar cf - --exclude='usr/lib/modules/*'")
                            for c in system.calls))
        self.assertIn(['mv', system_tmp(system) + '/shm0/IMG0.img', '/mnt/ext4/opt/8GB-GNOME.img'], system.calls)
        self.assertIn('/dev/null', ' '.join(' '.join(c) for c in system.calls))      # RAMDISKの骨組み(mknod dev/null)
        fstab = [v for k, v in system.files.items() if k.endswith('/debian/etc/fstab')][0]
        self.assertTrue(fstab.startswith('/dev/ram / ext4 errors=remount-ro 0 1\n'))
        self.assertIn('UUID=ROOT /mnt/debian ext4 rw,noauto', fstab)
        system.files['/mnt/ext4/opt/8GB-GNOME.img'] = 'x'
        code, out = run(system, '-s', 'GNOME')
        self.assertIn("8GB-GNOME: already exist. To overwrite, type '. U -s GNOME .'", out)

    def test_s_base_and_strage(self):
        code, out = run(fake(), '-s', '.')
        self.assertIn("Can't save to base-host.", out)
        system = fake(hostname='8GB-KDE')
        code, out = run(system, '-s', 'KDE', '.')
        self.assertIn("Strage-area needed ('. U -a'). Without it, only 8GB-rescue can be saved.", out)

    def test_s_too_big(self):
        system = self.save_fake()
        system.parts['/dev/sdd1']['free_kb'] = 1000
        code, out = run(system, '-s', 'KDE', '.')
        self.assertIn('-s: 8GB-KDE: img 3850000KB > free 1000KB (/mnt/ext4/opt).', out)  # (3000000+500000)*1.1

    def test_d(self):
        system = self.save_fake(packages=['acl'])
        system.hostname = '8GB-base'
        system.results['tasksel --list-tasks'] = (0, 'u kde-desktop\tKDE Plasma\nu gnome-desktop\tGNOME\n'
                                                     'u web-server\tweb server\n')
        code, out = run(system, '-d', '--ans=1', '--ans=Y', '--ans=n')
        self.assertIn(' 1)KDE-Plasma (kde-desktop)', out)
        self.assertIn(' 2)GNOME (gnome-desktop)', out)
        self.assertNotIn('web', out.split('Input No')[0])
        self.assertTrue(any('task-kde-desktop' in c for c in system.calls))
        self.assertTrue(any('task-japanese-desktop' in c for c in system.calls) or 'install' in out)
        self.assertIn('/opt/JP.u', ' '.join(' '.join(c) for c in system.calls))
        self.assertTrue(any(c[0] == 'mv' and c[-1] == '/mnt/ext4/opt/8GB-KDE-Plasma.img' for c in system.calls))
        self.assertNotIn(['systemctl', 'isolate', 'graphical.target'], system.calls)
        self.assertIn("-d: '. U -n' to boot.", out)

    def test_d_from_desktop_host(self):
        system = self.save_fake(packages=['acl', 'sddm'])
        system.files['/opt/BASE.u'] = 'acl\t\t\t\n'
        system.results['tasksel --list-tasks'] = (0, 'u gnome-desktop\tGNOME\n')
        code, out = run(system, '-d', '--ans=1', '--ans=Y', '--ans=Y')
        self.assertIn('(8GB-KDE -> BASE.u: the desktop stops now)', out)
        self.assertIn(['systemctl', 'isolate', 'multi-user.target'], system.calls)
        self.assertIn('dpkg --purge sddm', out)
        self.assertIn(['hostnamectl', 'set-hostname', '8GB-GNOME'], system.calls)
        self.assertIn('NEXT8=8GB-GNOME', system.files['/opt/U.opt'])
        self.assertEqual(system.calls[-1][:3] if system.calls[-1][0] == 'systemctl' else
                         [c for c in system.calls if c[:2] == ['systemctl', 'isolate']][-1],
                         ['systemctl', 'isolate', 'graphical.target'])

    def test_e(self):
        system = FakeSystem(parts={'/dev/nvme0n1p2': {'TYPE': 'ext4', 'UUID': 'ROOT'},
                                   '/dev/sdd1': {'TYPE': 'ext4', 'UUID': 'DDD'}},
                            mounted={'/': '/dev/nvme0n1p2', '/mnt/ext4': '/dev/sdd1'},
                            files={'/etc/fstab': 'UUID=ROOT / ext4 errors=remount-ro 0 1\n', '/opt/BASE.u': 'a\n',
                                   '/opt/DEBIAN.u': 'acl\t\t\t\n', '/opt/U.sh': '', '/opt/U.opt': '',
                                   '/opt/grub.ORG': 'x', '/opt/RAM-6.12-base.gz': 'x', '/opt/linux-image-x.deb': 'x'},
                            packages=['acl', 'gparted'], euid=0)
        system.results['dpkg -l'] = (0, 'ii  linux-image-6.12.57+4gb 1 amd64 x\nii  linux-image-6.12.57-amd64 1 amd64 x\n')
        lines = []
        code = ush_main.run(['-e', '--ans=Y'], system=system, uname='6.12.57-amd64', out=lines.append, prepare=False)
        out = '\n'.join(lines)
        self.assertIn(['dpkg', '--purge', 'linux-image-6.12.57+4gb'], system.calls)
        self.assertIn('dpkg --purge gparted', out)
        self.assertNotIn('/mnt/ext4', system.mounted)
        self.assertIn(['rm', '-rf', '/opt/RAM-6.12-base.gz'], system.calls)
        self.assertNotIn(['rm', '-rf', '/opt/U.sh'], system.calls)
        self.assertNotIn(['rm', '-rf', '/opt/DEBIAN.u'], system.calls)
        self.assertIn(['update-grub'], system.calls)

    def test_k(self):
        system = self.save_fake()
        system.dirs.add('/usr/src/linux-source-')
        code, out = run(system, '-k', '16')
        self.assertIn(['mount', '-o', 'loop', '/mnt/ext4/opt/BUILD_DEBIAN.img', '/opt/build'], system.calls)
        self.assertIn(['truncate', '-s', '7000000000', '/mnt/ext4/opt/BUILD_DEBIAN.img'], system.calls)  # 無いので作る
        self.assertIn(['make', 'menuconfig'], system.calls)
        self.assertIn(['make', '-j9', 'O=../build/', 'LOCALVERSION=+16gb'], system.calls)
        self.assertIn(['make', 'bindeb-pkg', 'O=../build/', 'LOCALVERSION=+16gb'], system.calls)
        code, out = run(self.save_fake(), '-k', 'x')
        self.assertIn("Please type '. U -k (.)(-)(??)'", out)
        code, out = run(fake(), '-k', '16')
        self.assertIn("-k: Strage-area needed ('. U -a').", out)

    def usb_fake(self, usb_bytes):
        system = fake(files={'/mnt/usb/etc/fstab': 'UUID=ROOT / ext4 errors=remount-ro 0 1\n'
                                                   'UUID=OLDEFI /boot/efi vfat umask=0077 0 1\n/swapfile none swap sw 0 0\n',
                             '/mnt/usb/etc/grub.d/40_custom': "menuentry 'RAM-6.12.57+4gb-base' {\n"
                                                              "\tsearch --fs-uuid --set=root ROOT\n}\n",
                             '/opt/U.opt': 'STRAGE=""\nSH=dash\n'})
        system.parts['/dev/sde'] = {'TYPE': '', 'UUID': ''}
        system.parts['/dev/sde3'] = {'TYPE': 'ext4', 'UUID': 'NEWROOT'}
        system.parts['/dev/sde1'] = {'TYPE': 'vfat', 'UUID': 'EFI1'}
        system.dirs.update({'/mnt/usb/dev', '/mnt/usb/proc', '/mnt/usb/sys'})
        system.results.update({
            'lsblk -no pkname /dev/nvme0n1p2': (0, 'nvme0n1\n'),
            'blockdev --getsize64 /dev/nvme0n1p2': (0, '%d\n' % (10 * 1024 ** 3)),
            'blockdev --getsize64 /dev/sde': (0, '%d\n' % usb_bytes),
            'blockdev --getsize64 /dev/sde3': (0, '%d\n' % (14 * 1024 ** 3))})
        return system

    def test_m(self):
        system = self.usb_fake(32 * 1000 ** 3)
        code, out = run(system, '-m', 'sde', '--ans=Y')
        self.assertIn('/dev/sde4:STRAGE(ext4)', out)
        sgdisk = [c for c in system.calls if c[0] == 'sgdisk' and c[1] != '-Z'][0]
        self.assertIn('3:0:+%dK' % (14 * 1024 * 1024), sgdisk)
        self.assertIn('4:0:0', sgdisk)
        dd = [c for c in system.calls if c[0] == 'dd']
        self.assertEqual(dd[1][1:], ['if=/dev/nvme0n1p2', 'of=/dev/sde3', 'bs=4M', 'skip=1', 'seek=1', 'conv=fsync',
                                     'status=progress'])
        self.assertEqual(dd[2][-2:], ['count=1', 'conv=fsync'])      # 先頭の4MiBは最後に
        fstab = system.files['/mnt/usb/etc/fstab']
        self.assertIn('UUID=NEWROOT / ext4', fstab)
        self.assertIn('UUID=EFI1 /boot/efi vfat', fstab)
        self.assertNotIn('OLDEFI', fstab)
        self.assertIn('#/swapfile none swap', fstab)
        self.assertIn('NEWROOT', system.files['/mnt/usb/etc/grub.d/40_custom'])
        self.assertEqual(system.files['/mnt/usb/opt/U.opt'], 'STRAGE=""\nNEXT4=4GB-rescue\nSH=dash\n')
        chroot = [c for c in system.calls if c[:2] == ['arch-chroot', '/mnt/usb']][0]
        self.assertIn("saved_entry=RAM-6.12.57+4gb-base", chroot[-1])

    def test_m_small_and_checks(self):
        system = self.usb_fake(12 * 1024 ** 3)
        code, out = run(system, '-m', 'sde', '--ans=Y')
        self.assertIn('/dev/sde3:/(rest)', out)
        self.assertNotIn('4:0:0', [c for c in system.calls if c[0] == 'sgdisk'][-1])
        code, out = run(self.usb_fake(5 * 1024 ** 3), '-m', 'sde', '--ans=Y')
        self.assertIn('/dev/sde: too small for /dev/nvme0n1p2 (10240MiB).', out)
        code, out = run(self.usb_fake(32 * 1000 ** 3), '-m', 'nvme1n1')
        self.assertIn('nvme1n1: sd? only.', out)
        system = self.usb_fake(32 * 1000 ** 3)
        code, out = run(system, '-m', 'sde', '--list')                    # 聞いた所で止まる(消さない)
        self.assertFalse(any(c[0] == 'sgdisk' for c in system.calls))

    def test_r(self):
        system = self.save_fake()
        system.files['/opt/U.opt'] = 'STRAGE=/mnt/ext4 UUID=DDD\nNEXT8=8GB-KDE\n'
        system.files['/mnt/ext4/opt/8GB-KDE.img'] = 'x'
        system.files['/etc/hosts'] = '127.0.0.1 localhost\n127.0.1.1 8GB-base\n::1 ip6\n'
        system.results['systemctl list-unit-files --type=service --state=enabled,generated --no-legend'] = (
            0, 'ssh.service enabled\nsddm.service enabled\ncups.service enabled\ngetty@.service enabled\n')
        system.files['/mnt/debian/opt/rc_systemd.txt'] = 'ssh.service\n'
        system.results['systemctl show -p ExecMainStartTimestampMonotonic --value cups.service'] = (0, '0\n')
        code, out = run(system, '-r')
        loop = [c for c in system.calls if c[0] == 'sh' and '--exclude=etc/fstab' in c[2]][0]
        self.assertTrue(loop[-1].startswith('(cwd=/tmp/ush.'))
        self.assertEqual(system.files['/etc/hostname'], '8GB-KDE\n')
        self.assertTrue(system.files['/etc/hosts'].startswith('127.0.0.1\tlocalhost\n127.0.1.1\t8GB-KDE\n::1 ip6'))
        self.assertIn(['systemctl', 'try-restart', '--no-block', 'ssh.service'], system.calls)
        self.assertIn(['systemctl', 'start', '--no-block', 'cups.service'], system.calls)
        self.assertFalse(any('sddm.service' in c for c in system.calls if c[:2] == ['systemctl', 'start']))
        self.assertTrue(any(c[:2] == ['bash', '-c'] and 'DOT_FILES' in c[2] for c in system.calls))

    def test_r_missing_image_and_ssd(self):
        system = self.save_fake()
        system.files['/opt/U.opt'] = 'STRAGE=/mnt/ext4 UUID=DDD\nNEXT8=8GB-KDE\n'
        code, out = run(system, '-r')
        self.assertIn('-r: 8GB-KDE.img: not exist (booted as 8GB-base).', out)
        self.assertNotIn('/etc/hostname', system.files)
        system = fake(files={'/opt/BASE.u': 'x'})
        run(system, '-r')
        self.assertNotIn(['timedatectl', 'set-local-rtc', '0'], system.calls)   # SSDの上では何もしない

    def test_prepare(self):
        system = self.save_fake()
        system.results['blkid'] = (0, '/dev/sdd1: UUID="DDD" TYPE="ext4"\n')
        system.results['ip a'] = (0, '2: eth0: <BROADCAST> mtu\n    inet 10.0.0.2/24 scope global eth0\n')
        system.files['/etc/systemd/timesyncd.conf'] = 'x'
        system.files['/etc/apt/sources.list.d/debian.sources'] = 'Types: deb deb-src\nComponents: main non-free-firmware\n'
        lines = []
        ush_main.run(['-v', '--batch'], system=system, uname='6.12.57+8gb', out=lines.append)
        self.assertIn('UUID=DDD /mnt/ext4 ext4 rw,noauto', system.files['/etc/fstab'])
        self.assertIn('STRAGE(/mnt/ext4/opt):', '\n'.join(lines))
        system = self.save_fake()  # STRAGEの区画が無い: 使わない
        system.results['blkid'] = (0, '')
        lines = []
        ush_main.run(['-v', '--batch'], system=system, uname='6.12.57+8gb', out=lines.append)
        self.assertNotIn('/mnt/ext4', system.files['/etc/fstab'])
        self.assertNotIn('STRAGE(', '\n'.join(lines))
        self.assertIn('Network unreachable.', '\n'.join(lines))
        system = fake()
        system.results['stat -c %d:%i /'] = (0, '1:2\n')
        system.results['stat -c %d:%i /proc/1/root/.'] = (0, '1:3\n')
        lines = []
        self.assertEqual(ush_main.run(['-v'], system=system, uname='x', out=lines.append), 1)
        self.assertIn('in chroot', lines[0])

    def test_u_rest(self):
        files = {'/etc/fstab': 'UUID=ROOT / ext4 errors=remount-ro 0 1\n', '/opt/BASE.u': 'acl\t\t\t\n',
                 '/opt/U.opt': 'STRAGE=/mnt/ext4 UUID=DDD\n', '/opt/U.sh': '', '/opt/TEMPLATE--base.gz': 'x',
                 '/opt/linux-image-6.12.57+4gb_1_amd64.deb': 'x', '/opt/RAM-6.12.57+4gb-base.gz': 'x',
                 'mtime:/opt/RAM-6.12.57+4gb-base.gz': 2, 'mtime:/opt/linux-image-6.12.57+4gb_1_amd64.deb': 1,
                 '/opt/4GB-rescue.img': 'x', '/etc/default/grub': 'GRUB_DEFAULT=0\n',
                 '/etc/grub.d/40_custom': '#!/bin/sh\nexec tail -n +3 $0\n#\n#\n#\nold\n'}
        system = FakeSystem(parts={'/dev/nvme0n1p2': {'TYPE': 'ext4', 'UUID': 'ROOT'},
                                   '/dev/sdd1': {'TYPE': 'ext4', 'UUID': 'DDD'}},
                            mounted={'/': '/dev/nvme0n1p2'}, files=files, dirs={'/lib/modules/6.12.57+4gb'},
                            packages=['acl'])
        system.files['/etc/fstab'] += 'UUID=DDD /mnt/ext4 ext4 rw,noauto 0 1\n'
        system.results['dpkg -l'] = (0, 'ii  linux-image-6.12.57+4gb 1 amd64 x\n')
        lines = []
        ush_main.run(['-u', '--ans=n'], system=system, uname='6.12.57-amd64', out=lines.append, prepare=False)
        out = '\n'.join(lines)
        self.assertIn('GRUB_DEFAULT=saved\nGRUB_SAVEDEFAULT=true', system.files['/etc/default/grub'])
        custom = system.files['/etc/grub.d/40_custom']
        self.assertNotIn('old', custom)
        self.assertIn("menuentry 'NONE-KERNEL'", custom)
        self.assertIn("menuentry 'RAM-6.12.57+4gb-base'", custom)                 # 土台は新しいので作らず、項目だけ
        self.assertFalse(any(c[:2] == ['make', '-j9'] for c in system.calls))      # カーネルはある
        self.assertIn(['update-grub'], system.calls)
        self.assertIn(['grub-set-default', 'RAM-6.12.57+4gb-base'], system.calls)
        self.assertIn('NEXT4=4GB-rescue', system.files['/opt/U.opt'])
        self.assertIn('Reboot now (RAM-6.12.57+4gb-base)? (Y/n): n', out)
        self.assertNotIn(['reboot'], system.calls)
        lines = []
        ush_main.run(['-u'], system=fake(), uname='6.12.57+8gb', out=lines.append, prepare=False)
        self.assertIn('-u: Not on a NONE-KERNEL.', lines)

    def test_options_list(self):
        code, out = run(fake(), '--list')
        self.assertEqual(code, 0)
        self.assertIn('  -a(dd_strage)', out)
        self.assertIn('  -m(ake_usb) device [-]', out)
        self.assertNotIn('-u(pdate_base)', out)                          # RAMDISKには無い
        from usilic import rescue
        self.assertEqual([o for o, _ in rescue.parse_options(out)],
                         ['-a', '-c', '-d', '-f', '-k', '-m', '-n', '-o', '-s', '-v'])

    def test_v_pager_as_user(self):
        from usilic.ush.env import Env
        from usilic.ush.options import page_with_usilic
        system = fake(files={'/etc/passwd': 'root:x:0:0::/root:/bin/bash\nusilic:x:1000:1000::/home/usilic:/bin/bash\n'})
        page_with_usilic(Env(system, '6.12.57+8gb'), 'text')
        call = [c for c in system.calls if c[:1] == ['runuser']][0]
        self.assertEqual(call[:6], ['runuser', '-u', 'usilic', '--', 'env', 'HOME=/home/usilic'])  # ユーザーのHOMEで
        self.assertTrue(call[7].startswith('USILIC_OPEN=/') and call[-1] == '/mnt')   # /mntの(ls)の上にFILEで

    def test_a_releases_f_mount(self):
        system = fake(mounted={'/mnt/1': '/dev/sdd1'})
        system.dirs.add('/mnt/1')
        code, out = run(system, '-a', '--ans=2')
        self.assertIn('umount /mnt/1', out)
        self.assertEqual(system.mounted.get('/mnt/ext4'), '/dev/sdd1')       # 2か所にはマウントしない
        self.assertNotIn('/mnt/1', system.mounted)
        self.assertEqual(system.links.get('/mnt/1'), '/mnt/ext4')           # -fの/mnt/1はSTRAGEへのリンクに
        self.assertIn('STRAGE=/mnt/ext4 UUID=DDD', system.files['/opt/U.opt'])


if __name__ == '__main__':
    unittest.main()
