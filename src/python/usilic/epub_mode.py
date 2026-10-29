"""EPUBの本(C++のfile_mode.cppのEPUBの読み込みとepub_to_help_markup.cppの、CUIの分)。

container.xml→OPF→spineの順にXHTMLを読み、段落(p・h1〜h6・li等)ごとに行にする。太字(b・strong・h1〜h6・CSSの
font-weight)・下線(i・em・a)の属性を付ける。ルビは「親字《ルビ》」、画像は「[image: 名前]」(C++版の表示と同じ)。
目次はnav.xhtml(無ければtoc.ncx、それも無ければspineの各ファイルの見出し)。

組版の指定(C++のEpubLayoutHints、EpubHints): 本文のファイルごとに、そのファイルが読み込むCSS(<link rel="stylesheet">・
<style>)の単純なセレクタの規則から、要素ごとに縦横(writing-mode)・text-align・行頭側の余白(縦書きはpadding-top、横書きは
padding-left)・text-indent・display:noneを決め、段落の最初の字の時点の値を段落に持たせる。縦書きの本の中の横書きの範囲
(奥付等)・扉(横書きの中の縦書きの箱)・下端寄せ・中央寄せ・字下げ・注釈の区画(epub:typeと、本文と双方向のリンク)。
"""
import math
import posixpath
import re
import zipfile
from dataclasses import dataclass, field
from html.parser import HTMLParser
from urllib.parse import unquote

from . import utf8
from .content_view import Line, wrap_text

_BLOCKS = {'p', 'div', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li', 'blockquote', 'section', 'aside', 'tr', 'dt',
           'dd', 'figure', 'figcaption', 'header', 'footer', 'article', 'nav', 'pre', 'table', 'ol', 'ul', 'dl',
           'body', 'hr', 'caption', 'main', 'address'}
_HEADINGS = {'h1', 'h2', 'h3', 'h4', 'h5', 'h6'}
_BOLD = {'b', 'strong', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'th'}
_ITALIC = {'i', 'em', 'cite'}  # 斜体(C++のchangeStyle(italic_))。下線はリンクだけ(_is_underlined_link())
_SKIP = {'head', 'script', 'style', 'rp', 'title'}
_VOID = {'br', 'img', 'image', 'hr', 'link', 'meta', 'input', 'col', 'area', 'base', 'wbr', 'source'}
HR_TEXT = '-' * 40  # 区切り線(中央に置く、C++と同じ)


# 本文の中の小説サイトの名前は、<a>の外ならそのサイトへのリンクにする(C++のepubNovelSiteLinks())。
NOVEL_SITE_LINKS = (('小説家になろう', 'https://syosetu.com/'), ('小説を読もう', 'https://yomou.syosetu.com/'),
                    ('ノクターンノベルズ', 'https://noc.syosetu.com/'), ('カクヨム', 'https://kakuyomu.jp/'),
                    ('青空文庫', 'https://www.aozora.gr.jp/'), ('Archive of Our Own', 'https://archiveofourown.org/'))


def _is_underlined_link(href):
    """下線を付けるリンク(C++: http(s)と、本の中への物。他の「://」・mailto:は付けない)。"""
    if not href:
        return False
    if href.startswith(('http://', 'https://')):
        return True
    return '://' not in href and not href.startswith('mailto:')


def _round(x):
    """C++のstd::lround()(0.5は0から遠い方へ)。"""
    return int(math.floor(abs(x) + 0.5)) * (1 if x >= 0 else -1)


# ---- CSS(C++のparseCssFontRules()の、CUIで使う分) ----

@dataclass
class CssRule:
    tag: str
    classes: list
    props: dict  # vertical・align_end・center・pad_top・pad_left・text_indent・hidden・bold


def _em(value):
    """「1em」「-1em」「0」(emか0だけ、他の単位は読まない)。"""
    v = value.strip()
    if v == '0':
        return 0.0
    m = re.fullmatch(r'([-+]?[0-9]*\.?[0-9]+)em', v)
    return float(m.group(1)) if m else None


def _writing_mode(value):
    if 'vertical' in value:
        return True
    if 'horizontal' in value:  # 知らない値(Kbt.epubの"hirozontal-tb"等の綴り違い)は無視
        return False
    return None


def _align_end(value):
    if 'right' in value or 'end' in value:
        return True
    return False if any(v in value for v in ('left', 'start', 'center', 'justify')) else None


def _align_center(value):
    if 'center' in value:
        return True
    return False if any(v in value for v in ('left', 'start', 'right', 'end', 'justify')) else None


def _font_weight(value):
    v = value.strip()
    if not v:
        return None
    if 'bold' in v:
        return True
    if v[0].isdigit():
        return int(re.match(r'\d+', v).group(0)) >= 600
    return False


def parse_css(css):
    """CSSから規則の並び(単純なセレクタ「タグ」「.クラス」「タグ.クラス」だけ、頭のhtml/bodyは外す)。"""
    css = re.sub(r'/\*.*?(\*/|$)', '', css, flags=re.S)
    rules, pos = [], 0
    while pos < len(css):
        open_at = css.find('{', pos)
        if open_at < 0:
            break
        selectors = css[pos:open_at].strip()
        if ';' in selectors:  # "@charset ...;"等の文を読み飛ばす
            selectors = selectors[selectors.rfind(';') + 1:].strip()
        depth, close = 1, open_at + 1
        while close < len(css) and depth > 0:
            depth += {'{': 1, '}': -1}.get(css[close], 0)
            close += 1
        body = css[open_at + 1:close - 1]
        pos = close
        if selectors.startswith('@'):
            if selectors.startswith('@media'):
                rules.extend(parse_css(body))
            continue
        props = {}
        for decl in body.split(';'):  # 同じ規則の中では後の宣言が勝つ
            name, colon, value = decl.partition(':')
            if not colon:
                continue
            name, value = name.strip().lower(), value.strip()
            readers = []
            if 'writing-mode' in name:
                readers.append(('vertical', _writing_mode))
            if 'text-align' in name:
                readers += [('align_end', _align_end), ('center', _align_center)]
            lengths = {'padding-top': 'pad_top', 'padding-left': 'pad_left', 'text-indent': 'text_indent'}
            if name in lengths:  # "-webkit-padding-top"等には当てない
                readers.append((lengths[name], _em))
            if name == 'display':
                readers.append(('hidden', lambda v: ('none' in v) if v.strip() else None))
            if 'font-weight' in name:
                readers.append(('bold', _font_weight))
            for key, read in readers:
                result = read(value)
                if result is not None:
                    props[key] = result
        if not props:
            continue
        for sel in selectors.split(','):
            sel = sel.strip()
            stripped = True
            while stripped:  # "html body { writing-mode: horizontal-tb }"(K3t.epub)は最後の部分と同じ
                stripped = False
                for outer in ('html', 'body'):
                    if sel.startswith(outer) and len(sel) > len(outer):
                        rest = sel[len(outer):]
                        nxt = len(rest) - len(rest.lstrip(' \t\n>'))
                        if nxt == 0 or nxt == len(rest):
                            continue
                        sel, stripped = rest[nxt:], True
            if not sel or any(c in sel for c in ' \t\n>+~:[#'):
                continue
            parts = sel.split('.')
            tag = '' if parts[0] == '*' else parts[0]
            rules.append(CssRule(tag, parts[1:], props))
    return rules


def rule_for(rules, tag, classes, key):
    """要素に当てはまりkeyを指定している規則のうち、詳しさ(クラスの数、次にタグ)が最も高いもの、同じなら後の物。"""
    best, score = None, -1
    for rule in rules:
        if key not in rule.props or (rule.tag and rule.tag != tag) or not all(c in classes for c in rule.classes):
            continue
        s = len(rule.classes) * 10 + (1 if rule.tag else 0)
        if s >= score:
            best, score = rule.props[key], s
    return best


@dataclass
class _Style:
    """要素ごとの状態(C++のBodyConverterのfontStack_の1つ分)。"""
    vertical: object = None       # writing-mode(None: 指定無し)
    outside: bool = False         # 外側の要素のどれかが横書き(扉の縦書きの箱の見分け)
    align_end: bool = False
    center: bool = False
    pad_top: float = 0.0          # 行頭側の余白(ブロックの中の行すべて、足していく)
    pad_left: float = 0.0
    text_indent: float = 0.0      # 段落の最初の行のずれ(継承)
    bold: bool = False


def is_note_type(value):
    """epub:typeの語に注釈そのもの(footnote・endnote・rearnote・note)があるか(noterefやendnotesは違う)。"""
    return any(w in ('footnote', 'endnote', 'rearnote', 'note') for w in (value or '').split())


class Block:
    def __init__(self, tag, classes):
        self.tag = tag
        self.classes = classes
        self.pieces = []          # (文字列, 属性, href)
        self.anchors = []         # このブロックの中のid
        self.pre = tag == 'pre'  # C++のpreDepth_と同じく<pre>の要素だけ(class="pre"は字の形だけ)
        self.styled = False       # 最初の字の時点の状態を持たせたか
        self.kind = 'normal'      # 'horizontal'(縦書きの本の中の横書き)・'centered'(扉)
        self.align_end = False
        self.center = False
        self.indent_em = (0, 0)       # 縦書きの列の頭の空き(1行目, 2行目から)
        self.indent_columns = (0, 0)  # 横書きの字下げの桁(半角の数)
        self.chapter = False      # 章の行(C++の行末のタブ): 見出しと、目次が指す所の字の行
        self.hidden_label = None  # この段落の前の隠した見出しの字(章の一覧の名前、C++のchapterLabel)

    def text(self):
        return ''.join(p[0] for p in self.pieces)


class _XhtmlParser(HTMLParser):
    """XHTMLを段落(Block)の並びに。rulesはこのファイルが読み込むCSSの規則、book_verticalは本全体の向き。"""

    def __init__(self, rules=(), book_vertical=False):
        super().__init__(convert_charrefs=True)
        self.rules = list(rules)
        self.book_vertical = book_vertical
        self.blocks = []
        self.stack = []       # (tag, classes, _Style)
        self.current = None
        self.skip = 0
        self.ruby = []        # ルビの読み(rtの中の字)
        self.in_rt = 0
        self.linking_site = False  # _add()が小説サイトの名前をリンクにしている間
        self.pending_anchors = []
        self.hrefs = []       # 開いている<a href>の並び
        self.hidden = None    # display:noneの要素(tag, 入れ子の数, 見出しか)
        self.hidden_text = []
        self.hidden_label = None  # 隠した見出しの字(章の名前)
        self.next_hidden_label = None  # 次の段落に持たせる隠した見出しの字
        self.block_frames = []    # (tag, 始まりのブロック, 注釈か, [id])
        self.noterefs = []        # 開いている<a>ごとに注釈へのリンクか(太字にする)
        self.tables = []          # 開いている表ごとの[(行のBlock, [(欄の頭, 終わり)のpiecesの番号])]
        self.row_cells = []       # 今の行(<tr>)の欄
        self.anchor_blocks = {}   # id→そのidを持つ(か囲む一番内側の)ブロックの範囲(ブロックの番号)
        self.note_ranges = []

    def style(self):
        return self.stack[-1][2] if self.stack else _Style()

    @staticmethod
    def _classes(attrs):
        return set((dict(attrs).get('class') or '').split())

    def _start_block(self, tag, classes):
        self._end_block()
        self.current = Block(tag, classes)
        self.current.anchors.extend(self.pending_anchors)
        self.pending_anchors = []

    def _end_block(self):
        """字の無いブロックは行にしない(idは次に字の出る段落へ、C++と同じ)。空の見出しだけは目次の名前の行のために残す
        (EpubBook._with_toc_labels())。"""
        block, self.current = self.current, None
        if block is None:
            return
        if block.pieces and not block.pre and not block.text().strip(' '):
            block.pieces = []  # 空白だけ(要素の間の改行等)は字の無いブロック
        if block.pieces or (block.anchors and block.tag in _HEADINGS):
            if block.pieces and self.next_hidden_label:
                block.hidden_label, self.next_hidden_label = self.next_hidden_label, None
            if block.tag in _HEADINGS and self.blocks and self.blocks[-1].pieces:
                self.blocks.append(Block('p', set()))  # 見出しの前は空行(C++のensureBlankLine())
            self.blocks.append(block)
        elif block.anchors:
            self.pending_anchors = block.anchors + self.pending_anchors

    def _vertical_now(self):
        """今の要素が縦書きか(CSSのwriting-mode、指定が無ければ本全体の向き)。"""
        vertical = self.style().vertical
        return vertical if vertical is not None else self.book_vertical

    def _attr(self):
        """今の字の属性(太字・斜体・下線を重ねて、C++のbold_・italic_・underline_)。無ければNone。"""
        names = []
        if any(t in _BOLD for t, _, _ in self.stack) or self.style().bold or any(self.noterefs):
            names.append('bold')
        if any(t in _ITALIC for t, _, _ in self.stack):
            names.append('italic')
        if any(_is_underlined_link(h) for h in self.hrefs) or any(t == 'u' for t, _, _ in self.stack):
            names.append('underline')  # <u>も下線(HELP.epubの注釈の付く名前、C++と同じ)
        if any(self.noterefs):
            names.append('note')  # 本文の注釈番号: 決まった色(C++の<note>)
        return tuple(names) or None

    def _apply_style(self, block):
        """段落の最初の字の時点の状態(C++のtext()のlineKind_・startLineIndent())。"""
        st = self.style()
        block.styled = True
        block.kind = 'horizontal' if st.vertical is False else 'centered' if st.vertical and st.outside else 'normal'
        block.align_end, block.center = st.align_end, st.center
        vertical = st.vertical if st.vertical is not None else self.book_vertical
        padding = st.pad_top if vertical else st.pad_left
        block.indent_em = (max(0, _round(padding + st.text_indent)), max(0, _round(padding)))
        block.indent_columns = (max(0, _round((padding + st.text_indent) * 2)), max(0, _round(padding * 2)))

    def _add(self, text, attr=None):
        if attr is None and not any(self.hrefs) and not self.linking_site and not self.in_rt and \
                not any(t in ('ruby', 'pre') for t, _, _ in self.stack):  # C++のpreDepth_は<pre>の要素だけ
            found = min(((text.find(name), name, url) for name, url in NOVEL_SITE_LINKS if name in text), default=None)
            if found:  # 最初に出てくる名前から(C++のtext())
                at, name, url = found
                self.linking_site = True
                if at:
                    self._add(text[:at])
                self.hrefs.append(url)
                self._add(name)
                self.hrefs.pop()
                self.linking_site = False
                if text[at + len(name):]:
                    self._add(text[at + len(name):])
                return
        if self.current is None:
            parent = next(((t, c) for t, c, _ in reversed(self.stack) if t in _BLOCKS), ('p', set()))
            self._start_block(parent[0], parent[1])
        if not self.current.styled and text.strip(' '):  # NBSPは字として残す(C++と同じ、行頭でも詰めない)
            self._apply_style(self.current)
        href = next((h for h in reversed(self.hrefs) if h), None)
        self.current.pieces.append((text, attr if attr is not None else self._attr(), href))

    def _push(self, tag, attrs):
        """要素の状態を積む(C++のhandle()の最後、fontStack_)。"""
        parent = self.style()
        classes = self._classes(attrs)
        st = _Style(parent.vertical, parent.outside or parent.vertical is False, parent.align_end, parent.center,
                    parent.pad_top, parent.pad_left, parent.text_indent, parent.bold)

        def rule(key):
            return rule_for(self.rules, tag, classes, key)
        for key in ('vertical', 'align_end', 'center', 'text_indent'):
            value = rule(key)
            if value is not None:
                setattr(st, key, value)
        st.pad_top += rule('pad_top') or 0.0
        st.pad_left += rule('pad_left') or 0.0
        if rule('bold'):
            st.bold = True
        self.stack.append((tag, classes, st))

    def handle_starttag(self, tag, attrs):
        if self.hidden is not None:
            if tag == self.hidden[0] and tag not in _VOID:
                self.hidden = (self.hidden[0], self.hidden[1] + 1, self.hidden[2])
            return
        if tag in _SKIP:
            self.skip += 1
            return
        if self.skip:
            return
        a = dict(attrs)
        if tag not in _VOID and rule_for(self.rules, tag, self._classes(attrs), 'hidden'):  # display:none
            self.hidden = (tag, 1, tag in _HEADINGS)
            self.hidden_text = []
            return
        if a.get('id') and not (tag in _BLOCKS and tag not in _VOID):  # ブロックのidは始めてから(下)
            if self.current is not None:
                self.current.anchors.append(a['id'])
            else:
                self.pending_anchors.append(a['id'])
            if tag not in _BLOCKS and self.block_frames:
                self.block_frames[-1][3].append(a['id'])
        if tag in ('br',):
            if self.current is not None:
                tag0, classes = self.current.tag, self.current.classes
                had = bool(self.current.pieces)
                self._end_block()
                if not had:  # 空の段落の<br/>は空の行(HELPの「blank」)
                    self.blocks.append(Block(tag0, classes))
                self.current = Block(tag0, classes)
            else:
                self.blocks.append(Block('p', set()))
            return
        if tag in ('img', 'image'):
            src = a.get('src') or a.get('xlink:href') or a.get('href') or ''
            self._add('[image: %s]' % posixpath.basename(unquote(src)))
            return
        if tag == 'hr':
            self._start_block('hr', set())
            self._add(HR_TEXT)
            self.current.center = True  # 区切り線は中央に置く(縦横は今の要素に従う)
            self._end_block()
            return
        if tag in _VOID:
            return
        if tag == 'rt':
            self.in_rt += 1
        if tag == 'a':
            noteref = 'noteref' in (a.get('epub:type') or a.get('type') or '')
            if noteref:  # 注釈番号は括弧で囲む(括弧はリンクの外、縦書きは全角、C++と同じ)
                self._add('［' if self._vertical_now() else '[')
            self.hrefs.append(a.get('href'))
            # 本文の注釈へのリンク(epub:type="noteref")の字は太字(C++のnoterefOpen_)
            self.noterefs.append(noteref)
        if tag == 'table':
            self.tables.append([])
        if tag in ('td', 'th') and self.tables and self.current is not None:  # 欄の頭(_finish_table()でそろえる)
            self.row_cells.append([len(self.current.pieces), len(self.current.pieces)])
        if tag == 'tr':
            self.row_cells = []
        if tag in _BLOCKS:
            self._start_block(tag, self._classes(attrs))
            if a.get('id'):
                self.current.anchors.append(a['id'])
            self.block_frames.append((tag, len(self.blocks), is_note_type(a.get('epub:type') or a.get('type')),
                                      [a['id']] if a.get('id') else []))
        self._push(tag, attrs)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in _VOID and tag not in _SKIP and self.hidden is None:
            self.handle_endtag(tag)
        elif tag in _SKIP:
            self.skip = max(0, self.skip - 1)

    def handle_endtag(self, tag):
        if self.hidden is not None:
            if tag == self.hidden[0]:
                nest = self.hidden[1] - 1
                if nest == 0:
                    label = re.sub(r'[ \t\r\n]+', ' ', ''.join(self.hidden_text)).strip(' ')  # 全角の空白は字のまま
                    if self.hidden[2] and label:
                        self.hidden_label = self.next_hidden_label = label  # 隠した見出しの字は次の章の行の名前に
                    self.hidden = None
                else:
                    self.hidden = (self.hidden[0], nest, self.hidden[2])
            return
        if tag in _SKIP:
            self.skip = max(0, self.skip - 1)
            return
        if self.skip:
            return
        if tag == 'rt':
            self.in_rt = max(0, self.in_rt - 1)
            if self.ruby:
                self._add('《' + ''.join(self.ruby) + '》')
                self.ruby = []
        if tag == 'a' and self.hrefs:
            self.hrefs.pop()
            if self.noterefs and self.noterefs.pop():
                self._add('］' if self._vertical_now() else ']')
        if tag in ('td', 'th') and self.tables and self.row_cells and self.current is not None:
            self.row_cells[-1][1] = len(self.current.pieces)
        if tag == 'tr' and self.tables and self.current is not None and self.row_cells:
            self.tables[-1].append((self.current, self.row_cells))
            self.row_cells = []
        if tag in _BLOCKS:
            self._end_block()
            if tag == 'table' and self.tables:
                _finish_table(self.tables.pop())
            for i in range(len(self.block_frames) - 1, -1, -1):
                if self.block_frames[i][0] == tag:
                    _, start, note, ids = self.block_frames.pop(i)
                    end = len(self.blocks) - 1
                    if end >= start:
                        for anchor in ids:
                            self.anchor_blocks.setdefault(anchor, (start, end))
                        if note:
                            self.note_ranges.append((start, end))
                    break
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i][0] == tag:
                del self.stack[i:]
                break

    def handle_data(self, data):
        if self.hidden is not None:
            if self.hidden[2]:
                self.hidden_text.append(data)
            return
        if self.skip:
            return
        if self.in_rt:
            self.ruby.append(data.strip())
            return
        pre = self.current.pre if self.current is not None else any(t == 'pre' for t, _, _ in self.stack)
        if not pre:
            data = re.sub(r'[ \t\r\n]+', ' ', data)
            if self.current is None and not data.strip(' '):  # NBSPだけの段落は空行として残す(str.strip()はNBSPも取る)
                return
        self._add(data)

    def close(self):
        super().close()
        self._end_block()


def _table_width(pieces):
    """欄の見た目の幅(全角は2、ルビの読み《…》は数えない、C++のmarkupWidth())。"""
    return utf8.text_width(re.sub(r'《[^》]*》', '', ''.join(p[0] for p in pieces)))


def _finish_table(rows):
    """表(HTMLの<table>)の欄の幅をそろえる(C++のfinishTable()、KDPのガイドラインの範囲で罫線・色は足さない):
    欄ごとに一番広い幅へ全角の空白(奇数なら半角の空白も)で埋め、欄の間に全角の空白を1つ。欄の外の空白は出さない。"""
    widths = []
    for block, cells in rows:
        for i, (start, end) in enumerate(cells):
            if len(widths) <= i:
                widths.append(0)
            widths[i] = max(widths[i], _table_width(block.pieces[start:end]))
    for block, cells in rows:
        pieces = []
        for i, (start, end) in enumerate(cells):
            cell = block.pieces[start:end]
            if i > 0:
                pieces.append(('\u3000', None, None))
            pieces.extend(cell)
            if i + 1 < len(cells):
                pad = widths[i] - _table_width(cell)
                if pad > 0:
                    pieces.append(('\u3000' * (pad // 2) + ' ' * (pad % 2), None, None))
        block.pieces = pieces


def _stylesheets(xhtml):
    """本文のファイルが読み込むCSS: (<link rel="stylesheet">のhrefの並び, <style>の中身の並び)。"""
    links = []
    for tag in re.findall(r'<link\b[^>]*>', xhtml, re.I):
        attrs = dict((k.lower(), v) for k, v in re.findall(r'([\w:-]+)\s*=\s*["\']([^"\']*)["\']', tag))
        if 'stylesheet' in attrs.get('rel', '') and attrs.get('href'):
            links.append(attrs['href'])
    styles = re.findall(r'<style\b[^>]*>(.*?)</style>', xhtml, re.I | re.S)
    return links, styles


def _is_cover_page(xhtml):
    """本文(<body>の中)に字が無く、画像(<img>・SVGの<image>)だけのページか。"""
    body = xhtml.find('<body')
    if body < 0:
        return False
    rest = xhtml[body:]
    image = re.search(r'<(img|image|svg:image)\b', rest) is not None
    return image and not re.sub(r'<[^>]*>', '', rest).strip()


class _NavParser(HTMLParser):
    """nav.xhtmlの目次(toc)の(href, 見出し, 深さ)。"""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.entries, self.depth, self.in_toc, self.nav_depth = [], 0, False, 0
        self.href, self.text = None, []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == 'nav':
            self.nav_depth += 1
            if a.get('epub:type', '') == 'toc' or a.get('id') == 'toc' or not self.entries:
                self.in_toc = True
        elif self.in_toc and tag == 'ol':
            self.depth += 1
        elif self.in_toc and tag == 'a':
            self.href, self.text = a.get('href', ''), []

    def handle_endtag(self, tag):
        if tag == 'nav':
            self.in_toc = False
        elif self.in_toc and tag == 'ol':
            self.depth -= 1
        elif self.in_toc and tag == 'a' and self.href is not None:
            self.entries.append((self.href, re.sub(r'\s+', ' ', ''.join(self.text)).strip(), max(0, self.depth - 1)))
            self.href = None

    def handle_data(self, data):
        if self.href is not None:
            self.text.append(data)


@dataclass
class EpubHints:
    """組版の指定(C++のEpubLayoutHints)。行は元の行の番号(段落の番号、1から)。"""
    horizontal: list = field(default_factory=list)   # 縦書きの本の中の横書きの範囲(両端を含む)
    centered: list = field(default_factory=list)     # 扉(横書きの中の縦書きの箱)の範囲
    align_end: set = field(default_factory=set)      # 下端寄せ(text-align: right/end)
    center: set = field(default_factory=set)         # 中央寄せ(text-align: center、区切り線)
    indent: dict = field(default_factory=dict)       # 元の行→(1行目, 2行目から)の字下げ(字の数)
    notes: list = field(default_factory=list)        # 注釈の区画の範囲
    chapter_files: list = field(default_factory=list)  # (章の頭の元の行, 書庫の中のパス)


class EpubBook:
    def __init__(self):
        self.path = ''
        self.title = ''
        self.creator = ''
        self.spine = []        # 書庫の中のパス
        self.documents = {}    # パス -> [Block]
        self.anchor_blocks = {}  # パス -> {id: (始まり, 終わり)}(ブロックの番号)
        self.note_blocks = {}    # パス -> [(始まり, 終わり)]
        self.hidden_labels = {}  # パス -> 隠した見出しの字
        self.toc = []          # (パス#id, 見出し, 深さ)
        self.right_to_left = False
        self.cover_first = False  # 最初のページが画像だけ(表紙): 書名・著者名はその後
        self.hints = EpubHints()

    def load(self, path):
        """読めなければFalse(壊れたzip、OPFが無い等)。"""
        try:
            with zipfile.ZipFile(path) as z:
                names = set(z.namelist())
                container = z.read('META-INF/container.xml').decode('utf-8', 'replace')
                m = re.search(r'full-path="([^"]+)"', container)
                if not m:
                    return False
                opf_path = m.group(1)
                opf = z.read(opf_path).decode('utf-8', 'replace')
                base = posixpath.dirname(opf_path)
                manifest, nav_href, ncx_href = {}, None, None
                for item in re.findall(r'<item\b[^>]*>', opf):
                    attrs = dict(re.findall(r'([\w:-]+)="([^"]*)"', item))
                    href = posixpath.normpath(posixpath.join(base, unquote(attrs.get('href', ''))))
                    manifest[attrs.get('id')] = href
                    if 'nav' in attrs.get('properties', '').split():
                        nav_href = href
                    if attrs.get('media-type') == 'application/x-dtbncx+xml':
                        ncx_href = href

                def meta(name):
                    found = re.search(r'<dc:%s[^>]*>(.*?)</dc:%s>' % (name, name), opf, re.S)
                    # 全角の空白は字のまま(C++のcollapseSpace()は半角の空白類だけ)
                    return re.sub(r'[ \t\r\n]+', ' ', re.sub(r'<[^>]+>', '', found.group(1))).strip(' ') if found else ''
                self.title, self.creator = meta('title'), meta('creator')
                self.right_to_left = bool(re.search(r'page-progression-direction="rtl"', opf))
                self.spine = [manifest[i] for i in re.findall(r'<itemref\b[^>]*idref="([^"]+)"', opf)
                              if manifest.get(i) in names]
                css_cache = {}
                for n, href in enumerate(self.spine):
                    xhtml = z.read(href).decode('utf-8', 'replace')
                    if n == 0:
                        self.cover_first = _is_cover_page(xhtml)
                    links, styles = _stylesheets(xhtml)
                    rules = []
                    for link in links:
                        css_path = self._resolve(posixpath.dirname(href), link).partition('#')[0]
                        if css_path not in css_cache:
                            css_cache[css_path] = parse_css(z.read(css_path).decode('utf-8', 'replace')) \
                                if css_path in names else []
                        rules += css_cache[css_path]
                    for style in styles:
                        rules += parse_css(style)
                    parser = _XhtmlParser(rules, self.right_to_left)
                    parser.feed(xhtml)
                    parser.close()
                    self.documents[href] = parser.blocks
                    self.anchor_blocks[href] = parser.anchor_blocks
                    self.note_blocks[href] = parser.note_ranges
                    if parser.hidden_label:
                        self.hidden_labels[href] = parser.hidden_label
                self.toc = self._read_toc(z, nav_href if nav_href in names else None,
                                          ncx_href if ncx_href in names else None)
        except (OSError, KeyError, zipfile.BadZipFile, UnicodeError):
            return False
        self.path = path
        return bool(self.spine)

    def _read_toc(self, z, nav_href, ncx_href):
        if nav_href:
            parser = _NavParser()
            parser.feed(z.read(nav_href).decode('utf-8', 'replace'))
            base = posixpath.dirname(nav_href)
            entries = [(self._resolve(base, h), t, d) for h, t, d in parser.entries if t]
            if entries:
                return entries
        if ncx_href:
            ncx = z.read(ncx_href).decode('utf-8', 'replace')
            base = posixpath.dirname(ncx_href)
            entries = []
            for label, src in re.findall(r'<navLabel>\s*<text>(.*?)</text>\s*</navLabel>\s*<content\s+src="([^"]+)"',
                                         ncx, re.S):
                entries.append((self._resolve(base, src), re.sub(r'\s+', ' ', label).strip(), 0))
            if entries:
                return entries
        out = []
        for href in self.spine:
            heading = next((b.text().strip() for b in self.documents[href] if b.tag in ('h1', 'h2', 'h3')), '')
            out.append((href, heading or self.hidden_labels.get(href) or posixpath.basename(href), 0))
        return out

    @staticmethod
    def _resolve(base, href):
        path, _, fragment = href.partition('#')
        path = posixpath.normpath(posixpath.join(base, unquote(path))) if path else ''
        return path + ('#' + fragment if fragment else '')

    def _with_toc_labels(self, href, blocks):
        """目次の項目が指す所に字が無ければ、目次の名前(太字)の行にする。空の見出しの次が画像だけの段落なら、
        「表紙 [image: title.jpg]」の1行にまとめる(C++のBodyConverterのpendingLabel_・deferredLabel_)。"""
        labels, pending = {}, None
        for target, label, _ in self.toc:
            path, _, fragment = target.partition('#')
            if path == href and label:
                if fragment:
                    labels.setdefault(fragment, label)
                elif pending is None:
                    pending = label
        out, i, index_map, carried = [], 0, {}, []
        while i < len(blocks):
            index_map[i] = len(out)
            block = blocks[i]
            if carried:  # 行にしなかった空の見出しのidは、この段落へ
                copy = Block(block.tag, block.classes)
                copy.__dict__.update(block.__dict__)
                copy.anchors = carried + block.anchors
                block, carried = copy, []
            for anchor in block.anchors:
                pending = labels.get(anchor, pending)
            if block.text().strip(' ') or pending is None or (block.tag not in _HEADINGS and
                                                                not any(a in labels for a in block.anchors)):
                if block.text().strip(' ') and (pending is not None or block.tag in _HEADINGS):
                    copy = Block(block.tag, block.classes)  # 章の行(見出し、目次が指す所の最初の字の行)
                    copy.__dict__.update(block.__dict__)
                    copy.chapter, block = True, copy
                if block.text().strip(' '):
                    pending = None
                if not block.pieces and block.tag in _HEADINGS:  # 名前の無い空の見出し: 行にしない
                    carried = block.anchors
                    i += 1
                    continue
                out.append(block)
                i += 1
                continue
            nxt = blocks[i + 1] if i + 1 < len(blocks) else None
            image_only = nxt is not None and re.fullmatch(r'(\[image: [^\]]*\] ?)+', nxt.text().strip(' '))
            merged = Block(nxt.tag if image_only else block.tag, nxt.classes if image_only else block.classes)
            merged.pieces = [(pending, 'bold', None)] + ([(' ', None, None)] + nxt.pieces if image_only else [])
            merged.anchors = block.anchors + (nxt.anchors if image_only else [])
            # 目次の名前の行の縦横等は、その所の状態(C++のcurrentKind())=続く段落の物
            source = nxt if nxt is not None and nxt.styled else block
            for name in ('styled', 'kind', 'align_end', 'center', 'indent_em', 'indent_columns'):
                setattr(merged, name, getattr(source, name))
            merged.tag = 'h2' if not image_only else merged.tag
            merged.chapter = True  # 章の行
            merged.hidden_label = source.hidden_label
            if image_only:
                index_map[i + 1] = len(out)
            out.append(merged)
            pending = None
            i += 2 if image_only else 1
        return out, index_map

    def _header_blocks(self):
        """本の冒頭の書名(太字)・著者名と空の行(C++のepubToHelpMarkup()のheader)。"""
        blocks = []
        for text, attr in ((self.title, 'bold'), (self.creator, None)):
            if text:
                block = Block('p', set())
                block.pieces.append((text, attr, None))
                blocks.append(block)
        return blocks + [Block('p', set())] if blocks else []

    def render(self, width):
        """本全体を画面の行に。(行の並び, 目次の各項目の行の番号)を返す。組版の指定はself.hintsへ。"""
        lines, anchors, number = [], {}, 0
        hints = EpubHints()
        kinds, heading_raws, links = {}, set(), []   # 元の行→種類、章の行、(元の行, 行き先)
        anchor_raws, header = {}, (0, 0)
        documents = [(href,) + self._with_toc_labels(href, self.documents[href]) for href in self.spine]
        at = 1 if self.cover_first else 0
        if self._header_blocks():
            documents.insert(min(at, len(documents)), (None, self._header_blocks(), {}))
        pending = []  # 字の出る行を待っているアンカー(C++のresolveAnchorsHere()、空の行は指さない)
        chapters, hidden_label = [], None  # 章の一覧(行, 名前)、まだ使っていない隠した見出しの字

        def resolve(index, raw):
            for key in pending:
                if key not in anchor_raws:
                    anchors.setdefault(key[:-1] if key.endswith('#') else key, index)
                    anchor_raws[key] = raw
            pending.clear()
        for href, blocks, index_map in documents:
            if href is None:
                header = (number + 1, number + len([b for b in blocks if b.pieces]))
            else:
                pending.append(href + '#')
            first_number = number + 1
            for block in blocks:
                number += 1
                if href is not None:
                    pending.extend(href + '#' + anchor for anchor in block.anchors)
                text, runs, line_links, pos = '', [], [], 0
                base = posixpath.dirname(href or '')
                for piece, attr, link in block.pieces:
                    for name in ((attr,) if isinstance(attr, str) else attr or ()):
                        runs.append((pos, pos + len(piece), name))
                    if link:
                        target = link if re.match(r'[a-z]+:', link) else (
                            self._resolve(base, link) if not link.startswith('#') else href + link)
                        if line_links and line_links[-1][1] == pos and line_links[-1][2] == target:
                            line_links[-1] = (line_links[-1][0], pos + len(piece), target)  # ルビ等で切れた同じリンク
                        else:
                            line_links.append((pos, pos + len(piece), target))
                        if not re.match(r'[a-z]+:', target):
                            links.append((number, target))
                    text += piece
                    pos += len(piece)
                if not block.pre:
                    stripped = text.lstrip(' ')
                    shift = len(text) - len(stripped)
                    text = stripped.rstrip(' ')
                    runs = [(max(0, s - shift), max(0, e - shift), a) for s, e, a in runs]
                    line_links = [(max(0, s - shift), max(0, e - shift), h) for s, e, h in line_links]
                if text:
                    kinds[number] = block.kind
                    if block.align_end:
                        hints.align_end.add(number)
                    if block.center:
                        hints.center.add(number)
                    if block.indent_em != (0, 0):
                        hints.indent[number] = block.indent_em
                    if block.tag in _HEADINGS:
                        heading_raws.add(number)
                if text:
                    resolve(len(lines), number)
                    hidden_label = block.hidden_label or hidden_label
                    if block.chapter:
                        chapters.append((len(lines), hidden_label or text))
                        hidden_label = None
                first, left = block.indent_columns if text else (0, 0)
                wrapped = _wrap_with_indent(text, runs, width, first, left, number, line_links, center=block.center)
                if text and block.chapter and wrapped:
                    # 章の行の印(C++のepubToHelpMarkup()が行末に付けるタブ)。PipeBufferと同じく、幅0のU+2060と
                    # 次の8桁目までの空白に広げた形で行に残す(見えないが、大きさ・検索はC++と同じに)
                    last = wrapped[-1]
                    column = utf8.text_width(last.text)
                    spaces = min(8 - column % 8, width - column)
                    if spaces > 0:
                        last.text += '\u2060' + ' ' * spaces
                lines.extend(wrapped)
            if href is not None:
                def at(i):
                    return first_number + index_map.get(i, i)
                for start, end in self.note_blocks.get(href, []):
                    hints.notes.append((at(start), at(end)))
                for anchor, (start, end) in self.anchor_blocks.get(href, {}).items():
                    anchor_raws.setdefault('#blocks#' + href + '#' + anchor, (at(start), at(end)))
                if lines and lines[-1].text:  # C++と同じく、ファイルの終わりが空行でなければ空行を1つ置く
                    number += 1
                    lines.append(Line('', number))
        if lines:
            resolve(len(lines) - 1, lines[-1].raw_line_number)
        for href in self.spine:  # ファイルの頭の行(字の無いファイルは次のファイルと同じ行になるので、後の方を残す)
            if href + '#' not in anchor_raws:
                continue
            line = max(1, anchor_raws[href + '#'])
            if hints.chapter_files and hints.chapter_files[-1][0] >= line:
                hints.chapter_files[-1] = (line, href)
            else:
                hints.chapter_files.append((line, href))
        self._layout_ranges(hints, kinds, number, header)
        # 章の一覧(C++の章の印の行で分けたCUT): 章の行の字(隠した見出しがあればその字)、最初の章より前に行があれば「--」
        if chapters and chapters[0][0] > 0:
            chapters.insert(0, (0, '--'))
        toc_lines = [index for index, _ in chapters]
        self.chapter_titles = [title for _, title in chapters]
        self.anchors = anchors
        chapter_raws = {lines[i].raw_line_number for i in toc_lines if 0 <= i < len(lines)}
        self._bidirectional_notes(hints, links, anchor_raws, chapter_raws)
        self.hints = hints
        return lines, toc_lines

    def _layout_ranges(self, hints, kinds, total, header):
        """同じ種類の行が(空行をはさんでも)続く所を1つの範囲に(C++のepubToHelpMarkup()のflush())。横書きの範囲は、
        後ろの空行等も次の行の手前まで(最後なら本の最後まで)含める。縦書きの本の冒頭の書名・著者名は扉と同じ扱い。"""
        run, run_kind = None, 'normal'

        def flush():
            if run is not None and run[1] >= 1:
                (hints.horizontal if run_kind == 'horizontal' else hints.centered).append((max(1, run[0]), run[1]))
        for number in sorted(kinds):
            kind = kinds[number]
            if run is not None and kind == run_kind:
                run[1] = number
                continue
            if run is not None and run_kind == 'horizontal':
                run[1] = number - 1
            flush()
            run = None
            if kind != 'normal':
                run, run_kind = [number, number], kind
        if run is not None and run_kind == 'horizontal':
            run[1] = max(run[1], total)
        flush()
        if header[0] and header[1] >= header[0] and self.right_to_left:
            hints.centered.append(header)
            hints.centered.sort()
            if self.creator:
                hints.align_end.add(header[1])

    @staticmethod
    def _bidirectional_notes(hints, links, anchor_raws, chapter_raws):
        """KDPの注釈(本文と双方向のリンク、epub:typeが無い本もある): リンクの行き先のidを持つ(か囲む)ブロックの中に、
        元の行へ戻るリンクがあれば、そのブロックを注釈の区画にする。注釈は参照する行より後ろにある物だけで、章の行を
        含む物・40行を超える物は除く(C++のaddBidirectionalNotes())。"""
        resolved = []
        for source, target in links:
            to = anchor_raws.get(target) or anchor_raws.get(target.partition('#')[0] + '#')
            resolved.append((source, to if isinstance(to, int) else None, target))
        found = set(hints.notes)
        for source, _, target in resolved:
            block = anchor_raws.get('#blocks#' + target)
            if not block:
                continue
            first, last = block
            if last - first + 1 > 40 or first <= source:
                continue
            if any(first <= c <= last for c in chapter_raws):
                continue
            if not any(first <= s <= last and to == source for s, to, _ in resolved):
                continue
            if not any(n[0] <= last and first <= n[1] for n in found):
                found.add((first, last))
        hints.notes = sorted(found)

    @property
    def chapter_files(self):
        return self.hints.chapter_files

    def line_for(self, href):
        """本の中のリンク先(「ファイル#id」)の行。無ければNone。"""
        anchors = getattr(self, 'anchors', {})
        return anchors.get(href, anchors.get(href.partition('#')[0]))


def _wrap_with_indent(text, runs, width, first, left, number, links=(), center=False):
    """1行目はfirst桁、2行目からはleft桁あけて、幅で折り返す(空の段落は空の行)。centerなら行ごとに中央へ。"""
    from .overstrike import slice_runs
    if not text:
        return [Line('', number)]
    out, offset, rest, indent = [], 0, text, first
    queued = []  # 2行目から(字下げは同じ)は一度に折り返した残り(以前は1行ごとに残り全部を折り返し直していた)
    while rest:
        room = max(4, width - indent)
        if not queued:
            queued = wrap_text(rest, room)
            if indent == left:
                queued.reverse()
            else:
                queued = [queued[0]]
        piece = queued.pop()
        if not piece:
            piece = rest[0]
            queued = []
        pad = indent
        if center:
            pad = max(pad, (width - utf8.text_width(' ' * indent + piece)) // 2 + indent)
        attrs = [(s + pad, e + pad, a) for s, e, a in slice_runs(runs, offset, offset + len(piece))]
        line_links = [(s + pad, e + pad, h) for s, e, h in slice_runs(links, offset, offset + len(piece))]
        out.append(Line(' ' * pad + piece, number, ends_with_newline=False, attributes=attrs, links=line_links))
        offset += len(piece)
        rest = rest[len(piece):]
        indent = left
    out[-1].ends_with_newline = True
    return out


def is_epub_file(path):
    return path.lower().endswith('.epub')


def text_width(text):
    return utf8.text_width(text)
