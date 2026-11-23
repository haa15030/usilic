"""移したオプション(U.shの同じ名前の関数)。"""
import os
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


def page_with_usilic(system, text):
    """_page(): usilicのページャー(このusilic.py)で(戻って読める、qで終える)。"""
    fd, path = tempfile.mkstemp(prefix='ush.', suffix='.txt')
    with os.fdopen(fd, 'w', encoding='utf-8') as f:
        f.write(text)
    usilic = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'usilic.py')
    if os.path.isfile(usilic):
        system.interactive([sys.executable, usilic, path])
    else:
        system.interactive(['less', path])
    os.unlink(path)
