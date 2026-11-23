"""-r(rc_local_only): RAMDISKで起動した時に、rc.localから動く。SHM_LISTをtmpfsへ、U.optのNEXT?=のホストのイメージを
展開し、DOT_FILES・ホスト名・サービスの初期化(_start_rc)、デスクトップがあればusilicを入れる。U.shと同じ手順。
起動の時なので、マウントできなくても止めない(イメージが見つからなければ?GB-baseのまま起動する)。
"""
from .desktop import dot_files, has_desktop, put_usilic
from .fs import fs
from .prepare import enabled_units


def start_rc(env, out, log):
    """_start_rc(): 展開したイメージのサービスをsystemdで初期化(BASEにもあるものは動いていれば再起動、イメージで
    足したものは一度も起動していなければ起動、ディスプレイマネージャは最後に)。"""
    system = env.system
    base_units = (system.read_text(env.mnt_debian + '/opt/rc_systemd.txt') or '').split()
    conf = env.conf('NETWORK_MANAGER_CONF', '/etc/NetworkManager/NetworkManager.conf')
    errors = []
    if not env.on_ubuntu and system.exists(conf):  # Debian: ifupdownからNetworkManagerへ
        out('systemctl stop networking.service')
        system.run(['systemctl', 'stop', 'networking.service'])
        interfaces = system.read_text('/etc/network/interfaces') or ''
        interfaces = interfaces.replace('allow-hotplug', '#allow-hotplug').replace('iface', '#iface')
        system.write_text('/etc/network/interfaces', interfaces.replace('#iface lo', 'iface lo').replace('##', '#'))
        text = system.read_text(conf) or ''
        if 'managed=false' in text:
            system.write_text(conf, text.replace('managed=false', 'managed=true'))
        system.run(['systemctl', 'enable', 'NetworkManager.service'])
    out('systemctl daemon-reload')
    system.run(['systemctl', 'daemon-reload'])  # 展開したイメージのユニットを読み込み直す
    skip = ('rc-local.service', 'display-manager.service')
    for unit in enabled_units(env):
        if unit in skip or unit.startswith(('gdm', 'lightdm', 'sddm', 'wdm', 'xdm')):
            continue  # 自分自身(rc.local)とディスプレイマネージャは除く
        if unit in base_units:  # BASEにもあるもの: 動いていれば再起動(設定がイメージ側に変わっているため)
            code, message = system.run(['systemctl', 'try-restart', '--no-block', unit])
        elif system.output(['systemctl', 'show', '-p', 'ExecMainStartTimestampMonotonic', '--value', unit]).strip() == '0':
            out('systemctl start ' + unit)  # イメージで足したもの: この起動の間に一度も起動していなければ
            code, message = system.run(['systemctl', 'start', '--no-block', unit])
        else:
            continue
        if code != 0 and message.strip():
            errors.append(message.strip())
    if system.exists(conf):
        out('systemctl start NetworkManager.service')
        system.run(['systemctl', 'start', '--no-block', 'NetworkManager.service'])
    # キーボードの設定(/etc/default/keyboard)をudevに読み直させてからXを起動する(udevが読んだのは土台のもの)
    out('udevadm trigger --subsystem-match=input --action=change')
    system.run(['udevadm', 'trigger', '--subsystem-match=input', '--action=change'])
    system.run(['udevadm', 'settle', '--timeout=10'])
    if system.islink('/etc/systemd/system/display-manager.service'):  # ディスプレイマネージャは最後に
        out('systemctl start display-manager.service')
        system.run(['systemctl', 'start', '--no-block', 'display-manager.service'])
    elif system.output(['systemctl', 'get-default']).strip() == 'graphical.target':
        out('systemctl start graphical.target')
        system.run(['systemctl', 'start', '--no-block', 'graphical.target'])
    for message in errors:
        out(message)
        log.add(message)


def rc_local_only(env, log, option, out):
    system = env.system
    if system.exists('/opt/BASE.u'):  # SSDの上では何もしない
        return
    system.run(['timedatectl', 'set-local-rtc', '0'])
    # RAMDISK(ram0)のext4はrootの予約(5%)を持たない。RAMDISKでは使い道が無い
    if system.isblock('/dev/ram0'):
        out('tune2fs -m 0 /dev/ram0')
        system.run(['tune2fs', '-m', '0', '/dev/ram0'])
    if env.shm_list and env.mem_total_gb >= env.rescue_gb:
        keep = env.tmp + '/shm'
        for d in env.shm_list:
            system.makedirs('%s/%s' % (keep, d))
            system.shell('mv /%s/* /%s/.* %s/%s 2>/dev/null' % (d, d, keep, d))
            # できなければ、元のディレクトリへ戻すだけ
            fs(env, log, 'mount', '-t', 'tmpfs', '-o', 'size=%dK' % env.tmpfs_kb, '/dev/shm', '/' + d, try_only=True)
            system.shell('mv %s/%s/.* %s/%s/* /%s 2>/dev/null' % (keep, d, keep, d, d))
    # 起動の時なので、マウントできなくても止めない(イメージが見つからず?GB-baseのまま起動する)
    fs(env, log, 'mount', env.mnt_debian, try_only=True)
    strage = env.strage or env.mnt_debian
    fs(env, log, 'mount', strage, try_only=True)
    system.run(['cp', env.mnt_debian + '/opt/BASE.u', '/opt'])
    gb = env.ramdisk_gb
    host = env.opt.get('NEXT%d' % gb, '') or '%dGB-base' % gb
    image = ''
    if not host.endswith('base'):  # rescueはDEBIAN区画だけ、それ以外はSTRAGEを先に探す
        if host == '%dGB-rescue' % gb:
            candidates = [env.mnt_debian]
        else:
            candidates = [strage, env.mnt_debian]
        for place in candidates:
            if system.exists('%s/opt/%s.img' % (place, host)):
                image = '%s/opt/%s.img' % (place, host)
                break
        if not image:
            message = '-r: %s.img: not exist (booted as %dGB-base).' % (host, gb)
            out(message)
            log.add(message)
            host = '%dGB-base' % gb
    if image:
        out('tar xfp %s -> /' % image)
        loop0 = env.tmp + '/loop0'
        if fs(env, log, 'mount', '-o', 'loop,ro', image, loop0, try_only=True):
            # 起動中のRAMDISKのもの(fstabはrc.localがu_root=で書いた)とtar自身は上書きしない
            system.shell('tar cf - --exclude=etc/fstab --exclude=etc/rc.local --exclude=etc/network/interfaces '
                         '--exclude=usr/bin/tar * | tar xfp - -C /', cwd=loop0)
            fs(env, log, 'umount', loop0)
        else:
            host = '%dGB-base' % gb
            image = ''
    dot_files(env, out)
    hosts = '127.0.0.1\tlocalhost\n127.0.1.1\t%s\n' % host
    bak = env.conf('BAK_HOST', '')
    old = (system.read_text('/etc/hosts') or '').splitlines()
    if bak:
        hosts += bak.replace(' ', '\t') + '\n'
        first = bak.split()[0]
        rest = [l for l in old if '127.0.0.1' not in l and '127.0.1.1' not in l and first not in l]
    else:
        rest = [l for l in old if '127.0.0.1' not in l and '127.0.1.1' not in l]
    system.write_text('/etc/hosts', hosts + ''.join(l + '\n' for l in rest))
    if image:
        system.write_text('/etc/hostname', host + '\n')  # U.optのNEXT?=の値をホスト名に
        system.run(['hostnamectl', 'set-hostname', host])
        start_rc(env, out, log)
    if has_desktop(env):  # デスクトップのあるホストには、usilicが無ければ入れる
        put_usilic(env, log, '-r', out)
