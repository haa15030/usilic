#!/usr/bin/env python3
"""旧C実装の関数コメントから、旧→C++の対応表(docs/legacy-cpp-map.md)を生成する。

使い方: gen_map.py > ../../docs/legacy-cpp-map.md
"""
import glob, os, re, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from legacy_funcs import functions
from check_legacy_comments import comment_before

SRC = os.path.join(HERE, '..', 'src')


def parse(block):
    body = re.sub(r'^\s*/\*\s*|\s*\*/\s*$', '', block, flags=re.M)
    lines = [re.sub(r'^\s*\*\s?', '', l) for l in block.split('\n')]
    text = '\n'.join(lines)
    def grab(tag):
        m = re.search(r'【' + re.escape(tag) + r'】(.*?)(?=\n?\s*【|\Z)', text, re.S)
        if not m:
            return ''
        # 行の継ぎ目: 両側が日本語(非ASCII)なら空白を入れず、それ以外は空白1つ
        def join(mm):
            a, b = mm.group(1), mm.group(2)
            return a + ('' if (ord(a) > 127 and ord(b) > 127) else ' ') + b
        return re.sub(r'(\S)\s*\n\s*(\S)', join, m.group(1)).strip().rstrip('/').strip()
    return grab('機能'), grab('C++'), grab('備考')


def main():
    rows = []
    for p in sorted(glob.glob(os.path.join(SRC, '*.c'))):
        lines = open(p, encoding='utf-8', errors='replace').read().split('\n')
        for f in functions(p):
            c = comment_before(lines, f['line'])
            if not c:
                continue
            kino, cpp, note = parse(c)
            rows.append((os.path.basename(p), f['name'], kino, cpp, note))
    print('# 旧C実装 → C++版 対応表\n')
    print('`old/tools/gen_map.py`が、旧ソース(`old/src/*.c`)の各関数の直前のコメントから自動生成する。')
    print('手で直さず、旧ソースのコメントを直してから再生成すること(`check_legacy_comments.py`で検証できる)。\n')
    print('- 旧関数の位置: `grep -n "^関数名" old/src/*.c`。行番号は書かない(すぐずれるため)。')
    print('- 「なし(…)」は、C++版に対応するものが無いことを表す(理由つき)。')
    print(f'- 関数数: {len(rows)}\n')
    print('## 目次\n')
    files = collections.OrderedDict()
    for r in rows:
        files.setdefault(r[0], []).append(r)
    for f, rs in files.items():
        print(f'- [{f}](#{f.replace(".", "")}) ({len(rs)}関数)')
    print('- [C++からの逆引き](#c-からの逆引き)\n')
    for f, rs in files.items():
        print(f'## {f}\n')
        print('| 旧関数 | 機能 | C++の対応先 |')
        print('|---|---|---|')
        for _, name, kino, cpp, note in rs:
            k = kino.replace('|', '\\|')
            cc = cpp.replace('|', '\\|')
            print(f'| `{name}` | {k} | {cc} |')
        print()
    # 逆引き: C++のファイル → 旧関数
    rev = collections.defaultdict(list)
    for f, name, kino, cpp, note in rows:
        seen = set()
        for path in re.findall(r'\((src/cpp/[^)\s]+)\)', cpp):
            if path not in seen:
                seen.add(path); rev[path].append(f'{f}:{name}')
    print('## C++からの逆引き\n')
    print('C++のファイルごとに、対応する旧関数を挙げる(そのファイルを参照しているもの)。\n')
    for path in sorted(rev):
        print(f'### `{path}`\n')
        print(', '.join(f'`{x}`' for x in rev[path]) + '\n')


if __name__ == '__main__':
    main()
