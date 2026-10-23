"""LOCATEモード(C++のlocate_mode.cppのLocateMode)。

`locate <文字列>`の結果の一覧。macOSでは~/.usilic/locate.db(自分のホームの中を自分の権限で集めた
データベース、C++のsetUserLocateDatabasePath())も引き、結果をつなぐ。名前はNFC・NFDの両方で探す
(macOSのファイル名はNFD、C++のunicodeNormalizationCandidates())。
"""
import os
import shlex
import subprocess
import sys
import unicodedata

from .content_view import Line


def normalization_candidates(query):
    out = [query]
    for form in ('NFC', 'NFD'):
        variant = unicodedata.normalize(form, query)
        if variant not in out:
            out.append(variant)
    return out


def user_database_path():
    return os.path.join(os.path.expanduser('~'), '.usilic', 'locate.db')


def user_database_argv():
    """macOSだけ: 個人用のデータベースを引くコマンド。"""
    if sys.platform != 'darwin':
        return None
    return ['locate', '-d', user_database_path()]


def user_database_build_argv():
    """macOSだけ: find -s ~ | locate.mklocatedb(C++と同じパイプライン)。"""
    if sys.platform != 'darwin':
        return None
    path = user_database_path()
    tmp = path + '.tmp'
    script = ('find -s "$HOME" -print 2>/dev/null | /usr/libexec/locate.mklocatedb -presort > %s && mv -f %s %s'
              ' && chmod 600 %s' % (shlex.quote(tmp), shlex.quote(tmp), shlex.quote(path), shlex.quote(path)))
    return ['sh', '-c', script]


class LocateMode:
    def __init__(self, view, configuration):
        self.view = view
        self.configuration = configuration
        self.query = ''
        self.results = []
        self.byte_count = 0

    def _run(self, argv, query, quiet):
        out = []
        for candidate in normalization_candidates(query):
            try:
                result = subprocess.run(argv + [candidate], capture_output=True)
            except OSError:
                return out
            if result.returncode not in (0, 1):
                continue
            for line in result.stdout.decode('utf-8', 'surrogateescape').split('\n'):
                if line and line not in out:
                    out.append(line)
        return out

    def search(self, query):
        """LocateMode::search()。システムのデータベースの後に個人用の分をつなぐ(重複は除く)。"""
        if not query:
            return False
        results = self._run(self.configuration.command_for('Locate'), query, False)
        user = user_database_argv()
        if user and os.path.exists(user_database_path()):
            for path in self._run(user, query, True):
                if path not in results:
                    results.append(path)
        self.query, self.results = query, results
        lines = [Line(p, i + 1, ellipsis=True) for i, p in enumerate(results)]
        if not results:
            lines = [Line(query + ': No results', None, attributes=[(len(query) + 2, len(query) + 12, 'underline')])]
        self.byte_count = sum(len(p.encode('utf-8', 'surrogateescape')) + 1 for p in results)
        self.view.set_lines(lines)
        self.view.cursor_index = self.view.top_index = 0
        return True

    def start_update_db(self):
        """LocateMode::startUpdateDb(): システムの分と個人用の分を、切り離して動かす。"""
        system = self.configuration.command_for('UpdateDb')
        user = user_database_build_argv()
        argv = system if not user else ['sh', '-c', ' '.join(shlex.quote(a) for a in system) + ' ; ' +
                                        ' '.join(shlex.quote(a) for a in user)]
        try:
            subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             start_new_session=True)
        except OSError:
            return False
        return True

    def selected_result(self):
        i = self.view.cursor_index
        return self.results[i] if 0 <= i < len(self.results) else None
