"""端末からの生のバイトをキーにする(C++のkey_input.cppのKeyInput)。

C++版と同じく、普通のキーはバイト(int)のまま返し、矢印・PageUp/PageDownだけを特別なキー(文字列)にする。
ESC単独(^[の後のキー)と矢印のエスケープ列は、続きのバイトがすぐに来るかで見分ける。
"""
import os
import select

UP, DOWN, RIGHT, LEFT, PAGE_UP, PAGE_DOWN = 'Up', 'Down', 'Right', 'Left', 'PageUp', 'PageDown'

_SEQUENCES = {
    b'[A': UP, b'[B': DOWN, b'[C': RIGHT, b'[D': LEFT, b'OA': UP, b'OB': DOWN, b'OC': RIGHT, b'OD': LEFT,
    b'[5~': PAGE_UP, b'[6~': PAGE_DOWN,
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
        for seq, name in _SEQUENCES.items():
            if self.pending[:len(seq)] == seq:
                del self.pending[:len(seq)]
                return 0, name
        return 0x1B, None
