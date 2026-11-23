"""U.shの始めの変数(ON_WSL・ON_RAMDISK・ON_UBUNTU・ON_TORAM・ON_NONE・MNT_DEBIAN・DEBIAN_ROOT_UUID・STRAGE等)。

設定(DEBIAN等、利用者が書き換える所)は、今のU.sh(/opt/U.sh)の「readonly 名前=値」から読む(U.shと同じ値を使う)。
STRAGE・UUIDは/opt/U.opt(-aが書く)から。
"""
import os
import re
import shlex

U_SH = '/opt/U.sh'
U_OPT = '/opt/U.opt'


def shell_value(text, name):
    """「readonly 名前=値」「名前=値」の値(引用符は外す、行末の注釈は除く)。無ければNone。"""
    if text is None:
        return None
    value = None
    for line in text.splitlines():
        m = re.match(r'^\s*(?:readonly\s+)?' + re.escape(name) + r'=(.*)$', line)
        if not m:
            continue
        v = m.group(1)
        if v.startswith('"'):
            end = v.find('"', 1)
            v = v[1:end] if end > 0 else v[1:]
        elif v.startswith("'"):
            end = v.find("'", 1)
            v = v[1:end] if end > 0 else v[1:]
        else:
            v = re.split(r'\s', v, maxsplit=1)[0]
        value = v
    return value


def assignments(text):
    """/opt/U.optの行の「名前=値」(1行に幾つでも、-aは「STRAGE=/mnt/ntfs UUID=〜」と1行に書く)。後の行が勝つ。"""
    values = {}
    for line in (text or '').splitlines():
        try:
            words = shlex.split(line, comments=True)
        except ValueError:
            continue
        for w in words:
            m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)=(.*)$', w)
            if m:
                values[m.group(1)] = m.group(2)
    return values


# U.shの設定の部分(関数より前、「readonly RESCUE_LIST RESCUE_LIST_NOREC 〜」の行まで)を、bashに読ませて取る値
# (UbuntuだけJP_LIST等を上書きする等、U.shの書き方のままの値になる)
CONFIG_END = '^readonly RESCUE_LIST RESCUE_LIST_NOREC'
CONFIG_NAMES = ('DEBIAN USR ROOT_GB EXTRA_GB SETUP_OPT RESCUE_DEV_PURGE IMG_EXCLUDE RAMDISK_MARGIN_KB WORK_AREA SHM_LIST '
                'RESCUE_LIST RESCUE_LIST_NOREC RESCUE_LIST_X JP_LIST USILIC_TOOLS NOSWAP_KBD MNTS SMBFS GRUB_CUSTOM '
                'DST_STRAGE_FS BUILD BUILD_IMG_GB RESCUE_GB KERNEL_VERSION TEMPLATE_MB LINUX_CONFIG PURGE_LIST CONFIG_NONE '
                'LINUX_SOURCE TEMPLATE_BASE BASE_LIST STD_LIST NO_SNAP NETPLAN_NM DESKTOP_LIST DESKTOP_APT_OPT '
                'NETWORK_MANAGER_CONF DISPLAY_MANAGER_LIST OPT_BUILD BUILD_LIST BAK_HOST').split()


def evaluate_config(system, path=U_SH):
    """U.shの設定の部分をbashで読み、{名前: 値}(読めなければ空)。"""
    script = ('eval "$(sed -n "1,/%s/p" %s)" >/dev/null 2>&1; for v in %s; do printf "%%s=%%s\\0" "$v" "${!v}"; done'
              % (CONFIG_END, path, ' '.join(CONFIG_NAMES)))
    code, text = system.run(['bash', '-c', script])
    values = {}
    if code == 0:
        for item in text.split('\0'):
            if '=' in item:
                k, v = item.split('=', 1)
                values[k] = v
    return values


class Env:
    def __init__(self, system, uname=None):
        self.system = system
        self.uname = uname if uname is not None else os.uname().release
        ush = system.read_text(U_SH)
        self.debian = shell_value(ush, 'DEBIAN') or 'debian'
        self.mnt_debian = '/mnt/' + self.debian
        self.fstab = system.read_text('/etc/fstab') or ''
        self.on_wsl = 'WSL' in self.uname
        self.on_ramdisk = any('remount-ro' in l and '/dev/ram' in l for l in self.fstab.splitlines())
        issue = system.read_text('/etc/issue') or ''
        self.on_ubuntu = any(l.strip() and 'Debian GNU/Linux' not in l for l in issue.splitlines())
        self.on_toram = system.output(['findmnt', '-no', 'FSTYPE', '/']).strip() == 'tmpfs'
        self.on_none = not self.on_toram and bool(re.search(r'(amd64|-generic)$', self.uname))
        self.keep_mnt_debian = not self.on_ramdisk and system.is_mountpoint(self.mnt_debian)
        if self.on_ramdisk:
            self.debian_root_uuid = self.fstab_uuid(self.mnt_debian)
        elif self.on_toram:
            self.debian_root_uuid = self.fstab_uuid('/')
        else:
            self.debian_root_uuid = system.output(['findmnt', '-no', 'UUID', '/']).strip()
        self.config = evaluate_config(system) if ush is not None else {}

        def conf(name, default):  # 利用者が書き換える設定(今のU.shをbashで読んだ値、無ければ行から、それも無ければ既定)
            if name in self.config:
                return self.config[name]
            v = shell_value(ush, name)
            return default if v is None else v
        self.conf = conf
        self.user = conf('USR', '')
        if not self.user:  # インストーラで作ったユーザー(/homeの最初の一般ユーザー)
            for line in (system.read_text('/etc/passwd') or '').splitlines():
                f = line.split(':')
                if len(f) >= 6 and f[2].isdigit() and 1000 <= int(f[2]) < 60000 and f[5].startswith('/home/'):
                    self.user = f[0]
                    break
        self.img_exclude = conf('IMG_EXCLUDE', 'home/*/git/usilic home/*/.cache').split()
        self.ramdisk_margin_kb = int(conf('RAMDISK_MARGIN_KB', '50000'))
        self.work_area = conf('WORK_AREA', 'tmpfs')
        self.shm_list = conf('SHM_LIST', 'home tmp var').split()
        self.numerator = 11
        self.rescue_gb = 8 if self.on_ubuntu else 4
        m = re.search(r'\+([0-9]+)gb', self.uname)
        from_uname = int(m.group(1)) if m else 0
        if self.on_ramdisk:
            self.ramdisk_gb = from_uname
        elif self.on_ubuntu or self.on_none or self.on_wsl:
            self.ramdisk_gb = self.rescue_gb
        else:
            self.ramdisk_gb = from_uname
        self.debian_root_part = system.output(['blkid', '-U', self.debian_root_uuid]).strip() \
            if self.debian_root_uuid else ''
        self.tmp = '/tmp/ush.%d' % os.getpid()  # U.shの/tmp/〜.$TMP_LABEL(loop0・shm0・debian等をこの下に)
        self.ush = ush
        self.opt = assignments(system.read_text(U_OPT))
        self.strage = self.opt.get('STRAGE', '')   # U.shのmnt_strage
        self.uuid = self.opt.get('UUID', '')
        meminfo = system.read_text('/proc/meminfo') or ''
        m = re.search(r'^MemTotal:\s*(\d+)', meminfo, re.M)
        self.mem_total_gb = int(m.group(1)) // 1000 // 1000 if m else 0
        self.tmpfs_kb = int(m.group(1)) // 2 if m else 0  # U.shのTMPFS_KB
        self.ubuntu_build = 'BUILD_UBUNTU' if self.on_ubuntu else 'BUILD_DEBIAN'  # U.shのBUILD(カーネルのビルド用イメージ)

    def fstab_uuid(self, mount_point):
        """_fstab_uuid(): /etc/fstabで$1にマウントする区画のUUID(UUID=〜と/dev/disk/by-uuid/〜のどちらでも)。"""
        for line in self.fstab.splitlines():
            fields = line.split()
            if len(fields) >= 2 and not fields[0].startswith('#') and fields[1] == mount_point:
                v = fields[0]
                if v.startswith('UUID='):
                    return v[5:]
                if v.startswith('/dev/disk/by-uuid/'):
                    return v[len('/dev/disk/by-uuid/'):]
                return v
        return ''
