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
    name_start: int = 0   # 行の中の名前の始まり(nameStartColumn、カーソルの反転はここから)


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
    return DirectoryEntry(line, name, is_directory, name_start)


def sort_by_kind(entries, reverse=False):
    """Kind(拡張子の順、C++のenterDirectory()): ./ ../ が先、拡張子のあるfile、無いfile、ディレクトリ。"""
    nav = [e for e in entries if e.name in ('./', '../')]
    rest = [e for e in entries if e.name not in ('./', '../')]

    def extension(name):
        stripped = name.rstrip('/*@=%|')
        dot = stripped.rfind('.')
        return '' if dot <= 0 else stripped[dot + 1:]

    def key(e):
        rank = 2 if e.is_directory else (1 if not extension(e.name) else 0)
        return (rank, extension(e.name), e.name)
    rest.sort(key=key, reverse=reverse)
    return nav + rest


SORT_KEYS = ['Name', 'Date', 'Size', 'Kind']


class DirectoryMode:
    def __init__(self, view):
        self.view = view
        self.current_directory = ''
        self.entries = []
        # ^Gで戻る所(C++のModeStack): (ディレクトリ, カーソル)の並び、または('floor', ここ, 前)1つだけ(2つの間を
        # ^Gで行き来する、pushFloor())
        self.mode_stack = []
        self.byte_count = 0
        self.cursor_positions = {}
        self.history = None  # ディレクトリで起動した時だけ(ページャーでは履歴に触れない)
        self.sort_key = 'Name'   # ^Olで巡回(Name→Date→Size→Kind、C++のLsSortKey)
        self.reverse = False     # ^O^L
        self.filename_only = False  # ^Oo(名前だけの表示)
        self.columns = 80           # 画面の幅(apply_column_auto_display()用、Applicationが入れる)
        self.column_threshold = 8   # usilicrcのCOLUMN
        self.sort_label_shown = False
        self.marks = set()   # 選択(下線、. ^SPC ^Oy ^On)。項目の番号
        self.temporary_marks = False  # CP/MV/展開の後の一時的な選択(^Lで消える)

    def enter_directory(self, path):
        """pathの一覧を出す(DirectoryMode::enterDirectory())。"""
        path = os.path.abspath(path)
        if not path.endswith('/'):
            path += '/'
        env = dict(os.environ, LC_ALL='C')
        try:
            argv = ['ls', '-alF'] + {'Date': ['-t'], 'Size': ['-S']}.get(self.sort_key, []) + \
                (['-r'] if self.reverse else []) + [path]
            result = subprocess.run(argv, capture_output=True, env=env)
        except OSError:
            return False
        if result.returncode != 0:
            return False
        if self.current_directory:
            self.save_current_cursor_position()
        text = result.stdout.decode('utf-8', 'replace')
        lines = [l for l in text.split('\n') if l and not l.startswith('total ')]
        self.entries = [parse_ls_line(l) for l in lines]
        if self.sort_key == 'Kind':
            self.entries = sort_by_kind(self.entries, self.reverse)
        self.current_directory = path
        self.marks = set()
        self.temporary_marks = False
        self.set_lines()
        cursor, top = self.cursor_positions.get(path, (0, 0))
        if self.history is not None:
            self.history.push_recent(hist.RECENT_DIRECTORY, path)
            pos = self.history.position_for(hist.DIRECTORY, path)
            cursor, top = (pos.row, 0) if pos else (0, 0)
        self.view.cursor_index, self.view.top_index = cursor, top
        self.view.clamp()
        self.view.ensure_cursor_visible()
        self.apply_column_auto_display()  # カーソルが決まった後に(C++のenterDirectory()と同じ)
        return True

    def set_filename_only(self, enabled):
        """setFilenameOnlyMode(): lsをやり直さずに、行を名前だけ/lsの行に。"""
        self.filename_only = enabled
        cursor, top = self.view.cursor_index, self.view.top_index
        self.set_lines()
        self.view.cursor_index, self.view.top_index = cursor, top
        self.view.clamp()

    def set_lines(self):
        """項目→行(名前だけ/lsの行)。大きさは出している行で数える(C++のtotalByteCount()、「total」の行は入らない)。"""
        lines = [Line(e.name if self.filename_only else e.ls_line, i + 1,
                      highlight_start=None if self.filename_only else e.name_start)
                 for i, e in enumerate(self.entries)]
        self.byte_count = sum(len(l.text.encode('utf-8')) + 1 for l in lines)
        self.view.set_lines(lines)

    def apply_column_auto_display(self):
        """applyColumnAutoDisplay(): 名前の欄(画面の幅-カーソル行の名前の始まり)がCOLUMNより狭ければ名前だけの表示、
        広ければlsの行に(^Ooで切り換えた後も、一覧を読み直す・幅が変わるたびにこの決まりに戻る)。"""
        if not self.entries:
            return
        entry = self.selected_entry() or self.entries[0]
        should = self.columns - entry.name_start < self.column_threshold
        if should != self.filename_only:
            self.set_filename_only(should)

    def save_current_cursor_position(self):
        """DirectoryMode::saveCurrentCursorPosition()(row=カーソル、totalRows=項目の数、top=0)。"""
        self.cursor_positions[self.current_directory] = (self.view.cursor_index, self.view.top_index)
        if self.history is not None and self.current_directory:
            self.history.set_position(hist.DIRECTORY, self.current_directory,
                                      hist.CursorPosition(self.view.cursor_index, len(self.entries), 0, -1))

    def selected_entry(self):
        i = self.view.cursor_index
        return self.entries[i] if 0 <= i < len(self.entries) else None

    # --- ^Gで戻る所(C++のDirectoryMode::descendTo()・goBack()・resetNavigationToFloor()) ---
    def push_floor(self, directory, previous):
        """ModeStack::pushFloor(): 戻る所を消して、ここと前の2つを行き来する1組にする。"""
        self.mode_stack = [('floor', directory, previous)]

    def reset_navigation_to_floor(self, previous):
        self.push_floor(self.current_directory, previous)

    def is_floor_on_top(self):
        return bool(self.mode_stack) and self.mode_stack[-1][0] == 'floor' and len(self.mode_stack[-1]) == 3

    def descend_to(self, target):
        """下(や親)のディレクトリへ。^Gで戻る所に今の所を積む(行き来の組の上なら、組を新しい2つに置き換える)。"""
        previous, cursor = self.current_directory, self.view.cursor_index
        floor = self.is_floor_on_top()
        if not self.enter_directory(target):
            return False
        if floor:
            self.push_floor(self.current_directory, previous)
        elif previous:
            self.mode_stack.append((previous, cursor))
        return True

    def go_back(self):
        """^G: 積んだ所へ。行き来の組なら前の所へ移って組を入れ替える(本当の行き来)。"""
        if not self.mode_stack:
            return False
        if self.current_directory:
            self.save_current_cursor_position()
        frame = self.mode_stack.pop()
        if len(frame) == 3:
            _, directory, previous = frame
            if not previous or not self.enter_directory(previous):
                return False
            self.push_floor(self.current_directory, directory)
            return True
        directory, cursor = frame
        if not self.enter_directory(directory):
            return False
        if self.history is None:  # 履歴が無い時(ページャー)は積んだ時のカーソル
            self.view.cursor_index = cursor
            self.view.ensure_cursor_visible()
        return True

    def go_to_parent(self):
        target = self.parent_directory(self.current_directory)
        if target == self.current_directory:
            return False
        return self.descend_to(target)

    @staticmethod
    def parent_directory(directory):
        trimmed = directory.rstrip('/')
        parent = os.path.dirname(trimmed) or '/'
        return parent if parent.endswith('/') else parent + '/'

    def cycle_sort_key(self):
        self.sort_key = SORT_KEYS[(SORT_KEYS.index(self.sort_key) + 1) % len(SORT_KEYS)]
        self.sort_label_shown = True
        return self.refresh()

    def toggle_reverse_sort(self):
        self.reverse = not self.reverse
        return self.refresh()

    def toggle_filename_only(self):
        """^Oo: 読み直さずに切り換える(C++のsetFilenameOnlyMode()、読み直すとCOLUMNの決まりで戻ってしまう)。"""
        self.set_filename_only(not self.filename_only)
        return True

    def refresh(self):
        cursor = self.view.cursor_index
        ok = self.enter_directory(self.current_directory)
        self.view.cursor_index = cursor
        self.view.clamp()
        self.view.ensure_cursor_visible()
        return ok
