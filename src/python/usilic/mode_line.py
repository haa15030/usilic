"""モードラインの位置の書式(C++のmode_line.cpp)。"""


def format(current_line, total_lines):  # noqa: A001  C++のModeLine::format()と同じ名前
    """「001/500 TOP」「250/500 50%」「500/500 END」。currentはtotalの桁数まで0で埋める。"""
    total = str(max(total_lines, 0))
    current = str(max(current_line, 0)).rjust(len(total), '0')
    # TOPより先にENDを見る(1行だけの時は「すべて見終えた」が優先)。
    if current_line >= total_lines:
        return current + '/' + total + ' END'
    if current_line <= 1:
        return current + '/' + total + ' TOP'
    percent = current_line * 100 // total_lines if total_lines > 0 else 0
    return current + '/' + total + ' ' + '%2d%%' % percent


def format_fixed_width(current_line, total_lines):
    """SHELLの「00002/00002 END」(5桁の0埋め、ModeLine::formatFixedWidth())。"""
    total = str(max(total_lines, 0)).rjust(5, '0')
    current = str(max(current_line, 0)).rjust(5, '0')
    if current_line >= total_lines:
        return current + '/' + total + ' END'
    if current_line <= 1:
        return current + '/' + total + ' TOP'
    percent = current_line * 100 // total_lines if total_lines > 0 else 0
    return current + '/' + total + ' ' + '%2d%%' % percent


# --- 欄からモードラインを組む(C++のContentView::scrollModeLineSegments()) ---
from dataclasses import dataclass  # noqa: E402

from . import utf8  # noqa: E402

TAG, QUERY_ECHO, PATH, PATH_MIDDLE = 'tag', 'query', 'path', 'pathmiddle'   # TrailingKind
TRAILING_PATH_TAIL_CHARS = 16   # truncatedTrailingPath()
_RESERVABLE_FIELD_LENGTH = 20


@dataclass
class Fields:
    """ContentView::ModeLineFields。欄は反転し、欄の間は反転しない空白1つ。"""
    name: str = ''
    trailing: str = ''
    fill_trailing_to_edge: bool = False
    right_watermark: str = ''
    trailing2: str = ''
    trailing_plain: bool = False
    trailing2_kind: str = QUERY_ECHO
    trailing_kind: str = TAG
    trailing3: str = ''
    split_layout_when_crowded: bool = False
    trailing2_plain: bool = False


def _bytes(s):
    return len(s.encode('utf-8'))


def _cut_bytes(s, start=None, end=None):
    """UTF-8のバイトの位置で切る(字の途中なら字の境目へ)。"""
    data = s.encode('utf-8')
    return data[start:end].decode('utf-8', 'ignore')


def truncated_trailing_path(path):
    """truncatedTrailingPath(): 16バイトより長ければ「...」+終わりの16バイト。"""
    if _bytes(path) <= TRAILING_PATH_TAIL_CHARS:
        return path
    return '...' + _cut_bytes(path, _bytes(path) - TRAILING_PATH_TAIL_CHARS)


def _middle_ellipsis_bytes(text, max_bytes):
    """middleEllipsisTruncate(): バイト数で、途中を「...」に。"""
    budget = max_bytes - 3
    if budget <= 0:
        return _cut_bytes(text, 0, max(0, max_bytes))
    boundaries, offset = [0], 0
    for ch in text:
        offset += _bytes(ch)
        boundaries.append(offset)
    size = boundaries[-1]
    head_budget = budget // 2
    tail_budget = budget - head_budget
    head_end = 0
    for b in boundaries:
        if b > head_budget:
            break
        head_end = b
    tail_start = size
    for b in reversed(boundaries):
        if size - b > tail_budget:
            break
        tail_start = b
    if tail_start <= head_end:
        return text
    return _cut_bytes(text, 0, head_end) + '...' + _cut_bytes(text, tail_start)


def _width(s):
    return utf8.text_width(s)


def middle_ellipsis_by_width(text, max_width):
    """middleEllipsisTruncateByWidth(): 表示幅で、途中を「...」に(幅0の字は前の字に付ける)。"""
    if _width(text) <= max_width:
        return text
    budget = max_width - 3
    if budget <= 0:
        return text[:max(0, max_width)]
    clusters = []
    for ch in text:
        w = utf8.display_width_of_codepoint(ord(ch))
        if w == 0 and clusters:
            clusters[-1][0] += ch
        else:
            clusters.append([ch, w])
    head_budget = budget // 2
    tail_budget = budget - head_budget
    head, used = 0, 0
    for i, (_, w) in enumerate(clusters):
        if used + w > head_budget:
            break
        used += w
        head = i + 1
    tail, used = len(clusters), 0
    for i in range(len(clusters) - 1, -1, -1):
        if used + clusters[i][1] > tail_budget:
            break
        used += clusters[i][1]
        tail = i
    if tail <= head:
        return text
    return ''.join(c for c, _ in clusters[:head]) + '...' + ''.join(c for c, _ in clusters[tail:])


def _truncate_by_kind(text, kind, budget):
    """truncateFieldByKind()。"""
    if budget <= 0:
        return ''
    if _bytes(text) <= budget:
        return text
    if budget < 4:
        return ''
    if kind == PATH_MIDDLE:
        return _middle_ellipsis_bytes(text, budget)
    tail = min(16, budget - 3) if kind == QUERY_ECHO else budget - 3
    return '...' + _cut_bytes(text, _bytes(text) - tail)


def _with_truncated_fields(f, position, cols):
    """withTruncatedFields()。"""
    if cols <= 0:
        return f
    reserved = 1
    for text, extra in ((f.trailing, 1), (f.right_watermark, 2), (f.trailing2, 1), (f.trailing3, 1)):
        if text and _bytes(text) <= _RESERVABLE_FIELD_LENGTH:
            reserved += extra + _bytes(text)
    budget = cols - len(position) - reserved
    if budget < 0:
        budget = cols - len(position) - 1
    budget = max(0, budget)
    if _bytes(f.name) > budget:
        f.name = _middle_ellipsis_bytes(f.name, budget) if budget >= 4 else _cut_bytes(f.name, 0, budget)
    if f.trailing_kind != TAG and f.trailing:
        base = len(position) + (_bytes(f.name) + 1 if f.name else 0)
        reserved2 = 1 + _bytes(f.trailing2) if f.trailing2 and _bytes(f.trailing2) <= _RESERVABLE_FIELD_LENGTH else 0
        f.trailing = _truncate_by_kind(f.trailing, f.trailing_kind, cols - base - 1 - reserved2)
    if not f.trailing2:
        return f
    base = len(position) + (_bytes(f.name) + 1 if f.name else 0)
    trailing_len = _bytes(f.trailing) + 1 if f.trailing else 0
    budget_with = cols - base - trailing_len - 1
    budget_without = cols - base - 1
    length = _bytes(f.trailing2)
    if length <= budget_with:
        return f
    if length <= budget_without and f.trailing:
        f.trailing = ''
        return f
    if budget_with >= 4:
        f.trailing2 = _truncate_by_kind(f.trailing2, f.trailing2_kind, budget_with)
        return f
    if budget_without >= 4 and f.trailing:
        f.trailing = ''
        f.trailing2 = _truncate_by_kind(f.trailing2, f.trailing2_kind, budget_without)
        return f
    f.trailing2 = ''
    return f


def _with_truncated_trailing3(f, position, cols):
    """withTruncatedTrailing3()。"""
    if cols <= 0 or not f.trailing3:
        return f
    base = len(position)
    for text in (f.name, f.trailing, f.trailing2):
        if text:
            base += _bytes(text) + 1
    budget = cols - base - 1
    if _bytes(f.trailing3) > budget:
        f.trailing3 = _middle_ellipsis_bytes(f.trailing3, budget) if budget >= 4 else ''
    return f


def _fit(f, position, cols):
    """fitModeLineFields(): (名前, trailing, trailing2)を出すか。"""
    def row_len(name, trailing, trailing2):
        n = len(position)
        for show, text in ((name, f.name), (trailing, f.trailing), (trailing2, f.trailing2)):
            if show and text:
                n += _bytes(text) + 1
        return n
    for choice in ((True, True, True), (True, False, True), (True, True, False), (True, False, False)):
        if cols <= 0 or row_len(*choice) <= cols:
            return choice
    return False, False, False


STANDOUT, PLAIN, BOLD, UNDERLINE = 'standout', 'plain', 'bold', 'underline'


class _Row:
    """モードラインの行を左から書く(moveCursor()は空白で埋める)。"""

    def __init__(self):
        self.segments, self.column = [], 0

    def put(self, text, attr=STANDOUT):
        if text:
            self.segments.append((text, attr))
            self.column += _width(text)

    def move(self, column):
        if column > self.column:
            self.put(' ' * (column - self.column), PLAIN)


def compose(fields, position, cols, name_underlined=False, trailing2_right_justified=False, position_plain=False):
    """欄→[(字, 属性)](端末のContentView::renderModeLine()・renderSplitModeLine())。欄は反転、欄の間の空白は
    反転しない。右端の印(usilic-1.5b)は太字。"""
    import copy
    raw = copy.copy(fields)
    row = _Row()
    if raw.split_layout_when_crowded:
        naive = len(position)
        for text in (raw.name, raw.trailing, raw.trailing2):
            if text:
                naive += _bytes(text) + 1
        if cols > 0 and naive > cols:  # renderSplitModeLine()
            center = position + (' ' + raw.trailing if raw.trailing else '')
            available = max(0, cols - _bytes(center) - 2)
            name_width, t2_width = _width(raw.name), _width(raw.trailing2)
            if name_width <= available // 2:
                left = name_width
                right = available - left
            elif t2_width <= available // 2:
                right = t2_width
                left = available - right
            else:
                left = available // 2
                right = available - left
            name = middle_ellipsis_by_width(raw.name, left)
            trailing2 = middle_ellipsis_by_width(raw.trailing2, right)
            row.put(name)
            row.move(left + 1)
            row.put(position, PLAIN if position_plain else STANDOUT)
            if raw.trailing:
                row.put(' ', PLAIN)
                row.put(raw.trailing)
            if trailing2:
                row.move(max(0, cols - _width(trailing2)))
                row.put(trailing2)
            return row.segments
    f = _with_truncated_trailing3(_with_truncated_fields(raw, position, cols), position, cols)
    show_name, show_trailing, show_trailing2 = _fit(f, position, cols)
    if show_name and f.name:
        row.put(f.name, UNDERLINE if name_underlined else STANDOUT)
        row.put(' ', PLAIN)
    row.put(position, PLAIN if position_plain else STANDOUT)
    if show_trailing and f.trailing:
        row.put(' ', PLAIN)
        trailing = f.trailing
        if f.fill_trailing_to_edge and not f.trailing2 and not f.trailing3:
            trailing += ' ' * max(0, (cols - 1) - row.column - _width(trailing))
        row.put(trailing, PLAIN if f.trailing_plain else STANDOUT)
    if show_trailing2 and f.trailing2:
        trailing2 = f.trailing2
        attr = PLAIN if f.trailing2_plain else STANDOUT
        if trailing2_right_justified:
            row.move(cols - _width(trailing2))
            row.put(trailing2, attr)
        else:
            row.put(' ', PLAIN)
            if f.fill_trailing_to_edge and not f.trailing3:
                trailing2 += ' ' * max(0, cols - row.column - _width(trailing2))
            row.put(trailing2, attr)
    if f.trailing3:
        trailing3 = f.trailing3
        if cols > 0 and row.column + 1 + _width(trailing3) <= cols:
            row.put(' ', PLAIN)
            if f.fill_trailing_to_edge:
                trailing3 += ' ' * max(0, (cols - 1) - row.column - _width(trailing3))
            row.put(trailing3)
    if f.right_watermark:
        watermark = ' ' + f.right_watermark + ' '
        if cols > 0 and row.column + len(watermark) <= cols:
            row.move(cols - len(watermark))
            row.put(watermark, BOLD)
    return row.segments


def prompt_tail(destination, destination_kind):
    """drawPromptLabel()の右端の戻り先: 16バイトまではそのまま、長ければ「...」+終わりの15バイト。MAN・LOCATE・DUは
    「MAN: 」等の頭を残して値の方を縮める。"""
    limit = TRAILING_PATH_TAIL_CHARS - 1
    if destination_kind == 'prefixed' and ': ' in destination:
        prefix, value = destination.split(': ', 1)
        prefix += ': '
        budget = max(1, limit + 3 - _bytes(prefix))
        if _bytes(value) <= budget:
            return prefix + value
        return prefix + '...' + _cut_bytes(value, _bytes(value) - max(1, budget - 3))
    if _bytes(destination) <= TRAILING_PATH_TAIL_CHARS:
        return destination
    return '...' + _cut_bytes(destination, _bytes(destination) - limit)


def prompt_segments(label, position, message, text, cursor, tail, cols, exit_destination=None, badge=False):
    """drawPromptLabel(): 名前(反転)・位置(反転)・尋ねる文・入力欄(反転しない)・右端の戻り先(反転)。
    (字と属性の並び, 入力欄の始まりの桁, 入力欄に出す字の始まり)。"""
    row = _Row()
    if exit_destination is not None:  # EXIT: 答えは2桁、その後ろに戻り先を右端まで反転
        row.put(label)
        row.put(' ', PLAIN)
        row.put(message, PLAIN)
        start = row.column
        row.put(text[:2].ljust(2), PLAIN)
        if exit_destination and row.column < cols:
            available = cols - row.column
            row.put(_cut_bytes(exit_destination, 0, available).ljust(available) if _bytes(exit_destination) > available
                    else exit_destination + ' ' * (available - _bytes(exit_destination)))
        else:
            row.put(' ' * max(0, cols - 1 - row.column))
        return row.segments, start, 0
    if label:
        row.put(label)
        row.put(' ', PLAIN)
    if position:
        row.put(position)
        row.put(' ', PLAIN)
    if message:
        row.put(message, PLAIN)
    start = row.column
    editable = max(1, cols - start - (TRAILING_PATH_TAIL_CHARS + 3 + 1))
    shift = 0  # LineEditor::adjustViewport(): カーソルが入力欄に収まるように頭を送る
    while shift < cursor and _width(text[shift:cursor]) > editable - 1:
        shift += 1
    visible, used = [], 0
    for ch in text[shift:]:
        w = utf8.display_width_of_codepoint(ord(ch))
        if used + w > editable:
            break
        visible.append(ch)
        used += w
    row.put(''.join(visible) + ' ' * (editable - used), PLAIN)
    column = (start + editable if badge else cols - 1 - _bytes(tail)) if tail else -1  # badge: 入力欄のすぐ後ろ
    if tail and column > start:
        row.move(column)
        row.put(tail)
    else:
        row.put(' ' * max(0, cols - 1 - row.column))
    return row.segments, start, shift


def segments_text(segments):
    return ''.join(text for text, _ in segments)
