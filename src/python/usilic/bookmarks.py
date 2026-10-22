"""ブックマーク(C++のapplication.cppのreadBookmarks()・writeBookmarks())。

~/.usilic/bookmarks: 1行に「名前<TAB>行番号<TAB>パス」(行番号0はディレクトリ)。書いた順(新しい順)。
"""
import os


def bookmarks_path():
    return os.path.join(os.path.expanduser('~'), '.usilic', 'bookmarks')


def read_bookmarks(path=None):
    """[(名前, パス, 行番号)]。"""
    out = []
    try:
        with open(path or bookmarks_path(), encoding='utf-8', errors='surrogateescape') as f:
            for line in f:
                parts = line.rstrip('\n').split('\t', 2)
                if len(parts) < 3 or not parts[0] or not parts[2]:
                    continue
                number = int(parts[1]) if parts[1].lstrip('-').isdigit() else 0
                out.append((parts[0], parts[2], number))
    except OSError:
        pass
    return out


def write_bookmarks(entries, path=None):
    path = path or bookmarks_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', errors='surrogateescape') as f:
        for name, place, number in entries:
            f.write('%s\t%d\t%s\n' % (name, number, place))


def set_bookmark(name, place, number, path=None):
    """同じ名前は消してから先頭へ。"""
    entries = [e for e in read_bookmarks(path) if e[0] != name]
    write_bookmarks([(name, place, number)] + entries, path)


def take_bookmark(name, path=None):
    """名前の物を先頭へ移して返す(無ければNone)。"""
    entries = read_bookmarks(path)
    for i, entry in enumerate(entries):
        if entry[0] == name:
            write_bookmarks([entry] + entries[:i] + entries[i + 1:], path)
            return entry
    return None


def delete_bookmark(name, path=None):
    entries = read_bookmarks(path)
    kept = [e for e in entries if e[0] != name]
    if len(kept) == len(entries):
        return False
    write_bookmarks(kept, path)
    return True


def display_lines(entries):
    """一覧の行: 「名前  パス:行番号」(名前は一番長いのに揃える)。"""
    width = max([4] + [len(n) for n, _, _ in entries])
    return [name.ljust(width + 2) + place + (':%d' % number if number > 0 else '') for name, place, number in entries]
