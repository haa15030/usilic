"""コマンド行の1行の編集(C++のline_editor.cppのLineEditor、今回使う分)。"""


class LineEditor:
    def __init__(self):
        self.text = ''
        self.cursor = 0
        self.kill_buffer = ''
        self.select_all_pending = False  # 入れておいた字を全部選んだ状態(C++のselectAllPending_)

    def set_text(self, text):
        self.text, self.cursor = text, len(text)
        self.select_all_pending = False

    def select_all(self):
        """LineEditor::selectAll(): 最初に打った字・BS・^Dで全部を消す。カーソルを動かすとやめる。"""
        self.select_all_pending = True

    def _take_selection(self):
        if not self.select_all_pending:
            return False
        self.select_all_pending = False
        self.text, self.cursor = '', 0
        return True

    def insert(self, s):
        self._take_selection()
        self.text = self.text[:self.cursor] + s + self.text[self.cursor:]
        self.cursor += len(s)

    def move_home(self):        # ^A
        self.select_all_pending = False
        self.cursor = 0

    def move_end(self):         # ^E
        self.select_all_pending = False
        self.cursor = len(self.text)

    def move_left(self):        # ^B
        self.select_all_pending = False
        self.cursor = max(0, self.cursor - 1)

    def move_right(self):       # ^F
        self.select_all_pending = False
        self.cursor = min(len(self.text), self.cursor + 1)

    def backspace(self):        # ^H
        if self._take_selection():
            return
        if self.cursor > 0:
            self.text = self.text[:self.cursor - 1] + self.text[self.cursor:]
            self.cursor -= 1

    def delete_forward(self):   # ^D
        if self._take_selection():
            return
        self.text = self.text[:self.cursor] + self.text[self.cursor + 1:]

    def kill_to_end(self):      # ^K
        self.kill_buffer = self.text[self.cursor:]
        self.text = self.text[:self.cursor]

    def kill_to_start(self):    # ^U
        self.kill_buffer = self.text[:self.cursor]
        self.text = self.text[self.cursor:]
        self.cursor = 0

    def yank(self):             # ^Y
        self.insert(self.kill_buffer)
