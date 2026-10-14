#!/usr/bin/env python3
# usilic -n が作るHELPのroff(doc/HELP.ja.1)を横書きのEPUB3にする(試作)。
# 使い方: python3 src/cpp/tools/help2epub.py doc/HELP.ja.1 src/HELP.epub
# 章(h2)ごとに1ファイル、見出し(INFO等、h3)は目次の入れ子。字下げ(.in/.ti)はpadding-left/text-indent(1n=0.5em)。
# 「usilicrc の URL」等の設定項目の名前は巻末の注釈(KDPの推奨どおりaside epub:type="footnote"と両方向の
# リンク)へのリンクにする。注釈の中身はusilicrc(3つ目の引数、省略時は入力の../src/usilicrc)のその項目の
# 見出しの直後の「#」の行。入力と同じ所のHELP.notes(doc/HELP.notes)に書いてあればその文。
# 目次(nav)はspineに入れない(KDPの論理目次としては残る)。
import datetime, html, re, sys, unicodedata, uuid, zipfile

src, out = sys.argv[1], sys.argv[2]
import os
notes_path = os.path.join(os.path.dirname(src) or '.', 'HELP.notes')
rc_path = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(src) or '.', '..', 'src', 'usilicrc')
lines = open(src, encoding='utf-8').read().split('\n')

def width(s):
    return sum(2 if unicodedata.east_asian_width(c) in 'WF' else 1 for c in s)

def num(arg):  # "19n" "0" → 19
    m = re.match(r'(\d+)', arg or '0')
    return int(m.group(1)) if m else 0

ESC = re.compile(r'\\f\[([A-Z]+)\]|\\f([BIRP])|\\\*\[usilic-tab\]|\\e|\\\[u([0-9A-Fa-f]{4,5})\]|\\&|\\-|\\(.)')

def plain(s):
    """エスケープを外した文字列(タブ展開・見出し名用)"""
    def rep(m):
        if m.group(3): return chr(int(m.group(3), 16))
        if m.group(0) == r'\e': return '\\'
        if m.group(0) == r'\-': return '-'
        if m.group(4): return m.group(4)
        return ''
    return ESC.sub(rep, s)

def inline(s, col0):
    """1行をXHTMLへ。タブは8桁ごと(.ta T 8n)、行頭の空白・2つ以上続く空白はnbsp。"""
    outp, font, prev, opened = [], 'R', 'R', None
    col = col0
    pos = 0
    def emit_text(t):
        nonlocal col
        buf = []
        for c in t:
            if c == '\t':
                n = 8 - (col % 8)
                buf.append(' ' * n); col += n
            else:
                buf.append(c); col += width(c)
        return ''.join(buf)
    pieces = []
    for m in ESC.finditer(s):
        pieces.append(('t', s[pos:m.start()]))
        pieces.append(('e', m))
        pos = m.end()
    pieces.append(('t', s[pos:]))
    for kind, v in pieces:
        if kind == 't':
            if v: outp.append(html.escape(emit_text(v), quote=False))
            continue
        m = v
        f = m.group(1) or m.group(2)
        if f:
            if f == 'JPM':  # 濁点の字の書体(groff用)。見た目の太字/斜体は変えない
                prev = font
                continue
            if f == 'R' and m.group(1):  # \f[R]は\f[JPM]の後始末
                continue
            if f == 'P': f = prev
            if opened: outp.append('</%s>' % opened); opened = None
            font = f
            if f == 'B': outp.append('<b>'); opened = 'b'
            elif f == 'I': outp.append('<i>'); opened = 'i'
            continue
        outp.append(html.escape(emit_text(plain(m.group(0))), quote=False))
    if opened: outp.append('</%s>' % opened)
    text = ''.join(outp)
    # 行頭の空白と、桁をそろえるための2つ以上の空白はnbspにする(詰められないように)
    text = re.sub(r'^ +', lambda m: '\u00a0' * len(m.group(0)), text)
    text = re.sub(r' {2,}', lambda m: '\u00a0' * len(m.group(0)), text)
    return text

# 本文の始まり(前置きの後の最初の文字の行)
start = next(i for i, l in enumerate(lines) if l and not l.startswith('.'))
ind, ti = 0, None
blocks = []   # ('p', indent, firstIndent, html, raw) / ('sp',) / ('h', level, title, html)
last_name = ''
for l in lines[start:]:
    if l.startswith('.'):
        parts = l.split()
        req = parts[0]
        if req == '.in': ind = num(parts[1] if len(parts) > 1 else '0')
        elif req == '.ti': ti = num(parts[1] if len(parts) > 1 else '0')
        elif req == '.sp': blocks.append(('sp',))
        continue
    if not l.strip():
        blocks.append(('sp',))
        continue
    first = ind if ti is None else ti
    is_tabhead = l.endswith(r'\*[usilic-tab]') and not l.startswith(r'\*[usilic-tab]')
    if is_tabhead or (first == 0 and ind == 0 and not l.startswith((' ', '\t', '\\*'))):
        p = plain(l).strip()
        if '：' in p:
            name, desc = p.split('：', 1)
            name = re.sub(r'\s+', ' ', name).strip()
            if not name:  # 2行に分けた見出し(F/EGREP)は、前の行の名前を使い、前の行は見出しへまとめる
                if blocks and blocks[-1][0] == 'p' and '：' in blocks[-1][4]:
                    name = re.sub(r'\s+', ' ', blocks.pop()[4].split('：', 1)[0]).strip()
                else:
                    name = last_name
            title = name + '：' + desc.strip()
            last_name = name
            body = inline(l.replace(r'\*[usilic-tab]', ''), 0)
            body = re.sub('\u00a0*：\u00a0*', '：', body).strip('\u00a0 ')
            if body.startswith('：'): body = '<b>%s</b>' % html.escape(name) + body
        else:
            title = p
            body = inline(l.replace(r'\*[usilic-tab]', ''), 0).strip('\u00a0 ')
        level = 2 if first == 0 else 3
        blocks.append(('h', level, title, body))
    else:
        blocks.append(('p', ind, first, inline(l, first), plain(l)))
    ti = None

# 章(h2)ごとにファイルを分ける
chapters = []
for b in blocks:
    if b[0] == 'h' and b[1] == 2:
        chapters.append({'title': b[2], 'blocks': [b], 'subs': []})
    elif chapters:
        chapters[-1]['blocks'].append(b)
        if b[0] == 'h': chapters[-1]['subs'].append(b[2])

classes = {}
def cls(i, f):
    name = 'i%dt%d' % (i, f)
    classes[name] = (i, f)
    return name

def xhtml(title, body):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE html>\n'
            '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="ja" lang="ja">\n'
            '<head><meta charset="UTF-8"/><title>%s</title>'
            '<link rel="stylesheet" type="text/css" href="../Style/style.css"/></head>\n<body>\n%s</body>\n</html>\n'
            % (html.escape(title), body))

# usilicrcの設定項目(行頭が英大文字の見出し)ごとの、見出しの直後に続く「#」の行(注釈の中身)。
rc_comments = {}
if os.path.exists(rc_path):
    item = None
    for l in open(rc_path, encoding='utf-8').read().split('\n'):
        if re.match(r'[A-Z][A-Z-]*\s*$', l):
            item = l.strip()
            rc_comments[item] = []
        elif item and l.startswith('#'):
            rc_comments[item].append(re.sub(r'^# ?', '', l))
        else:
            item = None

# HELP.notes: "@設定項目"(例 "@URL")の行の後に注釈の文を書くと、usilicrcのコメントの代わりにそれを使う。
written = {}
if os.path.exists(notes_path):
    key = None
    for l in open(notes_path, encoding='utf-8').read().split('\n'):
        if l.startswith('#'): continue
        if l.startswith('@'):
            key = l[1:].strip()
            written[key] = []
        elif key is not None:
            written[key].append(l)

notes = {}   # 設定項目 -> 注釈のid
backrefs = {}  # 注釈のid -> [(章のファイル, 参照のid)]
# 「usilicrc の URL」「usilicrc の \fBEGREP\fR」の設定項目の名前
REF = re.compile(r'(<i>usilicrc</i>[\s\u00a0]*の[\s\u00a0]*)(<b>)?([A-Z][A-Z-]*[A-Z])(</b>)?')
def link_refs(h, fname):
    def rep(m):
        item = m.group(3)
        if not (rc_comments.get(item) or written.get(item)): return m.group(0)
        nid = notes.setdefault(item, 'n%d' % (len(notes) + 1))
        rid = 'r%d' % (sum(len(v) for v in backrefs.values()) + 1)
        backrefs.setdefault(nid, []).append((fname, rid))
        return '%s<a id="%s" href="notes.xhtml#%s" epub:type="noteref">%s%s%s</a>' % (
            m.group(1), rid, nid, m.group(2) or '', item, m.group(4) or '')
    return REF.sub(rep, h)

def render(blist, fname):
    body, sub = [], 0
    for b in blist:
        if b[0] == 'sp':
            # 空行(HELPの「#」の行、.sp)は、余白(margin)ではなく空の行にする(usilicのEPUB表示は余白を読まない)
            body.append('<p class="blank"><br/></p>\n')
            continue
        if b[0] == 'h':
            tag = 'h%d' % b[1]
            idattr = ' id="s%d"' % sub if b[1] == 3 else ' id="top"'
            if b[1] == 3: sub += 1
            body.append('<%s%s>%s</%s>\n' % (tag, idattr, b[3], tag))
        else:
            _, i, f, h, raw = b
            c = [cls(i, f)]
            if re.search(r'^\s|\S {2,}|\t', raw): c.append('pre')
            h = link_refs(h, fname)
            body.append('<p class="%s">%s</p>\n' % (' '.join(c), h if h.strip('\u00a0 ') else '\u00a0'))
    return body

files = []
for n, ch in enumerate(chapters, 1):
    fname = 'ch%02d.xhtml' % n
    files.append(('Text/' + fname, xhtml(ch['title'], ''.join(render(ch['blocks'], fname)))))

# 巻末の注釈。注釈の頭に、参照した所へ戻るリンク(複数なら↩1 ↩2…)。中身はusilicrcのその項目の
# コメント(字下げ・桁そろえを保つので等幅で1行ずつ)、HELP.notesに書いてあればその文。
asides = []
for item, nid in sorted(notes.items(), key=lambda kv: int(kv[1][1:])):
    refs = backrefs[nid]
    back = ' '.join('<a epub:type="noteref" href="%s#%s">↩%s</a>' % (f, r, k + 1 if len(refs) > 1 else '')
                    for k, (f, r) in enumerate(refs))
    head = '<p class="nh">%s <i>usilicrc</i> の <b>%s</b></p>\n' % (back, html.escape(item))
    if item in written:
        paras = re.split(r'\n\s*\n', '\n'.join(written[item]).strip())
        inner = ''.join('<p>%s</p>\n' % html.escape(t).replace('\n', '<br/>') for t in paras if t.strip())
    else:
        lines_ = rc_comments[item]
        while lines_ and not lines_[-1].strip(): lines_ = lines_[:-1]
        inner = ''.join('<p class="rc">%s</p>\n' % (inline(l.replace('\\', '\\e'), 0) or '\u00a0') for l in lines_)
    asides.append('<aside id="%s" epub:type="footnote">\n%s%s</aside>\n' % (nid, head, inner))
if asides:
    chapters.append({'title': '注釈', 'blocks': [], 'subs': [], 'file': 'notes'})
    files.append(('Text/notes.xhtml', xhtml('注釈', '<h2 id="top">注釈</h2>\n' + ''.join(asides))))

css = ['html { writing-mode: horizontal-tb; }',
       'body { font-family: serif; line-height: 1.7; margin: 0 1em; }',
       'h2 { font-size: 140%; font-family: sans-serif; margin: 1em 0 0.5em; }',
       'h3 { font-size: 115%; font-family: sans-serif; margin: 1em 0 0.3em; padding-left: 2em; }',
       'p { margin: 0; }',
       'p.nh { margin-top: 1.2em; }',
       'aside { margin: 0 0 1em; }',
       'p.rc { font-family: "Osaka-Mono", "MS Gothic", "Noto Sans Mono CJK JP", monospace; padding-left: 1em; }',
       'p.pre { font-family: "Osaka-Mono", "MS Gothic", "Noto Sans Mono CJK JP", monospace; }']
for name, (i, f) in sorted(classes.items(), key=lambda kv: kv[1]):
    css.append('p.%s { padding-left: %gem; text-indent: %gem; }' % (name, i * 0.5, (f - i) * 0.5))
files.append(('Style/style.css', '\n'.join(css) + '\n'))

navitems = []
for n, ch in enumerate(chapters, 1):
    item = '<li><a href="%s.xhtml#top">%s</a>' % (ch.get('file', 'ch%02d' % n), html.escape(ch['title']))
    if ch['subs']:
        item += '\n<ol>' + ''.join('<li><a href="ch%02d.xhtml#s%d">%s</a></li>' % (n, k, html.escape(t))
                                   for k, t in enumerate(ch['subs'])) + '</ol>\n'
    navitems.append(item + '</li>\n')
nav = xhtml('目次', '<nav epub:type="toc" id="toc">\n<h2>目次</h2>\n<ol>\n%s</ol>\n</nav>\n' % ''.join(navitems))
files.append(('Text/nav.xhtml', nav))

th = re.search(r'^\.TH\s+(\S+)\s+(\S+)\s+"([^"]*)"\s+"([^"]*)"', '\n'.join(lines), re.M)
date = plain(th.group(3)) if th else ''
version = plain(th.group(4)) if th else 'usilic'
modified = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
manifest = ['<item id="nav" href="Text/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>',
            '<item id="css" href="Style/style.css" media-type="text/css"/>']
spine = []  # navは入れない(目次のページは出さない)
for n, ch in enumerate(chapters, 1):
    f = ch.get('file', 'ch%02d' % n)
    manifest.append('<item id="%s" href="Text/%s.xhtml" media-type="application/xhtml+xml"/>' % (f, f))
    spine.append('<itemref idref="%s"/>' % f)
opf = '''<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id" xml:lang="ja">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="id">urn:uuid:%s</dc:identifier>
<dc:title>usilic HELP</dc:title>
<dc:creator>誌璃空ユウ</dc:creator>
<dc:language>ja</dc:language>
<dc:description>%s %s(doc/HELP.ja.1から変換)</dc:description>
<meta property="dcterms:modified">%s</meta>
</metadata>
<manifest>
%s
</manifest>
<spine page-progression-direction="ltr">
%s
</spine>
</package>
''' % (uuid.uuid5(uuid.NAMESPACE_URL, 'usilic-help'), version, date, modified, '\n'.join(manifest), '\n'.join(spine))
files.append(('content.opf', opf))
container = '''<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
'''
with zipfile.ZipFile(out, 'w') as z:
    z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
    z.writestr('META-INF/container.xml', container, compress_type=zipfile.ZIP_DEFLATED)
    for name, data in files:
        z.writestr('OEBPS/' + name, data, compress_type=zipfile.ZIP_DEFLATED)
print('%d chapters, %d blocks, %d indent classes, %d notes (%d refs) -> %s' % (
    len(chapters), len(blocks), len(classes), len(notes), sum(len(v) for v in backrefs.values()), out))
for t, nid in notes.items():
    print('  note', nid, t, '(HELP.notes)' if t in written else '(usilicrcのコメント %d行)' % len(rc_comments[t]), [r for _, r in backrefs[nid]])
for ch in chapters:
    print(' ', ch['title'], '/', len(ch['subs']))
