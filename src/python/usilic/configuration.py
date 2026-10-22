"""usilicrcの設定(C++のconfiguration.cppのConfiguration、CUIで使う節だけ)。

usilicrcは「行頭が見出し(ARCHIVE等)の行が節の始まり、節の中身はタブか空白で始める」形。'#'以降は注釈
(""で囲んだ中の'#'は字)。C++版と同じく、知らない見出しの所で読むのをやめる。GUIの節(FONT・SPEED等)と
MACRO・URL・EDITOR等は、読み飛ばすだけ(この版では使わない)。
"""
import os
import shutil
import sys

from .key_bindings import KeyBindings

SECTIONS = ('ARCHIVE', 'FILTER', 'LAUNCH', 'GREP', 'PROGRAM', 'EDITOR', 'BUFFER', 'CODE', 'LINE', 'WAIT', 'COPY',
            'MOVE', 'DELETE', 'SERIF', 'SANS-SERIF', 'SPEED', 'DRAG', 'ARROW', 'FONT', 'LINE-HEIGHT',
            'LETTER-SPACING', 'ICON-FONT', 'ICON-LINE-HEIGHT', 'ICON-LETTER-SPACING', 'URL', 'KEY', 'MACRO')

# ArchiveKind(configuration.hpp)
TAR, LHA, ZIP, RPM, DEB, RAR, SEVEN_ZIP = 'Tar', 'Lha', 'Zip', 'Rpm', 'Deb', 'Rar', 'SevenZip'


def seven_zip_command():
    """sevenZipCommand(): 7zが無く7zz(Homebrewのsevenzip)があればそちら。"""
    return '7zz' if shutil.which('7z') is None and shutil.which('7zz') else '7z'


# 組み込みのアーカイブ(usilicrcのARCHIVEの注釈の表と同じ)。(suffix, 種類, 一覧のコマンド, 前段の伸張)
_BUILTIN_ARCHIVES = [
    ('.tar.gz', TAR, ['tar', 'tvf', '-'], ['gzip', '-dc']), ('.tgz', TAR, ['tar', 'tvf', '-'], ['gzip', '-dc']),
    ('.taz', TAR, ['tar', 'tvf', '-'], ['gzip', '-dc']), ('.tar.z', TAR, ['tar', 'tvf', '-'], ['gzip', '-dc']),
    ('.tar.bz2', TAR, ['tar', 'tvf', '-'], ['bzip2', '-dc']), ('.tbz2', TAR, ['tar', 'tvf', '-'], ['bzip2', '-dc']),
    ('.tar.xz', TAR, ['tar', 'tvf', '-'], ['xz', '-dc']), ('.txz', TAR, ['tar', 'tvf', '-'], ['xz', '-dc']),
    ('.tar.zst', TAR, ['tar', 'tvf', '-'], ['zstd', '-dc']), ('.tzst', TAR, ['tar', 'tvf', '-'], ['zstd', '-dc']),
    ('.tar', TAR, ['tar', 'tvf'], None), ('.lzh', LHA, ['lha'], None),
    ('.zip', ZIP, ['unzip', '-l'], None), ('.jar', ZIP, ['unzip', '-l'], None),
    ('.rpm', RPM, ['rpm', '-qplv'], None), ('.deb', DEB, ['ar', 'tv'], None),
    ('.rar', RAR, ['lsar', '-l'], None), ('.7z', SEVEN_ZIP, [None, 'l'], None),
]

# 役目ごとのコマンド(ConfiguredRole、C++のConfiguration()の既定と同じ)
DEFAULT_ROLES = {
    'Ls': ['ls', '-alF'], 'Du': ['du', '-ak'], 'Cp': ['cp', '-vR'], 'Mv': ['mv', '-v'], 'Rm': ['rm', '-vr'],
    'Groff': ['env', 'GROFF_NO_SGR=1', 'groff', '-mandoc', '-Kutf8', '-Tutf8'], 'Man': ['man'],
    'Locate': ['locate'],
    'UpdateDb': ['/usr/libexec/locate.updatedb'] if sys.platform == 'darwin' else ['updatedb'],
}


def tokenize(line):
    """節の中身の1行を語に分ける。""で囲めば空白・'#'を含められる。'#'以降は注釈。"""
    tokens, current, quoted, have = [], [], False, False
    for ch in line:
        if quoted:
            if ch == '"':
                quoted = False
            else:
                current.append(ch)
            continue
        if ch == '"':
            quoted, have = True, True
        elif ch == '#':
            break
        elif ch in ' \t':
            if current or have:
                tokens.append(''.join(current))
            current, have = [], False
        else:
            current.append(ch)
    if current or have:
        tokens.append(''.join(current))
    return tokens


URL_MODES = ('Open', 'Pdf', 'Dump', 'Term', 'Copy', 'Look', 'Epub')


def default_url_handlers():
    """URL節が無い時(usilicrcのURLの注釈の通り。Pdf・Epubはスクリプトがある時だけ)。"""
    lib = os.path.join(os.path.expanduser('~'), '.local', 'lib', 'usilic')
    if sys.platform == 'darwin':
        out = [('Open', ['open'])]
    else:
        out = [('Open', ['xdg-open'])]
    if os.path.exists(os.path.join(lib, 'url2pdf')):
        out.append(('Pdf', [os.path.join(lib, 'url2pdf')]))
    out += [('Dump', ['w3m', '-dump']), ('Term', ['w3m'])]
    out.append(('Copy', ['pbcopy'] if sys.platform == 'darwin' else ['xclip', '-selection', 'clipboard']))
    if sys.platform == 'darwin':
        out.append(('Look', []))
    if os.path.exists(os.path.join(lib, 'narou2epub')):
        out.append(('Epub', [os.path.join(lib, 'narou2epub')]))
    return out


class Configuration:
    def __init__(self):
        self.roles = {k: list(v) for k, v in DEFAULT_ROLES.items()}
        self.archive_aliases = [('.epub', '.zip'), ('.cbz', '.zip')]  # .epubは組み込みで.zipの別名
        self.filters = []          # (suffix, argv)
        self.launches = []         # (suffix, argv)
        self.grep_examples = []    # EGREPの^Rの一覧の先頭に載せる正規表現
        self.programs = []
        self.buffer_megabytes = 64
        self.binary_codes = None   # CODE(Noneは既定: 0x20未満で、ここに無い字があればバイナリ)
        self.retention_count = 0   # LINE(0は無制限)
        self.key_bindings = KeyBindings()
        self.macros = None         # MACRO(Noneは節が無い: 組み込みの6つ)
        self.url_handlers = None   # URL(Noneは節が無い: 組み込み)
        self.editors = []          # EDITOR
        self.wait_centiseconds = 10  # WAIT(マクロの間隔、10msec単位)
        self.speed_values = []     # SPEED(慣性の段ごとの1ドットの待ち時間、usec)
        self.arrow_values = []     # ARROW(CUIの慣性の段、無ければSPEED)
        self.errors = []
        self.path = None

    # --- 読み込み ---
    def load_rc_text(self, text):
        """loadRcText()。知らない見出しの所でやめる(Falseを返す)。"""
        section, body = None, {}
        for number, line in enumerate(text.split('\n'), 1):
            if not line.strip() or line.lstrip().startswith('#'):
                if section == 'KEY':
                    body.setdefault(section, []).append(line)
                continue
            if line[0] in ' \t':
                if section is None:
                    self.errors.append('%d: data before any section' % number)
                    continue
                body.setdefault(section, []).append(line)
                continue
            head = line.split('#', 1)[0].strip()
            if head not in SECTIONS:
                self.errors.append('%d: unknown section %s' % (number, head))
                self._apply(body)
                return False
            section = head
            body.setdefault(section, [])
        self._apply(body)
        return True

    def _apply(self, body):
        for line in body.get('ARCHIVE', []):
            t = tokenize(line)
            if len(t) >= 2:
                self.archive_aliases.append((t[0].lower(), t[1].lower()))
        for name, target in (('FILTER', self.filters), ('LAUNCH', self.launches)):
            for line in body.get(name, []):
                t = tokenize(line)
                if len(t) >= 2:
                    target.append((t[0].lower(), t[1:]))
        for line in body.get('GREP', []):
            self.grep_examples.extend(tokenize(line)[:1])
        for line in body.get('PROGRAM', []):
            t = tokenize(line)
            if t:
                self.programs.append(t)
        for line in body.get('BUFFER', []):
            t = tokenize(line)
            if t and t[0].isdigit() and 1 <= int(t[0]) <= 100:
                self.buffer_megabytes = int(t[0])
        codes = [int(x) for line in body.get('CODE', []) for x in tokenize(line) if x.isdigit()]
        if codes:
            self.binary_codes = codes
        for line in body.get('LINE', []):
            t = tokenize(line)
            if t and t[0].isdigit():
                self.retention_count = int(t[0])
        for name, role in (('COPY', 'Cp'), ('MOVE', 'Mv'), ('DELETE', 'Rm')):
            for line in body.get(name, []):
                t = tokenize(line)
                if t:
                    self.roles[role] = t
        for line in body.get('EDITOR', []):
            t = tokenize(line)
            if t:
                self.editors.append(t)
        for line in body.get('WAIT', []):
            t = tokenize(line)
            if t and t[0].isdigit():
                self.wait_centiseconds = int(t[0])
        for name, target in (('SPEED', self.speed_values), ('ARROW', self.arrow_values)):
            values = []
            for line in body.get(name, []):
                for token in tokenize(line):
                    if token.isdigit():
                        values.append(int(token))
                    else:
                        self.errors.append('%s: not a number: %s' % (name, token))
            if name in body:
                target[:] = values
        if 'MACRO' in body:
            from .macro import parse_macro_lines
            self.macros, errors = parse_macro_lines(body['MACRO'])
            self.errors.extend(errors)
        if 'URL' in body:
            self.url_handlers = []
            for line in body['URL']:
                t = tokenize(line)
                if not t:
                    continue
                name = t[0].capitalize()
                if name not in URL_MODES:
                    self.errors.append('URL: not Open/Pdf/Dump/Term/Copy/Look/Epub: ' + t[0])
                    continue
                argv = [os.path.expanduser(t[1])] + t[2:] if len(t) > 1 else []
                self.url_handlers.append((name, argv))
        if 'KEY' in body:
            self.key_bindings.load_key_section(body['KEY'])
            self.errors.extend(self.key_bindings.errors)

    def load_default_file(self):
        """~/.usilic/usilicrc、無ければ~/.local/lib/usilic/usilicrc(C++のset_usilicrc()と同じ順)。"""
        home = os.path.expanduser('~')
        for path in (os.path.join(home, '.usilic', 'usilicrc'), os.path.join(home, '.local', 'lib', 'usilic', 'usilicrc'),
                     '/usr/local/lib/usilic/usilicrc'):
            try:
                with open(path, encoding='utf-8', errors='replace') as f:
                    text = f.read()
            except OSError:
                continue
            self.path = path
            self.load_rc_text(text)
            return True
        return False

    # --- 問い合わせ ---
    def macro_list(self):
        if self.macros is not None:
            return self.macros
        from .macro import DEFAULT_MACRO_LINES, parse_macro_lines
        return parse_macro_lines(DEFAULT_MACRO_LINES)[0]

    def url_handler_list(self):
        return self.url_handlers if self.url_handlers else default_url_handlers()

    def editor_argv(self):
        """EDIT: 環境変数EDITORがあればそちら、無ければEDITOR節の最初(無ければpico)。"""
        if os.environ.get('EDITOR'):
            return os.environ['EDITOR'].split()
        return list(self.editors[0]) if self.editors else ['pico']

    def command_for(self, role):
        return list(self.roles.get(role, []))

    def buffer_limit_bytes(self):
        return self.buffer_megabytes * 1024 * 1024

    def _suffix_of(self, path, table):
        lower = path.lower()
        best = None
        for suffix, value in table:
            if lower.endswith(suffix) and (best is None or len(suffix) > len(best[0])):
                best = (suffix, value)
        return best

    def archive_for(self, path):
        """programForSuffix()のアーカイブの分: (種類, 一覧のargv, 前段のargv, バッジ)かNone。"""
        lower = path.lower()
        for alias, target in self.archive_aliases:
            if lower.endswith(alias):
                lower = lower[:len(lower) - len(alias)] + target
                break
        best = None
        for suffix, kind, argv, piped in _BUILTIN_ARCHIVES:
            if lower.endswith(suffix) and (best is None or len(suffix) > len(best[0])):
                best = (suffix, kind, argv, piped)
        if best is None:
            return None
        _, kind, argv, piped = best
        argv = [seven_zip_command() if a is None else a for a in argv]
        return kind, argv, piped, '(' + argv[0] + ')'

    def filter_for(self, path):
        found = self._suffix_of(path, self.filters)
        return found[1] if found else None

    def launch_for(self, path):
        found = self._suffix_of(path, self.launches)
        return found[1] if found else None

    def looks_binary(self, head):
        """FileMode::looksBinary(): 頭の64KBに、NULか、CODEに無い0x20未満の字(改行は除く)があるか。

        見つかればその字を返す(「(^@) data」の表示用)、無ければNone。code0が0なら判定しない。
        """
        if self.binary_codes is not None and (not self.binary_codes or self.binary_codes[0] == 0):
            return None
        allowed = set(self.binary_codes or [1, 7, 8, 9, 10, 12, 13, 14, 15, 26, 27, 31])
        for byte in head[:0x10000]:
            if byte == 0 or (byte < 0x20 and byte != 0x0A and byte not in allowed):
                return byte
        return None
