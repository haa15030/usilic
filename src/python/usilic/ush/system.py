"""外への働きかけ(コマンド・ファイル・マウントの状態)をまとめたもの。

本物はSystem。テストのFakeSystemは、区画(blkid)・マウント(/proc/mounts)・ファイルを手元の表で持ち、mount・umount・
ln・rm等のコマンドを`calls`に記録して、表を書き換える(実機に触れずに手順を確かめる)。
"""
import os
import subprocess


class System:
    def run(self, argv, input_text=None, cwd=None):
        """argvを動かし(終わるまで待つ)、(終わりの値, 標準出力+標準エラー出力)。動かせなければ(127, 理由)。"""
        try:
            result = subprocess.run(argv, input=input_text.encode() if input_text is not None else None,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=cwd)
        except OSError as e:
            return 127, str(e)
        return result.returncode, result.stdout.decode('utf-8', 'replace')

    def shell(self, command, cwd=None):
        """sh -c command(*の展開・パイプの要るもの、tarでイメージを作る等)。(終わりの値, 出力)。"""
        return self.run(['sh', '-c', command], cwd=cwd)

    def stream(self, argv, out, cwd=None):
        """argvを動かし、出力を1行ずつoutへ(apt-get・コンパイル等、長いもの)。終わりの値。"""
        try:
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=cwd)
        except OSError as e:
            out(str(e))
            return 127
        for raw in proc.stdout:
            out(raw.decode('utf-8', 'replace').rstrip('\n'))
        return proc.wait()

    def output(self, argv):
        """argvの標準出力(失敗しても、出たものを返す)。"""
        return self.run(argv)[1]

    def interactive(self, argv, cwd=None):
        """端末をそのまま渡して動かす(ページャー・make menuconfig等)。"""
        try:
            return subprocess.run(argv, cwd=cwd).returncode
        except OSError:
            return 127

    def mounts(self):
        """[(デバイス, マウントポイント, 種類, オプション)](/proc/mounts、空白は\\040等のまま)。"""
        out = []
        try:
            with open('/proc/mounts') as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 4:
                        out.append(tuple(fields[:4]))
        except OSError:
            pass
        return out

    def is_mountpoint(self, path):
        return os.path.ismount(path)

    def read_text(self, path):
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                return f.read()
        except OSError:
            return None

    def append_text(self, path, text):
        try:
            with open(path, 'a', encoding='utf-8') as f:
                f.write(text)
            return True
        except OSError:
            return False

    def write_text(self, path, text, mode=None):
        try:
            with open(path, 'w', encoding='utf-8') as f:
                f.write(text)
            if mode is not None:
                os.chmod(path, mode)
            return True
        except OSError:
            return False

    def mtime(self, path):
        try:
            return os.path.getmtime(path)
        except OSError:
            return None

    def exists(self, path):
        return os.path.exists(path)

    def isdir(self, path):
        return os.path.isdir(path)

    def islink(self, path):
        return os.path.islink(path)

    def isblock(self, path):
        import stat
        try:
            return stat.S_ISBLK(os.stat(path).st_mode)
        except OSError:
            return False

    def listdir(self, path):
        try:
            return sorted(os.listdir(path))
        except OSError:
            return []

    def realpath(self, path):
        return os.path.realpath(path)

    def makedirs(self, path):
        try:
            os.makedirs(path, exist_ok=True)
            return True
        except OSError:
            return False

    def rmdir(self, path):
        try:
            os.rmdir(path)
            return True
        except OSError:
            return False

    def unlink(self, path):
        try:
            os.unlink(path)
            return True
        except OSError:
            return False

    def symlink(self, target, path):
        try:
            os.symlink(target, path)
            return True
        except OSError:
            return False

    def euid(self):
        return os.geteuid()

    def sleep(self, seconds):
        """待つ(FakeSystemは待たずにFalse、待ち続けるループを抜ける)。"""
        import time
        time.sleep(seconds)
        return True


class FakeSystem(System):
    """テスト用。parts: {デバイス: {'TYPE':, 'UUID':, 'free_kb':, 'mountable': True/'ro'/False}}、
    mounted: {マウントポイント: デバイス}、files: {パス: 中身}、dirs: 有るディレクトリ、links: {パス: 行き先}。"""

    def __init__(self, parts=None, mounted=None, files=None, dirs=(), links=None, euid=0, packages=(), held=(),
                 hostname='8GB-base'):
        self.parts = dict(parts or {})
        self.mounted = dict(mounted or {})
        self.files = dict(files or {})
        self.dirs = set(dirs)
        self.links = dict(links or {})
        self.calls = []
        self._euid = euid
        self.packages = list(packages)   # 入っているパッケージ(dpkg --get-selections)
        self.held = set(held)            # dpkg --purgeで外れないもの(残す側が要している)
        self.hostname = hostname
        self.shell_results = {}          # sh -cの頭の文字列 -> (終わりの値, 出力)
        self.results = {}                # コマンド行(空白でつないだもの) -> (終わりの値, 出力)(先に見る)

    def euid(self):
        return self._euid

    def sleep(self, seconds):
        return False

    # --- コマンド
    def shell(self, command, cwd=None):
        self.calls.append(['sh', '-c', command] + (['(cwd=%s)' % cwd] if cwd else []))
        for prefix, (code, text) in self.shell_results.items():
            if command.startswith(prefix):
                return code, text
        return 0, ''

    def stream(self, argv, out, cwd=None):
        code, text = self.run(argv)
        for line in text.splitlines():
            out(line)
        return code

    def run(self, argv, input_text=None, cwd=None):
        self.calls.append(list(argv))
        while argv and (argv[0] == 'env' or '=' in argv[0]):  # env LC_ALL=C 〜
            argv = argv[1:]
        name = argv[0]
        if ' '.join(argv) in self.results:
            return self.results[' '.join(argv)]
        if name == 'hostname':
            return 0, self.hostname + '\n'
        if name == 'dpkg' and argv[1:2] == ['--get-selections']:
            return 0, ''.join('%s\t\t\t\tinstall\n' % p for p in self.packages)
        if name == 'dpkg' and argv[1:2] == ['--purge']:
            gone = [p for p in argv[2:] if p not in self.held]
            self.packages = [p for p in self.packages if p not in gone]
            return (1 if len(gone) < len(argv) - 2 else 0), ''.join(
                'dpkg: dependency problems prevent removal of %s:\n x depends on %s.\n' % (p, p)
                for p in argv[2:] if p in self.held)
        if name == 'apt-get' and argv[1:2] == ['install']:
            for p in argv[2:]:
                if not p.startswith('-') and p not in self.packages:
                    self.packages.append(p)
            return 0, ''
        if name == 'blkid':
            return self._blkid(argv[1:])
        if name == 'findmnt':
            return self._findmnt(argv[1:])
        if name == 'mount':
            return self._mount(argv[1:])
        if name == 'umount':
            target = argv[-1]
            if target in self.mounted:
                del self.mounted[target]
                return 0, ''
            return 32, 'umount: %s: not mounted.' % target
        if name == 'df':
            path = argv[-1].rstrip('/') or '/'
            under = [m for m in self.mounted if path == m or path.startswith(m.rstrip('/') + '/')]
            dev = self.mounted.get(max(under, key=len)) if under else None
            kb = self.parts.get(dev, {}).get('free_kb', 0)
            return 0, 'Filesystem 1K-blocks Used Available Use%% Mounted on\n%s 1 1 %d 1%% %s\n' % (dev, kb, path)
        if name == 'ls':
            return 0, ''.join(n + '\n' for n in self.listdir(argv[-1]))
        if name == 'dpkg-query':
            return 0, 'install ok installed'
        return 0, ''

    def _blkid(self, args):
        if args[:2] == ['-o', 'device'] and len(args) >= 4 and args[2] == '-t':
            want = args[3].split('=', 1)[1]
            return 0, ''.join(d + '\n' for d in sorted(self.parts) if self.parts[d]['TYPE'] == want)
        if args and args[0] == '-U':
            for d, info in self.parts.items():
                if info['UUID'] == args[1]:
                    return 0, d + '\n'
            return 2, ''
        dev = args[-1]
        if '-s' in args:
            key = args[args.index('-s') + 1]
            return 0, (self.parts.get(dev, {}).get(key, '') + '\n')
        return 0, ''

    def _findmnt(self, args):
        target = args[-1]
        fields = args[args.index('-no') + 1] if '-no' in args else 'TARGET'
        if fields == 'TARGET':  # デバイス -> マウントポイント
            return 0, ''.join(m + '\n' for m, d in sorted(self.mounted.items()) if d == target)
        dev = self.mounted.get(target)
        if dev is None:
            return 1, ''
        info = self.parts.get(dev, {})
        values = {'SOURCE': dev, 'FSTYPE': info.get('TYPE', ''), 'OPTIONS': info.get('options', 'rw,relatime'),
                  'UUID': info.get('UUID', '')}
        return 0, ' '.join(values[f] for f in fields.split(',')) + '\n'

    def _mount(self, args):
        target = args[-1]
        source = args[-2] if len(args) >= 2 else None
        options = args[args.index('-o') + 1] if '-o' in args else ''
        dev = None
        if ('-t' in args and args[args.index('-t') + 1] == 'tmpfs') or 'loop' in options.split(','):
            self.mounted[target] = source  # tmpfs・ループ(イメージ)
            self.dirs.add(target)
            return 0, ''
        if source and source.startswith('UUID='):
            for d, info in self.parts.items():
                if info['UUID'] == source[5:]:
                    dev = d
        elif source and source in self.parts:
            dev = source
        elif source is None or source == target:  # fstab(「fstab:マウントポイント」か、/etc/fstabのUUID=の行)
            dev = self.files.get('fstab:' + target)
            for line in (self.files.get('/etc/fstab') or '').splitlines():
                fields = line.split()
                if dev is None and len(fields) >= 2 and fields[1] == target and fields[0].startswith('UUID='):
                    dev = next((d for d, i in self.parts.items() if i['UUID'] == fields[0][5:]), None)
        if dev is None or dev not in self.parts:
            return 32, 'mount: %s: special device does not exist.' % (source or dev)
        mountable = self.parts.get(dev, {}).get('mountable', True)
        if mountable is False or (mountable == 'ro' and 'ro' not in options.split(',')):
            return 32, 'mount: %s: cannot mount %s read-write.' % (target, dev)
        self.mounted[target] = dev
        self.dirs.add(target)
        return 0, ''

    # --- 状態
    def mounts(self):
        return [(d, m, self.parts.get(d, {}).get('TYPE', ''), 'rw') for m, d in sorted(self.mounted.items())]

    def is_mountpoint(self, path):
        return path.rstrip('/') in self.mounted

    def read_text(self, path):
        return self.files.get(path)

    def append_text(self, path, text):
        self.calls.append(['append', path])
        self.files[path] = self.files.get(path, '') + text
        return True

    def write_text(self, path, text, mode=None):
        self.calls.append(['write', path])
        self.files[path] = text
        return True

    def mtime(self, path):
        return self.files.get('mtime:' + path)

    def exists(self, path):
        return path in self.files or path in self.dirs or path in self.links or path in self.mounted

    def isdir(self, path):
        return path in self.dirs or path in self.mounted

    def islink(self, path):
        return path in self.links

    def isblock(self, path):
        return path in self.parts

    def listdir(self, path):
        prefix = path.rstrip('/') + '/'
        names = set()
        for p in list(self.files) + list(self.dirs) + list(self.links) + list(self.mounted):
            if p.startswith(prefix) and p != prefix:
                names.add(p[len(prefix):].split('/')[0])
        return sorted(names)

    def realpath(self, path):
        return self.links.get(path, path)

    def makedirs(self, path):
        self.calls.append(['mkdir', '-p', path])
        self.dirs.add(path)
        return True

    def rmdir(self, path):
        if path in self.mounted:
            return False
        self.calls.append(['rmdir', path])
        self.dirs.discard(path)
        return True

    def unlink(self, path):
        self.calls.append(['rm', path])
        self.links.pop(path, None)
        self.files.pop(path, None)
        return True

    def symlink(self, target, path):
        self.calls.append(['ln', '-s', target, path])
        self.links[path] = target
        return True

    def interactive(self, argv, cwd=None):
        self.calls.append(list(argv))
        return 0
