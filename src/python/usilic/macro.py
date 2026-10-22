"""キーボードマクロ(C++のconfiguration.cppのMACRO節、application.cppのmacroItemsFromBytes()・
saveMacroToUsilicrc())。

MACRO節の行は「M-n  キー  K-n ... "文字列" ...」。キーを押すと、K-nの機能と、文字列を打ったことを順に行う。
^X(〜^X)で記録したバイト列は、制御キーで始まる所は既定のキーの並びに合うK-nに、印字できる字は"文字列"に
して、MACRO節の「--」の行の前に書き足す。
"""
import os
import shutil
import time

from .configuration import tokenize
from .key_bindings import DEFAULT_ENTRIES, DEFAULT_PREFIX_SPECS, DIGIT, parse_spec

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


def items_from_bytes(data):
    """macroItemsFromBytes(): 記録したバイト列→「K-n "文字列" ...」。"""
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
        i += 1
    if literal:
        items.append('"%s"' % literal)
    return ' '.join(items)


def save_macro(rc_path, key, items, label=''):
    """MACRO節の「--」の行の前に「M-n  キー  items」を書き足す(元はusilicrc.日時.bakへ)。番号を返す。"""
    with open(rc_path, encoding='utf-8', errors='surrogateescape') as f:
        lines = f.read().split('\n')
    start = next((i for i, l in enumerate(lines) if l.split('#', 1)[0].strip() == 'MACRO'), None)
    numbers = []
    if start is None:
        lines += ['MACRO']
        start = len(lines) - 1
    end = start + 1
    while end < len(lines) and (not lines[end] or lines[end][0] in ' \t#'):
        tokens = tokenize(lines[end])
        if tokens and tokens[0].startswith('M-') and tokens[0][2:].isdigit():
            numbers.append(int(tokens[0][2:]))
        end += 1
    marker = next((i for i in range(start + 1, end) if lines[i].strip().split('\t')[0].strip() == '--'), None)
    number = max(numbers or [0]) + 1
    line = '\tM-%d  %s\t%s' % (number, key, items) + ('\t\t# ' + label if label else '')
    if marker is None:
        lines[end:end] = [line, '\t--']
    else:
        lines[marker:marker] = [line]
    shutil.copy2(rc_path, '%s.%s.bak' % (rc_path, time.strftime('%Y%m%d%H%M%S')))
    tmp = rc_path + '.tmp.%d' % os.getpid()
    with open(tmp, 'w', encoding='utf-8', errors='surrogateescape') as f:
        f.write('\n'.join(lines))
    os.replace(tmp, rc_path)
    return number
