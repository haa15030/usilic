"""ファイル・標準入力の閲覧(C++のfile_mode.cppのFileMode)。

C++版と同じく、ページャーは画面に要る分だけ少しずつ読む(ensure_loaded_through)。行は表示幅で折り返して
ContentViewへ渡す(C++ではPipeBufferの役目)。
"""
import os

from .content_view import Line, wrap_text, expand_tabs


def visible_controls(text):
    """制御文字(タブを除く0x00〜0x1Fと0x7F)を「^X」に(C++のFILEの既定の表示)。"""
    out = []
    for ch in text:
        c = ord(ch)
        if c < 0x20 and ch != '\t':
            out.append('^' + chr(c + 0x40))
        elif c == 0x7F:
            out.append('^?')
        else:
            out.append(ch)
    return ''.join(out)


class FileMode:
    def __init__(self, view):
        self.view = view
        self.current_file = ''
        self.stream = None       # 読み込み中のファイル(読み終えたらNone)
        self.raw_count = 0       # 読んだ元の行の数
        self.byte_count = 0
        self.width = 80
        self._partial = b''
        self.raw_texts = []      # 元の行(折り返す前、幅が変わった時の組み直し用)

    def has_active_stream(self):
        return self.stream is not None

    def load_file(self, path, width, initial_lines):
        """pathを開き、最初の画面の分(initial_lines行)だけ読む(FileMode::loadFile())。"""
        try:
            stream = open(path, 'rb')
        except OSError:
            return False
        return self.load_stream(stream, path, width, initial_lines)

    def load_stream(self, stream, name, width, initial_lines):
        self.stream, self.current_file, self.width = stream, name, width
        self.raw_count = self.byte_count = 0
        self._partial = b''
        self.raw_texts = []
        self.view.cursor_index = self.view.top_index = 0
        self.view.set_lines([])
        self.ensure_loaded_through(initial_lines - 1)
        return True

    def _append_raw_line(self, data):
        self.raw_count += 1
        text = expand_tabs(visible_controls(data.decode('utf-8', 'replace')))
        self.raw_texts.append(text)
        self._append_wrapped(text, self.raw_count)

    def _append_wrapped(self, text, number):
        pieces = wrap_text(text, self.width)
        for i, piece in enumerate(pieces):
            self.view.lines.append(Line(piece, number, ends_with_newline=(i == len(pieces) - 1)))

    def rewrap(self, width):
        """幅が変わった時に、読んだ所までを組み直す(カーソルは同じ元の行へ)。"""
        if width == self.width:
            return
        lines = self.view.lines
        raw = lines[self.view.cursor_index].raw_line_number if lines else None
        self.width = width
        self.view.lines = []
        for number, text in enumerate(self.raw_texts, 1):
            self._append_wrapped(text, number)
        if raw is not None:
            for i, line in enumerate(self.view.lines):
                if line.raw_line_number == raw:
                    self.view.cursor_index = i
                    break
        self.view.clamp()
        self.view.ensure_cursor_visible()

    def ensure_loaded_through(self, target_line_index):
        """画面の行target_line_indexまで(あと1行先まで)読む。新しい行を読んだらTrue。"""
        if self.stream is None:
            return False
        before = len(self.view.lines)
        while len(self.view.lines) <= target_line_index + 1:
            chunk = self.stream.readline()
            if not chunk:
                if self._partial:
                    self._append_raw_line(self._partial)
                    self._partial = b''
                if self.stream is not getattr(os, 'stdin', None):
                    self.stream.close()
                self.stream = None
                break
            self.byte_count += len(chunk)
            if not chunk.endswith(b'\n'):
                self._partial += chunk
                continue
            data = self._partial + chunk[:-1]
            self._partial = b''
            if data.endswith(b'\r'):
                data = data[:-1]
            self._append_raw_line(data)
        self.view.clamp()
        return len(self.view.lines) > before

    def load_all(self):
        while self.ensure_loaded_through(len(self.view.lines) + 10000):
            pass
