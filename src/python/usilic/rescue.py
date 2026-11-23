"""RESCUE(^J^R、K-156): /opt/U.sh(rescue-USB・RAMDISKの道具、src/U_proto.sh)の前に付ける画面。

U.shの中身は移さず、U.shに足した--list(一覧を出して最初の問いで止まる)・--ans=答え(問いへの答えを順に)・
--batch(/dev/ttyから読まない)で動かす(docs/CHANGES.md項目1343・1344)。
  1. 引数無しの--listで、そのホストで使えるオプションの一覧(「  -a(dd_strage)」等)
  2. -a・-f・-d・-nは--listで番号の一覧(「 3)ntfs /dev/sda1 …」等)を出し、選んだ番号を最初の答えに
  3. 引数の要るオプション(-s name等)はモードラインで引数を打つ
  4. その後に聞くこと(QUESTIONS、U.shの_askの順)をモードラインで聞き、--ans=で渡す
  5. U.shは別のプロセス(別のセッション)で動かし、出力はログのファイルへ。SHELLのtail -fで見る
     (usilicが終わっても、-uの1時間の途中で止まらない)
sudoのパスワードはモードラインで(伏せて)聞き、sudo -Sに渡す。覚えるのは1回の操作の間だけ。
"""
import os
import re
import shlex
import subprocess
import sys
import threading

U_SH = '/opt/U.sh'
LOG = '/tmp/usilic-rescue.log'

# 番号の一覧を出すオプション
LIST_OPTIONS = ('-a', '-d', '-f', '-n')
# すぐ終わって出力を見るだけのもの: SHELLのtail -fでなく、出力をFILEで(^Gで戻る)
VIEW_OPTIONS = ('-v',)
# 一覧の番号(または引数)の後にU.shが聞くこと(U.shの_askの順)。(問い, 既定の答え)
QUESTIONS = {
    '-d': [('Install and save? (Y/N) ', 'N'), ('Start it now? (Y/n) ', 'Y')],
    '-n': [('Reboot now? (Y/n) ', 'Y')],
    '-m': [('Erase the device and copy? (Y/N) ', 'N')],
    '-e': [('Delete base? (Y/N) ', 'N')],
    '-u': [('Reboot now (first time only)? (Y/n) ', 'Y')],
}
# -fの一覧の頭に足す項目(「. U -f -」: 全部外す)
UNMOUNT_ALL = '-) unmount all /mnt/No.'

_OPTION = re.compile(r'^\s*(-[a-z])\(([^)]*)\)\s*(.*)$')
_NUMBER = re.compile(r'^\s*(\d+)\)(.*)$')


def available():
    """RESCUEを使えるか(Linuxで/opt/U.shがある)。"""
    return sys.platform.startswith('linux') and os.path.isfile(U_SH)


def parse_options(text):
    """U.sh --list(オプション無し)の出力から[(オプション, 表示)]。表示は「-a(dd_strage)」等の行のまま。"""
    out = []
    for line in text.splitlines():
        m = _OPTION.match(line)
        if m:
            out.append((m.group(1), line.strip()))
    return out


def takes_argument(display):
    """「-s(ave_host) name [.] or .」のように、名前の後に引数の説明がある。"""
    m = _OPTION.match(display)
    return bool(m and m.group(3).strip() and not m.group(3).lstrip().startswith(':'))


def parse_numbers(text):
    """U.sh -f --list等の出力から、番号の付いた行([(番号, 行)])。「-)」(マウントできない)・「 )<EXIT>」は除く。"""
    out = []
    for line in text.splitlines():
        m = _NUMBER.match(line)
        if m:
            out.append((m.group(1), line.strip()))
    return out


def number_of(item):
    """一覧の項目から番号(「-」は-fの全部外す)。"""
    if item == UNMOUNT_ALL:
        return '-'
    m = _NUMBER.match(item)
    return m.group(1) if m else None


def need_password():
    """sudoにパスワードが要るか(rootなら要らない、sudo -nで確かめる)。"""
    if os.geteuid() == 0:
        return False
    # 2026-11-23、直接のユーザー報告(-fで「sudo: パスワードが必要です」と出てSHELLのプロンプトに戻り、マウントされない):
    # sudoの覚えたパスワードは端末ごと(timestamp_type=tty)なので、usilicの端末ではsudo -nが通っても、別のセッション
    # (start()、端末が無い)のsudo -nは通らない。確かめる時も別のセッションで。
    try:
        return subprocess.run(['sudo', '-n', 'true'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=10, start_new_session=True).returncode != 0
    except (OSError, subprocess.TimeoutExpired):
        return True


# U.shのPython版(usilic/ush/、ush.py)。移したオプション(ush.PORTED)はこちらで動かす
USH_PY = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'ush.py')


def engine(args):
    """argsを動かすもの: 移したオプションはPython版(ush.py)、他はU.sh。"""
    from .ush import PORTED
    if args and args[0] in PORTED and os.path.isfile(USH_PY):
        return [sys.executable or 'python3', USH_PY]
    return ['bash', U_SH]


def command(args, password):
    """U.sh(移したオプションはPython版)を動かすargv(sudo -Sで、パスワードは標準入力から。rootならsudo無し)。"""
    argv = engine(args) + list(args)
    if os.geteuid() == 0:
        return argv
    if password is None:
        return ['sudo', '-n', '--'] + argv
    return ['sudo', '-S', '-p', '', '--'] + argv


def check_password(password):
    try:
        return subprocess.run(['sudo', '-S', '-p', '', '-v'], input=(password + '\n').encode(),
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def capture(args, password, timeout=180):
    """U.shを動かして出力(標準出力と標準エラー出力)を返す。一覧(--list)用。"""
    data = (password + '\n').encode() if password is not None else b''
    try:
        result = subprocess.run(command(args, password), input=data, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.decode('utf-8', 'replace')


def start(args, password):
    """U.shを別のセッションで動かし、出力をLOGへ。(pid, tailのコマンド行)を返す(動かせなければNone)。"""
    try:
        log = open(LOG, 'wb')
    except OSError:
        return None
    try:
        proc = subprocess.Popen(command(args, password), stdin=subprocess.PIPE, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
    except OSError:
        log.close()
        return None
    log.close()
    # 終わったら待ち取る(待ち取らないとゾンビとして残り、tail --pid=がいつまでも終わらなかった。-vの後のSHELLで、
    # プロンプトが戻らずqも利かないと報告された)
    threading.Thread(target=proc.wait, daemon=True).start()
    try:
        if password is not None:
            proc.stdin.write((password + '\n').encode())
        proc.stdin.close()
    except OSError:
        pass
    shown = 'U.sh ' + ' '.join(shlex.quote(a) for a in args if not a.startswith('--'))
    tail = 'echo %s; tail -n +1 --pid=%d -f %s' % (shlex.quote('# ' + shown + ' -> ' + LOG), proc.pid, LOG)
    return proc.pid, tail


def answer_args(answers):
    return ['--ans=' + a for a in answers]
