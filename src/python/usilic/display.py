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
        self.strip = 0  # 右端に空けておく桁(モードラインを隠している間のつまみ、C++のkScrollbarStripWidth)
        self.rows, self.columns = self.screen.getmaxyx()
        self.match_attr = curses.A_REVERSE  # 一致の色(C++版のマゼンタ、色が使えなければ反転)
        try:
            curses.start_color()
            curses.use_default_colors()
            curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_MAGENTA)
            self.match_attr = curses.color_pair(1)
        except curses.error:
            pass

    @property
    def columns(self):
        """使える桁(C++のTerminalDisplay::columns()、つまみの分を除く)。"""
        return max(0, self.full_columns - self.strip)

    @columns.setter
    def columns(self, value):
        self.full_columns = value

    def refresh_size(self):
        curses.update_lines_cols()
        self.rows, self.columns = self.screen.getmaxyx()
        return self.rows, self.columns

    def draw_scroll_thumb(self, top, height):
        """右端の2桁に反転した空白のつまみ(C++のContentView::renderScrollIndicatorOverlay())。"""
        for row in range(top, top + height):
            try:
                self.screen.addstr(row, self.full_columns - 2, '  ', curses.A_REVERSE)
            except curses.error:
                pass

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

    def draw_row(self, row, text, reverse=False, underline=False, to_edge=False):
        """1行を描く。反転・下線は字の所まで(C++のContentView::renderLine()、highlightToRowEnd_が既定でfalse)、
        to_edgeなら右端まで(モードライン)。"""
        attr = (curses.A_REVERSE if reverse else 0) | (curses.A_UNDERLINE if underline else 0)
        width = self.columns - (1 if row == self.rows - 1 else 0)  # 右下の桁に書くとcursesがエラーにする
        fitted = self._fit(text, width)
        body = fitted if to_edge or not attr else fitted.rstrip(' ')
        try:
            # 先に行末まで消す(C++のclearToEndOfLine())。字の幅を端末と違って数えた(▽等)時に前の行の字が残らないように
            self.screen.move(row, 0)
            self.screen.clrtoeol()
            self.screen.addstr(row, 0, body, attr)
            if len(body) < len(fitted):
                self.screen.addstr(row, utf8.text_width(body), fitted[len(body):], 0)
        except curses.error:
            pass

    def draw_row_styled(self, row, text, runs, reverse=False, underline=False):
        """属性付きで描く。runsは(開始, 終わり, 属性)の並び(字の位置)、属性は'bold'・'underline'・'match'。"""
        self.draw_row(row, text, reverse=reverse, underline=underline)
        if not runs:
            return
        base = (curses.A_REVERSE if reverse else 0) | (curses.A_UNDERLINE if underline else 0)
        names = {'bold': curses.A_BOLD, 'underline': curses.A_UNDERLINE, 'match': self.match_attr,
                 'reverse': curses.A_REVERSE, 'italic': getattr(curses, 'A_ITALIC', curses.A_UNDERLINE)}
        column, width = 0, self.columns - (1 if row == self.rows - 1 else 0)  # 右端まで(右下の桁だけはcursesが書けない)
        for index, ch in enumerate(text):
            w = utf8.display_width_of_codepoint(ord(ch))
            if column + w > width:
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

    def draw_mode_line(self, segments):
        """モードライン: [(字, 属性)](C++のContentView::renderModeLine()、属性は'standout'・'plain'・'bold'・
        'underline')、字だけなら全部反転。右下の桁にも書く(cursesのエラーは無視する)。"""
        if isinstance(segments, str):
            self.draw_row(self.rows - 1, segments, reverse=True, to_edge=True)
            return
        attrs = {'standout': curses.A_REVERSE, 'plain': 0, 'bold': curses.A_BOLD, 'underline': curses.A_UNDERLINE}
        row, column = self.rows - 1, 0
        try:
            self.screen.move(row, 0)
            self.screen.clrtoeol()
        except curses.error:
            pass
        for text, attr in segments:
            for ch in text:
                w = utf8.display_width_of_codepoint(ord(ch))
                if column + w > self.columns:
                    return
                try:
                    self.screen.addstr(row, column, ch, attrs.get(attr, curses.A_REVERSE))
                except curses.error:
                    pass
                column += w

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
