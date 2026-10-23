"""行の一覧の表示とカーソル(C++のcontent_view.cppのContentView)。

C++版と同じく、linesは画面の1行ずつ(折り返した後)で、各行は元の行の番号(raw_line_number)を持つ。
描画はdisplay(display.pyのTerminalDisplay)へ頼む。
"""
from dataclasses import dataclass, field
from typing import Optional

from . import utf8


@dataclass
class Line:
    """ContentView::Line。attributesは(開始, 終わり, 属性)の並び(属性は'reverse'・'underline')。"""
    text: str
    raw_line_number: Optional[int] = None
    ends_with_newline: bool = True
    attributes: list = field(default_factory=list)
    links: list = field(default_factory=list)   # (開始, 終わり, href)(EPUBの本文のリンク)
    # カーソルの反転・選択の下線をかける字の範囲(Line::highlightStartColumn・highlightEndColumn)。Noneなら行の字全体。
    highlight_start: Optional[int] = None
    highlight_end: Optional[int] = None
    ellipsis: bool = False            # 長ければ途中を「...」に(ellipsisEligible、LOCATE・DU・ARC・一覧)
    never_highlighted: bool = False   # カーソルがあっても反転しない(neverHighlighted、INFOの開けない行)
    hidden_bytes: int = 0             # C++が行に置く幅0の印(タブの所のU+2060)のバイト数(大きさを数える時だけ足す)
    shell_command: Optional[str] = None  # SHELLのプロンプトの行の送ったコマンド(Line::shellCommand、^Pで戻って^Mで再び)


# 行頭禁則の字(C++のpipe_buffer.cppのisForbiddenAtLineStart())。
FORBIDDEN_AT_LINE_START = set(
    '、。，．・：；？！ー'
    '）〕］｝〉》」』】〙〗〟’”｠»'
    'ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ')


def wrap_text(text, width):
    """textを表示幅widthで折り返した断片の並び(C++ではPipeBuffer::feed()が行う所)。空の行は['']。

    行頭禁則: 行の頭に来る字が禁則の字なら、前の行の最後の1字を次の行へ追い出す(1段だけ、C++版と同じ)。
    """
    if width <= 0:
        return [text]
    if text.isascii():  # ASCIIだけなら1字1桁で、行頭禁則の字も無い(結果は下と同じ、長いfileを組む時に速く)
        return [text[i:i + width] for i in range(0, len(text), width)] if text else ['']
    char_width = utf8.char_width
    pieces, current, used = [], [], 0
    for ch in text:
        w = char_width(ch)
        if used + w > width and current:
            carried = []
            if ch in FORBIDDEN_AT_LINE_START and len(current) > 1:
                carried = [current.pop()]
            pieces.append(''.join(current))
            current = carried
            used = sum(char_width(c) for c in current)
        current.append(ch)
        used += w
    pieces.append(''.join(current))
    return pieces


def expand_tabs(text, tab=8, marker=''):
    """タブを次の8桁の位置までの空白に(桁は表示幅で数える)。markerはタブの所に置く字(C++のPipeBufferのU+2060)。"""
    if '\t' not in text:
        return text
    out, column = [], 0
    for ch in text:
        if ch == '\t':
            spaces = tab - column % tab
            out.append(marker + ' ' * spaces)
            column += spaces
        else:
            out.append(ch)
            column += utf8.display_width_of_codepoint(ord(ch))
    return ''.join(out)


class ContentView:
    """ContentView(content_view.cpp)の、カーソル・topIndex・移動の部分。"""

    def __init__(self):
        self.lines = []
        self.top_index = 0
        self.cursor_index = 0
        self.height = 1                 # 本文の行数(モードラインを除く)
        self.cursor_highlight = True    # setCursorHighlightEnabled()(ページャーではFalse)
        self.position_text_override = None  # setPositionTextOverride()(一時的な通知)
        self.empty_placeholder = None   # 行が無い時に1行目に出す行(setEmptyPlaceholder()、位置は0/0のまま)
        self.raw_line_numbers = True    # 位置を元の行で数える(applyRawLineNumbers())。MAN・KEYの一覧は折り返した行で
        self.bottom_anchored = False    # setBottomAnchored(): 行が画面に足りない時、上を空けて下に寄せる(SHELL)

    # --- 内容 ---
    def set_lines(self, lines):
        self.lines = lines
        self.clamp()

    def set_height(self, height):
        self.height = max(1, height)
        self.clamp()

    def max_top(self):
        return max(0, len(self.lines) - self.height)

    def clamp(self):
        last = max(0, len(self.lines) - 1)
        self.cursor_index = min(max(self.cursor_index, 0), last)
        self.top_index = min(max(self.top_index, 0), self.max_top())

    def ensure_cursor_visible(self):
        """カーソルが画面の外なら、画面をカーソルへ寄せる。"""
        if self.cursor_index < self.top_index:
            self.top_index = self.cursor_index
        elif self.cursor_index >= self.top_index + self.height:
            self.top_index = self.cursor_index - self.height + 1
        self.clamp()

    def sync_visible_cursor(self):
        """画面を動かした後、カーソルを画面の中へ(ContentView::syncVisibleCursor())。"""
        if self.cursor_index < self.top_index:
            self.cursor_index = self.top_index
        elif self.cursor_index >= self.top_index + self.height:
            self.cursor_index = self.top_index + self.height - 1
        self.clamp()

    # --- 移動(Command::MoveDown等) ---
    def move_down(self, count=1):
        self.cursor_index += count
        self.clamp()
        self.ensure_cursor_visible()

    def move_up(self, count=1):
        self.cursor_index -= count
        self.clamp()
        self.ensure_cursor_visible()

    def scroll_line_down(self):
        """ContentView::scrollLineDown(): 画面を1行進め、カーソルは画面の最下行へ。"""
        if self.top_index >= self.max_top():
            return
        self.top_index += 1
        self.cursor_index = min(self.top_index + self.height - 1, len(self.lines) - 1)

    def scroll_line_up(self):
        """ContentView::scrollLineUp(): 画面を1行戻し、カーソルは画面の最上行へ。"""
        if self.top_index <= 0:
            return
        self.top_index -= 1
        self.cursor_index = self.top_index

    def page_down(self):
        """ContentView::pageDown(): 画面もカーソルも1画面分進める。"""
        if not self.lines:
            return
        self.top_index = min(self.max_top(), self.top_index + self.height)
        self.cursor_index = min(len(self.lines) - 1, self.cursor_index + self.height)

    def page_up(self):
        """ContentView::pageUp()。"""
        self.top_index = max(0, self.top_index - self.height)
        self.cursor_index = max(0, self.cursor_index - self.height)

    def go_to_top(self):
        self.cursor_index = 0
        self.top_index = 0

    def go_to_bottom(self):
        self.cursor_index = max(0, len(self.lines) - 1)
        self.top_index = self.max_top()

    def go_to_line(self, number):
        """number行目(1始まり)へ。元の行の番号があればそれで探す(^[N)。"""
        target = None
        for i, line in enumerate(self.lines):
            if line.raw_line_number == number:
                target = i
                break
        if target is None:
            target = number - 1
        self.cursor_index = target
        self.clamp()
        self.top_index = self.cursor_index - self.height // 2
        self.clamp()

    def anchor_offset(self):
        """bottomAnchorRowOffset(): 下に寄せる時、1行目の上に空ける行の数(寄せない時・画面が埋まる時は0)。"""
        if not self.bottom_anchored:
            return 0
        return max(0, self.height - min(len(self.lines) - self.top_index, self.height))

    def line_index_at_row(self, row):
        """画面の行(0から)の行の番号。行が無ければ-1(C++のlineIndexAtRow())。"""
        index = self.top_index + row - self.anchor_offset()
        if index < self.top_index:
            return -1
        return index if 0 <= row < self.height and index < len(self.lines) else -1

    def visible_lines(self):
        return self.lines[self.top_index:self.top_index + self.height]
