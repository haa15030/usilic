"""端末のマウス(C++のmouse_sgr.cppとdrag_scroll_speed.cppの、CUIの分)。

xtermのSGR拡張のマウスの報告(ESC [ < Cb ; Cx ; Cy M/m)を読み、慣性スクロールの段ごとの速さ(usilicrcのSPEED・ARROW)と、
ドラッグを離した時の動きの量→段を持つ。
"""
from dataclasses import dataclass

TRACKING_ON = '\x1b[?1003h\x1b[?1006h\x1b[?2004h'   # 全部の動き・SGRの座標・括弧付き貼り付け
TRACKING_OFF = '\x1b[?1003l\x1b[?1006l\x1b[?2004l'


@dataclass
class MouseEvent:
    """MouseSgrEvent。row・columnは0から。buttonは0=左・1=中・2=右(ホイールは-1、動きだけでボタン無しは3)。"""
    row: int
    column: int
    button: int
    motion: bool = False    # 動き(ドラッグ・ホバー)の報告
    pressed: bool = False   # 'M'(押した・押したままの動き)ならTrue、'm'(離した)ならFalse
    wheel: int = 0          # ホイールの1刻み: -1は上(前へ)、1は下


@dataclass
class Paste:
    """括弧付き貼り付けの中身(ファイルのドロップ)。"""
    text: str


def parse_sgr(params):
    """「0;45;12M」(ESC [ < の後ろ)をMouseEventに。形が違えばNone。"""
    if not params or params[-1] not in 'Mm':
        return None
    try:
        cb, cx, cy = (int(x) for x in params[:-1].split(';'))
    except ValueError:
        return None
    if cx < 1 or cy < 1:
        return None
    event = MouseEvent(cy - 1, cx - 1, cb & 0x3, motion=bool(cb & 0x20), pressed=params[-1] == 'M')
    if cb & 0x40:  # ホイール(66/67の横は向きを持たせない)
        event.button = -1
        if not cb & 0x2:
            event.wheel = 1 if cb & 0x1 else -1
    return event


# 段ごとの1ドットの待ち時間(usec、段0は未使用)。usilicrcのSPEEDで置き換えられる。
SCROLL_SPEED = [0, 90000, 20000, 10000, 5000, 3000, 2000, 1000, 500, 200, 100]
DRAG_ARROW_SPEED_OFFSET = 2   # ドラッグで始めた慣性は2段速い方から(ARROWがある時だけ)
TERMINAL_DOTS_PER_ROW = 8     # 端末は行単位なので、1行=8ドットとして待つ
_RELEASE_THRESHOLDS = (1, 2, 3, 5, 8, 12, 16, 20, 25, 30)


class Speeds:
    """慣性の段の速さ(C++のarrowSpeedStep()・arrowSpeedMaxLevel()・dragArrowSpeedOffset())。"""

    def __init__(self, speed=None, arrow=None):
        self.levels = list(SCROLL_SPEED)
        for i, value in enumerate(speed or [], 1):
            if i < len(self.levels) and value > 0:
                self.levels[i] = value
        self.arrow = list(arrow or [])

    def max_level(self):
        return len(self.arrow) if self.arrow else len(self.levels) - 1

    def drag_offset(self):
        return DRAG_ARROW_SPEED_OFFSET if self.arrow else 0

    def seconds_per_row(self, level):
        """段levelで1行進むごとに待つ秒数。"""
        if self.arrow:
            index = max(1, min(level, len(self.arrow)))
            usec = self.arrow[index - 1] if self.arrow[index - 1] > 0 else \
                self.levels[min(index, len(self.levels) - 1)]
        else:
            usec = self.levels[max(0, min(level, len(self.levels) - 1))]
        return usec * TERMINAL_DOTS_PER_ROW / 1e6


def level_for_release_distance(cells):
    """離す直前(150ms)に動いた行数・列数→慣性の段(0は慣性にしない)。"""
    magnitude, level = abs(cells), 0
    for threshold in _RELEASE_THRESHOLDS:
        if magnitude < threshold:
            break
        level += 1
    return level


def step_level_by_key(level, direction, pressed, max_level):
    """慣性中の矢印: 同じ向きなら1段速く、逆なら1段遅く(段1で逆なら向きを変える)。(段, 向き)。"""
    if level == 0:
        return 1, pressed
    if pressed == direction:
        return min(level + 1, max_level), direction
    if level > 1:
        return level - 1, direction
    return level, pressed


def dropped_paths(text):
    """貼り付けた字がファイルのパスの並び(引用符・\\のエスケープ、~/)で、全部あれば、その並び。違えば[]。"""
    import os
    words, word, in_word, quote, i = [], '', False, None, 0
    while i < len(text):
        c = text[i]
        if quote:
            if c == quote:
                quote = None
            else:
                word += c
        elif c == '\\' and i + 1 < len(text):
            i += 1
            word += text[i]
            in_word = True
        elif c in '\'"':
            quote, in_word = c, True
        elif c in ' \t\n\r':
            if in_word:
                words.append(word)
            word, in_word = '', False
        else:
            word += c
            in_word = True
        i += 1
    if in_word:
        words.append(word)
    out = []
    for w in words:
        if w.startswith('~/'):
            w = os.path.expanduser('~') + w[1:]
        if not w.startswith('/') or not os.path.exists(w):
            return []
        out.append(w)
    return out
