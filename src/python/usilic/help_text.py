"""HELPとKEY(C++のapplication.cppのreloadHelpText()・renderHelpRoff()・keySourceLines()・
keyBytesFromHelpLabel()・isKeyAssignmentLabel())。

HELP(^H)はC++版と同じくHELP.epubをEPUBで開く(application.pyの方)。ここは平文のHELP: KEY(^K)の元と、
KEYのhで開くHELP。HELP.1(英語はHELP_en.1)をgroffで画面の幅に組み、無ければ組んであるHELP(HELP.en)を読む。
"""
import os

from . import man_mode

FULL_COLON = '：'
MARKERS = ('⁠', '', '﻿')  # 組んだHELPの目印(行頭がタブの行等)。表示はしない


def strip_markers(text):
    for marker in MARKERS:
        text = text.replace(marker, '')
    return text


def library_directories():
    """C++のinstallLibDirectory_の候補(~/.local/lib/usilic/、/usr/local/lib/usilic/)。"""
    return [os.path.join(os.path.expanduser('~'), '.local', 'lib', 'usilic'), '/usr/local/lib/usilic']


def find_in_library(name):
    for directory in library_directories():
        path = os.path.join(directory, name)
        if os.path.isfile(path):
            return path
    return None


def help_epub_path(japanese):
    return find_in_library('HELP.epub' if japanese else 'HELP.en.epub')


def plain_help_lines(width, japanese, configuration):
    """平文のHELPの行と大きさ。(行, バイト数, パス)かNone。"""
    roff = find_in_library('HELP.1' if japanese else 'HELP_en.1')
    if roff:
        argv = configuration.command_for('Groff') + ['-rLL=%dn' % max(width, 20), '-rusilic-help=1', roff]
        found = man_mode.run_to_lines(argv, width, transform=strip_markers)
        if found and found[0]:
            return found[0], found[1], roff
    plain = find_in_library('HELP' if japanese else 'HELP.en')
    if plain:
        with open(plain, 'rb') as f:
            data = f.read()
        return man_mode.styled_lines(strip_markers(data.decode('utf-8', 'replace')), width), len(data), plain
    return None


def key_source_lines(lines):
    """keySourceLines(): 「：」のある行(元の行ごと、折り返した続きも)だけ。"""
    kept, keeping, line_start = [], False, True
    for line in lines:
        if line_start:
            keeping = FULL_COLON in line.text
        line_start = line.ends_with_newline
        if keeping:
            kept.append(line)
    return kept


def key_bytes_from_label(label):
    """keyBytesFromHelpLabel(): 「^J^H」→[0x0A, 0x08]、「␣」→空白、「^␣」→0x80。キーでなければ[]。"""
    label = strip_markers(label.split(FULL_COLON, 1)[0]).strip(' \t')
    if not label:
        return []
    slash = label.find('/')
    if 0 < slash < len(label) - 1:  # 「^N/^P」は最初のキー
        label = label[:slash]
    out, i = [], 0
    while i < len(label):
        c = label[i]
        if c == '^' and i + 1 < len(label):
            d = label[i + 1]
            if d == '␣' or d == '@':
                out.append(0x80)
            elif d == '?':
                out.append(0x7F)
            elif '@' <= d <= '_':
                out.append(ord(d) - 0x40)
            elif 'a' <= d <= 'z':
                out.append(ord(d) - ord('a') + 1)
            else:
                return []
            i += 2
            continue
        if c == '␣':
            out.append(0x20)
        elif ord(c) >= 0x80:
            return []
        else:
            out.append(ord(c))
        i += 1
    return out


def is_key_assignment_label(label):
    """isKeyAssignmentLabel(): 「CP/MV」「HELP」のような見出しはキーではない。"""
    text = strip_markers(label.split(FULL_COLON, 1)[0]).strip(' \t')
    if not text:
        return False
    i = 0
    while i < len(text):
        if text[i] == '^':
            i += 2
            continue
        if text[i] in ' \t':
            return False
        if text[i].isupper() and i + 1 < len(text) and text[i + 1].isalpha():
            return False
        i += 1
    return bool(key_bytes_from_label(label))
