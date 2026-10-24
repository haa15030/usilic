"""COPYモード(C++のcopy_mode.cppのCopyMode、横書きの画面の分)。

画面の行の上をカーソル(行, 字)で動き、^@(^␣)で範囲の始まりと終わりを、^X␣で矩形の範囲を切り換える。
範囲はいくつでも作れて、^[wでまとめてクリップボードへ(普通の範囲は改行で、^[tでTOOLへ入れる時は空白で
つなぐ。矩形は行ごとに改行)。
"""
from . import utf8


def column_of(text, index):
    """字の位置→表示の桁。"""
    return utf8.text_width(text[:index])


def index_at_column(text, column):
    """表示の桁→字の位置(桁の途中なら、その字)。"""
    used = 0
    for i, ch in enumerate(text):
        w = utf8.display_width_of_codepoint(ord(ch))
        if used + w > column:
            return i
        used += w
    return len(text)


class Region:
    def __init__(self, start, end, rectangle):
        self.start, self.end, self.rectangle = start, end, rectangle  # (行, 字)

    def ordered(self):
        return (self.start, self.end) if self.start <= self.end else (self.end, self.start)


class CopyMode:
    def __init__(self, view):
        self.view = view
        self.row = view.cursor_index
        self.col = 0
        self.regions = []
        self.anchor = None       # 範囲を作っている途中の始まり
        self.rectangle = False

    def lines(self):
        return [l.text for l in self.view.lines]

    def text_at(self, row):
        lines = self.view.lines
        return lines[row].text if 0 <= row < len(lines) else ''

    # --- 動く ---
    def move_row(self, step):
        before = column_of(self.text_at(self.row), self.col)
        self.row = max(0, min(len(self.view.lines) - 1, self.row + step))
        self.col = index_at_column(self.text_at(self.row), before)
        self._follow()

    def move_col(self, step):
        text = self.text_at(self.row)
        col = self.col + step
        if col < 0 and self.row > 0:
            self.row -= 1
            col = len(self.text_at(self.row))
        elif col > len(text) and self.row < len(self.view.lines) - 1:
            self.row += 1
            col = 0
        self.col = max(0, min(len(self.text_at(self.row)), col))
        self._follow()

    def line_start(self):
        self.col = 0

    def line_end(self):
        self.col = len(self.text_at(self.row))

    def go_to_row(self, row):
        self.row = max(0, min(len(self.view.lines) - 1, row))
        self.col = min(self.col, len(self.text_at(self.row)))
        self._follow()

    def _follow(self):
        self.view.cursor_index = self.row
        self.view.ensure_cursor_visible()

    # --- マウス(C++のplaceCursorAt()・beginOrExtendFromMouse()) ---
    def place_cursor_at(self, row, column):
        """画面の行(本文の行の番号)・桁へカーソルを(ドラッグ)。"""
        if not self.view.lines:
            return
        self.row = max(0, min(len(self.view.lines) - 1, row))
        self.col = index_at_column(self.text_at(self.row), column)
        self._follow()

    def begin_or_extend_from_mouse(self, row, column):
        """押した所: 広がりのある範囲の途中なら、それを1つの範囲として残してここから新しく始める。まだ広がりの無い
        範囲(^@の直後等)なら、その始まりのままカーソルだけ動かす。"""
        if not self.view.lines:
            return
        was_active = self.anchor is not None
        had_extent = was_active and self.anchor != (self.row, self.col)
        if had_extent:
            self.regions.append(Region(self.anchor, (self.row, self.col), self.rectangle))
            self.anchor = None
        self.place_cursor_at(row, column)
        if not was_active or had_extent:
            self.anchor, self.rectangle = (self.row, self.col), False

    # --- 範囲 ---
    def toggle(self, rectangle):
        """^@(rectangle=False)・^X␣(True): 始まりを置く/今の位置で閉じる。"""
        here = (self.row, self.col)
        if self.anchor is None:
            self.anchor, self.rectangle = here, rectangle
            return 'start'
        self.regions.append(Region(self.anchor, here, self.rectangle))
        self.anchor = None
        return 'end'

    def all_regions(self):
        regions = list(self.regions)
        if self.anchor is not None:
            regions.append(Region(self.anchor, (self.row, self.col), self.rectangle))
        return regions

    def runs_for_row(self, row):
        """描く時の反転(字の位置)。カーソルの字は反転しない(端末のカーソルで、C++と同じ)。"""
        text = self.text_at(row)
        runs = []
        for region in self.all_regions():
            (r1, c1), (r2, c2) = region.ordered()
            if not r1 <= row <= r2:
                continue
            if region.rectangle:
                left = min(column_of(self.text_at(r1), c1), column_of(self.text_at(r2), c2))
                right = max(column_of(self.text_at(r1), c1), column_of(self.text_at(r2), c2))
                runs.append((index_at_column(text, left), index_at_column(text, right), 'reverse'))
            else:
                start = c1 if row == r1 else 0
                end = c2 if row == r2 else len(text)
                runs.append((start, max(end, start), 'reverse'))
        return runs

    def region_text(self, region):
        (r1, c1), (r2, c2) = region.ordered()
        if region.rectangle:
            left = min(column_of(self.text_at(r1), c1), column_of(self.text_at(r2), c2))
            right = max(column_of(self.text_at(r1), c1), column_of(self.text_at(r2), c2))
            out = []
            for row in range(r1, r2 + 1):
                text = self.text_at(row)
                out.append(text[index_at_column(text, left):index_at_column(text, right)])
            return '\n'.join(out)
        if r1 == r2:
            return self.text_at(r1)[c1:c2]
        parts = [self.text_at(r1)[c1:]]
        for row in range(r1 + 1, r2):
            parts.append(self.text_at(row))
        parts.append(self.text_at(r2)[:c2])
        # 折り返した続きの行(ends_with_newlineでない)は改行を挟まない
        out = parts[0]
        for i, row in enumerate(range(r1 + 1, r2 + 1)):
            joined = self.view.lines[row - 1].ends_with_newline
            out += ('\n' if joined else '') + parts[i + 1]
        return out

    def confirmed_text(self):
        """^[w(C++のconfirmMark()): 空でない範囲が2つ以上で矩形が無ければ空白で、他は改行でつなぐ。範囲は消す。"""
        text = self.joined_text()
        self.regions, self.anchor, self.rectangle = [], None, False
        return text

    def joined_text(self):
        regions = self.all_regions()
        texts = [t for t in (self.region_text(r) for r in regions) if t]
        joiner = ' ' if len(texts) >= 2 and not any(r.rectangle for r in regions) else '\n'
        return joiner.join(texts)
