"""端末からの生のバイトをキーにする(C++のkey_input.cppのKeyInput)。

C++版と同じく、普通のキーはバイト(int)のまま返し、矢印・PageUp/PageDownだけを特別なキー(文字列)にする。
ESC単独(^[の後のキー)と矢印のエスケープ列は、続きのバイトがすぐに来るかで見分ける。マウスの報告(ESC [ < … M/m)は
mouse.MouseEvent、括弧付き貼り付け(ESC [200~ … ESC [201~、ファイルのドロップ)はmouse.Pasteを特別なキーとして返す。
"""
import os
import select

from .mouse import Paste, parse_sgr

UP, DOWN, RIGHT, LEFT, PAGE_UP, PAGE_DOWN = 'Up', 'Down', 'Right', 'Left', 'PageUp', 'PageDown'

_SEQUENCES = {
    b'[A': UP, b'[B': DOWN, b'[C': RIGHT, b'[D': LEFT, b'OA': UP, b'OB': DOWN, b'OC': RIGHT, b'OD': LEFT,
    b'[5~': PAGE_UP, b'[6~': PAGE_DOWN,
    # ^[+矢印を素早く押した時もShift+矢印として届く端末がある(C++のreadEscapeSequence())
    b'[1;2A': 'ShiftUp', b'[1;2B': 'ShiftDown', b'[1;2C': 'ShiftRight', b'[1;2D': 'ShiftLeft',
}


class KeyInput:
    def __init__(self, fd):
        self.fd = fd
        self.pending = bytearray()

    def _fill(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        data = os.read(self.fd, 1024)
        if not data:
            return False
        self.pending.extend(data)
        return True

    def read_key(self, timeout=None):
        """(byte, special)を返す。specialは矢印等ならUP等、それ以外はNone。timeoutまでに無ければNone。"""
        if not self.pending and not self._fill(timeout):
            return None
        b = self.pending.pop(0)
        if b != 0x1B:
            return b, None
        if not self.pending:
            self._fill(0.03)  # エスケープ列の続き(端末はまとめて送る)
        if self.pending[:2] == b'[<':  # マウスの報告
            while not any(c in self.pending for c in b'Mm') and len(self.pending) < 32 and self._fill(0.05):
                pass
            end = min((self.pending.find(c) for c in b'Mm' if c in self.pending), default=-1)
            event = parse_sgr(self.pending[2:end + 1].decode('ascii', 'replace')) if end > 0 else None
            if event is not None:
                del self.pending[:end + 1]
                return 0, event
        if self.pending[:4] == b'[200':  # 括弧付き貼り付け
            while b'\x1b[201~' not in self.pending and self._fill(0.2):
                pass
            end = self.pending.find(b'\x1b[201~')
            if end >= 0:
                text = bytes(self.pending[5:end]).decode('utf-8', 'replace')
                del self.pending[:end + 6]
                return 0, Paste(text)
        for seq, name in _SEQUENCES.items():
            if self.pending[:len(seq)] == seq:
                del self.pending[:len(seq)]
                return 0, name
        return 0x1B, None
