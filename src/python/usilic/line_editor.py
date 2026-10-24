"""コマンド行の1行の編集(C++のline_editor.cppのLineEditor、今回使う分)。"""

MAX_UNDO = 100
UNDO_INSERT_GROUP = 20  # Emacsの「文字の挿入は20個ずつ」


def is_word_char(c):
    """moveWordLeft()等の語の字(ASCIIの英数字と、ASCII以外の字)。"""
    return c >= '\x80' or c.isascii() and c.isalnum()


class LineEditor:
    def __init__(self, kill=None):
        self.text = ''
        self.cursor = 0
        self.kill = kill if kill is not None else {'text': ''}  # ^K・^Uの削除バッファ(プロンプトどうしで共有できる)
        self.select_all_pending = False  # 入れておいた字を全部選んだ状態(C++のselectAllPending_)
        self.undo_stack = []
        self.undo_insert_run = 0  # 直前の記録から続いている文字入力の数(0=まとまり無し)

    @property
    def kill_buffer(self):
        return self.kill['text']

    @kill_buffer.setter
    def kill_buffer(self, text):
        self.kill['text'] = text

    def set_text(self, text):
        # 空の行は新しい入力の始まりなので取り消しの履歴を捨てる。それ以外の置き換えは1回分の編集。
        if not text:
            self.clear_undo()
        elif text != self.text:
            self.push_undo()
        self.text, self.cursor = text, len(text)
        self.select_all_pending = False

    def push_undo(self):
        self.undo_insert_run = 0
        if len(self.undo_stack) >= MAX_UNDO:
            del self.undo_stack[0]
        self.undo_stack.append((self.text, self.cursor))

    def clear_undo(self):
        self.undo_stack = []
        self.undo_insert_run = 0

    def undo(self):             # ^_
        self.select_all_pending = False
        self.undo_insert_run = 0
        if self.undo_stack:
            self.text, self.cursor = self.undo_stack.pop()

    def select_all(self):
        """LineEditor::selectAll(): 最初に打った字・BS・^Dで全部を消す。カーソルを動かすとやめる。"""
        self.select_all_pending = True

    def _take_selection(self):
        if not self.select_all_pending:
            return False
        self.select_all_pending = False
        self.text, self.cursor = '', 0
        return True

    def insert(self, s, self_insert=False):
        if not s:
            return
        if not self_insert or self.select_all_pending or self.undo_insert_run == 0 \
                or self.undo_insert_run >= UNDO_INSERT_GROUP:
            self.push_undo()
        if self_insert:
            self.undo_insert_run += 1
        self._take_selection()
        self.text = self.text[:self.cursor] + s + self.text[self.cursor:]
        self.cursor += len(s)

    def move_home(self):        # ^A
        self.select_all_pending = False
        self.undo_insert_run = 0
        self.cursor = 0

    def move_end(self):         # ^E
        self.select_all_pending = False
        self.undo_insert_run = 0
        self.cursor = len(self.text)

    def move_left(self):        # ^B
        self.select_all_pending = False
        self.undo_insert_run = 0
        self.cursor = max(0, self.cursor - 1)

    def move_right(self):       # ^F
        self.select_all_pending = False
        self.undo_insert_run = 0
        self.cursor = min(len(self.text), self.cursor + 1)

    def backspace(self):        # ^H
        if self.select_all_pending:
            self.push_undo()
        if self._take_selection():
            return
        if self.cursor > 0:
            self.push_undo()
            self.text = self.text[:self.cursor - 1] + self.text[self.cursor:]
            self.cursor -= 1

    def delete_forward(self):   # ^D
        if self.select_all_pending:
            self.push_undo()
        if self._take_selection():
            return
        if self.cursor < len(self.text):
            self.push_undo()
        self.text = self.text[:self.cursor] + self.text[self.cursor + 1:]

    def kill_to_end(self):      # ^K
        self.select_all_pending = False
        if self.cursor < len(self.text):
            self.push_undo()
        self.kill_buffer = self.text[self.cursor:]
        self.text = self.text[:self.cursor]

    def kill_to_start(self):    # ^U
        self.select_all_pending = False
        if self.cursor > 0:
            self.push_undo()
        self.kill_buffer = self.text[:self.cursor]
        self.text = self.text[self.cursor:]
        self.cursor = 0

    def yank(self):             # ^Y
        self.insert(self.kill_buffer)

    def word_left_position(self):
        pos = self.cursor
        while pos > 0 and not is_word_char(self.text[pos - 1]):
            pos -= 1
        while pos > 0 and is_word_char(self.text[pos - 1]):
            pos -= 1
        return pos

    def word_right_position(self):
        pos, n = self.cursor, len(self.text)
        while pos < n and not is_word_char(self.text[pos]):
            pos += 1
        while pos < n and is_word_char(self.text[pos]):
            pos += 1
        return pos

    def move_word_left(self):   # ESC b
        self.select_all_pending = False
        self.undo_insert_run = 0
        self.cursor = self.word_left_position()

    def move_word_right(self):  # ESC f
        self.select_all_pending = False
        self.undo_insert_run = 0
        self.cursor = self.word_right_position()

    def kill_word_backward(self):  # ESC DEL
        self.select_all_pending = False
        start = self.word_left_position()
        if start == self.cursor:
            return
        self.push_undo()
        self.kill_buffer = self.text[start:self.cursor]
        self.text = self.text[:start] + self.text[self.cursor:]
        self.cursor = start

    def kill_word_forward(self):   # ESC d
        self.select_all_pending = False
        end = self.word_right_position()
        if end == self.cursor:
            return
        self.push_undo()
        self.kill_buffer = self.text[self.cursor:end]
        self.text = self.text[:self.cursor] + self.text[end:]
