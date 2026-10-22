"""DUモード(C++のdu_mode.cppのDuMode)。

`du -ak <ディレクトリ>`の出力(「サイズ<TAB>パス」)を、「サイズK  パス」(サイズは右詰め、種類の印 / @ * | =
を付ける)の行にする。並びはduの出力の順(C++版と同じ)。
"""
import os
import stat
from dataclasses import dataclass

from .content_view import Line


@dataclass
class DuEntry:
    size_kb: int
    path: str
    is_directory: bool
    type_marker: str = ''


def _type_marker(path):
    try:
        st = os.lstat(path)
    except OSError:
        return ''
    mode = st.st_mode
    if stat.S_ISLNK(mode):
        return '@'
    if stat.S_ISDIR(mode):
        return '/'
    if stat.S_ISFIFO(mode):
        return '|'
    if stat.S_ISSOCK(mode):
        return '='
    if stat.S_ISREG(mode) and mode & 0o111:
        return '*'
    return ''


def parse_du_output(text):
    entries = []
    for line in text.split('\n'):
        size, tab, path = line.partition('\t')
        if not tab:
            continue
        marker = _type_marker(path)
        if marker == '/' and path.endswith('/'):
            marker = ''  # du -ak /dir/ はディレクトリ自身を「/dir/」と書く
        entries.append(DuEntry(int(size) if size.strip().isdigit() else 0, path, os.path.isdir(path), marker))
    return entries


class DuMode:
    def __init__(self, view, configuration):
        self.view = view
        self.configuration = configuration
        self.current_path = ''
        self.entries = []
        self.byte_count = 0

    def command_argv_for(self, path):
        """DuMode::commandArgvFor(): 末尾の/を外して渡す。"""
        if len(path) > 1 and path.endswith('/'):
            path = path[:-1]
        return self.configuration.command_for('Du') + [path]

    def load_usage_from_output(self, path, output):
        self.entries = parse_du_output(output)
        width = max([len(str(e.size_kb)) + 1 for e in self.entries] or [0])
        lines = [('%dK' % e.size_kb).rjust(width) + '  ' + e.path + e.type_marker for e in self.entries]
        self.byte_count = sum(len(l.encode('utf-8', 'surrogateescape')) + 1 for l in lines)
        self.view.set_lines([Line(l, i + 1) for i, l in enumerate(lines)])
        self.view.cursor_index = self.view.top_index = 0
        self.current_path = path
        return True

    def selected_entry(self):
        i = self.view.cursor_index
        return self.entries[i] if 0 <= i < len(self.entries) else None
