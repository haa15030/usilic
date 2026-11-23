"""-o(_output_u): 入っているパッケージの記録(/opt/名前.u)と、記録へ戻す(-p)・記録へ足す(-i)。

記録の形はU.shと同じ(dpkg --get-selectionsから、カーネル・GRUB・shim・python3等を除き、「\\tinstall」を消した行)。
U.shが作った記録(BASE.u・DEBIAN.u等)をそのまま使える。比べる時はパッケージ名(行の最初の語)で。
"""
import re

# 記録に載せないもの(U.shの_barのgrep -v。python3はRESCUEが使うので、-p・-e・-dで外さない)
EXCLUDE = [r'deinstall$', 'efibootmgr', 'grub', 'linux-headers', 'linux-image', 'linux-modules', 'mokutil',
           'os-prober', 'shim-helpers-amd64-signed', 'shim-signed', 'shim-unsigned', r'^python3', r'^libpython3']


def current_lines(system):
    """今の記録の行(U.shの`echo $_bar | sh`)。"""
    out = []
    for line in system.output(['dpkg', '--get-selections']).splitlines():
        if not line.strip() or any(re.search(p, line) for p in EXCLUDE):
            continue
        out.append(line.replace('\tinstall', '', 1).replace('\tpurge', '', 1))
    return out


def names(lines):
    return [l.split()[0] for l in lines if l.split()]


def added(record_lines, now_lines):
    """記録に無くて今あるもの(U.shの`diff -ru 記録 今 | grep ^+`、並びは今の順)。"""
    have = set(names(record_lines))
    return [n for n in names(now_lines) if n not in have]


def write_record(env, log, name, lines, out, option='-o'):
    """/opt/名前を書く(RAMDISKならDEBIAN区画の/optへも)。BASE.u・DEBIAN.uは無い時だけ作る。"""
    from .fs import fs
    system = env.system
    if name in ('BASE.u', 'DEBIAN.u') and system.exists('/opt/' + name):
        log.add('%s: /opt/%s: already exist.' % (option, name))
        return
    if name == '.':
        name = system.output(['hostname']).strip() + '.u'
    elif not name.endswith('.u'):
        name += '.u'
    text = ''.join(l + '\n' for l in lines)
    system.write_text('/opt/' + name, text)
    out('-> /opt/' + name)
    if env.on_ramdisk:
        fs(env, log, 'mount', env.mnt_debian)
        system.write_text(env.mnt_debian + '/opt/' + name, text)
        out('-> %s/opt/%s' % (env.mnt_debian, name))


def purge_to(env, log, record_name, out):
    """記録へ戻す(-p): 記録に無いものをdpkg --purge。dpkg --purgeは依存関係を解かないので、残す側が要しているものは
    「dependency problems」で外さずに残る。外すもの同士の順番の都合で残ったものは、もう一度やれば外れるので3回まで
    (減らなくなったらやめる)。それでも残ったものは、dpkgの理由と一緒にログへ。外したかどうか(True)を返す。"""
    system = env.system
    record = (system.read_text('/opt/' + record_name) or '').splitlines()
    packages = added(record, current_lines(system))
    if not packages:
        return False
    previous, tries, reasons = None, 0, []
    while packages and packages != previous and tries < 3:
        out('dpkg --purge ' + ' '.join(packages))
        _, message = system.run(['env', 'LC_ALL=C', 'dpkg', '--purge'] + packages)
        for line in message.splitlines():
            out(line)
        reasons = message.splitlines()
        previous = packages
        packages = added(record, current_lines(system))
        tries += 1
    if packages:
        log.add('-o %s -p: not purged: %s' % (record_name, ' '.join(packages)))
        for line in sorted(set(l for l in reasons if ' depends on ' in l or 'error processing' in l))[:20]:
            log.add(line)
    return True


def install_from(env, log, record_name, out):
    """記録へ足す(-i): 記録にあって今無いものをapt-get install(全体で止まったら1つずつ)。足したかどうか。"""
    system = env.system
    record = (system.read_text('/opt/' + record_name) or '').splitlines()
    packages = added(current_lines(system), record)
    if not packages:
        return False
    out('apt-get install -y ' + ' '.join(packages))
    code, message = system.run(['apt-get', 'install', '-y'] + packages)
    for line in message.splitlines():
        out(line)
    if code != 0:
        for p in packages:
            if system.run(['apt-get', 'install', '-y', p])[0] != 0:
                log.add('%s: not installed.' % p)
    return True


def output_u(env, log, args, out):
    """_output_u(): 「-o 名前」記録を書く(「.」はホスト名.u)、「-o 名前 -p」戻す、「-o 名前 -i」足す。
    戻した・足した後は、その後の記録をcurrent.uに。"""
    system = env.system
    first = args[0] if args else ''
    second = args[1] if len(args) > 1 else ''
    if not first or first in ('-p', '-i') or (first == '.' and second in ('-p', '-i')):
        log.add("Please type '. U -o . or filename'[Enter]")
        return
    if second and not system.exists('/opt/' + first):
        log.add('-o : /opt/%s: not exist.' % first)
        return
    name = first
    if second == '-p':
        name = 'current.u' if purge_to(env, log, first, out) else '.'
    elif second == '-i':
        name = 'current.u' if install_from(env, log, first, out) else '.'
    elif second:
        log.add("%s: Please type '. U -o . or filename [-i)nstall or -p)urge]'[Enter]" % second)
        return
    write_record(env, log, name, current_lines(system), out)
