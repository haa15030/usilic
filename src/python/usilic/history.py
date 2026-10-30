"""~/.usilic/historyの読み書き(C++のhistory.cppのHistoryと、main.cppのsaveHistoryToDisk())。

C++版と同じファイルを使う(2026-10-22、ユーザーの判断)。壊さないために:
- 文字列はバイトのまま扱う(latin-1で1バイト=1字のstrにする。名前にUTF-8でないバイトがあっても往復する)。
- 末尾のUSILICの行(GUIの設定等、C++版が足していく欄)は解釈せず、読んだ字句をそのまま書き戻す。
- 保存はC++版と同じく、ロック(history.lock)→ディスクの最新を読む→このセッションの変更(journal)だけを
  重ねる→一時ファイルに書いてrename(元はhistory.oldへ)。
"""
import os
import re
import time

FORM_FEED = '\x0c'   # セクションの見出しの印(legacyのK_l)
SUB_EOF = '\x1a'     # ファイルの終わりの番兵(K_z)
SPACE_MARK = '\x01'  # 空白(K_a)
CTRL_MARK = '\x0f'   # 制御バイトの前置き(K_o)、単独なら空の欄

# PositionCategory・NameSetCategory・RingCategory(history.hpp)
FILE, DIRECTORY, MAN_PAGE, SCREEN = 'File', 'Directory', 'ManPage', 'Screen'
CUT_PATTERN, COMMAND_MODE_LABEL = 'CutPattern', 'CommandModeLabel'
SHELL_COMMAND, RECENT_FILE, RECENT_DIRECTORY, RECENT_MAN_PAGE = 'ShellCommand', 'RecentFile', 'RecentDirectory', 'RecentManPage'
RECENT_LOCATE_QUERY, RECENT_FGREP_PATTERN, RECENT_EGREP_PATTERN, USILIC = (
    'RecentLocateQuery', 'RecentFgrepPattern', 'RecentEgrepPattern', 'Usilic')
USILIC_TAG = ' (usilic)'  # kUsilicTag

_LEADING_POSITIONS = [(' FILE', FILE), (' DIR', DIRECTORY), (' MAN', MAN_PAGE)]
_NAME_SETS = [(' CUT', CUT_PATTERN), (' COMMAND', COMMAND_MODE_LABEL)]
_RINGS = [(' HISTORY', SHELL_COMMAND), (' FILE_HISTORY', RECENT_FILE), (' DIR_HISTORY', RECENT_DIRECTORY),
          (' MAN_HISTORY', RECENT_MAN_PAGE), (' LOCATE_HISTORY', RECENT_LOCATE_QUERY),
          (' FGREP_HISTORY', RECENT_FGREP_PATTERN), (' EGREP_HISTORY', RECENT_EGREP_PATTERN),
          (' USILIC_HISTORY', USILIC)]


def to_raw(name):
    """Pythonの名前(UTF-8、読めないバイトはsurrogateescape)を、内部のlatin-1の字(1バイト=1字)に。"""
    return name.encode('utf-8', 'surrogateescape').decode('latin-1')


def from_raw(raw):
    """to_raw()の逆(履歴の中の名前を、パスとして使えるstrに)。"""
    return raw.encode('latin-1').decode('utf-8', 'surrogateescape')


def encode_field(s):
    """encodeField(): 空白はK_a、制御バイトはK_o+(バイト+'@')、空の欄はK_o単独。"""
    if not s:
        return CTRL_MARK
    out = []
    for ch in s:
        c = ord(ch)
        if c and c < 0x20:
            out.append(CTRL_MARK + chr(c + 0x40))
        elif ch == ' ':
            out.append(SPACE_MARK)
        else:
            out.append(ch)
    return ''.join(out)


def decode_field(s):
    """decodeField()。"""
    if s == CTRL_MARK:
        return ''
    out, i = [], 0
    while i < len(s):
        ch = s[i]
        if ch == CTRL_MARK and i + 1 < len(s):
            out.append(chr((ord(s[i + 1]) - 0x40) & 0xFF))
            i += 2
        elif ch == SPACE_MARK:
            out.append(' ')
            i += 1
        else:
            out.append(ch)
            i += 1
    return ''.join(out)


_WHITESPACE = re.compile('[ \t\n\v\f\r]+')


def split_whitespace(s):
    """splitWhitespace()。区切りはCのisspace()の6字だけ(str.split()はlatin-1の\\x85・\\xa0も空白とみなし、
    UTF-8の字のバイトの途中で切ってしまう)。"""
    return [t for t in _WHITESPACE.split(s) if t]


def _is_section_boundary(line):
    return not line or line[0] in (FORM_FEED, SUB_EOF)


class CursorPosition:
    def __init__(self, row=0, total_rows=0, top=0, line_number=-1):
        self.row, self.total_rows, self.top, self.line_number = row, total_rows, top, line_number


class SessionState:
    """SessionState(LINE::old/dir/arg/prev等)。欄は文字列のまま持つ。"""
    def __init__(self):
        self.previous_directory = self.current_directory = self.mode_line_arg = ''
        self.previous_screen_marker = self.last_open_name = ''
        self.pending_cut_buffer = self.pending_cut_program = ''
        self.epub_vertical = False


class History:
    def __init__(self, retention_count=0):
        self.retention_count = retention_count
        self.positions = {}      # category -> {name: CursorPosition}
        self.name_sets = {}      # category -> [name]
        self.rings = {}          # category -> [name](古い順)
        self.session_state = SessionState()
        self.trailer_tokens = ['0', '1', '0']  # USILICの行(解釈しない、模様の初期値はC++の既定)
        self.journal = []        # このセッションの変更(replay_journal_onto())
        self.on_ring_pushed = None  # setOnRingPushed()(積むたびに保存する呼び出し側へ)
        self.complete = False
        self.errors = []

    # --- 読み取り(名前は呼び出し側のstr、中ではto_raw()の形) ---
    def position_for(self, category, name):
        return self.positions.get(category, {}).get(to_raw(name))

    def recent_entries(self, category):
        return [from_raw(n) for n in self.rings.get(category, [])]

    def names_in(self, category):
        return [from_raw(n) for n in self.name_sets.get(category, [])]

    # --- 変更(journalに積む) ---
    def set_position(self, category, name, position):
        name = to_raw(name)
        self.journal.append(lambda h: h._set_position_impl(category, name, position))
        self._set_position_impl(category, name, position)

    def _set_position_impl(self, category, name, position):
        self.positions.setdefault(category, {})[name] = position

    def remember_name(self, category, name):
        name = to_raw(name)
        self.journal.append(lambda h: h._remember_name_impl(category, name))
        self._remember_name_impl(category, name)

    def _remember_name_impl(self, category, name):
        names = self.name_sets.setdefault(category, [])
        if name not in names:  # 最初の出現が勝つ(save_str())
            names.append(name)

    def push_recent(self, category, name):
        name = to_raw(name)
        self.journal.append(lambda h: h._push_recent_impl(category, name))
        self._push_recent_impl(category, name)
        self.journal.append(lambda h: h._mirror_to_usilic_impl(category, name))
        self._mirror_to_usilic_impl(category, name)
        if self.on_ring_pushed:
            self.on_ring_pushed(category)

    def set_session_state(self, state):
        self.journal.append(lambda h: h._set_session_state_impl(state))
        self._set_session_state_impl(state)

    def _set_session_state_impl(self, state):
        self.session_state = state

    def limit_for(self, category):
        """limitFor(): 0は無制限、USILICはFILE/DIR/MANを1本にするので3倍。"""
        if self.retention_count <= 0:
            return None
        return self.retention_count * 3 if category == USILIC else self.retention_count

    def _trim(self, category):
        limit = self.limit_for(category)
        ring = self.rings[category]
        while limit is not None and len(ring) > limit:
            ring.pop(0)

    def _mirror_to_usilic_impl(self, source, name):
        """mirrorToUsilicImpl(): FILE/DIR/MANを時系列の1本(USILIC)にも写す。"""
        if source == RECENT_FILE:
            entry = name
        elif source == RECENT_DIRECTORY:
            entry = name
            if not entry.endswith(USILIC_TAG) and not entry.endswith('/'):
                entry += '/'
        elif source == RECENT_MAN_PAGE:
            entry = name + ' (man)'
        else:
            return
        ring = self.rings.setdefault(USILIC, [])
        if ring and ring[-1] == entry:
            return
        if len(ring) >= 2 and ring[-2] == entry:  # 1つ前との行き来は入れ替えるだけ(_push_recent_impl()参照)
            ring[-2], ring[-1] = ring[-1], ring[-2]
            return
        ring.append(entry)
        self._trim(USILIC)

    def _push_recent_impl(self, category, name):
        """pushRecentImpl(): 直前と同じなら積まない。コマンドは前の同じものを消す。FILEは親もDIRへ。"""
        ring = self.rings.setdefault(category, [])
        if ring and ring[-1] == name:
            return
        if category == SHELL_COMMAND:
            ring[:] = [n for n in ring if n != name]
        # 1つ前と同じものは積み足さずに末尾の2つを入れ替える(^J^Jの行き来でA,B,A,B…と積まない、C++の項目1122)
        if len(ring) >= 2 and ring[-2] == name:
            ring[-2], ring[-1] = ring[-1], ring[-2]
        else:
            ring.append(name)
            self._trim(category)
        if category == RECENT_FILE and not name.endswith(USILIC_TAG):
            path_part = name
            if len(path_part) >= 4 and path_part[-1] in '"/':  # 「パス "パターン"」の合成形はパスの部分だけ
                open_pos = path_part.rfind(' ' + path_part[-1], 0, len(path_part) - 1)
                if open_pos >= 0:
                    path_part = path_part[:open_pos]
            slash = path_part.rfind('/')
            if slash >= 0:
                self._push_recent_impl(RECENT_DIRECTORY, path_part[:slash + 1])

    def replay_journal_onto(self, target):
        for op in self.journal:
            op(target)

    # --- シリアライズ ---
    def load_history_text(self, text):
        """loadHistoryText()(textはlatin-1でデコードしたstr)。読めなければFalse。"""
        self.complete = False
        self.errors = []
        lines = text.split('\n')
        if lines and lines[-1] == '':
            lines.pop()
        i = 0
        while i < len(lines) and (not lines[i] or lines[i][0] == '#'):
            i += 1
        if i >= len(lines) or lines[i] != FORM_FEED + ' Current buffer':
            self.errors.append("missing 'Current buffer' marker")
            return False
        i += 1
        if i >= len(lines):
            self.errors.append("truncated after 'Current buffer' marker")
            return False
        toks = split_whitespace(lines[i])
        state = SessionState()
        fields = ['previous_directory', 'current_directory', 'mode_line_arg', 'previous_screen_marker',
                  'last_open_name']
        for index, field in enumerate(fields):
            if len(toks) > index:
                setattr(state, field, decode_field(toks[index]))
        if len(toks) > 6:
            state.pending_cut_buffer = decode_field(toks[6])
        if len(toks) > 7:
            state.pending_cut_program = decode_field(toks[7])
        if len(toks) > 8:
            state.epub_vertical = toks[8] == '1'
        self.session_state = state
        i += 1

        def expect(label):
            nonlocal i
            if i >= len(lines) or lines[i] != FORM_FEED + label:
                self.errors.append("expected section '%s'" % label)
                return False
            i += 1
            return True

        def read_positions(category):
            nonlocal i
            while i < len(lines) and not _is_section_boundary(lines[i]):
                toks = split_whitespace(lines[i])
                i += 1
                if len(toks) < 5:
                    continue
                pos = CursorPosition(max(0, _atoi(toks[1])), max(0, _atoi(toks[2])), max(0, _atoi(toks[3])),
                                     _atoi(toks[4]))
                if pos.line_number == 0:
                    pos.line_number = -1
                self.positions.setdefault(category, {})[decode_field(toks[0])] = pos

        def read_names(category):
            nonlocal i
            while i < len(lines) and not _is_section_boundary(lines[i]):
                self._remember_name_impl(category, decode_field(lines[i]))
                i += 1

        def read_ring(category):
            nonlocal i
            ring = self.rings.setdefault(category, [])
            while i < len(lines) and not _is_section_boundary(lines[i]):
                name = decode_field(lines[i])
                if category == SHELL_COMMAND:
                    ring[:] = [n for n in ring if n != name]
                ring.append(name)
                self._trim(category)
                i += 1

        for label, category in _LEADING_POSITIONS:
            if not expect(label):
                return False
            read_positions(category)
        for label, category in _NAME_SETS:
            if not expect(label):
                return False
            read_names(category)
        for label, category in _RINGS:
            if category == USILIC and (i >= len(lines) or lines[i] != FORM_FEED + label):
                continue  # USILIC_HISTORYの無い古い形式
            if not expect(label):
                return False
            read_ring(category)
        if not expect(' MODE'):
            return False
        read_positions(SCREEN)
        if not expect(' USILIC'):
            return False
        if i >= len(lines):
            self.errors.append('truncated at USILIC trailer')
            return False
        self.trailer_tokens = split_whitespace(lines[i])
        i += 1
        self.complete = i < len(lines) and lines[i][:1] == SUB_EOF
        if not self.complete:
            self.errors.append('missing EOF sentinel -- file may be truncated')
        return True

    def serialize(self, program_name, timestamp_text):
        """serialize()(latin-1のstrを返す)。"""
        out = ['# ~/.%s/history: %s\n' % (program_name, program_name),
               '# E\bEX\bXI\bIT\bT: %s\n' % timestamp_text,
               "# Note: The lines begin with the character \\014 or \\032 mustn't be deleted.\n\n",
               FORM_FEED + ' Current buffer\n']
        s = self.session_state
        out.append(' '.join([encode_field(s.previous_directory), encode_field(s.current_directory),
                             encode_field(s.mode_line_arg), encode_field(s.previous_screen_marker),
                             encode_field(s.last_open_name), '0', encode_field(s.pending_cut_buffer),
                             encode_field(s.pending_cut_program)]) + (' 1' if s.epub_vertical else ' 0') + '\n')

        def positions(label, category):
            out.append(FORM_FEED + label + '\n')
            for name in sorted(self.positions.get(category, {})):  # std::mapと同じ名前の順(バイト順)
                p = self.positions[category][name]
                out.append('%s %d %d %d %d\n' % (encode_field(name), p.row, p.total_rows, p.top, p.line_number))

        def names(label, category):
            out.append(FORM_FEED + label + '\n')
            for name in self.name_sets.get(category, []):
                out.append(encode_field(name) + '\n')

        for label, category in _LEADING_POSITIONS:
            positions(label, category)
        for label, category in _NAME_SETS:
            names(label, category)
        for label, category in _RINGS:
            out.append(FORM_FEED + label + '\n')
            for name in self.rings.get(category, []):  # 古い順(ファイルの順)
                out.append(encode_field(name) + '\n')
        positions(' MODE', SCREEN)
        out.append(FORM_FEED + ' USILIC\n')
        out.append(' '.join(self.trailer_tokens) + '\n')
        out.append(SUB_EOF + '\n')
        return ''.join(out)


def _atoi(s):
    """std::atoi(): 先頭の数字だけ、読めなければ0。"""
    s = s.strip()
    sign, i = 1, 0
    if s[:1] in '+-' and s:
        sign = -1 if s[0] == '-' else 1
        i = 1
    j = i
    while j < len(s) and s[j].isdigit():
        j += 1
    return sign * int(s[i:j]) if j > i else 0


# --- ファイル(main.cppのreadFileIfExists()・acquireHistoryLock()・saveHistoryToDisk()) ---
def read_file_if_exists(path):
    try:
        with open(path, 'rb') as f:
            return f.read().decode('latin-1')
    except OSError:
        return ''


def load_from_disk(usilic_dir, retention_count=0):
    """起動時に読む(壊れていればhistory.oldへ、main.cppと同じ)。"""
    history = History(retention_count)
    text = read_file_if_exists(os.path.join(usilic_dir, 'history'))
    if text:
        history.load_history_text(text)
        if not history.complete:
            old = read_file_if_exists(os.path.join(usilic_dir, 'history.old'))
            if old:
                history = History(retention_count)
                history.load_history_text(old)
    return history


def _try_create_lock(path):
    try:
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
    except OSError:
        return False
    os.write(fd, ('%d\n' % os.getpid()).encode())
    os.close(fd)
    return True


def _acquire_lock(path):
    for _ in range(100):
        if _try_create_lock(path):
            return
        try:
            with open(path) as f:
                holder = int(f.read().split()[0])
        except (OSError, ValueError, IndexError):
            holder = 0
        alive = False
        if holder > 0 and holder != os.getpid():
            try:
                os.kill(holder, 0)
                alive = True
            except OSError:
                alive = False
        if not alive:
            try:
                os.remove(path)
            except OSError:
                pass
            continue
        time.sleep(0.05)
    try:
        os.remove(path)
    except OSError:
        pass
    _try_create_lock(path)


def current_timestamp_text():
    """asctime()の形(末尾の改行を除く)。"""
    return time.asctime(time.localtime())


def save_to_disk(session, usilic_dir):
    """saveHistoryToDisk(): ロック→最新を読む→このセッションの変更を重ねる→一時ファイル→rename。"""
    if not session.journal:
        return
    path = os.path.join(usilic_dir, 'history')
    old_path = os.path.join(usilic_dir, 'history.old')
    lock_path = os.path.join(usilic_dir, 'history.lock')
    _acquire_lock(lock_path)
    try:
        disk = History(session.retention_count)
        current = read_file_if_exists(path)
        if current:
            disk.load_history_text(current)
            if not disk.complete:
                old = read_file_if_exists(old_path)
                if old:
                    disk = History(session.retention_count)
                    disk.load_history_text(old)
        session.replay_journal_onto(disk)
        tmp_path = '%s.tmp.%d' % (path, os.getpid())
        with open(tmp_path, 'wb') as f:
            f.write(disk.serialize('usilic', current_timestamp_text()).encode('latin-1'))
        if current:
            os.replace(path, old_path)
        os.replace(tmp_path, path)
        session.journal.clear()  # 反映した分は次の保存で重ね直さない(C++のHistory::clearJournal())
    finally:
        try:
            os.remove(lock_path)
        except OSError:
            pass
