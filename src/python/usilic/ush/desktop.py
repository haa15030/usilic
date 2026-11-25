"""-d(desktop_select)・-e(erase_base)と、それらが使うもの(ネット・Ubuntuのapt・usilicを入れる・DOT_FILES)。"""
import os
import re

from .fs import fs
from .packages import output_u, purge_to

NOREC_APT = ['env', 'DEBIAN_FRONTEND=noninteractive', 'apt-get', '-y', '-o', 'Dpkg::Options::=--force-confdef',
             '-o', 'Dpkg::Options::=--force-confold']


def on_line(env):
    """_on_line(): 今ネットにつながっているか(ip aのglobalなinet)。"""
    return any('inet ' in l and 'global' in l for l in env.system.output(['ip', 'a']).splitlines())


def net_restart(env, out):
    """_net_restart()。"""
    if env.on_ubuntu:
        out('netplan apply')
        env.system.run(['netplan', 'apply'])
    else:
        env.system.run(['systemctl', 'restart', 'networking.service'])
    env.system.sleep(3)


def wait_online(env, seconds=60):
    """rc.localはネットにつながる前に動くことがあるので、最大seconds秒待つ。"""
    for _ in range(seconds // 2):
        if on_line(env):
            return True
        if not env.system.sleep(2):
            return False
    return on_line(env)


def apt(env, args, out):
    """apt-get(出力を1行ずつ)。"""
    out('apt-get ' + ' '.join(args))
    return env.system.stream(['apt-get'] + args, out)


def dot_files(env, out):
    """DOT_FILES(U.shの設定の部分にある、利用者が書き換える関数)を、今の/opt/U.shのままbashで動かす。"""
    from .env import CONFIG_END, U_SH
    script = 'eval "$(sed -n "1,/%s/p" %s)" >/dev/null 2>&1; rm -f /tmp/dot_files.$TMP_LABEL; DOT_FILES' % (CONFIG_END, U_SH)
    code, text = env.system.run(['bash', '-c', script])
    for line in text.splitlines():
        out(line)
    return code == 0


def apt_sources_ubuntu(env, out):
    """_apt_sources_ubuntu(): deb822の形でuniverse・multiverseとdeb-srcを。"""
    system = env.system
    src = '/etc/apt/sources.list.d/ubuntu.sources'
    text = system.read_text(src)
    if text is None:  # debootstrapの直後は1行の形のsources.listだけ
        uri = suite = ''
        for line in (system.read_text('/etc/apt/sources.list') or '').splitlines():
            f = line.split()
            if f[:1] == ['deb'] and len(f) >= 3:
                uri, suite = f[1], f[2]
                break
        uri = uri or 'http://archive.ubuntu.com/ubuntu'
        if not suite:
            m = re.search(r'^VERSION_CODENAME=(.*)$', system.read_text('/etc/os-release') or '', re.M)
            suite = m.group(1).strip('"') if m else ''
        out('create %s (%s)' % (src, suite))
        block = ('Types: deb deb-src\nURIs: %s\nSuites: %s\nComponents: main restricted universe multiverse\n'
                 'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n')
        system.write_text(src, block % (uri, '%s %s-updates %s-backports' % (suite, suite, suite)) + '\n' +
                          block % ('http://security.ubuntu.com/ubuntu', suite + '-security'))
        if system.exists('/etc/apt/sources.list'):
            out('mv /etc/apt/sources.list /etc/apt/sources.list.U-bak')
            system.run(['mv', '/etc/apt/sources.list', '/etc/apt/sources.list.U-bak'])
        out('apt update -y')
        system.stream(['apt', 'update', '-y'], out)
    elif not re.search(r'^Types:.*deb-src', text, re.M) or not re.search(r'^Components:.*universe', text, re.M):
        out('sed -i %s (deb-src, universe multiverse)' % src)
        text = re.sub(r'^Types: deb$', 'Types: deb deb-src', text, flags=re.M)
        text = re.sub(r'^Components:.*$', 'Components: main restricted universe multiverse', text, flags=re.M)
        system.write_text(src, text)
        out('apt update -y')
        system.stream(['apt', 'update', '-y'], out)


def no_snap_ubuntu(env, out):
    """_no_snap_ubuntu(): snapdを入れず、FirefoxをMozillaのaptリポジトリから。"""
    system = env.system
    if not env.conf('NO_SNAP', 'y') or system.exists('/etc/apt/preferences.d/U-nosnap'):
        return
    out('create /etc/apt/preferences.d/U-nosnap, U-mozilla')
    system.write_text('/etc/apt/preferences.d/U-nosnap', 'Package: snapd\nPin: release a=*\nPin-Priority: -10\n')
    from .parts import install_if_missing
    install_if_missing(env, 'wget', out)
    system.run(['install', '-d', '-m', '0755', '/etc/apt/keyrings'])
    system.run(['wget', '-q', 'https://packages.mozilla.org/apt/repo-signing-key.gpg', '-O',
                '/etc/apt/keyrings/packages.mozilla.org.asc'])
    system.write_text('/etc/apt/sources.list.d/mozilla.list',
                      'deb [signed-by=/etc/apt/keyrings/packages.mozilla.org.asc] https://packages.mozilla.org/apt '
                      'mozilla main\n')
    system.write_text('/etc/apt/preferences.d/U-mozilla',
                      'Package: *\nPin: origin packages.mozilla.org\nPin-Priority: 1000\n')
    out('apt update -y')
    system.stream(['apt', 'update', '-y'], out)


def install_standard(env, out):
    """_install_standard(): 標準システムユーティリティ。"""
    if env.on_ubuntu:
        apt_sources_ubuntu(env, out)
        no_snap_ubuntu(env, out)
        apt(env, ['install', '-y'] + env.conf('STD_LIST', 'ubuntu-standard').split(), out)
    else:
        packages = env.system.output(['tasksel', '--task-packages', 'standard']).split()
        if packages:
            env.system.stream(NOREC_APT + ['install'] + packages, out)


def netplan_nm(env, on, out):
    """_netplan_nm(): Ubuntu: netplanのレンダラをNetworkManagerに(on)・networkdに戻す(off)。"""
    system = env.system
    nm = env.conf('NETPLAN_NM', '/etc/netplan/90-U-network-manager.yaml')
    conf = env.conf('NETWORK_MANAGER_CONF', '/etc/NetworkManager/NetworkManager.conf')
    if not system.isdir('/etc/netplan') or not system.output(['sh', '-c', 'command -v netplan']).strip():
        return
    # デスクトップの包み(ubuntu-desktop-minimal等)が/usr/lib/netplan/00-network-manager-all.yaml(renderer: NetworkManager)を
    # 入れるのに、NetworkManagerは推奨なので--no-install-recommendsでは入らない。そのままだとnetplanがnetworkd用の設定を
    # 書かず、誰もDHCPをしない(8GB-GNOMEでenp5s0にアドレスが付かず、sshもできなかった)
    if on and not system.exists(conf) and 'renderer: NetworkManager' in system.output(['netplan', 'get']):
        out('apt-get install -y network-manager (netplan: renderer: NetworkManager)')
        system.stream(['apt-get', 'install', '-y', 'network-manager'], out)
    if on and system.exists(conf):
        out('create %s (renderer: NetworkManager)' % nm)
        system.write_text(nm, 'network:\n  version: 2\n  renderer: NetworkManager\n', 0o600)
        system.run(['systemctl', 'disable', 'systemd-networkd.service', 'systemd-networkd-wait-online.service'])
        system.run(['systemctl', 'enable', 'NetworkManager.service'])
    elif not on and system.exists(nm) and not system.exists(conf):
        out('rm ' + nm)
        system.run(['rm', '-f', nm])
        system.run(['systemctl', 'enable', 'systemd-networkd.service'])
    system.run(['netplan', 'generate'])  # 反映は次の起動の時(インストール中の接続は切らない)


def desktop_label(task):
    """-dの一覧の括弧の中: 主のパッケージだけ(2026-11-25、直接のユーザー指摘: GNOMEの「,」で足した端末等まで並べると、
    中身が多いように見える)。「+」は推奨も入れる。"""
    label = task.lstrip('+').split(',')[0]
    return label + ' +recommends' if task.startswith('+') else label


def desktop_ubuntu(env, on, out):
    """_desktop_ubuntu(): デスクトップを入れた後の設定(on)・BASEへ戻した後の後始末(off)。"""
    system = env.system
    netplan_nm(env, on, out)
    if on:
        if not system.islink('/etc/systemd/system/display-manager.service'):
            for dm in ('lightdm', 'sddm', 'gdm', 'gdm3'):
                if system.output(['systemctl', 'list-unit-files', '--no-legend', dm + '.service']).strip():
                    out('systemctl enable --force ' + dm)
                    system.run(['systemctl', 'enable', '--force', dm])
                    break
        nvidia = env.conf('NVIDIA_DESKTOP', '').split()
        kernel_side = (env.conf('NVIDIA_LIST', '').split() or [''])[0]
        if nvidia and kernel_side and 'ok installed' in system.output(['dpkg-query', '-W', '-f=${Status}', kernel_side]):
            from .update import nvidia_gpu
            if nvidia_gpu(system):  # nvidiaの描画の側(GBM・EGL・Xorg用等。GNOMEは無いとログイン画面が出ない)
                out('apt-get install -y --no-install-recommends ' + ' '.join(nvidia))
                system.stream(['apt-get', 'install', '-y', '--no-install-recommends'] + nvidia, out)
        out('systemctl set-default graphical.target')
        system.run(['systemctl', 'set-default', 'graphical.target'])
    else:
        out('systemctl set-default multi-user.target')
        system.run(['systemctl', 'set-default', 'multi-user.target'])


def usilic_libs(env, log, option, binary, out):
    """_usilic_libs(): binaryに足りないライブラリ(lddのnot found)を、DEBIAN区画のdpkgの記録(/var/lib/dpkg/info/
    *.list)から引いて入れる。"""
    system = env.system
    packages, missing = [], []
    for line in system.output(['ldd', binary]).splitlines():
        if '=> not found' not in line:
            continue
        lib = line.split()[0]
        _, found = system.shell("grep -l '/%s$' %s/var/lib/dpkg/info/*.list 2>/dev/null | head -n1"
                                % (lib, env.mnt_debian))
        found = found.strip()
        if found:
            packages.append(os.path.basename(found)[:-len('.list')])
            continue
        # DEBIAN区画に無ければ(-uがBASE.uへ戻した時に外れる)名前から推して、aptにあれば
        # (libSDL2_ttf-2.0.so.0 -> libsdl2-ttf-2.0-0、libfoo.so.6 -> libfoo6)
        guess = re.sub(r'\.so\.([0-9]*)$', r'-\1', lib.lower().replace('_', '-'))
        guess = re.sub(r'^(.*[a-z])-([0-9]*)$', r'\1\2', guess)
        if system.run(['apt-cache', 'show', guess])[0] == 0:
            packages.append(guess)
        else:
            missing.append(lib)
    if missing:
        log.add('%s: %s: package not found.' % (option, ' '.join(missing)))
    if packages:
        if not on_line(env):
            net_restart(env, out)
        if apt(env, ['install', '-y'] + packages, out) != 0:
            apt(env, ['update', '-y'], out)
            apt(env, ['install', '-y'] + packages, out)
        system.run(['apt-get', 'clean'])


def has_desktop(env):
    """_has_desktop(): デスクトップ(X11かWaylandのセッション)が入っているか。"""
    system = env.system
    return any(n.endswith('.desktop') for d in ('/usr/share/xsessions', '/usr/share/wayland-sessions')
               for n in system.listdir(d))


def in_gui():
    """_in_gui(): デスクトップの中(の端末)から動いているか(祖先のプロセスのDISPLAY・WAYLAND_DISPLAY、sudoは消すので)。"""
    pid = os.getpid()
    while pid and pid > 1:
        try:
            with open('/proc/%d/environ' % pid, 'rb') as f:
                if any(v.startswith((b'DISPLAY=', b'WAYLAND_DISPLAY=')) for v in f.read().split(b'\0')):
                    return True
            with open('/proc/%d/status' % pid) as f:
                pid = int(next(l.split()[1] for l in f if l.startswith('PPid:')))
        except (OSError, StopIteration, ValueError):
            return False
    return False


def put_usilic(env, log, option, out):
    """_put_usilic(): DEBIAN区画でビルドしたusilic(~/.local)をRAMDISKのホームへ(前に入れて保存したものがあれば、
    そのまま)。ログインした時にusilic(-g)を出す。usilicが使うツール(USILIC_TOOLS)と足りないライブラリを入れる。"""
    system = env.system
    user = env.conf('USR', '') or env.user
    home, src = '/home/' + user, env.mnt_debian + '/home/' + user
    if not env.on_ramdisk or not system.is_mountpoint(env.mnt_debian) or not system.exists(src + '/.local/bin/usilic'):
        return
    files = ['.local/bin/usilic', '.local/lib/usilic', '.local/share/applications/usilic.desktop', '.local/usilic-web']
    if not system.exists(home + '/.local/bin/usilic'):
        present = [f for f in files if system.exists(src + '/' + f)]
        out('cd %s; tar cf - %s | tar xfp - -C %s' % (src, ' '.join(files), home))
        system.shell('tar cf - %s | tar xfp - -C %s' % (' '.join(present), home), cwd=src)
        # tarはファイルだけを写すので、途中のディレクトリ(.local・.local/bin・.local/share等)はrootで作られる
        # (2026-11-25、16GB-KDEでxfeが~/.localへ書けなかった)
        system.run(['chown', '-R', '%s:%s' % (user, user), home + '/.local'])
        line = 'PATH=$HOME/.local/bin:$PATH; export PATH'
        for rc in ('.profile', '.bashrc'):
            text = system.read_text(home + '/' + rc) or ''
            if line not in text.splitlines():
                system.append_text(home + '/' + rc, line + '\n')
                system.run(['chown', '%s:%s' % (user, user), home + '/' + rc])
        system.shell('command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database %s'
                     % (home + '/.local/share/applications'))
        log.add('%s: usilic -> %s/.local' % (option, home))
    # ログインした時にusilic(-g)を出す(デスクトップによって端末が出たり出なかったりするので、usilicは必ず)
    desktop = home + '/.local/share/applications/usilic.desktop'
    if system.exists(desktop):
        system.makedirs(home + '/.config/autostart')
        system.run(['cp', desktop, home + '/.config/autostart'])
        system.run(['chown', '%s:%s' % (user, user), home + '/.config'])  # 無ければmakedirsがrootで作る
        system.run(['chown', '-R', '%s:%s' % (user, user), home + '/.config/autostart'])
    tools = [t for t in env.conf('USILIC_TOOLS', '').split()
             if 'ok installed' not in system.output(['dpkg-query', '-W', '-f=${Status}', t])]
    libs = '=> not found' in system.output(['ldd', home + '/.local/bin/usilic'])
    if tools or libs:
        wait_online(env)
    if tools:
        log.add('%s: usilic: %s -> install' % (option, ' '.join(tools)))
        if apt(env, ['install', '-y'] + tools, out) != 0:
            apt(env, ['update', '-y'], out)
            apt(env, ['install', '-y'] + tools, out)
        system.run(['apt-get', 'clean'])
    if libs:
        log.add('%s: usilic: missing libraries -> install' % option)
        usilic_libs(env, log, option, home + '/.local/bin/usilic', out)


def desktop_list(env):
    """[(パッケージ, 名前)]: DebianはtaskselのX-desktop、UbuntuはDESKTOP_LIST(パッケージ:名前)。"""
    if env.on_ubuntu:
        return [tuple(i.split(':', 1)) for i in env.conf('DESKTOP_LIST', '').split() if ':' in i]
    items = []
    for line in env.system.output(['env', 'LANG=C', 'tasksel', '--list-tasks']).splitlines():
        head, _, desc = line.partition('\t')
        words = head.split()
        if len(words) >= 2 and re.search(r'.-desktop$', words[1]):
            items.append((words[1], re.sub(r'[^A-Za-z0-9]+', '-', desc)))
    return items


def desktop_select(env, log, option, ask, out):
    """-d(desktop_select()): ?GB-base(とデスクトップのホスト)の上で、デスクトップを選んで(幾つでも)JP_LIST(日本語環境)と
    一緒に入れ、STRAGEの?GB-名前.imgへ保存する。2つ目からは日本語環境だけの状態(JP.u)へ戻してから。デスクトップの
    ホストでは、先にBASE.uへ戻す(テキストのコンソールかsshから)。最後に、最後に入れたものをその場で始めるか聞く。"""
    from .save import save_host
    system = env.system
    gb = env.conf('EXTRA_GB', '') or str(env.rescue_gb * 2)
    gb = int(gb) if gb.isdigit() else env.rescue_gb * 2
    hostname = system.output(['hostname']).strip()
    if not env.on_ramdisk or 'GB-' not in hostname or env.ramdisk_gb < gb:
        log.add("%s: Please boot %dGB-base (RAM-*+%dgb-base, '. U -n'; %dGB is too small for a desktop)."
                % (option, gb, gb, env.rescue_gb))
        return
    origin = ''
    if not hostname.endswith('GB-base'):  # デスクトップ等のホスト: BASE.uへ戻してから入れる(起動し直さなくていい)
        if in_gui():
            log.add("%s: On %s: log in on a text console (Ctrl+Alt+F3) and type '. U -d' there." % (option, hostname))
            return
        if not system.exists('/opt/BASE.u'):
            log.add('%s: /opt/BASE.u: not exist.' % option)
            return
        origin = hostname
    fs(env, log, 'mount', env.mnt_debian)
    strage = env.strage or env.mnt_debian
    fs(env, log, 'mount', strage)
    if strage == env.mnt_debian:
        log.add("%s: Strage-area needed ('. U -a')." % option)
        return
    items = desktop_list(env)
    if not items:
        log.add('%s: No desktop list.' % option)
        return
    out(' No)NAME (package) [image already in STRAGE]')
    for i, (task, name) in enumerate(items, 1):
        image = '%s/%dGB-%s.img' % (env.opt_dir(strage), env.ramdisk_gb, name)
        shown = ' [%dGB-%s.img]' % (env.ramdisk_gb, name) if system.exists(image) else ''
        out(' %d)%s (%s)%s' % (i, name, desktop_label(task), shown))
    out('  )<EXIT>')
    chosen = []
    for word in ask("Input No (e.g. '2' or '2 5'):").split():
        if not word.isdigit() or not 1 <= int(word) <= len(items):
            log.add('%s: %s: Invalid No.' % (option, word))
            return
        if items[int(word) - 1] not in chosen:
            chosen.append(items[int(word) - 1])
    if not chosen:
        return
    out('')
    for task, name in chosen:
        image = '%s/%dGB-%s.img' % (env.opt_dir(strage), env.ramdisk_gb, name)
        out('-> %s %s' % (image, '(already exists: overwrite)' if system.exists(image) else '(new)'))
    if origin:
        out('(%s -> BASE.u: the desktop stops now)' % origin)
    if ask('Install and save? (Y/N): ', 'N').strip() not in ('Y', 'y'):
        return
    noninteractive = NOREC_APT
    if origin:
        out('systemctl isolate multi-user.target')
        system.run(['systemctl', 'isolate', 'multi-user.target'])  # ディスプレイマネージャとデスクトップを止める
        purge_to(env, log, 'BASE.u', out)
        system.stream(noninteractive + ['autoremove', '--purge'], out)
        if env.on_ubuntu:
            desktop_ubuntu(env, False, out)
    if not on_line(env):
        net_restart(env, out)
    apt(env, ['update', '-y'], out)
    out('apt-get upgrade -y')
    system.stream(noninteractive + ['upgrade'], out)
    install_standard(env, out)
    jp = env.conf('JP_LIST', '').split()
    out('apt-get install -y ' + ' '.join(jp))
    system.stream(noninteractive + ['install'] + jp, out)
    output_u(env, log, ['JP.u'], out)  # デスクトップを入れる前(2つ目からはここまで戻してから入れる)
    for n, (task, name) in enumerate(chosen):
        if n > 0:
            purge_to(env, log, 'JP.u', out)
            system.stream(noninteractive + ['autoremove', '--purge'], out)
            if env.on_ubuntu:
                desktop_ubuntu(env, False, out)
            if not on_line(env):
                net_restart(env, out)
        out('HOST:%dGB-%s' % (env.ramdisk_gb, name))
        if env.on_ubuntu:
            option_list = env.conf('DESKTOP_APT_OPT', '--no-install-recommends').split()
            if task.startswith('+'):  # 「+」: 推奨も入れる
                option_list = []
            packages = task.lstrip('+').split(',')  # DESKTOP_LISTの「,」で足した包み(端末等)も
            out('apt-get install -y %s %s' % (' '.join(option_list), ' '.join(packages)))
            system.stream(noninteractive + ['install'] + option_list + packages, out)
            desktop_ubuntu(env, True, out)
        else:
            out('apt-get install -y task-' + task)
            system.stream(noninteractive + ['install', 'task-' + task], out)
        # 保存する前に、起動の時(-r)と同じ設定(swapcapsのkxkbrc・dconf等)とusilicを入れておく
        dot_files(env, out)
        put_usilic(env, log, option, out)
        save_host(env, log, option, [name, '.'], out)  # STRAGEの?GB-名前.img(あれば上書き)
    system.run(['rm', '-f', '/opt/JP.u', env.mnt_debian + '/opt/JP.u'])
    log.add('%s: %s' % (option, ' '.join('%s:%s' % c for c in chosen)))
    # 最後に入れたもの(今のRAMDISKの中身)は、そのままそのホストとして起動できる
    host = '%dGB-%s' % (env.ramdisk_gb, chosen[-1][1])
    yn = ask('Start %s now? (Y/n): ' % host, 'n').strip()
    if yn and yn not in ('Y', 'y'):
        log.add("%s: '. U -n' to boot." % option)
        return
    system.write_text('/etc/hostname', host + '\n')
    system.run(['hostnamectl', 'set-hostname', host])
    hosts = system.read_text('/etc/hosts') or ''
    system.write_text('/etc/hosts', re.sub(r'^127\.0\.1\.1\s.*$', '127.0.1.1\t' + host, hosts, flags=re.M))
    lines = [l for l in (system.read_text('/opt/U.opt') or '').splitlines()
             if not l.startswith('NEXT%d=' % env.ramdisk_gb)] + ['NEXT%d=%s' % (env.ramdisk_gb, host)]
    system.write_text('/opt/U.opt', ''.join(l + '\n' for l in lines))
    system.run(['cp', '/opt/U.opt', env.mnt_debian + '/opt'])
    if env.on_ubuntu and system.output(['sh', '-c', 'command -v netplan']).strip():
        out('netplan apply')  # レンダラがNetworkManagerになっていれば、ここで移す(networkdが持っていたIPアドレスが無くなった)
        system.run(['netplan', 'apply'])
    out('systemctl isolate graphical.target')
    system.run(['systemctl', 'isolate', 'graphical.target'])


def erase_base(env, log, option, ask, out):
    """-e(erase_base()): NONE-KERNELの上で、-uで作ったもの(RAMDISKのカーネル・BASEへ足したパッケージ・/optの中身等)を消し、
    DEBIAN.uの状態へ戻す(U.shと記録・grubの元は残す)。"""
    system = env.system
    if not env.on_none and not env.on_wsl:
        log.add('%s: Not on a NONE-KERNEL.' % option)
        return
    if not system.exists('/opt/BASE.u'):
        log.add("%s: BASE.u: No such file.  Please type 'sudo touch /opt/BASE.u'[Enter]" % option)
        return
    if ask('Delete base? (Y/N): ', 'N').strip() not in ('Y', 'y'):
        return
    version = env.conf('KERNEL_VERSION', '')
    system.run(['rm', '-rf', '/usr/src/build', '/usr/src/CONFIG_NONE', '/usr/src/linux-source-' + version,
                '/usr/src/linux-config-' + version])
    fs(env, log, 'umount', env.conf('OPT_BUILD', '/opt/build'))
    if env.strage:
        fs(env, log, 'umount', env.strage)
    if 'ok installed' in system.output(['dpkg-query', '-W', '-f=${Status}', 'light-locker']) and \
            'light-locker' in system.output(['ps', 'auxw']):
        system.run(['killall', 'light-locker'])
    images = [l.split()[1] for l in system.output(['dpkg', '-l']).splitlines()
              if l.split()[:1] == ['ii'] and len(l.split()) > 1 and l.split()[1].startswith('linux-image')
              and re.search(r'[+][0-9]+gb$', l.split()[1])]
    if images:
        system.stream(['dpkg', '--purge'] + images, out)

    def busy():
        ssd, shm = [], []
        for dev, point, _, _ in system.mounts():
            if dev.startswith('/dev/shm'):
                shm.append(point)
            elif dev.startswith('/dev') and dev != env.debian_root_part:
                ssd.append(point)
        return ssd + shm
    for point in busy():
        fs(env, log, 'umount', point)
    left = busy()
    if left:
        log.add('%s: %s: Busy!' % (option, ' '.join(left)))
    else:
        purge_to(env, log, 'DEBIAN.u', out)
        out('rm -f /opt/BASE.u')
        system.run(['rm', '-f', '/opt/BASE.u'])
        out('apt-get clean')
        system.run(['apt-get', 'clean'])
        user = env.conf('USR', '') or env.user
        out('rm -rf /home/%s/.cache' % user)
        system.run(['rm', '-rf', '/home/%s/.cache' % user])
        out('cp -fv /opt/grub.ORG /etc/default/grub')
        system.run(['cp', '-fv', '/opt/grub.ORG', '/etc/default/grub'])
        custom = (system.read_text('/etc/grub.d/40_custom') or '').splitlines(True)[:5]
        system.write_text('/etc/grub.d/40_custom', ''.join(custom))
        # カーネルの.deb等も消す(-uでビルドし直す。ビルド用イメージはSTRAGEに残る)。U.sh・記録・grubの元は残す
        keep = re.compile(r'(\.sh|^U\.opt|^DEBIAN\.u|^grub\.ORG|^U_update\.log)$')
        for name in system.listdir('/opt'):
            if not keep.search(name):
                system.run(['rm', '-rf', '/opt/' + name])
        system.run(['rm', '-f', '/usr/local/bin/U'])
        out('update-grub')
        system.stream(['update-grub'], out)
        output_u(env, log, ['.'], out)
        log.add("Please type 'sudo bash (or dash) /opt/U.sh'[Enter]")
    out(system.output(['ls', '-l', '/opt']).rstrip('\n'))
