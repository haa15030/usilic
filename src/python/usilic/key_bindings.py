"""キーの割り当て(C++のkey_bindings.cppのKeyBindings)。

機能はK-番号(HELPの記載順)で表し、キーは「C-x f」のような書き方(usilicrcのKEY節と同じ)。接頭キーF-0〜F-4
(既定は^X・^J・^O・^C・^J^O)を書き換えると、それを使う機能のキーもまとめて変わる。

C++と同じく、本体(Application)は既定のキー列で書いてあり、KEY節で変えたキーは入口で既定のキー列へ読み替える
(configure()・feed())。番号には場面(全体・COPY・プロンプト・^Xeの直後)があり、COPY・プロンプトの番号はその場面で、
同じ既定のキーの全体の番号に代わって効く(2026-10-23、.steering/20261023-keys-all-in-usilicrc/)。
"""

DIGIT = -1  # 「N」(数字、^[N・^JN)。C++のKeyBindings::kDigit
ESC = 0x1B

DEFAULT_PREFIX_SPECS = ['C-x', 'C-j', 'C-o', 'C-c', 'C-j C-o']

# defaultEntries()と同じ(番号: キー、説明はC++のコメントの通り)。
DEFAULT_ENTRIES = [
    (1, 'j'), (2, 'k'), (3, 'C-n'), (4, 'C-p'), (5, 'SPC'), (6, 'C-v'), (7, 'C-f'), (8, 'b'), (9, 'C-b'),
    (10, 'ESC v'), (11, 'ESC <'), (12, 'ESC >'), (13, 'ESC N'), (14, 'F-0 F-0'),
    (15, 'n'), (16, 'p'), (17, 'x'), (18, 'f'), (19, 'q'), (20, 'C-g'), (21, 'C-l'), (22, 'C-z'),
    (23, 'F-2 f'), (24, 'F-1 C-c'), (25, 'C-]'), (26, 'F-1 C-b'), (27, 'F-1 N'), (28, 'C-m'),
    (29, 'F-2 C-m'), (30, 'ESC C-m'), (31, 'F-1 F-1'), (32, 'F-2 o'), (33, 'F-2 l'), (34, 'F-2 C-l'),
    (35, '.'), (36, 'C-SPC'), (37, 'F-2 y'), (38, 'F-2 n'), (40, 'F-2 c'), (41, 'F-2 m'), (42, 'F-2 r'),
    (43, 'F-1 t'), (44, 'F-1 C-t'), (45, 'F-2 u'), (46, 'F-2 a'), (47, 'F-1 e'), (48, 'F-1 C-e'),
    (49, 'F-2 e'), (50, 'C-r'), (51, 'C-s'), (52, 'F-1 C-s'), (53, 'F-1 p'), (55, 'F-1 d'), (56, 'F-1 C-d'),
    (57, 'F-1 l'), (58, 'F-1 C-l'), (59, 'F-1 m'), (60, 'F-1 C-m'), (61, 'F-0 f'), (62, 'F-0 C-f'),
    (63, 'F-0 d'), (64, 'F-0 C-d'), (65, 'F-2 C-c'), (66, 'F-2 C-y'), (67, 'ESC e'), (68, 'ESC s'),
    (69, 'F-1 s'), (70, 'ESC l'), (71, 'F-1 .'), (72, 'F-1 ,'), (73, 'F-1 w'), (74, 'F-1 C-w'),
    (75, 'F-2 /'), (76, 'F-2 C-f'), (77, 'F-2 C-b'), (78, 'ESC u'), (79, 'F-1 C-u'), (80, 'F-1 u'),
    (81, 'F-0 C-w'), (82, 'C-i'), (83, 'F-1 C-i'), (84, 'F-4 u'), (85, 'F-4 e'), (86, 'F-4 s'),
    (88, 'F-4 d'), (89, 'F-4 w'), (90, 'C-h'), (91, 'F-1 C-h'), (92, 'F-2 j'), (93, 'C-k'), (94, 'F-1 C-k'),
    (95, 'F-0 ('), (96, 'F-0 )'), (97, 'F-0 e'), (98, 'F-0 s'), (99, 'F-0 C-c'), (100, 'ESC ESC'),
    (101, 'F-2 s'), (102, 'F-2 w'), (103, 'F-1 C-a'), (104, 'F-1 C-f'), (105, 'h'), (106, 'F-1 C-p'),
    (107, 'F-2 d'), (108, 'F-2 C-e'), (109, 'ESC ['), (110, 'ESC ]'), (111, 'F-2 t'), (112, 'ESC {'),
    (113, 'ESC }'),
    # 2026-10-23: 以前はコードに書いた値で受けていたキー(C++と同じ番号)
    (114, 'ESC g g'), (115, 'C-u'), (116, 'F-0 z'), (117, 'F-0 r m'), (118, 'F-0 r b'), (119, 'F-0 r l'),
    (120, 'F-0 r SPC'), (121, 'F-0 r C-SPC'), (122, 'F-0 r j'), (123, 'F-2 C-s'), (124, 'F-2 F-1 C-s'),
    (125, '%'), (126, '*'), (127, 'C-t n'), (128, 'C-t p'), (129, 'C-t C-h'), (130, 'C-t C-k'), (131, 'e'),
    (132, 'C-f'), (133, 'C-b'), (134, 'C-a'), (135, 'C-e'), (136, 'F-0 SPC'), (137, 'ESC w'), (138, 'ESC t'),
    (139, 'C-a'), (140, 'C-e'), (141, 'C-f'), (142, 'C-b'), (143, 'C-d'), (144, 'C-h'), (145, 'C-u'),
    (146, 'C-k'), (147, 'C-y'), (148, 'C-_'), (149, 'C-q'), (150, 'C-i'), (151, 'C-w'), (152, 'ESC f'),
    (153, 'ESC b'), (154, 'ESC d'), (155, 'ESC C-h'),
    # 2026-11-23: RESCUE(^J^R)。2026-11-26から156→157(C++版の156は^T^T、^J^RはC++版も157)
    (157, 'F-1 C-r'),
]

# 番号の場面(C++のKeyBindings::Scope、書いていない番号は全体)
GLOBAL, COPY, PROMPT, MACRO_REPEAT = 'global', 'copy', 'prompt', 'macro_repeat'
SCOPES = dict([(131, MACRO_REPEAT)] + [(n, COPY) for n in range(132, 139)] + [(n, PROMPT) for n in range(139, 156)])
# 読み替えの場面(C++のKeyBindings::Context)
CONTEXT_NONE, LEADER_ONLY, ALL, CONTEXT_COPY, CONTEXT_PROMPT = 'none', 'leader', 'all', 'copy', 'prompt'
_LEADER_BYTES = (0x18, 0x0A, 0x0F, ESC)  # isLeaderByte()

CTRL_SPACE = 0x80  # KeyInputは^Space(NUL)を0x80として報告する(C++と同じ)


def parse_spec(spec, prefixes):
    """KeyBindings::parseSpec(): 「F-1 C-s」→[0x0A, 0x13]。誤りは(None, 理由)。"""
    out, saw_digit = [], False
    for token in spec.split():
        if saw_digit:
            return None, 'N must be the last key: ' + spec
        if len(token) == 3 and token.startswith('F-') and token[2].isdigit():
            n = int(token[2])
            if n >= len(prefixes) or not prefixes[n]:
                return None, 'undefined prefix %s: %s' % (token, spec)
            out.extend(prefixes[n])
        elif token in ('C-SPC', 'C-@'):
            out.append(CTRL_SPACE)
        elif len(token) == 3 and token.startswith('C-'):
            c = token[2]
            if 'a' <= c <= 'z':
                out.append(ord(c) - ord('a') + 1)
            elif c in '[\\]^_':
                out.append(ord(c) - 0x40)
            else:
                return None, 'invalid key %s: %s' % (token, spec)
        elif token == 'ESC':
            out.append(ESC)
        elif token == 'SPC':
            out.append(0x20)
        elif token == 'N':
            out.append(DIGIT)
            saw_digit = True
        elif len(token) == 1 and 0x20 < ord(token) < 0x7F:
            out.append(ord(token))
        else:
            return None, 'invalid key %s: %s' % (token, spec)
    if not out:
        return None, 'empty'
    return out, None


def _same_head(shorter, longer):
    """shorterがlongerの頭(Nは数字のどれとも合う、patternIsProperPrefix())。"""
    if len(shorter) >= len(longer):
        return False
    for s, l in zip(shorter, longer):
        if s == l or (s == DIGIT and 0x30 <= l <= 0x39) or (l == DIGIT and 0x30 <= s <= 0x39):
            continue
        return False
    return True


def prefix_label(sequence):
    """PREFIXの表示(「C-x-」「ESC-」「C-j C-o-」、C++のpendingPrefixLabel())。"""
    names = ['ESC' if b == ESC else 'C-' + chr(b + 0x60) if b < 0x20 else chr(b) for b in sequence]
    return ' '.join(names) + '-'


def key_label(sequence):
    """表示用(「^X^F」「^[<」「␣」)。KEYの一覧・PREFIXの表示に。"""
    out = []
    for b in sequence:
        if b == DIGIT:
            out.append('N')
        elif b == CTRL_SPACE:
            out.append('^␣')
        elif b == 0x20:
            out.append('␣')
        elif b < 0x20:
            out.append('^' + chr(b + 0x40))
        elif b == 0x7F:
            out.append('^?')
        else:
            out.append(chr(b))
    return ''.join(out)


def _help_width(s):
    """表示幅(全角は2、U+FEFF・U+2060は0、␣は1)。HELPの「キー：」の桁合わせ用。"""
    width = 0
    for c in s:
        if c in '\ufeff\u2060':
            continue
        width += 1 if ord(c) < 0x80 or c == '␣' else 2
    return width


def _is_control(b):
    return 0 <= b < 0x20 or b in (0x7F, CTRL_SPACE)


class Rule:
    def __init__(self, source, target, leader_eligible, prompt_eligible):
        self.source, self.target = source, target   # 利用者のキー列→既定のキー列(Noneなら捨てる=無効化)
        self.leader_eligible, self.prompt_eligible = leader_eligible, prompt_eligible


class KeyBindings:
    """KEY節の読み替え(C++のKeyBindings::configure()・feed())と、既定のキー列でのK-番号の照合(match())。"""

    def __init__(self):
        self.prefix_specs = list(DEFAULT_PREFIX_SPECS)   # 利用者の接頭キー(KEY節のF-n)
        self.overrides = {}   # K-番号 -> spec(''は割り当て無し)
        self.errors = []
        self.rebuild()

    def load_key_section(self, lines):
        """usilicrcのKEY節の行(「F-n キー」「K-n キー」、#以降は注釈)。"""
        self.user_prefixes = {}
        for line in lines:
            body = line.split('#', 1)[0].strip()
            if not body:
                continue
            tokens = body.split()
            head, spec = tokens[0], ' '.join(tokens[1:])
            if head.startswith('F-') and head[2:].isdigit():
                self.user_prefixes[int(head[2:])] = spec
            elif head.startswith('K-') and head[2:].isdigit():
                self.overrides[int(head[2:])] = spec
            else:
                self.errors.append('KEY: ' + body)
        self.rebuild()

    def rebuild(self):
        """C++のconfigure(): 既定のキー列(本体が照合する)と、場面ごとの読み替えの規則を組む。"""
        report = lambda message: self.errors.append('KEY: ' + message)  # noqa: E731
        user_prefixes = getattr(self, 'user_prefixes', {})
        # 既定の接頭キー(本体が使う)と利用者の接頭キー
        self.prefixes = []
        for spec in DEFAULT_PREFIX_SPECS:
            self.prefixes.append(parse_spec(spec, self.prefixes)[0] or [])
        user = []
        for n, spec in enumerate(DEFAULT_PREFIX_SPECS):
            user.append(list(self.prefixes[n]))
            given = user_prefixes.get(n)
            if given is None or not given.strip():
                continue
            seq, error = parse_spec(given, user)
            if seq is None:
                report('F-%d: %s' % (n, error))
            elif DIGIT in seq:
                report('F-%d: N is not allowed' % n)
            else:
                user[n] = seq
        for n in user_prefixes:
            if not 0 <= n < len(DEFAULT_PREFIX_SPECS):
                report('unknown F-%d' % n)
        self.user_prefix_sequences = user
        # 番号ごとの既定のキー列と利用者のキー列
        resolved = []   # [番号, 既定, 利用者(None=割り当て無し), 場面]
        self.sequences = {}
        for number, spec in DEFAULT_ENTRIES:
            default = parse_spec(spec, self.prefixes)[0]
            scope = SCOPES.get(number, GLOBAL)
            if scope == GLOBAL:
                self.sequences[number] = default
            given = self.overrides.get(number)
            if given is None:
                seq = parse_spec(spec, user)[0]
            elif not given.strip():
                seq = None
            else:
                seq, error = parse_spec(given, user)
                if seq is None:
                    report('K-%d: %s' % (number, error))
                    seq = parse_spec(spec, user)[0]
                elif (DIGIT in default) != (DIGIT in seq):
                    report('K-%d: N must be used exactly when the default uses it' % number)
                    seq = parse_spec(spec, user)[0]
                elif scope == MACRO_REPEAT and len(seq) != 1:
                    report('K-%d: must be one key' % number)
                    seq = parse_spec(spec, user)[0]
            resolved.append([number, default, seq, scope])
        known = {n for n, _ in DEFAULT_ENTRIES}
        for n in self.overrides:
            if n not in known:
                report('unknown K-%d' % n)

        def set_for(scope):
            out = []
            for i, r in enumerate(resolved):
                if r[3] == scope:
                    out.append(i)
                elif r[3] == GLOBAL and scope != GLOBAL and \
                        not any(o[3] == scope and o[1] == r[1] for o in resolved):
                    out.append(i)
            return sorted(out)
        for scope in (GLOBAL, COPY, PROMPT):  # 重なりは場面ごとに、後の方を割り当て無しに
            members = set_for(scope)
            for x, i in enumerate(members):
                ri = resolved[i]
                if ri[2] is None:
                    continue
                for j in members[:x]:
                    rj = resolved[j]
                    if rj[2] is None or (scope != GLOBAL and ri[3] == GLOBAL and rj[3] == GLOBAL):
                        continue
                    if rj[2] == ri[2] or _same_head(rj[2], ri[2]) or _same_head(ri[2], rj[2]):
                        report('K-%d conflicts with K-%d' % (ri[0], rj[0]))
                        ri[2] = None
                        break
        # HELPの書き換え(rewrite_help_text())は、全体の番号と、既定の表記が全体の番号に無い場面の番号(^[w等)だけ。
        self.changed = [(r[1], r[2]) for r in resolved
                        if r[2] is not None and r[2] != r[1] and r[3] != MACRO_REPEAT and
                        (r[3] == GLOBAL or not any(o[3] == GLOBAL and o[1] == r[1] for o in resolved))]
        self.macro_repeat_key = next((r[2][0] for r in resolved if r[3] == MACRO_REPEAT and r[2]), -1)
        leader_first = [ESC] + [user[n][0] for n in (0, 1, 2, 4) if user[n] and user[n][0] != 0x03]

        def leader_eligible(source, default):
            return len(source) >= 2 and default[0] in _LEADER_BYTES and source[0] in leader_first

        def prompt_eligible(source, default):
            if leader_eligible(source, default):
                return True
            from_control = (len(source) == 1 and _is_control(source[0])) or \
                (len(source) >= 2 and source[0] in leader_first)
            default_control = (len(default) == 1 and _is_control(default[0])) or \
                (len(default) >= 2 and default[0] in _LEADER_BYTES)
            return from_control and default_control

        def build(members):
            rules = []
            for i in members:
                number, default, seq, _ = resolved[i]
                if seq == default:
                    continue
                if seq is not None:
                    rules.append(Rule(seq, default, leader_eligible(seq, default), prompt_eligible(seq, default)))
                if not any(resolved[o][2] == default for o in members):
                    rules.append(Rule(default, None, default[0] in _LEADER_BYTES and len(default) >= 2,
                                      prompt_eligible(default, default)))
            return rules
        self.rules = {ALL: build(set_for(GLOBAL)), CONTEXT_COPY: build(set_for(COPY)),
                      CONTEXT_PROMPT: build(set_for(PROMPT))}

    def rewrite_help_text(self, text):
        """C++のrewriteHelpText(): KEY節で変えたキーを、HELPの表記(「キー：説明」のキー欄と、本文の「^」の表記)にも。"""
        if not self.changed:
            return text
        column_labels = [(key_label(a), key_label(b)) for a, b in self.changed]
        caret_labels = [(a, b) for a, b in column_labels if len(a) >= 2 and a[0] == '^']

        def rewrite_carets(s):
            out, i = [], 0
            while i < len(s):
                best = None
                if s[i] == '^':
                    for a, b in caret_labels:
                        if s.startswith(a, i) and (best is None or len(a) > len(best[0])):
                            best = (a, b)
                if best:
                    # 一致の後ろがさらに表記の続き(^x)なら、もっと長いキーの一部なので置き換えない。
                    end = i + len(best[0])
                    longer = end + 1 < len(s) and s[end] == '^' and s[end + 1] != ' '
                    if not longer or 'N' in best[0]:
                        out.append(best[1])
                        i = end
                        continue
                out.append(s[i])
                i += 1
            return ''.join(out)

        lines = []
        for line in text.split('\n'):
            # 「キー：説明」の行: 行頭の空白(U+FEFFの印を含む)、キー欄、「：」。
            key_start = len(line) - len(line.lstrip(' \ufeff'))
            colon = line.find('：', key_start)
            key = line[key_start:colon] if colon >= 0 else ''
            if key and ' ' not in key:
                replaced = next((b for a, b in column_labels if a == key), None) or rewrite_carets(key)
                lead = line[:key_start]
                if replaced != key:
                    # 「：」の桁を保つ: 幅の差だけ前の空白を詰める/足し、足りなければ字下げの印を減らす(1つは残す)。
                    diff = _help_width(replaced) - _help_width(key)
                    spaces, marks = lead.count(' '), lead.count('\ufeff')
                    if diff < 0:
                        spaces -= diff
                    else:
                        from_spaces = min(spaces, diff)
                        spaces -= from_spaces
                        marks = max(1 if marks else 0, marks - (diff - from_spaces))
                    lead = '\ufeff' * marks + ' ' * spaces
                line = lead + replaced + rewrite_carets(line[colon:])
            else:
                line = rewrite_carets(line)
            lines.append(line)
        return '\n'.join(lines)

    def is_identity(self):
        return not any(self.rules.values())

    def feed(self, byte, context, pending):
        """入力1バイトを読み替える(C++のfeed())。pendingは溜めている途中の[バイトの並び, 場面]。本体へ渡すバイトの
        並び(空なら溜めている途中か、無効化されたキー)。"""
        if self.is_identity():
            return [byte]
        if not pending[0]:
            if context == CONTEXT_NONE:
                return [byte]
            pending[1] = context
        context = pending[1]
        rules = self.rules[CONTEXT_COPY] if context == CONTEXT_COPY else \
            self.rules[CONTEXT_PROMPT] if context == CONTEXT_PROMPT else self.rules[ALL]
        bytes_ = pending[0]
        bytes_.append(byte)

        def eligible(rule):
            if context == CONTEXT_PROMPT:
                return rule.prompt_eligible
            return context != LEADER_ONLY or rule.leader_eligible

        def matches(pattern, data):
            return len(pattern) == len(data) and all(
                p == d or (p == DIGIT and 0x30 <= d <= 0x39) for p, d in zip(pattern, data))
        out = []
        while bytes_:
            if any(eligible(r) and len(bytes_) < len(r.source) and matches(r.source[:len(bytes_)], bytes_)
                   for r in rules):
                return out  # どれかの列の途中: 待つ
            best = None
            for r in rules:
                if eligible(r) and len(r.source) <= len(bytes_) and (best is None or len(r.source) > len(best.source)) \
                        and matches(r.source, bytes_[:len(r.source)]):
                    best = r
            if best is not None:
                if best.target is not None:
                    out.extend(bytes_[len(best.source) - 1] if k == DIGIT else k for k in best.target)
                del bytes_[:len(best.source)]
            else:
                out.append(bytes_.pop(0))
        return out

    def sequence_for(self, number):
        """K-番号の既定のキー列(本体が照合する)。"""
        return self.sequences.get(number)

    def match(self, pending):
        """打ったバイトの並び(読み替えた後の既定のキー列)を照合: ('done', K-番号)・('more', None)・('none', None)。

        Nの機能は、1桁目で決まり、続けて打つ数字はApplicationの方でためる(C++のgotoLineDigits_)。
        """
        partial = False
        for number, seq in self.sequences.items():
            if len(pending) == len(seq) and all(
                    s == p or (s == DIGIT and 0x30 <= p <= 0x39) for s, p in zip(seq, pending)):
                return 'done', number
            if len(pending) < len(seq) and all(
                    s == p or (s == DIGIT and 0x30 <= p <= 0x39) for s, p in zip(seq, pending)):
                partial = True
        return ('more', None) if partial else ('none', None)

    def is_prefix_head(self, pending):
        """pendingが接頭キー(F-n)そのものか(PREFIXの表示、「C-x-」等)。"""
        return any(pending == p for p in self.prefixes if p)
