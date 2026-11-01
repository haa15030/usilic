"""ARCモード(C++のarchive_mode.cppのArchiveModeとarchive_reader.cppの各Reader)。

一覧の行は「サイズ  名前」(サイズは右詰め、C++のformatEntryLine())。tar系・zip系はPythonの標準ライブラリ
(tarfile・zipfile)で読み、他の形式はC++版と同じコマンドの一覧を同じ読み方で読む(lha・rpm -qplv・ar tv・
lsar -l・7z l)。中のfileはメモリへ取り出す。
"""
import io
import os
import re
import subprocess
import tarfile
import zipfile
from dataclasses import dataclass

from . import configuration as conf
from .content_view import Line


@dataclass
class ArchiveEntry:
    name: str
    size: int = 0
    is_directory: bool = False


def _lines(text):
    return [l for l in text.split('\n') if l]


def parse_lha_listing(listing):
    """LhaReader::parseListing(): 「%」の欄(40桁より前)を目印に、その4欄先が名前、2欄前がサイズ。"""
    entries = []
    for line in _lines(listing):
        marker = line.find('%', 0, 40)
        if marker < 0:
            continue
        fields = line[marker:].split(None, 4)
        if len(fields) < 5:
            continue
        name = fields[4]
        before = line[:marker].split()
        size = int(before[-2]) if len(before) >= 2 and before[-2].isdigit() else 0
        entries.append(ArchiveEntry(name, size, name.endswith('/')))
    return entries


def parse_rpm_listing(listing):
    """RpmReader::parseListing(): ls -l形式の5欄目がサイズ(「,」はデバイス)、最後の欄が名前。"""
    entries = []
    for line in _lines(listing):
        arrow = line.find(' -> ')
        if arrow >= 0:
            line = line[:arrow]
        fields = line.split()
        if not fields:
            continue
        size_text = fields[4] if len(fields) > 4 else ''
        special = ',' in size_text
        size = int(size_text.split(',')[0]) if size_text[:1].isdigit() and not special else 0
        entries.append(ArchiveEntry(fields[-1], size, line.startswith('d')))
    return entries


def parse_ar_listing(listing):
    """ar tv: 「rw-r--r-- 0/0 1234 Jan 1 00:00 2020 名前」。"""
    entries = []
    for line in _lines(listing):
        fields = line.split(None, 7)
        if len(fields) >= 8 and fields[2].isdigit():
            entries.append(ArchiveEntry(fields[7], int(fields[2])))
    return entries


def parse_bsdtar_listing(listing):
    """`tar -tvf`(bsdtar)の一覧: 「種類と許可 リンク数 持ち主 グループ 大きさ 日付3つ 名前」。"""
    entries = []
    for line in listing.splitlines():
        parts = line.split(None, 8)
        if len(parts) < 9 or not parts[4].isdigit():
            continue
        name = parts[8].split(' -> ')[0]
        is_directory = parts[0].startswith('d') or name.endswith('/')
        entries.append(ArchiveEntry(name, 0 if is_directory else int(parts[4]), is_directory))
    return entries


def parse_rar_listing(listing):
    """RarReader::parseListing()(lsar -l): 「=」の行の後から、「(」で始まる注記の前まで。"""
    entries, in_table = [], False
    for line in _lines(listing):
        stripped = line.lstrip(' ')
        if not stripped:
            continue
        if not in_table:
            in_table = stripped[0] == '='
            continue
        if stripped[0] == '(':
            break
        fields = stripped.split(None, 7)
        if len(fields) < 8:
            continue
        flags, size = fields[1], fields[2]
        entries.append(ArchiveEntry(fields[7], int(size) if size.isdigit() else 0, 'D' in flags))
    return entries


def parse_7z_listing(listing):
    """SevenZipReader::parseListing(): 2本の罫線の間、属性20桁・サイズ26〜38桁・名前53桁から。"""
    entries, rules = [], 0
    for line in _lines(listing):
        if len(line) >= 19 and line[0] == '-' and not line.strip('- '):
            rules += 1
            if rules >= 2:
                break
            continue
        if rules < 1 or len(line) <= 53:
            continue
        attr = line[20:25]
        size = int(line[26:38].strip() or 0) if line[26:38].strip().isdigit() else 0
        entries.append(ArchiveEntry(line[53:], size, attr.startswith('D')))
    return entries


def is_paired_apple_double(sidecar, following):
    """C++のisPairedAppleDouble(): 「._X」で、すぐ後ろが本体のX(ディレクトリならX/)か。本体の無い「._」は隠さない
    (macOSのtarが「._」だけの書庫の半分を隠していた、C++の2026-10-16の件)。"""
    head, slash, base = sidecar.rpartition('/')
    if not base.startswith('._') or len(base) <= 2:
        return False
    if len(following) > 1 and following.endswith('/'):
        following = following[:-1]
    return head + slash + base[2:] == following


def _tar_line_name(line):
    """`tar tvf`の行の名前: bsdtarは「許可 リンク数 持ち主 グループ 大きさ 日付3つ 名前」、GNUは「許可 持ち主/グループ
    大きさ 日付 時刻 名前」(C++のparseTarLine())。"""
    parts = line.split()
    count = 8 if len(parts) > 8 and parts[1].isdigit() else 5
    fields = line.split(None, count)
    return fields[count].split(' -> ')[0] if len(fields) > count else line


def remove_paired_apple_double_lines(listing):
    """C++のremovePairedAppleDoubleLines(): tarの一覧の出力から、本体と組になる「._X」の行を除く(選べない一覧)。"""
    lines = listing.splitlines()
    return ''.join(line + '\n' for i, line in enumerate(lines)
                   if not (i + 1 < len(lines) and is_paired_apple_double(_tar_line_name(line),
                                                                         _tar_line_name(lines[i + 1]))))


def escape_member_pattern(name):
    """escapeArchiveMemberPattern(): tar・unzip・lhaの名前はパターンなので、* ? [ \\ を字として。"""
    return re.sub(r'([*?\[\\])', r'\\\1', name)


class ArchiveMode:
    def __init__(self, view, configuration):
        self.view = view
        self.configuration = configuration
        self.current_archive = ''   # 開いているアーカイブ(中のアーカイブは取り出した一時ファイル)
        self.display_name = ''      # モードラインの名前
        self.kind = None
        self.badge = ''
        self.entries = []
        self.marks = set()
        self.byte_count = 0
        self.error = None

    def _run(self, argv, stdin_bytes=None):
        try:
            result = subprocess.run(argv, input=stdin_bytes, capture_output=True,
                                    env=dict(os.environ, LC_ALL='C', LANG='C'))
        except OSError:
            return None
        return result.stdout if result.returncode in (0, 1) or result.stdout else None

    def _tar_open(self, path):
        if path.lower().endswith(('.zst', '.tzst')):
            data = self._run(['zstd', '-dc', path])
            if data is None:
                return None
            return tarfile.open(fileobj=io.BytesIO(data))
        return tarfile.open(path)

    def load_archive(self, path, display_name=None):
        """ArchiveMode::loadArchive()。読めない(一覧が取れない)形式はFalse(普通のfileとして開く)。"""
        found = self.configuration.archive_for(path)
        if not found:
            return False
        kind, argv, _, badge = found
        entries = None
        try:
            if kind == conf.TAR:
                with self._tar_open(path) as tar:
                    members = tar.getmembers()
                    entries = [ArchiveEntry(m.name + ('/' if m.isdir() else ''), 0 if m.isdir() else m.size, m.isdir())
                               for i, m in enumerate(members)
                               if not (i + 1 < len(members) and is_paired_apple_double(m.name, members[i + 1].name))]
            elif kind == conf.ZIP:
                with zipfile.ZipFile(path) as z:
                    entries = [ArchiveEntry(i.filename, 0 if i.is_dir() else i.file_size, i.is_dir())
                               for i in z.infolist()]
                if path.lower().endswith('.epub'):
                    entries.sort(key=lambda e: e.name)  # .epubは名前順(C++のfinishLoadFromOutput())
        except (OSError, tarfile.TarError, zipfile.BadZipFile, AttributeError):
            entries = None
        if entries is None and kind == conf.TAR and argv[:2] == ['tar', '-tvf']:
            listing = self._run(argv + [path])  # tarの形でないlzh・rar・7zをbsdtarに一覧させる(lha等の無いmacOS)
            if listing is not None:
                entries = parse_bsdtar_listing(listing.decode('utf-8', 'surrogateescape'))
        if entries is None and kind not in (conf.TAR, conf.ZIP):
            listing = self._run(argv + [path])
            if listing is not None:
                text = listing.decode('utf-8', 'surrogateescape')
                parser = {conf.LHA: parse_lha_listing, conf.RPM: parse_rpm_listing, conf.DEB: parse_ar_listing,
                          conf.RAR: parse_rar_listing, conf.SEVEN_ZIP: parse_7z_listing}[kind]
                entries = parser(text)
        if not entries:
            return False
        self.current_archive, self.kind, self.badge = path, kind, badge
        self.display_name = display_name or path
        self.entries = entries
        self.marks = set()
        self.error = None
        width = max(len(str(e.size)) for e in entries)
        lines = ['%*d  %s' % (width, e.size, e.name) for e in entries]
        self.byte_count = sum(len(l.encode('utf-8', 'surrogateescape')) + 1 for l in lines)
        self.view.set_lines([Line(l, i + 1, highlight_start=width + 2, ellipsis=True)  # 反転は名前から
                             for i, l in enumerate(lines)])
        self.view.cursor_index = self.view.top_index = 0
        return True

    def selected_entry(self):
        i = self.view.cursor_index
        return self.entries[i] if 0 <= i < len(self.entries) else None

    def extract(self, entry):
        """中のfileの中身(バイト列)。取り出せなければNone。"""
        path, kind, name = self.current_archive, self.kind, entry.name
        try:
            if kind == conf.TAR:
                try:
                    with self._tar_open(path) as tar:
                        member = tar.extractfile(name.rstrip('/') if entry.is_directory else name)
                        return member.read() if member else None
                except tarfile.ReadError:  # tarの形でない(bsdtarに一覧させたlzh・rar・7z): その1つをtarに取り出させる
                    return self._run(['tar', '-xOf', path, name])
            if kind == conf.ZIP:
                with zipfile.ZipFile(path) as z:
                    return z.read(name)
        except (OSError, KeyError, tarfile.TarError, zipfile.BadZipFile):
            return None
        argv = {conf.LHA: ['lha', 'pq', path, name], conf.DEB: ['ar', 'p', path, name],
                conf.RAR: ['unar', '-f', '-o', '-', '-q', path, name],
                conf.SEVEN_ZIP: [conf.seven_zip_command(), 'x', '-so', path, name]}.get(kind)
        if kind == conf.RPM:
            cpio = self._run(['rpm2cpio', path])
            if cpio is None:
                return None
            return self._run(['cpio', '-i', '--to-stdout', '--quiet', '.' + name if name.startswith('/') else name],
                             stdin_bytes=cpio)
        return self._run(argv) if argv else None

    def target_entries(self):
        return [self.entries[i] for i in sorted(self.marks)] if self.marks else []

    @staticmethod
    def extract_argv(path, kind, members, destination):
        """Application::buildArchiveExtractArgv()(rpm・deb・rarは無し)。"""
        if kind == conf.TAR:
            return ['tar', 'xf', path, '-C', destination] + [escape_member_pattern(m) for m in members]
        if kind == conf.ZIP:
            return ['unzip', '-o', path] + [escape_member_pattern(m) for m in members] + ['-d', destination]
        if kind == conf.SEVEN_ZIP:
            return [conf.seven_zip_command(), 'x', path, '-o' + destination, '-y'] + members
        if kind == conf.LHA:
            return ['lha', 'xfw=' + destination, path] + [escape_member_pattern(m) for m in members]
        return None

    @staticmethod
    def top_level_names(names):
        """展開した後の(ls)で下線を付ける名前(一番上の階層、C++と同じ)。"""
        out = []
        for name in names:
            if name.startswith('./'):
                name = name[2:]
            top = name.split('/', 1)[0]
            if top and top not in out:
                out.append(top)
        return out
