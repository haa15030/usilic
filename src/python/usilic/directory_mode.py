"""(ls)のディレクトリの一覧(C++のdirectory_mode.cppのDirectoryMode)。

C++版(Unix)と同じく`ls -alF`をCのロケールで動かし、その行をそのまま出す。名前は月名の位置から取り出す
(DirectoryMode::parseLsLine())。
"""
import os
import subprocess
from dataclasses import dataclass

from . import history as hist
from .content_view import Line

_MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')


@dataclass
class DirectoryEntry:
    ls_line: str
    name: str
    is_directory: bool


def parse_ls_line(line):
    """DirectoryMode::parseLsLine(): 月名(前後が空白)の13桁先が名前。-Fの印(*@=%|)は外す。"""
    searchable = line
    arrow = searchable.find(' -> ')  # シンボリックリンクの参照先は名前の一部ではない
    if arrow >= 0:
        searchable = searchable[:arrow]
    name_start = None
    for month in _MONTHS:
        pos = searchable.find(month)
        while pos >= 0:
            left_ok = pos == 0 or searchable[pos - 1] == ' '
            right_ok = pos + 3 < len(searchable) and searchable[pos + 3] == ' '
            if left_ok and right_ok and len(searchable) > pos + 13:
                if name_start is None or pos + 13 < name_start:
                    name_start = pos + 13
            pos = searchable.find(month, pos + 1)
    if name_start is None:
        name_start = searchable.rfind(' ') + 1
    name = searchable[name_start:]
    is_directory = line.startswith('d')
    if not is_directory and name and name[-1] in '*@=%|':
        name = name[:-1]
    return DirectoryEntry(line, name, is_directory)


class DirectoryMode:
    def __init__(self, view):
        self.view = view
        self.current_directory = ''
        self.entries = []
        self.mode_stack = []  # ^Gで戻る(ディレクトリ, カーソル)
        self.byte_count = 0
        self.cursor_positions = {}
        self.history = None  # ディレクトリで起動した時だけ(ページャーでは履歴に触れない)
        self.marks = set()   # 選択(下線、. ^SPC ^Oy ^On)。項目の番号
        self.temporary_marks = False  # CP/MV/展開の後の一時的な選択(^Lで消える)

    def enter_directory(self, path):
        """pathの一覧を出す(DirectoryMode::enterDirectory())。"""
        path = os.path.abspath(path)
        if not path.endswith('/'):
            path += '/'
        env = dict(os.environ, LC_ALL='C')
        try:
            result = subprocess.run(['ls', '-alF', path], capture_output=True, env=env)
        except OSError:
            return False
        if result.returncode != 0:
            return False
        if self.current_directory:
            self.save_current_cursor_position()
        text = result.stdout.decode('utf-8', 'replace')
        lines = [l for l in text.split('\n') if l and not l.startswith('total ')]
        self.entries = [parse_ls_line(l) for l in lines]
        self.byte_count = len(result.stdout)
        self.current_directory = path
        self.marks = set()
        self.temporary_marks = False
        self.view.set_lines([Line(e.ls_line, i + 1) for i, e in enumerate(self.entries)])
        cursor, top = self.cursor_positions.get(path, (0, 0))
        if self.history is not None:
            self.history.push_recent(hist.RECENT_DIRECTORY, path)
            pos = self.history.position_for(hist.DIRECTORY, path)
            cursor, top = (pos.row, 0) if pos else (0, 0)
        self.view.cursor_index, self.view.top_index = cursor, top
        self.view.clamp()
        self.view.ensure_cursor_visible()
        return True

    def save_current_cursor_position(self):
        """DirectoryMode::saveCurrentCursorPosition()(row=カーソル、totalRows=項目の数、top=0)。"""
        self.cursor_positions[self.current_directory] = (self.view.cursor_index, self.view.top_index)
        if self.history is not None and self.current_directory:
            self.history.set_position(hist.DIRECTORY, self.current_directory,
                                      hist.CursorPosition(self.view.cursor_index, len(self.entries), 0, -1))

    def selected_entry(self):
        i = self.view.cursor_index
        return self.entries[i] if 0 <= i < len(self.entries) else None

    @staticmethod
    def parent_directory(directory):
        trimmed = directory.rstrip('/')
        parent = os.path.dirname(trimmed) or '/'
        return parent if parent.endswith('/') else parent + '/'
