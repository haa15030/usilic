"""_list_strage・_list_parts(U.sh): 区画の一覧と、選ぶ番号。"""
import os

from .fs import fs, is_mounted


class Part:
    def __init__(self, number, kind, device, uuid, free_gb, mount_point, name=''):
        self.number, self.kind, self.device, self.uuid = number, kind, device, uuid
        self.free_gb, self.mount_point, self.name = free_gb, mount_point, name

    def line(self):
        """U.shと同じ形(「 3)ntfs /dev/sda3 UUID (12 GB) [/mnt/ntfs]」。ZFSはUUIDの後にプール名、空きが分からなければ?)。"""
        mp = ' [%s]' % self.mount_point if self.mount_point else ''
        name = ' ' + self.name if self.name else ''
        free = '?' if self.free_gb is None else '%d' % self.free_gb
        return '%2d)%s %s %s%s (%s GB)%s' % (self.number, self.kind, self.device, self.uuid, name, free, mp)

    def strage(self):
        """U.optの行(「STRAGE=/mnt/種類 UUID=〜」)。"""
        return 'STRAGE=/mnt/%s UUID=%s' % (self.kind, self.uuid)


def _free_gb(system, path):
    lines = system.output(['df', '-k', path]).splitlines()
    try:
        return int(lines[1].split()[3]) // 1000 // 1000
    except (IndexError, ValueError):
        return 0


def list_strage(env, log, kind, first, out, tmp):
    """_list_strage(): firstから番号を振って、kind(ntfs・exfat・ext4・vfat)の区画を出す。DEBIAN区画は出さない。
    マウント中の区画は外さずに空きを見る。マウントしていなければ空きを見るために仮にマウント(できなくても止めない、
    休止したWindowsのNTFS等は読むだけで)。[Part]を返す。"""
    if kind == 'zfs':
        return list_zfs(env, first, out)
    system = env.system
    parts, number = [], first
    devices = sorted(d for d in system.output(['blkid', '-o', 'device', '-t', 'TYPE=' + kind]).split()
                     if not d.startswith('/dev/ram'))
    for device in devices:
        uuid = system.output(['blkid', '-c', '/dev/null', '-s', 'UUID', '-o', 'value', device]).strip()
        if not uuid or uuid == env.debian_root_uuid:
            continue
        mount_point = (system.output(['findmnt', '-no', 'TARGET', device]).splitlines() or [''])[0].strip()
        if mount_point:
            free = _free_gb(system, mount_point)
        else:
            system.makedirs(tmp)
            system.run(['umount', tmp])
            if system.run(['mount', '-o', 'rw', 'UUID=' + uuid, tmp])[0] != 0:
                system.run(['mount', '-o', 'ro', 'UUID=' + uuid, tmp])
            if not is_mounted(system, tmp):
                out('  -)%s %s %s (can\'t mount)' % (kind, device, uuid))
                continue
            free = _free_gb(system, tmp)
            fs(env, log, 'umount', tmp, wait=0)
        part = Part(number, kind, device, uuid, free, mount_point)
        out(part.line())
        parts.append(part)
        number += 1
    return parts


def list_zfs(env, first, out):
    """_list_zfs(): list_strageのZFS(プールごとに1行)。読み込んでいないプールは読み込まない(FreeBSDのzroot等を
    一覧のためだけに読み込まない)ので、空きは?。選んだ時に読み込む。"""
    from . import zfs
    system = env.system
    devices = sorted(d for d in system.output(['blkid', '-o', 'device', '-t', 'TYPE=zfs_member']).split()
                     if not d.startswith('/dev/ram'))
    if not devices:
        return []
    if not zfs.ready(env, out):
        out('  -)zfs (no ZFS module: zfs-dkms)')
        return []
    parts, number, done = [], first, set()
    for device in devices:
        uuid = system.output(['blkid', '-c', '/dev/null', '-s', 'UUID', '-o', 'value', device]).strip()
        if not uuid or uuid in done:
            continue
        done.add(uuid)
        name, free, mount_point = zfs.pool(env, uuid), None, ''
        if name:
            data = zfs.dataset(env, uuid)
            try:
                free = int(system.output(['zfs', 'get', '-Hp', '-o', 'value', 'available', data]).strip()) \
                    // 1000 // 1000 // 1000
            except ValueError:
                pass
            mount_point = (system.output(['findmnt', '-no', 'TARGET', '-S', data]).splitlines() or [''])[0].strip()
        else:
            name = system.output(['blkid', '-c', '/dev/null', '-s', 'LABEL', '-o', 'value', device]).strip()
        part = Part(number, 'zfs', device, uuid, free, mount_point, name)
        out(part.line())
        parts.append(part)
        number += 1
    return parts


def install_if_missing(env, package, out):
    """_is_inst 名前 +: 入っていなければapt-get install。"""
    system = env.system
    if 'ok installed' in system.output(['dpkg-query', '-W', '-f=${Status}', package]):
        return
    out('apt-get install -y ' + package)
    system.stream(['apt-get', 'install', '-y', package], out)  # 長いので、出たものをその場で出す


NONE = '0'  # -aの「0)」(STRAGEを使わない)


def list_parts(env, log, kinds, ask, out, multi=False, tmp='/tmp/mount.ush'):
    """_list_parts(): kindsの区画を1)から一覧に出し、番号を選んでもらう(multiなら幾つでも、-fの時)。選んだ[Part]
    (空なら選ばない・終える)。1つだけ選ぶ時(-a)は0も選べて、[NONE]。範囲に無い番号はもう一度聞く(答えを前から
    渡された時は聞き直せないので終える)。"""
    parts = []
    for kind in kinds:
        if kind == 'ntfs':
            install_if_missing(env, 'ntfs-3g', out)
        elif kind == 'exfat':
            install_if_missing(env, 'exfat-fuse', out)
        parts += list_strage(env, log, kind, len(parts) + 1, out, tmp)
    out('  )<EXIT>')
    by_number = dict((p.number, p) for p in parts)
    while True:
        answer = ask("Input No (e.g. '2' or '2 5'):" if multi else 'Input No:')
        words = answer.split()
        low = 1 if multi else 0
        ok = all(w.isdigit() and (int(w) in by_number or (not multi and int(w) == 0)) for w in words) and \
            (multi or len(words) <= 1)
        if ok:
            chosen = []
            for w in words:
                item = NONE if int(w) == 0 else by_number[int(w)]
                if item not in chosen:
                    chosen.append(item)
            return chosen
        out('%s: Invalid No. (%d-%d)' % (answer, low, len(parts)))
        if ask.batch:
            return []


def default_tmp():
    return '/tmp/mount.ush.%d' % os.getpid()
