"""重ね打ち(man・groffの出力、C++のpipe_buffer.cppのwrapLine()の解釈)。

「X\\bX」は太字、「_\\bX」は下線(「_\\b_」は太字の下線の字)。結果は文字列と、(開始, 終わり, 属性)の並び。
"""


def decode(text):
    out, attrs = [], []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        attr = None
        # 「a\bb\bc…」と重ねた分をまとめて読む
        while i + 2 < n and text[i + 1] == '\b':
            nxt = text[i + 2]
            if ch == '_' and nxt != '_':
                attr = 'underline' if attr is None else attr
            elif ch == nxt:
                attr = 'bold'
            ch = nxt
            i += 2
        if ch == '\b':
            if out:
                out.pop()
                attrs.pop()
            i += 1
            continue
        out.append(ch)
        attrs.append(attr)
        i += 1
    return ''.join(out), _runs(attrs)


def _runs(attrs):
    runs, start = [], None
    for index, attr in enumerate(attrs + [None]):
        if start is not None and attr != attrs[start]:
            runs.append((start, index, attrs[start]))
            start = None
        if start is None and attr is not None:
            start = index
    return runs


def slice_runs(runs, start, end):
    """runsのうち[start, end)の分を、startを0とした位置で返す(折り返した1行ずつ用)。"""
    out = []
    for s, e, a in runs:
        s2, e2 = max(s, start), min(e, end)
        if s2 < e2:
            out.append((s2 - start, e2 - start, a))
    return out


def wrap_styled(text, runs, width, number):
    """属性付きの1行を幅で折り返し、ContentView.Lineの並びに(禁則で字が移っても順は同じなので、位置で切る)。"""
    from .content_view import Line, wrap_text
    pieces = wrap_text(text, width)
    lines, offset = [], 0
    for i, piece in enumerate(pieces):
        lines.append(Line(piece, number, ends_with_newline=(i == len(pieces) - 1),
                          attributes=slice_runs(runs, offset, offset + len(piece))))
        offset += len(piece)
    return lines
