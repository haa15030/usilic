"""U.shの始めの変数(ON_WSL・ON_RAMDISK・ON_UBUNTU・ON_TORAM・ON_NONE・MNT_DEBIAN・DEBIAN_ROOT_UUID・STRAGE等)。

設定(DEBIAN等、利用者が書き換える所)は、今のU.sh(/opt/U.sh)の「readonly 名前=値」から読む(U.shと同じ値を使う)。
STRAGE・UUIDは/opt/U.opt(-aが書く)から。
"""
import os
import re

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
        opt = system.read_text(U_OPT) or ''
        self.strage = shell_value(opt, 'STRAGE') or ''   # U.shのmnt_strage
        self.uuid = shell_value(opt, 'UUID') or ''

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
