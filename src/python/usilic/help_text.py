"""HELPとKEY(C++のapplication.cppのreloadHelpText()・renderHelpRoff()・keySourceLines()・
keyBytesFromHelpLabel()・isKeyAssignmentLabel())。

HELP(^H)はC++版と同じくHELP.epubをEPUBで開く(application.pyの方)。ここは平文のHELP: KEY(^K)の元と、
KEYのhで開くHELP。HELP.1(英語はHELP_en.1)をgroffで画面の幅に組み、無ければ組んであるHELP(HELP.en)を読む。
"""
import os

from . import man_mode, overstrike, utf8

FULL_COLON = '：'
MARKERS = ('⁠', '', '﻿')  # 組んだHELPの目印(行頭がタブの行等)。表示はしない


def strip_markers(text):
    for marker in MARKERS:
        text = text.replace(marker, '')
    return text


def markers_to_tabs(text):
    """renderHelpRoff(): groffが出した行頭・行末のタブの印(U+E000)をタブに戻す。行頭の印は1桁を占め、
    その後ろに元のタブが続くので、タブ(8桁)と残りの空白に。行末の印はタブに。"""
    out = []
    for line in text.split('\n'):
        at = line.find('\ue000')
        if at < 0:
            out.append(line)
            continue
        before, after = line[:at], line[at + 1:]
        if not before.strip(' '):
            body = after.lstrip(' ')
            columns = len(before) + 1 + len(after) - len(body)
            out.append('\t' + ' ' * max(0, columns - 8) + body)
        else:
            out.append(before.rstrip(' ') + '\t')
    return '\n'.join(out)


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
    """平文のHELPの行と大きさ。(行, バイト数, パス, 元の行(重ね打ちを解いた字、タブはそのまま))かNone。"""
    roff = find_in_library('HELP.1' if japanese else 'HELP_en.1')
    if roff:
        argv = configuration.command_for('Groff') + ['-rLL=%dn' % max(width, 20), '-rusilic-help=1', roff]
        rewrite = configuration.key_bindings.rewrite_help_text   # KEY節で変えたキーをHELPの表記にも
        shown = []

        def transform(text):
            shown.append(strip_markers(rewrite(markers_to_tabs(text))))
            return shown[0]
        found = man_mode.run_to_lines(argv, width, transform=transform)
        if found and found[0]:
            return found[0], found[1], roff, raw_lines(shown[0])
    plain = find_in_library('HELP' if japanese else 'HELP.en')
    if plain:
        with open(plain, 'rb') as f:
            data = f.read()
        text = strip_markers(configuration.key_bindings.rewrite_help_text(data.decode('utf-8', 'replace')))
        lines = man_mode.styled_lines(text, width)
        return lines, man_mode.shown_byte_count(lines, text), plain, raw_lines(text)
    return None


def raw_lines(text):
    return [overstrike.decode(line.replace('\r', ''))[0] for line in text.split('\n')]


def key_label_bytes(raw, width):
    """KEYの一覧の1行(C++のCUTの見出し)の大きさ: 元の行の折り返す前の分を、タブを「U+2060+次の8桁までの空白」
    (PipeBuffer)にして、行末の空白とタブの印を除いたもの(stripTrailingTabPadding())、と改行の1。"""
    out, column = [], 0
    for ch in raw:
        if ch == '\t':
            step = max(0, min(8 - column % 8, width - column))
            out.append('\u2060' + ' ' * step)
            column += step
        else:
            w = utf8.display_width_of_codepoint(ord(ch))
            if column + w > width:
                break
            out.append(ch)
            column += w
        if column >= width:
            break
    label = ''.join(out).rstrip(' ')
    if label.endswith('\u2060'):
        label = label[:-1]
    return len(label.encode('utf-8')) + 1


def key_list_byte_count(lines, raw, width):
    """KEYの一覧の大きさ(C++はCUTの見出しの行の大きさ、元の行ごとに1つ)。"""
    total, line_start = 0, True
    for line in lines:
        if line_start and line.raw_line_number and line.raw_line_number <= len(raw):
            total += key_label_bytes(raw[line.raw_line_number - 1], width)
        line_start = line.ends_with_newline
    return total


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
