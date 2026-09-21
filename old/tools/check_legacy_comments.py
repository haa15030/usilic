#!/usr/bin/env python3
"""旧C実装のコメント検証。

  check_legacy_comments.py                 全関数に【機能】【C++】コメントがあり、C++参照が実在するか
  check_legacy_comments.py --file scroll.c 特定のファイルだけ
  check_legacy_comments.py --strip-check BASE_REF GEN_DIR
      コメントを除いたコード(gcc -E -P)がBASE_REFの版と同一か。GEN_DIRは`config.sh`が生成した
      config.h/progname.h/Makefileのあるディレクトリ(old/src相当)。
"""
import glob, os, re, subprocess, sys, tempfile, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(REPO, 'old', 'src')
CPP = os.path.join(REPO, 'src', 'cpp')
sys.path.insert(0, HERE)
from legacy_funcs import functions  # noqa: E402


def comment_before(lines, head_line):
    """関数の頭(1始まり)の直前にあるブロックコメントの本文。無ければNone。"""
    j = head_line - 2
    if j < 0 or not lines[j].rstrip().endswith('*/'):
        return None
    k = j
    while k >= 0 and '/*' not in lines[k]:
        k -= 1
    if k < 0:
        return None
    return '\n'.join(lines[k:j + 1])


def cpp_ref_ok(item):
    """`Class::method (path)` / `func (path)` の1件が実在するか。"""
    item = item.strip()
    if item.startswith('なし'):
        return True, ''
    m = re.match(r'^(.+?)\s*\(([^)]+)\)\s*$', item)
    if not m:
        return False, f'書式が違う: {item!r}'
    sym, path = m.group(1).strip(), m.group(2).strip()
    full = os.path.join(REPO, path) if path.startswith('src/') else os.path.join(CPP, path)
    if not os.path.exists(full):
        return False, f'ファイルが無い: {path}'
    base = os.path.splitext(full)[0]
    text = ''
    for ext in ('.cpp', '.hpp', '.cc', '.h'):
        if os.path.exists(base + ext):
            text += open(base + ext, encoding='utf-8', errors='replace').read()
    if os.path.splitext(full)[1] not in ('.cpp', '.hpp', '.cc', '.h'):
        text = open(full, encoding='utf-8', errors='replace').read()
    # コメント(/* */ と //)を除いた本文だけで探す(コメントに名前が残っているだけの、廃止された
    # 関数を「実在する」と誤認しないため)
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    name = sym.split('::')[-1].strip()
    if not re.search(r'\b' + re.escape(name) + r'\b', text):
        return False, f'{name} が {path} に見つからない'
    if '::' in sym:
        cls = sym.split('::')[0].strip()
        if not re.search(r'\b' + re.escape(cls) + r'\b', text):
            return False, f'クラス {cls} が {path} に見つからない'
    return True, ''


def check(files):
    bad = 0
    total = 0
    for p in files:
        lines = open(p, encoding='utf-8', errors='replace').read().split('\n')
        for f in functions(p):
            total += 1
            c = comment_before(lines, f['line'])
            where = f"{os.path.basename(p)}:{f['line']} {f['name']}"
            if c is None or '【機能】' not in c or '【C++】' not in c:
                print('NG(コメント無し/書式違い):', where); bad += 1; continue
            m = re.search(r'【C\+\+】(.*?)(?=【|\*/|$)', c, re.S)
            body = re.sub(r'\n\s*\*', ' ', m.group(1)).strip()
            # 「A; B; C (path)」のように、パスの無い項目は次に現れるパスを共有する
            pending = []
            for item in [x.strip() for x in body.split(';') if x.strip()]:
                if item.startswith('なし'):
                    continue
                if '(' not in item:
                    pending.append(item); continue
                m2 = re.match(r'^(.+?)\s*\(([^)]+)\)\s*$', item)
                path = m2.group(2) if m2 else ''
                for sym in pending + [item]:
                    one = sym if '(' in sym else f'{sym} ({path})'
                    ok, why = cpp_ref_ok(one)
                    if not ok:
                        print('NG(参照):', where, '-', why); bad += 1
                pending = []
            if pending:
                print('NG(参照):', where, '- パスが無い項目:', pending); bad += 1
    print(f'関数 {total} 件、問題 {bad} 件')
    return bad


def preprocess(dirpath, files):
    out = {}
    for p in files:
        name = os.path.basename(p)
        r = subprocess.run(['gcc', '-E', '-P', '-I', dirpath, os.path.join(dirpath, name)],
                           capture_output=True, text=True)
        out[name] = re.sub(r'\s+', ' ', r.stdout)
    return out


def strip_check(base, gen):
    files = sorted(glob.glob(os.path.join(SRC, '*.c')))
    with tempfile.TemporaryDirectory() as td:
        a, b = os.path.join(td, 'work'), os.path.join(td, 'base')
        os.makedirs(a); os.makedirs(b)
        for p in glob.glob(os.path.join(SRC, '*.[ch]')):
            shutil.copy(p, a)
        names = subprocess.check_output(['git', '-C', REPO, 'ls-tree', '--name-only', base, 'old/src/'], text=True).split()
        for n in names:
            if n.endswith(('.c', '.h')):
                data = subprocess.check_output(['git', '-C', REPO, 'show', f'{base}:{n}'])
                open(os.path.join(b, os.path.basename(n)), 'wb').write(data)
        for g in ('config.h', 'progname.h'):
            for d in (a, b):
                shutil.copy(os.path.join(gen, g), d)
        wa, wb = preprocess(a, files), preprocess(b, files)
        bad = 0
        for n in wa:
            if wa[n] != wb.get(n):
                print('NG(コードが変わっている):', n); bad += 1
        print(f'前処理結果の比較(コメント無視): {len(wa)} ファイル、差異 {bad} 件')
        return bad


if __name__ == '__main__':
    if '--strip-check' in sys.argv:
        i = sys.argv.index('--strip-check')
        sys.exit(1 if strip_check(sys.argv[i + 1], sys.argv[i + 2]) else 0)
    files = sorted(glob.glob(os.path.join(SRC, '*.c')))
    if '--file' in sys.argv:
        files = [os.path.join(SRC, sys.argv[sys.argv.index('--file') + 1])]
    sys.exit(1 if check(files) else 0)
