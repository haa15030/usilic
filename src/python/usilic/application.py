"""モードの切り替えとキーのディスパッチ(C++のapplication.cppのApplication)。

今回の範囲は(ls)(Directory)とFILE(ページャー)。キーはC++版のresolveCommand()・dispatchByte()・
dispatchLeaderByte()と同じ割り当て(HELPの「ページャーで機能する主なキー」と(ls)の基本)。
"""
import os
import select
import time

from . import mode_line, utf8
from .content_view import ContentView, wrap_text, expand_tabs
from .directory_mode import DirectoryMode
from .file_mode import FileMode
from .line_editor import LineEditor
from .messages_ja import localized
from .shell import Shell
from .grep_mode import GrepMode, FIXED, REGEX, match_spans
from . import history as hist

PROGRAM_LABEL = 'usilic-1.5b'
DIRECTORY, FILE, SHELL = 'Directory', 'File', 'Shell'
CTRL_G, CTRL_J, CTRL_X, ESC, CR = 0x07, 0x0A, 0x18, 0x1B, 0x0D


class Command:
    """C++のenum class Commandのうち今回使うもの。"""
    MOVE_DOWN, MOVE_UP = 'MoveDown', 'MoveUp'
    SCROLL_LINE_DOWN, SCROLL_LINE_UP = 'ScrollLineDown', 'ScrollLineUp'
    PAGE_DOWN, PAGE_UP = 'PageDown', 'PageUp'
    GO_TO_TOP, GO_TO_BOTTOM = 'GoToTop', 'GoToBottom'
    ENTER, BACK, REDRAW = 'Enter', 'Back', 'Redraw'
    NEXT_FILE, PREVIOUS_FILE, FIRST_FILE, LAST_FILE, QUIT_PAGER = 'NextFile', 'PreviousFile', 'FirstFile', 'LastFile', 'QuitPager'
    PARENT_DIRECTORY, CURRENT_DIRECTORY, EXIT, EXCHANGE_POINT = 'ParentDirectory', 'CurrentDirectory', 'Exit', 'ExchangePoint'
    NONE = None


class Application:
    def __init__(self, display, key_input):
        self.display = display
        self.key_input = key_input
        self.directory_view = ContentView()
        self.file_view = ContentView()
        self.file_view.cursor_highlight = False  # FileModeのコンストラクタと同じ(ページャーは反転しない)
        self.directory_mode = DirectoryMode(self.directory_view)
        self.file_mode = FileMode(self.file_view)
        self.current_mode = DIRECTORY
        self.pager_launch = False   # 引数のfile・標準入力で起動した(Application::pagerLaunch_)
        self.pager_files = []       # n/p/x/fで切り替えるfile
        self.pager_index = 0
        self.start_directory = os.getcwd()
        self.wants_exit = False
        self.notice = None          # showTransientNotice()の文(次のキーで消える)
        # PREFIX(C++のleaderJPending_等)
        self.leader_j = self.leader_x = self.esc = False
        self.goto_digits = None     # ^[Nの数字(gotoLineDigits_)
        self.prompt = None          # 終了の確認の入力(PromptKind::Exit)
        self.mark_before_jump = None  # ^X^Xの戻り先(markBeforePageScroll()等)
        self.prefix_since = None
        self.prefix_shown = False
        # SHELL(Application::shell_、shellReturnMode_)
        self.shell = Shell()
        self.shell_view = ContentView()
        self.shell_view.cursor_highlight = False
        self.command_line = LineEditor()
        self.shell_return_mode = DIRECTORY
        self.history = None  # ディレクトリで起動した時だけ(usilic.py)
        self.utf8_pending = bytearray()
        # FGREP/EGREP(grepInPlaceActive_、PromptKind::Fgrep/Egrep)
        self.grep = GrepMode()
        self.grep_active = False
        self.grep_prompt = None   # (syntax, LineEditor)を打っている間

    # --- 画面 ---
    def active_view(self):
        if self.current_mode == SHELL:
            return self.shell_view
        return self.file_view if self.current_mode == FILE else self.directory_view

    def refresh_shell_view(self, follow=True):
        """SHELLの行を作り直す。followなら最後(プロンプト)へ(出力が来た時)。"""
        at_end = self.shell_view.cursor_index >= len(self.shell_view.lines) - 1
        self.shell_view.set_lines(self.shell.view_lines(self.wrap_width(), self.command_line.text))
        if follow or at_end:
            self.shell_view.go_to_bottom()

    def layout(self):
        rows, columns = self.display.refresh_size()
        for view in (self.directory_view, self.file_view, self.shell_view):
            view.set_height(rows - 1)
        self.file_mode.rewrap(self.wrap_width())
        if self.shell.alive():
            self.shell.on_resize(rows - 1, columns)
            self.refresh_shell_view()

    def wrap_width(self):
        """折り返しの幅(桁いっぱい、C++版と同じ)。"""
        return max(1, self.display.columns)

    def mode_line_text(self):
        """モードライン(ContentView::ModeLineFields)。欄の間は空白3つ。"""
        view = self.active_view()
        if self.prompt is not None:
            return 'EXIT   ' + localized('Exit? (y/n) ') + self.prompt
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
        position = self.notice if self.notice else None
        if self.current_mode == SHELL:
            pos = position or mode_line.format_fixed_width(view.cursor_index + 1, len(view.lines))
            return self.fit_mode_line(['SHELL', pos, str(self.shell.byte_count), '(bash)',
                                       self.shell.current_directory or self.directory_mode.current_directory])
        if self.current_mode == FILE:
            total = len(view.lines)
            pos = position or (mode_line.format_fixed_width(view.cursor_index + 1, total) if self.pager_launch
                               else mode_line.format(view.cursor_index + 1, total))
            name = self.file_mode.current_file if self.pager_launch else os.path.basename(self.file_mode.current_file)
            tail = PROGRAM_LABEL if self.pager_launch else self.directory_mode.current_directory
            return self.fit_mode_line([name, pos, str(self.file_mode.byte_count), tail])
        pos = position or mode_line.format(view.cursor_index + 1, len(view.lines))
        label = PROGRAM_LABEL if not self.directory_mode.mode_stack else ''
        return self.fit_mode_line([x for x in [self.directory_mode.current_directory, pos,
                                               str(self.directory_mode.byte_count), '(ls)', label] if x])

    def mode_line_target(self):
        """FGREP/EGREPのモードラインの最後の欄(探している物の名前)と大きさ。"""
        if self.current_mode == FILE:
            return self.file_mode.current_file, self.file_mode.byte_count
        if self.current_mode == SHELL:
            return 'SHELL', self.shell.byte_count
        return self.directory_mode.current_directory, self.directory_mode.byte_count

    def fit_mode_line_tail(self, fields):
        """最後の欄(パス)の途中を「...」で縮める版(FGREP/EGREPのモードライン)。"""
        rest = len('   '.join(fields[:-1])) + 3
        room = self.display.columns - 1 - rest
        name = fields[-1]
        if utf8.text_width(name) > room and room > 8:
            keep = room - 3
            head = keep // 2
            name = name[:head] + '...' + name[len(name) - (keep - head):]
        return '   '.join(fields[:-1] + [name])

    def fit_mode_line(self, fields):
        """欄を空白3つでつなぐ。幅に収まらなければ、最初の欄(名前)の途中を「...」で縮める(C++版と同じ)。"""
        rest = len('   '.join(fields[1:])) + 3
        room = self.display.columns - 1 - rest
        name = fields[0]
        if utf8.text_width(name) > room and room > 8:
            keep = room - 3
            head = keep // 2
            name = name[:head] + '...' + name[len(name) - (keep - head):]
        return '   '.join([name] + fields[1:])

    def redraw(self):
        view = self.active_view()
        visible = view.visible_lines()
        for row in range(view.height):
            if row < len(visible):
                index = view.top_index + row
                reverse = view.cursor_highlight and index == view.cursor_index
                if (self.grep_active or self.grep_prompt is not None) and index in self.grep.matches:
                    self.display.draw_row_with_spans(row, visible[row].text,
                                                     match_spans(visible[row].text, self.grep.pattern,
                                                                 self.grep.syntax), reverse=reverse)
                else:
                    self.display.draw_row(row, visible[row].text, reverse=reverse)
            else:
                self.display.draw_row(row, '')
        self.display.draw_mode_line(self.mode_line_text())
        if (self.current_mode == SHELL and self.prompt is None and self.goto_digits is None
                and self.grep_prompt is None and not self.grep_active):
            self.display.show_cursor_at(*self.shell_cursor_position())
        else:
            self.display.hide_cursor()
        self.display.flush()

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
            self.file_mode.save_cursor_position()
        self.directory_mode.save_current_cursor_position()

    def show_transient_notice(self, text):
        self.notice = localized(text)

    # --- 起動 ---
    def start_directory_listing(self, path):
        if not self.directory_mode.enter_directory(path):
            return False
        self.current_mode = DIRECTORY
        return True

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
        return self.file_mode.load_file(path, self.wrap_width(), self.file_view.height)

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
            if not self.directory_mode.enter_directory(target):
                self.show_transient_notice('Cannot list: ' + target)
                return False
            return True
        if not os.access(target, os.R_OK):
            self.show_transient_notice('Permission denied')
            return False
        if not self.file_mode.load_file(target, self.wrap_width(), self.file_view.height):
            self.show_transient_notice('Cannot read: ' + target)
            return False
        self.file_mode.load_all()  # (ls)から開いたファイルは全部読む(C++と同じく「001/500」)
        self.file_mode.restore_from_history()
        self.current_mode = FILE
        return True

    # --- キー ---
    def resolve_command(self, byte, special):
        """Application::resolveCommand(): 1バイト(またはspecial)で決まるコマンド。"""
        if special in ('Up', 'Left'):
            return Command.MOVE_UP
        if special in ('Down', 'Right'):
            return Command.MOVE_DOWN
        if special == 'PageUp':
            return Command.PAGE_UP
        if special == 'PageDown':
            return Command.PAGE_DOWN
        table = {
            ord('j'): Command.SCROLL_LINE_DOWN, ord('k'): Command.SCROLL_LINE_UP,
            0x0E: Command.MOVE_DOWN, 0x10: Command.MOVE_UP,
            ord(' '): Command.PAGE_DOWN, 0x06: Command.PAGE_DOWN, 0x16: Command.PAGE_DOWN,
            ord('b'): Command.PAGE_UP, 0x02: Command.PAGE_UP,
            CR: Command.ENTER, CTRL_G: Command.BACK, 0x0C: Command.REDRAW, 0x1D: Command.CURRENT_DIRECTORY,
        }
        if self.pager_launch:
            table.update({ord('n'): Command.NEXT_FILE, ord('p'): Command.PREVIOUS_FILE, ord('x'): Command.FIRST_FILE,
                          ord('f'): Command.LAST_FILE, ord('q'): Command.QUIT_PAGER})
        return table.get(byte)

    def pending_prefix_label(self):
        """Application::pendingPrefixLabel()(1秒経ったらモードラインへ)。"""
        if self.esc:
            return 'ESC-'
        if self.leader_x:
            return 'C-x-'
        if self.leader_j:
            return 'C-j-'
        return ''

    def feed_key(self, byte, special):
        """1キー(Application::feedByte()→dispatchByte())。"""
        self.notice = None
        if self.prompt is not None:
            return self.feed_prompt_byte(byte)
        if self.grep_prompt is not None:
            return self.feed_grep_prompt_byte(byte)
        if self.grep_active and special is None:
            if byte in (0x0E, 0x10):  # ^N/^P: 次/前の一致
                self.grep.move(1 if byte == 0x0E else -1)
                self.show_current_match()
                return True
            if byte == CR:   # そこに留まる
                self.grep_active = False
                return True
            if byte == CTRL_G:  # 一致の間を移るのをやめる(今いる一致のまま、C++版と同じ)
                self.grep_active = False
                return True
            self.grep_active = False  # 他のキーは、一致の間を移るのをやめてから
        if special is None and byte == 0x13 and not (self.esc or self.leader_j or self.leader_x):
            return self.start_grep_prompt(FIXED)
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
        if self.esc:
            self.esc = False
            if special is not None:
                return False
            if ord('0') <= byte <= ord('9'):
                self.goto_digits = chr(byte)
                return True
            if byte == ord('<'):
                return self.handle_command(Command.GO_TO_TOP)
            if byte == ord('>'):
                return self.handle_command(Command.GO_TO_BOTTOM)
            if byte == ord('v'):
                return self.handle_command(Command.PAGE_UP)
            if byte == ESC:
                return self.handle_command(Command.EXIT)
            if byte == ord('s'):
                return self.start_shell()
            return False
        if self.leader_j:
            self.leader_j = False
            if byte == 0x13:  # ^J^S
                return self.start_grep_prompt(REGEX)
            if byte == 0x02:  # ^J^B
                return self.handle_command(Command.PARENT_DIRECTORY)
            if byte == 0x03:  # ^J^C
                return self.handle_command(Command.CURRENT_DIRECTORY)
            return False
        if self.leader_x:
            self.leader_x = False
            if byte == 0x03:  # ^X^C
                return self.handle_command(Command.EXIT)
            if byte == CTRL_X:  # ^X^X
                return self.handle_command(Command.EXCHANGE_POINT)
            return False
        if self.current_mode == SHELL and special is None and byte not in (ESC, CTRL_J, CTRL_X):
            return self.feed_shell_byte(byte)
        if special is None and byte == ESC:
            self.esc = True
            return True
        if special is None and byte == CTRL_J:
            self.leader_j = True
            return True
        if special is None and byte == CTRL_X:
            self.leader_x = True
            return True
        command = self.resolve_command(byte, special)
        if command is None:
            return False
        return self.handle_command(command)

    def start_grep_prompt(self, syntax):
        """^S(FGREP、打つたびに探す)・^J^S(EGREP)のプロンプト(Application::startGrepPrompt())。"""
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.load_all()  # ページャーは残りを全部読んでから(docs/CHANGES.md項目1009)
        self.grep_active = False
        self.grep.matches, self.grep.current, self.grep.pattern = [], -1, ''
        self.grep.origin = self.active_view().cursor_index
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
        if byte == 0x13 and syntax == FIXED:  # 打っている間の^S: 次の一致へ
            self.grep.move(1)
            self.show_current_match()
            return True
        if byte in (0x08, 0x7F):
            editor.backspace()
        elif byte >= 0x80:
            self.utf8_pending.append(byte)
            if len(self.utf8_pending) < utf8.sequence_length(self.utf8_pending[0]):
                return True
            editor.insert(bytes(self.utf8_pending).decode('utf-8', 'replace'))
            self.utf8_pending.clear()
        elif byte >= 0x20:
            editor.insert(chr(byte))
        else:
            return True
        if syntax == FIXED:
            self.run_grep(FIXED, editor.text)
        return True

    def start_shell(self):
        """^[s: SHELLへ(初めてならbashを起動、Command::StartShell)。"""
        if self.current_mode == SHELL:
            return True
        if not self.shell.alive():
            directory = self.directory_mode.current_directory or os.getcwd()
            if not self.shell.start(directory, self.display.rows - 1, self.display.columns):
                self.shell.terminate()
                self.show_transient_notice('Cannot start SHELL')
                return False
        self.shell_return_mode = self.current_mode
        self.current_mode = SHELL
        self.refresh_shell_view()
        return True

    def feed_shell_byte(self, byte):
        """Application::feedShellByte(): コマンド行の編集、^Mで送る、^Cは中断、^Gで戻る。"""
        editor = self.command_line
        if byte == CTRL_G:
            self.current_mode = self.shell_return_mode
            return True
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
        elif byte == 0x01:
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
        elif byte == 0x0C:
            pass  # 再表示
        elif byte >= 0x80:  # UTF-8の字はバイトがそろってから(sequence_length())
            self.utf8_pending.append(byte)
            if len(self.utf8_pending) >= utf8.sequence_length(self.utf8_pending[0]):
                editor.insert(bytes(self.utf8_pending).decode('utf-8', 'replace'))
                self.utf8_pending.clear()
        elif byte >= 0x20:
            editor.insert(chr(byte))
        self.refresh_shell_view()
        return True

    def feed_prompt_byte(self, byte):
        """終了の確認の入力欄(LineEditorの今回使う分)。"""
        if byte == CR:
            answer, self.prompt = self.prompt, None
            if answer.strip().lower() == 'y':
                self.wants_exit = True
            return True
        if byte == CTRL_G:
            self.prompt = None
            return True
        if byte in (0x08, 0x7F):
            self.prompt = self.prompt[:-1]
            return True
        if 0x20 <= byte < 0x7F:
            self.prompt += chr(byte)
        return True

    def handle_command(self, command):
        """Application::handleCommand()(今回の分)。"""
        view = self.active_view()
        if command == Command.EXIT:
            self.prompt = 'y'  # C++版と同じく「y」を入れた状態で尋ねる
            return True
        if command == Command.QUIT_PAGER:
            self.wants_exit = True
            return True
        if command in (Command.NEXT_FILE, Command.PREVIOUS_FILE, Command.FIRST_FILE, Command.LAST_FILE):
            last = len(self.pager_files) - 1
            index = {Command.NEXT_FILE: min(self.pager_index + 1, last), Command.PREVIOUS_FILE: max(self.pager_index - 1, 0),
                     Command.FIRST_FILE: 0, Command.LAST_FILE: last}[command]
            if index != self.pager_index and self.pager_files[0] != '(stdin)':
                self.open_pager_file(index)
            return True
        if command in (Command.PARENT_DIRECTORY, Command.CURRENT_DIRECTORY) and self.current_mode == FILE:
            self.file_mode.save_cursor_position()
        if command == Command.PARENT_DIRECTORY:
            if self.pager_launch:
                self.show_transient_notice('Not available in pager')
                return False
            base = self.directory_mode.current_directory
            self.current_mode = DIRECTORY
            self.directory_mode.mode_stack.clear()
            return self.directory_mode.enter_directory(self.directory_mode.parent_directory(base))
        if command == Command.CURRENT_DIRECTORY:
            if self.pager_launch:
                self.show_transient_notice('Not available in pager')
                return False
            self.current_mode = DIRECTORY
            return self.directory_mode.enter_directory(self.directory_mode.current_directory)
        if command == Command.BACK:
            if self.current_mode == FILE and not self.pager_launch:
                self.file_mode.save_cursor_position()
                self.current_mode = DIRECTORY
                return True
            if self.current_mode == DIRECTORY and self.directory_mode.mode_stack:
                directory, cursor = self.directory_mode.mode_stack.pop()
                self.directory_mode.enter_directory(directory)
                self.directory_view.cursor_index = cursor
                self.directory_view.ensure_cursor_visible()
            return True
        if command == Command.ENTER:
            if self.current_mode == DIRECTORY:
                return self.open_selected_entry()
            return False
        if command == Command.REDRAW:
            return True
        if command == Command.EXCHANGE_POINT:
            if self.mark_before_jump is not None:
                here = view.cursor_index
                view.cursor_index = self.mark_before_jump
                self.mark_before_jump = here
                view.clamp()
                view.top_index = view.cursor_index - view.height // 2  # ^[Nと同じく中ほどへ
                view.clamp()
            return True
        # 移動。ページャーは先に要る所まで読む(ensurePagerStreamLoaded())。
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            if command == Command.GO_TO_BOTTOM:
                self.file_mode.load_all()
            else:
                self.file_mode.ensure_loaded_through(max(view.top_index, view.cursor_index) + 2 * view.height)
        if command in (Command.GO_TO_TOP, Command.GO_TO_BOTTOM):
            self.mark_before_jump = view.cursor_index
        {
            Command.MOVE_DOWN: view.move_down, Command.MOVE_UP: view.move_up,
            Command.SCROLL_LINE_DOWN: view.scroll_line_down, Command.SCROLL_LINE_UP: view.scroll_line_up,
            Command.PAGE_DOWN: view.page_down, Command.PAGE_UP: view.page_up,
            Command.GO_TO_TOP: view.go_to_top, Command.GO_TO_BOTTOM: view.go_to_bottom,
        }[command]()
        return True

    # --- 実行ループ(main.cppのrunTerminalPhase()) ---
    def run(self, resized):
        self.layout()
        self.redraw()
        while not self.wants_exit:
            fds = [self.key_input.fd] + ([self.shell.fd] if self.shell.alive() else [])
            try:
                readable, _, _ = select.select(fds, [], [], 0 if self.key_input.pending else 0.1)
            except InterruptedError:
                readable = []
            if resized():
                self.layout()
                self.redraw()
            if self.shell.alive() and self.shell.fd in readable:
                if not self.shell.on_pty_readable() and self.current_mode == SHELL:
                    self.current_mode = self.shell_return_mode  # bashが終わった
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
        self.shell.terminate()

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

