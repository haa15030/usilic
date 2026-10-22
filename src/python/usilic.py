#!/usr/bin/env python3
"""usilicのCUI版(Python3、学習用の移植、C++版はsrc/cpp/)。

  usilic.py [directory]   (ls)
  usilic.py file ...      ページャー(n/p/x/fで切り替え、qで終了)
  cmd | usilic.py         標準入力のページャー
"""
import curses
import locale
import os
import signal
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from usilic import history as hist  # noqa: E402
from usilic.application import Application  # noqa: E402
from usilic.display import TerminalDisplay  # noqa: E402
from usilic.key_input import KeyInput  # noqa: E402


def main():
    locale.setlocale(locale.LC_ALL, '')
    args = sys.argv[1:]
    stdin_stream = None
    if not sys.stdin.isatty():
        # 標準入力のページャー: データは元の標準入力から読み、キーと画面は端末(/dev/tty)へ(C++版と同じ)。
        stdin_stream = os.fdopen(os.dup(0), 'rb')
        tty = os.open('/dev/tty', os.O_RDWR)
        os.dup2(tty, 0)
        os.close(tty)
    resized_flag = [False]

    def on_winch(signum, frame):
        resized_flag[0] = True

    def resized():
        if not resized_flag[0]:
            return False
        resized_flag[0] = False
        size = os.get_terminal_size(0)
        curses.resizeterm(size.lines, size.columns)
        return True

    def run(screen):
        display = TerminalDisplay(screen)
        app = Application(display, KeyInput(0))
        app.layout()
        if stdin_stream is not None and not args:
            app.start_pager([], stdin_stream)
        elif args and not os.path.isdir(args[0]):
            missing = [a for a in args if not os.path.isfile(a)]
            if missing:
                return 'usilic: cannot read ' + missing[0]
            app.start_pager(args)
        else:
            # ディレクトリで起動した時だけ、C++版と同じ~/.usilic/historyを読み書きする(積むたびに保存、C++の
            # setOnRingPushed()と同じくコマンドの履歴は除く)。
            usilic_dir = os.path.join(os.path.expanduser('~'), '.usilic')
            history = hist.load_from_disk(usilic_dir) if os.path.isdir(usilic_dir) else None
            if history is not None:
                history.on_ring_pushed = lambda category: (
                    hist.save_to_disk(history, usilic_dir) if category != hist.SHELL_COMMAND else None)
                app.directory_mode.history = app.file_mode.history = history
            if not app.start_directory_listing(args[0] if args else '.'):
                return 'usilic: cannot list ' + (args[0] if args else '.')
            app.on_exit = lambda: (app.save_positions(), history and hist.save_to_disk(history, usilic_dir))
        signal.signal(signal.SIGWINCH, on_winch)
        app.run(resized)
        if getattr(app, 'on_exit', None):
            app.on_exit()
        return None

    message = curses.wrapper(run)
    if message:
        print(message, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
