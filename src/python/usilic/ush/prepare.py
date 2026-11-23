"""U.shのmainの始めの下準備(どのオプションでも、-oの他は)。U.shと同じ順。

chrootの中なら断る。/opt/U.opt。時刻(timesyncd)。aptの設定(Debianはnon-free-firmware・deb-src、Ubuntuはdeb822)。
NONE-KERNELのDebianはwgetを外しDEBIAN.uを作る。ネットにつながらなければ直す。BASEの有効なサービスの一覧。STRAGE
(/etc/fstabの行、区画が無ければ使わない)とSMBFS。SSDの上では/mnt/debianを/へのリンクに。-rの他はDOT_FILES。
(/usr/local/bin/Uは書かない。'. U'はU.shを動かすので、U.shが書く。)
"""
import re

from .desktop import apt_sources_ubuntu, dot_files, net_restart, on_line
from .fs import fs
from .packages import output_u
from .parts import install_if_missing


class InChroot(Exception):
    pass


def in_chroot(system):
    a = system.output(['stat', '-c', '%d:%i', '/']).strip()
    b = system.output(['stat', '-c', '%d:%i', '/proc/1/root/.']).strip()
    return bool(a) and a != b


def enabled_units(env):
    """_enabled_units(): 有効なサービス(init.dのスクリプトからsystemdが作ったもの=generatedも)。"""
    out = []
    for line in env.system.output(['systemctl', 'list-unit-files', '--type=service', '--state=enabled,generated',
                                   '--no-legend']).splitlines():
        if line.split() and not line.split()[0].endswith('@.service'):
            out.append(line.split()[0])
    return out


def prepare(env, log, option, out):
    system = env.system
    if in_chroot(system):
        out('U.sh: in chroot (kernel %s, hostname %s). Please run it outside the chroot.'
            % (env.uname, system.output(['hostname']).strip()))
        raise InChroot()
    if not system.exists('/opt/U.opt'):
        system.write_text('/opt/U.opt', 'STRAGE=""\nSH=bash\n')
    if not system.exists('/etc/systemd/timesyncd.conf') and \
            (not env.on_ubuntu or 'ok installed' not in system.output(['dpkg-query', '-W', '-f=${Status}', 'chrony'])):
        for cmd in (['apt', 'update', '-y'], ['apt', 'upgrade', '-y'], ['apt', 'install', '-y', 'systemd-timesyncd']):
            system.stream(cmd, out)
        conf = system.read_text('/etc/systemd/timesyncd.conf') or ''
        system.write_text('/etc/systemd/timesyncd.conf', conf.replace('#NTP=', 'NTP=ntp.nict.jp'))
        system.run(['systemctl', 'restart', 'systemd-timesyncd'])
        system.run(['timedatectl', 'set-local-rtc', '0'])
    if env.on_ubuntu:
        apt_sources_ubuntu(env, out)
    else:
        sources = system.read_text('/etc/apt/sources.list.d/debian.sources')
        if sources is not None:  # Debian 13(deb822の形)
            if not re.search(r'^Components:.*non-free-firmware', sources, re.M) or \
                    not re.search(r'^Types:.*deb-src', sources, re.M):
                out('sed -i /etc/apt/sources.list.d/debian.sources (non-free-firmware, deb-src)')
                sources = re.sub(r'^(Components:(?!.*non-free-firmware).*)$', r'\1 non-free-firmware', sources, flags=re.M)
                sources = re.sub(r'^Types: deb$', 'Types: deb deb-src', sources, flags=re.M)
                system.write_text('/etc/apt/sources.list.d/debian.sources', sources)
                system.stream(['apt', 'update', '-y'], out)
        else:
            sources = system.read_text('/etc/apt/sources.list') or ''
            if 'non-free-firmware' not in sources:
                sources = sources.replace(' main', ' main non-free-firmware')
                system.write_text('/etc/apt/sources.list', sources)
                system.stream(['apt', 'update', '-y'], out)
            if 'deb-src' not in sources:
                extra = ''.join('deb-src ' + l[4:] + '\n' for l in sources.splitlines() if l.startswith('deb '))
                system.write_text('/etc/apt/sources.list', sources + extra)
                system.stream(['apt', 'update', '-y'], out)
    if env.on_none and not env.on_ubuntu:
        installed = system.output(['dpkg', '-l'])
        if any(l.startswith('ii') and 'wget' in l for l in installed.splitlines()) and \
                'ok installed' not in system.output(['dpkg-query', '-W', '-f=${Status}', 'apt-listchanges']):
            system.run(['dpkg', '--purge', 'wget'])  # U.shを取った後は要らない
    if env.on_ubuntu and env.conf('GNU_COREUTILS', '') and \
            'ok installed' in system.output(['dpkg-query', '-W', '-f=${Status}', 'coreutils-from-uutils']):
        # mv・cp・ls等をGNU版に(Ubuntu 25.10からの既定はRustのuutilsで、端のケースがGNUと違う。DEBIAN.uを記録する前に)
        # uutilsは必須(Essential)なので、aptは自分では外さない(「競合」で止まる)。名前の後ろの「-」で外すと指定する
        out('apt-get install -y --allow-remove-essential coreutils-from-gnu coreutils-from-uutils-')
        system.stream(['apt-get', 'install', '-y', '--allow-remove-essential', 'coreutils-from-gnu',
                       'coreutils-from-uutils-'], out)
    if env.on_none and not system.exists('/opt/DEBIAN.u'):  # 入れたばかりの状態の記録(Ubuntuも。-uがここへ戻してからBASEを作る)
        output_u(env, log, ['DEBIAN.u'], out)
    _network(env, log, out)
    if not system.exists('/opt/rc_systemd.txt'):  # BASEの有効なサービスの一覧(start_rc()で使う)
        system.write_text('/opt/rc_systemd.txt', ''.join(u + '\n' for u in enabled_units(env)))
    _strage(env, log, out)
    ssd = env.on_wsl or env.on_none or (env.on_ubuntu and not env.on_ramdisk and not env.on_toram)
    if ssd and not system.islink(env.mnt_debian) and not env.keep_mnt_debian:
        if system.isdir(env.mnt_debian):
            system.rmdir(env.mnt_debian)
        out('ln -sf / ' + env.mnt_debian)
        system.run(['ln', '-sf', '/', env.mnt_debian])
    if option != '-r':
        dot_files(env, out)


def _network(env, log, out):
    system = env.system
    if '<BROADCAST' not in system.output(['ip', 'a']):
        log.add('Network unreachable.')
    if on_line(env):
        return
    if env.on_ubuntu:  # Ubuntuはnetplanで設定し直す
        net_restart(env, out)
        if not on_line(env):
            log.add('Network unreachable.')
        return
    conf = env.conf('NETWORK_MANAGER_CONF', '/etc/NetworkManager/NetworkManager.conf')
    interfaces = system.read_text('/etc/network/interfaces') or ''
    if not system.exists(conf) and re.search(r'^#allow-hotplug', interfaces, re.M):
        # NetworkManagerを入れた状態からBASE.uへ戻すと、NetworkManagerも外れるので、コメントにした設定を戻す
        system.write_text('/etc/network/interfaces',
                          interfaces.replace('#allow-hotplug', 'allow-hotplug').replace('#iface', 'iface'))
        system.run(['systemctl', 'start', 'networking.service'])
    if on_line(env):
        return
    # それでもつながらなければ、イーサネットのデバイスごとに設定し直す(USB等へ写したシステムを別の機械で起動した時等)
    devices = []
    for line in system.output(['ip', 'a']).splitlines():
        m = re.match(r'^[0-9]+: ([^:@]+)', line)
        if m and m.group(1) != 'lo':
            devices.append(m.group(1))
    for device in devices:
        head = ''.join((system.read_text('/etc/network/interfaces') or '').splitlines(True)[:10])
        system.write_text('/etc/network/interfaces', head + 'allow-hotplug %s\niface %s inet dhcp\n' % (device, device))
        out('ifdown %s; ifup %s' % (device, device))
        system.run(['ifdown', device])
        system.run(['ifup', device])
        if on_line(env):
            return
    log.add('Network unreachable.')


def _strage(env, log, out):
    """/etc/fstabのSTRAGEの行(STRAGE・UUIDの区画が無ければ、STRAGEを使わない)とSMBFS。"""
    system = env.system
    fstab = [l for l in (system.read_text('/etc/fstab') or '').splitlines()
             if not any(m in l for m in ('/mnt/cifs', '/mnt/ntfs', '/mnt/exfat', '/mnt/ext4'))]
    strage = env.strage
    smbfs = env.conf('SMBFS', '')
    if smbfs:
        install_if_missing(env, 'cifs-utils', out)
        words = smbfs.split()
        if len(words) >= 4 and fs(env, log, 'mount', '-t', 'cifs', '-o', words[3], words[0], words[1], try_only=True):
            strage = '/mnt/cifs'
            fstab.append(smbfs)
            fs(env, log, 'umount', '/mnt/cifs')
        else:
            strage = ''
    if strage and strage not in (env.mnt_debian, '/mnt/cifs'):
        if strage == '/mnt/ntfs':
            install_if_missing(env, 'ntfs-3g', out)
        elif strage == '/mnt/exfat':
            install_if_missing(env, 'exfat-fuse', out)
        if env.uuid and env.uuid in system.output(['blkid']):  # STRAGEの区画があるか
            fstab.append('UUID=%s %s %s rw,noauto\t    0 1' % (env.uuid, strage, strage.split('/')[2]))
            fs(env, log, 'umount', strage)
        else:
            strage = ''
    system.write_text('/etc/fstab', ''.join(l + '\n' for l in fstab), 0o644)
    system.run(['systemctl', 'daemon-reload'])
    env.strage = strage  # 空ならSTRAGEを使わない(DEBIAN区画)
