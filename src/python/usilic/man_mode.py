"""MANモード(C++のman_mode.cppのManMode)と、groffで組む文書(HELP.1)。

`man`をMANWIDTH=画面の幅で動かし、出力の重ね打ち(太字・下線)を属性にして行にする。
"""
import os
import subprocess

from . import overstrike
from .content_view import expand_tabs


def run_to_lines(argv, width, env=None, input_bytes=None, limit=None, transform=None):
    """argvの出力を重ね打ちを解いて折り返した行に。動かせなければNone。"""
    try:
        result = subprocess.run(argv, capture_output=True, input=input_bytes,
                                env=dict(os.environ, **(env or {})))
    except OSError:
        return None
    if result.returncode != 0 and not result.stdout:
        return None
    data = result.stdout[:limit] if limit else result.stdout
    text = data.decode('utf-8', 'replace')
    text = transform(text) if transform else text
    lines = styled_lines(text, width)
    return lines, shown_byte_count(lines, text)


def shown_byte_count(lines, text):
    """C++のContentView::totalByteCount(): PipeBufferを通した後の行(折り返した1行ごとに改行の1を足す)の大きさ。
    PipeBufferはタブの位置に幅0の印U+2060(3バイト)を置くので、その分も。"""
    return sum(len(l.text.encode('utf-8')) + 1 for l in lines) + 3 * text.count('\t')


def styled_lines(text, width):
    lines = []
    raw = text.split('\n')
    if raw and raw[-1] == '':
        raw.pop()
    for number, line in enumerate(raw, 1):
        plain, runs = overstrike.decode(line.replace('\r', ''))
        plain = expand_tabs(plain)  # 桁は表示幅で(str.expandtabs()は全角の字も1桁と数える)
        lines.extend(overstrike.wrap_styled(plain, runs, width, number))
    return lines


class ManMode:
    def __init__(self, view, configuration):
        self.view = view
        self.configuration = configuration
        self.current_page = ''
        self.byte_count = 0
        self.view.cursor_highlight = False  # ManModeのコンストラクタと同じ

    def load_page(self, name, width):
        """ManMode::loadPage()。無ければFalse。"""
        if not name:
            return False
        argv = self.configuration.command_for('Man') + [name]
        if name == 'usilic':  # usilic自身はHELP.1(英語はHELP_en.1)をgroffで(C++のsetPageResolver())
            from . import help_text
            from .messages_ja import japanese
            page = (help_text.find_in_library('HELP.1') if japanese() else None) or \
                help_text.find_in_library('HELP_en.1')
            groff = self.configuration.command_for('Groff')
            if page and groff:
                argv = groff + ['-rLL=%dn' % max(width, 20), page]
        found = run_to_lines(argv, width, env={'MANWIDTH': str(width), 'MAN_KEEP_FORMATTING': '1'},
                             limit=self.configuration.buffer_limit_bytes())
        if not found or not found[0]:
            return False
        self.view.set_lines(found[0])
        self.byte_count = found[1]
        self.current_page = name
        self.view.cursor_index = self.view.top_index = 0
        return True
