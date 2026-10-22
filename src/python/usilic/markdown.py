"""Markdownの文字装飾(C++のmarkdown_to_help_markup.cppと、その結果を展開するexpandHelpMarkup()のCUIの分)。

.md・.markdownのfileは、見出し(#は太字+下線、##は太字、###以下は字下げした太字)・強調(*・_、**で太字、*で斜体)・
`コード`と[リンク](URL)の下線・引用(字下げした斜体)・リスト(2桁ごとに字下げ)・表(列をそろえ、見出しの行は太字)・
コードのフェンス(4桁の字下げ)・区切り線にして見せる。C++と同じく、いったん<b>・<i>・<u>・<indentNN>の印に組み直し、
それを字の属性に展開する。
"""
import re

from . import utf8
from .content_view import Line


def is_markdown_file(path):
    return path.lower().endswith(('.md', '.markdown'))


def _match_heading(line):
    m = re.match(r'(#{1,6})( .*|)$', line)
    if not m or not m.group(2):
        return None  # 「#foo」・「#」だけは見出しではない
    return len(m.group(1)), m.group(2).strip(_WS)


def _is_rule(trimmed):
    if not trimmed or trimmed[0] not in '-*_':
        return False
    marker = trimmed[0]
    if any(c not in (marker, ' ', '\t') for c in trimmed):
        return False
    return trimmed.count(marker) >= 3


def _fence(trimmed):
    m = re.match(r'(`{3,}|~{3,})', trimmed)
    return m.group(1)[0] if m else None


def _list_item(line):
    m = re.match(r'([-*+] |\d+[.)] )(.*)$', line)
    return (m.group(1), m.group(2)) if m and m.group(2) is not None else None


def _blockquote(line):
    if not line.startswith('>'):
        return None
    depth, i = 0, 0
    while i < len(line) and line[i] == '>':
        depth += 1
        i += 1
        if i < len(line) and line[i] == ' ':
            i += 1
    return depth, line[i:]


def _indent_open(width):
    return '<indent%02d>' % max(0, min(99, width))


_INDENT_CLOSE = '</indent>'
_WS = ' \t\r\n\f\v'  # ASCIIの空白だけ(全角の空白は字のまま、C++のisspace())
_PUNCT = set('!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~')


def _flanking(text, start, end, char):
    def classify(pos):  # 0=空白相当、1=句読点、2=文字
        if pos < 0 or pos >= len(text) or text[pos] in ' \t':
            return 0
        return 1 if text[pos] in _PUNCT else 2
    before, after = classify(start - 1), classify(end)
    left = after != 0 and (after != 1 or before in (0, 1))
    right = before != 0 and (before != 1 or after in (0, 1))
    if char == '*':
        return left, right
    return left and (not right or before == 1), right and (not left or after == 1)  # '_'は単語の中では強調にならない


def _tokenize(text):
    """(区切りか, 文字列, 区切りの字, 長さ, 開けるか, 閉じられるか)の並び。"""
    pieces, plain, i, n = [], '', 0, len(text)

    def flush():
        nonlocal plain
        if plain:
            pieces.append([False, plain, '', 0, False, False])
            plain = ''
    while i < n:
        c = text[i]
        if c == '\\' and i + 1 < n:
            plain += text[i + 1]
            i += 2
            continue
        if c == '`':
            close = text.find('`', i + 1)
            if close >= 0:
                flush()
                pieces.append([False, '<u>' + text[i + 1:close] + '</u>', '', 0, False, False])
                i = close + 1
                continue
        if c == '!' and text.startswith('[', i + 1):
            alt_end = text.find(']', i + 2)
            if alt_end >= 0 and text.startswith('(', alt_end + 1):
                url_end = text.find(')', alt_end + 2)
                if url_end >= 0:
                    flush()
                    pieces.append([False, '[image] <u>%s</u> (%s)' % (text[i + 2:alt_end], text[alt_end + 2:url_end]),
                                   '', 0, False, False])
                    i = url_end + 1
                    continue
        if c == '[':
            text_end = text.find(']', i + 1)
            if text_end >= 0 and text.startswith('(', text_end + 1):
                url_end = text.find(')', text_end + 2)
                if url_end >= 0:
                    flush()
                    pieces.append([False, '<u>%s</u> (%s)' % (text[i + 1:text_end], text[text_end + 2:url_end]),
                                   '', 0, False, False])
                    i = url_end + 1
                    continue
        if c in '*_':
            flush()
            start = i
            while i < n and text[i] == c:
                i += 1
            can_open, can_close = _flanking(text, start, i, c)
            pieces.append([True, '', c, i - start, can_open, can_close])
            continue
        plain += c
        i += 1
    flush()
    return pieces


def _resolve_emphasis(pieces):
    i = 0
    while i < len(pieces):
        p = pieces[i]
        if p[0] and p[5] and p[3] > 0:
            found = -1
            for j in range(i - 1, -1, -1):
                q = pieces[j]
                if not q[0] or q[2] != p[2] or not q[4] or q[3] <= 0:
                    continue
                both = (q[4] and q[5]) or (p[4] and p[5])
                rule_of_3 = both and (q[3] + p[3]) % 3 == 0 and not (q[3] % 3 == 0 and p[3] % 3 == 0)
                if not rule_of_3:
                    found = j
                    break
            if found >= 0:
                strength = 2 if pieces[found][3] >= 2 and p[3] >= 2 else 1
                inner = ''.join(q[2] * q[3] if q[0] else q[1] for q in pieces[found + 1:i])
                tag = 'b' if strength == 2 else 'i'
                pieces[found][3] -= strength
                p[3] -= strength
                del pieces[found + 1:i]
                i = found + 1
                pieces.insert(i, [False, '<%s>%s</%s>' % (tag, inner, tag), '', 0, False, False])
                i += 1
                continue
        i += 1


def render_inline(text):
    pieces = _tokenize(text)
    _resolve_emphasis(pieces)
    return ''.join(p[2] * p[3] if p[0] else p[1] for p in pieces)


def _is_table_delimiter(trimmed):
    return bool(trimmed) and trimmed[0] == '|' and '-' in trimmed and all(c in '|:- \t' for c in trimmed)


def _split_table_row(trimmed):
    cells, cur, code, i = [], '', False, 1 if trimmed.startswith('|') else 0
    while i < len(trimmed):
        c = trimmed[i]
        if c == '`':
            code = not code
        if c == '\\' and i + 1 < len(trimmed) and trimmed[i + 1] == '|':
            cur += '|'
            i += 2
            continue
        if c == '|' and not code:
            cells.append(cur.strip(' \t'))
            cur = ''
        else:
            cur += c
        i += 1
    if cur.rstrip(_WS):
        cells.append(cur.strip(' \t'))
    return cells


def _markup_width(text):
    width, i = 0, 0
    while i < len(text):
        if text[i] == '<':
            close = text.find('>', i)
            if close >= 0:
                if text.startswith('<lt>', i):
                    width += 1
                i = close + 1
                continue
        width += utf8.display_width_of_codepoint(ord(text[i]))
        i += 1
    return width


def markdown_to_markup(source):
    """Markdownを<b>・<i>・<u>・<indentNN>…</indent>・<lt>の印へ(C++のmarkdownToHelpMarkup())。"""
    text = source.replace('<', '<lt>')
    lines = text.split('\n')
    if text.endswith('\n'):
        lines.pop()
    out, in_fence, fence_marker, paragraph = [], False, '`', []

    def flush_paragraph():
        if not paragraph:
            return
        joined = ''
        for k, line in enumerate(paragraph):
            hard = line.endswith('  ')
            joined += line.rstrip(_WS)
            if k + 1 < len(paragraph):
                joined += '\n' if hard else ' '
        out.append(render_inline(joined))
        paragraph.clear()
    li = 0
    while li < len(lines):
        raw = lines[li]
        trimmed = raw.lstrip(' \t')
        lead = len(raw) - len(trimmed)
        marker = _fence(trimmed.rstrip(_WS))
        if marker:
            flush_paragraph()
            if not in_fence:
                in_fence, fence_marker = True, marker
            elif marker == fence_marker:
                in_fence = False
            li += 1
            continue  # フェンスの行自体(```や言語名)は出さない
        if in_fence:
            out.append(_indent_open(4) + raw + _INDENT_CLOSE)
            li += 1
            continue
        if not raw.strip(_WS):
            flush_paragraph()
            out.append('')
            li += 1
            continue
        heading = _match_heading(trimmed)
        if heading:
            flush_paragraph()
            level, body = heading
            rendered = render_inline(body)
            if level == 1:
                out.append('<u><b>' + rendered + '</b></u>')  # 最上位は太字+下線
            elif level == 2:
                out.append('<b>' + rendered + '</b>')
            else:
                out.append(_indent_open((level - 2) * 2) + '<b>' + rendered + '</b>' + _INDENT_CLOSE)
            li += 1
            continue
        if _is_rule(trimmed.rstrip(_WS)):
            flush_paragraph()
            out.append('-' * 60)
            li += 1
            continue
        quote = _blockquote(trimmed)
        if quote:
            flush_paragraph()
            depth, body = quote
            out.append(_indent_open(depth * 2) + '<i>' + render_inline(body.rstrip(_WS)) + '</i>' + _INDENT_CLOSE)
            li += 1
            continue
        if trimmed.startswith('|') and li + 1 < len(lines) and _is_table_delimiter(lines[li + 1].strip(' \t')):
            flush_paragraph()
            rows, end = [], li
            while end < len(lines):
                t = lines[end].strip(' \t')
                if not t or t[0] != '|':
                    break
                if end != li + 1:  # 区切りの行
                    rows.append([render_inline(cell) for cell in _split_table_row(t)])
                end += 1
            widths = []
            for row in rows:
                for c, cell in enumerate(row):
                    if len(widths) <= c:
                        widths.append(0)
                    widths[c] = max(widths[c], _markup_width(cell))
            for r, row in enumerate(rows):
                line = ''
                for c, cell in enumerate(row):
                    line += ('<b>' + cell + '</b>') if r == 0 else cell
                    if c + 1 < len(row):
                        line += ' ' * (widths[c] - _markup_width(cell) + 2)
                out.append(_indent_open(2) + line + _INDENT_CLOSE)
                if r == 0:
                    rule, used = '', 2
                    for c, w in enumerate(widths):
                        if c + 1 == len(widths):
                            w = max(min(w, 80 - used), _markup_width(rows[0][c]) if c < len(rows[0]) else 1)
                        rule += '-' * w + ('  ' if c + 1 < len(widths) else '')
                        used += w + 2
                    out.append(_indent_open(2) + rule + _INDENT_CLOSE)
            li = end
            continue
        item = _list_item(trimmed)
        if item:
            flush_paragraph()
            mark, body = item
            out.append(_indent_open(lead // 2 * 2) + mark + render_inline(body.rstrip(_WS)) + _INDENT_CLOSE)
            li += 1
            continue
        paragraph.append(raw)
        li += 1
    flush_paragraph()
    return '\n'.join(out)


_TAGS = {'b': 'bold', 'i': 'italic', 'u': 'underline'}


def markup_lines(markup):
    """印の文字列を(字, 属性のラン, 字下げの桁)の行に展開する(expandHelpMarkup())。装飾は行をまたいでも続く。"""
    out, state, indent = [], {'bold': 0, 'italic': 0, 'underline': 0}, 0
    for line in markup.split('\n'):
        text, attrs, i, line_indent = [], [], 0, indent
        while i < len(line):
            if line[i] == '<':
                m = re.match(r'<(/?)(b|i|u)>|<indent(\d+)>|</indent>|<lt>', line[i:])
                if m:
                    token = m.group(0)
                    if token == '<lt>':
                        text.append('<')
                        attrs.append(tuple(k for k, v in state.items() if v))
                    elif m.group(2):
                        name = _TAGS[m.group(2)]
                        state[name] = max(0, state[name] + (-1 if m.group(1) else 1))
                    elif m.group(3) is not None:
                        indent = int(m.group(3))
                        if not text:
                            line_indent = indent
                    else:
                        indent = 0
                    i += len(token)
                    continue
            text.append(line[i])
            attrs.append(tuple(k for k, v in state.items() if v))
            i += 1
        runs, open_run = [], {}  # 属性→続いているランの番号
        for pos, names in enumerate(attrs):
            for name in names:
                k = open_run.get(name)
                if k is not None and runs[k][1] == pos:
                    runs[k] = (runs[k][0], pos + 1, name)
                else:
                    open_run[name] = len(runs)
                    runs.append((pos, pos + 1, name))
        out.append((''.join(text), runs, line_indent))
    return out


def render_markdown(source, width):
    """Markdownのfileを画面の行に(字下げの行は折り返した続きも同じだけ字下げ)。"""
    from .epub_mode import _wrap_with_indent
    lines = []
    for number, (text, runs, indent) in enumerate(markup_lines(markdown_to_markup(source)), 1):
        lines.extend(_wrap_with_indent(text, runs, width, indent, indent, number))
    return lines
