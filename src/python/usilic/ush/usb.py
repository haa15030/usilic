"""-m(make_usb): DEBIAN区画をUSB(/dev/sd?)へ写して、BIOSとUEFIの両方で起動できるrescue-USBを作る。

U.shと同じ手順: EFI(100MiB)・BIOS boot(1MiB)・ルート(ROOT_GB、DEBIAN区画より小さくしない)・STRAGE(残り、512MiBに満たな
ければ作らない)。ルートは区画ごとddで写す。先頭の4MiB(ext4のスーパーブロック=UUID)は最後に書き、すぐUUIDを変える
(途中で止まっても、USB側に同じUUIDのext4が無いので、SSDのGRUBがUSBの書きかけを選ばない)。
"""
import re

from .fs import cwd_in, fs, is_mounted
from .kernel import correct_uuid
from .options import change_root, entry_titles
from .parts import install_if_missing


def _size(system, device):
    text = system.output(['blockdev', '--getsize64', device]).strip()
    return int(text) if text.isdigit() else 0


def unmount_disk(env, log, disk):
    """_unmount_part(): diskの区画でマウントしているものを外す。"""
    for dev, point, _, _ in env.system.mounts():
        if dev.startswith(disk):
            fs(env, log, 'umount', point)


def make_usb(env, log, option, args, ask, out):
    system = env.system
    if not env.on_ramdisk:
        log.add('%s: Not on a RAMDISK.' % option)
        return False
    if not args or (len(args) > 1 and args[1] != '-'):
        log.add("%s: Please type '. U -m devicename'[Enter]" % option)
        return False
    if not re.match(r'^sd[a-z]+$', args[0]):
        log.add('%s: %s: sd? only.' % (option, args[0]))
        return False
    disk = '/dev/' + args[0]
    if not system.isblock(disk):
        log.add('%s: %s: No such device.' % (option, disk))
        return False
    root = env.debian_root_part
    source_disk = '/dev/' + (system.output(['lsblk', '-no', 'pkname', root]).splitlines() or [''])[0].strip()
    if source_disk == disk:
        log.add('%s: %s: %s is on it.' % (option, disk, root))
        return False
    efi_part, root_part, strage_part = disk + '1', disk + '3', disk + '4'
    fs(env, log, 'umount', env.mnt_debian)
    unmount_disk(env, log, source_disk)
    unmount_disk(env, log, disk)
    if system.is_mountpoint(env.mnt_debian) or any(d.startswith(disk) for d, _, _, _ in system.mounts()):
        log.add('%s: %s or %s: Busy.' % (option, env.mnt_debian, disk))
        # 写す前に外せない時、mnt_debianの中にいるもの(2026-11-26、直接のユーザー依頼: -vの後に/mnt/ubuntuで
        # シェルを開いてマウントを保っていると、ここで止まる)
        inside = cwd_in(env.mnt_debian) if system.is_mountpoint(env.mnt_debian) else []
        if inside:
            log.add("%s: in %s: %s -> 'cd', then '. U -m %s'" % (option, env.mnt_debian, ' '.join(inside), disk[len('/dev/'):]))
        return False
    root_kb = int(env.conf('ROOT_GB', '14') or 14) * 1024 * 1024
    source_kb = _size(system, root) // 1024
    if source_kb > root_kb:  # 区画ごとddで写すので、ルートはDEBIAN区画より小さくできない
        root_kb = (source_kb + 2047) // 1024 * 1024
    disk_size = _size(system, disk)
    if disk_size < (source_kb + 103 * 1024) * 1024:  # 足りなければ、消す前にやめる(EFI・BIOS boot・GPTの分)
        log.add('%s: %s: too small for %s (%dMiB).' % (option, disk, root, source_kb // 1024))
        return False
    if disk_size < (root_kb + (101 + 512) * 1024) * 1024:
        root_end, strage_part = '0', ''
    else:
        root_end = '+%dK' % root_kb
    fs_kind = env.conf('DST_STRAGE_FS', 'ext4') or 'ext4'
    out(system.output(['lsblk', '-o', 'NAME,SIZE,TYPE,FSTYPE,LABEL,MODEL', disk]).rstrip('\n'))
    if strage_part:
        out('-> %s1:EFI(100MiB) %s2:BIOS boot(1MiB) %s:/(%dMiB) %s:STRAGE(%s)'
            % (disk, disk, root_part, root_kb // 1024, strage_part, fs_kind))
    else:
        out('-> %s1:EFI(100MiB) %s2:BIOS boot(1MiB) %s:/(rest)' % (disk, disk, root_part))
    if ask('Erase %s and copy %s? (Y/N): ' % (disk, root), 'N').strip() not in ('Y', 'y'):
        return False
    install_if_missing(env, 'gdisk', out)
    install_if_missing(env, 'dosfstools', out)
    out('sgdisk -Z ' + disk)
    system.run(['sgdisk', '-Z', disk])
    code = '8300' if fs_kind == 'ext4' else '0700'
    layout = ['-n', '1:0:+100M', '-t', '1:ef00', '-n', '2:0:+1M', '-t', '2:ef02', '-n', '3:0:' + root_end, '-t', '3:8300']
    if strage_part:
        layout += ['-n', '4:0:0', '-t', '4:' + code]
    out('sgdisk %s %s' % (' '.join(layout), disk))
    if system.run(['sgdisk'] + layout + [disk])[0] != 0:
        log.add('%s: %s: sgdisk failed.' % (option, disk))
        return False
    system.run(['udevadm', 'settle'])
    system.sleep(2)
    if _size(system, root) > _size(system, root_part):
        log.add('%s: %s is smaller than %s.' % (option, root_part, root))
        return False
    out('dd if=/dev/zero of=%s bs=4M count=1 conv=fsync' % root_part)  # 前に途中で止まった時の残り
    system.run(['dd', 'if=/dev/zero', 'of=' + root_part, 'bs=4M', 'count=1', 'conv=fsync'])
    out('dd if=%s of=%s bs=4M skip=1 seek=1 conv=fsync status=progress' % (root, root_part))
    error = ''
    if system.stream(['dd', 'if=' + root, 'of=' + root_part, 'bs=4M', 'skip=1', 'seek=1', 'conv=fsync',
                      'status=progress'], out) != 0:
        error = 'dd failed.'
    else:
        out('dd if=%s of=%s bs=4M count=1 conv=fsync; tune2fs -U random %s' % (root, root_part, root_part))
        if system.run(['dd', 'if=' + root, 'of=' + root_part, 'bs=4M', 'count=1', 'conv=fsync'])[0] == 0:
            system.shell('echo y | tune2fs -U random ' + root_part)  # 確かめ(e2fsck)は後
            system.stream(['e2fsck', '-f', '-p', root_part], out)
            out('resize2fs ' + root_part)
            system.stream(['resize2fs', root_part], out)
        else:
            error = 'dd failed (first 4MiB).'
    if error:
        out('%s: %s' % (option, error))
        log.add('%s: %s' % (option, error))
        return False
    system.stream(['e2fsck', '-f', '-p', root_part], out)
    out('mkfs.vfat -F 32 -n EFI ' + efi_part)
    system.run(['mkfs.vfat', '-F', '32', '-n', 'EFI', efi_part])
    if strage_part:
        out('mkfs(%s) %s' % (fs_kind, strage_part))
        if fs_kind == 'ext4':
            system.run(['mkfs.ext4', '-q', '-F', '-L', 'STRAGE', strage_part])
        elif fs_kind == 'vfat':
            system.run(['mkfs.vfat', '-F', '32', '-n', 'STRAGE', strage_part])
        elif fs_kind == 'exfat':
            install_if_missing(env, 'exfatprogs', out)
            system.run(['mkfs.exfat', '-n', 'STRAGE', strage_part])
        elif fs_kind == 'ntfs':
            install_if_missing(env, 'ntfs-3g', out)
            system.run(['mkfs.ntfs', '-Q', '-L', 'STRAGE', strage_part])
        else:
            system.run(['wipefs', '-a', strage_part])
    system.run(['udevadm', 'settle'])
    fs(env, log, 'mount', root_part, '/mnt/usb')
    system.makedirs('/mnt/usb/boot/efi')
    if not correct_uuid(env, log, '/mnt/usb/etc/fstab', root, root_part, efi_part):
        return False
    fstab = system.read_text('/mnt/usb/etc/fstab') or ''  # SSDのスワップはUSBには無い
    system.write_text('/mnt/usb/etc/fstab', re.sub(r'^([^#].*\sswap\s)', r'#\1', fstab, flags=re.M))
    if not correct_uuid(env, log, '/mnt/usb/etc/grub.d/40_custom', root, root_part):
        return False
    system.run(['rm', '-f', '/mnt/usb/opt/U.opt', '/mnt/usb/opt/U.diff'])  # U.diffは以前の版が残したもの
    system.write_text('/mnt/usb/opt/U.opt', 'STRAGE=""\nNEXT%d=%dGB-rescue\nSH=%s\n'
                      % (env.rescue_gb, env.rescue_gb, env.opt.get('SH', 'bash')))
    fs(env, log, 'mount', efi_part, '/mnt/usb/boot/efi')
    entries = [t for t in entry_titles(system, '/mnt/usb') if re.match(r'^RAM-.*\+%dgb-base$' % env.rescue_gb, t)]
    rescue_entry = entries[-1] if entries else ''
    # BIOS(MBR+BIOS boot区画)とUEFI(EFI/BOOT/BOOTX64.EFI)の両方。--no-nvramで今の機械の起動設定には触らない
    change_root(env, log, ['/mnt/usb'], out, option=option, command=(
        'dpkg -s grub-pc-bin > /dev/null 2>&1 || apt-get install -y grub-pc-bin\n'
        'dpkg -s grub-efi-amd64-bin > /dev/null 2>&1 || apt-get install -y grub-efi-amd64-bin\n'
        'update-grub 2> /dev/null\n'
        'grub-install --target=i386-pc %s\n'
        'grub-install --target=x86_64-efi --efi-directory=/boot/efi --removable --no-nvram --no-uefi-secure-boot\n'
        "grub-editenv /boot/grub/grubenv set 'saved_entry=%s'\n" % (disk, rescue_entry)))
    out('-> %s (BIOS/UEFI, %s)' % (disk, rescue_entry))
    return True
