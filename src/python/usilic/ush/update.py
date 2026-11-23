"""-u(update_base): NONE-KERNEL(SSDの配布のカーネル)の上で、RAMDISKのカーネル・RAMの土台(RAM-*-base.gz)・
?GB-rescue.img(usilicを含む)までを作る。U.shと同じ手順。

最初の一歩(/opt/BASE.uの状態、python3が無い)はU.sh(旧シェル版)の-uで。テンプレートの中のrc.local等はdata/に
ある(U.shに埋め込んであったものと同じ。rc.localは、Python版があればそれで-rを動かす)。
"""
import os
import re

from .desktop import no_snap_ubuntu
from .fs import fs
from .kernel import Lap, compile_kernel, update_base_image
from .options import entry_titles
from .packages import output_u, purge_to
from .parts import install_if_missing
from .save import save_host, tmp_debian

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')


def data(name):
    with open(os.path.join(DATA, name), encoding='utf-8') as f:
        return f.read()


def get_usilic_repo(env, out):
    """_get_usilic_repo(): usilicのリポジトリ(~/git/usilic)を取る・新しくする。"""
    system, user = env.system, env.user
    home = '/home/' + user
    install_if_missing(env, 'git', out)
    if not system.isdir(home + '/git/usilic'):
        out('git clone https://github.com/haa15030/usilic.git %s/git/usilic' % home)
        return system.stream(['su', user, '-c', 'git clone https://github.com/haa15030/usilic.git ~/git/usilic'],
                             out) == 0
    out('git pull (%s/git/usilic)' % home)
    system.stream(['su', user, '-c', 'cd ~/git/usilic && git pull --ff-only'], out)
    return True


def install_usilic_py(env, log, out):
    """_install_usilic_py(): Python版のusilic(src/python)を/usr/local/lib/usilic-pyへ、usilic-pyで起動。RAM-*-base.gz
    を作る前に入れるので、どのホストにもある。"""
    system = env.system
    src = '/home/%s/git/usilic/src/python' % env.user
    install_if_missing(env, 'python3', out)
    if not system.exists(src + '/usilic.py'):
        log.add('-u: %s/usilic.py: not exist (usilic-py not installed).' % src)
        return
    if not system.exists(src + '/ush.py') or not system.exists(src + '/usilic/ush/data/rc.local'):
        # リポジトリ(GitHub)のものが古い(Python版のU.shがまだ無い)時は、入れ替えない(前は消してから写したので、
        # 今のもの・動いているこのush.pyのdata/も消え、-uが止まった)
        log.add('-u: %s: old (no ush.py), /usr/local/lib/usilic-py not replaced.' % src)
        return
    system.run(['rm', '-rf', '/usr/local/lib/usilic-py'])
    system.makedirs('/usr/local/lib/usilic-py')
    system.shell('tar cf - --exclude=__pycache__ usilic.py ush.py usilic | tar xf - -C /usr/local/lib/usilic-py', cwd=src)
    system.write_text('/usr/local/bin/usilic-py', '#!/bin/sh\nexec python3 /usr/local/lib/usilic-py/usilic.py "$@"\n',
                      0o755)
    out('usilic-py -> /usr/local/lib/usilic-py (RESCUE: ^J^R)')


def setup_usilic(env, out):
    """_setup_usilic(): usilicの環境(tools/setup-debian.sh)を作り、ビルドにだけ要るもの(RESCUE_DEV_PURGE)を外す。"""
    system = env.system
    home = '/home/' + env.user
    if not get_usilic_repo(env, out):
        return False
    if env.on_ubuntu:  # setup-debian.shが入れるfirefoxを、snapでなくMozillaのaptリポジトリから(snapのfirefoxは
        no_snap_ubuntu(env, out)  # snap storeから落とすので長く、RAMDISKにはsnapdも要る)
    if system.stream(['sh', home + '/git/usilic/tools/setup-debian.sh', env.user] +
                     env.conf('SETUP_OPT', '--gpu=none').split(), out) != 0:
        return False
    binary = home + '/.local/bin/usilic'
    purge = env.conf('RESCUE_DEV_PURGE', '').split()
    if purge and system.exists(binary):
        # usilicが使うライブラリ(libSDL2等)は、-devを外してもautoremoveで消えないよう手動で入れた扱いに
        libs = [l.split()[2] for l in system.output(['ldd', binary]).splitlines()
                if len(l.split()) > 2 and l.split()[1] == '=>' and l.split()[2].startswith('/')]
        packages = set()
        for lib in libs:
            found = system.output(['dpkg', '-S', lib]) or system.output(['dpkg', '-S', '/usr' + lib])
            for line in found.splitlines():
                if ':' in line:
                    packages.add(line.split(':')[0])
        out('apt-mark manual ' + ' '.join(sorted(packages)))
        system.run(['apt-mark', 'manual'] + sorted(packages))
        out('apt-get purge -y ' + ' '.join(purge))
        system.stream(['apt-get', 'purge', '-y'] + purge, out)
        system.stream(['apt-get', 'autoremove', '--purge', '-y'], out)
    return True


def toram_setup(env, log, option, out):
    """_toram_setup(): Ubuntu(dracut): GRUBの「TORAM」(起動オプションtoram)で起動した時だけ、ルートの区画をtmpfsへ
    コピーしてそこで動かす(dracutのモジュール99ramdisk-root)。"""
    system = env.system
    module = '/usr/lib/dracut/modules.d/99ramdisk-root'
    root_uuid = system.output(['findmnt', '-no', 'UUID', '/']).strip()
    if 'ok installed' not in system.output(['dpkg-query', '-W', '-f=${Status}', 'dracut']):
        log.add('%s: dracut is not installed (initramfs-tools?).' % option)
        return
    out('create ' + module)
    system.makedirs(module)
    system.write_text(module + '/module-setup.sh', data('module-setup.sh'), 0o755)
    system.write_text(module + '/ramdisk-root.service', data('ramdisk-root.service'), 0o644)
    system.write_text(module + '/ramdisk-root.sh', data('ramdisk-root.sh'), 0o755)
    if not system.exists('/etc/toram.skip'):
        system.write_text('/etc/toram.skip', 'swap.img\nlost+found\n')
    out('dracut --force --kver ' + env.uname)
    if system.stream(['dracut', '--force', '--kver', env.uname], out) != 0:
        return
    if "menuentry 'TORAM'" not in (system.read_text('/etc/grub.d/40_custom') or ''):
        system.append_text('/etc/grub.d/40_custom', (
            "menuentry 'TORAM' {{\n\tinsmod part_gpt\n\tinsmod part_msdos\n\tinsmod ext2\n"
            "\tsearch --no-floppy --fs-uuid --set=root {u}\n\techo\t'Loading vmlinuz (toram) ...'\n"
            "\tlinux\t/boot/vmlinuz root=UUID={u} ro toram\n\techo\t'Loading initial ramdisk ...'\n"
            "\tinitrd\t/boot/initrd.img\n}}\n").format(u=root_uuid))
        system.run(['chmod', '755', '/etc/grub.d/40_custom'])
    grub = system.read_text('/etc/default/grub') or ''
    if re.search(r'^GRUB_TIMEOUT_STYLE=hidden', grub, re.M):  # メニューを出す
        grub = re.sub(r'^GRUB_TIMEOUT_STYLE=hidden', 'GRUB_TIMEOUT_STYLE=menu', grub, flags=re.M)
        grub = re.sub(r'^GRUB_TIMEOUT=0$', 'GRUB_TIMEOUT=5', grub, flags=re.M)
        system.write_text('/etc/default/grub', grub)
    system.stream(['update-grub'], out)
    log.add("%s: Select 'TORAM' to run on tmpfs (from another GRUB: 'e' -> add 'toram' to the linux line -> Ctrl-x)."
            % option)


def _kernels(env, gb):
    """/optのlinux-image-*+gbgb*(新しい順)。"""
    system = env.system
    names = [n for n in system.listdir('/opt') if 'linux-image' in n and '+%dgb' % gb in n]
    return sorted(names, key=lambda n: system.mtime('/opt/' + n) or 0, reverse=True)


def _installed(env, gb):
    return any(l.startswith('ii  linux-image') and '+%dgb' % gb in l for l in env.system.output(['dpkg', '-l']).splitlines())


def purge_ubuntu(env, out):
    """_purge_ubuntu(): Ubuntuで、BASE.uを記録する前にRAMDISKでは要らないもの(UBUNTU_PURGE_LIST、「*」も使える)を外す。
    ubuntu-server-minimal等のメタが一緒に外れると、それが入れた残すもの(netplan.io・chrony・sudo-rs等)までautoremoveで
    消えるので、今残っているものは手で入れた扱いに。"""
    system = env.system
    patterns = env.conf('UBUNTU_PURGE_LIST', '').split()
    if not env.on_ubuntu or not patterns:
        return
    packages = [l.split()[1] for l in system.output(['dpkg-query', '-W', '-f=${db:Status-Abbrev} ${Package}\n']
                                                     + patterns).splitlines()
                if len(l.split()) == 2 and l.split()[0] == 'ii']
    if not packages:
        return
    out('apt-get purge -y ' + ' '.join(packages))
    system.stream(['apt-get', 'purge', '-y'] + packages, out)
    keep = [l.split()[1] for l in system.output(['apt-get', '-s', 'autoremove']).splitlines()
            if l.startswith('Remv ') and len(l.split()) > 1]
    if keep:
        out('apt-mark manual ' + ' '.join(keep))
        system.run(['apt-mark', 'manual'] + keep)


def make_template(env, log, option, out, lap):
    """RAMDISKの土台のテンプレート(/opt/TEMPLATE-*-base.gz、BASEの状態のルート、カーネルのモジュールは除く)。"""
    system = env.system
    purge_to(env, log, 'BASE.u', out)
    out(system.output(['free']).rstrip('\n'))
    loop1, shm0 = env.tmp + '/loop1', env.tmp + '/shm0'
    template_kb = int(env.conf('TEMPLATE_MB', '2000') or 2000) * 1000
    fs(env, log, 'umount', shm0)
    out('mount -t tmpfs -o size=%dK /dev/shm %s' % (template_kb, shm0))
    fs(env, log, 'mount', '-t', 'tmpfs', '-o', 'size=%dK' % template_kb, '/dev/shm', shm0)
    system.run(['truncate', '-s', str(template_kb * 1000), shm0 + '/IMG1.img'])
    system.run(['mkfs.ext4', '-q', shm0 + '/IMG1.img'])
    fs(env, log, 'umount', loop1)
    fs(env, log, 'mount', '-o', 'loop', shm0 + '/IMG1.img', loop1)
    system.run(['apt-get', 'clean'])
    # カーネルのモジュールとヘッダはtarで外す(/tmpへmvすると、tmpfsなので途中で止まった時にSSDから消える)
    command = ("tar cf - --exclude='usr/lib/modules/*' --exclude='usr/src/linux-headers*' [e-h]* root srv usr var "
               "| tar xfp - -C " + loop1)
    out(command)
    errors = env.tmp + '/tar.err'
    system.shell(command + ' 2>' + errors, cwd='/')
    error_text = (system.read_text(errors) or '').strip()
    if error_text:
        out('%s: %s' % (option, error_text))
        log.add('%s: %s' % (option, error_text))
        fs(env, log, 'umount', loop1)
        fs(env, log, 'umount', shm0)
        return False
    system.run(['rm', '-rf', loop1 + '/var/log/journal'])
    skeleton = tmp_debian(env, out)
    system.shell('tar cf - * | tar xfp - -C ' + loop1, cwd=skeleton)
    # rc.local(RAMDISKで動かす)。起動オプションu_root=のUUIDを$MNT_DEBIANの区画としてfstabへ書く
    out('create %s/etc/rc.local' % loop1)
    system.write_text(loop1 + '/etc/rc.local', data('rc.local').replace('@MNT_DEBIAN@', env.mnt_debian), 0o755)
    if env.on_ubuntu:  # rc.localを動かすユニット(systemdのrc.localの互換(generator)より優先される)
        system.write_text(loop1 + '/etc/systemd/system/rc-local.service', data('rc-local.service'))
        system.makedirs(loop1 + '/etc/systemd/system/multi-user.target.wants')
        system.run(['ln', '-sf', '../rc-local.service',
                    loop1 + '/etc/systemd/system/multi-user.target.wants/rc-local.service'])
    fs(env, log, 'umount', loop1)
    template = '/opt/%s.gz' % env.conf('TEMPLATE_BASE', '')
    out('gzip -c %s/IMG1.img > %s' % (shm0, template))
    system.shell('gzip -c %s/IMG1.img > %s' % (shm0, template))
    lap(os.path.basename(template), out)
    fs(env, log, 'umount', shm0)
    out(system.output(['df']).rstrip('\n'))
    return True


def update_base(env, log, option, ask, out):
    system = env.system
    if not env.on_none:
        log.add('%s: Not on a NONE-KERNEL.' % option)
        return False
    os.environ['DEBIAN_FRONTEND'] = 'noninteractive'
    lap = Lap(env)
    lap.begin(option)
    rescue_gb = env.rescue_gb
    strage = env.strage or env.mnt_debian
    if not system.exists('/opt/BASE.u'):
        purge_to(env, log, 'DEBIAN.u', out)
        purge_ubuntu(env, out)
        system.stream(['apt-get', 'install', '-y'] + env.conf('BASE_LIST', '').split(), out)  # 要るパッケージを足す
        output_u(env, log, ['BASE.u'], out)
        system.run(['chmod', '444', '/opt/BASE.u'])
    # Python版のusilic(RESCUE)とpython3を、RAM-*-base.gzを作る前に(取れなくても止めない)
    get_usilic_repo(env, out)
    install_usilic_py(env, log, out)
    system.stream(['apt-get', 'autoremove', '-y'], out)
    output_u(env, log, ['current.u'], out)
    head = ''.join((system.read_text('/etc/grub.d/40_custom') or '').splitlines(True)[:5])
    custom = env.conf('GRUB_CUSTOM', '')
    system.write_text('/etc/grub.d/40_custom', head + (custom + '\n' if custom else ''), 0o755)
    journald = system.read_text('/etc/systemd/journald.conf') or ''
    if 'Storage=volatile' not in journald:  # ログはメモリに
        system.write_text('/etc/systemd/journald.conf', journald.replace('#Storage=auto', 'Storage=volatile'))
    if not system.exists('/opt/grub.ORG'):
        out('cp /etc/default/grub /opt/grub.ORG')
        system.run(['cp', '/etc/default/grub', '/opt/grub.ORG'])
    # 起動する項目は名前で選ぶ(GRUB_DEFAULT=saved、-nと-mはgrubenvのsaved_entryを書き換える)
    first = False
    grub = system.read_text('/etc/default/grub') or ''
    if not re.search(r'^GRUB_DEFAULT=saved', grub, re.M):
        first = True
        grub = re.sub(r'^GRUB_DEFAULT=.*$', 'GRUB_DEFAULT=saved\nGRUB_SAVEDEFAULT=true', grub, flags=re.M)
        grub = grub.replace('GRUB_DISABLE_OS_PROBER=false', 'GRUB_DISABLE_OS_PROBER=true')
        grub = grub.replace('#GRUB_DISABLE_OS_PROBER=', 'GRUB_DISABLE_OS_PROBER=')
        grub = grub.replace('#GRUB_TERMINAL=console', 'GRUB_TERMINAL=console')
        system.write_text('/etc/default/grub', grub)
        uefi = system.read_text('/etc/grub.d/30_uefi-firmware')
        if uefi is not None:
            system.write_text('/etc/grub.d/30_uefi-firmware', uefi.replace('OS_INDICATIONS=', '#OS_INDICATIONS='), 0o755)
    fs(env, log, 'umount', '/usr/src')
    fs(env, log, 'mount', strage)
    if 'NONE-KERNEL' not in (system.read_text('/etc/grub.d/40_custom') or ''):
        uuid, name = env.debian_root_uuid, env.uname
        system.append_text('/etc/grub.d/40_custom', (
            "menuentry 'NONE-KERNEL' {{\n\tinsmod part_gpt\n\tinsmod part_msdos\n\tinsmod ext2\n"
            "\tsearch --no-floppy --fs-uuid --set=root {u}\n\techo\t'Loading base:vmlinuz-{k} from {u} ...'\n"
            "\tlinux\t/boot/vmlinuz-{k} root=UUID={u} ro\n\techo\t'Loading initial ramdisk ...'\n"
            "\tinitrd\t/boot/initrd.img-{k}\n\tsavedefault\n}}\n").format(u=uuid, k=name))
    if env.on_ubuntu:  # TORAM(dracutの時だけ。update-grubもする)
        toram_setup(env, log, option, out)
    if system.islink('/usr/src/build'):
        version = env.conf('KERNEL_VERSION', '')
        system.run(['rm', '-rf', '/usr/src/build', '/usr/src/CONFIG_NONE', '/usr/src/linux-source-' + version,
                    '/usr/src/linux-config-' + version])
    if not system.exists('/opt/%s.gz' % env.conf('TEMPLATE_BASE', '')):
        if not make_template(env, log, option, out, lap):
            return False
    # RAMDISK(RESCUE_GB)用のカーネル
    if not _kernels(env, rescue_gb):
        if strage == env.mnt_debian:  # $BUILD.imgはSTRAGEにだけ置く
            log.add("%s: To build linux-image-*+%dgb, Strage-area needed ('. U -a')." % (option, rescue_gb))
            return False
        compile_kernel(env, log, option, [str(rescue_gb), '/'], out, lap)
        if not _kernels(env, rescue_gb):
            log.add('%s: linux-image-*+%dgb: build failed.' % (option, rescue_gb))
            return False
    extra = env.conf('EXTRA_GB', '')
    if extra.isdigit() and int(extra) != rescue_gb and not _kernels(env, int(extra)):
        if strage == env.mnt_debian:
            log.add("%s: To build linux-image-*+%sgb, Strage-area needed ('. U -a')." % (option, extra))
        else:
            compile_kernel(env, log, option, [extra, '/'], out, lap)
            if not _kernels(env, int(extra)):
                log.add('%s: linux-image-*+%sgb: build failed.' % (option, extra))
                return False
    if not _installed(env, rescue_gb):
        out('dpkg -i ' + '/opt/' + _kernels(env, rescue_gb)[0])
        system.stream(['dpkg', '-i', '/opt/' + _kernels(env, rescue_gb)[0]], out)
    system.shell('rm -f /boot/initrd*+%dgb*' % rescue_gb)  # RAMの土台(RAM-*-base.gz)をinitrdとして使うので要らない
    update_base_image(env, log, rescue_gb, out, lap)
    for name in sorted(system.listdir('/opt')):
        m = re.match(r'^linux-image.*\+([0-9]+)gb_', name)
        if not m or int(m.group(1)) == rescue_gb:
            continue
        gb = int(m.group(1))
        if strage == env.mnt_debian:
            log.add("%s: To update 'RAM-*+%dgb-base.gz', please setup strage-area." % (option, gb))
            continue
        if not _installed(env, gb):
            out('dpkg -i ' + '/opt/' + name)
            system.stream(['dpkg', '-i', '/opt/' + name], out)
        update_base_image(env, log, gb, out, lap)
    rescue = '%dGB-rescue' % rescue_gb
    # ?GB-rescue: tools/setup-debian.shとRESCUE_LISTで作った環境(ビルドにだけ要るものは外す)を保存する。DEBIAN区画は
    # この環境のまま残す。止まらずにrescueとusilicまで作る(rescue-USBを作るのが本分)
    if not system.exists('/opt/%s.img' % rescue):
        out(system.output(['sh', '-c', 'ls -l /opt/RAM-* /opt/linux-image-*']).rstrip('\n'))
        if 'ok installed' in system.output(['dpkg-query', '-W', '-f=${Status}', 'libelf-dev']):
            purge_to(env, log, 'BASE.u', out)  # カーネルのビルドに使ったもの(build-dep等)が残っていれば外す
            system.stream(['apt-get', 'autoremove', '--purge', '-y'], out)
        system.stream(['apt-get', 'update', '-y'], out)
        system.stream(['apt-get', 'upgrade', '-y'], out)
        if not setup_usilic(env, out):
            log.add('%s: tools/setup-debian.sh failed.' % option)
            lap('(stopped) tools/setup-debian.sh failed', out)
            return False
        lap('usilic (tools/setup-debian.sh)', out)
        system.stream(['apt-get', 'install', '-y'] + env.conf('RESCUE_LIST', '').split(), out)
        norec = env.conf('RESCUE_LIST_NOREC', '').split()
        if norec:
            system.stream(['apt-get', 'install', '-y', '--no-install-recommends'] + norec, out)
        output_u(env, log, [rescue + '.u'], out)  # 何が入っているかの記録
        save_host(env, log, option, ['rescue'], out)
        lap(rescue + '.img', out)
        purge = env.conf('RESCUE_DEV_PURGE', '').split()
        if purge:  # SSDのrootでは、またusilicをビルドできるように戻す
            system.stream(['apt-get', 'install', '-y'] + purge, out)
        if not system.exists('%s/opt/%s.img' % (env.mnt_debian, rescue)):
            log.add('%s: %s.img: not created.' % (option, rescue))
            return False
    system.run(['rm', '-f', '/opt/current.u'])
    system.stream(['update-grub'], out)
    lap('done', out)
    entries = [t for t in entry_titles(system, '/') if re.match(r'^RAM-.*\+%dgb-base$' % rescue_gb, t)]
    rescue_entry = entries[-1] if entries else ''
    if rescue_entry:
        out("grub-set-default '%s'" % rescue_entry)
        system.run(['grub-set-default', rescue_entry])
    lines = [l for l in (system.read_text('/opt/U.opt') or '').splitlines() if not l.startswith('NEXT%d=' % rescue_gb)]
    system.write_text('/opt/U.opt', ''.join(l + '\n' for l in lines + ['NEXT%d=%s' % (rescue_gb, rescue)]))
    out(system.output(['ls', '-l', '/opt']).rstrip('\n'))
    if first and ask('Reboot now (%s)? (Y/n): ' % rescue_entry, 'n').strip() in ('', 'Y', 'y'):
        system.run(['reboot'])
    return True
