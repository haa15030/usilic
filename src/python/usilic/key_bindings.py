"""キーの割り当て(C++のkey_bindings.cppのKeyBindings)。

機能はK-番号(HELPの記載順)で表し、キーは「C-x f」のような書き方(usilicrcのKEY節と同じ)。接頭キーF-0〜F-4
(既定は^X・^J・^O・^C・^J^O)を書き換えると、それを使う機能のキーもまとめて変わる。
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
]

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


class KeyBindings:
    def __init__(self):
        self.prefix_specs = list(DEFAULT_PREFIX_SPECS)
        self.overrides = {}   # K-番号 -> spec(''は割り当て無し)
        self.errors = []
        self.rebuild()

    def rebuild(self):
        """接頭キーと各機能のキー列を組み直す(後から来た重複は割り当て無し、C++と同じ)。"""
        self.prefixes = []
        for spec in self.prefix_specs:
            seq, _ = parse_spec(spec, self.prefixes)  # F-4はF-1を使える(前の接頭キーまで)
            self.prefixes.append(seq or [])
        self.sequences = {}   # K-番号 -> [バイト]
        for number, spec in DEFAULT_ENTRIES:
            spec = self.overrides.get(number, spec)
            if not spec:
                continue
            seq, error = parse_spec(spec, self.prefixes)
            if seq is None:
                self.errors.append('K-%d: %s' % (number, error))
                continue
            clash = next((n for n, other in self.sequences.items()
                          if other == seq or _same_head(other, seq) or _same_head(seq, other)), None)
            if clash is not None:
                self.errors.append('K-%d: conflicts with K-%d: %s' % (number, clash, spec))
                continue
            self.sequences[number] = seq

    def load_key_section(self, lines):
        """usilicrcのKEY節の行(「F-n キー」「K-n キー」、#以降は注釈)。"""
        for line in lines:
            body = line.split('#', 1)[0].strip()
            if not body:
                continue
            tokens = body.split()
            head, spec = tokens[0], ' '.join(tokens[1:])
            if head.startswith('F-') and head[2:].isdigit() and int(head[2:]) < len(self.prefix_specs):
                self.prefix_specs[int(head[2:])] = spec
            elif head.startswith('K-') and head[2:].isdigit():
                self.overrides[int(head[2:])] = spec
            else:
                self.errors.append('KEY: ' + body)
        self.rebuild()

    def sequence_for(self, number):
        return self.sequences.get(number)

    def match(self, pending):
        """打ったバイトの並びを照合: ('done', K-番号, 数字の並び)・('more',)・('none',)。

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
