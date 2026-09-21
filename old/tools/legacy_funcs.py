#!/usr/bin/env python3
"""旧C実装(old/src/*.c)の関数定義を列挙する。

旧ソースは「関数の宣言が行頭から始まり、次に行頭の`{`が来る」K&R風の書式なので、
行頭の`{`から遡って関数の頭(header)を特定する。

使い方:
  legacy_funcs.py              # ファイルごとの関数数と一覧
  legacy_funcs.py --json       # JSONで出力(check_legacy_comments.pyが利用)
"""
import glob, json, os, re, sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')


def functions(path):
    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    out = []
    for i, l in enumerate(lines):
        # 行頭の`{`(後ろに行内コメントが付くものも含む)
        if not re.match(r'^\{\s*(/\*.*)?$', l):
            continue
        j = i - 1
        head = []
        while j >= 0:
            s = lines[j]
            st = s.strip()
            if st == '' or s.startswith(('}', '#')) or st.startswith(('/*', '*', '//')):
                break
            if st.endswith((';', '}')):  # 直前の宣言・文の終わり(関数の頭ではない)
                break
            head.insert(0, (j, s))
            j -= 1
        if not head:
            continue
        text = ' '.join(s.strip() for _, s in head)
        if text.endswith('=') or '(' not in text or text.startswith(('typedef', 'struct', 'union', 'enum')) and ')' not in text:
            continue
        # 直前の行が代入・宣言の続き(セミコロン終わり)なら関数の頭ではない
        first = head[0][1].strip()
        if first.endswith(';'):
            continue
        m = re.search(r'([A-Za-z_]\w*)\s*\(', text)
        if not m:
            continue
        out.append({'name': m.group(1), 'line': head[0][0] + 1, 'brace': i + 1, 'header': text})
    return out


def main():
    res = {}
    for p in sorted(glob.glob(os.path.join(SRC, '*.c'))):
        res[os.path.basename(p)] = functions(p)
    if '--json' in sys.argv:
        json.dump(res, sys.stdout, ensure_ascii=False, indent=1)
        return
    total = 0
    for f, fs in res.items():
        print(f'{f}: {len(fs)}')
        total += len(fs)
        for x in fs:
            print(f'  {x["line"]:5d} {x["name"]}')
    print('total', total)


if __name__ == '__main__':
    main()
