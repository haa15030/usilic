"""コマンド行の1行の編集(C++のline_editor.cppのLineEditor、今回使う分)。"""


class LineEditor:
    def __init__(self):
        self.text = ''
        self.cursor = 0
        self.kill_buffer = ''

    def set_text(self, text):
        self.text, self.cursor = text, len(text)

    def insert(self, s):
        self.text = self.text[:self.cursor] + s + self.text[self.cursor:]
        self.cursor += len(s)

    def move_home(self):        # ^A
        self.cursor = 0

    def move_end(self):         # ^E
        self.cursor = len(self.text)

    def move_left(self):        # ^B
        self.cursor = max(0, self.cursor - 1)

    def move_right(self):       # ^F
        self.cursor = min(len(self.text), self.cursor + 1)

    def backspace(self):        # ^H
        if self.cursor > 0:
            self.text = self.text[:self.cursor - 1] + self.text[self.cursor:]
            self.cursor -= 1

    def delete_forward(self):   # ^D
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
