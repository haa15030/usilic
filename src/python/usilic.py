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
from usilic.configuration import Configuration  # noqa: E402
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
        configuration = Configuration()
        configuration.load_default_file()  # ~/.usilic/usilicrc(無ければ~/.local/lib/usilic/usilicrc)
        app = Application(display, KeyInput(0), configuration)
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
            # ^Ouで開いた外のusilicは、その場限りの(ls)(履歴は読み書きしない、C++と同じ)
            ephemeral = os.environ.pop('USILIC_NO_HISTORY', '') == '1'
            history = (hist.load_from_disk(usilic_dir, configuration.retention_count)
                       if os.path.isdir(usilic_dir) and not ephemeral else None)
            if history is not None:
                history.on_ring_pushed = lambda category: (
                    hist.save_to_disk(history, usilic_dir) if category != hist.SHELL_COMMAND else None)
                app.directory_mode.history = app.file_mode.history = app.history = history
            if args:
                started = app.start_directory_listing(args[0])
            else:  # 引数無し: 前回終わった時に見ていた物から(C++版と同じ「Current buffer」の名前)
                last = hist.from_raw(history.session_state.last_open_name) if history is not None else ''
                started = app.start_resuming_session(last, '.')
            if not started:
                return 'usilic: cannot list ' + (args[0] if args else '.')

            def on_exit():
                app.save_positions()
                if history is None:
                    return
                # C++と同じく、ディレクトリで起動した時は見ていた物を残す(引数があっても)
                resumed = hist.SessionState()
                resumed.__dict__.update(history.session_state.__dict__)
                resumed.last_open_name = hist.to_raw(app.current_resumable_name())
                resumed.epub_vertical = False  # 縦横は本が決める
                history.set_session_state(resumed)
                hist.save_to_disk(history, usilic_dir)
            app.on_exit = on_exit
        signal.signal(signal.SIGWINCH, on_winch)
        try:
            app.run(resized)
        finally:
            display.set_mouse_tracking(False)  # 落ちても端末にマウスの報告を残さない
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
