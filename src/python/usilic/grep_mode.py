"""FGREP/EGREP(C++のgrep_mode.cppのGrepMode、画面の中で一致の間を移る形)。

C++版と同じく、見ている画面の行を`grep -Fne`/`grep -Ene`の標準入力へ渡し、`-n`の行番号を画面の行の番号と
して使う。行の中の改行とNULは空白にする(joinLinesForGrep()、docs/CHANGES.md項目1002)。一致した位置(色を
付ける所)は、Pythonのreで求める(EREとPythonのreは、よく使う形では同じ)。
"""
import re
import subprocess

FIXED, REGEX = 'Fixed', 'Regex'


def join_lines_for_grep(lines):
    """joinLinesForGrep(): 1行ずつ\\nでつなぐ。行の中の\\nとNULは空白(1字を1字に)。"""
    return ''.join(line.replace('\n', ' ').replace('\0', ' ') + '\n' for line in lines)


def grep_line_indices(lines, pattern, syntax):
    """一致した行(0始まり)の並び。grepが使えなければNone、パターンの誤りもNone。"""
    argv = ['grep', '-Fne' if syntax == FIXED else '-Ene', pattern]
    try:
        result = subprocess.run(argv, input=join_lines_for_grep(lines).encode('utf-8', 'surrogateescape'),
                                capture_output=True)
    except OSError:
        return None
    if result.returncode not in (0, 1):
        return None
    out = []
    for line in result.stdout.decode('utf-8', 'replace').split('\n'):
        head = line.split(':', 1)[0]
        if head.isdigit():
            out.append(int(head) - 1)
    return out


def match_spans(text, pattern, syntax):
    """行の中の一致した所(開始, 終わり)の並び(色を付ける所)。"""
    if syntax == FIXED:
        spans, start = [], text.find(pattern)
        while pattern and start >= 0:
            spans.append((start, start + len(pattern)))
            start = text.find(pattern, start + len(pattern))
        return spans
    try:
        return [(m.start(), m.end()) for m in re.finditer(pattern, text) if m.end() > m.start()]
    except re.error:
        return [(0, len(text))]


class GrepMode:
    """画面の中での一致(ContentView::setGrepMatchHighlight()の状態)。"""

    def __init__(self):
        self.pattern = ''
        self.syntax = FIXED
        self.matches = []   # 一致した行(画面の行の番号)
        self.current = -1   # 今いる一致(matchesの番号)
        self.origin = 0     # 探し始めた時のカーソル(^Gで戻る所)

    def search(self, lines, pattern, syntax, origin):
        self.pattern, self.syntax, self.origin = pattern, syntax, origin
        found = grep_line_indices(lines, pattern, syntax) if pattern else []
        self.matches = found or []
        self.current = 0 if self.matches else -1  # 最初の一致(カーソルの位置に関わらず、C++版と同じ)
        return found is not None

    def label(self):
        return 'FGREP' if self.syntax == FIXED else 'EGREP'

    def position_text(self):
        return '%d/%d' % (self.current + 1, len(self.matches)) if self.matches else '0/0'

    def move(self, step):
        if self.matches:
            self.current = max(0, min(len(self.matches) - 1, self.current + step))

    def current_line(self):
        return self.matches[self.current] if self.current >= 0 else None
