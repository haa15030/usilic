#!/usr/bin/env python3
"""旧関数名がC++側のどのファイル・コメントで言及されているかを集計する(対応先を探す手掛かり)。

使い方: cpp_mentions.py FILE.c   # そのファイルの各関数について、言及の多いC++ファイルと例を表示
"""
import glob, os, re, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from legacy_funcs import functions

CPP = os.path.join(HERE, '..', '..', 'src', 'cpp')
files = sorted(glob.glob(os.path.join(CPP, 'src', '*.[ch]pp')) + glob.glob(os.path.join(CPP, 'app', '*.cpp')))
texts = {f: open(f, encoding='utf-8', errors='replace').read().split('\n') for f in files}

for fn in functions(os.path.join(HERE, '..', 'src', sys.argv[1])):
    name = fn['name']
    pat = re.compile(r'\b' + re.escape(name) + r'\s*\(')
    hits = collections.Counter(); sample = {}
    for f, ls in texts.items():
        for i, l in enumerate(ls):
            if pat.search(l):
                hits[os.path.basename(f)] += 1
                sample.setdefault(os.path.basename(f), l.strip()[:110])
    top = ', '.join(f'{f}({n})' for f, n in hits.most_common(3)) or '(言及なし)'
    print(f'{name}: {top}')
    for f, _ in hits.most_common(1):
        print('     e.g.', sample[f])
