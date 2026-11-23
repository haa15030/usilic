"""_list_strage・_list_parts(U.sh): 区画の一覧と、選ぶ番号。"""
import os

from .fs import fs, is_mounted


class Part:
    def __init__(self, number, kind, device, uuid, free_gb, mount_point):
        self.number, self.kind, self.device, self.uuid = number, kind, device, uuid
        self.free_gb, self.mount_point = free_gb, mount_point

    def line(self):
        """U.shと同じ形(「 3)ntfs /dev/sda3 UUID (12 GB) [/mnt/ntfs]」)。"""
        mp = ' [%s]' % self.mount_point if self.mount_point else ''
        return '%2d)%s %s %s (%d GB)%s' % (self.number, self.kind, self.device, self.uuid, self.free_gb, mp)

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
