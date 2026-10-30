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


def help_roff_path(japanese):
    """HELPのroff版(日本語はHELP.1、英語はHELP_en.1、C++のhelpRoffSourcePath())。"""
    return find_in_library('HELP.1' if japanese else 'HELP_en.1')


def generated_help_epub_path(japanese):
    """help_epub_path()が作る~/.usilic/HELP.epub(英語はHELP.en.epub、C++のgeneratedHelpEpubPath())。"""
    return os.path.join(os.path.expanduser('~'), '.usilic', 'HELP.epub' if japanese else 'HELP.en.epub')


def is_generated_help_epub(path):
    """作ったHELP.epub(どちらの言語も)か。履歴に積まない(C++のisGeneratedHelpEpub())。"""
    if not path:
        return False
    for japanese in (True, False):
        try:
            if os.path.samefile(path, generated_help_epub_path(japanese)):
                return True
        except OSError:
            pass
    return False


def help_epub_path(japanese):
    """HELPのEPUB版(C++のhelpEpubPath()、項目1121): HELP.epubは入れず、HELP.1(英語はHELP_en.1)から
    ~/.usilic/HELP.epub(HELP.en.epub)を作って開く。HELP.1の方が新しければ作り直す。注釈の元と表紙はHELP.1に
    埋め込んである(usilic -n)。埋め込みの無い古いHELP.1なら、HELP.1と同じ場所のusilicrc・HELP.notesを使う。"""
    from . import help_to_epub
    source = help_roff_path(japanese)
    if not source:
        return None
    out = generated_help_epub_path(japanese)
    try:
        if os.path.getmtime(out) >= os.path.getmtime(source):
            return out
    except OSError:
        pass

    def read(path):
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                return f.read()
        except OSError:
            return ''
    roff = read(source)
    if not roff.startswith('.'):
        return None
    directory = os.path.dirname(source)
    english = not japanese
    epub = help_to_epub.convert_ms_roff_to_help_epub(
        roff, read(os.path.join(directory, 'usilicrc.en' if english else 'usilicrc')),
        read(os.path.join(directory, 'HELP.en.notes' if english else 'HELP.notes')))[0]
    temp = '%s.tmp%d' % (out, os.getpid())
    try:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(temp, 'wb') as f:
            f.write(epub)
        os.replace(temp, out)
    except OSError:
        try:
            os.remove(temp)
        except OSError:
            pass
        return None
    return out


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


def normalize_for_match(line):
    """normalizeHelpLineForMatch(): 組み方によらず比べられるように、空白類と不可視の印(NBSPも)を除く。"""
    return ''.join(c for c in line if c not in ' \t\r\n\u00a0\u2060\ue000\ufeff')


def lines_match(a, b):
    """helpLinesMatch(): 短い方がもう片方の頭(groffの折り返し)で、短い方も「：」を含むこと。"""
    if not a or not b:
        return False
    shorter, longer = (a, b) if len(a) < len(b) else (b, a)
    return FULL_COLON in shorter and longer.startswith(shorter)


def key_line_request(raw, raw_line_number):
    """jumpFromKeyToHelp(): KEYの行(元の行の番号)→(比べる形の字, 前に同じ行がいくつあるか)。無ければNone。"""
    if not raw_line_number or raw_line_number > len(raw):
        return None
    key = normalize_for_match(raw[raw_line_number - 1])
    if not key:
        return None
    return key, sum(1 for line in raw[:raw_line_number - 1] if lines_match(normalize_for_match(line), key))


def find_key_line(lines, key, nth):
    """jumpHelpToKeyLine(): 行(折り返した続きは元の行ごとにつなぐ)の中で、nth番目に一致する所の最初の行。"""
    count, i = 0, 0
    while i < len(lines):
        j, joined = i + 1, lines[i].text
        while j < len(lines) and lines[i].raw_line_number is not None and \
                lines[j].raw_line_number == lines[i].raw_line_number:
            joined += lines[j].text
            j += 1
        if lines_match(normalize_for_match(joined), key):
            if count == nth:
                return i
            count += 1
        i = j
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
