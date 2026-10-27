# -c(端末版)を疑似端末(pty)の中で動かし、キーを送って画面を文字で取り出す(2026-10-27、直接のユーザー依頼
# "入れておいて"、docs/CHANGES.md項目1104。それまではアシスタントのセッションの一時置き場にあった)。
#
# 使い方: python3 tools/ptyrun.py 設定.json
#   {"home": "/tmp/h", "argv": ["build/usilic", "-c", "file"], "script": ["\u001b>", 0.5, "SNAP1", "j", "SNAP2"]}
#   home   HOMEにするディレクトリ(本物の~を汚さないよう)
#   script 文字列はキーとして送り、数はその秒数だけ出力を待ち、"SNAP…"はその時点の画面を出す
# 窓は50×120(ROWS/COLS)。窓の大きさを設定しないとusilicが0×0とみなすので必ず設定する(CLAUDE.mdの2026-07-16の
# 訂正参照)。端末の制御コードはカーソル移動・行の消去・画面の消去だけを解釈する簡単なもの(色等は捨てる)。
import os, pty, sys, time, select, struct, fcntl, termios, re, unicodedata, json
ROWS, COLS = 50, 120
def run(argv, env, script):
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(argv[0], argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLS, 0, 0))
    out = bytearray()
    def pump(t):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.02)
            if r:
                try: out.extend(os.read(fd, 65536))
                except OSError: return
    snaps = []
    for item in script:
        if isinstance(item, (int, float)): pump(item)
        elif item.startswith('SNAP'): pump(0.05); snaps.append((item, len(out)))
        else: os.write(fd, item.encode('latin1') if item.isascii() else item.encode()); pump(0.05)
    os.kill(pid, 9)
    return bytes(out), snaps
def width(ch):
    return 2 if unicodedata.east_asian_width(ch) in 'WF' else (0 if unicodedata.combining(ch) else 1)
def emulate(data):
    scr = [[' '] * COLS for _ in range(ROWS)]
    r = c = 0
    s = data.decode('utf-8', 'replace')
    i = 0
    while i < len(s):
        ch = s[i]
        if ch == '\x1b':
            m = re.match(r'\x1b\[([?0-9;]*)([A-Za-z@])', s[i:])
            if m:
                p, f = m.group(1), m.group(2)
                nums = [int(x) if x.isdigit() else 0 for x in p.lstrip('?').split(';')] if p else []
                if f in 'Hf':
                    r = (nums[0] if nums and nums[0] else 1) - 1; c = (nums[1] if len(nums) > 1 and nums[1] else 1) - 1
                elif f == 'K':
                    for x in range(c, COLS): scr[r][x] = ' '
                elif f == 'J' and nums and nums[0] == 2:
                    scr = [[' '] * COLS for _ in range(ROWS)]
                i += len(m.group(0)); continue
            m = re.match(r'\x1b[()][0-9A-Za-z]', s[i:])
            i += len(m.group(0)) if m else 2; continue
        if ch == '\r': c = 0
        elif ch == '\n': r = min(r + 1, ROWS - 1)
        elif ch == '\b': c = max(c - 1, 0)
        elif ord(ch) >= 32:
            w = width(ch)
            if w and 0 <= r < ROWS and c < COLS:
                scr[r][c] = ch
                if w == 2 and c + 1 < COLS: scr[r][c + 1] = ''
                c += w
        i += 1
    return '\n'.join(''.join(x).rstrip() for x in scr)
if __name__ == '__main__':
    cfg = json.load(open(sys.argv[1]))
    env = dict(os.environ, HOME=cfg['home'], TERM='xterm-256color', LANG='ja_JP.UTF-8')
    env.pop('USILIC', None)
    script = [x for x in cfg['script']]
    out, snaps = run(cfg['argv'], env, script)
    for name, n in snaps:
        print('=====', name); print(emulate(out[:n]))
