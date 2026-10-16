#!/usr/bin/env python3
# src/graphical_display.cppのGraphicalDisplayのメンバ関数の定義から、GUI無しのビルド用の
# 空の定義(src/graphical_display_stub.cpp)を作り直す(2026-10-05、.steering/20261005-windows-gitbash/)。
# 使い方: src/cpp で python3 tools/gen_graphical_display_stub.py
import re

src = open('src/graphical_display.cpp', encoding='utf-8').read()
pat = re.compile(r'^([^\n#/ ][^\n;{}]*?\bGraphicalDisplay::(~?\w+)\((?:[^(){};]|\([^()]*\))*\)\s*(?:const)?)\s*\{',
                 re.M)
defs, seen = [], set()
WINDOWS_ONLY = {'scanFontFiles'}
for m in pat.finditer(src):
    sig, name = m.group(1).strip(), m.group(2)
    if name in ('GraphicalDisplay', '~GraphicalDisplay') or sig in seen:
        continue
    seen.add(sig)
    sig = re.sub(r'\s*=\s*[^,)]+(?=[,)])', '', sig)  # 既定引数を除く
    ret = sig[:sig.index('GraphicalDisplay::' + name)].strip()
    body = '{}' if ret == 'void' else '{ return nullptr; }' if name == 'create' else '{ return {}; }'
    definition = sig + ' ' + body
    # Windowsのネイティブだけで宣言しているもの(graphical_display.hppの#ifdef _WIN32の中、2026-10-16)。
    if name in WINDOWS_ONLY:
        definition = '#ifdef _WIN32\n' + definition + '\n#endif'
    defs.append(definition)

header = '''// GUI無しのビルド(2026-10-05、.steering/20261005-windows-gitbash/): SDL2の無い環境(MSYS2の
// MSYS環境等)で、graphical_display.cppの代わりにビルドする(Makefileの USILIC_NO_GUI)。
// create()はnullptrを返す(-gは使えない)ので、他のメンバ関数が呼ばれることは無い。リンクを
// 通すためだけの空の定義で、tools/gen_graphical_display_stub.pyが作る(手で直さない)。
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "graphical_display.hpp"

namespace fm {

struct GraphicalDisplay::Impl {};

GraphicalDisplay::GraphicalDisplay() : impl_(std::make_unique<Impl>()) {}
GraphicalDisplay::~GraphicalDisplay() = default;

'''
with open('src/graphical_display_stub.cpp', 'w', encoding='utf-8') as f:
    f.write(header + '\n'.join(defs) + '\n\n}  // namespace fm\n')
print(len(defs), 'definitions')
