"""C++版とPython版を同じキーで動かして画面を比べる(docs/CHANGES.md項目1191)。

ユニットテストは起動の経路(ページャー等)を通らないので、C++版にあってPython版に無い動きが見逃されていた
(項目1190)。実際の端末(PTY)で両方を起動し、キーごとの画面の先頭の行とモードラインを比べる。
C++版(src/cpp/build/usilic)か、インストールしたHELP(~/.local/lib/usilic/HELP.1)が無ければ飛ばす。
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import tarfile
import io
import tempfile
import termios
import time
import unicodedata
import unittest
import zipfile

from usilic import install_offer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CPP = os.path.join(ROOT, 'src', 'cpp', 'build', 'usilic')
PYTHON = os.path.join(ROOT, 'src', 'python', 'usilic.py')
REAL_LIB = os.path.expanduser('~/.local/lib')
ROWS, COLUMNS = 24, 100
H, G, K = b'\x08', b'\x07', b'\x0b'
SEQUENCE = re.compile(r'\x1b\[([?0-9;]*)([A-Za-z@])')


class Screen:
    """端末(vt100)の最小限の真似: カーソル移動・消去・スクロール範囲と字だけ。curses(Python版)はスクロールも使う。"""

    def __init__(self):
        self.rows = [[' '] * COLUMNS for _ in range(ROWS)]
        self.row = self.column = 0
        self.top, self.bottom = 0, ROWS - 1
        self.saved = (0, 0)
        self.autowrap = True
        self.wrap_pending = False
        self.pending = b''

    def feed(self, data):
        self.pending += data
        text = self.pending.decode('utf-8', 'ignore')
        self.pending = self.pending[len(text.encode('utf-8')):]
        i = 0
        while i < len(text):
            ch = text[i]
            if ord(ch) < 0x20:  # 字以外の動きは、右端での折り返しの待ちを消す
                self.wrap_pending = False
            if ch == '\x1b':
                match = SEQUENCE.match(text, i)
                if match:
                    self._control(*match.groups())
                    i = match.end()
                    continue
                following = text[i + 1:i + 2]
                if not following or (following == '[' and SEQUENCE.fullmatch(text[i:] + 'm')):
                    self.pending = text[i:].encode('utf-8') + self.pending  # 読み込みの切れ目で切れた列
                    return
                if following in ('(', ')', '#'):  # 文字集合の指定等(3バイト)
                    i += 3
                    continue
                if following == 'D':
                    self._index()
                elif following == 'M':
                    self._reverse_index()
                elif following == 'E':
                    self.column = 0
                    self._index()
                elif following == '7':
                    self.saved = (self.row, self.column)
                elif following == '8':
                    self.row, self.column = self.saved
                i += 2
                continue
            if ch == '\r':
                self.column = 0
            elif ch == '\n':
                self._index()
            elif ch == '\b':
                self.column = max(0, self.column - 1)
            elif ch == '\t':
                self.column = min(COLUMNS - 1, (self.column // 8 + 1) * 8)
            elif ord(ch) >= 0x20:
                self._put(ch)
            i += 1

    def _index(self):
        if self.row == self.bottom:
            del self.rows[self.top]
            self.rows.insert(self.bottom, [' '] * COLUMNS)
        else:
            self.row = min(ROWS - 1, self.row + 1)

    def _reverse_index(self):
        if self.row == self.top:
            del self.rows[self.bottom]
            self.rows.insert(self.top, [' '] * COLUMNS)
        else:
            self.row = max(0, self.row - 1)

    def _put(self, ch):
        width = 2 if unicodedata.east_asian_width(ch) in 'WF' else 1
        if self.wrap_pending:  # 右端の字の次の字で初めて折り返す(実際の端末と同じ)
            self.column = 0
            self._index()
        elif self.column + width > COLUMNS:
            self.column = COLUMNS - width
        self.wrap_pending = False
        if 0 <= self.row < ROWS:
            self.rows[self.row][self.column] = ch
            if width == 2:
                self.rows[self.row][self.column + 1] = ''
        self.column += width
        if self.column >= COLUMNS:
            self.column = COLUMNS - 1
            self.wrap_pending = self.autowrap

    def _control(self, parameters, kind):
        if parameters == '?7':
            self.autowrap = kind == 'h'
        numbers = [int(p) if p.isdigit() else 0 for p in parameters.split(';')] if '?' not in parameters else []
        count = (numbers[0] if numbers else 0) or 1
        if kind in 'Hf':
            self.row = ((numbers[0] if numbers else 1) or 1) - 1
            self.column = ((numbers[1] if len(numbers) > 1 else 1) or 1) - 1
        elif kind == 'A':
            self.row -= count
        elif kind == 'B':
            self.row += count
        elif kind == 'C':
            self.column += count
        elif kind == 'D':
            self.column -= count
        elif kind == 'K':
            which = numbers[0] if numbers else 0
            columns = {0: range(self.column, COLUMNS), 1: range(0, self.column + 1)}.get(which, range(COLUMNS))
            for x in columns:
                self.rows[self.row][x] = ' '
        elif kind == 'J':
            which = numbers[0] if numbers else 0
            if which == 0:
                self.rows[self.row][self.column:] = [' '] * (COLUMNS - self.column)
            for y in {0: range(self.row + 1, ROWS), 1: range(self.row)}.get(which, range(ROWS)):
                self.rows[y] = [' '] * COLUMNS
        elif kind == 'r':
            self.top = ((numbers[0] if numbers else 1) or 1) - 1
            self.bottom = ((numbers[1] if len(numbers) > 1 else ROWS) or ROWS) - 1
            self.row = self.column = 0

    def line(self, row):
        return ''.join(self.rows[row]).rstrip()


def run_keys(argv, home, steps):
    """argvを起動し、(ラベル, 先頭の行, モードライン)を起動直後と各キーの後について返す。"""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM='vt100', LANG='ja_JP.UTF-8', HOME=home)
        os.execv(argv[0], argv)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLUMNS, 0, 0))
    screen = Screen()

    def settle(quiet=0.5, limit=15.0):
        """出力が止まるまで読む。"""
        end, last = time.time() + limit, time.time()
        while time.time() < end and time.time() - last < quiet:
            if select.select([fd], [], [], 0.05)[0]:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    return
                if not data:
                    return
                screen.feed(data)
                last = time.time()

    results = []
    try:
        settle(quiet=1.0)
        results.append(('start', screen.line(0), screen.line(ROWS - 1)))
        for label, keys in steps:
            os.write(fd, keys)
            settle()
            results.append((label, screen.line(0), screen.line(ROWS - 1)))
    finally:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        os.close(fd)
    return results


def write_tgz(path):
    with tarfile.open(path, 'w:gz') as tar:
        for name in ('._a.txt', 'a.txt', 'b.txt', '._only.txt'):  # 本体と組の「._」は隠し、本体の無い「._」は残す
            info = tarfile.TarInfo(name)
            info.size, info.mtime = 2, 1000000000
            tar.addfile(info, io.BytesIO(b'x\n'))


def write_vertical_epub(path):
    """縦書き(page-progression-direction="rtl")の本: 傍点の段落と、中央寄せ・ルビのある横書きの奥付。"""
    item = '<item id="c%d" href="c%d.xhtml" media-type="application/xhtml+xml"/>'
    opf = ('<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="i">'
           '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="i">x</dc:identifier>'
           '<dc:title>t</dc:title><dc:language>ja</dc:language></metadata><manifest>'
           '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
           '<item id="s" href="s.css" media-type="text/css"/>' +
           item % (1, 1) + item % (2, 2) + item % (3, 3) +
           '</manifest><spine page-progression-direction="rtl"><itemref idref="c1"/><itemref idref="c2"/>'
           '<itemref idref="c3"/></spine></package>')
    nav = ('<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body>'
           '<nav epub:type="toc"><ol><li><a href="c1.xhtml">第一章</a></li><li><a href="c2.xhtml">第二章</a></li>'
           '<li><a href="c3.xhtml">奥付</a></li></ol></nav></body></html>')
    css = ('.em { -webkit-text-emphasis-style: filled sesame; }\n'
           '.h { writing-mode: horizontal-tb; }\n'
           '.c { text-align: center; }\n')
    page = ('<html xmlns="http://www.w3.org/1999/xhtml"><head><link rel="stylesheet" href="s.css"/></head>'
            '<body%s>%s</body></html>')
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml',
                   '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles>'
                   '<rootfile full-path="o.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('o.opf', opf)
        z.writestr('nav.xhtml', nav)
        z.writestr('s.css', css)
        z.writestr('c1.xhtml', page % ('', '<h1>第一章</h1><p>本文です。<span class="em">傍点の字</span>です。' + '続きです。' * 40 + '</p>'))
        z.writestr('c2.xhtml', page % ('', '<h1>第二章</h1><p>' + '続きです。' * 40 + '</p>'))
        z.writestr('c3.xhtml', page % (' class="h"', '<h1>奥付</h1><p class="c"><ruby>燈<rt>ひ</rt></ruby>色の場</p>'
                                       '<p class="c"><span class="em">傍点</span>の行</p><p>ふつうの行</p>'))


@unittest.skipUnless(os.path.exists(CPP), 'C++版が無い(make usilic)')
@unittest.skipUnless(os.path.exists(os.path.join(REAL_LIB, 'usilic', 'HELP.1')), 'HELPが無い(make install)')
class CppParityTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        base = os.path.realpath(self.directory.name)
        self.work = os.path.join(base, 'w')
        os.mkdir(self.work)
        write_tgz(os.path.join(self.work, 'x.tgz'))
        write_vertical_epub(os.path.join(self.work, 'v.epub'))
        with open(os.path.join(self.work, 'a.txt'), 'w') as f:
            f.write(''.join('line %d\n' % i for i in range(100)))
        # 家を分ける(保存した余白・位置を持ち込まない)。historyがあれば最初の起動にはならない。
        self.homes = []
        for name in ('cpp', 'python'):
            home = os.path.join(base, name)
            os.makedirs(os.path.join(home, '.usilic'))
            os.makedirs(os.path.join(home, '.local'))
            os.symlink(REAL_LIB, os.path.join(home, '.local', 'lib'))
            with open(os.path.join(home, '.usilic', 'history'), 'w') as f:
                f.write('# ~/.usilic/history: usilic\n')
            with open(os.path.join(home, '.usilic', 'install-offered'), 'w') as f:  # インストールの案内を出さない
                # フォントは家の下も探すので、本物の家に入っていても分けた家では案内される
                packages = set(install_offer.missing_packages()) | {'font-ipafont'}
                f.write(''.join(package + '\n' for package in sorted(packages)))
            self.homes.append(home)

    def compare(self, arguments, steps):
        cpp = run_keys([CPP, '-c'] + arguments, self.homes[0], steps)
        python = run_keys([sys.executable, PYTHON] + arguments, self.homes[1], steps)
        for ours, theirs in zip(python, cpp):
            with self.subTest(step=ours[0]):
                self.assertEqual(ours, theirs)

    def path(self, name):
        return os.path.join(self.work, name)

    def test_pager_file_help_and_key(self):
        self.compare([self.path('a.txt')], [('^H', H), ('^G', G), ('^G', G), ('^K', K), ('^G', G), ('^G', G)])

    def test_pager_archive_listing_help_and_key(self):
        self.compare([self.path('x.tgz')], [('^H', H), ('^G', G), ('^G', G), ('^K', K), ('^G', G), ('^G', G)])

    def test_pager_vertical_book(self):
        self.compare([self.path('v.epub')], [('^G', G), ('^G', G), ('^K', K), ('^G', G), ('^H', H), ('^G', G), ('^G', G)])

    def test_vertical_book_colophon(self):
        # 章の一覧の最後(横書きの奥付)へ: 位置の総数・傍点のバイト数・中央寄せの桁
        self.compare([self.path('v.epub')], [('^G', G), ('^[>', b'\x1b>'), ('^M', b'\r')])

    def test_epub_margins(self):
        up, down, left, right = b'\x1b[A', b'\x1b[B', b'\x1b[D', b'\x1b[C'
        steps = []
        for label, side in (('左', left), ('右', right), ('右', right)):
            steps += [('^[' + label, b'\x1b'), (label, side), ('↑', up)]
        self.compare([self.path('a.txt')], [('^H', H)] + steps + [('^G', G), ('^G', G)])
        vertical = []
        for label, side in (('上', up), ('下', down)):
            vertical += [('^[' + label, b'\x1b'), (label, side), ('↑', up)]
        self.compare([self.path('v.epub')], vertical + [('^G', G)] + vertical[:3] + [('下の↓', b'\x1b'), ('下', down),
                                                                                    ('↓', down)])

    def test_fgrep_move_keys(self):
        typed = [('^S', b'\x13'), ('line 1', b'line 1')]
        moves = [('^S次', b'\x13'), ('^S次', b'\x13'), ('^R前', b'\x12')]
        # 打っている間、確定の後、^Uで消した真っ新なプロンプトの^R(履歴)、^J^Sの確定の後の^N
        self.compare([self.path('a.txt')], typed + moves + [('^M', b'\r')] + moves + [('^G', G)])
        self.compare([self.path('a.txt')], typed + [('^U', b'\x15'), ('^R', b'\x12'), ('^G', G), ('^G', G)])
        self.compare([self.path('a.txt')], [('^J^S', b'\x0a\x13'), ('line 2', b'line 2'), ('^M', b'\r'), ('^N', b'\x0e')])

    def test_language_choice_survives_restart(self):
        self.compare([self.work], [('^Oj', b'\x0fj')])
        self.compare([self.work], [('^H', H)])

    def test_egrep_reverse_key_needs_empty_prompt(self):
        # ^J^S・^O^S・^O^J^S: 文字がある間の^Rはその旨を出し、次のキーで消す。確定の後の^Rも同じ
        for name, keys in (('^J^S', b'\x0a\x13'), ('^O^S', b'\x0f\x13'), ('^O^J^S', b'\x0f\x0a\x13')):
            self.compare([self.path('a.txt')], [(name, keys), ('line', b'line'), ('^R', b'\x12'), ('l', b'l'), ('^G', G)])
        self.compare([self.path('a.txt')], [('^J^S', b'\x0a\x13'), ('line 2', b'line 2'), ('^M', b'\r'), ('^R', b'\x12'), ('^N', b'\x0e')])

    def test_directory_open_archive_listing(self):
        self.compare([self.work], [('^[>', b'\x1b>'), ('^O^M', b'\x0f\r'), ('^G', G)])


if __name__ == '__main__':
    unittest.main()
