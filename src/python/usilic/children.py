"""子のusilic(^T^H・^T^K・-H・-Kの窓、^Ouの外のページャー)と親子の受け渡し(C++のapplication.cppの
openInNewTerminalWindow()・openChildChannel()・sendToChannel()・registerAsChild()・terminateChildren())。

子は端末の新しいウィンドウで開く(macOSはTerminal.app・iTerm2、他はgnome-terminal等)。親子は一時ディレクトリの
usilic-ch-<親のPID>/のFIFOで1行ずつやり取りする: 親は"keys"、HELPの子は"help"を読む。KEYの子は親の"keys"へ
「K c|- 16進のキー」(キーを親で動かす)・「H 番目:16進の行」(HELPのその行へ)を書く。
"""
import os
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time


def channel_directory(owner_pid):
    return os.path.join(tempfile.gettempdir(), 'usilic-ch-%d' % owner_pid)


def send(owner_pid, name, message):
    """`owner_pid`の受け渡しのFIFO `name`へ1行。読む側がいなければFalse(C++のsendToChannel())。"""
    path = os.path.join(channel_directory(owner_pid), name)
    try:
        fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
    except OSError:
        return False
    try:
        data = message.encode('utf-8')
        return os.write(fd, data) == len(data)
    except OSError:
        return False
    finally:
        os.close(fd)


class Channel:
    """自分が読むFIFO(C++のopenChildChannel()・closeChildChannel()・registerAsChild())。"""

    def __init__(self):
        self.path = None
        self.fd = -1
        self.keep_fd = -1
        self.buffer = b''
        self.pid_file = None

    def open(self, owner_pid, name):
        self.close()
        directory = channel_directory(owner_pid)
        try:
            os.mkdir(directory, 0o700)
        except FileExistsError:
            pass
        except OSError:
            return
        try:
            st = os.stat(directory)
        except OSError:
            return
        if not stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid():
            return
        path = os.path.join(directory, name)
        try:
            os.unlink(path)
        except OSError:
            pass
        try:
            os.mkfifo(path, 0o600)
            self.fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            self.keep_fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
        except OSError:
            return
        self.path = path

    def is_help(self):
        return bool(self.path) and self.path.endswith('/help')

    def register_as_child(self, parent_pid):
        """親の受け渡しのディレクトリに"pid.<自分のPID>"(中身は自分のFIFOの名前)。親が終わる時に止める。"""
        directory = channel_directory(parent_pid)
        try:
            os.mkdir(directory, 0o700)
        except OSError:
            pass
        self.pid_file = os.path.join(directory, 'pid.%d' % os.getpid())
        try:
            with open(self.pid_file, 'w') as f:
                f.write(os.path.basename(self.path) if self.path else '')
        except OSError:
            self.pid_file = None

    def read_lines(self):
        if self.fd < 0:
            return []
        while True:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                break
            except OSError:
                break
            if not chunk:
                break
            self.buffer += chunk
        lines = []
        while b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            lines.append(line.decode('utf-8', 'replace'))
        return lines

    def terminate_children(self):
        """C++のterminateChildren(): 登録された子(psでusilicと確かめて)にSIGTERMを送り、終わるのを待つ(1.5秒まで)。"""
        if not self.path:
            return
        directory = os.path.dirname(self.path)
        pids = []
        try:
            names = os.listdir(directory)
        except OSError:
            return
        for name in names:
            if not name.startswith('pid.'):
                continue
            try:
                pid = int(name[4:])
            except ValueError:
                continue
            try:
                os.kill(pid, 0)
            except OSError:
                try:
                    os.remove(os.path.join(directory, name))
                except OSError:
                    pass
                continue
            try:
                comm = subprocess.run(['ps', '-p', str(pid), '-o', 'command='], capture_output=True,
                                      text=True).stdout
            except OSError:
                comm = ''
            if 'usilic' not in comm:
                continue
            try:
                os.kill(pid, signal.SIGTERM)
                pids.append(pid)
            except OSError:
                pass
        for _ in range(30):
            alive = False
            for pid in pids:
                try:
                    os.kill(pid, 0)
                    alive = True
                except OSError:
                    pass
            if not alive:
                break
            time.sleep(0.05)

    def close(self):
        if self.pid_file:
            for remove in (lambda: os.unlink(self.pid_file), lambda: os.rmdir(os.path.dirname(self.pid_file))):
                try:
                    remove()
                except OSError:
                    pass
            self.pid_file = None
        for fd in (self.fd, self.keep_fd):
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass
        self.fd = self.keep_fd = -1
        if self.path:
            for remove in (lambda: os.unlink(self.path), lambda: os.rmdir(os.path.dirname(self.path))):
                try:
                    remove()
                except OSError:
                    pass
        self.path = None


def parent_gone(parent_pid):
    """C++のtransientParentGone(): 親のusilicがいなくなったか。"""
    if parent_pid <= 1:
        return False
    try:
        os.kill(parent_pid, 0)
    except ProcessLookupError:
        return True
    except OSError:
        return False
    return False


def _cd_wrap(directory, command):
    return ('cd %s && ' % shlex.quote(directory) if directory else '') + command


_screen_rects = None


def _terminal_screen_rects():
    """画面ごとの使える範囲(AppleScriptの{{左, 上, 右, 下}, ...}、C++のterminalScreenRects())。"""
    global _screen_rects
    if _screen_rects is not None:
        return _screen_rects
    script = ("ObjC.import('AppKit'); var s = $.NSScreen.screens; var H = s.objectAtIndex(0).frame.size.height;"
              " var out = []; for (var i = 0; i < s.count; i++) { var f = s.objectAtIndex(i).visibleFrame;"
              " out.push('{' + [f.origin.x, H - (f.origin.y + f.size.height), f.origin.x + f.size.width,"
              " H - f.origin.y].map(Math.round).join(', ') + '}'); } '{' + out.join(', ') + '}'")
    try:
        text = subprocess.run(['osascript', '-l', 'JavaScript', '-e', script], capture_output=True,
                              text=True).stdout.strip()
    except OSError:
        text = ''
    _screen_rects = text if len(text) > 2 and text.startswith('{') else '{{-100000, -100000, 100000, 100000}}'
    return _screen_rects


LINUX_TERMINALS = (('gnome-terminal', '--', 'GNOME_TERMINAL_SCREEN'), ('konsole', '-e', 'KONSOLE_VERSION'),
                   ('xfce4-terminal', '-x', None), ('xterm', '-e', 'XTERM_VERSION'),
                   ('x-terminal-emulator', '-e', None))


def open_in_new_terminal_window(directory, command, slot=0, cascade=0):
    """`command`を端末の新しいウィンドウで(C++のopenInNewTerminalWindow())。終わると消える目印のファイルのパス、
    開けなければNone。slotは-H・^T^HのHELP(1)・KEY(2)の置き場所(親の右隣、KEYはその下)、0は右下へずらす。"""
    if os.environ.get('SSH_CONNECTION') or os.environ.get('SSH_TTY'):
        return None
    mac = sys.platform == 'darwin'
    iterm = os.environ.get('TERM_PROGRAM') == 'iTerm.app' or os.environ.get('LC_TERMINAL') == 'iTerm2'
    terminal = None
    if not mac:
        if not os.environ.get('DISPLAY') and not os.environ.get('WAYLAND_DISPLAY'):
            return None
        for program, flag, hint in LINUX_TERMINALS:
            if hint and os.environ.get(hint) and shutil.which(program):
                terminal = (program, flag)
        for program, flag, hint in LINUX_TERMINALS:
            if terminal is None and shutil.which(program):
                terminal = (program, flag)
        if terminal is None:
            return None
    directory = directory or os.getcwd()
    fd, marker = tempfile.mkstemp(prefix='usilic-pager-%d-' % os.getpid())
    os.close(fd)
    if mac:
        line = ''
        if not iterm:
            line += ("WID=$(osascript -e 'on run argv' -e 'tell application \"Terminal\"' "
                     "-e 'set ids to id of every window' -e 'repeat with k from 1 to count of ids' "
                     "-e 'set wid to item k of ids' -e 'try' "
                     "-e 'if tty of selected tab of window id wid is (item 1 of argv) then return wid' "
                     "-e 'end try' -e 'end repeat' -e 'end tell' -e 'end run' \"$(tty)\" 2>/dev/null); ")
        line += _cd_wrap(directory, command) + '; rm -f ' + shlex.quote(marker) + '; '
        if not iterm:
            line += ("[ -n \"$WID\" ] && nohup osascript -e 'on run argv' -e 'delay 0.5' "
                     "-e 'tell application \"Terminal\"' -e 'try' -e 'close window id ((item 1 of argv) as integer)' "
                     "-e 'end try' -e 'end tell' -e 'end run' \"$WID\" >/dev/null 2>&1 & disown; ")
        line += 'exit'
        quoted = '"' + line.replace('\\', '\\\\').replace('"', '\\"') + '"'
        if iterm:
            argv = ['osascript', '-e', 'tell application "iTerm"', '-e',
                    'set newWindow to (create window with default profile)', '-e',
                    'tell current session of newWindow to write text ' + quoted, '-e', 'activate', '-e', 'end tell']
        else:
            try:
                tty = os.ttyname(0)
            except OSError:
                tty = ''
            offset = (cascade % 10 + 1) * 32
            lines = [
                'tell application "Terminal"',
                'set pName to missing value', 'set pb to missing value', 'set pw to missing value',
                'repeat with w in windows', 'repeat with t in tabs of w',
                'if tty of t is "' + tty + '" then',
                'set pName to name of current settings of t', 'set pb to bounds of w', 'set pw to id of w',
                'end if', 'end repeat', 'end repeat',
                'set newTab to do script ' + quoted,
                'try', 'if pName is not missing value then set current settings of newTab to settings set pName',
                'end try',
                'try', 'if pb is not missing value then', 'set newTty to tty of newTab',
                'repeat with w in windows', 'if tty of selected tab of w is newTty then',
                'set nb to bounds of w', 'set cw to (item 3 of nb) - (item 1 of nb)',
                'set ch to (item 4 of nb) - (item 2 of nb)',
                'set scr to ' + _terminal_screen_rects(), 'set sr to item 1 of scr',
                'repeat with r in scr',
                'if (item 1 of pb) >= (item 1 of r) and (item 1 of pb) < (item 3 of r) and (item 2 of pb) >= '
                '(item 2 of r) and (item 2 of pb) < (item 4 of r) then set sr to contents of r',
                'end repeat',
                'set x to item 3 of pb', 'if x + cw > (item 3 of sr) then set x to (item 1 of pb) - cw',
                'if x < (item 1 of sr) then set x to item 1 of sr',
                'set hy to item 2 of pb', 'if hy + ch > (item 4 of sr) then set hy to (item 4 of sr) - ch',
                'if hy < (item 2 of sr) then set hy to item 2 of sr',
                'set y to hy',
                'if %d > 1 then' % slot, 'set y to hy + ch', 'if y + ch > (item 4 of sr) then',
                'set y to hy - ch', 'if y < (item 2 of sr) then set y to (item 4 of sr) - ch', 'end if', 'end if',
                'if %d = 0 then' % slot, 'set x to (item 1 of pb) + %d' % offset,
                'set y to (item 2 of pb) + %d' % offset,
                'if x + cw > (item 3 of sr) then set x to (item 3 of sr) - cw',
                'if y + ch > (item 4 of sr) then set y to (item 4 of sr) - ch',
                'if x < (item 1 of sr) then set x to item 1 of sr',
                'if y < (item 2 of sr) then set y to item 2 of sr', 'end if',
                'set bounds of w to {x, y, x + cw, y + ch}', 'exit repeat', 'end if', 'end repeat', 'end if',
                'end try',
                'activate', 'try', 'if pw is not missing value then set index of window id pw to 1', 'end try',
                'end tell']
            argv = ['osascript']
            for l in lines:
                argv += ['-e', l]
    else:
        path = os.environ.get('PATH')
        shell_line = ('PATH=%s; export PATH; ' % shlex.quote(path) if path else '') + \
            _cd_wrap(directory, command) + '; rm -f ' + shlex.quote(marker)
        argv = ['sh', '-c', 'nohup ' + ' '.join(shlex.quote(a) for a in (terminal[0], terminal[1], 'sh', '-c',
                                                                          shell_line)) +
                ' </dev/null >/dev/null 2>&1 &']
    try:
        ok = subprocess.run(argv, capture_output=True).returncode == 0
    except OSError:
        ok = False
    if not ok:
        try:
            os.remove(marker)
        except OSError:
            pass
        return None
    return marker
