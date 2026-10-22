"""モードの切り替えとキーのディスパッチ(C++のapplication.cppのApplication)。

キーはC++版と同じK-番号(key_bindings.py、usilicrcのKEY節で置き換えられる)で受ける。打ったバイトを
pending_keysにため、割り当てと完全に一致すればその機能(handle_key_number())、どれかの頭なら次を待つ。
^Gの戻り先はreturn_stack(モードを積む、(ls)でディレクトリへ移ると空にする、C++と同じ)。
"""
import os
import re
import select
import shlex
import signal
import subprocess
import sys
import tempfile
import time

from . import mode_line, utf8
from . import history as hist
from . import help_text
from . import bookmarks as bm
from . import macro as mc
from . import messages_ja
from . import prompt as pr
from .archive_mode import ArchiveMode
from .configuration import Configuration
from .content_view import ContentView, Line, wrap_text, expand_tabs
from .copy_mode import CopyMode
from .directory_mode import DirectoryMode
from .du_mode import DuMode
from . import epub_vertical as ev
from .epub_mode import EpubBook, is_epub_file
from .markdown import is_markdown_file, render_markdown
from .file_mode import FileMode
from .grep_mode import GrepMode, FIXED, REGEX, match_spans
from .key_bindings import CTRL_SPACE, DIGIT, prefix_label
from .line_editor import LineEditor
from .list_browser_mode import ListBrowserMode
from .locate_mode import LocateMode
from .man_mode import ManMode
from .part_mode import PartMode, occur_lines
from .messages_ja import japanese, localized
from .mouse import MouseEvent, Paste, Speeds, dropped_paths, level_for_release_distance, step_level_by_key
from .shell import Shell

PROGRAM_LABEL = 'usilic-1.5b'
DIRECTORY, FILE, SHELL = 'Directory', 'File', 'Shell'
ARCHIVE, LIST, MAN, DU, LOCATE, KEY, HELP, TOC = 'Archive', 'List', 'Man', 'Du', 'Locate', 'Key', 'Help', 'Toc'
PART_TOC, PART, OCCUR, INFO = 'PartToc', 'Part', 'Occur', 'Info'
USILIC_SCRIPT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'usilic.py')
REPEATABLE = set(range(1, 13)) | {15, 16, 97}  # ^Uの回数が効くK-番号(移動・n/p・^Xe)
CTRL_G, CTRL_J, CTRL_O, CTRL_X, ESC, CR = 0x07, 0x0A, 0x0F, 0x18, 0x1B, 0x0D
CPMVRM_MARKER = '#usilic-cpmvrm#'  # SHELLで動かしたCP/MV/RM・展開の終わりの印(C++のkCpMvRmExitMarker)

# ページャーとして起動した時に使えるK-番号(HELPの行頭がタブの行のキー、C++のpagerAllowedKeys())
PAGER_KEYS = set(range(1, 24)) | {28, 47, 48, 49, 51, 52, 99, 100}


class Application:
    def __init__(self, display, key_input, configuration=None):
        self.display = display
        self.key_input = key_input
        self.configuration = configuration or Configuration()
        self.bindings = self.configuration.key_bindings
        self.directory_view = ContentView()
        self.file_view = ContentView()
        self.file_view.cursor_highlight = False  # FileModeのコンストラクタと同じ(ページャーは反転しない)
        self.directory_mode = DirectoryMode(self.directory_view)
        self.file_mode = FileMode(self.file_view)
        self.current_mode = DIRECTORY
        self.return_stack = []      # ^Gの戻り先(モード)
        self.pager_launch = False   # 引数のfile・標準入力で起動した(Application::pagerLaunch_)
        self.pager_files = []       # n/p/x/fで切り替えるfile
        self.pager_index = 0
        self.start_directory = os.getcwd()
        self.wants_exit = False
        self.notice = None          # showTransientNotice()の文(次のキーで消える)
        self.pending_keys = []      # 照合中のキー(C++のleaderXPending_等をまとめたもの)
        self.goto_digits = None     # ^[Nの数字(gotoLineDigits_)
        self.prompt = None          # モードラインの入力欄(prompt.Prompt)
        self.mark_before_jump = None  # ^X^Xの戻り先(markBeforePageScroll()等)
        self.prefix_since = None
        self.prefix_shown = False
        self.utf8_pending = bytearray()
        # SHELL(Application::shell_)
        self.shell = Shell()
        self.shell_view = ContentView()
        self.shell_view.cursor_highlight = False
        self.command_line = LineEditor()
        self.pending_shell_run = None   # SHELLで動かしているCP/MV/RM・展開(終わったら行き先の(ls)へ)
        self.history = None  # ディレクトリで起動した時だけ(usilic.py)
        # FGREP/EGREP(grepInPlaceActive_、PromptKind::Fgrep/Egrep)
        self.grep = GrepMode()
        self.grep_active = False
        self.grep_prompt = None   # (syntax, LineEditor)を打っている間
        # 一覧・各モード
        self.list_view = ContentView()
        self.list_mode = ListBrowserMode(self.list_view)
        self.list_prompt = None    # ^Rで一覧を開いた元のプロンプト(^Mでそこへ戻す)
        self.archives = []         # ARCの入れ子(一番後ろが今のアーカイブ)
        self.man_view = ContentView()
        self.man_mode = ManMode(self.man_view, self.configuration)
        self.du_view = ContentView()
        self.du_mode = DuMode(self.du_view, self.configuration)
        self.locate_view = ContentView()
        self.locate_mode = LocateMode(self.locate_view, self.configuration)
        self.key_view = ContentView()
        self.help_view = ContentView()
        self.help_view.cursor_highlight = False
        self.help_byte_count = 0
        self.key_byte_count = 0
        self.toc_view = ContentView()
        self.epub = None           # 開いている本(EpubBook)
        self.epub_name = ''        # モードラインの名前(HELPの本は「HELP」)
        self.epub_toc_lines = []   # 目次の各項目の本文の行
        self.previous_file = None  # ^J^Jの行き先
        # 第4段: PART・OCCUR・INFO・COPY・マーク・^U・マクロ・レジスタ等
        self.part_toc_view = ContentView()
        self.part_view = ContentView()
        self.part_view.cursor_highlight = False
        self.part = PartMode(self.part_toc_view, self.part_view)
        self.occur_view = ContentView()
        self.occur_items = []      # (元の画面の行, Line)
        self.occur_origin = None   # 一覧を作った元のモード
        self.occur_syntax = FIXED
        self.occur_global = False  # occur_itemsの行が本全体の行(EPUBの記録の一覧)
        self.info_view = ContentView()
        self.info_targets = []     # INFOの行ごとの開く物(Noneは開けない)
        self.copy = None           # COPY(CopyMode)
        self.copy_prefix = None    # COPYの中の'ESC'・'X'・'goto'
        self.copy_digits = ''
        self.clipboard_text = ''
        self.mark_ring = []        # (view, 行)、最大16(^␣・^X^X・^U^␣)
        self.count = None          # ^Uの回数
        self.count_digits = None
        self.last_command = None   # ^Xzで繰り返す(K-番号, 数字, 回数)
        self.repeating_z = False
        self.register_state = None  # 'r'(^Xrの後)・'set'・'jump'
        self.registers = {}
        self.macro_recording = None  # ^X(〜^X)のバイト列
        self.macro_bytes = b''
        self.macro_just_played = False
        self.playing = False
        self.mode_line_hidden = False  # ^Of
        self.url_mode = 0              # ^Owで巡回(usilicrcのURLの何番目か)
        self.epub_link_stack = []      # 本の中のリンクで飛ぶ前の行(^Gで戻る)
        self.epub_chapter_mode = False  # ^Os(章ごと)
        self.epub_lines = []           # 本全体の行(章ごとの時はその一部をfile_viewへ)
        self.epub_chapter = 0
        # 縦書き(本のOPFがpage-progression-direction="rtl"の時、epub_vertical.py)
        self.vertical = None           # 組んだ列の並びと位置(VerticalBook)
        self.vertical_view = ContentView()
        self.vertical_view.cursor_highlight = False
        self.vertical_key = None       # 組んだ時の(本の行, 行数, 桁数, 章ごと)、変われば組み直す
        self.vertical_synced = None    # 縦書きに合わせた本全体の行(横書きの側で動いたら縦書きを合わせる)
        self.vertical_pieces = {}      # 元の行→[(本全体の行, 頭の空白の数, 論理行の中での始まり)]
        self.vertical_last_number = None
        self.vertical_source = None    # 組んだ時の本全体の行(epub_lines、別の物になれば組み直す)
        # 縦書きの本の中の横書きの範囲(奥付等、EpubHints.horizontal)を見ている間、その範囲(元の行の両端)
        self.epub_section = None
        self.epub_section_line = None  # 範囲を見ている時の本全体の行(キー以外で動いたら範囲を離れたとみなす)
        self.epub_from_below = False   # 先へ送って範囲へ入り、最初の行がまだ画面の上まで上がっていない
        self.epub_note = None          # 注釈の区画を見ている間、元の所(show_epub_note())
        self.last_key_number = None    # 最後のK-番号(横書きの範囲から出たかの決め方)
        # マウス(C++のapp/main.cppのrunTerminalPhase()・runTerminalMouseDrag()・runCoastLoop())
        self.drag = None               # 左ボタンを押している間(押した所・動いた量・離す直前の動き)
        self.coast = None              # 慣性スクロール(段・向き・縦書きの列か・次に進む時刻)
        self.click_targets_cursor = False  # 慣性を止めた後の次のクリックはハイライト行へ
        self.wheel_reversed = False    # ^Od(ホイールの向き)
        self.speeds = Speeds(self.configuration.speed_values, self.configuration.arrow_values)
        # 縦書きの本の章の一覧・検索結果の一覧(OCCUR)の縦書き(C++のrefreshEpubTocVertical())
        self.vlist = None              # 組んだ一覧(VerticalBook)
        self.vlist_key = None
        self.vlist_first = {TOC: 0, OCCUR: 0}  # 右端の列(一覧ごと)
        self.vlist_view = ContentView()
        self.vlist_view.cursor_highlight = False
        self.vertical_prefix = None    # 縦書きのCOPYの'ESC'
        self.work_root = os.path.join('/tmp' if os.path.isdir('/tmp') else tempfile.gettempdir(), '.usilic',
                                      'python-%d' % os.getpid())  # HELPの「/tmp/.usilic/」
        self.work_files = []
        self.last_shell_insert = ''    # ^Wで出し直す(^J^W・^Jw)
        self.tool_directory = ''
        self.bookmark_place = None     # ^Xrmで置く所
        self.list_stack = []           # 一覧の中から開いた一覧(^Gで前の一覧へ)
        self.prompt_ctrl_o = False     # プロンプトの^O(^O^Y)
        self.occur_byte_count = self.info_byte_count = 0

    # --- 画面 ---
    def views(self):
        return [self.directory_view, self.file_view, self.shell_view, self.list_view, self.man_view, self.du_view,
                self.locate_view, self.key_view, self.help_view, self.toc_view, self.part_toc_view, self.part_view,
                self.occur_view, self.info_view, self.vertical_view, self.vlist_view] + \
            [a.view for a in self.archives]

    def active_view(self):
        return {
            PART_TOC: self.part_toc_view, PART: self.part_view, OCCUR: self.occur_view, INFO: self.info_view,
            SHELL: self.shell_view, FILE: self.file_view, LIST: self.list_view, MAN: self.man_view,
            DU: self.du_view, LOCATE: self.locate_view, KEY: self.key_view, HELP: self.help_view,
            TOC: self.toc_view, ARCHIVE: self.archives[-1].view if self.archives else self.directory_view,
        }.get(self.current_mode, self.directory_view)

    def refresh_shell_view(self, follow=True):
        """SHELLの行を作り直す。followなら最後(プロンプト)へ(出力が来た時)。"""
        at_end = self.shell_view.cursor_index >= len(self.shell_view.lines) - 1
        self.shell_view.set_lines(self.shell.view_lines(self.wrap_width(), self.command_line.text))
        if follow or at_end:
            self.shell_view.go_to_bottom()

    def layout(self):
        rows, columns = self.display.refresh_size()
        for view in self.views():
            view.set_height(rows - (0 if self.mode_line_hidden else 1))
        self.file_mode.rewrap(self.wrap_width())
        if self.shell.alive():
            self.shell.on_resize(rows - 1, columns)
            self.refresh_shell_view()

    def wrap_width(self):
        """折り返しの幅(桁いっぱい、C++版と同じ)。"""
        return max(1, self.display.columns)

    @staticmethod
    def position_numbers(view):
        """モードラインの位置(今の行, 行の数)。行が元の行の番号を持っていれば(折り返したFILE等)その番号で数える
        (C++のContentView::applyRawLineNumbers())。"""
        lines = view.lines
        current, total = (view.cursor_index + 1 if lines else 0), len(lines)
        if lines and lines[0].raw_line_number is not None:
            index = max(0, min(view.cursor_index, len(lines) - 1))
            current = lines[index].raw_line_number if lines[index].raw_line_number is not None else current
            total = lines[-1].raw_line_number if lines[-1].raw_line_number is not None else total
        return current, total

    def position_text(self, view, fixed=False):
        if self.notice:
            return self.notice
        return (mode_line.format_fixed_width if fixed else mode_line.format)(*self.position_numbers(view))

    def mode_line_text(self):
        """モードライン(ContentView::ModeLineFields)。マクロの記録中はモード名をMACROに。"""
        text = self.mode_line_body()
        if self.macro_recording is not None and self.prompt is None:
            return 'MACRO   ' + text
        return text

    def mode_line_body(self):
        """モードライン(ContentView::ModeLineFields)。欄の間は空白3つ。"""
        view = self.active_view()
        if self.vertical_selecting():
            v = self.vertical
            return 'COPY   ' + ('Record?(y/n)' if v.asking else '^@' if v.anchor is not None else '---') + (
                '   ' + self.notice if self.notice else '')
        if self.copy is not None:
            return 'COPY   ' + ('^@' if self.copy.anchor is not None and not self.copy.rectangle else
                                '^X␣' if self.copy.anchor is not None else '---') + (
                '   ' + self.notice if self.notice else '')
        if self.prompt is not None:
            p = self.prompt
            head = [p.label] + ([mode_line.format(view.cursor_index + 1, len(view.lines))] if p.show_position else [])
            return '   '.join(head + [localized(p.message) + p.editor.text]) if p.message else \
                '   '.join(head + [p.editor.text])
        if self.goto_digits is not None:
            return 'Goto line: ' + self.goto_digits
        if self.grep_prompt is not None:
            syntax, editor = self.grep_prompt
            if syntax == FIXED:  # インクリメンタル: 「FGREP 1/11 打った字」
                return '   '.join(['FGREP', self.grep.position_text(), editor.text])
            return 'EGREP   ' + editor.text
        if self.grep_active:
            name, size = self.mode_line_target()
            return self.fit_mode_line_tail([self.grep.label(), self.grep.position_text(), str(size),
                                            '(' + self.grep.label().lower() + ')', name])
        position = self.position_text(view)
        directory = self.directory_mode.current_directory
        mode = self.current_mode
        if mode == SHELL:
            pos = self.notice or mode_line.format_fixed_width(view.cursor_index + 1, len(view.lines))
            return self.fit_mode_line(['SHELL', pos, str(self.shell.byte_count), '(bash)',
                                       self.shell.current_directory or directory])
        if mode == FILE:
            fm = self.file_mode
            if self.epub is not None and fm.current_file == self.epub.path:
                if self.vertical_active() and self.vertical is not None:
                    position = self.notice or self.vertical.position_text()
                    return self.fit_mode_line([self.epub_name, position, str(fm.byte_count), 'EPUB 縦書き'])
                return self.fit_mode_line([self.epub_name, position, str(fm.byte_count), 'EPUB'])
            pos = self.notice or (mode_line.format_fixed_width(*self.position_numbers(view))
                                  if self.pager_launch else position)
            name = fm.current_file if self.pager_launch else os.path.basename(fm.current_file)
            tail = PROGRAM_LABEL if self.pager_launch else (fm.tail or directory)
            label = [fm.label] if fm.label else []
            return self.fit_mode_line([name, pos, str(fm.byte_count)] + label + [tail])
        if mode == ARCHIVE:
            a = self.archives[-1]
            return self.fit_mode_line([a.display_name, position, str(a.byte_count), a.badge, directory])
        if mode == LIST:
            item = self.list_mode.selected_item() or ''
            return self.fit_mode_line_tail([self.list_mode.label, position, item])
        if mode == MAN:
            return self.fit_mode_line([self.man_mode.current_page, position, str(self.man_mode.byte_count),
                                       '(man)', 'MAN'])
        if mode == DU:
            return self.fit_mode_line_tail(['DU', position, str(self.du_mode.byte_count), self.du_mode.current_path])
        if mode == LOCATE:
            return self.fit_mode_line(['LOCATE', position, str(self.locate_mode.byte_count), directory])
        if mode == KEY:
            return self.fit_mode_line_tail(['KEY', position, str(self.key_byte_count), directory])
        if mode == HELP:
            return self.fit_mode_line_tail(['HELP', position, str(self.help_byte_count), directory])
        if mode == TOC:
            size = os.path.getsize(self.epub.path) if self.epub and os.path.exists(self.epub.path) else 0
            if self.vertical_list_active():  # 縦書きの章の一覧: 位置は「選んでいる章/章の数」
                position = self.notice or '%02d/%02d' % (view.cursor_index + 1, max(1, len(view.lines)))
                return self.fit_mode_line([self.epub_name, position, str(size), '縦書き', directory])
            return self.fit_mode_line([self.epub_name, position, str(size), directory])
        if mode == PART_TOC:
            return self.fit_mode_line(['PART', position, str(self.part.byte_count), '(egrep)', directory])
        if mode == PART:
            return self.fit_mode_line(['PART', position, str(self.part.byte_count), 'PART: ' + self.part.pattern])
        if mode == OCCUR and self.occur_global:
            return self.fit_mode_line(['EPUB', position, str(self.occur_byte_count), '(marks)', self.epub_name])
        if mode == OCCUR:
            label = 'FGREP' if self.occur_syntax == FIXED else 'EGREP'
            return self.fit_mode_line([label, position, str(self.occur_byte_count), '(' + label.lower() + ')',
                                       directory])
        if mode == INFO:
            return self.fit_mode_line(['INFO', position, str(self.info_byte_count), directory])
        label = PROGRAM_LABEL if not self.directory_mode.mode_stack else ''
        sort = self.directory_mode.sort_key if self.directory_mode.sort_label_shown else ''
        return self.fit_mode_line([x for x in [directory, position, str(self.directory_mode.byte_count), '(ls)',
                                               sort, label] if x])

    def mode_line_target(self):
        """FGREP/EGREPのモードラインの最後の欄(探している物の名前)と大きさ。"""
        if self.current_mode == FILE:
            return self.file_mode.current_file, self.file_mode.byte_count
        if self.current_mode == SHELL:
            return 'SHELL', self.shell.byte_count
        if self.current_mode == MAN:
            return self.man_mode.current_page, self.man_mode.byte_count
        if self.current_mode == ARCHIVE:
            return self.archives[-1].display_name, self.archives[-1].byte_count
        return self.directory_mode.current_directory, self.directory_mode.byte_count

    def fit_mode_line_tail(self, fields):
        """最後の欄(パス)の途中を「...」で縮める版(FGREP/EGREPのモードライン)。"""
        rest = utf8.text_width('   '.join(fields[:-1])) + 3
        room = self.display.columns - 1 - rest
        name = fields[-1]
        if utf8.text_width(name) > room and room > 8:
            keep = room - 3
            head = keep // 2
            name = name[:head] + '...' + name[len(name) - (keep - head):]
        return '   '.join(fields[:-1] + [name])

    def fit_mode_line(self, fields):
        """欄を空白3つでつなぐ。幅に収まらなければ、最後の欄(ディレクトリ)を「...」+終わりの方に、それでも
        収まらなければ最初の欄(名前)の途中を「...」で縮める(C++版の「...ratchpad/home/t/」の形)。"""
        fields = list(fields)
        limit = self.display.columns - 1
        if len(fields) > 1 and utf8.text_width('   '.join(fields)) > limit:
            last = fields[-1]
            room_last = limit - utf8.text_width('   '.join(fields[:-1])) - 3
            keep = max(8, limit // 4)
            if room_last >= keep:  # 名前が短い: 途中を縮める(「ARC: /private/...home/t/arc.tgz」)
                head = (room_last - 3) // 2
                fields[-1] = last[:head] + '...' + last[len(last) - (room_last - 3 - head):]
            elif utf8.text_width(last) > keep:
                fields[-1] = '...' + last[len(last) - (keep - 3):]
        rest = utf8.text_width('   '.join(fields[1:])) + 3
        room = limit - rest
        name = fields[0]
        if utf8.text_width(name) > room:
            name = self.elide_middle(name, max(room, 8))
        return '   '.join([name] + fields[1:])

    @staticmethod
    def elide_middle(text, width):
        """表示幅widthに収まるよう、真ん中を「...」にする(頭と終わりを残す)。"""
        room = width - 3
        head_width, head = 0, []
        for ch in text:
            w = utf8.display_width_of_codepoint(ord(ch))
            if head_width + w > room // 2:
                break
            head.append(ch)
            head_width += w
        tail_width, tail = 0, []
        for ch in reversed(text):
            w = utf8.display_width_of_codepoint(ord(ch))
            if tail_width + w > room - head_width:
                break
            tail.append(ch)
            tail_width += w
        return ''.join(head) + '...' + ''.join(reversed(tail))

    def marked_rows(self):
        """下線を付ける行(選択)。"""
        if self.current_mode == DIRECTORY:
            return self.directory_mode.marks
        if self.current_mode == ARCHIVE and self.archives:
            return self.archives[-1].marks
        return ()

    def redraw(self):
        view = self.active_view()
        if self.current_mode == FILE and self.in_epub_body() and self.epub.right_to_left:
            self.update_epub_section()
        if self.current_mode == FILE and self.vertical_active():
            self.render_vertical()
            view = self.vertical_view
        elif self.copy is not None and self.vertical_list_active():
            view = self.copy.view
        elif self.vertical_list_active():
            self.render_vertical_list()
            view = self.vlist_view
        section = self.epub_section if view is self.file_view and self.in_epub_body() and \
            self.epub.right_to_left and not self.epub_note_active() else None
        visible = view.visible_lines()
        marks = self.marked_rows()
        grep_on = self.grep_active or self.grep_prompt is not None
        for row in range(view.height):
            if row < len(visible) and section is not None and \
                    not section[0] <= visible[row].raw_line_number <= section[1]:
                self.display.draw_row(row, '')  # 横書きの範囲の外の行は空に(C++のsetShownIndexRange())
            elif row < len(visible):
                index = view.top_index + row
                line = visible[row]
                reverse = view.cursor_highlight and index == view.cursor_index and not (
                    self.prompt is not None and self.prompt.show_position) and self.copy is None
                runs = list(line.attributes)
                if self.copy is not None:
                    runs += self.copy.runs_for_row(index)
                text = line.text
                if self.current_mode in (LIST, DU, LOCATE) and utf8.text_width(text) > self.display.columns:
                    text = self.elide_middle(text, self.display.columns)  # C++版と同じく途中を「...」に
                    line = Line(text, line.raw_line_number)
                if (grep_on or self.grep.persistent) and index in self.grep.matches and view is self.grep.view:
                    runs += [(s, e, 'match') for s, e in match_spans(line.text, self.grep.pattern, self.grep.syntax)]
                self.display.draw_row_styled(row, text, runs, reverse=reverse, underline=index in marks)
            else:
                self.display.draw_row(row, '')
        if not self.mode_line_hidden or self.prompt is not None or self.notice:
            self.display.draw_mode_line(self.mode_line_text())
        if (self.current_mode == SHELL and self.prompt is None and self.goto_digits is None
                and self.grep_prompt is None and not self.grep_active):
            self.display.show_cursor_at(*self.shell_cursor_position())
        elif view is self.vertical_view and self.prompt is None and self.grep_prompt is None:
            self.display.show_cursor_at(*self.vertical.cursor_screen_position())  # 列カーソルの頭(C++の-cと同じ)
        elif view is self.vlist_view and self.prompt is None and self.grep_prompt is None:
            self.display.show_cursor_at(0, max(0, ev.screen_column(self.vlist_cursor_column() - self.vlist.first,
                                                                   self.display.columns)))
        elif self.prompt is not None or self.grep_prompt is not None or self.goto_digits is not None:
            self.display.show_cursor_at(self.display.rows - 1, self.prompt_cursor_column())
        elif self.copy is not None:
            c = self.copy
            self.display.show_cursor_at(max(0, c.row - c.view.top_index),
                                        utf8.text_width(c.text_at(c.row)[:c.col]))
        elif 0 <= view.cursor_index - view.top_index < view.height:
            # ブロックカーソルはカーソル行の頭に(C++のsyncVisibleCursor()、反転しない画面ではこれが今の行の印)
            self.display.show_cursor_at(view.cursor_index - view.top_index, 0)
        else:
            self.display.hide_cursor()
        self.display.flush()

    def prompt_cursor_column(self):
        """入力中のモードラインの、入力欄のカーソルの桁。"""
        text = self.mode_line_text()
        editor = self.prompt.editor if self.prompt is not None else \
            self.grep_prompt[1] if self.grep_prompt is not None else None
        tail = editor.text[editor.cursor:] if editor is not None else ''
        return max(0, min(self.display.columns - 1, utf8.text_width(text) - utf8.text_width(tail)))

    def shell_cursor_position(self):
        """コマンド行のカーソルの画面の位置(行, 桁)。"""
        view = self.shell_view
        head = (self.shell.prompt if self.shell.prompt_ready else self.shell.pending)
        before = head + self.command_line.text[:self.command_line.cursor]
        width = self.wrap_width()
        pieces = wrap_text(expand_tabs(before), width)
        live_start = len(view.lines) - len(wrap_text(expand_tabs(head + self.command_line.text), width))
        row = live_start + len(pieces) - 1 - view.top_index
        return max(0, min(row, view.height - 1)), utf8.text_width(pieces[-1])

    def save_positions(self):
        """終了の時: 今の画面の位置を覚える(保存はusilic.pyが)。"""
        if self.current_mode == FILE:
            if self.epub_note_active():
                self.leave_epub_note()
            self.file_mode.save_cursor_position()
        self.save_list_positions()
        self.directory_mode.save_current_cursor_position()

    def save_list_positions(self):
        """ARC・DU・MANの位置(SCREEN・MAN、C++のsaveCursorPosition())。"""
        if self.history is None:
            return
        if self.archives:
            a = self.archives[-1]
            self.history.set_position(hist.SCREEN, a.current_archive,
                                      hist.CursorPosition(a.view.cursor_index, len(a.entries), 0, -1))
        if self.du_mode.current_path:
            self.history.set_position(hist.SCREEN, self.du_mode.current_path,
                                      hist.CursorPosition(self.du_view.cursor_index, len(self.du_mode.entries), 0, -1))
        if self.man_mode.current_page:
            self.history.set_position(hist.MAN_PAGE, self.man_mode.current_page,
                                      hist.CursorPosition(self.man_view.cursor_index, len(self.man_view.lines), 0, -1))

    def restore_list_position(self, category, name, view):
        if self.history is None:
            return
        pos = self.history.position_for(category, name)
        if pos is not None:
            view.cursor_index = pos.row
            view.clamp()
            view.top_index = view.cursor_index - view.height // 2
            view.clamp()

    def show_transient_notice(self, text):
        self.notice = localized(text)

    # --- モードの行き来 ---
    def enter_mode(self, mode):
        """新しいモードへ(今のモードを^Gの戻り先に積む)。同じモードなら積まない(C++と同じ)。"""
        if self.current_mode != mode:
            self.save_mode_position()
            self.return_stack.append(self.current_mode)
        self.current_mode = mode

    def save_mode_position(self):
        if self.current_mode == FILE:
            self.file_mode.save_cursor_position()
        elif self.current_mode in (ARCHIVE, DU, MAN):
            self.save_list_positions()

    def go_back(self):
        """^G: 積んだ戻り先へ(無ければ(ls))。"""
        self.save_mode_position()
        self.current_mode = self.return_stack.pop() if self.return_stack else DIRECTORY
        if self.current_mode == ARCHIVE and not self.archives:
            self.current_mode = DIRECTORY
        return True

    def enter_directory_listing(self, path, keep_stack=False):
        """(ls)へ(ディレクトリを選んだ時は^Gの戻り先を空にする、keep_stackならLOCATE等からの1回目)。"""
        if self.current_mode != DIRECTORY:
            self.save_mode_position()
            if keep_stack:
                self.return_stack.append(self.current_mode)
            else:
                self.return_stack.clear()
        elif not keep_stack:
            self.return_stack.clear()
        if not self.directory_mode.enter_directory(path):
            self.show_transient_notice('Cannot list: ' + path)
            return False
        self.current_mode = DIRECTORY
        return True

    # --- 起動 ---
    def start_directory_listing(self, path):
        if not self.directory_mode.enter_directory(path):
            return False
        self.current_mode = DIRECTORY
        return True

    def current_resumable_name(self):
        """終わる時に見ていた物(次に引数無しで起動した時に開き直す、C++のcurrentResumableName()): FILEのファイル、
        fileを分けたPART(「パス "文字列"」)、一番外のアーカイブ(ARCで見ていた.epubは「パス/」)、他は(ls)のディレクトリ。"""
        mode = self.current_mode
        name = self.file_mode.current_file
        if mode == FILE and name and os.path.isabs(name) and os.path.isfile(name):
            return name
        if mode in (PART_TOC, PART) and self.part.source_name and os.path.isfile(self.part.source_name):
            return '%s "%s"' % (self.part.source_name, self.part.pattern)
        if mode == ARCHIVE and len(self.archives) == 1:
            path = self.archives[0].current_archive
            return path + '/' if is_epub_file(path) else path
        return self.directory_mode.current_directory

    def start_resuming_session(self, last, fallback):
        """引数無しの起動: 前回終わった時に見ていた物から始める(C++のstartResumingSession())。開けなければその所の
        (ls)。名前が無ければDIRの履歴の最新の(外から起動した物は除く)ディレクトリ。"""
        spec = self.part_spec(last) if last else None
        if spec and os.path.isfile(spec[0]):
            if self.start_directory_listing(os.path.dirname(spec[0]) or fallback):
                self.reopen_part(last)
                return True
        if last.endswith('/') and is_epub_file(last[:-1]) and os.path.isfile(last[:-1]):
            if self.start_directory_listing(os.path.dirname(last[:-1])):
                self.open_archive(last[:-1])
                return True
        if last and os.path.isfile(last):
            if self.start_directory_listing(os.path.dirname(last) or fallback):
                if is_epub_file(last):
                    self.open_epub(last)
                elif not (self.configuration.archive_for(last) and self.open_archive(last)):
                    self.open_file(last)
                return True
        if not last:
            recent = [d for d in self.ring(hist.RECENT_DIRECTORY) if not d.endswith(hist.USILIC_TAG)]
            if recent and os.path.exists(recent[-1]):
                fallback = recent[-1]
        directory = last or fallback
        while directory and not os.path.isdir(directory):  # 無くなっていれば、ある所まで上へ
            parent = os.path.dirname(directory.rstrip('/'))
            directory = parent if parent != directory else ''
        return self.start_directory_listing(directory or fallback)

    def start_pager(self, files, stdin_stream=None):
        """引数のfile(複数ならn/p/x/f)または標準入力(Application::startPagerFileList())。"""
        self.pager_launch = True
        self.current_mode = FILE
        if stdin_stream is not None:
            self.pager_files = ['(stdin)']
            return self.file_mode.load_stream(stdin_stream, '(stdin)', self.wrap_width(), self.file_view.height)
        self.pager_files = files
        self.pager_index = 0
        return self.open_pager_file(0)

    def open_pager_file(self, index):
        self.pager_index = index
        path = os.path.abspath(self.pager_files[index])
        if is_epub_file(path) and self.open_epub(path, push=False):
            return True
        if is_markdown_file(path) and self.open_markdown(path, push=False):
            return True
        return self.file_mode.load_file(path, self.wrap_width(), self.file_view.height)

    # --- ファイルを開く ---
    def resolve_path(self, text, base=None):
        """入力のパス: ~は家、相対は(ls)のディレクトリ基準。"""
        text = os.path.expanduser(text.strip())
        if not os.path.isabs(text):
            text = os.path.join(base or self.directory_mode.current_directory or os.getcwd(), text)
        return os.path.normpath(text) + ('/' if text.endswith('/') and len(text) > 1 else '')

    def open_path(self, path, keep_stack=True):
        """FILE(^Xf)・一覧から: ディレクトリ・アーカイブ・EPUB・fileのどれかとして開く。"""
        if os.path.isdir(path):
            return self.enter_directory_listing(path, keep_stack=keep_stack)
        if not os.path.exists(path):
            self.show_transient_notice("Can't open " + path)
            return False
        return self.open_file_entry(path)

    def open_file_entry(self, path, tail=None):
        """(ls)の^Mでfileを開く(EPUB・ARC・LAUNCH・FILTER・バイナリ、Application::openSelectedEntry())。"""
        if not os.access(path, os.R_OK):
            self.show_transient_notice('Permission denied')
            return False
        if is_epub_file(path):
            return self.open_epub(path)
        launch = self.configuration.launch_for(path)
        if launch:
            return self.run_external(launch + [path])
        if self.configuration.archive_for(path):
            if self.open_archive(path):
                return True
        return self.open_file(path, tail=tail)

    def open_file(self, path, tail=None, streaming=False, raw=False):
        """fileをFILEで(FILTERの出力、バイナリはfile(1)の結果)。streamingは^O^M(画面の分だけ読む)。"""
        width = self.wrap_width()
        program = None if raw else self.configuration.filter_for(path)
        self.remember_previous_file()
        if program:
            try:
                result = subprocess.run(program + [path], capture_output=True)
            except OSError:
                self.show_transient_notice('Cannot run: ' + program[0])
                return False
            self.file_mode.load_bytes(result.stdout, path, width)
            self.file_mode.label = '(' + program[0] + ')'
        else:
            try:
                size = os.path.getsize(path)
                with open(path, 'rb') as f:
                    head = f.read(0x10000)
            except OSError:
                self.show_transient_notice('Cannot read: ' + path)
                return False
            offending = None if raw else self.configuration.looks_binary(head)
            if offending is not None:
                return self.show_binary_info(path, offending, tail)
            if not raw and is_markdown_file(path):
                return self.open_markdown(path, tail)
            if not self.file_mode.load_file(path, width, self.file_view.height):
                self.show_transient_notice('Cannot read: ' + path)
                return False
            if not streaming and size <= self.configuration.buffer_limit_bytes():
                self.file_mode.load_all()
            elif not streaming:
                self.show_transient_notice('Not enough buffer')
        self.file_mode.tail = tail
        self.file_mode.record_history = not (streaming or raw)
        self.file_mode.restore_from_history()
        self.enter_mode(FILE)
        return True

    def open_markdown(self, path, tail=None, push=True):
        """.md・.markdown: 文字装飾を付けて(C++のmarkdownToHelpMarkup()+expandHelpMarkup()、^[^Mは生のまま)。"""
        try:
            with open(path, 'rb') as f:
                source = f.read(self.configuration.buffer_limit_bytes()).decode('utf-8', 'replace')
        except OSError:
            self.show_transient_notice('Cannot read: ' + path)
            return False
        self.file_mode.set_rendered(path, render_markdown(source, self.wrap_width()), os.path.getsize(path),
                                    self.wrap_width(), renderer=lambda w: render_markdown(source, w))
        self.file_mode.tail = tail
        self.file_mode.record_history = not self.pager_launch
        self.file_mode.restore_from_history()
        if push:
            self.enter_mode(FILE)
        else:
            self.current_mode = FILE
        return True

    def show_binary_info(self, path, offending, tail=None, data=None):
        """FileMode::binaryTypeText(): 「(^@) Mach-O 64-bit executable」を太字で。"""
        try:
            if data is None:
                result = subprocess.run(['file', '-b', path], capture_output=True)
            else:
                result = subprocess.run(['file', '-b', '-'], input=data, capture_output=True)
            kind = result.stdout.decode('utf-8', 'replace').split('\n')[0]
        except OSError:
            kind = "cannot execute `file'"
        text = '(^%s) %s' % (chr(0x40 + offending), kind)
        self.file_mode.set_rendered(path, [Line(text, 1, attributes=[(0, len(text), 'bold')])], len(text) + 1,
                                    self.wrap_width())
        self.file_mode.tail = tail
        self.file_mode.record_history = False
        self.enter_mode(FILE)
        return True

    def remember_previous_file(self):
        """^J^Jの行き先(今のfile)。"""
        if self.current_mode == FILE and self.file_mode.current_file and self.epub is None:
            self.previous_file = self.file_mode.current_file

    def open_selected_entry(self):
        """(ls)の^M(Application::openSelectedEntry())。"""
        entry = self.directory_mode.selected_entry()
        if entry is None:
            return False
        if entry.name in ('./', '.'):
            return self.directory_mode.enter_directory(self.directory_mode.current_directory)
        target = os.path.normpath(os.path.join(self.directory_mode.current_directory, entry.name))
        if entry.is_directory or os.path.isdir(target):
            self.directory_mode.mode_stack.append((self.directory_mode.current_directory, self.directory_view.cursor_index))
            if entry.name in ('../', '..'):
                self.directory_mode.mode_stack.clear()  # 親へは^Gのスタックを消す(C++のresetNavigationToFloor())
            self.return_stack.clear()
            if not self.directory_mode.enter_directory(target):
                self.show_transient_notice('Cannot list: ' + target)
                return False
            return True
        return self.open_file_entry(target)

    def run_external(self, argv, cwd=None, env=None):
        """LAUNCH・TOOL・EDIT: 画面を抜けてプログラムを動かし、終わったら戻る。"""
        self.display.suspend()
        try:
            subprocess.call(argv, cwd=cwd, env=env)
        except OSError:
            pass
        finally:
            self.display.resume()
            self.layout()
        return True

    # --- ARC ---
    def open_archive(self, path, display_name=None, nested=False):
        archive = ArchiveMode(ContentView(), self.configuration)
        archive.view.set_height(self.directory_view.height)
        self.show_transient_notice('Loading archive...')
        self.redraw()
        self.notice = None
        if not archive.load_archive(path, display_name):
            return False
        if not nested:
            self.archives = []
            if self.history is not None:
                self.history.push_recent(hist.RECENT_FILE, path)
        self.archives.append(archive)
        self.restore_list_position(hist.SCREEN, path, archive.view)
        self.enter_mode(ARCHIVE)
        return True

    def open_archive_member(self):
        """ARCの^M: 中のfileを表示(中のアーカイブはさらに一覧)。"""
        archive = self.archives[-1]
        entry = archive.selected_entry()
        if entry is None or entry.is_directory:
            return False
        data = archive.extract(entry)
        if data is None:
            self.show_transient_notice("Can't open " + entry.name)
            return False
        display = archive.display_name + '/' + entry.name
        if self.configuration.archive_for(entry.name):
            suffix = os.path.basename(entry.name)
            fd, temp = tempfile.mkstemp(prefix='usilic-', suffix='-' + suffix)
            with os.fdopen(fd, 'wb') as f:
                f.write(data)
            if self.open_archive(temp, display, nested=True):
                return True
        tail = 'ARC: ' + archive.display_name
        offending = self.configuration.looks_binary(data[:0x10000])
        if offending is not None:
            return self.show_binary_info(entry.name, offending, tail, data=data)
        self.remember_previous_file()
        self.file_mode.load_bytes(data[:self.configuration.buffer_limit_bytes()], entry.name, self.wrap_width())
        self.file_mode.tail = tail
        self.file_mode.record_history = False
        self.enter_mode(FILE)
        return True

    def archive_back(self):
        """ARCの^G: 入れ子なら外のアーカイブへ、でなければ戻り先へ。"""
        if len(self.archives) > 1:
            inner = self.archives.pop()
            if os.path.basename(inner.current_archive).startswith('usilic-'):
                try:
                    os.remove(inner.current_archive)
                except OSError:
                    pass
            return True
        self.save_list_positions()
        return self.go_back()

    def start_archive_extract(self):
        """^Oa: 展開先を尋ねる(ARCでは選択か全体、(ls)ではカーソルのアーカイブ全体)。"""
        if self.current_mode == ARCHIVE:
            archive = self.archives[-1]
            path, kind = archive.current_archive, archive.kind
            members = [e.name for e in archive.target_entries()]
            names = members or [e.name for e in archive.entries]
        elif self.current_mode == DIRECTORY:
            entry = self.directory_mode.selected_entry()
            if entry is None or len(self.directory_mode.marks) > 1:
                return False
            path = os.path.join(self.directory_mode.current_directory, entry.name)
            found = self.configuration.archive_for(path)
            if not found:
                return False
            kind, members = found[0], []
            probe = ArchiveMode(ContentView(), self.configuration)
            names = [e.name for e in probe.entries] if probe.load_archive(path) else []
        else:
            return False

        def submit(text):
            destination = self.resolve_path(text) if text else self.data_directory()
            if not os.path.isdir(destination):
                self.show_transient_notice(destination + ': No such directory')
                return False
            argv = ArchiveMode.extract_argv(path, kind, members, destination)
            if argv is None:
                self.show_transient_notice('Extract not supported for this archive type')
                return False
            return self.start_shell_run(argv, destination, ArchiveMode.top_level_names(names))
        self.prompt = pr.Prompt(pr.ARCHIVE, on_submit=submit)
        return True

    # --- EPUB ---
    def open_epub(self, path, name=None, push=True):
        """.epubを本として開く(前回の位置があれば本文、無ければ最初の章、C++のopenEpubQuietly())。"""
        book = EpubBook()
        self.show_transient_notice('Reading...')
        self.redraw()
        self.notice = None
        if not book.load(path):
            self.show_transient_notice('Not a book: ' + path)
            return False
        self.remember_previous_file()
        self.epub = book
        self.epub_name = name or os.path.basename(path)
        width = self.wrap_width()
        lines, self.epub_toc_lines = book.render(width)

        self.epub_lines, self.epub_link_stack = lines, []

        def renderer(w):
            self.epub_lines, self.epub_toc_lines = book.render(w)
            if not self.epub_chapter_mode:
                return self.epub_lines
            start, end = self.chapter_bounds(self.epub_chapter)
            return self.epub_lines[start:end]
        self.file_mode.set_rendered(path, lines, os.path.getsize(path), width, renderer=renderer)
        self.file_mode.record_history = name is None and not self.pager_launch
        self.toc_view.set_lines([Line(title, i + 1) for i, title in enumerate(book.chapter_titles)])
        self.toc_view.cursor_index = self.toc_view.top_index = 0
        first_time = self.history is None or self.history.position_for(hist.FILE, path) is None
        self.file_mode.restore_from_history()
        if push:
            self.enter_mode(FILE)
        else:
            self.current_mode = FILE
        if self.epub_chapter_mode:
            line = self.file_view.cursor_index
            self.epub_chapter = self.chapter_of_line(line)
            start = self.apply_epub_chapter()
            self.center_on(self.file_view, line - start)
        if first_time and len(self.epub_toc_lines) > 1:
            self.go_to_chapter(0)
        self.sync_toc_cursor()
        return True

    def go_to_chapter(self, index):
        if not self.epub_toc_lines:
            self.show_transient_notice('No chapter')
            return False
        index = max(0, min(index, len(self.epub_toc_lines) - 1))
        self.epub_chapter = index
        offset = self.apply_epub_chapter()
        self.file_view.cursor_index = self.file_view.top_index = self.epub_toc_lines[index] - offset
        self.file_view.clamp()
        self.toc_view.cursor_index = index
        self.toc_view.ensure_cursor_visible()
        return True

    def current_chapter(self):
        """本文のカーソルがある章(目次の番号)。"""
        return self.chapter_of_line(self.global_epub_line())

    def sync_toc_cursor(self):
        if self.epub_toc_lines:
            self.toc_view.cursor_index = self.current_chapter()
            self.toc_view.ensure_cursor_visible()

    def in_epub_body(self):
        return self.current_mode == FILE and self.epub is not None and self.file_mode.current_file == self.epub.path

    # --- 一覧(履歴) ---
    def show_list(self, items, label, on_choose, sorted_view, prompt_kind=None, badge_for=None):
        if not items:
            self.show_transient_notice('No string')
            return False
        if self.current_mode == LIST:
            self.list_stack.append(self.list_mode.snapshot())
        else:
            self.list_stack = []
        self.list_mode.show(items, sorted_view, label, on_choose, prompt_kind, badge_for)
        self.enter_mode(LIST)
        return True

    def ring(self, category):
        return self.history.recent_entries(category) if self.history is not None else []

    def file_badge(self, item):
        """fileHistoryBadges(): .epubは(epub)、アーカイブは(tar)等。"""
        if is_epub_file(item):
            return '(epub)'
        found = self.configuration.archive_for(item)
        return found[3] if found else ''

    @staticmethod
    def strip_tags(item):
        """「パス (usilic)」「パス "パターン"」の印を外したパス。"""
        if item.endswith(hist.USILIC_TAG):
            item = item[:-len(hist.USILIC_TAG)]
        if len(item) >= 4 and item.endswith('"'):
            at = item.rfind(' "', 0, len(item) - 1)
            if at >= 0:
                item = item[:at]
        return item

    def start_file_list(self, filter_fn=None, label='FILE', on_choose=None):
        items = [i for i in self.ring(hist.RECENT_FILE)
                 if filter_fn is None or (not i.endswith(hist.USILIC_TAG) and filter_fn(self.strip_tags(i)))]
        return self.show_list(items, label, on_choose or (lambda i: self.open_path(self.strip_tags(i))), True,
                              pr.FILE, self.file_badge)

    def start_prompt(self, kind, text='', on_submit=None, show_position=False):
        self.prompt = pr.Prompt(kind, text, on_submit, show_position)
        return True

    def prompt_handlers(self, kind):
        """プロンプトの種類ごとの^Mの処理(Application::executePromptSubmission())。"""
        return {
            pr.FILE: lambda t: self.open_path(self.resolve_path(t)) if t else False,
            pr.DIRECTORY: lambda t: self.enter_directory_listing(self.resolve_path(t) + '/', keep_stack=True)
            if t else False,
            pr.MAN: self.execute_man,
            pr.LOCATE: self.execute_locate,
            pr.EPUB: self.execute_epub_input,
        }.get(kind)

    def start_history_prompt(self, kind):
        return self.start_prompt(kind, '', self.prompt_handlers(kind))

    def start_list_from_prompt(self):
        """プロンプトの^R: 入力順の一覧(EGREPはusilicrcのGREPの例を先頭に)。"""
        p = self.prompt
        if p.history_category is None:
            return True
        items = list(self.ring(p.history_category))
        if p.kind in (pr.EGREP, pr.PART, pr.OCCUR_REGEX, pr.MARK_REGEX):
            items = list(self.configuration.grep_examples) + items
        if not items:
            self.show_transient_notice('No string')
            return True
        self.prompt = None
        self.list_prompt = p

        def choose(item):
            back = self.list_prompt
            self.list_prompt = None
            self.go_back()
            back.editor.set_text(self.strip_tags(item))
            self.restore_prompt(back)
            return True
        return self.show_list(items, p.label, choose, False, prompt_kind=p.kind)

    def restore_prompt(self, p):
        if p.kind in (pr.FGREP, pr.EGREP):
            self.grep_prompt = (FIXED if p.kind == pr.FGREP else REGEX, p.editor)
        else:
            self.prompt = p

    # --- MAN・LOCATE・DU ---
    def execute_man(self, name):
        name = name.strip()
        if not name:
            return False
        self.show_transient_notice('Reading...')
        self.redraw()
        self.notice = None
        if not self.man_mode.load_page(name, self.wrap_width()):
            self.show_transient_notice('No manual entry for ' + name)
            return False
        if self.history is not None:
            self.history.push_recent(hist.RECENT_MAN_PAGE, name)
        self.restore_list_position(hist.MAN_PAGE, name, self.man_view)
        self.enter_mode(MAN)
        return True

    def execute_locate(self, query):
        if not query:
            return False
        if self.history is not None:
            self.history.push_recent(hist.RECENT_LOCATE_QUERY, query)
        if query != self.locate_mode.query or not self.locate_mode.results:
            self.show_transient_notice('Reading...')
            self.redraw()
            self.notice = None
            if not self.locate_mode.search(query):
                return False
        if self.current_mode == LOCATE:  # LOCATEの中で^J^L: 途中の戻り先は捨てる(C++と同じ)
            return True
        self.enter_mode(LOCATE)
        return True

    def start_locate_prompt(self):
        last = self.ring(hist.RECENT_LOCATE_QUERY)
        text = self.locate_mode.query or (last[-1] if last else '')
        return self.start_prompt(pr.LOCATE, text, self.execute_locate)

    def start_du(self, path):
        """^Jd: duを動かす(^Gで中断、それまでの結果を出す)。"""
        argv = self.du_mode.command_argv_for(path)
        output, _ = self.run_abortable(argv, 'Disk usage...')
        if output is None:
            self.show_transient_notice('Cannot run: ' + argv[0])
            return False
        self.save_list_positions()
        self.du_mode.load_usage_from_output(path, output.decode('utf-8', 'surrogateescape'))
        self.restore_list_position(hist.SCREEN, path, self.du_view)
        self.enter_mode(DU)
        return True

    def run_abortable(self, argv, label):
        """argvを動かし、出力を集める。その間に^Gを押すとやめ、そこまでの出力を返す。"""
        self.show_transient_notice(label)
        self.redraw()
        self.notice = None
        try:
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except OSError:
            return None, False
        chunks, aborted, limit = [], False, self.configuration.buffer_limit_bytes()
        while True:
            readable, _, _ = select.select([proc.stdout, self.key_input.fd], [], [], 0.2)
            if self.key_input.fd in readable:
                key = self.key_input.read_key(timeout=0)
                if key and key[0] == CTRL_G:
                    proc.kill()
                    aborted = True
            if proc.stdout in readable:
                data = os.read(proc.stdout.fileno(), 65536)
                if not data:
                    break
                chunks.append(data)
                if sum(len(c) for c in chunks) > limit:
                    proc.kill()
                    self.show_transient_notice('Not enough buffer')
            elif aborted:
                break
        proc.wait()
        return b''.join(chunks), aborted

    def du_or_locate_choose(self, path):
        """DU・LOCATEの^M: ディレクトリは(ls)(^Gで一覧へ戻れる)、fileは開く。"""
        if not path:
            return False
        if os.path.isdir(path):
            return self.enter_directory_listing(path, keep_stack=True)
        if not os.path.exists(path):
            self.show_transient_notice("Can't open " + path)
            return False
        return self.open_file_entry(path, tail=os.path.dirname(path) + '/')

    def execute_epub_input(self, text):
        """^Je: カレントディレクトリの.epubだけ(C++と同じ)。作品番号・作品のURLはnarou2epubで、他のURLは^Owで。"""
        text = text.strip()
        if re.fullmatch(r'[nN][0-9]{4,}[a-zA-Z]+', text) or re.fullmatch(r'[0-9]{10,}', text) or re.match(
                r'https?://(ncode|novel18|yomou|noc)\.syosetu\.com/[nN][0-9]{4,}[a-zA-Z]+|https?://kakuyomu\.jp/works/'
                r'[0-9]+|https?://www\.aozora\.gr\.jp/cards/', text):
            if self.history is not None:
                self.history.push_recent(hist.RECENT_FILE, text)
            return self.run_narou2epub(text)
        if re.match(r'https?://', text):
            return self.open_url(text)
        path = self.resolve_path(text)
        if not is_epub_file(path) or not os.path.isfile(path):
            self.show_transient_notice('No such EPUB: ' + text)
            return False
        return self.open_epub(path)

    # --- 選択とCP/MV/RM ---
    def toggle_mark(self):
        view = self.active_view()
        marks = self.marked_rows()
        if not isinstance(marks, set):
            return False
        index = view.cursor_index
        if self.current_mode == DIRECTORY:
            entry = self.directory_mode.selected_entry()
            if entry is None or entry.name in ('./', '../', '.', '..'):
                return False
            self.directory_mode.temporary_marks = False
        if index in marks:
            marks.discard(index)
        else:
            marks.add(index)
        return True

    def mark_all(self, on):
        if self.current_mode == DIRECTORY:
            self.directory_mode.marks = set(i for i, e in enumerate(self.directory_mode.entries)
                                            if e.name not in ('./', '../')) if on else set()
            self.directory_mode.temporary_marks = False
        elif self.current_mode == ARCHIVE:
            self.archives[-1].marks = set(range(len(self.archives[-1].entries))) if on else set()
        return True

    def target_paths(self):
        d = self.directory_mode
        return [os.path.join(d.current_directory, d.entries[i].name.rstrip('/'))
                for i in sorted(d.marks) if i < len(d.entries)]

    def data_directory(self):
        """~/.usilic/data/(行き先を入れずに^Mした時、C++のdataDirectory())。"""
        path = os.path.join(os.path.expanduser('~'), '.usilic', 'data')
        os.makedirs(path, exist_ok=True)
        return path + '/'

    def previous_directory(self):
        """^W: 以前のディレクトリ(DIRの履歴で今のディレクトリより前の物)。"""
        here = self.directory_mode.current_directory
        for item in reversed(self.ring(hist.RECENT_DIRECTORY)):
            if item != here and not item.endswith(hist.USILIC_TAG):
                return item
        return ''

    def start_cp_mv_rm(self, kind):
        """^Oc・^Om・^Or(何も選択していない時は動かない、C++のstartCopyPrompt()等)。"""
        if self.current_mode != DIRECTORY or not self.directory_mode.marks:
            return False
        targets = self.target_paths()

        def submit(text):
            if kind == pr.REMOVE:
                if text != 'y':
                    return False
                if not all(self.is_writable(t) for t in targets):
                    self.show_transient_notice('Permission denied')
                    return False
                return self.start_shell_run(self.configuration.command_for('Rm') + targets, None, [])
            destination = self.resolve_path(text) if text else self.data_directory()
            if kind == pr.MOVE and not all(self.is_writable(t) for t in targets):
                self.show_transient_notice('Permission denied')
                return False
            role = 'Cp' if kind == pr.COPY else 'Mv'
            names = [os.path.basename(t) for t in targets]
            return self.start_shell_run(self.configuration.command_for(role) + targets + [destination],
                                        destination, names)
        return self.start_prompt(kind, '', submit, show_position=True)

    @staticmethod
    def is_writable(path):
        """CpMvRmMode::isWritable(): シンボリックリンク、書ける、自分の物。"""
        if os.getuid() == 0 or os.path.islink(path) or os.access(path, os.W_OK):
            return True
        try:
            return os.stat(path).st_uid == os.getuid()
        except OSError:
            return False

    def start_shell_run(self, argv, destination, names, on_done=None):
        """SHELLでargvを動かし、終わりの印で結果を見る(Application::startCpMvRmShellRun())。"""
        if not self.ensure_shell():
            return False
        self.pending_shell_run = {'destination': destination, 'names': names, 'on_done': on_done,
                                  'origin': self.directory_mode.current_directory, 'start': len(self.shell.lines)}
        self.directory_mode.marks = set()
        self.enter_mode(SHELL)
        command = ' '.join(shlex.quote(a) for a in argv) + '; echo ' + shlex.quote(CPMVRM_MARKER) + '$?'
        self.shell.submit_line(command)
        self.refresh_shell_view()
        return True

    def check_shell_run(self):
        """SHELLの出力に終わりの印が来たら、行き先の(ls)へ(成功した時だけ)。"""
        run = self.pending_shell_run
        if run is None:
            return
        for i in range(len(self.shell.lines) - 1, -1, -1):
            line = self.shell.lines[i]
            if line.startswith(CPMVRM_MARKER):
                del self.shell.lines[i]
                break
        else:
            return
        self.pending_shell_run = None
        status = line[len(CPMVRM_MARKER):].strip()
        if status != '0':
            return  # 失敗: SHELLに残る(経過とエラーが見える)
        if run['on_done'] is not None:  # narou2epub・url2pdf: 出力の最後の行(作ったファイル)
            output = [l for l in self.shell.lines[run['start'] + 1:i] if l.strip()]
            run['on_done'](output)
            return
        destination = run['destination']
        if destination is None:  # RM: 元の(ls)を読み直す
            self.return_stack.clear()
            self.directory_mode.enter_directory(run['origin'])
            self.current_mode = DIRECTORY
            return
        names = run['names']
        if not os.path.isdir(destination):  # 名前を変えた(1つのMV・CP)
            names = [os.path.basename(destination.rstrip('/'))]
            destination = os.path.dirname(destination.rstrip('/'))
        self.return_stack.clear()
        if self.directory_mode.enter_directory(destination):
            self.directory_mode.marks = set(i for i, e in enumerate(self.directory_mode.entries)
                                            if e.name.rstrip('/') in names)
            self.directory_mode.temporary_marks = True
        self.current_mode = DIRECTORY

    # --- HELP・KEY ---
    def start_help(self):
        """^H: HELP.epubをEPUBで(無ければ平文のHELP)。"""
        path = help_text.help_epub_path(japanese())
        if self.in_epub_body() and self.epub is not None and self.epub.path == path:
            self.show_transient_notice('Already in HELP')
            return True
        if path and self.open_epub(path, name='HELP'):
            return True
        return self.open_plain_help(None)

    def plain_help(self):
        return help_text.plain_help_lines(self.wrap_width(), japanese(), self.configuration)

    def open_plain_help(self, raw_line):
        found = self.plain_help()
        if not found:
            self.show_transient_notice('HELP not found: %s (make install)' % 'HELP.1')
            return False
        lines, self.help_byte_count, _ = found
        self.help_view.set_lines(lines)
        self.help_view.cursor_index = self.help_view.top_index = 0
        if raw_line is not None:
            self.help_view.go_to_line(raw_line)  # 画面の中ほどに(C++と同じ)
        self.enter_mode(HELP)
        return True

    def start_key(self):
        """^K: HELPの「：」の行の一覧(Application::startKey())。"""
        if self.current_mode == KEY:
            self.show_transient_notice('Already in KEY')
            return True
        found = self.plain_help()
        if not found:
            self.show_transient_notice('HELP not found: %s (make install)' % 'HELP.1')
            return False
        lines = help_text.key_source_lines(found[0])
        self.key_byte_count = sum(len(l.text.encode('utf-8')) + (1 if l.ends_with_newline else 0) for l in lines)
        cursor = self.key_view.cursor_index
        self.key_view.set_lines(lines)
        self.key_view.cursor_index = cursor  # 前回KEYを離れた行(keyCursorIndex_)
        self.key_view.clamp()
        self.key_view.ensure_cursor_visible()
        self.enter_mode(KEY)
        return True

    def key_line_label(self):
        """カーソルのある元の行の頭(折り返しの続きにいても、その行の最初の断片)。"""
        view = self.key_view
        i = view.cursor_index
        while i > 0 and not view.lines[i - 1].ends_with_newline:
            i -= 1
        return view.lines[i] if view.lines else None

    def execute_key_line(self):
        """KEYの^M: その行のキーを、戻り先のモードで打ったことにする(executeSelectedKeyLine())。"""
        line = self.key_line_label()
        if line is None or not help_text.is_key_assignment_label(line.text):
            return False
        keys = help_text.key_bytes_from_label(line.text)
        self.go_back()
        for b in keys:
            self.feed_key(b, None)
        return True

    def jump_from_key_to_help(self):
        """KEYのh: 平文のHELPの同じ行へ(jumpFromKeyToHelp()のopenPlainHelp())。"""
        line = self.key_line_label()
        if line is None:
            return False
        return self.open_plain_help(line.raw_line_number)

    # --- キー ---
    def pending_prefix_label(self):
        """Application::pendingPrefixLabel()(1秒経ったらモードラインへ)。"""
        if not self.pending_keys:
            return ''
        return prefix_label(self.pending_keys)

    def feed_key(self, byte, special):
        """1キー(Application::feedByte()→dispatchByte())。マクロの記録中は打ったバイトを残す。"""
        if isinstance(special, MouseEvent):
            return self.handle_mouse(special)
        if isinstance(special, Paste):
            return self.handle_paste(special.text)
        if self.coast is not None and special in ('Up', 'Down', 'Left', 'Right'):
            return self.adjust_coast_by_arrow(special)  # 慣性中の矢印: 速さ・向き
        self.coast = self.drag = None  # キーで慣性・ドラッグは終わる
        self.click_targets_cursor = False
        if special in ('Up', 'Down', 'Left', 'Right') and self.start_coast_by_arrow(special):
            return True
        if self.macro_recording is not None and not self.playing and special is None:
            self.macro_recording.append(byte)
        self.notice = None
        if self.vertical_selecting():
            return self.feed_vertical_copy_key(byte, special)
        if self.copy is not None:
            return self.feed_copy_key(byte, special)
        if self.prompt is not None:
            return self.feed_prompt_byte(byte, special)
        if self.grep_prompt is not None:
            return self.feed_grep_prompt_byte(byte)
        if self.grep_active and special is None:
            if byte in (0x0E, 0x10):  # ^N/^P: 次/前の一致
                self.grep.move(1 if byte == 0x0E else -1)
                self.show_current_match()
                return True
            if byte in (CR, CTRL_G):  # そこに留まる(^Gも今いる一致のまま、C++版と同じ)
                self.grep_active = False
                return True
            self.grep_active = False  # 他のキーは、一致の間を移るのをやめてから
        if self.goto_digits is not None:
            if special is None and ord('0') <= byte <= ord('9'):
                self.goto_digits += chr(byte)
                return True
            digits, self.goto_digits = self.goto_digits, None
            if byte == CR and digits:  # ^X^Xの戻り先は変えない(C++版と同じ)
                if self.current_mode == FILE:
                    self.file_mode.ensure_loaded_through(int(digits) + self.file_view.height)
                self.active_view().go_to_line(int(digits))
            return True
        if special is not None:
            self.pending_keys = []
            return self.handle_special(special)
        if self.current_mode == SHELL and (self.utf8_pending or byte >= 0x80):
            return self.feed_shell_byte(byte)  # UTF-8の字(続きのバイトの0x80は^SPCではない)
        if byte >= 0x80:
            return False
        if byte == 0:
            byte = CTRL_SPACE  # ^SPCはNUL(C++のKeyInputと同じく0x80に)
        if self.register_state is not None:
            return self.feed_register_byte(byte)
        if not self.pending_keys:
            if self.current_mode == SHELL and not self.starts_binding_in_text_mode(byte):
                return self.feed_shell_byte(byte)
            if self.current_mode == LIST and self.list_mode.prompt_kind and byte >= 0x20 and byte != 0x7F \
                    and byte != CTRL_SPACE and not self.starts_binding_in_text_mode(byte):
                return self.type_into_list_prompt(byte)
            if self.feed_count_byte(byte):
                return True
            if self.repeating_z and byte == ord('z'):  # ^Xzの後のz
                return self.repeat_last_command()
            self.repeating_z = False
            if self.current_mode == DIRECTORY and byte in (ord('%'), ord('*')):
                return self.start_mark_pattern(byte == ord('*'))
            macro = self.macro_for_byte(byte)
            if macro is not None:
                return self.run_macro(macro)
        self.pending_keys.append(byte)
        keys = self.pending_keys
        x_prefix, o_prefix = self.bindings.prefixes[0], self.bindings.prefixes[2]
        if x_prefix and keys == x_prefix + [ord('r')]:  # ^Xr…(レジスタ・ブックマーク)
            self.pending_keys = []
            self.register_state = 'r'
            return True
        if x_prefix and keys == x_prefix + [ord('z')]:  # ^Xz
            self.pending_keys = []
            return self.repeat_last_command()
        if o_prefix and keys in (o_prefix + [0x13], o_prefix + [CTRL_J, 0x13]):  # ^O^S・^O^J^S(occur)
            self.pending_keys = []
            return self.start_occur_prompt(FIXED if len(keys) == len(o_prefix) + 1 else REGEX)
        if o_prefix and keys == o_prefix + [CTRL_J]:
            return True
        if keys == [ESC, ord('g')]:  # ^[gg(EmacsのM-g g): 行番号を打つ(^[Nと同じ)
            return True
        if keys == [ESC, ord('g'), ord('g')]:
            self.pending_keys = []
            self.goto_digits = ''
            return True
        state, number = self.bindings.match(self.pending_keys)
        if state == 'more':
            return True
        keys, self.pending_keys = self.pending_keys, []
        if state == 'none':
            self.count = None
            return False
        digit = keys[-1] if self.bindings.sequence_for(number)[-1] == DIGIT else None
        if self.pager_launch and number not in PAGER_KEYS:
            self.show_transient_notice('Not available in pager')
            return False
        return self.run_command(number, digit)

    def run_command(self, number, digit):
        """K-番号の機能を^Uの回数だけ(移動等)。^Xzのために覚える。"""
        count = self.count or 1
        self.count = self.count_digits = None
        if number not in (97, 98):
            self.macro_just_played = False
        if number == 36 and count > 1 and self.current_mode not in (DIRECTORY, ARCHIVE):
            return self.pop_mark()  # ^U^␣: マークを遡る
        self.last_command = (number, digit, count)
        result = False
        for _ in range(count if number in REPEATABLE else 1):
            result = self.handle_key_number(number, digit)
        return result

    def repeat_last_command(self):
        """^Xz: 直前のコマンドを、回数も含めてもう一度(続けてzで繰り返し)。"""
        if self.last_command is None:
            self.show_transient_notice('No command to repeat')
            return False
        number, digit, count = self.last_command
        self.repeating_z = True
        self.count = count
        result = self.run_command(number, digit)
        self.repeating_z = True
        return result

    def feed_count_byte(self, byte):
        """^U: 次のコマンドを4回(^U^Uで16回、^U数字でその回数)。"""
        if byte == 0x15:
            self.count = 4 if self.count is None else self.count * 4
            self.count_digits = ''
            self.notice = 'C-u %d' % self.count
            return True
        if self.count_digits is not None and ord('0') <= byte <= ord('9'):
            self.count_digits += chr(byte)
            self.count = int(self.count_digits)
            self.notice = 'C-u %d' % self.count
            return True
        return False

    def starts_binding_in_text_mode(self, byte):
        """SHELL・字を打つ一覧で、割り当ての方へ回すキー(F-0/F-1/F-2/F-4かESCで始まる物、HELPのKEYの章)。"""
        heads = [p[0] for i, p in enumerate(self.bindings.prefixes) if p and i != 3]
        return byte == ESC or byte in heads

    def type_into_list_prompt(self, byte):
        """字を打つ一覧: 打った字でプロンプトを開く(^J^M等の「新たに入力された文字列」)。"""
        kind = self.list_mode.prompt_kind
        back = self.list_prompt
        self.list_prompt = None
        self.go_back()
        if back is not None:  # プロンプトの^Rの一覧: カーソル行の文字列に続けて打つ
            p = back
            p.editor.set_text(self.strip_tags(self.list_mode.selected_item() or ''))
        else:
            p = pr.Prompt(kind, '', self.prompt_handlers(kind))
        self.restore_prompt(p)
        if kind in (pr.FGREP, pr.EGREP):
            return self.feed_grep_prompt_byte(byte)
        return self.feed_prompt_byte(byte, None)

    def handle_special(self, special):
        """矢印・PageUp/PageDown(Application::resolveCommand()の特別なキー)。"""
        if self.current_mode == SHELL:
            if special in ('Left', 'Right'):
                (self.command_line.move_left if special == 'Left' else self.command_line.move_right)()
                self.refresh_shell_view()
                return True
        number = {'Up': 4, 'Left': 4, 'Down': 3, 'Right': 3, 'PageUp': 8, 'PageDown': 5}[special]
        if ((self.current_mode == FILE and self.vertical_active()) or self.vertical_list_active()) and \
                special in ('Left', 'Right'):
            number = 3 if special == 'Left' else 4  # 縦書きは←で先(左)へ
        return self.handle_key_number(number, None)

    def handle_key_number(self, number, digit):
        """K-番号の機能(Application::handleCommand()の、この版で動く分)。"""
        mode = self.current_mode
        view = self.active_view()
        self.last_key_number = number
        if self.vertical_list_active():
            result = self.vertical_list_number(number)
            if result is not None:
                return result
        if mode == FILE and self.vertical_active():
            result = self.vertical_number(number)
            if result is not None:
                return result
        extra = self.handle_stage4_number(number, digit, mode, view)
        if extra is not None:
            return extra
        if number in (3, 4) and mode == OCCUR and view.lines and \
                view.cursor_index == (len(view.lines) - 1 if number == 3 else 0):
            view.cursor_index = 0 if number == 3 else len(view.lines) - 1  # 一致の一覧の^N/^Pは巡回(;・:も)
            view.ensure_cursor_visible()
            return True
        if number in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12):
            return self.move(number, view)
        if number == 13:  # ^[N
            self.goto_digits = chr(digit)
            return True
        if number == 14:
            return self.exchange_point(view)
        if number in (15, 16, 17, 18):
            return self.pager_switch(number)
        if number == 19:
            if self.pager_launch:
                self.wants_exit = True
            return True
        if number == 20:
            return self.back()
        if number == 21:  # ^L: 再表示(一時的な選択は消す)
            if mode == DIRECTORY and self.directory_mode.temporary_marks:
                self.directory_mode.marks = set()
            self.display.screen.clear()
            return True
        if number == 22:
            return self.suspend()
        if number in (24, 25):  # ^J^C・^]
            if self.pager_launch:
                self.show_transient_notice('Not available in pager')
                return False
            return self.enter_directory_listing(self.directory_mode.current_directory)
        if number == 26:  # ^J^B
            if self.pager_launch:
                self.show_transient_notice('Not available in pager')
                return False
            self.directory_mode.mode_stack.clear()
            return self.enter_directory_listing(self.directory_mode.parent_directory(
                self.directory_mode.current_directory))
        if number == 27:  # ^JN: N番目の「/」までのディレクトリ(truncateToPathPrefix())
            base = self.file_mode.current_file if mode == FILE and self.file_mode.current_file.startswith('/') \
                else self.directory_mode.current_directory
            return self.enter_directory_listing(self.truncate_to_path_prefix(base, digit - ord('0')))
        if number == 28:
            return self.enter_key()
        if number in (29, 30):  # ^O^M(1行ずつ)・^[^M(そのまま)
            if mode == DIRECTORY:
                entry = self.directory_mode.selected_entry()
                if entry and not entry.is_directory:
                    path = os.path.join(self.directory_mode.current_directory, entry.name)
                    return self.open_file(path, streaming=number == 29, raw=True)
            return False
        if number == 31:  # ^J^J
            if self.previous_file and os.path.exists(self.previous_file):
                return self.open_file(self.previous_file)
            return False
        if number in (35, 36):
            return self.toggle_mark()
        if number in (37, 38):
            return self.mark_all(number == 37)
        if number == 40:
            return self.start_cp_mv_rm(pr.COPY)
        if number == 41:
            return self.start_cp_mv_rm(pr.MOVE)
        if number == 42:
            return self.start_cp_mv_rm(pr.REMOVE)
        if number == 46:
            return self.start_archive_extract()
        if number == 47:  # ^Je
            return self.start_prompt(pr.EPUB, '', self.execute_epub_input)
        if number == 48:  # ^J^E
            return self.start_file_list(is_epub_file, 'FILE')
        if number == 49:  # ^Oe: .epubをARCとして
            if mode == DIRECTORY:
                entry = self.directory_mode.selected_entry()
                if entry and is_epub_file(entry.name):
                    return self.open_archive(os.path.join(self.directory_mode.current_directory, entry.name))
            return False
        if number == 50:  # ^R
            return self.toggle_history_view()
        if number == 51:
            return self.start_grep_prompt(FIXED)
        if number == 52:
            return self.start_grep_prompt(REGEX)
        if number == 55:  # ^Jd
            path = self.directory_mode.current_directory
            if mode == DU:
                entry = self.du_mode.selected_entry()
                if entry and entry.is_directory:
                    path = entry.path
            return self.start_du(path)
        if number == 56:  # ^J^D
            if not self.du_mode.current_path:
                return self.start_du(self.directory_mode.current_directory)
            self.enter_mode(DU)
            return True
        if number == 57:  # ^Jl
            if self.locate_mode.start_update_db():
                self.show_transient_notice('updatedb')
            return True
        if number == 58:  # ^J^L
            return self.start_locate_prompt()
        if number == 59:  # ^Jm
            return self.start_history_prompt(pr.MAN)
        if number == 60:  # ^J^M
            items = self.ring(hist.RECENT_MAN_PAGE)
            return self.show_list(items, 'MAN', self.execute_man, False, pr.MAN) if items else \
                self.start_history_prompt(pr.MAN)
        if number == 61:  # ^Xf
            return self.start_history_prompt(pr.FILE)
        if number == 62:  # ^X^F
            return self.start_file_list()
        if number == 63:  # ^Xd
            return self.start_history_prompt(pr.DIRECTORY)
        if number == 64:  # ^X^D
            items = self.ring(hist.RECENT_DIRECTORY)
            return self.show_list(items, 'DIR', lambda i: self.enter_directory_listing(self.strip_tags(i),
                                                                                         keep_stack=True),
                                  True, pr.DIRECTORY)
        if number == 68:
            return self.start_shell()
        if number == 69:  # ^Js: SHELLを再起動
            self.shell.terminate()
            self.shell = Shell()
            return self.start_shell()
        if number in (90, 91):
            return self.start_help()
        if number in (93, 94):
            return self.start_key()
        if number in (99, 100):
            return self.start_prompt(pr.EXIT, 'y', self.execute_exit)  # C++版と同じく「y」を入れて尋ねる
        if number == 103:  # ^J^A
            return self.start_file_list(lambda p: bool(self.configuration.archive_for(p)) and not is_epub_file(p),
                                        'FILE', lambda i: self.open_archive(self.strip_tags(i)) or
                                        self.open_path(self.strip_tags(i)))
        if number == 105:  # h(KEYからHELPへ)
            return self.jump_from_key_to_help() if mode == KEY else False
        return False

    def move(self, number, view):
        """移動(K-1〜K-12)。ページャーは先に要る所まで読む(ensurePagerStreamLoaded())。"""
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            if number == 12:
                self.file_mode.load_all()
            else:
                self.file_mode.ensure_loaded_through(max(view.top_index, view.cursor_index) + 2 * view.height)
        if number in (5, 6, 7, 8, 9, 10, 11, 12):
            self.push_mark(view)  # ページ送り・先頭・末尾の前の行にマーク(HELPの^X^X)
        {
            1: view.scroll_line_down, 2: view.scroll_line_up, 3: view.move_down, 4: view.move_up,
            5: view.page_down, 6: view.page_down, 7: view.page_down, 8: view.page_up, 9: view.page_up,
            10: view.page_up, 11: view.go_to_top, 12: view.go_to_bottom,
        }[number]()
        return True

    def push_mark(self, view, index=None):
        """マーク(最大16個、同じ画面の物だけを使う)。"""
        index = view.cursor_index if index is None else index
        if self.mark_ring and self.mark_ring[-1] == (view, index):
            return
        self.mark_ring.append((view, index))
        del self.mark_ring[:-16]

    def marks_for(self, view):
        return [i for v, i in self.mark_ring if v is view]

    def center_on(self, view, index):
        view.cursor_index = index
        view.clamp()
        view.top_index = view.cursor_index - view.height // 2  # ^[Nと同じく中ほどへ
        view.clamp()

    def exchange_point(self, view):
        """^X^X: 最新のマークと今の行を入れ替える。"""
        for i in range(len(self.mark_ring) - 1, -1, -1):
            v, index = self.mark_ring[i]
            if v is view:
                self.mark_ring[i] = (view, view.cursor_index)
                self.center_on(view, index)
                return True
        self.show_transient_notice('No mark set')
        return False

    def pop_mark(self):
        """^U^␣: 新しいマークから順に遡って移る(遡った物は一番古い所へ回す)。"""
        if self.current_mode == FILE and self.vertical_active():  # 縦書きの記録は1つ(C++と同じ)
            self.sync_vertical()
            if not self.vertical.pop_mark():
                self.show_transient_notice('No mark set')
                return False
            self.vertical_to_file()
            return True
        view = self.active_view()
        for i in range(len(self.mark_ring) - 1, -1, -1):
            v, index = self.mark_ring[i]
            if v is view:
                del self.mark_ring[i]
                self.mark_ring.insert(0, (view, index))
                self.center_on(view, index)
                return True
        self.show_transient_notice('No mark set')
        return False

    def pager_switch(self, number):
        if not self.pager_launch:
            return False
        last = len(self.pager_files) - 1
        index = {15: min(self.pager_index + 1, last), 16: max(self.pager_index - 1, 0), 17: 0, 18: last}[number]
        if index != self.pager_index and self.pager_files[0] != '(stdin)':
            self.open_pager_file(index)
        return True

    @staticmethod
    def truncate_to_path_prefix(path, digit):
        """truncateToPathPrefix(): 先頭の「/」を0番目として、digit番目の「/」まで。"""
        remaining, pos = digit + 1, 0
        while remaining > 0:
            slash = path.find('/', pos)
            if slash < 0:
                return path
            pos = slash + 1
            remaining -= 1
        return path[:pos]

    def back(self):
        """^G(モードごと)。"""
        mode = self.current_mode
        if mode == DIRECTORY:
            if self.return_stack:
                return self.go_back()
            if self.directory_mode.mode_stack:
                directory, cursor = self.directory_mode.mode_stack.pop()
                self.directory_mode.enter_directory(directory)
                self.directory_view.cursor_index = cursor
                self.directory_view.ensure_cursor_visible()
            return True
        if mode == FILE:
            if self.pager_launch and not self.in_epub_body():
                return True
            if self.in_epub_body():  # 本文の^Gは目次へ
                self.file_mode.save_cursor_position()
                self.sync_toc_cursor()
                self.current_mode = TOC
                return True
            return self.go_back()
        if mode == TOC:
            if self.pager_launch:
                self.current_mode = FILE
                return True
            return self.go_back()
        if mode == ARCHIVE:
            return self.archive_back()
        if mode == LIST and self.list_stack:  # 一覧の中から開いた一覧: 前の一覧へ
            self.list_mode.restore(self.list_stack.pop())
            return True
        if mode == LIST and self.list_prompt is not None:  # プロンプトの^Rの一覧: プロンプトへ戻る
            back, self.list_prompt = self.list_prompt, None
            self.go_back()
            self.restore_prompt(back)
            return True
        return self.go_back()

    def enter_key(self):
        """^M(モードごと)。"""
        mode = self.current_mode
        if mode == DIRECTORY:
            return self.open_selected_entry()
        if mode == ARCHIVE:
            return self.open_archive_member()
        if mode == LIST:
            item = self.list_mode.selected_item()
            if item is None:
                return False
            return self.list_mode.on_choose(item)
        if mode == TOC:
            index = self.toc_view.cursor_index
            self.current_mode = FILE
            return self.go_to_chapter(index)
        if mode == DU:
            entry = self.du_mode.selected_entry()
            return self.du_or_locate_choose(entry.path if entry else None)
        if mode == LOCATE:
            return self.du_or_locate_choose(self.locate_mode.selected_result())
        if mode == KEY:
            return self.execute_key_line()
        if mode == PART_TOC:
            if not self.part.open_selected():
                return False
            self.enter_mode(PART)
            return True
        if mode == OCCUR:
            return self.occur_jump()
        if mode == INFO:
            return self.info_open()
        return False

    def toggle_history_view(self):
        """^R(ToggleHistoryView): (ls)はDIR、FILE・ARCはFILEの入力順の一覧、一覧では並びを切り換え。"""
        mode = self.current_mode
        if mode == LIST:
            self.list_mode.toggle_sorted()
            return True
        if mode == DIRECTORY:
            items = self.ring(hist.RECENT_DIRECTORY)
            return self.show_list(items, 'DIR', lambda i: self.enter_directory_listing(self.strip_tags(i),
                                                                                         keep_stack=True),
                                  False, pr.DIRECTORY)
        if mode in (FILE, ARCHIVE):
            items = self.ring(hist.RECENT_FILE)
            return self.show_list(items, 'FILE', lambda i: self.open_path(self.strip_tags(i)), False, pr.FILE,
                                  self.file_badge)
        return False

    def suspend(self):
        """^Z: サスペンド(端末を戻してから止まる)。"""
        self.display.suspend()
        os.kill(os.getpid(), signal.SIGSTOP)
        self.display.resume()
        self.layout()
        return True

    # --- プロンプト ---
    def feed_prompt_byte(self, byte, special):
        """モードラインの入力欄(LineEditor)。^Mで実行、^Gでやめる、^Rで履歴、^Iで補完、^Wで以前のディレクトリ。"""
        p = self.prompt
        editor = p.editor
        if special is not None:
            if special in ('Left', 'Right'):
                (editor.move_left if special == 'Left' else editor.move_right)()
            return True
        if self.prompt_ctrl_o:  # ^O^Y: クリップボードの内容を入れる
            self.prompt_ctrl_o = False
            if byte == 0x19:
                editor.insert(self.clipboard_contents().replace('\n', ' '))
            return True
        if byte == CTRL_O:
            self.prompt_ctrl_o = True
            return True
        if p.kind in (pr.BOOKMARK_SET, pr.BOOKMARK_JUMP) and byte in (0x0E, 0x10):  # 一覧を選ぶ
            (self.list_view.move_down if byte == 0x0E else self.list_view.move_up)()
            editor.set_text(self.list_mode.selected_item() or '')
            return True
        if byte == CR:
            self.prompt = None
            text = editor.text
            return p.on_submit(text) if p.on_submit else True
        if byte == CTRL_G:
            self.prompt = None
            if p.kind in (pr.BOOKMARK_SET, pr.BOOKMARK_JUMP):
                self.bookmark_place = None
                self.go_back()
            return True
        if byte == 0x12:  # ^R
            return self.start_list_from_prompt()
        if byte == 0x09 and p.kind in (pr.FILE, pr.DIRECTORY, pr.EPUB, pr.COPY, pr.MOVE, pr.ARCHIVE):
            base = self.directory_mode.current_directory or os.getcwd()
            editor.set_text(pr.complete_path(editor.text, base, p.kind != pr.FILE and p.kind != pr.EPUB))
            return True
        if byte == 0x17 and p.kind in (pr.COPY, pr.MOVE, pr.ARCHIVE):  # ^W
            editor.insert(self.previous_directory())
            return True
        return self.edit_line(editor, byte)

    def edit_line(self, editor, byte):
        """コマンド行の編集(^A ^E ^B ^F ^H ^D ^K ^U ^Y、UTF-8の字)。"""
        if byte == 0x01:
            editor.move_home()
        elif byte == 0x05:
            editor.move_end()
        elif byte == 0x02:
            editor.move_left()
        elif byte == 0x06:
            editor.move_right()
        elif byte in (0x08, 0x7F):
            editor.backspace()
        elif byte == 0x04:
            editor.delete_forward()
        elif byte == 0x0B:
            editor.kill_to_end()
        elif byte == 0x15:
            editor.kill_to_start()
        elif byte == 0x19:
            editor.yank()
        elif byte >= 0xC0 or (self.utf8_pending and byte >= 0x80):
            if byte >= 0xC0:
                self.utf8_pending.clear()
            self.utf8_pending.append(byte)
            if len(self.utf8_pending) >= utf8.sequence_length(self.utf8_pending[0]):
                editor.insert(bytes(self.utf8_pending).decode('utf-8', 'replace'))
                self.utf8_pending.clear()
        elif 0x20 <= byte < 0x7F:
            editor.insert(chr(byte))
        else:
            return False
        return True

    def execute_exit(self, answer):
        if answer.strip().lower() == 'y':
            self.wants_exit = True
        return True

    # --- FGREP/EGREP ---
    def start_grep_prompt(self, syntax):
        """^S(FGREP、打つたびに探す)・^J^S(EGREP)のプロンプト(Application::startGrepPrompt())。"""
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.load_all()  # ページャーは残りを全部読んでから(docs/CHANGES.md項目1009)
        self.grep_active = False
        self.grep.matches, self.grep.occurrences, self.grep.current, self.grep.pattern = [], [], -1, ''
        self.grep.persistent = False
        self.grep.view = self.active_view()
        self.grep.origin = self.active_view().cursor_index
        self.push_mark(self.active_view())  # ^Sの開始にもマーク(HELPの^X^X)
        self.grep_prompt = (syntax, LineEditor())
        return True

    def run_grep(self, syntax, pattern):
        view = self.active_view()
        ok = self.grep.search([line.text for line in view.lines], pattern, syntax, self.grep.origin)
        if ok and self.grep.matches:
            self.show_current_match()
        return ok

    def show_current_match(self):
        """今の一致の行へ、画面の中ほどに(C++版と同じ)。"""
        line = self.grep.current_line()
        if line is None:
            return
        view = self.active_view()
        view.cursor_index = line
        view.top_index = line - view.height // 2
        view.clamp()

    def feed_grep_prompt_byte(self, byte):
        syntax, editor = self.grep_prompt
        if byte == CTRL_G:
            self.grep_prompt = None
            view = self.active_view()
            view.cursor_index = self.grep.origin
            view.ensure_cursor_visible()
            return True
        if byte == CR:
            self.grep_prompt = None
            pattern = editor.text
            if not pattern:
                return True
            if self.history is not None:
                self.history.push_recent(hist.RECENT_FGREP_PATTERN if syntax == FIXED else hist.RECENT_EGREP_PATTERN,
                                         pattern)
            if not self.run_grep(syntax, pattern) or not self.grep.matches:
                self.show_transient_notice('No string')
                return True
            self.grep_active = True
            return True
        if byte == 0x12:  # ^R: 入力順の一覧(EGREPはusilicrcのGREPの例を先頭に)
            self.grep_prompt = None
            self.prompt = pr.Prompt(pr.FGREP if syntax == FIXED else pr.EGREP)
            self.prompt.editor = editor
            result = self.start_list_from_prompt()
            if self.prompt is not None:  # 一覧が空だった
                self.prompt = None
                self.grep_prompt = (syntax, editor)
            return result
        if byte in (0x13, 0x0E, 0x10) and syntax == FIXED:  # 打っている間の^S・^N: 次の一致へ、^P: 前へ(巡回)
            self.grep.move(-1 if byte == 0x10 else 1)
            self.show_current_match()
            return True
        if not self.edit_line(editor, byte):
            return True
        if syntax == FIXED:
            self.run_grep(FIXED, editor.text)
        return True

    # --- SHELL ---
    def ensure_shell(self):
        if self.shell.alive():
            return True
        directory = self.directory_mode.current_directory or os.getcwd()
        if not self.shell.start(directory, self.display.rows - 1, self.display.columns):
            self.shell.terminate()
            self.show_transient_notice('Cannot start SHELL')
            return False
        return True

    def start_shell(self):
        """^[s: SHELLへ(初めてならbashを起動、Command::StartShell)。"""
        if self.current_mode == SHELL:
            return True
        if not self.ensure_shell():
            return False
        self.enter_mode(SHELL)
        self.refresh_shell_view()
        return True

    def feed_shell_byte(self, byte):
        """Application::feedShellByte(): コマンド行の編集、^Mで送る、^Cは中断、^Gで戻る。"""
        editor = self.command_line
        if byte == CTRL_G:
            return self.go_back()
        if byte == CR:
            command = editor.text
            editor.set_text('')
            self.shell.submit_line(command)
            if command.strip() and self.history is not None:
                self.history.push_recent(hist.SHELL_COMMAND, command)
        elif byte == 0x03:   # ^C
            editor.set_text('')
            self.shell.send_control_byte(0x03)
        elif byte == 0x04 and not editor.text:   # 空の行の^DはEOF
            self.shell.send_control_byte(0x04)
        elif byte == 0x12:   # ^R: SHELLのコマンドの履歴(入力順)
            items = self.ring(hist.SHELL_COMMAND)
            if items:
                def choose(item):
                    self.go_back()
                    self.command_line.set_text(item)
                    self.refresh_shell_view()
                    return True
                return self.show_list(items, 'SHELL', choose, False)
        elif byte == 0x0C:
            pass  # 再表示
        else:
            self.edit_line(editor, byte)
        self.refresh_shell_view()
        return True

    # --- 第4段: K-番号の追加分 ---
    def handle_stage4_number(self, number, digit, mode, view):
        """第4段で足した機能。該当しなければNone(handle_key_number()の続きへ)。"""
        if number == 5 and mode == DIRECTORY and sys.platform == 'darwin':
            return self.quick_look()  # (ls)の␣はクイックルック
        if number == 36 and mode not in (DIRECTORY, ARCHIVE):  # ^␣: マークを置く
            self.push_mark(view)
            self.show_transient_notice('Mark set')
            return True
        if number == 23:  # ^Of
            self.mode_line_hidden = not self.mode_line_hidden
            self.layout()
            return True
        if number == 32 and mode == DIRECTORY:
            return self.directory_mode.toggle_filename_only()
        if number == 33 and mode == DIRECTORY:
            return self.directory_mode.cycle_sort_key()
        if number == 34:
            if mode == DIRECTORY:
                return self.directory_mode.toggle_reverse_sort()
            self.display.screen.clear()
            return True
        if number == 43:
            return self.start_tool_prompt()
        if number == 44:
            items = self.ring(hist.SHELL_COMMAND)
            return self.show_list(items, 'TOOL', lambda i: self.start_tool_prompt(i), True, pr.TOOL)
        if number == 45:
            return self.open_external_usilic()
        if number == 53:
            return self.start_part_prompt()
        if number == 106:  # ^J^P
            items = [i for i in self.ring(hist.RECENT_FILE) if self.part_spec(i)]
            return self.show_list(items, 'FILE', self.reopen_part, True, pr.FILE)
        if number == 65:
            return self.start_copy()
        if number == 66:
            return False  # ^O^Yはプロンプトの中だけ(feed_prompt_byte())
        if number == 67:
            return self.start_edit()
        if number == 70 or number == 86:  # ^[l・^J^Os
            return self.show_shell_log()
        if number in (71, 72, 73, 74, 75, 76, 77):
            return self.shell_helper(number)
        if number == 78:
            return self.start_usilic_toc()
        if number == 79:
            return self.show_list(self.ring(hist.USILIC), 'USILIC', self.open_usilic_item, True)
        if number == 80:
            return self.show_list(self.ring(hist.USILIC), 'USILIC', self.open_usilic_item, False)
        if number == 81:
            return self.save_buffer_to_work()
        if number in (82, 83):
            return self.start_info()
        if number == 84:
            return self.open_file(self.configuration.path) if self.configuration.path else False
        if number == 85:
            path = os.path.join(os.path.expanduser('~'), '.usilic', 'error')
            return self.open_file(path) if os.path.exists(path) else False
        if number == 88:
            return self.enter_directory_listing(self.data_directory(), keep_stack=True)
        if number == 89:
            return self.show_work_list()
        if number == 92:
            return self.toggle_language()
        if number == 95:
            if self.macro_recording is not None:
                self.show_transient_notice('Already defining kbd macro')
                return False
            self.macro_recording = []
            self.show_transient_notice('Defining kbd macro...')
            return True
        if number == 96:
            if self.macro_recording is None:
                self.show_transient_notice('Not defining kbd macro')
                return False
            seq = self.bindings.sequence_for(96) or []
            recorded = self.macro_recording
            self.macro_recording = None
            if seq and recorded[-len(seq):] == seq:
                recorded = recorded[:-len(seq)]
            self.macro_bytes = bytes(recorded)
            self.show_transient_notice('Keyboard macro defined')
            return True
        if number == 97:
            return self.play_recorded_macro()
        if number == 98:
            return self.save_or_reload_macros()
        if number == 101 and self.in_epub_body():
            return self.toggle_epub_chapter_mode()
        if number == 102:
            return self.cycle_url_mode()
        if number == 107:  # ^Od: ホイールの向き(既定はmacOSのナチュラルスクロールの向き=reversed)
            self.wheel_reversed = not self.wheel_reversed
            self.show_transient_notice('Wheel: normal' if self.wheel_reversed else 'Wheel: reversed')
            return True
        if number == 108:
            return self.show_epub_marks()
        if number == 28 and self.in_epub_body():
            return self.follow_epub_link()
        if number == 20 and self.in_epub_body() and self.epub_note_active():  # 注釈の区画から元の所へ
            return self.leave_epub_note()
        if number == 20 and self.in_epub_body() and self.epub_link_stack:  # 注釈から戻る
            line = self.epub_link_stack.pop()
            self.set_epub_cursor(line)
            return True
        return None

    # --- PART・OCCUR ---
    @staticmethod
    def part_spec(item):
        """FILEの履歴の「パス "文字列"」を(パス, 文字列)に。違えばNone。"""
        if len(item) >= 4 and item.endswith('"'):
            at = item.rfind(' "', 0, len(item) - 1)
            if at > 0:
                return item[:at], item[at + 2:-1]
        return None

    def start_part_prompt(self):
        """^Jp: 今の画面を拡張正規表現で分ける。"""
        return self.start_prompt(pr.PART, '', self.execute_part)

    def execute_part(self, pattern):
        if not pattern:
            return False
        view = self.active_view()
        name = self.file_mode.current_file if self.current_mode == FILE else ''
        size = {FILE: self.file_mode.byte_count, MAN: self.man_mode.byte_count,
                SHELL: self.shell.byte_count}.get(self.current_mode, self.directory_mode.byte_count)
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.load_all()
        if not self.part.split(view.lines, pattern, size, name):
            self.show_transient_notice('No string')
            return False
        if self.history is not None:
            self.history.push_recent(hist.RECENT_EGREP_PATTERN, pattern)
            self.history.remember_name(hist.CUT_PATTERN, pattern)
            if name and self.current_mode == FILE and os.path.isfile(name):
                self.history.push_recent(hist.RECENT_FILE, '%s "%s"' % (name, pattern))
        self.enter_mode(PART_TOC)
        return True

    def reopen_part(self, item):
        """^J^Pの項目(「パス "文字列"」): fileを開き直して同じ分割をやり直す。"""
        spec = self.part_spec(self.strip_tags(item) if item.endswith(hist.USILIC_TAG) else item)
        if not spec or not os.path.isfile(spec[0]):
            return False
        if not self.open_file(spec[0]):
            return False
        return self.execute_part(spec[1])

    def start_occur_prompt(self, syntax):
        """^O^S・^O^J^S: 今の画面で一致した行を一覧に。"""
        kind = pr.OCCUR_FIXED if syntax == FIXED else pr.OCCUR_REGEX
        return self.start_prompt(kind, '', lambda t: self.execute_occur(t, syntax))

    def execute_occur(self, pattern, syntax):
        if not pattern:
            return False
        view = self.active_view()
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.load_all()
        found = occur_lines(view.lines, pattern, syntax)
        if not found:
            self.show_transient_notice('No string')
            return False
        if self.history is not None:
            self.history.push_recent(hist.RECENT_FGREP_PATTERN if syntax == FIXED else hist.RECENT_EGREP_PATTERN,
                                     pattern)
        self.occur_items, self.occur_syntax, self.occur_origin = found, syntax, self.current_mode
        self.occur_global = False
        self.grep.pattern, self.grep.syntax, self.grep.view = pattern, syntax, view
        self.grep.set_matches([l.text for l in view.lines], [i for i, _ in found])
        self.occur_byte_count = sum(len(l.text.encode('utf-8')) + 1 for _, l in found)
        self.occur_view.set_lines([Line(l.text, i + 1, attributes=l.attributes) for i, (_, l) in enumerate(found)])
        self.occur_view.cursor_index = self.occur_view.top_index = 0
        self.enter_mode(OCCUR)
        return True

    def occur_jump(self):
        """一覧の^M: 元の画面のその行へ(一致の強調は残す、; :で前/次)。"""
        i = self.occur_view.cursor_index
        if not 0 <= i < len(self.occur_items):
            return False
        line = self.occur_items[i][0]
        if self.occur_global:  # EPUBの記録の一覧(^O^E): 本全体の行
            self.enter_mode(self.occur_origin)
            self.set_epub_cursor(line)
            return True
        self.grep.select_line(line)
        self.grep.persistent = True
        self.enter_mode(self.occur_origin)
        view = self.active_view()
        self.push_mark(view)
        self.center_on(view, line)
        return True

    # --- TOOL・外のページャー ---
    def start_tool_prompt(self, text=None):
        """^Jt: 選択(無ければカーソル行)のfile名を入れて、その前にプログラム名を打つ。"""
        self.tool_directory = ''
        seed = ''
        if text is None:
            if self.current_mode == DIRECTORY:
                d = self.directory_mode
                indices = sorted(d.marks) or [d.view.cursor_index]
                names = [d.entries[i].name for i in indices if i < len(d.entries) and not d.entries[i].is_directory]
                names = [n.rstrip('*@=%|') for n in names]
                if names:
                    self.tool_directory = d.current_directory
                    seed = ' '.join(shlex.quote(n) for n in names)
            elif self.current_mode == FILE and os.path.isfile(self.file_mode.current_file):
                self.tool_directory = os.path.dirname(self.file_mode.current_file)
                seed = shlex.quote(os.path.basename(self.file_mode.current_file))
            text = seed
        p = pr.Prompt(pr.TOOL, text, self.execute_tool)
        if seed:
            p.editor.cursor = 0
        self.prompt = p
        return True

    def execute_tool(self, command):
        """Application::executeProgram(): PROGRAMの物は画面を抜けて、usilicは外のページャー、他はSHELLで。"""
        words = command.split()
        if not words:
            return False
        if self.history is not None:
            self.history.push_recent(hist.SHELL_COMMAND, command)
        directory, self.tool_directory = self.tool_directory, ''
        programs = [p[0] for p in self.configuration.programs if p]
        if words[0] == 'usilic':
            return self.run_external([sys.executable, USILIC_SCRIPT] + shlex.split(command)[1:], cwd=directory or None)
        if words[0] in programs:
            return self.run_external(['sh', '-c', command], cwd=directory or None)
        if not self.ensure_shell():
            return False
        line = '(cd %s && %s)' % (shlex.quote(directory), command) if directory else command
        self.enter_mode(SHELL)
        self.shell.submit_line(line)
        self.refresh_shell_view()
        return True

    def open_external_usilic(self):
        """^Ou: 選択(無ければカーソル行)を外のusilic(ページャー・その場限りの(ls))で、1つずつ。"""
        if self.current_mode != DIRECTORY:
            return False
        d = self.directory_mode
        indices = sorted(d.marks) or [d.view.cursor_index]
        paths = [os.path.join(d.current_directory, d.entries[i].name.rstrip('*@=%|'))
                 for i in indices if i < len(d.entries) and d.entries[i].name not in ('./', '../')]
        for path in paths:
            env = dict(os.environ, USILIC_NO_HISTORY='1')
            self.run_external([sys.executable, USILIC_SCRIPT, path], env=env)
            if self.history is not None:
                category = hist.RECENT_DIRECTORY if os.path.isdir(path) else hist.RECENT_FILE
                self.history.push_recent(category, path.rstrip('/') + ('/' if os.path.isdir(path) else '') +
                                         hist.USILIC_TAG)
        return True

    def quick_look(self):
        d = self.directory_mode
        indices = sorted(d.marks) or [d.view.cursor_index]
        paths = [os.path.join(d.current_directory, d.entries[i].name.rstrip('*@=%|'))
                 for i in indices if i < len(d.entries) and d.entries[i].name not in ('./', '../')]
        if not paths:
            return False
        try:
            subprocess.Popen(['qlmanage', '-p'] + paths, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            return False
        return True

    # --- USILIC・WORK ---
    def start_usilic_toc(self):
        """^[u: MAN・DIR・FILE・TOOL・WORK・USILIC・ARC・EPUB・PARTの目次。"""
        titles = ['MAN', 'DIR', 'FILE', 'TOOL', 'WORK', 'USILIC', 'ARC', 'EPUB', 'PART']
        result = self.show_list(titles, 'USILIC', self.open_usilic_group, False)
        self.list_view.cursor_index = self.list_view.top_index = 0
        return result

    def open_usilic_group(self, title):
        if title == 'MAN':
            return self.show_list(self.ring(hist.RECENT_MAN_PAGE), 'MAN', self.execute_man, True, pr.MAN)
        if title == 'DIR':
            return self.handle_key_number(64, None)
        if title == 'FILE':
            return self.start_file_list()
        if title == 'TOOL':
            return self.handle_key_number(44, None)
        if title == 'WORK':
            return self.show_work_list()
        if title == 'USILIC':
            items = [i for i in self.ring(hist.USILIC) if i.endswith(hist.USILIC_TAG)]
            return self.show_list(items, 'USILIC', self.open_usilic_item, True)
        if title == 'ARC':
            return self.handle_key_number(103, None)
        if title == 'EPUB':
            return self.handle_key_number(48, None)
        return self.handle_key_number(106, None)

    def open_usilic_item(self, item):
        """USILICの項目: 「名前 (man)」はMAN、「(usilic)」付きは外のusilic、/で終わればディレクトリ。"""
        if item.endswith(' (man)'):
            return self.execute_man(item[:-len(' (man)')])
        if item.endswith(hist.USILIC_TAG):
            path = item[:-len(hist.USILIC_TAG)]
            env = dict(os.environ, USILIC_NO_HISTORY='1')
            return self.run_external([sys.executable, USILIC_SCRIPT, path], env=env)
        spec = self.part_spec(item)
        if spec:
            return self.reopen_part(item)
        return self.open_path(item)

    def allocate_work_file(self):
        os.makedirs(self.work_root, exist_ok=True)
        path = os.path.join(self.work_root, 'w.%d' % (len(self.work_files) + 1))
        self.work_files.append(path)
        return path

    def save_buffer_to_work(self):
        """^X^W: 今の画面の行を作業ファイル(WORK)へ足す。"""
        path = self.work_files[-1] if self.work_files else self.allocate_work_file()
        try:
            with open(path, 'a', encoding='utf-8', errors='surrogateescape') as f:
                for line in self.active_view().lines:
                    f.write(line.text + '\n')
        except OSError:
            self.show_transient_notice('Cannot write: ' + path)
            return False
        self.show_transient_notice('Saved: ' + path)
        return True

    def show_work_list(self):
        return self.show_list(self.work_files, 'WORK', lambda p: self.open_file(p), False)

    # --- マクロ ---
    def macro_for_byte(self, byte):
        if self.current_mode in (KEY,) and byte == ord('h'):
            return None
        for macro in self.configuration.macro_list():
            seq = mc.macro_key_bytes(macro, self.bindings.prefixes)
            if seq == [byte]:
                return macro
        return None

    def run_macro(self, macro):
        """MACRO節のキー: K-nは機能、"文字列"は打ったことにする。"""
        self.playing = True
        try:
            for kind, value in macro.items:
                if kind == 'k':
                    if self.pager_launch and value not in PAGER_KEYS:
                        continue
                    self.run_command(value, None)
                else:
                    for b in value.encode('utf-8'):
                        self.feed_key(b, None)
        finally:
            self.playing = False
        return True

    def play_recorded_macro(self):
        """^Xe: 記録したキーを打ち直す(間隔はWAIT)。"""
        if not self.macro_bytes:
            self.show_transient_notice('Not defining kbd macro')
            return False
        self.playing = True
        wait = self.configuration.wait_centiseconds / 100.0
        try:
            for b in self.macro_bytes:
                self.feed_key(b, None)
                if wait:
                    self.redraw()
                    time.sleep(wait)
        finally:
            self.playing = False
        self.macro_just_played = True
        return True

    def save_or_reload_macros(self):
        """^Xs: ^Xeの直後なら記録をMACRO節へ(キーを尋ねる)、それ以外はusilicrcを読み直してMACROの一覧。"""
        if self.macro_just_played and self.macro_bytes and self.configuration.path:
            items = mc.items_from_bytes(list(self.macro_bytes))
            if not items:
                return False

            def submit(key):
                key = key.strip()
                if not key:
                    return False
                try:
                    mc.save_macro(self.configuration.path, key, items)
                except OSError:
                    self.show_transient_notice('Cannot write: ' + self.configuration.path)
                    return False
                self.reload_configuration()
                return True
            self.macro_just_played = False
            return self.start_prompt(pr.MACRO_KEY, '', submit)
        self.reload_configuration()
        lines = ['%s  %s' % (m.key_spec, ' '.join('K-%d' % v if k == 'k' else '"%s"' % v for k, v in m.items))
                 for m in self.configuration.macro_list()]
        result = self.show_list(lines, 'MACRO', lambda i: True, False)
        self.list_view.cursor_index = self.list_view.top_index = 0
        return result

    def reload_configuration(self):
        """usilicrcを読み直す(EDITで直した時・^Xs)。"""
        config = Configuration()
        if config.load_default_file():
            self.configuration = config
            self.bindings = config.key_bindings
            for mode in (self.man_mode, self.du_mode, self.locate_mode):
                mode.configuration = config
        return True

    # --- COPY ---
    def start_copy(self):
        if self.prompt is not None:
            return False
        if self.current_mode == FILE and self.vertical_active():  # 縦書きのまま字を範囲指定する
            self.sync_vertical()
            self.vertical.start_select()
            self.vertical_prefix = None
            return True
        if self.vertical_list_active():  # 縦書きの一覧は描いた画面のまま(C++のCopyModeのスナップショット)
            self.render_vertical_list()
            snapshot = ContentView()
            snapshot.set_height(self.vlist_view.height)
            snapshot.set_lines(list(self.vlist_view.lines))
            self.copy = CopyMode(snapshot)
        else:
            self.copy = CopyMode(self.active_view())
        self.copy_prefix = None
        return True

    def feed_copy_key(self, byte, special):
        """COPYのキー(^N ^P ^F ^B ^A ^E・矢印、^@、^X␣、^[w、^[t、^[N、^G)。"""
        c = self.copy
        if special is not None:
            {'Up': lambda: c.move_row(-1), 'Down': lambda: c.move_row(1), 'Left': lambda: c.move_col(-1),
             'Right': lambda: c.move_col(1), 'PageUp': lambda: c.move_row(-c.view.height),
             'PageDown': lambda: c.move_row(c.view.height)}[special]()
            return True
        prefix, self.copy_prefix = self.copy_prefix, None
        if prefix == 'goto':
            if ord('0') <= byte <= ord('9'):
                self.copy_digits += chr(byte)
                self.copy_prefix = 'goto'
                return True
            if byte == CR and self.copy_digits:
                c.go_to_row(int(self.copy_digits) - 1)
            return True
        if prefix == 'ESC':
            if byte == ord('w'):
                return self.copy_to_clipboard(False)
            if byte == ord('t'):
                return self.copy_to_clipboard(True)
            if ord('0') <= byte <= ord('9'):
                self.copy_prefix, self.copy_digits = 'goto', chr(byte)
                return True
            if byte == ord('<'):
                c.go_to_row(0)
            elif byte == ord('>'):
                c.go_to_row(len(c.view.lines) - 1)
            elif byte == ord('v'):
                c.move_row(-c.view.height)
            return True
        if prefix == 'X':
            if byte == ord(' '):
                c.toggle(True)
            return True
        if byte == ESC:
            self.copy_prefix = 'ESC'
        elif byte == CTRL_X:
            self.copy_prefix = 'X'
        elif byte in (0, CTRL_SPACE):
            c.toggle(False)
        elif byte == CTRL_G:
            self.copy = None
        elif byte in (0x0E, 0x10):
            c.move_row(1 if byte == 0x0E else -1)
        elif byte in (0x06, 0x02):
            c.move_col(1 if byte == 0x06 else -1)
        elif byte == 0x01:
            c.line_start()
        elif byte == 0x05:
            c.line_end()
        elif byte == 0x16:
            c.move_row(c.view.height)
        return True

    def copy_to_clipboard(self, to_tool):
        """^[w: 範囲をクリップボードへ(macOSはpbcopy、他はxclip)。^[tはさらにTOOLのプロンプトへ。"""
        c = self.copy
        text = c.collected_text(for_tool=to_tool)
        self.copy = None
        return self.deliver_copied_text(text, to_tool)

    def deliver_copied_text(self, text, to_tool):
        """COPYで集めた字をクリップボードへ(to_toolならTOOLのプロンプトへも)。"""
        if not text:
            return False
        self.clipboard_text = text
        argv = ['pbcopy'] if sys.platform == 'darwin' else ['xclip', '-selection', 'clipboard']
        try:
            subprocess.run(argv, input=text.encode('utf-8'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass
        if to_tool:
            return self.start_tool_prompt(text)
        self.show_transient_notice('Copied: ' + text.split('\n')[0][:40])
        return True

    def clipboard_contents(self):
        if sys.platform == 'darwin':
            try:
                return subprocess.run(['pbpaste'], capture_output=True).stdout.decode('utf-8', 'replace')
            except OSError:
                pass
        return self.clipboard_text

    # --- EDIT ---
    def start_edit(self):
        """^[e: エディタ(+N付き)で開き、終わったら読み直す(usilicrcなら設定も)。"""
        editing_file = self.current_mode == FILE and not self.in_epub_body() and \
            os.path.isfile(self.file_mode.current_file)
        if editing_file:
            path = self.file_mode.current_file
            lines = self.file_view.lines
            line = (lines[self.file_view.cursor_index].raw_line_number if lines else 1) or 1
        elif self.current_mode == DIRECTORY and self.directory_mode.selected_entry() and \
                not self.directory_mode.selected_entry().is_directory:
            path = os.path.join(self.directory_mode.current_directory,
                                self.directory_mode.selected_entry().name.rstrip('*@=%|'))
            line = 1
        else:
            return False
        self.run_external(self.configuration.editor_argv() + ['+%d' % line, path])
        if self.configuration.path and os.path.realpath(path) == os.path.realpath(self.configuration.path):
            self.reload_configuration()
        if editing_file:
            cursor, tail, label = self.file_view.cursor_index, self.file_mode.tail, self.file_mode.label
            if self.file_mode.load_file(path, self.wrap_width(), self.file_view.height):
                self.file_mode.load_all()
                self.file_mode.tail, self.file_mode.label = tail, label
                self.file_view.cursor_index = cursor
                self.file_view.clamp()
                self.file_view.ensure_cursor_visible()
        else:
            self.directory_mode.refresh()
        return True

    # --- INFO・SHELLの補助 ---
    def start_info(self):
        """^I: Boot options・Startup file・ERROR・SHELL・DATA・WORK(^Mでそれぞれを開く)。"""
        home = os.path.join(os.path.expanduser('~'), '.usilic')
        boot = ' '.join(sys.argv[1:])
        rows = [('Boot options', boot, None), ('Startup file', self.configuration.path or '', 'file'),
                ('ERROR', os.path.join(home, 'error'), 'file'), ('SHELL', 'SHELL', 'shell'),
                ('DATA', self.data_directory(), 'dir')]
        works = self.work_files or ['']
        for i, path in enumerate(works):
            rows.append(('WORK' if i == 0 else '', path, 'file' if path else None))
        lines, self.info_targets = [], []
        for number, (label, value, kind) in enumerate(rows, 1):
            text = (label.rjust(12) + ' : ' if label else ' ' * 15) + value
            lines.append(Line(text, number))
            self.info_targets.append((kind, value) if kind else None)
        self.info_byte_count = sum(len(l.text.encode('utf-8')) + 1 for l in lines)
        self.info_view.set_lines(lines)
        self.info_view.cursor_index = self.info_view.top_index = 0
        self.enter_mode(INFO)
        return True

    def info_open(self):
        i = self.info_view.cursor_index
        target = self.info_targets[i] if 0 <= i < len(self.info_targets) else None
        if target is None:
            return False
        kind, value = target
        if kind == 'shell':
            return self.show_shell_log()
        if kind == 'dir':
            return self.enter_directory_listing(value, keep_stack=True)
        if not os.path.exists(value):
            open(value, 'a').close() if os.path.isdir(os.path.dirname(value)) else None
        return self.open_file(value) if os.path.exists(value) else False

    def show_shell_log(self):
        """^J^Os・^[l: SHELLの出力をFILEで(PART等が使える)。"""
        lines = []
        for number, text in enumerate(self.shell.lines, 1):
            pieces = wrap_text(expand_tabs(text), self.wrap_width())
            for i, piece in enumerate(pieces):
                lines.append(Line(piece, number, ends_with_newline=i == len(pieces) - 1))
        size = sum(len(t.encode('utf-8')) + 1 for t in self.shell.lines)
        self.file_mode.set_rendered('SHELL', lines, size, self.wrap_width())
        self.file_mode.record_history = False
        self.file_view.go_to_bottom()
        self.enter_mode(FILE)
        return True

    def shell_helper(self, number):
        """^J. ^J, ^Jw ^J^W ^O/ ^O^F ^O^B(SHELLの章)。"""
        if number == 71:  # ^J.: SHELLのカレントディレクトリを(ls)の所へ
            directory = self.directory_mode.current_directory
            if self.current_mode == FILE and os.path.isfile(self.file_mode.current_file):
                directory = os.path.dirname(self.file_mode.current_file) + '/'
            if not self.ensure_shell():
                return False
            self.enter_mode(SHELL)
            self.shell.submit_line('cd ' + shlex.quote(directory))
            self.refresh_shell_view()
            return True
        if number == 72:
            if not self.ensure_shell():
                return False
            self.enter_mode(SHELL)
            self.shell.submit_line('ls -l')
            self.refresh_shell_view()
            return True
        if number in (73, 74):
            origin = self.return_stack[-1] if self.current_mode == SHELL and self.return_stack else self.current_mode
            if number == 73:  # ^Jw: バッファ名
                name = self.file_mode.current_file if origin == FILE else self.directory_mode.current_directory
                self.last_shell_insert = shlex.quote(name)
            else:  # ^J^W: バッファの内容を~/.usilic/data/へ保存して「cat ファイル名」
                view = self.active_view() if self.current_mode != SHELL else {
                    FILE: self.file_view, DIRECTORY: self.directory_view}.get(origin, self.directory_view)
                path = os.path.join(self.data_directory(), origin)
                with open(path, 'w', encoding='utf-8', errors='surrogateescape') as f:
                    for line in view.lines:
                        f.write(line.text + '\n')
                self.last_shell_insert = 'cat ' + shlex.quote(path)
            if not self.ensure_shell():
                return False
            if self.current_mode != SHELL:
                self.enter_mode(SHELL)
            self.command_line.set_text(self.last_shell_insert)
            self.refresh_shell_view()
            return True
        if self.current_mode != SHELL:
            return False
        if number == 75:
            return self.start_grep_prompt(FIXED)
        self.shell_view.page_down() if number == 76 else self.shell_view.page_up()
        return True

    def toggle_language(self):
        """^Oj: 日本語/英語(HELPを見ていればその言語のHELPへ)。"""
        messages_ja.set_japanese(not japanese())
        if self.in_epub_body() and self.epub_name == 'HELP':
            fraction = self.file_view.cursor_index / max(1, len(self.file_view.lines))
            back = list(self.return_stack)
            if self.start_help() and self.in_epub_body():
                self.return_stack = back
                self.set_epub_cursor(int(fraction * len(self.epub_lines)))
        return True

    # --- (ls)の%・* ---
    def start_mark_pattern(self, extension):
        if extension:
            entry = self.directory_mode.selected_entry()
            name = entry.name.rstrip('/*@=%|') if entry else ''
            dot = name.rfind('.')
            seed = name[dot:] if dot > 0 else ''
            return self.start_prompt(pr.MARK_EXTENSION, seed, lambda t: self.execute_mark_pattern(t, True))
        return self.start_prompt(pr.MARK_REGEX, '', lambda t: self.execute_mark_pattern(t, False))

    def execute_mark_pattern(self, text, extension):
        """Application::executeMarkPattern(): 名前が正規表現・拡張子に合う行を選ぶ。"""
        if not text:
            return False
        if extension and not text.startswith('.'):
            text = '.' + text
        try:
            pattern = None if extension else re.compile(text)
        except re.error:
            self.show_transient_notice('No string')
            return False
        count = 0
        for i, entry in enumerate(self.directory_mode.entries):
            name = entry.name.rstrip('/*@=|')
            if name in ('.', '..'):
                continue
            if (pattern.search(name) if pattern else len(name) > len(text) and name.endswith(text)):
                self.directory_mode.marks.add(i)
                count += 1
        self.show_transient_notice('%d %s' % (count, 'file marked' if count == 1 else 'files marked'))
        return count > 0

    # --- レジスタ・ブックマーク ---
    def current_place(self):
        """(パス, 元の行の番号)。ディレクトリは行0。"""
        if self.current_mode == FILE and self.file_mode.current_file and os.path.exists(self.file_mode.current_file):
            lines = self.file_view.lines
            raw = lines[self.file_view.cursor_index].raw_line_number if lines else 1
            return os.path.abspath(self.file_mode.current_file), raw or 1
        if self.current_mode == DIRECTORY:
            return self.directory_mode.current_directory, 0
        return None

    def jump_to_place(self, path, raw_line):
        if raw_line == 0:
            return os.path.isdir(path) and self.enter_directory_listing(path)
        if not os.path.exists(path):
            return False
        if not (self.current_mode == FILE and os.path.abspath(self.file_mode.current_file) == path):
            if not self.open_path(path):
                return False
        view = self.file_view
        self.push_mark(view)
        view.go_to_line(raw_line)
        return True

    def feed_register_byte(self, byte):
        """^Xrの後: ␣c(レジスタへ)・jc(レジスタの所へ)・m(ブックマーク)・b/l(ブックマークの一覧)。"""
        state, self.register_state = self.register_state, None
        if byte == CTRL_G:
            return False
        if state == 'r':
            if byte == ord(' '):
                self.register_state = 'set'
                return True
            if byte == ord('j'):
                self.register_state = 'jump'
                return True
            if byte == ord('m'):
                return self.start_bookmark_list(True)
            if byte in (ord('b'), ord('l')):
                return self.start_bookmark_list(False)
            return False
        if state == 'set':
            place = self.current_place()
            if place is None:
                self.show_transient_notice("Can't set register here")
                return False
            self.registers[byte] = place
            self.show_transient_notice('Register %s set' % chr(byte))
            return True
        place = self.registers.get(byte)
        if place is None:
            self.show_transient_notice('Register %s is empty' % chr(byte))
            return False
        if not self.jump_to_place(*place):
            self.show_transient_notice("Can't open " + place[0])
            return False
        return True

    def start_bookmark_list(self, setting):
        """^Xrm(名前を尋ねて置く、初期値はファイル名)・^Xrb(一覧から^N/^Pで選んで^M、-名前で消す)。"""
        if setting:
            self.bookmark_place = self.current_place()
            if self.bookmark_place is None:
                self.show_transient_notice("Can't set bookmark here")
                return False
        entries = bm.read_bookmarks()
        if not setting and not entries:
            self.show_transient_notice('No bookmarks')
            return False
        names = [e[0] for e in entries]
        displays = dict(zip(names, bm.display_lines(entries)))
        if setting:
            path = self.bookmark_place[0].rstrip('/')
            initial = os.path.basename(path) or path
        else:
            initial = names[0]
        if not names:
            names, displays = [initial], {initial: '-- (new)'}
        self.list_mode.show(names, False, 'BOOKMARK', lambda i: True, None, None, lambda n: displays.get(n, n))
        self.list_view.cursor_index = 0
        self.enter_mode(LIST)
        kind = pr.BOOKMARK_SET if setting else pr.BOOKMARK_JUMP
        self.prompt = pr.Prompt(kind, initial, self.execute_bookmark, show_position=True)
        return True

    def execute_bookmark(self, text):
        name = text.strip()
        setting = self.bookmark_place is not None
        self.go_back()
        if not name:
            return False
        if setting:
            path, raw = self.bookmark_place
            self.bookmark_place = None
            bm.set_bookmark(name, path, raw)
            self.show_transient_notice('Bookmark "%s" set' % name)
            return True
        if name.startswith('-') and len(name) > 1:
            if bm.delete_bookmark(name[1:]):
                self.show_transient_notice('Bookmark "%s" deleted' % name[1:])
                return True
            return False
        entry = bm.take_bookmark(name)
        if entry is None:
            return False
        if not self.jump_to_place(entry[1], entry[2]):
            self.show_transient_notice("Can't open " + entry[1])
            return False
        return True

    # --- EPUBの残り ---
    def set_epub_cursor(self, line):
        """本全体の行lineへ(章ごとの時はその章を出す)。"""
        if self.epub_chapter_mode:
            self.epub_chapter = self.chapter_of_line(line)
            start = self.apply_epub_chapter()
            line -= start
        self.center_on(self.file_view, line)

    def chapter_of_line(self, line):
        current = 0
        for i, start in enumerate(self.epub_toc_lines):
            if start <= line:
                current = i
        return current

    def chapter_bounds(self, index):
        starts = sorted(set(self.epub_toc_lines)) or [0]
        start = self.epub_toc_lines[index] if self.epub_toc_lines else 0
        later = [s for s in starts if s > start]
        return start, (later[0] if later else len(self.epub_lines))

    def apply_epub_chapter(self):
        """章ごとの時: file_viewにその章の行だけを出す。始まりの行を返す。"""
        if not self.epub_chapter_mode:
            self.file_view.set_lines(self.epub_lines)
            return 0
        start, end = self.chapter_bounds(self.epub_chapter)
        self.file_view.set_lines(self.epub_lines[start:end])
        return start

    def global_epub_line(self):
        offset = self.chapter_bounds(self.epub_chapter)[0] if self.epub_chapter_mode else 0
        return self.file_view.cursor_index + offset

    def toggle_epub_chapter_mode(self):
        """^Os: 章ごと/連続のスクロール。"""
        line = self.global_epub_line()
        self.epub_chapter_mode = not self.epub_chapter_mode
        self.epub_chapter = self.chapter_of_line(line)
        start = self.apply_epub_chapter()
        self.center_on(self.file_view, line - start)
        self.show_transient_notice('Scroll: chapter' if self.epub_chapter_mode else 'Scroll: continuous')
        return True

    def follow_epub_link(self, href=None):
        """本文の^M: カーソル行の最初のリンク(本の中は飛んで^Gで戻る、Webは^Owのモードで)。縦書きは選んでいる
        リンク(href)。"""
        if href is None:
            lines = self.file_view.lines
            if not lines:
                return False
            line = lines[self.file_view.cursor_index]
            if not line.links:
                return False
            href = line.links[0][2]
        if re.match(r'[a-z]+:', href):
            return self.open_url(href)
        target = self.epub.line_for(href)
        if target is None:
            return False
        if self.show_epub_note(self.epub_lines[target].raw_line_number):  # 注釈の区画へ(^Gで元の所へ)
            return True
        if self.epub_note_active():  # 注釈の中のリンク(↩等)から本文へ: 本文の行に戻してから
            self.leave_epub_note()
            self.set_epub_cursor(target)
            return True
        self.epub_link_stack.append(self.global_epub_line())
        self.set_epub_cursor(target)
        self.file_view.top_index = self.file_view.cursor_index
        self.file_view.clamp()
        self.show_transient_notice('^G to return from note')
        return True

    def cycle_url_mode(self):
        """^Ow: Webのリンクの開き方を巡回(名前をモードラインへ)。"""
        handlers = self.configuration.url_handler_list()
        self.url_mode = (self.url_mode + 1) % len(handlers)
        self.show_transient_notice(handlers[self.url_mode][0])
        return True

    def open_url(self, url):
        """Webのリンク(usilicrcのURL、Application::openUrl())。処理が終わると既定(最初)に戻る。"""
        handlers = self.configuration.url_handler_list()
        name, argv = handlers[self.url_mode % len(handlers)]
        self.url_mode = 0
        try:
            if name == 'Open':
                subprocess.Popen(argv + [url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            elif name == 'Copy':
                subprocess.run(argv, input=url.encode('utf-8'))
                self.show_transient_notice('Copied: ' + url)
            elif name == 'Look':
                subprocess.Popen(['qlmanage', '-p', url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            elif name == 'Term':
                return self.run_external(argv + [url])
            elif name == 'Dump':
                result = subprocess.run(argv + [url], capture_output=True)
                self.remember_previous_file()
                self.file_mode.load_bytes(result.stdout, url, self.wrap_width())
                self.file_mode.record_history = False
                self.file_mode.tail = 'Dump'
                self.enter_mode(FILE)
            elif name == 'Pdf':
                return self.start_shell_run(argv + [url], None, [], on_done=self.after_pdf)
            elif name == 'Epub':
                return self.run_narou2epub(url)
        except OSError:
            self.show_transient_notice('Cannot run: ' + (argv[0] if argv else name))
            return False
        return True

    def after_pdf(self, output):
        path = output[-1].strip() if output else ''
        if path and os.path.exists(path):
            opener = ['qlmanage', '-p'] if sys.platform == 'darwin' else ['xdg-open']
            subprocess.Popen(opener + [path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True

    def run_narou2epub(self, target):
        """作品番号・URL→narou2epubで~/.usilic/data/(本を読んでいればその本の所)にEPUBを作って開く。"""
        handler = dict(self.configuration.url_handler_list()).get('Epub')
        if not handler:
            self.show_transient_notice('No Epub in URL (usilicrc)')
            return False
        directory = os.path.dirname(self.epub.path) if self.in_epub_body() else self.data_directory().rstrip('/')
        return self.start_shell_run(handler + [target, '-o', directory], None, [],
                                    on_done=lambda out: self.after_narou2epub(out))

    def after_narou2epub(self, output):
        path = output[-1].strip() if output else ''
        if path.endswith('.epub') and os.path.exists(path):
            return self.open_epub(path)
        return False

    def epub_marks_path(self):
        return os.path.join(os.path.expanduser('~'), '.usilic', 'epub-marks.txt')

    def read_epub_marks(self):
        try:
            with open(self.epub_marks_path(), encoding='utf-8', errors='replace') as f:
                return [l.rstrip('\n') for l in f if '.epub:' in l]
        except OSError:
            return []

    @staticmethod
    def parse_epub_mark(entry):
        """「本.epub:章のパス:位置: 文字列」を(本, 章のパス, 位置, 文字列)に。違えばNone。"""
        book, sep, rest = entry.partition('.epub:')
        path, _, rest = rest.partition(':')
        digits, sep2, text = rest.partition(': ')
        if not sep or not sep2 or not digits.isdigit():
            return None
        return book + '.epub', path, int(digits), text

    def epub_mark_place(self, chapter, offset, text, logical=None):
        """開いている本の記録の位置を(本全体の行, その行の中の字の範囲)に。無ければNone。"""
        chapters = {href: start for start, href in getattr(self.epub, 'chapter_files', [])}
        if chapter not in chapters:
            return None
        sources, pieces = logical or self.epub_logical_lines()
        place = ev.place_in_chapter([x.raw for x in sources], {x.raw: x.text for x in sources}, chapters[chapter],
                                    offset, text)
        if place is None:
            return None
        raw, pos = place
        g, pad, start = next((p for p in reversed(pieces.get(raw, [])) if p[1] is not None and p[2] <= pos),
                             (None, 0, 0))
        if g is None:
            return None
        shown = self.epub_lines[g].text
        begin = min(pos - start + pad, len(shown))
        return g, (begin, min(begin + len(text), len(shown)))

    def show_epub_marks(self):
        """^O^E: 記録(~/.usilic/epub-marks.txt、「本.epub:章のパス:位置: 文字列」)の一覧。本文からは今の本の記録を、
        記録した字を強調した本文の行の一覧に(C++のstartEpubMarkList()、^Mでその行へ)。他の画面からは全部の本の
        記録(^Mでその本を開いてその所へ)。"""
        entries = [e for e in (self.parse_epub_mark(x) for x in self.read_epub_marks()) if e]
        if self.in_epub_body():
            book = os.path.basename(self.epub.path)
            logical = self.epub_logical_lines()
            by_line = {}
            for name, chapter, offset, text in entries:
                if name != book:
                    continue
                found = self.epub_mark_place(chapter, offset, text, logical)
                if found is not None:
                    by_line.setdefault(found[0], []).append(found[1])
            if not by_line:
                self.show_transient_notice('No marks')
                return False
            lines = sorted(by_line)
            self.occur_items = [(g, self.epub_lines[g]) for g in lines]
            self.occur_global, self.occur_origin, self.occur_syntax = True, FILE, FIXED
            self.occur_byte_count = sum(len(self.epub_lines[g].text.encode('utf-8')) + 1 for g in lines)
            self.occur_view.set_lines([Line(self.epub_lines[g].text, i + 1,
                                            attributes=[(a, b, 'match') for a, b in by_line[g] if b > a])
                                       for i, g in enumerate(lines)])
            self.occur_view.cursor_index = self.occur_view.top_index = 0
            self.enter_mode(OCCUR)
            return True
        items = [x for x in self.read_epub_marks() if self.parse_epub_mark(x)]
        if not items:
            self.show_transient_notice('No marks')
            return False

        def choose(item):
            name, chapter, offset, text = self.parse_epub_mark(item)
            path = next((self.strip_tags(f) for f in self.ring(hist.RECENT_FILE)
                         if os.path.basename(self.strip_tags(f)) == name), name)  # FILEの履歴から本の場所を
            if not self.open_epub(path):
                return False
            found = self.epub_mark_place(chapter, offset, text)
            line = found[0] if found else self.epub.line_for(chapter)
            if line is not None:
                self.set_epub_cursor(line)
            return True
        return self.show_list(items, 'EPUB', choose, False)

    # --- EPUBの縦書き(epub_vertical.py) ---
    def vertical_active(self):
        """縦書きの本の本文を見ている(縦横は本が決める、OPFのpage-progression-direction="rtl"、C++と同じ)。"""
        return self.in_epub_body() and self.epub.right_to_left and self.epub_section is None

    def vertical_selecting(self):
        return (self.vertical is not None and self.vertical.selecting and self.current_mode == FILE
                and self.vertical_active() and self.prompt is None)

    def epub_logical_lines(self):
        """本全体の行(epub_lines)を元の行ごとの論理行につなぎ直す(C++のepubLogicalLines())。中央寄せ・字下げの行は
        頭の空白を除き、縦書きでは列の頭の空きにする。(SourceLineの並び, 元の行→[(本全体の行, 頭の空白の数,
        論理行の中での始まり)])。"""
        sources, pieces = [], {}
        hints = self.epub.hints
        for g, line in enumerate(self.epub_lines):
            raw = line.raw_line_number
            head = not sources or sources[-1].raw != raw
            pad = len(line.text) - len(line.text.lstrip(' ')) if raw in hints.center or raw in hints.indent else 0
            text = line.text[pad:]
            runs = [(max(0, a - pad), max(0, b - pad), t) for a, b, t in line.attributes]
            links = [(max(0, a - pad), max(0, b - pad), h) for a, b, h in line.links]
            if head:
                first, rest = hints.indent.get(raw, (0, 0))
                sources.append(ev.SourceLine(text, raw, runs, links, first, rest, raw in hints.align_end))
                pieces[raw] = [(g, pad, 0)]
                continue
            src = sources[-1]
            offset = len(src.text)
            src.text += text
            src.runs += [(a + offset, b + offset, t) for a, b, t in runs]
            for a, b, h in links:  # 折り返しで切れたリンクは1つに
                if src.links and src.links[-1][1] == a + offset and src.links[-1][2] == h:
                    src.links[-1] = (src.links[-1][0], b + offset, h)
                else:
                    src.links.append((a + offset, b + offset, h))
            pieces[raw].append((g, pad, offset))
        return sources, pieces

    def sync_vertical(self):
        """縦書きの列を(本・幅・行数・章ごとが変わっていれば)組み直し、横書きの側(FGREP・目次・リンク等)で
        動いていれば、その行を縦書きの右端に置く。"""
        key = (self.file_view.height, self.display.columns, self.epub_chapter_mode)
        line = self.global_epub_line()
        if self.vertical is None or key != self.vertical_key or self.vertical_source is not self.epub_lines:
            sources, self.vertical_pieces = self.epub_logical_lines()
            hints = self.epub.hints
            sources = [x for x in sources if self.epub_horizontal_range(x.raw) is None]  # 横書きの範囲は列にしない
            chapter_raws = {self.epub_lines[i].raw_line_number for i in self.epub_toc_lines
                            if 0 <= i < len(self.epub_lines)}
            self.vertical = ev.VerticalBook(sources, chapter_raws, self.file_view.height, self.display.columns,
                                            self.epub_chapter_mode, hints.centered, hints.horizontal)
            self.vertical_key, self.vertical_source, self.vertical_synced = key, self.epub_lines, None
        if line != self.vertical_synced:
            self.show_vertical_line(line)

    def show_vertical_line(self, line):
        if not 0 <= line < len(self.epub_lines):
            return
        raw = self.epub_lines[line].raw_line_number
        pos = next((start for g, _, start in self.vertical_pieces.get(raw, []) if g == line), 0)
        self.vertical.show_raw(raw, pos)
        self.vertical_synced = line

    def vertical_to_file(self):
        """縦書きの列カーソルの所へ、横書きの側(file_view、FILEの位置の記録・目次・^G)を合わせる。"""
        v = self.vertical
        if not v.cols:
            return
        column = v.cols[v.cursor]
        pos = next((cell.start for cell in column.cells if cell.start >= 0), 0)
        line = None
        for g, _, start in self.vertical_pieces.get(column.raw, []):
            if start <= pos:
                line = g
        if line is None:
            return
        self.set_epub_cursor(line)
        self.vertical_synced = line

    def vertical_highlights(self):
        """FGREP/EGREPの一致の字(元の行→論理行の中の範囲)。"""
        grep_on = self.grep_active or self.grep_prompt is not None or self.grep.persistent
        if not grep_on or self.grep.view is not self.file_view or not self.grep.pattern:
            return None
        offset = self.chapter_bounds(self.epub_chapter)[0] if self.epub_chapter_mode else 0
        out = {}
        for index in self.grep.matches:
            g = index + offset
            if not 0 <= g < len(self.epub_lines):
                continue
            line = self.epub_lines[g]
            for piece, pad, start in self.vertical_pieces.get(line.raw_line_number, []):
                if piece == g:
                    out.setdefault(line.raw_line_number, []).extend(
                        (a - pad + start, b - pad + start) for a, b in match_spans(line.text, self.grep.pattern,
                                                                                    self.grep.syntax))
        return out

    def render_vertical(self):
        self.sync_vertical()
        rows = self.vertical.render(self.vertical_highlights())
        self.vertical_view.set_lines([Line(text, None, attributes=runs) for text, runs in rows])
        self.vertical_view.cursor_index = self.vertical_view.top_index = 0

    def vertical_number(self, number):
        """縦書きの本文のK-番号(移動・^M・^␣)。該当しなければNone(ふつうの処理へ、横書きの側で動けば
        sync_vertical()が縦書きを合わせる)。"""
        self.sync_vertical()
        v = self.vertical
        last, self.vertical_last_number = self.vertical_last_number, number
        if number == 28:
            href = v.selected_href()
            if href is None:
                return False
            v.mark = v.cursor
            return self.follow_epub_link(href)
        if number == 36:
            v.mark = v.cursor
            self.show_transient_notice('Mark set')
            return True
        if number in (1, 2):  # j/k: 本の途中・端の横書きの範囲へも入る(C++のscrollEpubVertical())
            delta = 1 if number == 1 else -1
            edge = v.per_screen - 1 if not v.chapter_breaks and delta > 0 else 0
            from_column = v.first + edge
            if not v.step_first(delta):
                if not self.enter_epub_section_at_edge(delta):
                    return False
                return True
            if not self.enter_epub_section_between(from_column, v.first + edge, delta):
                self.vertical_to_file()
            return True
        if number == 11 and self.enter_epub_section_at_book_end():  # ^[<: 本の末尾の奥付等の最下行へ
            v.mark = v.cursor
            return True
        from_raw = v.current_raw()
        pages = (5, 6, 7, 8, 9, 10)
        result = v.command(number, repeated_page=last in pages and number in pages)
        if result is None:
            return None
        if result is False and number == 14:
            self.show_transient_notice('No mark set')
        if number in (3, 4, 5, 6, 7, 8, 9, 10) and self.maybe_enter_epub_section(from_raw, number in (3, 5, 6, 7)):
            return result
        self.vertical_to_file()
        return result

    # --- 縦書きの本の中の横書きの範囲(奥付等)・注釈の区画 ---
    def epub_horizontal_range(self, raw):
        return next((r for r in self.epub.hints.horizontal if r[0] <= raw <= r[1]), None)

    def epub_line_of_raw(self, raw):
        """元の行rawの(無ければその後の)最初の本全体の行。"""
        return next((g for g, line in enumerate(self.epub_lines) if line.raw_line_number >= raw),
                    len(self.epub_lines) - 1)

    def enter_epub_section(self, section, raw, from_below=False, from_above=False):
        """横書きの範囲へ入る(C++のenterEpubHorizontal()): カーソルをrawの行に置き、範囲の最初の行を画面の一番上に
        (範囲の途中/最後へ入る時はその行が見える所まで)。from_below: 縦書きから先へ送って入る時、範囲の最初の行を
        画面の一番下に置く。from_above: 前へ送って入る時、範囲の最後の行を画面の一番上に置く。"""
        self.epub_section = section
        raw = max(section[0], min(raw, section[1]))
        line = self.epub_line_of_raw(raw)
        self.set_epub_cursor(line)
        view, offset = self.file_view, line - self.file_view.cursor_index
        start = self.epub_line_of_raw(section[0]) - offset
        index = view.cursor_index
        view.top_index = max(start, index - max(1, view.height) + 1)
        self.epub_from_below = from_below and raw == section[0]
        if self.epub_from_below:
            view.top_index = max(0, start - max(1, view.height) + 1)
        if from_above and raw == section[1]:
            view.top_index = index
        self.epub_section_line = self.global_epub_line()

    def maybe_enter_epub_section(self, from_raw, forward):
        """縦書きで先へ/前へ動いた後: 動く前の列と今の列の間に横書きの範囲があれば(本の端でそれ以上動けない時は
        その先にあれば)、そこへ入る(C++のmaybeEnterEpubHorizontal())。"""
        v = self.vertical
        now = v.current_raw()

        def no_text_beyond(d):
            c = v.cursor + d
            while 0 <= c < len(v.cols):
                if not v.cols[c].padding:
                    return False
                c += d
            return True
        ranges = self.epub.hints.horizontal
        if forward:
            for r in ranges:
                if r[0] > from_raw and (now >= r[0] or no_text_beyond(1)):
                    self.enter_epub_section(r, r[0], from_below=True)
                    return True
        else:
            for r in reversed(ranges):
                if r[1] < from_raw and (now <= r[1] or no_text_beyond(-1)):
                    self.enter_epub_section(r, r[1])
                    return True
        return False

    def enter_epub_section_between(self, from_column, to_column, delta):
        """j/kで列を送った時、送る前と後の列の行の間に横書きの範囲(本の途中の付録等)があれば入る。"""
        v = self.vertical
        total = len(v.cols)
        from_column, to_column = min(from_column, total - 1), min(to_column, total - 1)
        if from_column < 0 or to_column < 0:
            return False
        a, b = v.cols[from_column].raw, v.cols[to_column].raw
        for r in self.epub.hints.horizontal:
            if (delta > 0 and r[0] > a and r[1] < b) or (delta < 0 and r[1] < a and r[0] > b):
                self.enter_epub_section(r, r[0] if delta > 0 else r[1], from_below=delta > 0, from_above=delta < 0)
                return True
        return False

    def enter_epub_section_at_edge(self, delta):
        """縦書きの列の端で、その先(前)に横書きの範囲があればそこへ入る(右端の列の行と比べる)。"""
        v = self.vertical
        if not 0 <= v.first < len(v.cols):
            return False
        edge = v.cols[v.first].raw
        ranges = self.epub.hints.horizontal
        target = next((r for r in ranges if r[0] > edge), None) if delta > 0 else \
            next((r for r in reversed(ranges) if r[1] < edge), None)
        if target is None:
            return False
        self.enter_epub_section(target, target[0] if delta > 0 else target[1], from_below=delta > 0)
        return True

    def enter_epub_section_at_book_end(self):
        """^[<: 縦書きの本文の最後より後ろに横書きの範囲(奥付等)があれば、その最下行(画面の一番下)へ。"""
        ranges = self.epub.hints.horizontal
        last_raw = max((c.raw for c in self.vertical.cols if not c.padding and c.cells), default=0)
        if not ranges or ranges[-1][0] <= last_raw:
            return False
        r = ranges[-1]
        self.enter_epub_section(r, r[1])
        view = self.file_view
        last = max((i for i, l in enumerate(view.lines) if r[0] <= l.raw_line_number <= r[1]), default=None)
        start = min((i for i, l in enumerate(view.lines) if r[0] <= l.raw_line_number <= r[1]), default=0)
        if last is not None:
            view.cursor_index = last
            view.top_index = max(start, last - max(1, view.height) + 1)
        self.epub_section_line = self.global_epub_line()
        return True

    def update_epub_section(self):
        """描く前に: 横書きの側で横書きの範囲の行へ動いていれば(目次・リンク・検索・開いた位置)範囲に入り、範囲を
        見ている間は範囲の外へ出たら縦書きへ戻る(C++のshowEpubVerticalRaw()・leaveEpubHorizontalIfOutside())。"""
        if self.epub_note_active() or not self.epub_lines:
            return
        line = self.global_epub_line()
        if self.epub_section is not None and line != self.epub_section_line and \
                self.last_key_number not in range(1, 13):
            self.epub_section = None  # 目次・リンク等で飛んだ(移動のキーではない)
            self.vertical_synced = None
        if self.epub_section is None:
            if line != self.vertical_synced:
                section = self.epub_horizontal_range(self.epub_lines[line].raw_line_number)
                if section is not None:
                    self.enter_epub_section(section, self.epub_lines[line].raw_line_number)
            return
        self.leave_epub_section_if_outside()
        if self.epub_section is not None:
            self.epub_section_line = self.global_epub_line()

    def leave_epub_section_if_outside(self):
        """横書きの範囲で、範囲の外へ出たら縦書きへ戻る(前へ出たら範囲の直前の行の最後の列、先へ出たらその後の行)。
        j/k等の画面を送るキーは範囲が画面に見えているかで、↑↓・ページ送りはカーソルの行で決める。"""
        section, view = self.epub_section, self.file_view
        lines, number = view.lines, self.last_key_number
        scrolling = number in (1, 2)
        by_visibility = scrolling or number not in (3, 4, 5, 6, 7, 8, 9, 10)
        if not lines:
            return
        raw = lines[view.cursor_index].raw_line_number
        top = view.top_index
        top_raw = lines[top].raw_line_number if 0 <= top < len(lines) else raw
        height = max(1, view.height)
        first_index = next((i for i, l in enumerate(lines) if l.raw_line_number >= section[0]), len(lines))
        if top >= first_index:
            self.epub_from_below = False
        last_index = next((i for i in range(len(lines) - 1, -1, -1) if lines[i].raw_line_number <= section[1]), -1)
        if by_visibility:
            before, after = first_index >= top + height, last_index < top
        else:
            before = raw < section[0] or (not self.epub_from_below and top_raw < section[0])
            after = raw > section[1]
        if not before and not after:
            return
        if before:
            raw = section[0] - 1
        elif scrolling or (by_visibility and raw <= section[1] + height):
            raw = section[1] + 1
        self.epub_section = None
        other = self.epub_horizontal_range(raw)
        if other is not None:
            self.enter_epub_section(other, raw)
            return
        self.vertical_synced = None  # 組み直しは幅等が変わった時だけ(sync_vertical())
        self.set_epub_cursor(self.epub_line_of_raw(max(1, raw)))
        self.sync_vertical()
        v = self.vertical
        if before:
            col = max((c for c, column in enumerate(v.cols) if not column.padding and column.raw <= raw), default=0)
            after_columns = any(not column.padding and column.raw > section[1] for column in v.cols)
            if v.chapter_breaks or not after_columns:
                v.set_view(col, col)
            else:
                v.set_view(col - v.per_screen + 1, col)
        elif raw == section[1] + 1 and not v.chapter_breaks:
            col = next((c for c, column in enumerate(v.cols) if not column.padding and column.raw >= raw), None)
            if col is not None:
                v.set_view(col, col)
        self.vertical_to_file()

    def epub_note_active(self):
        if self.epub_note is None:
            return False
        if self.file_view.lines is self.epub_note['shown'] and self.in_epub_body():
            return True
        self.epub_note = None
        return False

    def show_epub_note(self, raw):
        """元の行rawが注釈の区画(入れ子なら一番狭い物)の中なら、file_viewをその注釈の行だけにする(縦書きの本では
        横書きの範囲として横書きで見せる)。^Gで元の所へ(C++のshowEpubNoteAt())。"""
        notes = [r for r in self.epub.hints.notes if r[0] <= raw <= r[1]]
        if not notes:
            return False
        first, until = min(notes, key=lambda r: r[1] - r[0])
        note = [l for l in self.epub_lines if first <= l.raw_line_number <= until]
        while note and not note[-1].text.strip(' '):
            note.pop()  # 注釈の後ろの空行
        if not note:
            return False
        if self.epub_note_active():
            origin = self.epub_note
            self.leave_epub_note()
        else:
            v = self.vertical if self.vertical_active() else None
            origin = {'lines': self.file_view.lines, 'top': self.file_view.top_index,
                      'cursor': self.file_view.cursor_index, 'section': self.epub_section,
                      'vertical': (v.first, v.cursor) if v is not None else None, 'chapter': self.epub_chapter}
        if self.epub.right_to_left:
            self.epub_section = (first, until)
        self.file_view.set_lines(note)
        self.file_view.cursor_index = self.file_view.top_index = 0
        origin['shown'] = self.file_view.lines
        self.epub_note = origin
        return True

    def leave_epub_note(self):
        """注釈から本文の行と、注釈へ飛ぶ前の位置へ。"""
        note, self.epub_note = self.epub_note, None
        self.file_view.set_lines(note['lines'])
        self.epub_chapter = note['chapter']
        self.file_view.cursor_index, self.file_view.top_index = note['cursor'], note['top']
        self.epub_section = note['section']
        self.epub_section_line = self.global_epub_line()
        if note['vertical'] is not None and self.vertical_active():
            self.sync_vertical()
            self.vertical.set_view(*note['vertical'])
            self.vertical_synced = self.global_epub_line()
        return True

    def feed_vertical_copy_key(self, byte, special):
        """縦書きのCOPYのキー。^N/←で次の列(左)、^P/→で前の列、^F/↓で下のマス、^B/↑で上のマス、^A/^Eで
        列の頭/末、^V/^[vで1画面先/前。^@で範囲を始め、もう一度で閉じてRecord?(y/n)(yで記録)。^[wで
        クリップボードへ、^[tでTOOLへ、^Gでやめる。"""
        v = self.vertical
        if v.asking:
            v.asking = False
            if special is None and byte in (ord('y'), ord('Y')):
                return self.record_vertical_selection()
            return True
        if special is not None:
            columns, cells = {'Up': (0, -1), 'Down': (0, 1), 'Left': (1, 0), 'Right': (-1, 0),
                              'PageUp': (-v.per_screen, 0), 'PageDown': (v.per_screen, 0)}[special]
            v.move_point(columns, cells)
            return True
        prefix, self.vertical_prefix = self.vertical_prefix, None
        if prefix == 'ESC':
            if byte in (ord('w'), ord('t')):
                text = v.selection_text(' ' if byte == ord('t') else '\n')
                v.end_select()
                if not text:
                    self.show_transient_notice('No string')
                    return False
                return self.deliver_copied_text(text, byte == ord('t'))
            if byte == ord('v'):
                v.move_point(-v.per_screen, 0)
            return True
        moves = {0x0E: (1, 0), 0x10: (-1, 0), 0x06: (0, 1), 0x02: (0, -1), 0x16: (v.per_screen, 0)}
        if byte == ESC:
            self.vertical_prefix = 'ESC'
        elif byte in (0, CTRL_SPACE):
            v.toggle_anchor()
        elif byte == CTRL_G:
            v.end_select()
        elif byte in moves:
            v.move_point(*moves[byte])
        elif byte in (0x01, 0x05):
            v.point_to_column_edge(byte == 0x05)
        return True

    def record_vertical_selection(self):
        """範囲の字を~/.usilic/epub-marks.txtへ「本.epub:章のパス:位置: 文字列」で記録する(位置は章の頭からの
        字数、C++のrecordEpubSelection()と同じ書式、^O^Eの一覧で見る)。"""
        v = self.vertical
        ranges = v.selection_ranges()
        text = v.selection_text()
        v.end_select()
        if not text or not ranges:
            self.show_transient_notice('No string')
            return True
        raw, pos = ranges[0][0], ranges[0][1]
        chapter = None
        for start, href in self.epub.chapter_files:
            if start <= raw:
                chapter = (start, href)
        if chapter is None:
            self.show_transient_notice('No chapter')
            return True
        path = self.epub_marks_path()
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'a', encoding='utf-8') as f:
                f.write('%s:%s:%d: %s\n' % (os.path.basename(self.epub.path), chapter[1],
                                            v.offset_in_chapter(chapter[0], raw, pos), text))
        except OSError:
            self.show_transient_notice('Cannot write ' + path)
            return True
        self.show_transient_notice('Recorded: ' + text[:20])
        return True

    # --- マウス(C++のapp/main.cppのrunTerminalPhase()・runTerminalMouseDrag()・runCoastLoop()) ---
    def sync_mouse_tracking(self):
        """SHELL以外ではマウスを追う(SHELLでは端末の選択・貼り付けをそのまま使えるよう追わない)。"""
        want = self.current_mode != SHELL
        if hasattr(self.display, 'set_mouse_tracking') and getattr(self.display, 'mouse_on', None) != want:
            self.display.set_mouse_tracking(want)

    def mouse_view(self):
        """マウスが相手にする画面(縦書きは描いた画面ではなく列の並びなので別に扱う)。"""
        return self.active_view()

    def vertical_scroll_active(self):
        """縦書きの本文・一覧(ホイール・ドラッグ・慣性は列ごと)。"""
        return self.prompt is None and ((self.current_mode == FILE and self.vertical_active())
                                        or self.vertical_list_active())

    def copy_mouse_active(self):
        return self.copy is not None or self.vertical_selecting()

    def handle_mouse(self, event):
        """マウスの報告1つ。慣性中・ドラッグ中はその続き。"""
        self.notice = None
        if self.coast is not None:
            return self.mouse_during_coast(event)
        if self.drag is not None:
            return self.mouse_during_drag(event)
        if event.wheel:
            return self.scroll_by_wheel(event.wheel)
        if event.pressed and not event.motion and event.button == 2:  # 右クリック: ^G
            return self.feed_key(CTRL_G, None)
        if event.pressed and not event.motion and event.button == 0:
            return self.start_mouse_drag(event)
        if event.motion and event.button == 3 and self.prompt is None and self.grep_prompt is None:
            return self.hover(event.row)
        return False

    def hover(self, row):
        """ボタンを押していない動き: ポインタの行にカーソルを合わせる(C++のapplyHoverRowAndRedraw())。"""
        self.click_targets_cursor = False
        if self.vertical_scroll_active() or self.copy_mouse_active() or self.current_mode == SHELL:
            return False
        view = self.mouse_view()
        index = view.line_index_at_row(row)
        if index < 0 or index == view.cursor_index:
            return False
        view.cursor_index = index
        return True

    def scroll_by_wheel(self, direction, lines=1):
        """ホイールの1刻み: j/kと同じく1行(縦書きは1列、縦書きの一覧は1列)。端では何もしない。"""
        if self.wheel_reversed:
            direction = -direction
        if self.vertical_scroll_active():
            for _ in range(lines):
                if not self.scroll_epub_vertical(direction):
                    break
            return True
        view = self.mouse_view()
        for _ in range(lines):
            if direction < 0:
                if view.top_index <= 0:
                    break
                self.handle_key_number(2, None)
            else:
                if self.current_mode == FILE and self.file_mode.has_active_stream():
                    self.file_mode.ensure_loaded_through(view.top_index + 2 * view.height)
                if view.top_index >= view.max_top():
                    break
                self.handle_key_number(1, None)
        return True

    def scroll_epub_vertical(self, delta):
        """縦書きを1列ずらす(ホイール・ドラッグ・慣性、C++のscrollEpubVertical())。動いたらTrue。"""
        if self.vertical_list_active():
            v = self.vlist
            total = len(v.cols)
            first = max(0, min(v.first + delta, max(0, total - v.per_screen)))
            if first == v.first or total == 0:
                return False
            self.vlist_first[self.current_mode] = first
            col = min(first + v.per_screen, total) - 1 if delta > 0 else first
            source = self.vlist_source()
            source.cursor_index = max(0, min(v.cols[col].raw - 1, len(source.lines) - 1))
            self.render_vertical_list(keep_first=True)
            return True
        if not (self.current_mode == FILE and self.vertical_active()):
            return False
        before = (self.vertical.first, self.epub_section)
        self.last_key_number = 1 if delta > 0 else 2  # 画面を送る操作(j/k)として
        self.vertical_number(1 if delta > 0 else 2)
        return (self.vertical.first, self.epub_section) != before

    # --- 左ボタン(ドラッグ・クリック) ---
    def start_mouse_drag(self, event):
        if self.copy_mouse_active():
            self.copy_mouse_down(event.row, event.column)
            self.drag = {'copy': True}
            return True
        self.drag = {'copy': False, 'row': event.row, 'column': event.column, 'moved': False, 'samples': [],
                     'columns': self.vertical_scroll_active(), 'accum': 0,
                     'targets_cursor': self.click_targets_cursor}
        self.click_targets_cursor = False
        return True

    def mouse_during_drag(self, event):
        d = self.drag
        if event.wheel or event.button < 0:
            return False  # ドラッグ中のホイールは無視
        if d['copy']:
            if event.motion:
                self.copy_mouse_drag(event.row, event.column)
            elif not event.pressed:
                self.drag = None
                self.copy_mouse_up()
            return True
        now = time.monotonic()
        if event.motion:
            if d['columns']:
                d['accum'] += event.column - d['column']
                d['column'] = event.column
                while abs(d['accum']) >= ev.PITCH:
                    direction = 1 if d['accum'] > 0 else -1
                    d['accum'] -= direction * ev.PITCH
                    d['moved'] = True
                    d['samples'].append((now, direction))
                    if not self.scroll_epub_vertical(direction):
                        d['accum'] = 0
                    if not self.vertical_scroll_active():  # 横書きの範囲へ入った: 上下のドラッグとして続ける
                        d.update(columns=False, row=event.row, samples=[])
                        break
                return True
            delta = event.row - d['row']
            d['row'] = event.row
            if delta:
                d['moved'] = True
                self.drag_rows(delta)
                d['samples'].append((now, delta))
                if self.vertical_scroll_active():  # 横書きの範囲の外へ出た: 縦書きの左右のドラッグとして続ける
                    d.update(columns=True, column=event.column, accum=0, samples=[])
            return True
        if event.pressed:
            return False  # 押したままもう一度押した
        self.drag = None
        if not d['moved']:
            view = self.mouse_view()
            if d['targets_cursor'] and not d['columns']:
                row = view.cursor_index - view.top_index
                if 0 <= row < view.height:
                    return self.handle_mouse_click(row, 0)
            return self.handle_mouse_click(event.row, event.column)
        recent = sum(v for t, v in d['samples'] if t >= now - 0.15)  # 離す直前(150ms)の動き
        level = level_for_release_distance(recent)
        if level == 0 or (d['columns'] and not self.vertical_scroll_active()):
            return True
        # 上下は自然なスクロール(下へ引くと前が見える)、左右は引いた向きへ列を送る
        direction = (1 if recent > 0 else -1) if d['columns'] else (-1 if recent > 0 else 1)
        return self.start_coast(level, direction, from_drag=True)

    def drag_rows(self, delta):
        """上下のドラッグ: 画面を指の向きに送り(下へ引くと前が見える)、カーソルは送る向きの端の行。"""
        view = self.mouse_view()
        before = view.top_index
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.ensure_loaded_through(before - delta + 2 * view.height)
        view.top_index = max(0, min(before - delta, view.max_top()))
        view.cursor_index = min(view.top_index + view.height - 1, len(view.lines) - 1) if delta < 0 else \
            view.top_index
        self.last_key_number = 1 if delta < 0 else 2  # 横書きの範囲の出入りは画面を送る操作(j/k)として
        self.update_scrolled_section()

    def update_scrolled_section(self):
        """画面を送った後に、縦書きの本の横書きの範囲から出たか(描く前に見る、update_epub_section())。"""
        if self.current_mode == FILE and self.in_epub_body() and self.epub.right_to_left:
            self.epub_section_line = self.global_epub_line() if self.epub_section is not None else None
            self.update_epub_section()

    def handle_mouse_click(self, row, column):
        """クリック(C++のhandleMouseClickImpl()): 一覧はその行を選び、選んでいる行ならそれを開く。縦書きは列を選び、
        選んでいる列のリンク・リンクの字ならリンクへ。横書きのEPUBはリンクの字ならリンクへ。"""
        if self.vertical_selecting() or self.prompt is not None or self.grep_prompt is not None:
            return True
        mode = self.current_mode
        if mode == FILE and self.vertical_active():
            return self.click_epub_vertical(row, column)
        if self.vertical_list_active():
            return self.click_vertical_list(column)
        if mode == FILE and self.in_epub_body():
            index = self.file_view.line_index_at_row(row)
            return index >= 0 and self.click_epub_horizontal_link(index, column)
        if mode in (DIRECTORY, DU, ARCHIVE, LOCATE, LIST, TOC, PART_TOC, OCCUR, INFO):
            view = self.mouse_view()
            index = view.line_index_at_row(row)
            if index < 0:
                return False
            if index == view.cursor_index:
                return self.enter_key()
            view.cursor_index = index
            return True
        return False

    def click_epub_vertical(self, row, column):
        """縦書きの本文: リンクの字ならそのリンクへ。選んでいる列なら(リンク・画像の列は)開き、他の列は列カーソルを
        動かすだけ(C++のclickEpubVertical())。"""
        self.sync_vertical()
        v = self.vertical
        k = ev.column_at(column, self.display.columns)
        c = v.first + k
        if k < 0 or c >= len(v.cols):
            return False
        cells = v.cols[c].cells
        if 0 <= row < len(cells) and cells[row].link >= 0:
            v.set_view(v.first, c, cells[row].link)
            self.vertical_to_file()
            return self.vertical_number(28)
        if c == v.cursor:
            return self.vertical_number(28) if v.selected_href() is not None else True
        v.set_view(v.first, c)
        self.vertical_to_file()
        return True

    def click_epub_horizontal_link(self, index, column):
        """横書きのEPUBの本文: 下線(リンク)の字のクリックはそのリンクへ。"""
        line = self.file_view.lines[index]
        from .copy_mode import index_at_column
        pos = index_at_column(line.text, column)
        if pos >= len(line.text):
            return False
        for start, end, href in line.links:
            if start <= pos < end:
                self.file_view.cursor_index = index
                return self.follow_epub_link(href)
        return False

    # --- COPYのドラッグ(C++のcopyMouseDown()・copyMouseDrag()・copyMouseUp()) ---
    def copy_mouse_down(self, row, column):
        if self.vertical_selecting():  # 縦書きのCOPY: 押した所から範囲を始め直す
            v = self.vertical
            v.asking = False
            v.anchor = v.point = v.cell_at(row, column)
            return
        view = self.copy.view
        index = view.line_index_at_row(row)
        if index < 0:  # 最後の行より下の余白は最後の行へ
            index = len(view.lines) - 1
        if index >= 0:
            self.copy.begin_or_extend_from_mouse(index, column)

    def copy_mouse_drag(self, row, column):
        if self.vertical_selecting():
            v = self.vertical
            if v.anchor is not None:
                v.point = v.cell_at(row, column)
            return
        view = self.copy.view
        index = view.line_index_at_row(row)
        if index < 0:
            index = len(view.lines) - 1
        if index >= 0:
            self.copy.place_cursor_at(index, column)

    def copy_mouse_up(self):
        """縦書きのCOPY: 範囲があれば(押した所から動かしていれば)Record?(y/n)と問う。横書きは範囲を残すだけ。"""
        if self.vertical_selecting():
            v = self.vertical
            if v.anchor is None or v.anchor == v.point:
                v.anchor = None
            else:
                v.asking = True

    # --- 慣性 ---
    def start_coast(self, level, direction, from_drag=False):
        level = min(level + (self.speeds.drag_offset() if from_drag else 0), self.speeds.max_level())
        self.coast = {'level': max(1, level), 'direction': direction, 'columns': self.vertical_scroll_active(),
                      'due': time.monotonic() + self.speeds.seconds_per_row(max(1, level))}
        return True

    def start_coast_by_arrow(self, special):
        """←→(縦書きの本文は↓↑)で慣性を始める(段1、→・↓で先へ)。縦書きの一覧・SHELL・COPY・入力中は今までどおり。"""
        if self.current_mode == SHELL or self.copy_mouse_active() or self.prompt is not None or \
                self.grep_prompt is not None or self.grep_active or self.pending_keys or \
                self.goto_digits is not None or self.register_state is not None:
            return False
        vertical_body = self.current_mode == FILE and self.vertical_active()
        trigger = ('Down', 'Up') if vertical_body else () if self.vertical_list_active() else ('Right', 'Left')
        if special not in trigger:
            return False
        horizontal_epub = self.current_mode == FILE and self.in_epub_body() and not vertical_body
        forward = special == 'Down' or special == ('Left' if horizontal_epub else 'Right')
        return self.start_coast(1, 1 if forward else -1)

    def adjust_coast_by_arrow(self, special):
        c = self.coast
        if c['columns']:
            forward = special in ('Down', 'Left')
        elif self.current_mode == FILE and self.in_epub_body():
            forward = special in ('Left', 'Up')
        else:
            forward = special in ('Right', 'Up')
        c['level'], c['direction'] = step_level_by_key(c['level'], c['direction'], 1 if forward else -1,
                                                       self.speeds.max_level())
        return True

    def mouse_during_coast(self, event):
        """慣性中のマウス: ホイールは止めて1刻み、右クリックは止めて^G、クリックは止めるだけ(行の字の上なら、
        止めてハイライト行をクリックしたことに。次のクリックはハイライト行へ)。動き・離すは何もしない。"""
        if event.wheel:
            self.coast = None
            return self.scroll_by_wheel(event.wheel)
        if event.pressed and not event.motion and event.button == 2:
            self.coast = None
            return self.feed_key(CTRL_G, None)
        if event.pressed and not event.motion:
            columns = self.coast['columns']
            self.coast = None
            if columns:
                return True
            view = self.mouse_view()
            index = view.line_index_at_row(event.row)
            on_text = index >= 0 and utf8.text_width(view.lines[index].text) > event.column
            if on_text:
                row = view.cursor_index - view.top_index
                if 0 <= row < view.height:
                    return self.handle_mouse_click(row, 0)
            else:
                self.click_targets_cursor = True
            return True
        return False

    def coast_step(self):
        """慣性で1行(縦書きは1列)進める。端で止まる。"""
        c = self.coast
        if c['columns']:
            if not self.scroll_epub_vertical(c['direction']):
                self.coast = None
                return
            if not self.vertical_scroll_active():  # 横書きの範囲へ入った: 上下の慣性として続ける
                c['columns'] = False
        else:
            view = self.mouse_view()
            before = view.top_index
            if self.current_mode == FILE and self.file_mode.has_active_stream():
                self.file_mode.ensure_loaded_through(before + 2 * view.height)
            view.top_index = max(0, min(before + c['direction'], view.max_top()))
            if view.top_index == before:
                self.coast = None
                self.redraw()
                return
            view.cursor_index = min(view.top_index + view.height - 1, len(view.lines) - 1) \
                if c['direction'] > 0 else view.top_index
            self.last_key_number = 1 if c['direction'] > 0 else 2
            self.update_scrolled_section()
            if self.vertical_scroll_active():  # 横書きの範囲から出た: 縦書きの左右の慣性として続ける
                c['columns'] = True
        c['due'] = time.monotonic() + self.speeds.seconds_per_row(c['level'])
        self.redraw()

    # --- ファイルのドロップ(括弧付き貼り付け) ---
    def handle_paste(self, text):
        """貼り付けた字がファイルのパスなら開く(1つなら^Xfと同じ、いくつもならUSILICの一覧)。違えば、またはプロンプト・
        SHELLの中なら、打った字として渡す。"""
        paths = dropped_paths(text)
        if paths and self.current_mode != SHELL and self.prompt is None and self.grep_prompt is None:
            if len(paths) == 1:
                return self.open_path(paths[0])
            if not self.show_list(paths, 'USILIC', self.open_usilic_item, False):
                return False
            self.list_view.cursor_index = self.list_view.top_index = 0  # 最初のファイルを開く(^Gで一覧へ)
            return self.enter_key()
        for b in text.encode('utf-8'):
            self.feed_key(b, None)
        return True

    # --- 縦書きの章の一覧・検索結果の一覧(C++のrefreshEpubTocVertical()・handleEpubTocVerticalCommand()) ---
    def vertical_list_active(self):
        """縦書きの本の章の一覧(TOC)か、縦書きの本文から作った一致の一覧・記録の一覧(OCCUR)を見ている。"""
        if self.epub is None or not self.epub.right_to_left or self.file_mode.current_file != self.epub.path:
            return False
        if self.current_mode == TOC:
            return True
        return self.current_mode == OCCUR and self.occur_origin == FILE

    def vlist_source(self):
        return self.toc_view if self.current_mode == TOC else self.occur_view

    def vlist_cursor_column(self):
        source = self.vlist_source()
        return next((c for c, col in enumerate(self.vlist.cols) if col.raw == source.cursor_index + 1), 0)

    def render_vertical_list(self, keep_first=False):
        """一覧の1行を1つの論理行として本文と同じ組版で列に並べ、選んでいる行の最初の列を反転表示する。選んでいる
        行が画面の外なら右端の列を動かす(keep_first: 動かさない、ホイール等の1列送り)。"""
        source = self.vlist_source()
        key = (id(source.lines), len(source.lines), self.file_view.height, self.display.columns)
        if self.vlist is None or self.vlist_key != key:
            sources = []
            for i, line in enumerate(source.lines):
                runs = list(line.attributes)
                if self.current_mode == OCCUR and not self.occur_global:  # 一致した字の色
                    runs = [(a, b, 'match') for a, b in match_spans(line.text, self.grep.pattern,
                                                                    self.occur_syntax)] + runs
                pad = len(line.text) - len(line.text.lstrip(' '))
                sources.append(ev.SourceLine(line.text.strip(' '), i + 1,
                                             [(max(0, a - pad), max(0, b - pad), t) for a, b, t in runs]))
            self.vlist = ev.VerticalBook(sources, set(), self.file_view.height, self.display.columns, False)
            self.vlist_key = key
        v = self.vlist
        cursor = self.vlist_cursor_column()
        first = self.vlist_first.get(self.current_mode, 0)
        if not keep_first and cursor < first:
            first = cursor
        if not keep_first and cursor >= first + v.per_screen:
            first = cursor - v.per_screen + 1
        v.first = max(0, min(first, max(0, len(v.cols) - v.per_screen)))
        self.vlist_first[self.current_mode] = v.first
        rows = v.render(highlight_column=cursor)
        self.vlist_view.set_lines([Line(text, None, attributes=runs) for text, runs in rows])
        self.vlist_view.cursor_index = self.vlist_view.top_index = 0

    def select_vertical_list(self, index):
        source = self.vlist_source()
        source.cursor_index = max(0, min(index, len(source.lines) - 1))
        source.ensure_cursor_visible()
        return True

    def vertical_list_number(self, number):
        """縦書きの一覧のK-番号: ^N・j・←で次、^P・k・→で前、ページ送り、^[>で先頭・^[<で末尾(本文と同じく逆)。"""
        source = self.vlist_source()
        if self.vlist is None:
            self.render_vertical_list()
        v, index, count = self.vlist, source.cursor_index, len(source.lines)

        def at_column(c):
            if not v.cols:
                return 0
            return v.cols[max(0, min(c, len(v.cols) - 1))].raw - 1
        cursor = self.vlist_cursor_column()
        wrap = self.current_mode == OCCUR  # 縦書きの一致の一覧の^N/^Pは巡回(章の一覧は端で止まる、C++と同じ)
        if number == 3 and wrap and index >= count - 1:
            return self.select_vertical_list(0)
        if number == 4 and wrap and index <= 0:
            return self.select_vertical_list(count - 1)
        if number in (1, 3):
            return self.select_vertical_list(index + 1)
        if number in (2, 4):
            return self.select_vertical_list(index - 1)
        if number in (5, 6, 7):
            return self.select_vertical_list(max(index + 1, at_column(cursor + v.per_screen)))
        if number in (8, 9, 10):
            return self.select_vertical_list(min(index - 1, at_column(cursor - v.per_screen)))
        if number == 12:
            return self.select_vertical_list(0)
        if number == 11:
            return self.select_vertical_list(count - 1)
        return None

    def click_vertical_list(self, column):
        """縦書きの一覧のクリック: その列の行を選び、選んでいる行なら開く。"""
        if self.vlist is None:
            self.render_vertical_list()
        k = ev.column_at(column, self.display.columns)
        c = self.vlist.first + k
        if k < 0 or c >= len(self.vlist.cols):
            return False
        index = self.vlist.cols[c].raw - 1
        if index == self.vlist_source().cursor_index:
            return self.enter_key()
        return self.select_vertical_list(index)

    # --- 実行ループ(main.cppのrunTerminalPhase()) ---
    def run(self, resized):
        self.layout()
        self.redraw()
        while not self.wants_exit:
            self.sync_mouse_tracking()
            fds = [self.key_input.fd] + ([self.shell.fd] if self.shell.alive() else [])
            wait = 0.1
            if self.coast is not None:  # 慣性: 次の1行(1列)の時刻まで
                wait = max(0.0, min(wait, self.coast['due'] - time.monotonic()))
            try:
                readable, _, _ = select.select(fds, [], [], 0 if self.key_input.pending else wait)
            except InterruptedError:
                readable = []
            if self.coast is not None and time.monotonic() >= self.coast['due'] and not self.key_input.pending \
                    and self.key_input.fd not in readable:
                self.coast_step()
                continue
            if resized():
                self.layout()
                self.redraw()
            if self.shell.alive() and self.shell.fd in readable:
                if not self.shell.on_pty_readable() and self.current_mode == SHELL:
                    self.go_back()  # bashが終わった
                self.check_shell_run()
                if self.current_mode == SHELL:
                    self.refresh_shell_view()
                self.redraw()
            key = self.key_input.read_key(timeout=0) if self.key_input.fd in readable or self.key_input.pending else None
            if key is None:
                self.poll_prefix_echo()
                continue
            self.prefix_shown = False
            self.feed_key(*key)
            self.redraw()
        if getattr(self.display, 'mouse_on', False):
            self.display.set_mouse_tracking(False)
        self.shell.terminate()
        while len(self.archives) > 1:  # 入れ子のアーカイブの一時ファイル
            self.archive_back()
        if os.path.isdir(self.work_root):  # このセッションの作業ファイル(WORK)
            import shutil
            shutil.rmtree(self.work_root, ignore_errors=True)

    def poll_prefix_echo(self):
        """Application::pollPrefixEcho(): PREFIXのまま1秒経ったらモードラインへ。"""
        label = self.pending_prefix_label()
        if not label:
            self.prefix_since = None
            return
        now = time.monotonic()
        if self.prefix_since is None:
            self.prefix_since = now
        elif not self.prefix_shown and now - self.prefix_since >= 1.0:
            self.prefix_shown = True
            self.notice = label
            self.redraw()
