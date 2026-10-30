"""~/.usilic/usilicrcの読み書き(C++のusilicrc_file.cppのensureUser()・updateUserIfOlder()・setSection())。

起動時に、無ければインストールされた既定のusilicrcを写し、既定の方が新しければ控え(.日付.bak)を取ってから置き換える
(usilic自身が書き込む節の値は引き継ぐ)。-S・-D等の起動オプションは節の値をここで書き換える。
"""
import os
import shutil
import time

# usilic自身が書き込む節(置き換えても前の値を引き継ぐ)。FONT・SANS-SERIF・SERIFは2026-10-30にC++版で~/.usilic/historyへ移した
OWN_SECTIONS = ('SPEED', 'DRAG', 'WAIT', 'BUFFER', 'LINE')


def user_path(home):
    return os.path.join(home, '.usilic', 'usilicrc')


def installed_path(home):
    for path in (os.path.join(home, '.local', 'lib', 'usilic', 'usilicrc'), '/usr/local/lib/usilic/usilicrc'):
        if os.path.isfile(path):
            return path
    return None


def _read(path):
    try:
        with open(path, encoding='utf-8', errors='replace', newline='') as f:
            return f.read()
    except OSError:
        return ''


def _split(text):
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    return lines


def _join(lines):
    return ''.join(line + '\n' for line in lines)


def _is_keyword_line(line):
    return bool(line) and line[0] not in '# \t'


def _first_token(line):
    return line.split(None, 1)[0] if line.split() else ''


def _find_header(lines, section):
    for i, line in enumerate(lines):
        if _is_keyword_line(line) and _first_token(line) == section:
            return i
    return None


def _section_end(lines, header):
    e = header + 1
    while e < len(lines) and not _is_keyword_line(lines[e]):
        e += 1
    return e


def _is_value_line(line):
    if not line or line[0] not in ' \t':
        return False
    stripped = line.lstrip(' \t')
    return bool(stripped) and stripped[0] != '#'


def section_values(text, section):
    lines = _split(text)
    header = _find_header(lines, section)
    if header is None:
        return None
    return [lines[i].lstrip(' \t').rstrip('\r\n \t') for i in range(header + 1, _section_end(lines, header))
            if _is_value_line(lines[i])]


def replace_section(text, section, values, template_text):
    """replaceSection(): 節の値の行を`values`に(節が無ければ既定のusilicrcの同じ節を雛形にKEYの手前へ)。"""
    lines = _split(text)
    header = _find_header(lines, section)
    if header is None:
        template = _split(template_text)
        th = _find_header(template, section)
        block = template[th:_section_end(template, th)] if th is not None else []
        if not block:
            block = [section, '']
        if block[-1]:
            block.append('')
        at = _find_header(lines, 'KEY')
        if at is None:
            at = len(lines)
            if lines and lines[-1]:
                lines.append('')
                at += 1
        lines[at:at] = block
        header = at
    end = _section_end(lines, header)
    insert_at = None
    for i in range(end - 1, header, -1):
        if not _is_value_line(lines[i]):
            continue
        del lines[i]
        end -= 1
        insert_at = i
    value_lines = ['\t' + v for v in values]
    if insert_at is None:
        last = header
        for i in range(header + 1, end):
            if lines[i]:
                last = i
        insert_at = last + 1
        if last != header:
            value_lines.insert(0, '')
    lines[insert_at:insert_at] = value_lines
    return _join(lines)


def _write_atomically(path, text):
    temp = path + '.tmp'
    try:
        with open(temp, 'w', encoding='utf-8', newline='') as f:
            f.write(text)
        os.replace(temp, path)
        return True
    except OSError:
        return False


def ensure_user(home):
    """ensureUser(): ~/.usilic/usilicrcが無ければ既定のものを写す。"""
    if not home:
        return False
    user = user_path(home)
    if os.path.exists(user):
        return True
    installed = installed_path(home)
    if not installed:
        return False
    try:
        os.makedirs(os.path.join(home, '.usilic'), exist_ok=True)
        shutil.copyfile(installed, user)
        return True
    except OSError:
        return False


def update_user_if_older(home):
    """updateUserIfOlder(): 既定のusilicrcの方が新しければ控えを取って置き換える(OWN_SECTIONSの値は引き継ぐ)。
    控えのパス(置き換えなければNone)。"""
    user = user_path(home)
    installed = installed_path(home)
    if not installed:
        return None
    try:
        if os.path.getmtime(installed) <= os.path.getmtime(user):
            return None
    except OSError:
        return None
    backup = user + '.' + time.strftime('%Y%m%d') + '.bak'
    if os.path.exists(backup):
        backup = user + '.' + time.strftime('%Y%m%d%H%M%S') + '.bak'
    try:
        shutil.copyfile(user, backup)
    except OSError:
        return None
    user_text, installed_text = _read(user), _read(installed)
    merged = installed_text
    for section in OWN_SECTIONS:
        mine = section_values(user_text, section)
        if mine is not None and mine != (section_values(installed_text, section) or []):
            merged = replace_section(merged, section, mine, installed_text)
    if not _write_atomically(user, merged):
        return None
    return backup


def set_section(home, section, values):
    """setSection(): ~/.usilic/usilicrcの節の値を書き換える。"""
    if not ensure_user(home):
        return False
    user = user_path(home)
    installed = installed_path(home)
    return _write_atomically(user, replace_section(_read(user), section, values, _read(installed) if installed else ''))
