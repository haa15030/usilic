#!/usr/bin/env python3
"""関数の直前へ【機能】【C++】【備考】コメントを挿入する(冪等)。

使い方: add_comments.py FILE.c SPEC.py
SPEC.py は `SPEC = {"関数名": ("機能(改行で折り返し)", "C++の対応先", "備考(空でよい)"), ...}` を定義する。
既に【機能】を含むコメントが直前にある関数はスキップする。
"""
import os, runpy, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from legacy_funcs import functions  # noqa: E402
from check_legacy_comments import comment_before  # noqa: E402

SRC = os.path.join(HERE, '..', 'src')


def block(func, cpp, note):
    out = ['/*']
    lines = func.split('\n')
    out.append(' * 【機能】' + lines[0])
    out += [' *       ' + l for l in lines[1:]]
    out.append(' * 【C++】' + cpp)
    if note:
        nl = note.split('\n')
        out.append(' * 【備考】' + nl[0])
        out += [' *       ' + l for l in nl[1:]]
    out.append(' */')
    return out


def main():
    path = os.path.join(SRC, sys.argv[1])
    spec = runpy.run_path(sys.argv[2])['SPEC']
    lines = open(path, encoding='utf-8').read().split('\n')
    funcs = functions(path)
    names = {f['name'] for f in funcs}
    missing = set(spec) - names
    if missing:
        print('SPECにあるが関数として見つからない:', sorted(missing)); sys.exit(1)
    todo = [f for f in funcs if f['name'] in spec and (comment_before(lines, f['line']) is None or '【機能】' not in comment_before(lines, f['line']))]
    for f in sorted(todo, key=lambda x: -x['line']):
        fn, cpp, note = spec[f['name']]
        lines[f['line'] - 1:f['line'] - 1] = block(fn, cpp, note)
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))
    left = [f['name'] for f in funcs if f['name'] not in spec]
    print(f'挿入 {len(todo)} 件。SPECに無い関数: {left}')


if __name__ == '__main__':
    main()
