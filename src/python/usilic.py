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
from usilic import help_text, messages_ja  # noqa: E402


USAGE = """usage: usilic [options] [<file] [file] [directory]
  -c            CUI (terminal) mode
  -g            GUI (window) mode (not in this Python version: starts in CUI)
  -H            also open HELP in a child usilic
  -s usec ...   scroll SPEED per level (1-10)
  -d dot ...    flick DRAG thresholds per level (1-10)
  -f file       FONT file for -g
  -z pt         font size for -g (default: last size, or 18)
  -w wait       macro WAIT (x 10 msec)
  -b MB         BUFFER size (1-100)
  -l lines      history LINE count
  -S -D -F -W -B -L   same as lowercase, and also write it to ~/.usilic/usilicrc
  -e HELP.1     write HELP.1 as an EPUB to stdout and exit
"""


def read_help_companion(name, markup_path):
    """C++のreadHelpCompanion(): $USILIC_EPUB_RC・$USILIC_EPUB_NOTES・$USILIC_EPUB_COVER、無ければ
    ~/.local/lib/usilic/、/usr/local/lib/usilic/、原稿と同じディレクトリの順に探す。"""
    variable = ('USILIC_EPUB_RC' if name.startswith('usilicrc') else
                'USILIC_EPUB_NOTES' if name.startswith('HELP.') else 'USILIC_EPUB_COVER')
    binary = name.endswith('.png')

    def read(path):
        try:
            with open(path, 'rb') as f:
                data = f.read()
            return data if binary else data.decode('utf-8', 'replace')
        except OSError:
            return None
    given = os.environ.get(variable)
    if given:
        return read(given) or (b'' if binary else '')
    for directory in (os.path.join(os.path.expanduser('~'), '.local', 'lib', 'usilic'), '/usr/local/lib/usilic',
                      os.path.dirname(markup_path) or '.'):
        text = read(os.path.join(directory, name))
        if text is not None:
            return text
    return b'' if binary else ''


def help_companions(roff, markup_path):
    """C++のreadHelpCompanions(): 英語の原稿ならusilicrc.en・HELP.en.notes。表紙はusilic_icon_256.png。"""
    from usilic import help_to_epub
    english = help_to_epub.is_english_roff(roff)
    return (read_help_companion('usilicrc.en' if english else 'usilicrc', markup_path),
            read_help_companion('HELP.en.notes' if english else 'HELP.notes', markup_path),
            read_help_companion('usilic_icon_256.png', markup_path))


def getline_split(text):
    """std::getline()で読んだ行(最後の改行の後ろの空の行は無し)。"""
    lines = text.split('\n') if text else []
    if lines and lines[-1] == '':
        lines.pop()
    return lines


def run_text_option(option, path):
    """-m・-n・-e(画面を使わずに標準出力へ書いて終わる、C++のmain()と同じ)。"""
    from usilic import help_to_epub
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            source = f.read()
    except OSError:
        print('usilic: %s: cannot read %s.' % (option, path), file=sys.stderr)
        return 1
    out = sys.stdout.buffer
    if option == '-m':
        out.write(help_to_epub.convert_markup_to_legacy_help_text(source).encode('utf-8'))
        return 0
    if option == '-n':
        roff = help_to_epub.convert_markup_to_ms_roff_source(source)
        rc, notes, cover = help_companions(roff, path)
        lines = [roff]
        for l in getline_split(notes):
            if not l or l[0] != '#':
                lines.append('.\\" usilic-notes-line' + (' ' + l if l else '') + '\n')
        for l in getline_split(rc):
            lines.append('.\\" usilic-rc-line' + (' ' + l if l else '') + '\n')
        if cover:
            import base64
            encoded = base64.b64encode(cover).decode('ascii')
            for k in range(0, len(encoded), 76):
                lines.append('.\\" usilic-cover-line ' + encoded[k:k + 76] + '\n')
        out.write(''.join(lines).encode('utf-8'))
        return 0
    roff = source if source.startswith('.') else help_to_epub.convert_markup_to_ms_roff_source(source)
    rc, notes, cover = help_companions(roff, path)
    epub, summary, unnoted = help_to_epub.convert_ms_roff_to_help_epub(roff, rc, notes, cover)
    out.write(epub)
    out.flush()
    print('usilic -e: ' + summary, file=sys.stderr)
    if unnoted:
        print('usilic -e: no note: ' + ' '.join(unnoted), file=sys.stderr)
    return 0


def parse_options(argv):
    """C++のmain()のオプション。(オプションの辞書, 位置引数, 終了コード(すぐ終わるなら))。"""
    options = {'help_child': False, 'key_child': False, 'key_of_pager_parent': False, 'rc': []}
    positional = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-c', '-g'):
            pass
        elif a == '-H':
            options['help_child'] = True
        elif a == '-K':
            options['key_child'] = True
        elif a == '-P':
            options['key_of_pager_parent'] = True
        elif a == '-z':
            value = argv[i + 1] if i + 1 < len(argv) else ''
            if not value.isdigit() or not 4 <= int(value) <= 200:
                print('usilic: -z requires a font size in points (4-200, e.g. -z 20).', file=sys.stderr)
                return options, positional, 1
            i += 1
        elif a == '-h':
            sys.stdout.write(USAGE)
            return options, positional, 0
        elif len(a) == 2 and a[0] == '-' and a[1] in 'sSdD':
            values = []
            while i + 1 < len(argv) and argv[i + 1].isdigit():
                values.append(argv[i + 1])
                i += 1
            if not values:
                print('usilic: -%s requires one or more %s.' % (
                    a[1], 'microsecond values (e.g. -s 90000 20000 10000)' if a[1] in 'sS'
                    else 'pixel-distance values (e.g. -d 1 2 4 8 16)'), file=sys.stderr)
                return options, positional, 1
            options['rc'].append((a[1], values))
        elif len(a) == 2 and a[0] == '-' and a[1] in 'fFwWbBlL':
            numeric = a[1] not in 'fF'
            if i + 1 >= len(argv) or (numeric and not argv[i + 1].isdigit()):
                print('usilic: -%s requires %s.' % (
                    a[1], 'a number' if numeric else 'a font file path (e.g. -f /Library/Fonts/MS-Gothic.ttf)'),
                    file=sys.stderr)
                return options, positional, 1
            options['rc'].append((a[1], [argv[i + 1]]))
            i += 1
        elif a in ('-m', '-n', '-e'):
            if i + 1 >= len(argv):
                print('usilic: %s requires a %s path.' % (a, 'HELP.1' if a == '-e' else 'markup file'), file=sys.stderr)
                return options, positional, 1
            return options, positional, run_text_option(a, argv[i + 1])
        else:
            positional.append(a)
        i += 1
    return options, positional, None


def apply_rc_options(configuration, rc_options):
    """C++のapplyRcOptions(): 小文字はこの起動だけ、大文字は~/.usilic/usilicrcへも書く。"""
    from usilic import mouse, usilicrc_file
    home = os.path.expanduser('~')
    for letter, values in rc_options:
        lower = letter.lower()
        if lower == 's':
            levels = len(mouse.SCROLL_SPEED) - 1
            if len(values) > levels:
                print('usilic: -%s only tunes %d levels; the extra %d trailing value(s) will be ignored.' % (
                    letter, levels, len(values) - levels), file=sys.stderr)
            configuration.speed_values = [int(v) for v in values[:levels]]
            current = mouse.SCROLL_SPEED[1:]
            section = 'SPEED'
            saved = [' '.join(str(int(values[k]) if k < len(values) else current[k]) for k in range(levels))]
        elif lower == 'd':
            configuration.drag_values = [int(v) for v in values]
            section, saved = 'DRAG', [' '.join(values)]
        elif lower == 'f':
            configuration.font_path = values[0]
            section, saved = 'FONT', [values[0]]
        elif lower == 'w':
            configuration.wait_centiseconds = int(values[0])
            section, saved = 'WAIT', [values[0]]
        elif lower == 'b':
            if not 1 <= int(values[0]) <= 100:
                print('usilic: -%s: value out of range (1-100): %s' % (letter, values[0]), file=sys.stderr)
                continue
            configuration.buffer_megabytes = int(values[0])
            section, saved = 'BUFFER', [values[0]]
        elif lower == 'l':
            configuration.retention_count = int(values[0])
            section, saved = 'LINE', [values[0]]
        else:
            continue
        if letter != lower and not usilicrc_file.set_section(home, section, saved):
            print('usilic: -%s: cannot write %s to %s' % (letter, section, usilicrc_file.user_path(home)),
                  file=sys.stderr)


def main():
    locale.setlocale(locale.LC_ALL, '')
    options, args, code = parse_options(sys.argv[1:])
    if code is not None:
        return code
    from usilic import usilicrc_file
    home = os.path.expanduser('~')
    usilicrc_file.ensure_user(home)  # 無ければ既定のusilicrcを写す、既定の方が新しければ置き換える(C++と同じ)
    usilicrc_file.update_user_if_older(home)
    # usilicの中から起動した子(^Ou・^Jtのusilic等、C++のUSILIC_TRANSIENT=親のPID): 右下は「USILIC」
    transient = os.environ.pop('USILIC_TRANSIENT', '')
    parent_pid = int(transient) if transient.isdigit() else 0
    if parent_pid > 1:
        from usilic import application
        application.PROGRAM_LABEL = 'USILIC'
    window_slot = os.environ.pop('USILIC_WINDOW_SLOT', '')
    window_slot = int(window_slot) if window_slot.isdigit() else 0
    help_jump = os.environ.pop('USILIC_HELP_JUMP', '') or None  # 親が開き直したHELPの子が飛ぶ行
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
        apply_rc_options(configuration, options['rc'])  # -s・-S等(オプション > usilicrc > 組み込み)
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
            ephemeral = os.environ.pop('USILIC_NO_HISTORY', '') == '1' or bool(transient)
            # 最初の起動(~/.usilic/historyが無い、C++のfirstPagerStart): HELP(EPUB)をページャーとして開き、qで
            # 終わる。historyは既定値のままその場で作る(次の起動から普段の画面)。
            first_start = not ephemeral and not os.path.exists(os.path.join(usilic_dir, 'history'))
            if first_start:
                os.makedirs(usilic_dir, exist_ok=True)
            history = (hist.load_from_disk(usilic_dir, configuration.retention_count)
                       if os.path.isdir(usilic_dir) and not ephemeral else None)
            if first_start and history is not None:
                hist.save_to_disk(history, usilic_dir)
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
            if not ephemeral:  # 足りないプログラムのインストールの案内(実行ループの最初でSHELLへ)
                from usilic import install_offer
                offer = install_offer.plan(usilic_dir)
                if offer:
                    app.pending_shell_offer = (offer[0], offer[1], 3, offer[2])
            if first_start and help_text.help_roff_path(messages_ja.japanese()):
                app.pager_launch = True
                app.first_start_pager = True
                app.shell.display_name = 'USILIC'
                app.help_in_parent_pending = True

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
        app.setup_children(parent_pid, window_slot)  # 親子の受け渡しのFIFO(C++のopenChildChannel())
        app.help_jump_on_start = help_jump
        key_child = options['key_child'] and parent_pid <= 1
        app.children_pending = (options['help_child'], key_child)  # -H・-K(実行ループの最初で開く)
        if options['key_child'] and not key_child:  # -K(子のKEYのウィンドウ自身): 「：」の行だけのKEY
            app.pager_launch = options['key_of_pager_parent']  # -P: ページャーの親のKEY(行頭がタブのキーだけ)
            app.key_colon_list_launch = True
            app.start_key()
        signal.signal(signal.SIGTERM, lambda signum, frame: setattr(app, 'wants_exit', True))
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
