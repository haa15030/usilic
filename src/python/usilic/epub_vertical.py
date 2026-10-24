"""EPUBの縦書き(C++のepub_vertical.cppと、application.cppのrebuildEpubVertical()・handleEpubVerticalCommand()・
縦書きのCOPYの、CUIの分)。

本文の論理行(段落、横書きの画面の行を元の行の番号でつなぎ直したもの)を、1行=1列(全角2桁+行間1桁)にして
右から左へ並べる。句読点・括弧等は縦書き用の字形へ置き換え、半角の英数2字までは1マスに横に並べ(縦中横)、
行頭禁則・行末禁則は前の列の最後の字を次の列へ送る(追い出し)。下線(リンク)の字は、その列の右の行間に
「|」を置いて傍線にする(端末の下線は字ごとに細切れになるため、C++の-cと同じ)。

範囲指定(COPY)は列の番号とその列の上からのマスで持ち、記録(~/.usilic/epub-marks.txt)の位置は章の頭からの
字数(論理行の字で数える)。
"""
import re
from dataclasses import dataclass, field
from typing import Optional

from . import mode_line, utf8

PITCH = 3  # 1列の幅(全角2桁+行間1桁)

_VERTICAL_FORMS = {
    0x3001: 0xFE11, 0x3002: 0xFE12, 0xFF0C: 0xFE10, 0x2026: 0xFE19, 0x2025: 0xFE30, 0x2014: 0xFE31,
    0x2015: 0xFE31, 0x300C: 0xFE41, 0x300D: 0xFE42, 0x300E: 0xFE43, 0x300F: 0xFE44, 0xFF08: 0xFE35,
    0xFF09: 0xFE36, 0xFF5B: 0xFE37, 0xFF5D: 0xFE38, 0x3014: 0xFE39, 0x3015: 0xFE3A, 0x3010: 0xFE3B,
    0x3011: 0xFE3C, 0x300A: 0xFE3D, 0x300B: 0xFE3E, 0x3008: 0xFE3F, 0x3009: 0xFE40, 0xFF3B: 0xFE47,
    0xFF3D: 0xFE48, 0x30FC: 0xFF5C, 0x2500: 0xFF5C, ord('('): 0xFE35, ord(')'): 0xFE36, ord('['): 0xFE47,
    ord(']'): 0xFE48,
    # 矢印は90度回した向きに
    0x2192: 0x2193, 0x2190: 0x2191, 0x2193: 0x2190, 0x2191: 0x2192,
    0x21D2: 0x21D3, 0x21D0: 0x21D1, 0x21D3: 0x21D0, 0x21D1: 0x21D2,
    # ＜＞は〈〉の縦書きの字形
    0xFF1C: 0xFE3F, 0xFF1E: 0xFE40,
}

_FORMS = {chr(k): chr(v) for k, v in _VERTICAL_FORMS.items()}

# 行頭禁則(列の先頭に来てはいけない字)
_NO_START = set(chr(c) for c in (
    0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF09, 0x300D, 0x300F, 0x3011, 0x3015, 0x3009, 0x300B, 0xFF3D,
    0xFF5D, 0x2026, 0x2025, 0x30FC, 0x3005, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083,
    0x3085, 0x3087, 0x308E, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7,
    0x30EE, 0x30F5, 0x30F6, 0xFF01, 0xFF1F, 0xFF1A, 0xFF1B, 0x30FB)) | set(')]!?,.')
# 行末禁則(列の最後に来てはいけない字)
_NO_END = set(chr(c) for c in (0xFF08, 0x300C, 0x300E, 0x3010, 0x3014, 0x3008, 0x300A, 0xFF3B, 0xFF5B)) | set('([')
# 傍点の読み(「《﹅》」、端末では書かない、C++のisEmphasisMarkReading())
_EMPHASIS = set('﹅﹆◎◉○●△▲◦•')


@dataclass
class SourceLine:
    """論理行(C++のEpubVerticalSourceLine)。runs・linksの位置はtextの字の位置。"""
    text: str
    raw: int
    runs: list = field(default_factory=list)     # (開始, 終わり, 'bold'・'underline')
    links: list = field(default_factory=list)    # (開始, 終わり, href)
    indent_first: int = 0   # 最初の列の頭の空きのマス
    indent_rest: int = 0    # 続く列の頭の空きのマス
    align_end: bool = False  # 1列に収まれば列の下端に寄せる(著者名等)


@dataclass(slots=True)
class Cell:
    glyph: str              # 画面の2桁分(全角1字、半角2字、半角+空白)
    original: str = ' '
    attr: Optional[str] = None
    link: int = -1          # 論理行の中のリンクの番号
    start: int = -1         # 論理行の中の字の範囲[start, end)(空きのマスは-1)
    end: int = -1
    note: bool = False      # 本文の注釈番号(epub_modeの'note'、決まった色で描く、C++のEpubVerticalCell::fg)


@dataclass(slots=True)
class Column:
    cells: list
    raw: int
    padding: bool = False   # 章の頭を画面の右端に揃えるために足した空の列
    title_page: bool = False  # 扉の1ページ分の列(その空の列も含む)


_BLANK = Cell('  ')


def _is_alnum(ch):
    return ch.isascii() and ch.isalnum()


def _is_graphic(ch):
    return 0x20 < ord(ch) < 0x7F


def to_cells(line):
    """論理行を縦書きのマスへ(C++のtoCells()の-cの分)。"""
    s = line.text
    width_of = utf8.display_width_of_codepoint
    glyphs = [(ch, w, i) for i, ch in enumerate(s) for w in (width_of(ord(ch)),) if w > 0]
    if '《' in s:  # 傍点の「《﹅》」は書かない
        kept, i = [], 0
        while i < len(glyphs):
            if glyphs[i][0] == '《' and i + 2 < len(glyphs) and glyphs[i + 2][0] == '》' and \
                    glyphs[i + 1][0] in _EMPHASIS:
                i += 3
                continue
            kept.append(glyphs[i])
            i += 1
        glyphs = kept
    while glyphs and glyphs[-1][0] == ' ':
        glyphs.pop()
    n = len(glyphs)
    # 半角の続く長さ(空白以外の半角で始まって終わり、途中の空白を含む)
    run_length = [0] * n
    if any(ch < '\x7f' for ch in s):
        i = 0
        while i < n:
            if not _is_graphic(glyphs[i][0]):
                i += 1
                continue
            j = end = i
            while j < n and (_is_graphic(glyphs[j][0]) or glyphs[j][0] == ' '):
                if _is_graphic(glyphs[j][0]):
                    end = j + 1
                j += 1
            for k in range(i, end):
                run_length[k] = end - i
            i = end
    # 字の位置ごとの属性とリンクの番号(行ごとに一度だけ作る)
    attr_of, link_of = [None] * (len(s) + 1), [-1] * (len(s) + 1)
    for start, end, attr in reversed(line.runs):  # 前の物が勝つ(重なりは最初のラン)
        if attr in ('bold', 'underline', 'match'):  # 'match'は検索結果の一覧の一致の字
            attr_of[start:end] = [attr] * len(attr_of[start:end])
    note_of = [False] * (len(s) + 1)
    for start, end, attr in line.runs:
        if attr == 'note':
            note_of[start:end] = [True] * len(note_of[start:end])
    for number, (start, end, _) in reversed(list(enumerate(sorted(line.links)))):
        link_of[start:end] = [number] * len(link_of[start:end])
    cells, i = [], 0
    while i < n:
        ch, width, pos = glyphs[i]
        link = link_of[pos]
        attr = 'underline' if link >= 0 else attr_of[pos]  # リンクは太字等と重なっても傍線
        if ch < '\x7f' and i + 1 < n:  # 縦中横: 半角英数2字まで、"!!" "!?" "?!" "??"
            h = glyphs[i + 1][0]
            if (_is_alnum(ch) and _is_alnum(h) and run_length[i] == 2) or (ch in '!?' and h in '!?'):
                cells.append(Cell(ch + h, ch, attr, link, pos, glyphs[i + 1][2] + 1))
                i += 2
                continue
        glyph = _FORMS.get(ch)
        if glyph is None:
            glyph = '  ' if ch == ' ' else ch if width >= 2 else ch + ' '
        cells.append(Cell(glyph, ch, attr, link, pos, pos + 1))
        i += 1
    for cell in cells:
        if 0 <= cell.start < len(note_of):
            cell.note = note_of[cell.start]
    return cells


def layout_columns(lines, rows):
    """1列の高さrowsで、論理行を列の並びへ組む(C++のlayoutEpubVerticalColumns())。"""
    height = max(1, rows)
    cols = []
    for line in lines:
        cells = to_cells(line)
        if not cells:
            cols.append(Column([], line.raw))
            continue
        if line.align_end and len(cells) < height:  # 下端に寄せる
            cells = [_BLANK] * (height - len(cells)) + cells
        i = 0
        while i < len(cells):
            indent = max(0, min(line.indent_first if i == 0 else line.indent_rest, height - 1))
            count = min(height - indent, len(cells) - i)
            if i + count < len(cells):
                # 追い出し: 次の列の頭が行頭禁則の字なら、この列の最後の字も次の列へ(2字まで)
                for _ in range(2):
                    if count > 1 and cells[i + count].original in _NO_START:
                        count -= 1
                if count > 1 and cells[i + count - 1].original in _NO_END:
                    count -= 1
            cols.append(Column([_BLANK] * indent + cells[i:i + count], line.raw))
            i += count
    return cols


def columns_per_screen(columns):
    return max(1, columns // PITCH)


def screen_column(k, columns):
    """右からk番目の列の左の桁(行間はその右の桁)。"""
    return columns - 3 - k * PITCH


def column_at(screen_col, columns):
    """画面の桁(0から)が右から何番目の列か。列の上でなければ(行間・画面の外)-1(C++のepubVerticalColumnAt())。"""
    from_right = columns - 1 - screen_col
    if from_right < 0 or from_right % PITCH == 0:
        return -1
    k = from_right // PITCH
    return k if k < columns_per_screen(columns) else -1


class VerticalBook:
    """縦書きの本文の列の並びと、右端の列(first)・列カーソル(cursor)・記録(mark)・範囲指定(C++のApplicationの
    epubVerticalCols_・epubVerticalFirst_・epubVerticalCursor_・epubVerticalMark_・epubSelect…)。"""

    def __init__(self, sources, chapter_raws, rows, columns, chapter_breaks, centered=(), horizontal=()):
        self.rows, self.columns = max(1, rows), columns
        self.per_screen = columns_per_screen(columns)
        self.chapter_breaks = chapter_breaks
        self.texts = {s.raw: s.text for s in sources}
        self.order = [s.raw for s in sources]
        self.link_targets = {s.raw: [h for _, _, h in sorted(s.links)] for s in sources if s.links}
        self.cols, self.boundaries = [], []
        laid = layout_columns(sources, self.rows)
        c = 0
        while c < len(laid):
            raw = laid[c].raw
            line_start = c == 0 or laid[c - 1].raw != raw
            box = next((r for r in centered if r[0] <= raw <= r[1]), None)
            if box is None:
                if line_start and raw in chapter_raws:
                    self._pad_to_page()
                self.cols.append(laid[c])
                c += 1
                continue
            # 扉(横書きの中の縦書きの箱): 1ページとし、両側に空の列を足して中央に置く
            self._pad_to_page()
            end = c
            while end < len(laid) and box[0] <= laid[end].raw <= box[1]:
                end += 1
            title, c = laid[c:end], end
            pad = max(0, self.per_screen - len(title))
            self.cols += [Column([], box[0], padding=True, title_page=True) for _ in range(pad // 2)]
            for column in title:
                column.title_page = True
                self.cols.append(column)
            self.cols += [Column([], box[1], padding=True, title_page=True) for _ in range(pad - pad // 2)]
            self._pad_to_page()  # 扉のページの後
        self._pad_to_page()  # 最後の章も右端から始められるよう
        # 本文の後に横書きの範囲(奥付等)があれば、最後の章も本文の最後の列が右端に来るまで送れるよう1画面分の空の列
        last_raw = max((col.raw for col in self.cols if not col.padding and col.cells), default=0)
        if horizontal and horizontal[-1][0] > last_raw and self.cols:
            raw = self.cols[-1].raw
            self.cols += [Column([], raw, padding=True) for _ in range(self.per_screen)]
        self.first = self.cursor = 0
        self.mark = None
        self.link, self.link_raw = -1, -1
        # 範囲指定(COPY): 押した所(anchor)と今の所(point)、(列, マス)
        self.selecting = False
        self.anchor = None
        self.point = (0, 0)
        self.asking = False   # Record?(y/n)の答え待ち

    def _pad_to_page(self):
        """章の頭の列が画面1枚分の区切りに来るよう空の列を足す(章ごとの時だけ、連続なら区切りだけ記録)。"""
        if self.chapter_breaks:
            size = len(self.cols)
            page = size - size % self.per_screen
            if page != size and all(not c.cells and not c.title_page for c in self.cols[page:]):
                del self.cols[page:]  # 章の終わりの空行だけの新しいページは白紙になるので除く
            raw = self.cols[-1].raw if self.cols else 0
            while len(self.cols) % self.per_screen:
                self.cols.append(Column([], raw, padding=True))
        at = len(self.cols)
        if at > 0 and (not self.boundaries or self.boundaries[-1] != at):
            self.boundaries.append(at)

    # --- 位置 ---
    def current_raw(self):
        if not 0 <= self.cursor < len(self.cols):
            return 1
        return self.cols[self.cursor].raw

    def show_raw(self, raw, offset=0):
        """元の行rawの、字の位置offsetを含む列を右端に置く。"""
        first = None
        for c, column in enumerate(self.cols):
            if column.raw < raw:
                continue
            if first is None:
                first = c
            if column.raw != raw:
                break
            if any(cell.start >= 0 and cell.start <= offset < cell.end for cell in column.cells):
                first = c
                break
        first = first if first is not None else max(0, len(self.cols) - 1)
        cursor = first  # 扉のページなら書名の列へ(列の頭はページの頭のまま)
        while cursor + 1 < len(self.cols) and self.cols[cursor].title_page and self.cols[cursor].padding:
            cursor += 1
        self.set_view(first, cursor)

    def shown_range(self, first):
        """右端の列firstの章の列の範囲[from, until)(章ごとの時、画面には右端の列の章だけを描く)。"""
        if not self.chapter_breaks:
            return 0, len(self.cols)
        start, until = 0, len(self.cols)
        for boundary in self.boundaries:
            if boundary <= first:
                start = boundary
            else:
                until = boundary
                break
        return start, until

    def set_view(self, first, cursor, link=None):
        total = len(self.cols)
        last = max(0, total - self.per_screen)
        cursor = max(0, min(cursor, total - 1))
        first = max(0, min(first, last))
        if cursor < first:
            first = cursor
        if cursor >= first + self.per_screen:
            first = cursor - self.per_screen + 1
        self.first, self.cursor = max(0, min(first, last)), cursor
        self.link = -1
        self.link_raw = self.cols[cursor].raw if total else -1
        if link is not None:
            self.link = link
        elif total:
            self.link = next((cell.link for cell in self.cols[cursor].cells if cell.link >= 0), -1)

    def set_first(self, first):
        last = max(0, len(self.cols) - self.per_screen)
        first = max(0, min(first, last))
        self.set_view(first, max(first, min(self.cursor, first + self.per_screen - 1)))

    def step(self, cursor, forward):
        """1列の送り/戻り(C++のepubVerticalStep())。空の列は飛ばし、飛ばした時は着いた列のページの頭を
        右端にする。返り値は(右端の列(-1なら今のまま), 列カーソル)。"""
        n = len(self.cols)
        if n == 0:
            return -1, cursor
        cursor = max(0, min(cursor, n - 1))
        d = 1 if forward else -1
        t, jumped = cursor + d, False
        if self.cols[cursor].title_page:  # 扉のページは1列の送り/戻りでページごと越える
            while 0 <= t < n and self.cols[t].title_page:
                t += d
            jumped = True
        while 0 <= t < n and self.cols[t].padding and not self.cols[t].title_page:
            t += d
            jumped = True
        if not 0 <= t < n:
            return -1, cursor
        if self.cols[t].title_page:
            return self.title_page_of(t)
        return ((t // self.per_screen) * self.per_screen if jumped else -1), t

    def title_page_of(self, c):
        """扉の列cの(ページの頭, 書名の列)。"""
        start = c
        while start > 0 and self.cols[start - 1].title_page:
            start -= 1
        title = start
        while title < len(self.cols) and self.cols[title].title_page and self.cols[title].padding:
            title += 1
        if title >= len(self.cols) or not self.cols[title].title_page:
            title = start
        return start, title

    def step_first(self, delta):
        """右端の列を1列ずつdelta列ずらす(j/k)。列カーソルは送る向きの先頭。動いたらTrue。
        連続スクロールでは扉も1列ずつ流す(C++のstepEpubVerticalFirst()、章ごとの時だけ扉をページごと越える)。"""
        moved = False
        if not self.chapter_breaks:
            n = len(self.cols)
            for _ in range(abs(delta)):
                first = max(0, min(self.first + (1 if delta > 0 else -1), max(0, n - self.per_screen)))
                if first == self.first:
                    break
                last_shown = min(first + self.per_screen, n) - 1
                cursor = last_shown if delta > 0 else first
                while delta > 0 and cursor > first and self.cols[cursor].padding:
                    cursor -= 1
                while delta < 0 and cursor < last_shown and self.cols[cursor].padding:
                    cursor += 1
                self.set_view(first, cursor)
                moved = True
            return moved
        for _ in range(abs(delta)):
            before = self.first
            first, target = self.step(self.first, delta > 0)
            title = 0 <= target < len(self.cols) and self.cols[target].title_page
            self.set_first(first if title and first >= 0 else target)
            if title and self.first != before:
                moved = True
                continue
            if self.first != before:
                cursor = self.first
                if delta > 0:
                    until = self.shown_range(self.first)[1]
                    cursor = min(self.first + self.per_screen, until) - 1
                    while cursor > self.first and self.cols[cursor].padding:
                        cursor -= 1
                self.set_view(self.first, cursor)
                moved = True
            else:
                break
        return moved

    def snap_to_page(self, forward):
        c = self.cursor
        if 0 <= c < len(self.cols) and self.cols[c].title_page:  # 扉: ページの頭を右端に、カーソルは書名の列
            self.set_view(*self.title_page_of(c))
            return
        if 0 <= c < len(self.cols) and self.cols[c].padding:
            first, cursor = self.step(c, forward)
            self.set_view(self.first if first < 0 else first, cursor)

    def select_adjacent_link(self, delta):
        """複数のリンクのある段落: ^N/^Pで次/前のリンクを選ぶ。"""
        if self.link < 0 or not 0 <= self.cursor < len(self.cols):
            return False
        raw = self.cols[self.cursor].raw
        targets = self.link_targets.get(raw, [])
        nxt = self.link + delta
        if raw != self.link_raw or len(targets) < 2 or not 0 <= nxt < len(targets):
            return False
        for c, column in enumerate(self.cols):
            if column.raw == raw and any(cell.link == nxt for cell in column.cells):
                self.set_view(self.first, c, nxt)
                return True
        return False

    def selected_href(self):
        """列カーソルの段落の、選んでいるリンクの行き先(無ければNone)。"""
        targets = self.link_targets.get(self.link_raw, [])
        return targets[self.link] if 0 <= self.link < len(targets) else None

    def command(self, number, repeated_page=False):
        """縦書きの移動(K-番号)。←/^N/↓で1列先(左)へ、→/^P/↑で1列戻る、j/kで右端の列を1列ずつ、SPC/^Vで
        1画面先、^[vで1画面戻る、^[</^[>で末尾/先頭(横書きと逆)、^␣・^X^Xで記録の列と行き来。"""
        first, cursor = self.first, self.cursor
        if number == 3:
            if not self.select_adjacent_link(1):
                f, nxt = self.step(cursor, True)
                self.set_view(first if f < 0 else f, nxt)
        elif number == 4:
            if not self.select_adjacent_link(-1):
                f, nxt = self.step(cursor, False)
                self.set_view(first if f < 0 else f, nxt)
        elif number == 1:
            self.step_first(1)
        elif number == 2:
            self.step_first(-1)
        elif number in (5, 6, 7):
            if not repeated_page:
                self.mark = cursor
            self.set_view(first + self.per_screen, cursor + self.per_screen)
            self.snap_to_page(True)
        elif number in (8, 9, 10):
            if not repeated_page:
                self.mark = cursor
            self.set_view(first - self.per_screen, cursor - self.per_screen)
            self.snap_to_page(False)
        elif number == 12:
            self.mark = cursor
            self.set_view(0, 0)
        elif number == 11:
            self.mark = cursor
            self.set_view(len(self.cols), len(self.cols))
        elif number == 14:
            if self.mark is None:
                return False
            target, self.mark = self.mark, cursor
            self.set_view(target, target)
        else:
            return None
        return True

    def pop_mark(self):
        if self.mark is None:
            return False
        self.set_view(self.mark, self.mark)
        return True

    # --- 描画 ---
    def position_text(self):
        """「列カーソルの元の行/元の行の数 %」(元の行で数える、空の列は手前の列の行)。"""
        current = total = 0
        for i, column in enumerate(self.cols):
            total = max(total, column.raw)
            if i <= self.cursor:
                current = max(current, column.raw)
        if total <= 0:
            current, total = self.cursor + 1, len(self.cols)
        current = max(current, 1)
        return mode_line.format(current, max(total, 1))  # 横書きと同じく総数の桁(C++と同じ、以前は5桁に決め打ち)

    def cursor_screen_position(self):
        """端末のカーソルの(行, 桁): 範囲指定の間は今の所のマス、ふだんは列カーソルの列の頭。"""
        if self.selecting:
            c, r = self.point
            return r, max(0, screen_column(c - self.first, self.columns))
        return 0, max(0, screen_column(self.cursor - self.first, self.columns))

    def render(self, highlights=None, highlight_column=-1):
        """右端に列firstを置いて画面1枚分を描く。(文字列, runs)の並び。highlightsは元の行→[(開始, 終わり)]
        (検索の一致)。リンクのある列にカーソルがあれば、選んでいるリンクの字を反転表示する。"""
        columns = self.columns
        shown_from, shown_until = self.shown_range(self.first)
        selection = self.selection()
        highlight_link = self.link if self.selected_href() is not None else -1
        out = []
        for r in range(self.rows):
            screen = [' '] * columns
            attrs = [None] * columns
            for k in range(self.per_screen):
                c = self.first + k
                x = screen_column(k, columns)
                if x < 0 or not shown_from <= c < shown_until or c >= len(self.cols):
                    continue
                column = self.cols[c]
                if r >= len(column.cells):
                    continue
                cell = column.cells[r]
                if utf8.display_width_of_codepoint(ord(cell.glyph[0])) >= 2:
                    screen[x] = cell.glyph
                    if x + 1 < columns:
                        screen[x + 1] = ''
                else:
                    screen[x] = cell.glyph[0]
                    if x + 1 < columns:
                        screen[x + 1] = cell.glyph[1:]
                attr = cell.attr
                if attr == 'underline':  # 傍線: 列の右の行間に「|」
                    attr = 'note' if cell.note else None
                    if x + 2 < columns:
                        screen[x + 2] = '|'
                # 強調の順: 選んでいるリンク・列(一覧の選んでいる行) < 検索の一致 < 範囲指定
                if c == highlight_column:
                    attr = 'reverse'
                if highlight_link >= 0 and column.raw == self.link_raw and cell.link == highlight_link:
                    attr = 'reverse'
                if highlights and cell.start >= 0 and any(s < cell.end and cell.start < e
                                                          for s, e in highlights.get(column.raw, ())):
                    attr = 'match'
                if selection is not None and _contains(selection, c, r):
                    attr = 'reverse'
                attrs[x] = attr
                if x + 1 < columns:
                    attrs[x + 1] = attr
            text, runs = '', []
            for x in range(columns):
                start = len(text)
                text += screen[x]
                end = len(text)
                if attrs[x] and end > start:
                    if runs and runs[-1][1] == start and runs[-1][2] == attrs[x]:
                        runs[-1] = (runs[-1][0], end, attrs[x])
                    else:
                        runs.append((start, end, attrs[x]))
            text = text.rstrip(' ')
            runs = [(s, min(e, len(text)), a) for s, e, a in runs if s < len(text)]
            out.append((text, runs))
        return out

    # --- 範囲指定(COPY) ---
    def cell_at(self, row, screen_col):
        """画面の行・桁のマス(列, 列の上からのマス)。行間や画面の外は近い列・マスへ(C++のepubSelectCellAt())。"""
        from_right = max(0, self.columns - 1 - screen_col)
        k = min(from_right // PITCH, self.per_screen - 1)
        c = max(0, min(self.first + k, len(self.cols) - 1))
        return c, max(0, min(row, self.rows - 1))

    def start_select(self):
        self.selecting, self.anchor, self.asking = True, None, False
        column = self.cols[self.cursor] if self.cols else None
        row = next((i for i, cell in enumerate(column.cells) if cell.start >= 0), 0) if column else 0
        self.point = (self.cursor, row)

    def end_select(self):
        self.selecting, self.anchor, self.asking = False, None, False

    def selection(self):
        if not self.selecting or self.anchor is None:
            return None
        return min(self.anchor, self.point), max(self.anchor, self.point)

    def _cells_in(self, c):
        return len(self.cols[c].cells) if 0 <= c < len(self.cols) else 0

    def move_point(self, columns=0, cells=0):
        """今の所を、列(正で先=左へ)・マス(正で下へ、列の端では次/前の列へ)だけ動かし、見える所まで画面を送る。"""
        c, r = self.point
        n = len(self.cols)
        if columns:
            c = max(0, min(n - 1, c + columns))
            r = min(r, max(0, self._cells_in(c) - 1))
        for _ in range(abs(cells)):
            if cells > 0:
                if r + 1 < self._cells_in(c):
                    r += 1
                elif c + 1 < n:
                    c, r = c + 1, 0
            else:
                if r > 0:
                    r -= 1
                elif c > 0:
                    c -= 1
                    r = max(0, self._cells_in(c) - 1)
        self.point = (c, r)
        if c < self.first or c >= self.first + self.per_screen:
            self.set_view(c if c < self.first else c - self.per_screen + 1, c)
        elif not self.shown_range(self.first)[0] <= c < self.shown_range(self.first)[1]:
            self.set_view(c, c)

    def point_to_column_edge(self, end):
        c, _ = self.point
        self.point = (c, max(0, self._cells_in(c) - 1) if end else 0)

    def toggle_anchor(self):
        """^@: 範囲を始める。もう一度で範囲を閉じ、範囲があればTrue(Record?(y/n)と問う)。"""
        if self.anchor is None:
            self.anchor = self.point
            return False
        if self.anchor == self.point:
            self.anchor = None
            return False
        self.asking = True
        return True

    def selection_ranges(self):
        """範囲の字を、論理行ごとの(元の行, 開始, 終わり)に。"""
        selection = self.selection()
        if selection is None:
            return []
        ranges = []
        (fc, _), (tc, _) = selection
        for c in range(fc, min(tc, len(self.cols) - 1) + 1):
            column = self.cols[c]
            for r, cell in enumerate(column.cells):
                if cell.start < 0 or not _contains(selection, c, r):
                    continue
                if ranges and ranges[-1][0] == column.raw:
                    raw, start, end = ranges[-1]
                    ranges[-1] = (raw, min(start, cell.start), max(end, cell.end))
                else:
                    ranges.append((column.raw, cell.start, cell.end))
        return ranges

    def selection_text(self, separator=' '):
        """範囲の字(論理行ごとに切り出してseparatorでつなぐ)。空白の並びは1つの空白に(C++の記録と同じ)。"""
        pieces = [self.texts.get(raw, '')[start:end] for raw, start, end in self.selection_ranges()]
        pieces = [squeeze_spaces(p) for p in pieces]
        return separator.join(p for p in pieces if p)

    def offset_in_chapter(self, chapter_raw, raw, pos):
        return offset_in_chapter(self.order, self.texts, chapter_raw, raw, pos)


def offset_in_chapter(order, texts, chapter_raw, raw, pos):
    """章の頭の元の行chapter_rawから、元の行rawの字の位置posまでの字数(記録の位置)。orderは元の行の並び、
    textsは元の行→論理行の字。"""
    offset = 0
    for r in order:
        if r < chapter_raw:
            continue
        if r >= raw:
            return offset + (pos if r == raw else 0)
        offset += len(texts[r])
    return offset


def place_in_chapter(order, texts, chapter_raw, offset, text=''):
    """記録の位置(章の頭からの字数)を(元の行, 字の位置)に。記録の字がそこに無ければ(C++版との数え方の違い等)、
    章の頭から先でその字を探す。"""
    found, remaining = None, offset
    chapter = [r for r in order if r >= chapter_raw]
    for r in chapter:
        n = len(texts[r])
        if remaining < n:
            found = (r, remaining)
            break
        remaining -= n
    head = squeeze_spaces(text)[:10]
    if head and (found is None or not squeeze_spaces(texts[found[0]][found[1]:]).startswith(head)):
        for r in chapter:
            pos = texts[r].find(head)
            if pos >= 0:
                return r, pos
    return found


def squeeze_spaces(text):
    """半角の空白・タブ・改行の並びを空白1つに(全角の空白は字のまま、C++のepubSelectionText()と同じ)。"""
    return re.sub(r'[ \t\r\n]+', ' ', text).strip(' ')


def _contains(selection, c, r):
    (fc, fr), (tc, tr) = selection
    if c < fc or c > tc:
        return False
    if c == fc and r < fr:
        return False
    if c == tc and r > tr:
        return False
    return True
