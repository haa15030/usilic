"""端末の画面(C++のterminal_display.cppのTerminalDisplay)をcursesで。

桁の計算は自前(utf8.text_width)で行い、cursesには1行ずつ描く。最下行がモードライン。
"""
import curses

from . import utf8


class TerminalDisplay:
    def __init__(self, screen):
        self.screen = screen
        curses.raw()
        curses.noecho()
        try:
            curses.curs_set(0)
        except curses.error:
            pass
        self.screen.keypad(False)
        self.rows, self.columns = self.screen.getmaxyx()
        self.match_attr = curses.A_REVERSE  # 一致の色(C++版のマゼンタ、色が使えなければ反転)
        try:
            curses.start_color()
            curses.use_default_colors()
            curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_MAGENTA)
            self.match_attr = curses.color_pair(1)
        except curses.error:
            pass

    def refresh_size(self):
        curses.update_lines_cols()
        self.rows, self.columns = self.screen.getmaxyx()
        return self.rows, self.columns

    def _fit(self, text, width):
        """表示幅widthに切り詰め、足りなければ空白で埋める。"""
        out, used = [], 0
        for ch in text:
            w = utf8.display_width_of_codepoint(ord(ch))
            if used + w > width:
                break
            out.append(ch)
            used += w
        return ''.join(out) + ' ' * (width - used)

    def draw_row(self, row, text, reverse=False, underline=False):
        attr = (curses.A_REVERSE if reverse else 0) | (curses.A_UNDERLINE if underline else 0)
        width = self.columns - (1 if row == self.rows - 1 else 0)  # 右下の桁に書くとcursesがエラーにする
        try:
            self.screen.addstr(row, 0, self._fit(text, width), attr)
        except curses.error:
            pass

    def draw_row_with_spans(self, row, text, spans, reverse=False):
        """一致した所(spans、字の位置)だけ色を変えて描く。"""
        self.draw_row(row, text, reverse=reverse)
        column = 0
        for index, ch in enumerate(text):
            w = utf8.display_width_of_codepoint(ord(ch))
            if column + w > self.columns - 1:
                break
            if any(start <= index < end for start, end in spans):
                try:
                    self.screen.addstr(row, column, ch, self.match_attr)
                except curses.error:
                    pass
            column += w

    def draw_mode_line(self, text):
        self.draw_row(self.rows - 1, text, reverse=True)

    def show_cursor_at(self, row, column):
        try:
            curses.curs_set(1)
            self.screen.move(row, min(column, self.columns - 1))
        except curses.error:
            pass

    def hide_cursor(self):
        try:
            curses.curs_set(0)
        except curses.error:
            pass

    def flush(self):
        self.screen.refresh()
