"""HELPの原稿・roffからEPUB(C++のmarkup_to_roff.cppのconvertMarkupToMsRoffSource()・help_text.cppの
joinMarkupLinesAtBreaks()・help_to_epub.cppのconvertMsRoffToHelpEpub()を写したもの)。

usilic -n(原稿→roff、HELP.1)・usilic -e(HELP.1か原稿→HELP.epub)と、^HのHELP(HELP.1から~/.usilic/HELP.epubを
作る、application.pyのhelp_epub_path())が使う。出力はC++版と同じになるようにする(zipは無圧縮で自前に書く)。
"""
import base64
import re
import struct
import time
import zlib

from . import utf8

NBSP = '\u00a0'
COLON = '：'


# --- 原稿→roff(usilic -n) ---

def join_markup_lines_at_breaks(source):
    """joinMarkupLinesAtBreaks(): <br />のある原稿は、<br />の所だけで改行し、他の改行は前の行につなぐ。"""
    breaks = ('<br />', '<br/>', '<br>')
    if not any(tag in source for tag in breaks):
        return source
    text, i = [], 0
    while i < len(source):
        length = 0
        for tag in breaks:
            if source.startswith(tag, i):
                length = len(tag)
                break
        if not length:
            text.append(source[i])
            i += 1
            continue
        i += length
        j = i
        while j < len(source) and source[j] in ' \t':
            j += 1
        if j < len(source) and source[j] == '\n':
            i = j + 1
        text.append('\x02\n')
    lines = ''.join(text).split('\n')
    out, joining = [], False

    def is_ascii_text(c):
        return '\x20' < c < '\x7f'
    for k, line in enumerate(lines):
        forced = line.endswith('\x02')
        if forced:
            line = line[:-1]
        if joining:
            line = line.lstrip(' \t')
            if out and out[-1] and line and is_ascii_text(out[-1][-1]) and is_ascii_text(line[0]):
                out.append(' ')
        out.append(line)
        if k + 1 == len(lines):
            break
        here, nxt = line.strip(' \t'), lines[k + 1].strip(' \t')
        if nxt.endswith('\x02'):
            nxt = nxt[:-1]
        keep = forced or not here or not nxt or nxt.startswith('<indent') or here.endswith('</indent>')
        if keep:
            out.append('\n')
            joining = False
        else:
            joining = True
    return ''.join(out)


def convert_markup_to_legacy_help_text(source):
    """convertMarkupToLegacyHelpText()(usilic -m): <b>は重ね打ち、<u>・<i>は下線の重ね打ち、<indentN>は
    U+FEFFをN+1個(</indent>は1個)。"""
    markup = join_markup_lines_at_breaks(source)
    out, i, n = [], 0, len(markup)
    while i < n:
        length = _indent_open_length(markup, i)
        if length:
            out.append('\ufeff' * (int(markup[i + 7:i + length - 1]) + 1))
            i += length
            if i < n and markup[i] == '\n':
                i += 1
            continue
        if markup.startswith('</indent>', i):
            if out and out[-1].endswith('\n'):
                out[-1] = out[-1][:-1]
            out.append('\ufeff')
            i += 9
            continue
        tag = None
        for open_tag, close_tag in (('<b>', '</b>'), ('<u>', '</u>'), ('<i>', '</i>')):
            if markup.startswith(open_tag, i):
                tag = (open_tag, close_tag)
                break
        if tag:
            close_pos = markup.find(tag[1], i + 3)
            if close_pos < 0:
                out.append(tag[0])
                i += 3
                continue
            content = markup[i + 3:close_pos]
            out.append(''.join(ch + '\b' + ch if tag[0] == '<b>' else '_\b' + ch for ch in content))
            i = close_pos + 4
            continue
        out.append(markup[i])
        i += 1
    return ''.join(out)


MISCOUNTED_KANA = frozenset([
    0x304C, 0x304E, 0x3050, 0x3052, 0x3054, 0x3056, 0x3058, 0x305A, 0x305C, 0x305E, 0x3060, 0x3062,
    0x3065, 0x3067, 0x3069, 0x3070, 0x3071, 0x3073, 0x3074, 0x3076, 0x3077, 0x3079, 0x307A, 0x307C,
    0x307D, 0x3094, 0x309E, 0x30AC, 0x30AE, 0x30B0, 0x30B2, 0x30B4, 0x30B6, 0x30B8, 0x30BA, 0x30BC,
    0x30BE, 0x30C0, 0x30C2, 0x30C5, 0x30C7, 0x30C9, 0x30D0, 0x30D1, 0x30D3, 0x30D4, 0x30D6, 0x30D7,
    0x30D9, 0x30DA, 0x30DC, 0x30DD, 0x30F4, 0x30F7, 0x30F8, 0x30F9, 0x30FA, 0x30FE])


def wrap_miscounted_kana(text, font):
    """groffが1桁に数える濁点・半濁点の仮名だけJPM(太字はJPG)で出し、`font`へ戻す。"""
    out = []
    for ch in text:
        if ord(ch) in MISCOUNTED_KANA:
            out.append(('\\f[JPG]' if font == 'B' else '\\f[JPM]') + ch + '\\f[' + font + ']')
        else:
            out.append(ch)
    return ''.join(out)


def extract_help_notes(markup, notes):
    """extractHelpNotes(): 「≡注釈の文…∠」を本文から外して1行ずつnotesへ(中の<indentN>は本文に残す)。"""
    body, pos = [], 0
    while True:
        start = markup.find('≡', pos)
        end = -1 if start < 0 else markup.find('∠', start + 1)
        if end < 0:
            break
        body.append(markup[pos:start])
        inner = markup[start + 1:end]
        tags, text, k = [], [], 0
        while k < len(inner):
            length = 0
            if inner.startswith('</indent>', k):
                length = 9
            elif inner.startswith('<indent', k):
                g = inner.find('>', k)
                if g >= 0:
                    length = g + 1 - k
            if length:
                tags.append(inner[k:k + length])
                k += length
            else:
                text.append(inner[k])
                k += 1
        lines = ''.join(text).split('\n')
        while lines and not lines[-1].strip(' \t'):
            lines.pop()
        notes.append(lines)
        body.append(''.join(tags))
        pos = end + 1
        joined = ''.join(body)
        line_start = not joined or joined.endswith('\n')
        if line_start and not tags and pos < len(markup) and markup[pos] == '\n':
            pos += 1
    return ''.join(body) + markup[pos:]


ROFF_PREAMBLE = (
    '.\\" usilic -n: -m(usilic自身の表示)と同じ見た目にするための前置き\n'
    '.nr PO 0\n.po 0\n.nr HM 0\n.nr FM 0\n.nr PL 100000v\n.pl 100000v\n.ds CH\n'
    '.nh\n.ad l\n'
    '.ie r usilic-help .ds usilic-tab \\[uE000]\n.el .ds usilic-tab\n'
    '.ds usilic-note\n'
    '.ds usilic-i\n.ds usilic-ie\n'
    '.ta T 8n\n'
    ".char - \\[u002D]\n.char ' \\[u0027]\n.char ^ \\[u005E]\n.char ~ \\[u007E]\n.char ` \\[u0060]\n"
    '.class [CJKprepunct] , : ; > } \\[u3001] \\[u3002] \\[uFF0C] \\[uFF0E] \\[u30FB] \\[uFF1A] '
    '\\[uFF1B] \\[uFF1F] \\[uFF01] \\[uFF09] \\[u3015] \\[uFF3D] \\[uFF5D] \\[u300D] \\[u300F] '
    '\\[u3011] \\[u3041] \\[u3043] \\[u3045] \\[u3047] \\[u3049] \\[u3063] \\[u3083] \\[u3085] '
    '\\[u3087] \\[u30FC] \\[u30A1] \\[u30A3] \\[u30A5] \\[u30A7] \\[u30A9] \\[u30C3] \\[u30E3] '
    '\\[u30E5] \\[u30E7]\n'
    '.class [CJKpostpunct] \\[uFF08] \\[u3014] \\[uFF3B] \\[uFF5B] \\[u300C] \\[u300E] \\[u3010]\n'
    '.class [CJKnormal] \\[u3041]-\\[u3096] \\[u30A0]-\\[u30FF] \\[u4E00]-\\[u9FFF]\n'
    ".cflags 128 \\C'[CJKprepunct]'\n.cflags 256 \\C'[CJKpostpunct]'\n.cflags 512 \\C'[CJKnormal]'\n")


def _indent_open_length(markup, at):
    """<indentN>(Nは1〜2桁)ならタグの長さ、でなければ0。"""
    if not markup.startswith('<indent', at):
        return 0
    end = at + 7
    while end < len(markup) and end < at + 9 and markup[end].isdigit() and markup[end] < '\x80':
        end += 1
    if end == at + 7 or end >= len(markup) or markup[end] != '>':
        return 0
    return end + 1 - at


def convert_markup_to_ms_roff_source(source):
    """convertMarkupToMsRoffSource(): HELPの原稿(<b>・<u>・<i>・<indentN>、⊥・‡・≡…∠)をroffへ。"""
    notes = []
    tabbed = join_markup_lines_at_breaks(source).replace('⊥', '\t')
    markup = extract_help_notes(tabbed, notes)
    out = []
    at_line_start = True
    current_indent = 0
    n = len(markup)

    def ensure_fresh_line():
        if out and not out[-1].endswith('\n'):
            out.append('\n')

    def mid_line_tag(at, line_started):
        if not line_started:
            return 0
        if markup.startswith('</indent>', at):
            after = at + 9
            if after >= n or markup[after] == '\n':
                return 0
            return 9
        return _indent_open_length(markup, at)

    japanese = any(0x3041 <= ord(c) <= 0x30FF or 0x4E00 <= ord(c) <= 0x9FFF for c in markup)
    date = time.strftime('%Y\\[u002D]%m\\[u002D]%d', time.localtime())
    out.append('.TH USILIC 1 "' + date + '" "usilic 1.5b" "' +
               (wrap_miscounted_kana('ユーザーコマンド', 'R') if japanese else 'User Commands') + '"\n')
    out.append(ROFF_PREAMBLE)
    i = 0
    while i < n:
        indent_length = _indent_open_length(markup, i)
        skip = mid_line_tag(i, not at_line_start)
        if skip:
            i += skip
            continue
        if indent_length:
            value = int(markup[i + 7:i + indent_length - 1])
            ensure_fresh_line()
            out.append('.in %dn\n' % value)
            current_indent = value
            i += indent_length
            if i < n and markup[i] == '\n':
                i += 1
            at_line_start = True
            continue
        if markup.startswith('</indent>', i):
            ensure_fresh_line()
            out.append('.in 0\n')
            current_indent = 0
            i += 9
            if i < n and markup[i] == '\n':
                i += 1
            at_line_start = True
            continue
        if markup[i] == '\n':
            if at_line_start:
                ensure_fresh_line()
                out.append('.sp\n')
            else:
                out.append('\n.br\n')
            i += 1
            at_line_start = True
            continue
        if at_line_start:
            continuation = None
            k = i
            while k < n and markup[k] != '\n':
                length = mid_line_tag(k, k > i)
                if length:
                    continuation = 0 if markup[k + 1] == '/' else int(re.match(r'\d+', markup[k + 7:]).group(0))
                    k += length
                else:
                    k += 1
            if continuation is not None and continuation != current_indent:
                ensure_fresh_line()
                out.append('.in %dn\n.ti %dn\n' % (continuation, current_indent))
                current_indent = continuation
        if at_line_start and markup[i] in '.\'':
            out.append('\\&')
        first_on_line = at_line_start
        at_line_start = False
        if markup[i] == '\t':
            rest = i + 1
            while rest < n and markup[rest] in ' \t':
                rest += 1
            after_tags = rest
            while after_tags < n and markup.startswith('</indent>', after_tags):
                after_tags += 9
                while after_tags < n and markup[after_tags] in ' \t':
                    after_tags += 1
            at_line_end = after_tags >= n or markup[after_tags] == '\n'
            if first_on_line or at_line_end:
                out.append('\\*[usilic-tab]')
                if at_line_end:
                    i = rest
                    continue
        if markup[i] == '‡':
            d = i + 1
            while d < n and '0' <= markup[d] <= '9':
                d += 1
            out.append('\\*[usilic-note' + (' ' + markup[i + 1:d] if d > i + 1 else '') + ']')
            i = d
            continue
        tag = None
        for open_tag, close_tag in (('<b>', '</b>'), ('<u>', '</u>'), ('<i>', '</i>')):
            if markup.startswith(open_tag, i):
                tag = (open_tag, close_tag)
                break
        if tag:
            content_start = i + 3
            close_pos = markup.find(tag[1], content_start)
            if close_pos < 0:
                out.append(tag[0])
                i = content_start
                continue
            span = markup[content_start:close_pos].replace('\\', '\\e')
            if tag[0] == '<i>':
                out.append('\\*[usilic-i]' + (wrap_miscounted_kana(span, 'R') if japanese else span) +
                           '\\*[usilic-ie]')
            else:
                bold = tag[0] == '<b>'
                out.append(('\\fB' if bold else '\\fI') +
                           (wrap_miscounted_kana(span, 'B' if bold else 'I') if japanese else span) + '\\fR')
            i = close_pos + 4
            continue
        ch = markup[i]
        if ch == '\\':
            out.append('\\e')
        elif japanese and 0x3000 <= ord(ch) <= 0x3FFF:
            out.append(wrap_miscounted_kana(ch, 'R'))
        else:
            out.append(ch)
        i += 1
    ensure_fresh_line()
    for note in notes:
        out.append('.\\" usilic-note-begin\n')
        for line in note:
            out.append('.\\" usilic-note-line ' + line + '\n')
    out.append('.br\n.if !d an-footer .pl \\n(nlu+1v\n')
    return ''.join(out)


# --- roff→EPUB(usilic -e) ---

def _strip_nbsp_and_space(s):
    return s.strip(' ' + NBSP)


def _collapse_spaces(s):
    return re.sub(r'\s+', ' ', s).strip()


def _escape_html(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def _number(arg):
    m = re.match(r'\d+', arg or '0')
    return int(m.group(0)) if m else 0


# 切れ端: ('text', 字) / ('font', 名前, \f[...]の形か) / ('literal', 字)
def _tokenize(s):
    pieces, text = [], []

    def flush():
        if text:
            pieces.append(('text', ''.join(text), False))
            text.clear()
    i, n = 0, len(s)
    while i < n:
        if s[i] != '\\' or i + 1 >= n:
            text.append(s[i])
            i += 1
            continue
        c = s[i + 1]
        if c == 'f' and i + 2 < n and s[i + 2] == '[':
            close = s.find(']', i + 3)
            if close > i + 3:
                name = s[i + 3:close]
                if all('A' <= ch <= 'Z' for ch in name):
                    flush()
                    pieces.append(('font', name, True))
                    i = close + 1
                    continue
        if c == 'f' and i + 2 < n and s[i + 2] in 'BIRP':
            flush()
            pieces.append(('font', s[i + 2], False))
            i += 3
            continue
        if s.startswith('\\*[usilic-note', i) and s.find(']', i) >= 0:
            close = s.find(']', i)
            arg = s[i + 14:close].lstrip(' ')
            flush()
            pieces.append(('literal', '‡' + arg, False))
            i = close + 1
            continue
        if s.startswith('\\*[usilic-i]', i) or s.startswith('\\*[usilic-ie]', i):
            end = s.startswith('\\*[usilic-ie]', i)
            flush()
            pieces.append(('font', 'PATHEND' if end else 'PATH', True))
            i += 13 if end else 12
            continue
        if s.startswith('\\*[usilic-tab]', i):
            flush()
            pieces.append(('literal', '', False))
            i += 14
            continue
        if c in 'e&-':
            flush()
            pieces.append(('literal', '\\' if c == 'e' else '-' if c == '-' else '', False))
            i += 2
            continue
        if c == '[' and i + 2 < n and s[i + 2] == 'u':
            m = re.match(r'[0-9A-Fa-f]{4,5}\]', s[i + 3:i + 9])
            if m:
                digits = len(m.group(0)) - 1
                flush()
                pieces.append(('literal', chr(int(s[i + 3:i + 3 + digits], 16)), False))
                i += 3 + digits + 1
                continue
        if c == '\n':
            text.append(s[i])
            i += 1
            continue
        flush()
        pieces.append(('literal', s[i + 1], False))
        i += 2
    flush()
    return pieces


def _plain(s):
    return ''.join(p[1] for p in _tokenize(s) if p[0] != 'font')


def _inline_html_pieces(pieces, col0):
    out = []
    font, prev, opened = 'R', 'R', ''
    col = [col0]

    def emit_text(t):
        buf = []
        for ch in t:
            if ch == '\t':
                k = 8 - col[0] % 8
                buf.append(' ' * k)
                col[0] += k
                continue
            buf.append(ch)
            col[0] += utf8.display_width_of_codepoint(ord(ch))
        return _escape_html(''.join(buf))
    for kind, text, bracket in pieces:
        if kind != 'font':
            out.append(emit_text(text))
            continue
        f = text
        if f == 'JPM':
            prev = font
            continue
        if f == 'R' and bracket:
            continue
        if f == 'PATHEND':
            f = 'R'
        if f == 'P':
            f = prev
        if opened:
            out.append('</' + opened + '>')
            opened = ''
        font = f
        opened = {'B': 'b', 'I': 'u', 'PATH': 'i'}.get(f, '')
        if opened:
            out.append('<' + opened + '>')
    if opened:
        out.append('</' + opened + '>')
    joined = ''.join(out)
    lead = len(joined) - len(joined.lstrip(' '))
    rest = re.sub(r' {2,}', lambda m: NBSP * len(m.group(0)), joined[lead:])
    return NBSP * lead + rest


def _inline_html(s, col0):
    return _inline_html_pieces(_tokenize(s), col0)


def _inline_text(s):
    return _inline_html_pieces([('text', s, False)], 0)


def _markup_line_html(s):
    out, text, i = [], [], 0
    tags = ('<b>', '</b>', '<i>', '</i>', '<u>', '</u>')
    while i < len(s):
        for t in tags:
            if s.startswith(t, i):
                out.append(_escape_html(''.join(text)))
                text.clear()
                out.append(t)
                i += len(t)
                break
        else:
            text.append(s[i])
            i += 1
    out.append(_escape_html(''.join(text)))
    return ''.join(out)


def _looks_preformatted(raw):
    if raw[:1].isspace():
        return True
    if '\t' in raw:
        return True
    prev_non_space = False
    for i, ch in enumerate(raw):
        if prev_non_space and raw.startswith('  ', i):
            return True
        prev_non_space = not ch.isspace()
    return False


class _Block:
    def __init__(self, kind):
        self.kind = kind  # 'p' / 'space' / 'h'
        self.level = 0
        self.indent = 0
        self.first = 0
        self.title = ''
        self.html = ''
        self.raw = ''


def _xhtml(title, body, lang):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE html>\n'
            '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="' +
            lang + '" lang="' + lang + '">\n<head><meta charset="UTF-8"/><title>' + _escape_html(title) +
            '</title><link rel="stylesheet" type="text/css" href="../Style/style.css"/></head>\n<body>\n' + body +
            '</body>\n</html>\n')


def _format_g(v):
    return '%g' % v


class _ZipWriter:
    """無圧縮のzip(C++のZipWriterと同じ並び)。"""

    def __init__(self):
        t = time.localtime()
        self.dos_time = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
        self.dos_date = ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
        self.out = bytearray()
        self.entries = []

    def header(self, name, crc, size):
        return struct.pack('<HHHHHIIIHH', 20, 0x0800, 0, self.dos_time, self.dos_date, crc, size, size,
                           len(name), 0)

    def add(self, name, data):
        if isinstance(data, str):
            data = data.encode('utf-8')
        name_bytes = name.encode('utf-8')
        crc = zlib.crc32(data) & 0xFFFFFFFF
        self.entries.append((name_bytes, crc, len(data), len(self.out)))
        self.out += struct.pack('<I', 0x04034b50) + self.header(name_bytes, crc, len(data)) + name_bytes + data

    def finish(self):
        central = len(self.out)
        for name, crc, size, offset in self.entries:
            self.out += (struct.pack('<IH', 0x02014b50, 20) + self.header(name, crc, size) +
                         struct.pack('<HHHII', 0, 0, 0, 0, offset) + name)
        size = len(self.out) - central
        self.out += struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(self.entries), len(self.entries), size, central,
                                0)
        return bytes(self.out)


def _half_width(t):
    out = []
    for ch in t:
        c = ord(ch)
        if 0xFF10 <= c <= 0xFF3F:
            out.append(chr(c - 0xFF10 + 0x30))
        elif 0xFF40 <= c <= 0xFF5A:
            out.append(chr(c - 0xFF40 + 0x60))
        else:
            out.append(ch)
    return ''.join(out)


def _name_like(t):
    letter = False
    for c in t:
        if c.isascii() and c.isalpha():
            letter = True
        elif not (c.isascii() and c.isdigit()) and c not in '-_':
            return False
    return letter


def convert_ms_roff_to_help_epub(roff, usilicrc_text='', notes_text='', cover=b''):
    """convertMsRoffToHelpEpub(): usilic -nのroffを横書きのEPUB3に。(EPUBのバイト列, 要約, 注釈の無い名前)。"""
    lines = roff.split('\n')
    cover_png = cover
    if not cover_png:
        encoded = ''.join(l[22:] for l in lines if l.startswith('.\\" usilic-cover-line '))
        cover_png = base64.b64decode(encoded + '=' * (-len(encoded) % 4)) if encoded else b''
    english = False
    for l in lines:
        if l.startswith('.TH '):
            english = '"User Commands"' in l
            break
    lang = 'en' if english else 'ja'
    notes_title = 'Notes' if english else '注釈'
    toc_title = 'Contents' if english else '目次'

    start = 0
    while start < len(lines) and (not lines[start] or lines[start][0] == '.'):
        start += 1
    ind, ti = 0, None
    blocks = []
    last_name = ''
    tab = '\\*[usilic-tab]'
    manual_notes = []
    for l in lines[start:]:
        if l == '.\\" usilic-note-begin':
            manual_notes.append([])
            continue
        if l.startswith('.\\" usilic-note-line '):
            if manual_notes:
                manual_notes[-1].append(l[21:])
            continue
        if l and l[0] == '.':
            words = l.split()
            req = words[0] if words else ''
            arg = words[1] if len(words) > 1 else ''
            if req == '.in':
                ind = _number(arg or '0')
            elif req == '.ti':
                ti = _number(arg or '0')
            elif req == '.sp':
                blocks.append(_Block('space'))
            continue
        if not l.strip():
            blocks.append(_Block('space'))
            continue
        first = ti if ti is not None else ind
        tab_head = l.endswith(tab) and not l.startswith(tab)
        starts_indented = l[0] in ' \t' or l.startswith('\\*')
        if tab_head or (first == 0 and ind == 0 and not starts_indented):
            p = _plain(l).strip()
            h = _Block('h')
            colon = p.find(COLON)
            if colon >= 0:
                name = _collapse_spaces(p[:colon])
                desc = p[colon + 1:]
                if not name:
                    if blocks and blocks[-1].kind == 'p' and COLON in blocks[-1].raw:
                        raw = blocks[-1].raw
                        name = _collapse_spaces(raw[:raw.find(COLON)])
                        blocks.pop()
                    else:
                        name = last_name
                h.title = name + COLON + desc.strip()
                last_name = name
                body = _inline_html(l.replace(tab, ''), 0)
                body = re.sub(NBSP + '*' + COLON + NBSP + '*', COLON, body)
                body = _strip_nbsp_and_space(body)
                if body.startswith(COLON):
                    body = '<b>' + _escape_html(name) + '</b>' + body
                h.html = body
            else:
                h.title = p
                h.html = _strip_nbsp_and_space(_inline_html(l.replace(tab, ''), 0))
            h.level = 2 if first == 0 else 3
            blocks.append(h)
        else:
            b = _Block('p')
            b.indent = ind
            b.first = first
            b.html = _inline_html(l, first)
            b.raw = _plain(l)
            blocks.append(b)
        ti = None

    chapters = []  # [title, blocks, subs, file]
    for b in blocks:
        if b.kind == 'h' and b.level == 2:
            chapters.append([b.title, [b], [], ''])
        elif chapters:
            chapters[-1][1].append(b)
            if b.kind == 'h':
                chapters[-1][2].append(b.title)

    rc_comments, rc_defaults = {}, {}
    rc_lines = [l for l in lines if l.startswith('.\\" usilic-rc-line') and (len(l) == 18 or l[18] == ' ')]
    rc_text = '\n'.join(l[19:] for l in rc_lines) if rc_lines else usilicrc_text
    item = section = ''
    for l in rc_text.split('\n'):
        m = re.match(r'[A-Z][A-Z-]*', l)
        heading = bool(m) and not l[m.end():].strip()
        if heading:
            item = section = l.strip()
            rc_comments[item] = []
        elif item and l.startswith('#'):
            c = l[1:]
            if c.startswith(' '):
                c = c[1:]
            rc_comments[item].append(c)
        else:
            item = ''
            if section:
                rc_defaults.setdefault(section, []).append(l)
    for key, v in rc_defaults.items():
        while v and not v[0].strip():
            v.pop(0)
        while v and not v[-1].strip():
            v.pop()

    notes_all = notes_text
    for l in lines:
        if l.startswith('.\\" usilic-notes-line '):
            notes_all += '\n' + l[22:]
        elif l == '.\\" usilic-notes-line':
            notes_all += '\n'
    written = {}
    key = None
    for l in notes_all.split('\n'):
        if l.startswith('#'):
            continue
        if l.startswith('@'):
            key = l[1:].strip()
            written[key] = []
        elif key is not None:
            written[key].append(l)

    def opted_out(name):
        return name in written and not any(l.strip() for l in written[name])

    def has_note(name):
        if opted_out(name):
            return False
        return bool(rc_comments.get(name)) or bool(written.get(name))

    missing = []
    note_order, note_ids, backrefs = [], {}, {}
    counters = {'ref': 0, 'manual': 0}

    def manual_key(k):
        return '\x01' + str(k)

    def note_id(name):
        if name not in note_ids:
            note_ids[name] = 'n' + str(len(note_ids) + 1)
            note_order.append(name)
        return note_ids[name]

    def new_ref(nid, fname):
        counters['ref'] += 1
        rid = 'r' + str(counters['ref'])
        backrefs.setdefault(nid, []).append((fname, rid))
        return rid

    def link_refs(h, fname):
        lead = '<i>usilicrc</i>'
        out, pos = [], 0
        while True:
            m = h.find(lead, pos)
            if m < 0:
                break
            i = m + len(lead)
            while i < len(h) and h[i].isspace():
                i += 1
            ok = h.startswith('の', i)
            if ok:
                i += 1
                while i < len(h) and h[i].isspace():
                    i += 1
            group1_end = i
            bold = ok and h.startswith('<b>', i)
            if bold:
                i += 3
            item_start = j = i
            if ok and j < len(h) and 'A' <= h[j] <= 'Z':
                while j < len(h) and ('A' <= h[j] <= 'Z' or h[j] == '-'):
                    j += 1
            while j > item_start and h[j - 1] == '-':
                j -= 1
            if not ok or j - item_start < 2:
                out.append(h[pos:m + len(lead)])
                pos = m + len(lead)
                continue
            name = h[item_start:j]
            close_bold = h.startswith('</b>', j)
            end = j + (4 if close_bold else 0)
            if not has_note(name):
                out.append(h[pos:end])
                pos = end
                continue
            nid = note_id(name)
            rid = new_ref(nid, fname)
            out.append(h[pos:group1_end])
            out.append(('<b>' if bold else '') + name + ('</b>' if close_bold else '') + '<a id="' + rid +
                       '" href="notes.xhtml#' + nid + '" epub:type="noteref">' + nid[1:] + '</a>')
            pos = end
        out.append(h[pos:])
        text = ''.join(out)
        linked, pos = [], 0
        while True:
            m = text.find('<u>', pos)
            dagger = text.find('‡', pos)
            if dagger >= 0 and (m < 0 or dagger < m):
                d = dagger + 1
                while d < len(text) and text[d].isdigit():
                    d += 1
                if d > dagger + 1:
                    k = int(text[dagger + 1:d])
                else:
                    counters['manual'] += 1
                    k = counters['manual']
                linked.append(text[pos:dagger])
                pos = d
                if k < 1 or k > len(manual_notes):
                    continue
                nid = note_id(manual_key(k))
                rid = new_ref(nid, fname)
                linked.append('<a id="' + rid + '" href="notes.xhtml#' + nid + '" epub:type="noteref">' + nid[1:] +
                              '</a>')
                continue
            if m < 0:
                break
            close = text.find('</u>', m + 3)
            if close < 0:
                break
            shown = text[m + 3:close]
            name = _half_width(shown)
            end = close + 4
            in_quote = m >= 1 and text[m - 1] == '「'
            after = end
            while after < len(text) and text[after].isspace():
                after += 1
            legend = text.startswith(':', after)
            a_open, a_close = text.rfind('<a ', 0, m + 3), text.rfind('</a>', 0, m + 4)
            in_link = a_open >= 0 and (a_close < 0 or a_close < a_open)
            candidate = bool(name) and '<' not in name and not in_quote and not legend and not in_link
            if candidate and _name_like(name) and not has_note(name) and not opted_out(name) and name not in missing:
                missing.append(name)
            if not candidate or not has_note(name):
                linked.append(text[pos:end])
                pos = end
                continue
            nid = note_id(name)
            rid = new_ref(nid, fname)
            linked.append(text[pos:m])
            linked.append('<u>' + shown + '</u><a id="' + rid + '" href="notes.xhtml#' + nid +
                          '" epub:type="noteref">' + nid[1:] + '</a>')
            pos = end
        linked.append(text[pos:])
        return ''.join(linked)

    classes = {}
    files = []
    paragraph_count = 0
    for n, chapter in enumerate(chapters):
        fname = 'ch%02d.xhtml' % (n + 1)
        body = []
        sub = 0
        cb = chapter[1]

        def list_item(b):
            return b.first < b.indent and (COLON in b.raw or ' : ' in b.raw or '）: ' in b.raw)
        pre = [b.kind == 'p' and (_looks_preformatted(b.raw) or list_item(b)) for b in cb]
        k = 0
        while k < len(cb):
            def key_line(j, k=k):
                return (cb[j].kind == 'p' and (COLON in cb[j].raw or ' : ' in cb[j].raw) and
                        cb[j].indent == cb[k].indent and cb[j].first == cb[k].first)
            if not key_line(k):
                k += 1
                continue
            end = k
            any_pre = False
            while end < len(cb) and key_line(end):
                any_pre = any_pre or pre[end]
                end += 1
            if any_pre and end - k > 1:
                for j in range(k, end):
                    pre[j] = True
            k = end
        for bi, b in enumerate(cb):
            if b.kind == 'space':
                body.append('<p class="blank"><br/></p>\n')
                continue
            if b.kind == 'h':
                tag = 'h%d' % b.level
                if b.level == 3:
                    hid = 's%d' % sub
                    sub += 1
                else:
                    hid = 'top'
                body.append('<' + tag + ' id="' + hid + '">' + b.html + '</' + tag + '>\n')
                continue
            paragraph_count += 1
            cls = 'i%dt%d' % (b.indent, b.first)
            classes[(b.indent, b.first)] = cls
            if pre[bi]:
                cls += ' pre'
            h = link_refs(b.html, fname)
            body.append('<p class="' + cls + '">' + (NBSP if not _strip_nbsp_and_space(h) else h) + '</p>\n')
        files.append(['Text/' + fname, _xhtml(chapter[0], ''.join(body), lang)])
    for k in range(1, len(manual_notes) + 1):
        if manual_key(k) not in note_ids:
            note_id(manual_key(k))

    renumber = {}

    def renumbered(nid):
        if nid not in renumber:
            renumber[nid] = 'n' + str(len(renumber) + 1)
        return renumber[nid]
    href = 'href="notes.xhtml#'
    for f in files:
        x = f[1]
        fixed, pos = [], 0
        while True:
            at = x.find(href, pos)
            if at < 0:
                break
            id_start = at + len(href)
            id_end = x.find('"', id_start)
            to = renumbered(x[id_start:id_end])
            gt = x.find('>', id_end)
            lt = x.find('<', gt)
            fixed.append(x[pos:id_start] + to + x[id_end:gt + 1] + to[1:])
            pos = lt
        f[1] = ''.join(fixed) + x[pos:]
    for name in note_order:
        renumbered(note_ids[name])
    backrefs = {renumber[k]: v for k, v in backrefs.items()}
    for name in note_ids:
        note_ids[name] = renumber[note_ids[name]]
    note_order.sort(key=lambda name: int(note_ids[name][1:]))

    asides = []
    for name in note_order:
        nid = note_ids[name]
        refs = backrefs.get(nid, [])
        back = ' '.join('<a role="doc-backlink" href="' + f + '#' + r + '">' + (nid[1:] if k == 0 else '↩%d' % (k + 1))
                        + '</a>' for k, (f, r) in enumerate(refs))
        inner = []
        if name[0] == '\x01':
            note = manual_notes[int(name[1:]) - 1]
            head = ''
            for k, line in enumerate(note):
                t = _markup_line_html(line)
                if k == 0:
                    head = t
                else:
                    inner.append('<p>' + (NBSP if not t.strip() else t) + '</p>\n')
            asides.append('<aside id="' + nid + '" epub:type="footnote">\n<p class="nh">' + back + head + '</p>\n' +
                          ''.join(inner) + '</aside>\n')
            continue
        if name in written:
            paragraph = []

            def flush_paragraph():
                t = '<br/>'.join(_escape_html(p) for p in paragraph)
                if t.strip():
                    inner.append('<p>' + t + '</p>\n')
                paragraph.clear()
            body = list(written[name])
            while body and not body[0].strip():
                body.pop(0)
            while body and not body[-1].strip():
                body.pop()
            for l in body:
                if not l.strip():
                    flush_paragraph()
                else:
                    paragraph.append(l)
            flush_paragraph()
        else:
            rc = list(rc_comments.get(name, []))
            while rc and not rc[-1].strip():
                rc.pop()
            for l in rc:
                t = _inline_text(l)
                inner.append('<p class="rc">' + (t or NBSP) + '</p>\n')
            default = rc_defaults.get(name, [])
            if default:
                inner.append('<p class="rc">' + NBSP + '</p>\n')
                inner.append('<p class="dh">' + ('Default' if english else '既定値') + '</p>\n')
                for l in default:
                    t = _inline_text(l)
                    inner.append('<p class="rc">' + (t or NBSP) + '</p>\n')
        if name not in rc_comments:
            title = ' <b>' + _escape_html(name) + '</b>'
        elif english:
            title = ' <b>' + _escape_html(name) + '</b> in <i>usilicrc</i>'
        else:
            title = ' <i>usilicrc</i> の <b>' + _escape_html(name) + '</b>'
        asides.append('<aside id="' + nid + '" epub:type="footnote">\n<p class="nh">' + back + title + '</p>\n' +
                      ''.join(inner) + '</aside>\n')
    if asides:
        chapters.append([notes_title, [], [], 'notes'])
        files.append(['Text/notes.xhtml', _xhtml(notes_title, '<h2 id="top">' + notes_title + '</h2>\n' +
                                                 ''.join(asides), lang)])

    css = ('html { writing-mode: horizontal-tb; }\n'
           'body { font-family: serif; line-height: 1.7; margin: 0 1em; }\n'
           'h2 { font-size: 140%; font-family: sans-serif; margin: 1em 0 0.5em; }\n'
           'h2.invisible { display: none; }\n'
           'h3 { font-size: 115%; font-family: sans-serif; margin: 1em 0 0.3em; padding-left: 2em; }\n'
           'p { margin: 0; }\n'
           'p.nh { margin-top: 1.2em; }\n'
           'p.dh { margin-top: 0.5em; padding-left: 1em; font-family: sans-serif; }\n'
           'aside { margin: 0 0 1em; }\n'
           'p.rc { font-family: "Osaka-Mono", "MS Gothic", "Noto Sans Mono CJK JP", monospace; padding-left: 1em; }\n'
           'p.pre { font-family: "Osaka-Mono", "MS Gothic", "Noto Sans Mono CJK JP", monospace; }\n')
    for (indent, first), cls in sorted(classes.items()):
        css += ('p.' + cls + ' { padding-left: ' + _format_g(indent * 0.5) + 'em; text-indent: ' +
                _format_g((first - indent) * 0.5) + 'em; }\n')
    files.append(['Style/style.css', css])

    def chapter_file(ch, n):
        return ch[3] or 'ch%02d' % (n + 1)
    nav_items = []
    cover_name = 'Cover' if english else '表紙'
    if cover_png:
        files.append(['Images/usilic.png', cover_png])
        files.append(['Text/cover.xhtml', _xhtml(cover_name, '<h2 id="cover" class="invisible">' + cover_name +
                                                 '</h2>\n<p><img alt="usilic" src="../Images/usilic.png"/></p>\n',
                                                 lang)])
        nav_items.append('<li><a href="cover.xhtml">' + cover_name + '</a></li>\n')
    for n, ch in enumerate(chapters):
        item = '<li><a href="' + chapter_file(ch, n) + '.xhtml#top">' + _escape_html(ch[0]) + '</a>'
        if ch[2]:
            item += '\n<ol>'
            for k, sub_title in enumerate(ch[2]):
                item += '<li><a href="' + chapter_file(ch, n) + '.xhtml#s%d">' % k + _escape_html(sub_title) + \
                        '</a></li>'
            item += '</ol>\n'
        nav_items.append(item + '</li>\n')
    files.append(['Text/nav.xhtml', _xhtml(toc_title, '<nav epub:type="toc" id="toc">\n<h2>' + toc_title +
                                           '</h2>\n<ol>\n' + ''.join(nav_items) + '</ol>\n</nav>\n', lang)])

    date, version = '', 'usilic'
    for l in lines:
        if not l.startswith('.TH '):
            continue
        quoted = re.findall(r'"([^"]*)"', l)
        if len(quoted) >= 2:
            date, version = _plain(quoted[0]), _plain(quoted[1])
        break
    modified = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    manifest = ('<item id="nav" href="Text/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>\n'
                '<item id="css" href="Style/style.css" media-type="text/css"/>')
    spine = ''
    if cover_png:
        manifest += ('\n<item id="cover-image" href="Images/usilic.png" media-type="image/png" '
                     'properties="cover-image"/>'
                     '\n<item id="cover" href="Text/cover.xhtml" media-type="application/xhtml+xml"/>')
        spine += '<itemref idref="cover"/>\n'
    for n, ch in enumerate(chapters):
        f = chapter_file(ch, n)
        manifest += '\n<item id="' + f + '" href="Text/' + f + '.xhtml" media-type="application/xhtml+xml"/>'
        spine += ('\n' if n else '') + '<itemref idref="' + f + '"/>'
    opf = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id" xml:lang="' + lang +
           '">\n<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
           '<dc:identifier id="id">urn:uuid:b9e22b63-e1df-51a5-aced-609cf3c8f37c</dc:identifier>\n'
           '<dc:title>HELP</dc:title>\n'
           '<dc:creator>誌璃空ユウ</dc:creator>\n'
           '<dc:language>' + lang + '</dc:language>\n'
           '<dc:description>' + _escape_html(version + ' ' + date) + '(usilic -e)</dc:description>\n'
           '<meta property="dcterms:modified">' + modified + '</meta>\n'
           '</metadata>\n<manifest>\n' + manifest + '\n</manifest>\n'
           '<spine page-progression-direction="ltr">\n' + spine + '\n</spine>\n</package>\n')
    files.append(['content.opf', opf])

    z = _ZipWriter()
    z.add('mimetype', 'application/epub+zip')
    z.add('META-INF/container.xml',
          '<?xml version="1.0" encoding="UTF-8"?>\n'
          '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
          '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
          '</rootfiles>\n</container>\n')
    for name, data in files:
        z.add('OEBPS/' + name, data)
    summary = '%d chapters, %d paragraphs, %d notes (%d refs)' % (len(chapters), paragraph_count, len(note_order),
                                                                  counters['ref'])
    return z.finish(), summary, missing


def is_english_roff(roff):
    """.THに"User Commands"(usilic -nが英語の原稿に付ける)があれば英語。"""
    for l in roff.split('\n'):
        if l.startswith('.TH '):
            return '"User Commands"' in l
    return False
