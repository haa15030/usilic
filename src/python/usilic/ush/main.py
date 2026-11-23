"""ush.pyの入口: 引数(U.shと同じ)・問い(_ask)・ログ(/tmp/log.*の代わり、終わる時に/opt/U.log_日付へ)。"""
import datetime
import sys

from . import PORTED
from .env import Env
from .fs import MountError, fs
from .options import add_strage, change_root, file_system, next_host, page_with_usilic, verbose
from .packages import output_u
from .system import System


class Ask:
    """_ask(): --ans=の答えを問いの順に。尽きたら既定(番号の問いは空=選ばない、Y/nはn・N)。前から答えを
    渡されていなければ端末(/dev/tty)から読む。"""

    def __init__(self, answers, batch, out):
        self.answers = list(answers)
        self.batch = batch
        self.out = out

    def __call__(self, question, default=''):
        if self.batch:
            value = self.answers.pop(0) if self.answers else default
            self.out(question + value)
            return value
        try:
            with open('/dev/tty') as tty:
                sys.stdout.write(question)
                sys.stdout.flush()
                return tty.readline().rstrip('\n')
        except OSError:
            return default


class Log:
    """U.shの/tmp/log.*(終わる時に出して、/opt/U.log_日付へ。RAMDISKならDEBIAN区画の/opt/U.log_日付へも)。"""

    def __init__(self):
        self.lines = []

    def add(self, text):
        self.lines.append(text)

    def save(self, env, out):
        if not self.lines:
            return
        text = ''.join(l + '\n' for l in self.lines)
        for line in self.lines:
            out(line)
        name = 'U.log_' + datetime.date.today().isoformat()
        system = env.system
        system.append_text('/opt/' + name, text)
        if env.on_ramdisk:
            mounted = system.is_mountpoint(env.mnt_debian)
            if mounted or fs(env, Log(), 'mount', env.mnt_debian, try_only=True, wait=0):
                system.append_text(env.mnt_debian + '/opt/' + name, text)
                if not mounted:
                    system.run(['umount', env.mnt_debian])


def parse(argv):
    """[オプション, 引数...]・答え・前から使うか(--list・--ans=・--batch)。"""
    words, answers, batch = [], [], False
    for a in argv:
        if a in ('--list', '--batch'):
            batch = True
        elif a.startswith('--ans='):
            batch = True
            answers.append(a[len('--ans='):])
        else:
            words.append(a)
    return words, answers, batch


def run(argv, system=None, uname=None, out=None):
    system = system or System()
    if out is None:
        def out(text):
            print(text, flush=True)
    words, answers, batch = parse(argv)
    if not words or words[0] not in PORTED:
        out('ush: %s: not in the Python version yet (use /opt/U.sh). Ported: %s'
            % (words[0] if words else '(none)', ' '.join(PORTED)))
        return 2
    if system.euid() != 0:
        out('ush: run as root (sudo).')
        return 1
    env = Env(system, uname)
    log = Log()
    ask = Ask(answers, batch, out)
    option, args = words[0], words[1:]
    code = 0
    try:
        if option == '-v':
            verbose(env, log, ask, out, lambda text: page_with_usilic(system, text))
        elif option == '-f':
            file_system(env, log, args, ask, out)
        elif option == '-a':
            add_strage(env, log, ask, out)
        elif option == '-n':
            next_host(env, log, ask, out)
        elif option == '-o':
            output_u(env, log, args, out)
        elif option == '-c':
            change_root(env, log, args, out)
    except MountError:
        code = 1
    log.save(env, out)
    return code


def main():
    sys.exit(run(sys.argv[1:]))
