"""_fs・_fs_try・_is_mounted(U.sh)。"""
import time

WAIT = 1.0  # 試すごとの待ち(秒、テストでは0)


class MountError(Exception):
    """_fs: マウントできなかった(U.shは_dieで終える)。"""


def cwd_in(point):
    """_cwd_in(): pointの中にいる(カレントディレクトリがある)プロセスの「PID(名前)」。"""
    import os
    found = []
    try:
        pids = [p for p in os.listdir('/proc') if p.isdigit()]
    except OSError:
        return found
    for pid in pids:
        try:
            cwd = os.readlink('/proc/%s/cwd' % pid)
            with open('/proc/%s/comm' % pid) as f:
                name = f.read().strip()
        except OSError:
            continue
        if cwd == point or cwd.startswith(point.rstrip('/') + '/'):
            found.append('%s(%s)' % (pid, name))
    return found


def is_mounted(system, target):
    """_is_mounted(): ブロックデバイスなら/proc/mountsのデバイスに、他はマウントポイントとして(ちょうど同じもの)。"""
    if system.isblock(target):
        return any(dev == target for dev, _, _, _ in system.mounts())
    return system.is_mountpoint(target)


def fs(env, log, op, *args, try_only=False, wait=None):
    """_fs(): mount・umount。argsの最後がマウントポイント。mountは、前にマウントしたもの(ループのイメージ等)が
    残っていれば外してから(外せなければそのまま使う)。2回まで試す。マウントできなければログに書いて、
    try_onlyならFalseを返し、でなければMountError(U.shの_die)。"""
    system = env.system
    point = args[-1] if args else ''
    if not point or point == '/opt/' or system.islink(point):
        return True
    if env.keep_mnt_debian and point == env.mnt_debian:
        return True
    if op == 'mount':
        if system.isdir(point):
            system.run(['umount', point])
        else:
            system.makedirs(point)
    message = ''
    for _ in range(2):
        if (op == 'mount') == is_mounted(system, point):
            break
        _, message = system.run([op] + list(args))
        if (WAIT if wait is None else wait):
            time.sleep(WAIT if wait is None else wait)
    if op == 'mount' and not is_mounted(system, point):
        log.add('%s %s: failed. %s' % (op, ' '.join(args), (message.splitlines() or [''])[0]))
        if try_only:
            return False
        raise MountError(point)
    return True
