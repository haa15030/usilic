"""PART(C++のcut_mode.cppのCutMode)と、一致した行の一覧(^O^S・^O^J^S、Emacsのoccur)。

PARTは、画面の行を拡張正規表現に一致した行で分け、各部分の頭の行(見出し)を一覧にする。一致する前の行が
あれば、それも最初の部分(見出しは「--」)。一致した行はgrep -E/-Fで探す(grep_mode.pyと同じ)。
"""
from .content_view import Line
from .grep_mode import REGEX, grep_line_indices


class Segment:
    def __init__(self, start, end, label):
        self.start, self.end, self.label = start, end, label


def split_lines(texts, pattern, syntax=REGEX):
    """CutMode::split(): 行の並びを一致した行で分けた[Segment]。grepが使えなければNone。"""
    found = grep_line_indices(texts, pattern, syntax)
    if found is None:
        return None
    starts = sorted(set(found))
    if not starts:  # 一致が無ければ全体で1つ(見出しは空、行が無ければ無し)
        return [Segment(0, len(texts), '')] if texts else []
    segments = [Segment(0, starts[0], '--')] if starts[0] > 0 else []  # 一致の前の行は「--」
    for i, start in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else len(texts)
        segments.append(Segment(start, end, texts[start].rstrip(' ')))  # 行末の空白(タブの分)は取る
    return segments


class PartMode:
    """分けた結果(見出しの一覧のview・部分のview)。"""

    def __init__(self, toc_view, content_view):
        self.toc_view = toc_view
        self.content_view = content_view
        self.pattern = ''
        self.source_lines = []
        self.segments = []
        self.byte_count = 0
        self.source_name = ''   # 分けた物(FILEならパス)
        self.origin = 'PART'    # 分けた元(C++のcutOrigin_): 'PART'(ls)・'FILE'・'MAN'・'LOCATE'・'DU'・'LOG'
        self.origin_path = ''   # 一覧のモードラインの最後の欄(MANのページ・LOCATEの検索語・DUのパス・LOGのファイル)
        self.syntax = REGEX

    def split(self, lines, pattern, byte_count, source_name, syntax=REGEX, origin='PART', origin_path=''):
        segments = split_lines([l.text for l in lines], pattern, syntax)
        if segments is None:
            return False
        self.pattern, self.source_lines, self.segments = pattern, list(lines), segments
        self.byte_count, self.source_name = byte_count, source_name
        self.syntax, self.origin, self.origin_path = syntax, origin, origin_path
        self.toc_view.set_lines([Line(s.label, i + 1) for i, s in enumerate(segments)])
        self.toc_view.cursor_index = self.toc_view.top_index = 0
        return True

    def open_selected(self):
        i = self.toc_view.cursor_index
        if not 0 <= i < len(self.segments):
            return False
        s = self.segments[i]
        self.content_view.set_lines(self.source_lines[s.start:s.end])
        self.content_view.cursor_index = self.content_view.top_index = 0
        return True


def occur_lines(lines, pattern, syntax):
    """一致した行の(画面の行の番号, 行)の並び。grepが使えなければNone。"""
    found = grep_line_indices([l.text for l in lines], pattern, syntax)
    if found is None:
        return None
    return [(i, lines[i]) for i in found if i < len(lines)]
