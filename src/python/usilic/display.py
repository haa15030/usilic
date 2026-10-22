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

    def draw_row_styled(self, row, text, runs, reverse=False, underline=False):
        """属性付きで描く。runsは(開始, 終わり, 属性)の並び(字の位置)、属性は'bold'・'underline'・'match'。"""
        self.draw_row(row, text, reverse=reverse, underline=underline)
        if not runs:
            return
        base = (curses.A_REVERSE if reverse else 0) | (curses.A_UNDERLINE if underline else 0)
        names = {'bold': curses.A_BOLD, 'underline': curses.A_UNDERLINE, 'match': self.match_attr,
                 'reverse': curses.A_REVERSE}
        column = 0
        for index, ch in enumerate(text):
            w = utf8.display_width_of_codepoint(ord(ch))
            if column + w > self.columns - 1:
                break
            attr = 0
            for start, end, name in runs:
                if start <= index < end:
                    attr |= names.get(name, 0)
            if attr:
                try:
                    self.screen.addstr(row, column, ch, base | attr)
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

    def set_mouse_tracking(self, enabled):
        """マウスを追う(全部の動き・SGRの座標・括弧付き貼り付け)/やめる(C++のsetTerminalMouseTracking())。"""
        import os
        from .mouse import TRACKING_OFF, TRACKING_ON
        self.mouse_on = enabled
        try:
            os.write(1, (TRACKING_ON if enabled else TRACKING_OFF).encode('ascii'))
        except OSError:
            pass

    def suspend(self):
        """外のプログラム(LAUNCH)の間、端末を元に戻す(C++のleaveRawMode())。"""
        self.set_mouse_tracking(False)
        curses.def_prog_mode()
        curses.endwin()

    def resume(self):
        curses.reset_prog_mode()
        self.screen.clear()
        self.refresh_size()
