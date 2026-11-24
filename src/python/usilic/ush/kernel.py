"""-k(kernel_remake・_compile)と、-uが使うカーネル・RAMの土台(_update_base・_ram_entry・_correct_uuid・経過時間)。

カーネルはSTRAGEの$BUILD.img(ビルド用イメージ、ループで/opt/buildへ)の中でビルドする。ソースは/usr/srcへ(メモリが
あればtmpfs)。U.shと同じ手順。
"""
import re
import time

from .fs import fs


class Lap:
    """_lap_start・_lap: -uの区切りごとの経過時間を/opt/U_update.logへ(マニュアルの目安用、動きには関わらない)。"""

    def __init__(self, env):
        self.env = env
        self.start = self.previous = None

    @staticmethod
    def hms(seconds):
        return '%d:%02d:%02d' % (seconds // 3600, seconds % 3600 // 60, seconds % 60)

    def begin(self, option):
        system, env = self.env.system, self.env
        self.start = self.previous = int(time.time())
        cpu = ''
        for line in (system.read_text('/proc/cpuinfo') or '').splitlines():
            if line.startswith('model name'):
                cpu = line.split(':', 1)[1].strip()
                break
        strage = system.output(['findmnt', '-no', 'SOURCE,FSTYPE', '--target', env.strage or env.mnt_debian])
        system.append_text('/opt/U_update.log', '\n=== U.sh %s %s %s (%s, %s threads, %dGB, BUILD_IMG_GB=%s, STRAGE=%s) ===\n'
                           'time      +lap     total    step\n'
                           % (option, time.strftime('%Y-%m-%d %H:%M:%S'), system.output(['hostname']).strip(), cpu,
                              system.output(['nproc']).strip(), env.mem_total_gb, env.conf('BUILD_IMG_GB', ''),
                              (strage.splitlines() or [''])[0].strip()))

    def __call__(self, step, out):
        if self.start is None:
            return
        now = int(time.time())
        line = '%s  %s  %s  %s' % (time.strftime('%H:%M:%S'), self.hms(now - self.previous), self.hms(now - self.start), step)
        out(line)
        self.env.system.append_text('/opt/U_update.log', line + '\n')
        self.previous = now


def build_image(env):
    strage = env.strage or env.mnt_debian
    return '%s/opt/%s.img' % (strage, env.conf('BUILD', env.ubuntu_build))


def mount_build_img(env, log, out):
    """_mount_build_img(): ビルド用イメージ(STRAGEにだけ置く)が無いかBUILD_IMG_GBより小さければ作り直し、/opt/buildへ。"""
    system = env.system
    strage = env.strage or env.mnt_debian
    fs(env, log, 'mount', strage)
    image = build_image(env)
    want_kb = int(env.conf('BUILD_IMG_GB', '7') or 7) * 1000 * 1000
    size_kb = 0
    listing = system.output(['ls', '-l', image]).split()
    if len(listing) > 4 and listing[4].isdigit():
        size_kb = int(listing[4]) // 1000
    if size_kb < want_kb:  # 無いか、BUILD_IMG_GBより小さい(壊れた、またはBUILD_IMG_GBを増やした)
        system.run(['rm', '-f', image])
        out('truncate -s %d %s' % (want_kb * 1000, image))
        system.run(['truncate', '-s', str(want_kb * 1000), image])
        out('mkfs.ext4 -q ' + image)
        system.run(['mkfs.ext4', '-q', image])
    opt_build = env.conf('OPT_BUILD', '/opt/build')
    fs(env, log, 'umount', opt_build)
    out('mount -o loop %s %s' % (image, opt_build))
    fs(env, log, 'mount', '-o', 'loop', image, opt_build)


def compile_kernel(env, log, option, args, out, lap):
    """_compile(): args[0]が「.」なら今のカーネルの設定でRAMDISK_GBの、「-」なら配布のカーネルの設定で(args[1]が名前の
    後ろ)、数字ならそのGBのRAMDISKのカーネルを作る。args[1]が「/」ならmake oldconfigの問いは既定で、menuconfigはしない。"""
    system = env.system
    how = args[0] if args else ''
    skip = args[1] if len(args) > 1 else ''
    version_name = env.conf('KERNEL_VERSION', '')
    source = env.conf('LINUX_SOURCE', 'linux-source-' + version_name)
    new_kb, new_config = None, ''
    if how == '.':
        new_kb = env.ramdisk_gb * 1000 * 1000
        new_config = 'config-' + env.uname
        local = '+%dgb' % env.ramdisk_gb
    elif how == '-':
        images = sorted((l.split()[1] for l in system.output(['dpkg', '-l']).splitlines()
                         if len(l.split()) > 1 and 'linux-image' in l.split()[1] and version_name in l.split()[1]
                         and 'amd64' in l.split()[1]), reverse=True)
        new_config = images[0].replace('linux-image', 'config', 1) if images else ''
        local = '-' + skip if skip else ('-generic' if env.on_ubuntu else '-amd64')
    elif how.isdigit():
        new_kb = int(how) * 1000 * 1000
        local = '+%sgb' % how
    else:
        log.add("%s: Please type '. U -k (.)(-)(??)'[Enter]" % option)
        return False
    fs(env, log, 'mount', env.mnt_debian)
    mount_build_img(env, log, out)
    system.stream(['apt', 'update'], out)
    errors = env.tmp + '/build-dep.err'
    system.makedirs(env.tmp)
    command = 'apt %s -y' % env.conf('BUILD_LIST', 'build-dep linux')
    out(command)  # 長い(ビルドに要るものを入れる)ので、出たものをその場で出す(エラーは後で見るのでファイルへ)
    system.stream(['sh', '-c', '%s 2>%s' % (command, errors)], out)
    src = '/usr/src/' + source
    if not system.exists(src) or 'ok installed' not in system.output(['dpkg-query', '-W', '-f=${Status}', 'make']):
        system.stream(['apt', 'update'], out)
        system.stream(['apt', 'install', '-y', 'fakeroot', 'libncurses-dev'], out)
        error_text = system.read_text(errors) or ''
        purge = env.conf('PURGE_LIST', '').split()
        if error_text.strip() and purge:
            message = '%s: %s' % (option, error_text.strip())
            out(message)
            log.add(message)
            system.stream(['dpkg', '--purge'] + purge, out)
        if not system.exists(src):
            fs(env, log, 'umount', '/usr/src')
            installed = 'linux-source' in system.output(['dpkg', '-l'])
            if env.on_ubuntu:
                if env.mem_total_gb >= 8:
                    out('mount -t tmpfs -o size=2200000K /dev/shm /usr/src')
                    fs(env, log, 'mount', '-t', 'tmpfs', '-o', 'size=2200000K', '/dev/shm', '/usr/src')
                if installed:
                    system.stream(['dpkg', '--purge', 'linux-source', source], out)
                system.stream(['apt', 'install', '-y', 'linux-source'], out)
                out('Extracting %s.tar.bz2 ...' % source)
                system.shell('mv %s/%s.tar.bz2 .. && tar xfj ../%s.tar.bz2 && rm ../%s.tar.bz2'
                             % (source, source, source, source), cwd='/usr/src')
            elif env.on_ramdisk or env.on_wsl or (env.on_none and env.mem_total_gb >= 8):
                out('mount -t tmpfs -o size=2200000K /dev/shm /usr/src')  # Debian 13(6.12)では1600000Kでは不足
                fs(env, log, 'mount', '-t', 'tmpfs', '-o', 'size=2200000K', '/dev/shm', '/usr/src')
                if installed:
                    system.stream(['dpkg', '--purge', 'linux-source', source, 'linux-config-' + version_name], out)
                system.stream(['apt', 'install', '-y', 'linux-source'], out)
                config = env.conf('LINUX_CONFIG', 'linux-config-' + version_name)
                none = env.conf('CONFIG_NONE', 'config.amd64_none_amd64')
                out('Extracting %s.tar.xz ...' % source)
                system.shell('xzcat %s.tar.xz | tar xf - && xzcat %s/%s.xz > CONFIG_NONE && rm %s.tar.xz %s/%s.xz'
                             % (source, config, none, source, config, none), cwd='/usr/src')
        out(system.output(['df']).rstrip('\n'))
        lap('%s -> /usr/src' % source, out)
    if not system.exists(src + '/.config'):
        if new_config:
            out('cp %s/boot/%s .config' % (env.mnt_debian, new_config))
            system.run(['cp', '%s/boot/%s' % (env.mnt_debian, new_config), src + '/.config'])
        else:
            if env.on_ubuntu:
                system.run(['cp', '%s/boot/%s' % (env.mnt_debian, env.conf('LINUX_CONFIG', '')), src + '/.config'])
            else:
                out('cp ../CONFIG_NONE .config')
                system.run(['cp', '/usr/src/CONFIG_NONE', src + '/.config'])
            _edit_config(system, src + '/.config', [
                ('CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y', '# CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT is not set'),
                ('CONFIG_BLK_DEV_RAM=m', 'CONFIG_BLK_DEV_RAM=y'), ('CONFIG_EXT4_FS=m', 'CONFIG_EXT4_FS=y')])
        if new_kb:
            text = system.read_text(src + '/.config') or ''
            system.write_text(src + '/.config', re.sub(r'^CONFIG_BLK_DEV_RAM_SIZE=.*$',
                                                       'CONFIG_BLK_DEV_RAM_SIZE=%d' % new_kb, text, flags=re.M))
        else:
            _edit_config(system, src + '/.config', [
                ('CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y', '# CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT is not set')])
        if env.on_ubuntu:
            _edit_config(system, src + '/.config', [
                ('CONFIG_INPUT_EVBUG=m', 'CONFIG_INPUT_EVBUG=n'), ('debian/canonical-certs.pem', ''),
                ('debian/canonical-revoked-certs.pem', ''), ('CONFIG_DEBUG_INFO_BTF=y', 'CONFIG_DEBUG_INFO_BTF=n'),
                ('CONFIG_DEBUG_INFO_DWARF5=y', 'CONFIG_DEBUG_INFO_DWARF5=n')])
        if skip == '/':  # -uの途中: 新しい設定の問い(make oldconfig)は既定で、menuconfigはしない
            out('make oldconfig')
            system.shell('yes "" | make oldconfig', cwd=src)
        else:
            if how != '.':
                out('make oldconfig')
                system.interactive(['make', 'oldconfig'], cwd=src)
            out('make menuconfig')
            system.interactive(['make', 'menuconfig'], cwd=src)
        out('make mrproper')
        system.stream(['sh', '-c', 'cp .config .. && make mrproper && cp ../.config .'], out, cwd=src)
    opt_build = env.conf('OPT_BUILD', '/opt/build')
    out('ln -sf %s /usr/src' % opt_build)
    system.run(['ln', '-sf', opt_build, '/usr/src'])
    system.run(['mv', '-fv', src + '/.config', '/usr/src/build'])
    for cmd in (['scripts/config', '--file', '../build/.config', '--disable', 'DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT',
                 '--disable', 'DEBUG_INFO_DWARF4', '--disable', 'DEBUG_INFO_DWARF5', '--enable', 'DEBUG_INFO_NONE'],
                # モジュールを1つずつ署名しない(MODULE_SIG_ALL)。MODULE_SIG(署名の確かめ)は残す
                ['scripts/config', '--file', '../build/.config', '--disable', 'MODULE_SIG_ALL']):
        out(' '.join(cmd))
        system.run(cmd, cwd=src)
    disable = env.conf('KERNEL_DISABLE', '').split()
    if disable:  # 今では使われないもの等を外す(/lib/modulesを小さく)
        out('scripts/config --file ../build/.config --disable ' + ' '.join(disable))
        cmd = ['scripts/config', '--file', '../build/.config']
        for name in disable:
            cmd += ['--disable', name]
        system.run(cmd, cwd=src)
    enable = env.conf('KERNEL_ENABLE', '').split()
    if enable:  # 足すもの(LEGACY_PTYS: 旧C版のSHELLはBSD形式の擬似端末を使う)
        out('scripts/config --file ../build/.config --enable ' + ' '.join(enable))
        cmd = ['scripts/config', '--file', '../build/.config']
        for name in enable:
            cmd += ['--enable', name]
        system.run(cmd, cwd=src)
    if env.on_ubuntu:  # Ubuntuの署名鍵(debian/canonical-*.pem)はソースに含まれないので常に空に
        system.run(['scripts/config', '--file', '../build/.config', '--set-str', 'SYSTEM_TRUSTED_KEYS', '',
                    '--set-str', 'SYSTEM_REVOCATION_KEYS', ''], cwd=src)
    for cmd in (['make', 'O=../build/', 'olddefconfig'], ['make', '-j9', 'O=../build/', 'LOCALVERSION=' + local],
                ['make', 'modules', '-j9', 'O=../build/', 'LOCALVERSION=' + local],
                ['make', 'bindeb-pkg', 'INSTALL_MOD_STRIP=1', 'DPKG_FLAGS=-d', 'O=../build/', 'LOCALVERSION=' + local]):
        out(' '.join(cmd))
        system.stream(cmd, out, cwd=src)
    lap('linux-image-*%s .deb' % local, out)
    if not system.islink(env.mnt_debian):
        out('mv /opt/*.deb /opt/linux-upstream* %s/opt' % env.mnt_debian)
        system.shell('mv /opt/*.deb /opt/linux-upstream* %s/opt' % env.mnt_debian)
    fs(env, log, 'umount', opt_build)
    fs(env, log, 'umount', env.mnt_debian)
    return True


def _edit_config(system, path, pairs):
    text = system.read_text(path) or ''
    for old, new in pairs:
        text = text.replace(old, new)
    system.write_text(path, text)


def kernel_remake(env, log, option, args, out, lap=None):
    """kernel_remake()(-k)。"""
    from .parts import install_if_missing
    if (env.on_none or env.on_ramdisk) and (env.strage or env.mnt_debian) == env.mnt_debian:  # $BUILD.imgを置く所
        log.add("%s: Strage-area needed ('. U -a')." % option)
        return False
    if env.on_wsl and args[:1] == ['.']:
        log.add("%s: Please type '. U -k ??(GB)'[Enter]" % option)
        return False
    install_if_missing(env, 'openssl', out)
    return compile_kernel(env, log, option, args, out, lap or (lambda step, out: None))


def correct_uuid(env, log, path, part_from, part_to, efi=None):
    """_correct_uuid(): ファイルpathの中の区画part_fromのUUIDをpart_toのUUIDへ。efiがあればpathをfstabとして/boot/efiの
    行を作り直す(STRAGE等の行は除く)。"""
    system = env.system
    uuid_from = system.output(['blkid', '-c', '/dev/null', '-s', 'UUID', '-o', 'value', part_from]).strip()
    uuid_to = system.output(['blkid', '-c', '/dev/null', '-s', 'UUID', '-o', 'value', part_to]).strip()
    if not uuid_from or not uuid_to:
        log.add('_correct_uuid: %s %s: UUID not found.' % (part_from, part_to))
        return False
    text = (system.read_text(path) or '').replace(uuid_from, uuid_to)
    if efi:
        lines = [l for l in text.splitlines()
                 if not any(m in l for m in ('/boot/efi', '/mnt/cifs', '/mnt/ext4', '/mnt/ntfs', '/mnt/exfat'))]
        efi_uuid = system.output(['blkid', '-c', '/dev/null', '-s', 'UUID', '-o', 'value', efi]).strip()
        text = ''.join(l + '\n' for l in lines) + 'UUID=%s /boot/efi vfat umask=0077 0 1\n' % efi_uuid
    system.write_text(path, text)  # 中身だけ書き換える(40_customの実行権を残す)
    return True


def ram_entry(env, module):
    """_ram_entry(): RAM-module-baseの起動項目を40_customへ(u_root=はrc.localがDEBIAN区画として使う)。"""
    system = env.system
    custom = system.read_text('/etc/grub.d/40_custom') or ''
    if "'RAM-%s-base'" % module in custom:
        return
    uuid = env.debian_root_uuid
    system.append_text('/etc/grub.d/40_custom', (
        "menuentry 'RAM-{m}-base' {{\n\tinsmod part_gpt\n\tinsmod part_msdos\n\tinsmod ext2\n"
        "\tsearch --no-floppy --fs-uuid --set=root {u}\n\techo\t'Loading base:vmlinuz-{m} from {u} ...'\n"
        "\tlinux\t/boot/vmlinuz-{m} root=/dev/ram0 ro u_root={u}\n\techo\t'Loading initial ramdisk ...'\n"
        "\tinitrd\t/opt/RAM-{m}-base.gz\n\tsavedefault\n}}\n").format(m=module, u=uuid))


def update_base_image(env, log, gb, out, lap):
    """_update_base(): gbのカーネルのRAMの土台(/opt/RAM-モジュール-base.gz)を、テンプレート(TEMPLATE-*-base.gz)に
    そのカーネルのモジュールとヘッダを足して作り、起動項目を足す。カーネルより新しい土台があれば作り直さない。"""
    system = env.system

    def newest(names):
        named = [(system.mtime(p) or 0, p) for p in names]
        return max(named)[1] if named else ''
    kernels = ['/opt/' + n for n in system.listdir('/opt') if 'linux-image' in n and '+%dgb' % gb in n]
    modules = [n for n in system.listdir('/lib/modules') if '+%dgb' % gb in n]
    kernel = newest(kernels)
    module = max(modules, key=lambda n: system.mtime('/lib/modules/' + n) or 0) if modules else ''
    base = 'RAM-%s-base' % module
    if system.exists('/opt/%s.gz' % base) and not newest(['/opt/%s.gz' % base] + ([kernel] if kernel else [])
                                                         ).startswith('/opt/linux'):
        ram_entry(env, module)  # カーネルより新しいbaseがあれば作り直さない
        return
    strage = env.strage or env.mnt_debian
    loop0, loop1, shm0 = env.tmp + '/loop0', env.tmp + '/loop1', env.tmp + '/shm0'
    template_kb = int(env.conf('TEMPLATE_MB', '2000') or 2000) * 1000
    fs(env, log, 'umount', shm0)
    out('mount -t tmpfs -o size=%dK /dev/shm %s' % (template_kb, shm0))
    fs(env, log, 'mount', '-t', 'tmpfs', '-o', 'size=%dK' % template_kb, '/dev/shm', shm0)
    template = '/opt/%s.gz' % env.conf('TEMPLATE_BASE', '')
    out('zcat %s > %s/IMG1.img' % (template, shm0))
    system.shell('zcat %s > %s/IMG1.img' % (template, shm0))
    fs(env, log, 'umount', loop1)
    fs(env, log, 'mount', '-o', 'loop', shm0 + '/IMG1.img', loop1)
    # このカーネルのモジュールとヘッダだけを写す
    system.shell('tar cf - /lib/modules/%s /usr/src/linux-headers-%s 2>/dev/null | tar xf - -C %s' % (module, module, loop1))
    img0 = strage + '/opt/IMG0.img'
    system.run(['rm', '-f', img0])
    out('truncate -s %d %s' % (gb * 1000 * 1000 * 1000, img0))
    system.run(['truncate', '-s', str(gb * 1000 * 1000 * 1000), img0])
    system.run(['mkfs.ext4', '-q', img0])
    fs(env, log, 'mount', '-o', 'loop', img0, loop0)
    system.shell('tar cf - * | tar xfp - -C ' + loop0, cwd=loop1)
    system.write_text(loop0 + '/etc/hostname', '%dGB-base\n' % gb)
    hosts = system.read_text(loop0 + '/etc/hosts') or ''
    system.write_text(loop0 + '/etc/hosts', hosts.replace(env.debian, '%dGB-base' % gb, 1))
    for point in (loop0, loop1, shm0):
        fs(env, log, 'umount', point)
    # gzipでなくxzで(GRUBはinitrdを丸ごとメモリに読むので、小さいほどいい。Ubuntuの1.13GBはUEFIで「Out of memory」、
    # BIOSで「the size of initrd is bigger than addr_max」になった)。カーネルは中身の形(xz)を見て展開する(RD_XZ)ので、
    # 名前は.gzのまま(GRUBの項目等を変えずに済む)。カーネルのxzはCRC64を確かめられないので--check=crc32
    from .parts import install_if_missing
    install_if_missing(env, 'xz-utils', out)
    out('xz -T0 --check=crc32 -c %s > /opt/%s.gz' % (img0, base))
    system.shell('xz -T0 --check=crc32 -c %s > /opt/%s.gz' % (img0, base))
    lap(base + '.gz', out)
    system.run(['rm', '-f', img0])
    out(system.output(['df', '/']).rstrip('\n'))
    ram_entry(env, module)
