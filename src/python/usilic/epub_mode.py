"""EPUBの本(C++のfile_mode.cppのEPUBの読み込みとepub_to_help_markup.cppの、CUIの横書きの分)。

container.xml→OPF→spineの順にXHTMLを読み、段落(p・h1〜h6・li等)ごとに行にする。字下げはCSSの
padding-left・text-indent(1em=2桁)。太字(b・strong・h1〜h6)・下線(i・em・a)の属性を付ける。ルビは
「親字《ルビ》」、画像は「[image: 名前]」(C++版の表示と同じ)。目次はnav.xhtml(無ければtoc.ncx、それも
無ければspineの各ファイルの見出し)。
"""
import posixpath
import re
import zipfile
from html.parser import HTMLParser
from urllib.parse import unquote

from . import utf8
from .content_view import Line, wrap_text

_BLOCKS = {'p', 'div', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li', 'blockquote', 'section', 'aside', 'tr', 'dt',
           'dd', 'figure', 'figcaption', 'header', 'footer', 'article', 'nav', 'pre', 'table', 'ol', 'ul', 'dl',
           'body', 'hr'}
_BOLD = {'b', 'strong', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'th'}
_UNDERLINE = {'i', 'em', 'a', 'cite', 'u'}
_SKIP = {'head', 'script', 'style', 'rp', 'title'}


class Block:
    def __init__(self, tag, classes):
        self.tag = tag
        self.classes = classes
        self.pieces = []          # (文字列, 属性)
        self.anchors = []         # このブロックの中のid
        self.pre = 'pre' in classes or tag == 'pre'

    def text(self):
        return ''.join(t for t, _ in self.pieces)


class _XhtmlParser(HTMLParser):
    """XHTMLを段落(Block)の並びに。"""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.blocks = []
        self.stack = []       # (tag, classes)
        self.current = None
        self.skip = 0
        self.ruby = []        # ルビの読み(rtの中の字)
        self.in_rt = 0
        self.pending_anchors = []

    def _classes(self, attrs):
        return set((dict(attrs).get('class') or '').split())

    def _start_block(self, tag, classes):
        self._end_block()
        self.current = Block(tag, classes)
        self.current.anchors.extend(self.pending_anchors)
        self.pending_anchors = []

    def _end_block(self):
        if self.current is not None and (self.current.pieces or self.current.anchors):
            self.blocks.append(self.current)
        self.current = None

    def _attr(self):
        bold = any(t in _BOLD for t, _ in self.stack)
        under = any(t in _UNDERLINE for t, _ in self.stack)
        return 'bold' if bold else 'underline' if under else None

    def _add(self, text, attr=None):
        if self.current is None:
            parent = next(((t, c) for t, c in reversed(self.stack) if t in _BLOCKS), ('p', set()))
            self._start_block(parent[0], parent[1])
        text = text.replace('\xa0', ' ')
        self.current.pieces.append((text, attr if attr is not None else self._attr()))

    def handle_starttag(self, tag, attrs):
        if tag in _SKIP:
            self.skip += 1
            return
        a = dict(attrs)
        if a.get('id'):
            if self.current is not None:
                self.current.anchors.append(a['id'])
            else:
                self.pending_anchors.append(a['id'])
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
            self._add('[image: %s]' % posixpath.basename(src))
            return
        if tag == 'rt':
            self.in_rt += 1
        if tag in _BLOCKS:
            self._start_block(tag, self._classes(attrs))
            if tag == 'hr':
                self._add('―' * 8)
                self._end_block()
                return
        self.stack.append((tag, self._classes(attrs)))

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in ('br', 'img', 'image', 'hr') and tag not in _SKIP:
            self.handle_endtag(tag)
        elif tag in _SKIP:
            self.skip -= 1

    def handle_endtag(self, tag):
        if tag in _SKIP:
            self.skip = max(0, self.skip - 1)
            return
        if tag == 'rt':
            self.in_rt = max(0, self.in_rt - 1)
            if self.ruby:
                self._add('《' + ''.join(self.ruby) + '》')
                self.ruby = []
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i][0] == tag:
                del self.stack[i:]
                break
        if tag in _BLOCKS:
            self._end_block()

    def handle_data(self, data):
        if self.skip:
            return
        if self.in_rt:
            self.ruby.append(data.strip())
            return
        pre = self.current.pre if self.current is not None else any('pre' in c or t == 'pre' for t, c in self.stack)
        if not pre:
            data = re.sub(r'[ \t\r\n]+', ' ', data)
            if self.current is None and not data.strip():
                return
        self._add(data)

    def close(self):
        super().close()
        self._end_block()


def _parse_css(text):
    """CSSから「要素.クラス」ごとの(padding-left, text-indent)をemで。"""
    rules = {}
    for selector, body in re.findall(r'([^{}]+)\{([^}]*)\}', text):
        props = dict((k.strip(), v.strip()) for k, _, v in
                     (p.partition(':') for p in body.split(';') if ':' in p))

        def em(name):
            m = re.match(r'(-?[0-9.]+)em', props.get(name, ''))
            return float(m.group(1)) if m else None
        left, indent = em('padding-left'), em('text-indent')
        if left is None and indent is None:
            left = em('margin-left')
        if left is None and indent is None:
            continue
        for sel in selector.split(','):
            rules[sel.strip()] = (left or 0.0, indent or 0.0)
    return rules


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


class EpubBook:
    def __init__(self):
        self.path = ''
        self.title = ''
        self.spine = []        # 書庫の中のパス
        self.documents = {}    # パス -> [Block]
        self.css = {}
        self.toc = []          # (パス#id, 見出し, 深さ)
        self.right_to_left = False

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
                manifest, nav_href, ncx_href, css = {}, None, None, []
                for item in re.findall(r'<item\b[^>]*>', opf):
                    attrs = dict(re.findall(r'([\w:-]+)="([^"]*)"', item))
                    href = posixpath.normpath(posixpath.join(base, unquote(attrs.get('href', ''))))
                    manifest[attrs.get('id')] = href
                    if 'nav' in attrs.get('properties', '').split():
                        nav_href = href
                    if attrs.get('media-type') == 'application/x-dtbncx+xml':
                        ncx_href = href
                    if attrs.get('media-type') == 'text/css':
                        css.append(href)
                title = re.search(r'<dc:title[^>]*>(.*?)</dc:title>', opf, re.S)
                self.title = re.sub(r'<[^>]+>', '', title.group(1)).strip() if title else ''
                self.right_to_left = bool(re.search(r'page-progression-direction="rtl"', opf))
                self.spine = [manifest[i] for i in re.findall(r'<itemref\b[^>]*idref="([^"]+)"', opf)
                              if manifest.get(i) in names]
                for href in css:
                    if href in names:
                        self.css.update(_parse_css(z.read(href).decode('utf-8', 'replace')))
                for href in self.spine:
                    parser = _XhtmlParser()
                    parser.feed(z.read(href).decode('utf-8', 'replace'))
                    parser.close()
                    self.documents[href] = parser.blocks
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
            out.append((href, heading or posixpath.basename(href), 0))
        return out

    @staticmethod
    def _resolve(base, href):
        path, _, fragment = href.partition('#')
        path = posixpath.normpath(posixpath.join(base, unquote(path))) if path else ''
        return path + ('#' + fragment if fragment else '')

    def _indent(self, block):
        """段落の(左の桁, 1行目の桁)。"""
        left = indent = 0.0
        for key in [block.tag] + ['%s.%s' % (block.tag, c) for c in sorted(block.classes)] + \
                   ['.%s' % c for c in sorted(block.classes)]:
            if key in self.css:
                left, indent = self.css[key]
        return max(0, round(left * 2)), max(0, round((left + indent) * 2))

    def render(self, width):
        """本全体を画面の行に。(行の並び, 目次の各項目の行の番号)を返す。"""
        lines, anchors, number = [], {}, 0
        for href in self.spine:
            anchors[href] = len(lines)
            for block in self.documents[href]:
                number += 1
                for anchor in block.anchors:
                    anchors.setdefault(href + '#' + anchor, len(lines))
                text, runs, pos = '', [], 0
                for piece, attr in block.pieces:
                    if attr:
                        runs.append((pos, pos + len(piece), attr))
                    text += piece
                    pos += len(piece)
                if not block.pre:
                    stripped = text.lstrip(' ')
                    shift = len(text) - len(stripped)
                    text = stripped.rstrip(' ')
                    runs = [(max(0, s - shift), max(0, e - shift), a) for s, e, a in runs]
                left, first = self._indent(block)
                lines.extend(_wrap_with_indent(text, runs, width, first, left, number))
        toc_lines = []
        for href, _, _ in self.toc:
            toc_lines.append(anchors.get(href, anchors.get(href.partition('#')[0], 0)))
        return lines, toc_lines


def _wrap_with_indent(text, runs, width, first, left, number):
    """1行目はfirst桁、2行目からはleft桁あけて、幅で折り返す(空の段落は空の行)。"""
    from .overstrike import slice_runs
    if not text:
        return [Line('', number)]
    out, offset, rest, indent = [], 0, text, first
    while rest:
        room = max(4, width - indent)
        piece = wrap_text(rest, room)[0]
        if not piece:
            piece = rest[0]
        pad = ' ' * indent
        attrs = [(s + indent, e + indent, a) for s, e, a in slice_runs(runs, offset, offset + len(piece))]
        out.append(Line(pad + piece, number, ends_with_newline=False, attributes=attrs))
        offset += len(piece)
        rest = rest[len(piece):]
        indent = left
    out[-1].ends_with_newline = True
    return out


def is_epub_file(path):
    return path.lower().endswith('.epub')


def text_width(text):
    return utf8.text_width(text)
