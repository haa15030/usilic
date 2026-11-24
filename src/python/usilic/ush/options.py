"""移したオプション(U.shの同じ名前の関数)。"""
import os
import re
import sys
import tempfile

from .fs import fs, is_mounted
from .parts import default_tmp, list_parts


def verbose(env, log, ask, out, page):
    """-v(verbose()): DEBIAN区画・STRAGEの/opt、-fでマウントした/mnt/番号、df。答えを前から渡された時(--batch等)は
    そのまま出し、でなければページャー(page)で。"""
    system = env.system
    text = ['']
    if env.on_none or env.on_wsl:
        text += ['DEBIAN(/opt):', system.output(['ls', '-l', '/opt/']).rstrip('\n')]
    else:
        fs(env, log, 'mount', env.mnt_debian)
        text += ['DEBIAN(%s/opt):' % env.mnt_debian, system.output(['ls', '-l', env.mnt_debian + '/opt/']).rstrip('\n')]
    strage = env.strage
    if strage and strage != env.mnt_debian:
        system.makedirs(strage)
        if not is_mounted(system, strage):
            if env.uuid:
                fs(env, log, 'mount', '-o', 'rw', 'UUID=' + env.uuid, strage)
            else:
                fs(env, log, 'mount', strage)
        text += ['', 'STRAGE(%s/opt):' % strage, system.output(['ls', '-l', strage + '/opt/']).rstrip('\n')]
    text += list_mnt_no(env)
    text += ['', system.output(['df']).rstrip('\n')]
    body = '\n'.join(text) + '\n'
    if ask.batch:
        out(body.rstrip('\n'))
    else:
        page(body)


def list_mnt_no(env):
    """_list_mnt_no(): -fでマウントした/mnt/番号の素性と中身。"""
    system = env.system
    lines = []
    for name in system.listdir('/mnt'):
        if not name[:1].isdigit():
            continue
        path = '/mnt/' + name
        real = system.realpath(path)
        if is_mounted(system, real):
            info = system.output(['findmnt', '-no', 'SOURCE,FSTYPE', real]).strip()
            device = info.split(' ')[0]
            uuid = system.output(['blkid', '-c', '/dev/null', '-s', 'UUID', '-o', 'value', device]).strip()
            options = system.output(['findmnt', '-no', 'OPTIONS', real]).strip().split(',')[0]
            info = '%s %s (%s)' % (info, uuid, options)
        else:
            info = '(not mounted)'
        if real != path:
            info += ' -> ' + real
        lines += ['', '%s: %s' % (path, info)]
        lines += [l for l in system.output(['ls', '-l', path + '/']).rstrip('\n').splitlines()
                  if not l.startswith('total')]
    return lines


def file_system(env, log, args, ask, out):
    """-f(file_system()): マウントできる区画(-aと同じ一覧+vfat)から番号を選んで/mnt/番号へ。「-f -」で全部外す。
    fstabは書き換えない(再起動すれば無くなる)。もうどこかにマウントされている区画は、/mnt/番号をそこへのリンクに。"""
    system = env.system
    if env.on_wsl:
        log.add('-f: Not supported.')
        return
    if args[:1] == ['-']:
        for name in system.listdir('/mnt'):
            if not name[:1].isdigit():
                continue
            path = '/mnt/' + name
            if system.islink(path):
                out('rm ' + path)
                system.unlink(path)
            else:
                out('umount ' + path)
                fs(env, log, 'umount', path)
                if not is_mounted(system, path):
                    system.rmdir(path)
        return
    if args:
        log.add("-f %s: Invalid option ('. U -f' or '. U -f -')." % args[0])
        return
    out(' No)TYPE DEVICENAME UUID (free) [mounted]')
    chosen = list_parts(env, log, ['ntfs', 'exfat', 'ext4', 'vfat'], ask, out, multi=True, tmp=default_tmp())
    out('')
    for part in chosen:
        path = '/mnt/%d' % part.number
        device = part.device
        mount_point = (system.output(['findmnt', '-no', 'TARGET', device]).splitlines() or [''])[0].strip()
        # 前の/mnt/番号(番号は区画の増減で変わる)は外す
        if system.islink(path):
            system.unlink(path)
        elif is_mounted(system, path):
            source = system.output(['findmnt', '-no', 'SOURCE', path]).strip()
            if source != device:
                out('umount %s (%s)' % (path, source))
                fs(env, log, 'umount', path)
        if mount_point == path:
            out('%s: %s (already mounted)' % (path, device))
        elif mount_point:
            system.rmdir(path)
            out('ln -s %s %s' % (mount_point, path))
            system.symlink(mount_point, path)
        else:
            system.makedirs(path)
            # 休止したWindows(高速スタートアップ)のNTFS等は読み書きではマウントできないので、読むだけで
            code, message = system.run(['mount', '-o', 'rw', 'UUID=' + part.uuid, path])
            if code != 0:
                code, message2 = system.run(['mount', '-o', 'ro', 'UUID=' + part.uuid, path])
                message = message or message2
            if code == 0:
                kind = system.output(['findmnt', '-no', 'FSTYPE,OPTIONS', path]).strip().split(',')[0]
                out('mount %s %s (%s)' % (device, path, kind))
            else:
                log.add("-f: %s: can't mount. %s" % (device, (message.splitlines() or [''])[0]))
                system.rmdir(path)


def page_with_usilic(env, text):
    """_page(): このusilic.pyで(/mntの(ls)の上にFILEで開く。戻って読め、^Gで/mntへ、qで終える)。usilicはU.shと同じrootで
    (SHELLもroot。旧tcbと同じく/root/.usilicができてかまわない。前は一般ユーザーにしていた)、HOMEはrootのもの。標準エラー出力は
    捨てる(最初の起動でHELPを組むgroffの警告が端末に出ないように)。"""
    system = env.system
    fd, path = tempfile.mkstemp(prefix='U-v.', suffix='.txt')
    with os.fdopen(fd, 'w', encoding='utf-8') as f:
        f.write(text)
    os.chmod(path, 0o644)
    usilic = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'usilic.py')
    # ページャーでなく、/mntの(ls)の上にFILEで開く(USILIC_OPEN)。^Gで/mntへ出て、そのまま/mnt/番号へ行ける
    if os.path.isfile(usilic):
        argv = ['env', 'USILIC_OPEN=' + path, sys.executable, usilic, '/mnt']
    else:
        argv = ['less', path]
    if system.euid() == 0:
        argv = ['env', 'HOME=/root'] + argv
    system.interactive(argv, quiet=True)
    os.unlink(path)


def _write_opt(env, lines):
    """/opt/U.optを書き、RAMDISKならDEBIAN区画の/opt/U.optへも。"""
    system = env.system
    text = ''.join(l + '\n' for l in lines)
    system.write_text('/opt/U.opt', text, 0o644)
    if env.on_ramdisk:
        system.write_text(env.mnt_debian + '/opt/U.opt', text, 0o644)


def add_strage(env, log, ask, out):
    """-a(add_strage()): STRAGE(イメージ等を置く区画)を選んで/opt/U.optへ(STRAGE=/mnt/種類 UUID=〜)。0)はSTRAGEを
    使わない(STRAGE="")。選んだ区画は/etc/fstabに/mnt/種類(noauto)で書いてマウントし、空きが10GBより多ければ
    カーネルのビルド用イメージ($BUILD.img)をDEBIAN区画から写す。"""
    from .parts import NONE
    system = env.system
    if env.on_wsl:
        log.add('-a: Not supported.')
        return
    if env.strage == '/mnt/cifs':
        log.add('-a: STRAGE=/mnt/cifs.')
        return
    others = [l for l in (system.read_text('/opt/U.opt') or '').splitlines() if not l.startswith('STRAGE=')]
    out(' 0)TYPE DEVICENAME UUID (free) [mounted]')
    chosen = list_parts(env, log, ['ntfs', 'exfat', 'ext4'], ask, out, tmp=default_tmp())
    out('')
    if not chosen:
        return
    if chosen[0] == NONE:
        _write_opt(env, others + ['STRAGE=""'])
        log.add('-a: STRAGE="" -> /opt/U.opt')
        return
    part = chosen[0]
    strage = '/mnt/' + part.kind
    # 選んだ区画が他の所(-fの/mnt/番号、デスクトップの/media/〜等)にマウントされていれば外す(ntfs-3g・exfat-fuseは
    # 同じ区画を2か所にマウントできず「already exclusively opened」で止まった)。使用中なら、U.optを書き換える前にやめる
    relink = []
    for point in system.output(['findmnt', '-rno', 'TARGET', '-S', 'UUID=' + part.uuid]).split():
        if point == strage:
            continue
        out('umount ' + point)
        system.run(['umount', point])
        if system.is_mountpoint(point):
            log.add('%s: busy.' % point)
            log.add("-a: close it (or '. U -f -'), then '. U -a' again.")
            return
        if re.match(r'^/mnt/[0-9]', point):
            relink.append(point)
    opt_img = ''
    if env.on_ramdisk:
        opt_img = env.mnt_debian
        fs(env, log, 'mount', opt_img)
    if env.strage and env.strage != env.mnt_debian:
        out('umount ' + env.strage)
        fs(env, log, 'umount', env.strage)
    _write_opt(env, others + [part.strage()])
    log.add(part.strage() + ' -> /opt/U.opt')
    system.makedirs(strage)
    fstab = [l for l in (system.read_text('/etc/fstab') or '').splitlines()
             if l.strip() and not any(m in l for m in ('/mnt/ext4', '/mnt/ntfs', '/mnt/exfat'))]
    fstab.append('UUID=%s %s %s rw,noauto 0 1' % (part.uuid, strage, part.kind))
    system.write_text('/etc/fstab', ''.join(l + '\n' for l in fstab))
    system.run(['systemctl', 'daemon-reload'])
    fs(env, log, 'mount', strage)
    for point in relink:  # 外した-fの/mnt/番号は、STRAGEへのリンクに
        if system.rmdir(point) and system.symlink(strage, point):
            out('ln -s %s %s' % (strage, point))
    system.makedirs(strage + '/opt')
    df = system.output(['df']).rstrip('\n')
    out(df)
    free_gb = 0
    for line in system.output(['df', '-k', strage]).splitlines()[1:2]:
        try:
            free_gb = int(line.split()[3]) // 1000 // 1000
        except (IndexError, ValueError):
            pass
    build = '%s/opt/%s.img' % (opt_img, env.ubuntu_build)
    if free_gb > 10 and system.exists(build):
        destination = strage + '/opt/' + build.rsplit('/', 1)[1]
        source_time, destination_time = system.mtime(build), system.mtime(destination)
        copy = destination_time is None
        if not copy and source_time is not None and source_time > destination_time:  # cp -iu: 新しければ聞いて
            copy = ask('overwrite %s? (y/n) ' % destination, 'n').strip().lower() == 'y'
        if copy:
            out('cp -uv %s %s/opt' % (build, strage))
            for line in system.output(['cp', '-uv', build, strage + '/opt']).splitlines():
                out(line)
    out('')
    out('STRAGE(%s/opt):' % strage)
    out('\n'.join(l for l in system.output(['ls', '-l', strage + '/opt/']).rstrip('\n').splitlines()
                  if not l.startswith('total')))
    out('')


def entry_titles(system, root):
    """_entry_titles(): root/etc/grub.d/40_customの起動項目名。"""
    import re
    text = system.read_text(root.rstrip('/') + '/etc/grub.d/40_custom') or ''
    return [m.group(1) for m in re.finditer(r"^menuentry '([^']*)'", text, re.M)]


def next_host(env, log, ask, out):
    """-n(next_host()): 次に起動するホスト(?GB-*.imgと?GB-base)・GRUBの項目を選ぶ。ホストならU.optのNEXT?=と、
    その大きさのRAM-*+?gb-baseを、GRUBのgrubenvのsaved_entryへ(update-grubはしない)。最後に再起動するか聞く。"""
    import re
    system = env.system
    if env.on_wsl:
        log.add('-n: Not supported.')
        return
    root = env.mnt_debian
    if env.on_toram:
        # TORAMの/mnt/debianはSSDの「/へのリンク」を写したもので、tmpfsの/を指す(grubenvを書いても再起動で消え、
        # TORAMのまま起動した)。SSDのルートの区画を別にマウントして、そちらのgrubenvへ
        root = env.tmp + '/root'
        system.makedirs(root)
        if not env.debian_root_part or \
                not fs(env, log, 'mount', env.debian_root_part, root, try_only=True):
            log.add('-n: %s: cannot mount the root partition.' % (env.debian_root_part or '?'))
            return
    else:
        fs(env, log, 'mount', env.mnt_debian)
    if env.strage:
        fs(env, log, 'mount', env.strage)
    titles = entry_titles(system, root)
    names = set()
    for directory in (env.strage + '/opt' if env.strage else None, root + '/opt'):
        if directory:
            names.update(n[:-4] for n in system.listdir(directory) if re.match(r'^[0-9]+GB-.*\.img$', n))
    for t in titles:
        m = re.match(r'^RAM-.*\+([0-9]+)gb-base$', t)
        if m:
            names.add(m.group(1) + 'GB-base')
    choices = []
    for name in sorted(names):
        gb = int(name.split('GB-', 1)[0])
        if gb > env.mem_total_gb or not any(re.match(r'^RAM-.*\+%dgb-base$' % gb, t) for t in titles):
            continue  # メモリが足りないか、そのカーネルが無い
        choices.append(('H', name))
    choices += [('E', t) for t in titles if not t.startswith('RAM-')]
    for i, (_, name) in enumerate(choices, 1):
        out(' %d)%s' % (i, name))
    out('  )<EXIT>')
    answer = ask('Input No:').strip()
    if not answer.isdigit() or not 1 <= int(answer) <= len(choices):
        return
    kind, name = choices[int(answer) - 1]
    if kind == 'H':
        gb = int(name.split('GB-', 1)[0])
        lines = [l for l in (system.read_text('/opt/U.opt') or '').splitlines() if not l.startswith('NEXT%d=' % gb)]
        _write_opt(env, lines + ['NEXT%d=%s' % (gb, name)])
        if env.on_toram:  # TORAMの/optはtmpfs。SSDの方へも(RAMDISKで起動した時のrc.localはそちらを読む)
            system.write_text(root + '/opt/U.opt', ''.join(l + '\n' for l in lines + ['NEXT%d=%s' % (gb, name)]), 0o644)
        out('NEXT%d=%s -> /opt/U.opt' % (gb, name))
        name = [t for t in titles if re.match(r'^RAM-.*\+%dgb-base$' % gb, t)][-1]
    # GRUB_DEFAULT=saved(-uで設定)なので、update-grubせずgrubenvのsaved_entryだけ書き換える
    grubenv = root + '/boot/grub/grubenv'
    out('grub-editenv %s set saved_entry=%s' % (grubenv, name))
    system.run(['grub-editenv', grubenv, 'set', 'saved_entry=' + name])
    grub = system.read_text(root + '/etc/default/grub') or ''
    if not re.search(r'^GRUB_DEFAULT=saved', grub, re.M):
        log.add("-n: GRUB_DEFAULT is not 'saved' in %s/etc/default/grub." % root)
    if env.on_toram:
        fs(env, log, 'umount', root)
    yn = ask('Reboot now? (Y/n): ', 'n').strip()
    if yn in ('', 'Y', 'y'):
        if not env.on_toram:
            fs(env, log, 'umount', env.mnt_debian)
        system.run(['reboot'])


def change_root(env, log, args, out, command=None, option='-c'):
    """-c(change_root()・_change_root()): 引数が無ければDEBIAN区画(RAMDISKの時だけ)、あればそのディレクトリ(マウント
    されたルートの区画、/mnt/usb等)へarch-chroot。commandがあれば、その中で/bin/sh -c command(-mが使う)。終えたら
    umount -R。"""
    from .parts import install_if_missing
    system = env.system
    target = args[0] if args else ''
    if not target and (env.on_none or env.on_wsl):
        log.add('%s: Not on a RAMDISK.' % option)
    root = ''
    if not target or target == env.mnt_debian:
        if not env.on_ramdisk:
            log.add('%s: Not on a RAMDISK.' % option)
            return False
        fs(env, log, 'mount', env.mnt_debian)
        root = env.mnt_debian
    else:
        directory = target.rstrip('/') or '/'
        if not system.exists(directory):
            log.add('%s: %s: No such directory.' % (option, directory))
        elif not system.is_mountpoint(directory):
            log.add('%s: %s: not mounted.' % (option, directory))
        elif not all(system.exists(directory + d) for d in ('/dev', '/proc', '/sys')):
            log.add('%s: %s: not a root system.' % (option, directory))
        else:
            root = directory
    if not root:
        return False
    install_if_missing(env, 'arch-install-scripts', out)
    out('arch-chroot ' + root)
    if command:
        code, message = system.run(['arch-chroot', root, '/bin/sh', '-c', command])
        for line in message.splitlines():
            out(line)
    else:
        code = system.interactive(['arch-chroot', root])
    fs(env, log, 'umount', '-R', root)
    return code == 0
