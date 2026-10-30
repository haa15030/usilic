"""SHELLモード(C++のshell.cppのShell)。

疑似端末でbashを動かし、出力を行にためる。C++版と同じく、PS1にプロンプトの印(<usilic>)を仕込んで
プロンプトを見分け(表示は「usilic-1.5b:ディレクトリ$ 」)、起動の時は「#eos<PID>#」が返るまで待つ。
コマンド行はusilic側で編集し、^Mで1行まとめて送る。

C++版との違い(学習用に簡素にした所): bashは`--noediting`(readline無し)で動かし、ECHOも切る。C++版は
readlineのままで、readline自身の再描画のエコーを見分けて取り除いているが、それを省いた。
"""
import codecs
import os
import pty
import select
import signal
import struct
import fcntl
import termios
import time

from .content_view import Line, wrap_text, expand_tabs

PROMPT_MARK = '<usilic>'
PROGRAM_NAME = 'usilic'
PROMPT_DISPLAY_NAME = 'usilic-1.5b'


class PromptLine(str):
    """「プロンプト+コマンド」の確定した行(字は普通の行と同じ、送ったコマンドを持つ)。"""
    def __new__(cls, text, command):
        line = super().__new__(cls, text)
        line.command = command
        return line


class Shell:
    def __init__(self):
        self.pid = None
        self.fd = None
        self.lines = []          # 確定した行(折り返す前の文字列)
        self.pending = ''        # まだ改行の来ていない行
        self.prompt = ''         # 今のプロンプト(表示用、「usilic-1.5b:~/x$ 」)
        self.prompt_ready = False
        self.prompt_generation = 0   # プロンプトが出た回数(C++のpromptGeneration())
        self.display_name = PROMPT_DISPLAY_NAME  # プロンプトの名前(最初の起動は「USILIC」、C++のsetPromptName())
        self.current_directory = ''
        self.byte_count = 0
        self._decoder = codecs.getincrementaldecoder('utf-8')('replace')
        self._escape = None      # 読みかけのエスケープ列
        self._raw_tail = ''

    def alive(self):
        return self.pid is not None

    def start(self, initial_directory, rows, columns):
        """Shell::start(): 疑似端末とbash、設定のコマンド、ハンドシェイク。"""
        pid, fd = pty.fork()
        if pid == 0:
            try:
                # resetSignalsToDefault(): 親(usilic)のシグナルの設定を受け継がない(無視のままだと、^Cで
                # 前面のプログラムが止まらない)。
                for number in range(1, signal.NSIG):
                    try:
                        signal.signal(number, signal.SIG_DFL)
                    except (OSError, ValueError):
                        pass
                signal.pthread_sigmask(signal.SIG_SETMASK, [])  # ブロックも受け継がない
                attrs = termios.tcgetattr(0)
                attrs[3] &= ~(termios.ECHO | termios.ECHONL)  # C++版のsetupTio.c_lflag &= ~(ECHO|ECHONL)
                termios.tcsetattr(0, termios.TCSANOW, attrs)
                fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))
                if initial_directory:
                    os.chdir(initial_directory)
                env = dict(os.environ, USILIC='1', USILIC_LIVE_SHELL='1', TERM='dumb')
                os.execvpe('bash', ['bash', '--noediting', '-i'], env)
            finally:
                os._exit(127)
        self.pid, self.fd = pid, fd
        mark, prog = PROMPT_MARK, PROGRAM_NAME
        setup = ("unset PROMPT_COMMAND ; export PS1='%s%s:\\w\\$ %s' ; set -o ignoreeof ; "
                 "export COLUMNS=%d ; unset HISTFILE ; echo \"#eos$$#\"\n" % (mark, prog, mark, columns))
        os.write(fd, setup.encode())
        return self.wait_for_handshake()

    def wait_for_handshake(self, timeout=10.0):
        """Shell::waitForHandshake(): 「#eos<PID>#」までの出力は捨てる(設定のコマンドの分)。番兵の後も
        出力が20ms途切れるまで読み、最初のプロンプトの印より前(番兵の行の改行等)も捨てる。"""
        end, data, found = time.time() + timeout, b'', -1
        token = ('#eos%d#' % self.pid).encode()
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if not r:
                if found >= 0:
                    break  # 番兵の後のまとまりが届き終わった
                continue
            try:
                chunk = os.read(self.fd, 4096)
            except OSError:
                return False
            if not chunk:
                return False
            data += chunk
            if found < 0:
                at = data.find(token)
                if at >= 0:
                    found = at + len(token)
        if found < 0:
            return False
        rest = data[found:]
        mark = rest.find(PROMPT_MARK.encode())
        if mark >= 0:
            rest = rest[mark:]
        if rest:
            self.process_output_bytes(rest)
        return True

    def on_pty_readable(self):
        """Shell::onPtyReadable()。読めなければ(bashが終わった)False。"""
        try:
            data = os.read(self.fd, 65536)
        except OSError:
            data = b''
        if not data:
            self.terminate()
            return False
        self.process_output_bytes(data)
        return True

    def process_output_bytes(self, data):
        """Shell::processOutputBytes(): 改行・CR・BS・エスケープ列(除く)・プロンプトの印。"""
        self.byte_count += len(data)
        for ch in self._decoder.decode(data):
            if self._escape is not None:
                self._escape += ch
                if self._escape_done():
                    self._escape = None
                continue
            if ch == '\x1b':
                self._escape = ch
            elif ch == '\n':
                self.lines.append(self.pending)
                self.pending = ''
                self.prompt_ready = False
            elif ch == '\r':
                continue  # \r\nの\r(行の頭へ戻って上書きするプログレス表示は、続きを同じ行へ足すだけ)
            elif ch in ('\b', '\x7f'):
                self.pending = self.pending[:-1]
            elif ch == '\x07':
                continue
            else:
                self.pending += ch
            self._detect_prompt()

    def _escape_done(self):
        """読みかけのエスケープ列が終わったか(CSI・OSC・その他、C++のPipeBuffer::escapeLengthAt()の形)。"""
        s = self._escape
        if len(s) < 2:
            return False
        if s[1] == '[':
            return len(s) > 2 and '\x40' <= s[-1] <= '\x7e'
        if s[1] == ']':
            return s[-1] == '\x07' or s.endswith('\x1b\\')
        return '\x30' <= s[-1] <= '\x7e'

    def _detect_prompt(self):
        """「<usilic>usilic:ディレクトリ$ <usilic>」が揃ったら、表示用のプロンプトにする。"""
        start = self.pending.find(PROMPT_MARK)
        if start < 0:
            return
        end = self.pending.find(PROMPT_MARK, start + len(PROMPT_MARK))
        if end < 0:
            return
        inner = self.pending[start + len(PROMPT_MARK):end]
        before = self.pending[:start]
        if before:
            self.lines.append(before)  # 改行の無い出力の後に来たプロンプト
        if inner.startswith(PROGRAM_NAME + ':'):
            body = inner[len(PROGRAM_NAME) + 1:]
            directory = body[:-2] if body.endswith('$ ') or body.endswith('# ') else body
            self.current_directory = os.path.expanduser(directory)
            self.prompt = self.display_name + ':' + body
        else:
            self.prompt = inner
        self.pending = self.pending[end + len(PROMPT_MARK):]
        self.prompt_ready = True
        self.prompt_generation += 1

    def submit_line(self, command):
        """Shell::submitLine(): プロンプトとコマンドを行として残し、bashへ送る。"""
        self.lines.append(PromptLine((self.prompt if self.prompt_ready else self.pending) + command, command))
        if not self.prompt_ready:
            self.pending = ''
        self.prompt_ready = False
        os.write(self.fd, (command + '\n').encode('utf-8'))

    def send_control_byte(self, byte):
        """Shell::sendControlByte()(^C・^Dをそのままbashへ)。"""
        os.write(self.fd, bytes([byte]))

    def on_resize(self, rows, columns):
        if self.fd is not None:
            try:
                fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))
            except OSError:
                pass

    def view_lines(self, width, command_text):
        """画面の行(ContentView用): 確定した行と、最後に「プロンプト+編集中のコマンド」。"""
        out = []
        for number, text in enumerate(self.lines, 1):
            pieces = wrap_text(expand_tabs(text), width)
            command = getattr(text, 'command', None)
            for i, piece in enumerate(pieces):
                out.append(Line(piece, number, ends_with_newline=(i == len(pieces) - 1), shell_command=command))
        live = (self.prompt if self.prompt_ready else self.pending) + command_text
        bold = len(self.display_name) + 1 if self.prompt_ready and self.prompt.startswith(self.display_name + ':') else 0
        for piece in wrap_text(expand_tabs(live), width):
            # 打っている行のプロンプトの「usilic-1.5b:」は太字(C++のSHELLのプロンプトの行)
            attributes = [(0, min(bold, len(piece)), 'bold')] if bold else []
            bold = max(0, bold - len(piece))
            out.append(Line(piece, len(self.lines) + 1, attributes=attributes))
        return out

    def committed_rows(self, width):
        """確定した行を折り返した行の字(C++のscrollback_の行と同じく、タブの所は幅0の印U+2060と空白)。"""
        rows = []
        for text in self.lines:
            rows.extend(wrap_text(expand_tabs(text, marker='\u2060'), width))
        return rows

    def shown_byte_count(self, width):
        """モードラインの大きさ(C++はscrollback_の行ごとに字+1): 確定した行を折り返した1行ごとに。
        プロンプトと編集中のコマンドの行は数えない。C++はタブの所に幅0の印U+2060(3バイト)を置くので、その分も。"""
        return sum(sum(len(piece.encode('utf-8')) + 1 for piece in wrap_text(expand_tabs(text), width))
                   + 3 * text.count('\t') for text in self.lines)

    def terminate(self):
        if self.pid is not None:
            try:
                os.kill(self.pid, signal.SIGHUP)
                os.waitpid(self.pid, 0)
            except OSError:
                pass
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
        self.pid = self.fd = None
