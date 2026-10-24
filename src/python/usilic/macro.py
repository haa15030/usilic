"""キーボードマクロ(C++のconfiguration.cppのMACRO節、application.cppのmacroItemsFromBytes()・
saveMacroToUsilicrc())。

MACRO節の行は「M-n  キー  K-n ... "文字列" ...」。キーを押すと、K-nの機能と、文字列を打ったことを順に行う。
^X(〜^X)で記録したバイト列は、制御キーで始まる所は既定のキーの並びに合うK-nに、印字できる字は"文字列"に
して、MACRO節の「--」の行の前に書き足す。
"""
import os

from .configuration import tokenize
from .key_bindings import DEFAULT_ENTRIES, DEFAULT_PREFIX_SPECS, DIGIT, key_label, parse_spec

# usilicrcにMACRO節が無い時の既定(C++の組み込みと同じ6つ)
DEFAULT_MACRO_LINES = [
    'M-1  ;\tK-20 K-4 K-28', 'M-2  :\tK-20 K-3 K-28', 'M-3  [\tK-35 K-4', 'M-4  ]\tK-35 K-3',
    'M-5  e\tK-67', 'M-6  l\tK-86 K-53 "^usilic-1.5b:" K-28',
]


class Macro:
    def __init__(self, number, key_spec, items):
        self.number, self.key_spec, self.items = number, key_spec, items  # items: [('k', n)|('s', 文字列)]


def parse_macro_lines(lines):
    """MACRO節の行→[Macro](「--」の行等は飛ばす)。誤りは(macros, errors)のerrorsへ。"""
    macros, errors = [], []
    for line in lines:
        tokens = tokenize(line)
        if len(tokens) < 3 or not tokens[0].startswith('M-') or not tokens[0][2:].isdigit():
            continue
        items, ok = [], True
        raw = _raw_items(line)
        for token in raw[2:]:
            if token.startswith('"'):
                items.append(('s', token[1:-1] if token.endswith('"') and len(token) > 1 else token[1:]))
            elif token.startswith('K-') and token[2:].isdigit():
                items.append(('k', int(token[2:])))
            else:
                errors.append('MACRO: %s: not a K-number or a "string": %s' % (tokens[0], token))
                ok = False
                break
        if ok and items:
            macros.append(Macro(int(tokens[0][2:]), tokens[1], items))
    return macros, errors


def _raw_items(line):
    """""を残したまま語に分ける(#以降は注釈)。"""
    out, i = [], 0
    while i < len(line):
        while i < len(line) and line[i] in ' \t':
            i += 1
        if i >= len(line) or line[i] == '#':
            break
        start = i
        if line[i] == '"':
            close = line.find('"', i + 1)
            i = len(line) if close < 0 else close + 1
        else:
            while i < len(line) and line[i] not in ' \t':
                i += 1
        out.append(line[start:i])
    return out


def macro_key_bytes(macro, prefixes):
    seq, _ = parse_spec(macro.key_spec, prefixes)
    return seq


def default_sequences():
    """既定のキーの並び(K-番号→バイト列、Nの物は除く)。記録の変換はC++と同じく既定の割り当てで。"""
    prefixes = []
    for spec in DEFAULT_PREFIX_SPECS:
        seq, _ = parse_spec(spec, prefixes)
        prefixes.append(seq or [])
    out = {}
    for number, spec in DEFAULT_ENTRIES:
        seq, _ = parse_spec(spec, prefixes)
        if seq and DIGIT not in seq:
            out[number] = seq
    return out


def items_from_bytes(data, failed=None):
    """macroItemsFromBytes(): 記録したバイト列→「K-n "文字列" ...」。書けない字があればfailed(リスト)へTrueを。"""
    sequences = default_sequences()
    items, literal, i = [], '', 0
    while i < len(data):
        head = data[i]
        best, length = 0, 0
        if head < 0x20 or head in (0x7F, 0x80):
            for number, seq in sequences.items():
                if len(seq) > length and data[i:i + len(seq)] == seq:
                    best, length = number, len(seq)
        if best:
            if literal:
                items.append('"%s"' % literal)
                literal = ''
            items.append('K-%d' % best)
            i += length
            continue
        if head >= 0x20 and head not in (ord('"'), ord('#'), 0x7F):
            literal += chr(head) if head < 0x80 else bytes([head]).decode('latin-1')
        elif failed is not None:
            failed.append(True)
        i += 1
    if literal:
        items.append('"%s"' % literal)
    return ' '.join(items)


SPACE_MARK = '\u2423'  # ␣


def notation(items):
    """macroNotation(): 要素の並び→キーの表記(K-nは既定のキー「^G^P」等、"文字列"は中身、空白は␣)。"""
    sequences = default_sequences()
    out, p = '', 0
    while p < len(items):
        while p < len(items) and items[p] in ' \t':
            p += 1
        if p >= len(items):
            break
        start = p
        if items[p] == '"':
            close = items.find('"', p + 1)
            p = len(items) if close < 0 else close + 1
            body = items[start + 1:len(items) if close < 0 else close]
            out += body.replace(' ', SPACE_MARK)
            continue
        while p < len(items) and items[p] not in ' \t':
            p += 1
        word = items[start:p]
        number = int(word[2:]) if word.startswith('K-') and word[2:].isdigit() else None
        out += key_label(sequences[number]) if number in sequences else word
    return out


def items_from_notation(text):
    """macroItemsFromNotation(): キーの表記→要素の並び(「^X」は制御キー、「"…"」はそのまま)。読めなければNone。"""
    items, data, failed = [], [], []

    def flush():
        part = items_from_bytes(data, failed)
        if part:
            items.append(part)
        data.clear()
    p = 0
    while p < len(text):
        c = text[p]
        if c == '"':
            close = text.find('"', p + 1)
            if close < 0:
                return None
            flush()
            items.append(text[p:close + 1])
            p = close + 1
        elif c == '^' and p + 1 < len(text):
            n = text[p + 1]
            p += 2
            if n == SPACE_MARK:
                data.append(0x80)
            elif n == '?':
                data.append(0x7F)
            elif 'a' <= n <= 'z':
                data.append(ord(n) - ord('a') + 1)
            elif '@' <= n <= '_':
                data.append(ord(n) - ord('@'))
            else:
                return None
        elif c in ' \t':
            p += 1
        elif c == SPACE_MARK:
            data.append(0x20)
            p += 1
        else:
            data.extend(c.encode('utf-8'))
            p += 1
    flush()
    return None if failed else ' '.join(items)


class MacroLine:
    """MACRO節の「M-n キー 要素... # コメント」の行と「--」の行(number=0、C++のusilicrc::MacroLine)。"""
    def __init__(self, number=0, key='', items='', comment=''):
        self.number, self.key, self.items, self.comment = number, key, items, comment


def _is_keyword_line(line):
    return bool(line) and line[0] not in '# \t'


def _section(lines, name='MACRO'):
    """節の見出しの行と、節の終わり(次の見出し)。見出しが無ければ(None, None)。"""
    for i, line in enumerate(lines):
        if _is_keyword_line(line) and line.split(None, 1)[0] == name:
            end = i + 1
            while end < len(lines) and not _is_keyword_line(lines[end]):
                end += 1
            return i, end
    return None, None


def _split(text):
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    return lines


def _join(lines):
    return ''.join(line + '\n' for line in lines)


def _parse_macro_line(raw):
    words = raw.split()
    if not words:
        return None

    def rest(after):
        """after語目の後ろ: 「"」の外の「#」より前が要素、後ろが注釈。"""
        p = 0
        for _ in range(after):
            while p < len(raw) and raw[p] in ' \t':
                p += 1
            while p < len(raw) and raw[p] not in ' \t':
                p += 1
        quoted, hash_at = False, -1
        for i in range(p, len(raw)):
            if raw[i] == '"':
                quoted = not quoted
            elif raw[i] == '#' and not quoted:
                hash_at = i
                break
        body = raw[p:] if hash_at < 0 else raw[p:hash_at]
        return body.strip(' \t'), (raw[hash_at + 1:].strip(' \t') if hash_at >= 0 else '')
    if words[0] == '--':
        return MacroLine(0, '', *rest(1))
    if len(words[0]) < 3 or not words[0].startswith('M-') or not words[0][2:].isdigit() or len(words) < 2:
        return None
    return MacroLine(int(words[0][2:]), words[1], *rest(2))


def macro_lines(text):
    lines = _split(text)
    start, end = _section(lines)
    if start is None:
        return []
    return [m for m in (_parse_macro_line(line) for line in lines[start + 1:end]) if m is not None]


def replace_macro_marker(text, replacement):
    """「--」の行をreplacementの行で置き換える(無ければ節の最後の行の後ろへ、節も無ければ末尾へ)。"""
    lines = _split(text)
    start, end = _section(lines)
    if start is None:
        if lines and lines[-1]:
            lines.append('')
        lines.append('MACRO')
        start, end = len(lines) - 1, len(lines)
    for i in range(start + 1, end):
        if lines[i].lstrip(' \t').startswith('--'):
            lines[i:i + 1] = replacement
            return _join(lines)
    last = start
    for i in range(start + 1, end):
        if lines[i]:
            last = i
    lines[last + 1:last + 1] = replacement
    return _join(lines)


def next_macro_number(text):
    lines = _split(text)
    start, end = _section(lines)
    numbers = [0]
    if start is not None:
        for line in lines[start + 1:end]:
            word = line.lstrip(' \t').split(None, 1)[0] if line.strip() else ''
            if word.startswith('M-'):
                digits = ''
                for c in word[2:]:
                    if not c.isdigit():
                        break
                    digits += c
                numbers.append(int(digits or 0))
    return max(numbers) + 1


def replace_macro_line(text, number, line):
    lines = _split(text)
    start, end = _section(lines)
    if start is None:
        return text
    for i in range(start + 1, end):
        parsed = _parse_macro_line(lines[i])
        if parsed is not None and parsed.number == number:
            lines[i] = line
            return _join(lines)
    return text


def read_text(path):
    try:
        with open(path, encoding='utf-8', errors='surrogateescape') as f:
            return f.read()
    except OSError:
        return ''


def write_atomically(path, text):
    tmp = path + '.tmp'
    try:
        with open(tmp, 'w', encoding='utf-8', errors='surrogateescape') as f:
            f.write(text)
        os.replace(tmp, path)
    except OSError:
        return False
    return True


def set_macro_marker(path, replacement):
    return write_atomically(path, replace_macro_marker(read_text(path), replacement))


def set_macro_line(path, number, line):
    text = read_text(path)
    replaced = replace_macro_line(text, number, line)
    return replaced != text and write_atomically(path, replaced)
