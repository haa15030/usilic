"""-s(save_host・_save_host): 今のホスト(RAMDISKの中身)を?GB-名前.imgに保存する。-d・-uも使う。

U.shと同じ手順: イメージの大きさを決め、保存先(rescueはDEBIAN区画、他はSTRAGE)と作る場所(rescueでSTRAGEがあれば
STRAGE)に空きがあるか見る。rescueの上書きは今のイメージを控えてから(失敗したら控えから戻す)。中身の無いファイル
(truncate)をext4にしてループでマウントし、tarで写す(カーネルのモジュールとIMG_EXCLUDEは入れない)。RAMDISKの
骨組み(_tmp_debian)を重ね、最後にRAMDISKに収まるかを確かめる。
"""
import re

from .fs import fs


def tmpfs_size(env, used=None):
    """_tmpfs_size(): usedが無ければ/dev/shmの空き(KB)、あればusedにSHM_LISTのtmpfsの使用量を足したもの。"""
    lines = env.system.output(['df']).splitlines()[1:]
    if used is not None:
        total = used
        if env.shm_list and env.mem_total_gb >= env.rescue_gb:
            for line in lines:
                f = line.split()
                if len(f) >= 6 and f[5] in ['/' + s for s in env.shm_list]:
                    total += int(f[2])
        return total
    free = 0
    for line in lines:
        f = line.split()
        if len(f) < 6:
            continue
        if f[5] == '/dev/shm':
            free = int(f[1])
        if f[0] == 'tmpfs' or f[0].startswith('/dev/shm'):
            free -= int(f[2])
    return free


# ?GB-rescue.imgに入れないもの(2026-11-27)。/usr/srcはdkmsの元(nvidia-*)とlinux-headers、gsp_*はnvidiaのGSPのfirmware
# (どちらもRAMの土台の側で、展開の前にnvidiaを読み込む時に使い終わっている)。入れるとDebian 13の4GB-rescueが4GBの
# RAMDISKを231224KB超え、起動の時の展開で場所が尽きて止まった
RESCUE_EXCLUDE = ('usr/src/*', 'usr/lib/firmware/nvidia/*/gsp_*')


def ramdisk_cap_kb(env, gb):
    """_ramdisk_cap_kb(): gbのRAMDISK(同じ大きさでmkfs.ext4)に入る量(KB、dfの大きさ)。/tmpに中身の無いファイルで。"""
    system = env.system
    image, mnt = env.tmp + '/cap.img', env.tmp + '/cap'
    system.makedirs(env.tmp)
    kb = None
    if system.run(['truncate', '-s', str(gb * 1000 * 1000 * 1000), image])[0] == 0 and \
            system.run(['mkfs.ext4', '-q', image])[0] == 0:
        system.makedirs(mnt)
        if system.run(['mount', '-o', 'loop,ro', image, mnt])[0] == 0:
            lines = system.output(['df', '-k', mnt]).splitlines()
            try:
                kb = int(lines[1].split()[1])
            except (IndexError, ValueError):
                kb = None
            system.run(['umount', mnt])
        system.rmdir(mnt)
    system.run(['rm', '-f', image])
    return kb


def restore_bak(env, bak, image, out):
    """_restore_bak(): 控え(.BAK.gzか.BAK.img)があれば戻す。"""
    system = env.system
    if not bak or not system.exists(bak):
        return
    if bak.endswith('.gz'):
        out('zcat %s > %s' % (bak, image))
        system.shell('zcat %s > %s' % (bak, image))
    else:
        out('cp %s %s' % (bak, image))
        system.run(['cp', bak, image])


def on_usb(env):
    """_on_usb(): DEBIAN区画がUSBのディスク(USBメモリ等)にあるか。"""
    system = env.system
    disk = system.output(['lsblk', '-ndo', 'PKNAME', env.debian_root_part]).strip() if env.debian_root_part else ''
    return bool(disk) and system.output(['lsblk', '-ndo', 'TRAN', '/dev/' + disk]).strip() == 'usb'


RAM_DIRS = 'boot dev media opt proc run sys usr/lib/modules tmp mnt/{debian}'


def ram_fstab(env, fstab_text):
    """RAMDISKのetc/fstab(_tmp_debian): DEBIAN区画・/boot/efi・STRAGEの行は除き、ルートの行は「/dev/ram / ext4
    errors=remount-ro」(ON_RAMDISKはこれで見分ける)、ルートの区画の中のスワップファイル(Ubuntuの/swap.img)は#に。
    最後にDEBIAN区画を$MNT_DEBIANへ(noauto)。"""
    out = []
    for line in fstab_text.splitlines():
        if any(m in line for m in ('/mnt/debian', env.mnt_debian, '/boot/efi', '/mnt/ext4', '/mnt/ntfs', '/mnt/exfat')):
            continue
        f = line.split()
        if f and not f[0].startswith('#') and len(f) >= 2 and f[1] == '/':
            out.append('/dev/ram / ext4 errors=remount-ro 0 1')
        elif f and f[0].startswith('/') and not f[0].startswith('/dev/') and len(f) >= 3 and f[2] == 'swap':
            out.append('#' + line)
        else:
            out.append(line)
    uuid = env.debian_root_uuid
    if not uuid:  # U.shと同じく、fstabのremount-roの行の「=」の後
        for line in fstab_text.splitlines():
            if 'remount-ro' in line and '=' in line:
                uuid = line.split('=', 1)[1].split()[0]
                break
    out.append('UUID=%s %s ext4 rw,noauto\t    0 1' % (uuid, env.mnt_debian))
    return ''.join(l + '\n' for l in out)


def tmp_debian(env, out, plus=''):
    """_tmp_debian(): RAMDISKの骨組み(/tmp/…/debian、dev/console・dev/null、usr/bin等へのリンク、etc/fstab)。
    plusが「-」ならetcを作らない(-uのテンプレート)。作ったディレクトリを返す。"""
    system = env.system
    root = env.tmp + '/' + env.debian
    if system.isdir(root):
        return root
    out('mkdir ' + root)
    system.makedirs(root)
    dirs = RAM_DIRS.format(debian=env.debian).split()
    system.run(['mkdir', '-p'] + dirs, cwd=root)
    for cmd in (['mknod', 'dev/console', 'c', '5', '1'], ['chmod', '600', 'dev/console'],
                ['mknod', 'dev/null', 'c', '1', '3'], ['chmod', '666', 'dev/null'], ['chmod', '777', 'tmp'],
                ['ln', '-sf', 'usr/bin', 'usr/lib', 'usr/lib64', 'usr/sbin', '.']):
        out(' '.join(cmd))
        system.run(cmd, cwd=root)
    if plus in ('', '+'):
        system.makedirs(root + '/etc')
        system.write_text(root + '/etc/fstab', ram_fstab(env, system.read_text('/etc/fstab') or ''))
        if plus == '+':
            system.run(['mkdir', 'bin.usr-is-merged', 'lib.usr-is-merged', 'sbin.usr-is-merged'], cwd=root)
    return root


def _df_field(system, path, index):
    try:
        return int(system.output(['df', '-k', path]).splitlines()[1].split()[index])
    except (IndexError, ValueError):
        return 0


def save_host(env, log, option, args, out):
    """_save_host(): args = [名前] [.] [-](U.shの$2 $3 $4)。名前が「.」なら今のホスト名で上書き。成功ならTrue。"""
    system = env.system
    name = args[0] if args else ''
    dot = args[1] if len(args) > 1 else ''
    work = args[2] if len(args) > 2 else ''
    hostname = system.output(['hostname']).strip()
    gb = env.ramdisk_gb
    rescue = '%dGB-rescue' % gb
    if hostname.endswith('GB-base') and name == '.':
        log.add("%s: Can't save to base-host." % option)
        return False
    fs(env, log, 'mount', env.mnt_debian)
    strage = env.strage or env.mnt_debian  # U.shのmnt_strage(STRAGEが無ければDEBIAN区画)
    fs(env, log, 'mount', strage)
    plus = ''
    new_host = ''
    if env.on_ubuntu and not env.on_ramdisk and not env.on_none:
        new_host = hostname if '%dGB' % gb in hostname else '%dGB-%s' % (gb, hostname)
        plus = '+' if name == '+' else ''
    elif name == '.':
        new_host = hostname
    elif env.on_none:
        new_host = rescue
    elif name and dot == '.':
        new_host = '%dGB-%s' % (gb, re.sub('^%dGB-' % gb, '', name))
    elif name:
        new_host = '%dGB-%s' % (gb, re.sub('^%dGB-' % gb, '', name))
        existing = set(n[:-4] for d in (env.opt_dir(strage), env.mnt_debian + '/opt') for n in system.listdir(d)
                       if n.startswith('%dGB-' % gb) and n.endswith('.img'))
        if new_host in existing:
            log.add("%s: %s: already exist. To overwrite, type '. U %s %s .'[Enter]" % (option, new_host, option, name))
            return False
    if not new_host:
        log.add("%s: Please type '. U %s name' or '. U %s .'[Enter]" % (option, option, option))
        return False
    # STRAGEが無い(DEBIAN区画に保存する)時は、rescueの上書きだけ
    if (not env.on_ubuntu or env.on_ramdisk) and strage == env.mnt_debian and new_host != rescue:
        log.add("%s: %s: Strage-area needed ('. U -a'). Without it, only %s can be saved." % (option, new_host, rescue))
        return False
    # rescue-USB(DEBIAN区画がUSBメモリ)でSTRAGEが無い: 遅すぎるので、先にSTRAGEを設定してもらう
    if strage == env.mnt_debian and new_host == rescue and on_usb(env):
        message = "%s: %s: on a USB memory, Strage-area needed (too slow without it). Type '. U -a', then '. U %s %s'." \
            % (option, new_host, option, name)
        out(message)
        log.add(message)
        return False
    work_area = env.work_area
    if work == '-' or env.on_none or (env.on_ubuntu and not env.on_ramdisk):
        work_area = 'opt'
    out('HOST:' + new_host)
    fs(env, log, 'umount', '/usr/src')
    if any(m == '/usr/src' for _, m, _, _ in system.mounts()):
        log.add('%s: /usr/src: Busy.' % option)
        return False
    out('apt-get clean')
    system.run(['apt-get', 'clean'])
    excludes = ' '.join(env.img_exclude)
    if env.on_none or (env.on_ubuntu and not env.on_ramdisk):
        # 使用量(カーネルのモジュールとIMG_EXCLUDEを除く)のNUMERATOR/10倍+100MB
        used = _du_total(system, 'du -skxc [e-h]* root srv usr var 2>/dev/null | tail -n1 | cut -f1')
        used -= _du_total(system, 'du -skxc usr/lib/modules %s 2>/dev/null | tail -n1 | cut -f1' % excludes)
        img_kb = used * env.numerator // 10 + 100000
        out('used: %dKB -> img: %dKB' % (used, img_kb))
    else:
        root_used = 0
        for line in system.output(['df']).splitlines()[1:]:
            f = line.split()
            if len(f) >= 6 and f[5] == '/':
                root_used = int(f[2])
        img_kb = tmpfs_size(env, root_used) * 11 // 10  # ルートシステム長の1.1倍
    # 保存先: rescueはDEBIAN区画(-mでUSBへそのまま写り、STRAGEが無くても起動できる)、それ以外はSTRAGE
    dst = env.mnt_debian + '/opt' if new_host == rescue else env.opt_dir(strage)
    bld = env.opt_dir(strage) if new_host == rescue and strage != env.mnt_debian else dst
    system.makedirs(dst)
    system.makedirs(bld)
    image = '%s/%s.img' % (dst, new_host)
    bak = ''
    if new_host == rescue and system.exists(image):
        if bld != dst:
            bak = '%s/%s.BAK.img' % (bld, new_host)
            out('cp %s %s' % (image, bak))
            if system.run(['cp', image, bak])[0] != 0:
                system.run(['rm', '-f', bak])
                bak = ''
        else:
            bak = '%s/%s.BAK.gz' % (dst, new_host)
            out('gzip -c %s > %s' % (image, bak))
            if system.shell('gzip -c %s > %s' % (image, bak))[0] != 0:
                system.run(['rm', '-f', bak])
                bak = ''
        if not bak:
            message = '%s: %s.BAK: failed (no space?).' % (option, new_host)
            out(message)
            log.add(message)
            return False
        system.run(['rm', '-f', image])
        out(system.output(['ls', '-l', bak]).rstrip('\n'))

    def fail(message):
        out(message)
        log.add(message)
        restore_bak(env, bak, image, out)
        return False

    # 保存先(と作る場所)にイメージを置く空きがあるか
    for place in sorted(set([dst, bld])):
        free = _df_field(system, place + '/', 3)
        out('free: %dKB (%s)' % (free, place))
        if img_kb > free:
            return fail('%s: %s: img %dKB > free %dKB (%s).' % (option, new_host, img_kb, free, place))
    loop0, shm0 = env.tmp + '/loop0', env.tmp + '/shm0'
    system.makedirs(env.tmp)
    if work_area == 'opt' or img_kb > tmpfs_size(env):
        img0 = bld + '/IMG0.img'
        out('truncate -s %d %s' % (img_kb * 1000, img0))
        system.run(['rm', '-f', img0])
        code, message = system.run(['truncate', '-s', str(img_kb * 1000), img0])
        if code != 0:
            system.run(['rm', '-f', img0])
            return fail('%s: IMG0.img: %s' % (option, (message.splitlines() or [''])[-1]))
        system.run(['mkfs.ext4', '-q', '-m', '0', img0])  # root用の予約(既定の5%)は要らない
        out('mount -o loop %s %s' % (img0, loop0))
        fs(env, log, 'mount', '-o', 'loop', img0, loop0)
    else:
        img0 = shm0 + '/IMG0.img'
        fs(env, log, 'umount', loop0)
        out('mount -t tmpfs -o size=%dK /dev/shm %s' % (img_kb, shm0))
        fs(env, log, 'mount', '-t', 'tmpfs', '-o', 'size=%dK' % img_kb, '/dev/shm', shm0)
        out('truncate -s %d %s' % (img_kb * 1000, img0))
        system.run(['truncate', '-s', str(img_kb * 1000), img0])
        system.run(['mkfs.ext4', '-q', '-m', '0', img0])  # root用の予約(既定の5%)は要らない
        out('mount -o loop %s %s' % (img0, loop0))
        fs(env, log, 'mount', '-o', 'loop', img0, loop0)
    # カーネルのモジュールは入れない(RAMの土台の側にある)。+なら今のカーネルのものだけ入れる
    if plus == '+':
        excl = ' '.join('--exclude=usr/lib/modules/' + n for n in system.listdir('/usr/lib/modules') if n != env.uname)
    else:
        excl = "--exclude='usr/lib/modules/*'"
    excl += ''.join(" --exclude='%s'" % x for x in env.img_exclude)
    if new_host == rescue:
        excl += ''.join(" --exclude='%s'" % x for x in RESCUE_EXCLUDE)
    command = 'tar cf - %s [e-h]* root srv usr var | tar xfp - -C %s' % (excl, loop0)
    out(command)
    errors = env.tmp + '/tar.err'  # 写す側(tar xfp)のエラーだけ(U.shの2>/tmp/error.*)
    _, shown = system.shell(command + ' 2>' + errors, cwd='/')
    for line in shown.splitlines():
        out(line)
    tar_errors = system.read_text(errors) or ''
    skeleton = tmp_debian(env, out, plus)
    system.shell('tar cf - * | tar xfp - -C %s' % loop0, cwd=skeleton)
    out(system.output(['df', loop0]).rstrip('\n'))
    used_kb = _df_field(system, loop0, 2)  # イメージの中身の実際の使用量
    shm_kb = 0  # そのうちRAMDISKに入らない分(SHM_LISTのディレクトリは、-rでtmpfsの方に入る)
    for d in env.shm_list:
        if system.isdir(loop0 + '/' + d):
            shm_kb += _du_total(system, 'du -skx %s/%s | cut -f1' % (loop0, d))
    fs(env, log, 'umount', loop0)
    if tar_errors.strip():
        system.run(['rm', '-f', bld + '/IMG0.img'])
        fs(env, log, 'umount', shm0)
        return fail('%s: %s' % (option, tar_errors.strip()))
    out('mv %s %s' % (img0, image))
    if system.run(['mv', img0, image])[0] != 0:
        system.run(['rm', '-f', image, bld + '/IMG0.img'])
        fs(env, log, 'umount', shm0)
        return fail('%s: %s.img: failed (no space?).' % (option, new_host))
    fs(env, log, 'umount', shm0)
    out(system.output(['sh', '-c', 'ls -l %s/%dGB* %s' % (dst, gb, bak)]).rstrip('\n'))
    out(system.output(['df', dst + '/']).rstrip('\n'))
    # RAMDISKに収まるか(最後のチェック)。収まらなくてもイメージは残す(中を見て削るものを決められるように)
    cap_kb = ramdisk_cap_kb(env, gb)
    mod_kb = _du_total(system, 'du -skc /usr/lib/modules/*+%dgb 2>/dev/null | tail -n1 | cut -f1' % gb)
    need_kb = used_kb - shm_kb + mod_kb + env.ramdisk_margin_kb
    out('%s.img: used %dKB - %s %dKB + modules %dKB + margin %dKB = %dKB / RAMDISK %sKB'
        % (new_host, used_kb, ' '.join(env.shm_list), shm_kb, mod_kb, env.ramdisk_margin_kb, need_kb, cap_kb))
    if not plus and cap_kb and need_kb > cap_kb:
        message = "%s: %s.img: %dKB > %dGB RAMDISK %dKB (over %dKB, can't boot)." % (
            option, new_host, need_kb, gb, cap_kb, need_kb - cap_kb)
        out(message)
        log.add(message)
        log.add('mount -o loop,ro %s /mnt; du -sh /mnt/* /mnt/usr/* /mnt/usr/share/*' % image)
    return True


def _du_total(system, command):
    _, text = system.shell(command, cwd='/')
    try:
        return int(text.strip().splitlines()[-1])
    except (IndexError, ValueError):
        return 0


def save(env, log, option, args, out):
    """save_host()(-s): RAMDISKの上で(UbuntuはSSDの上でも)。"""
    if not env.on_ramdisk and (not env.on_ubuntu or env.on_none or env.on_toram):
        log.add('%s: Not on a RAMDISK.' % option)
        return False
    return save_host(env, log, option, args, out)
