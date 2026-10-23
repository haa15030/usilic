"""UTF-8のバイト長・表示幅(C++のutf8.hpp/utf8.cpp)。

バイト数と表示桁数を取り違えないよう、別々の関数にしてある(C++版と同じ理由)。Pythonの文字列は
コードポイントの列なので、表示幅はコードポイントから求める(display_width_of_codepoint)。
"""


def sequence_length(c):
    """先頭バイトcで始まるUTF-8の列のバイト長(1〜4)。不正なバイトは1(Utf8::sequence_length)。"""
    if c < 0x80:
        return 1
    if (c & 0xE0) == 0xC0:
        return 2
    if (c & 0xF0) == 0xE0:
        return 3
    if (c & 0xF8) == 0xF0:
        return 4
    return 1


def display_width_of_codepoint(cpt):
    """コードポイントの表示幅(0・1・2桁)。範囲はUtf8::display_width()の表と同じ。"""
    if cpt < 0x80:
        return 1
    if cpt in (0x2060, 0xFEFF):
        return 0
    if (0x0300 <= cpt <= 0x036F or cpt in (0x3099, 0x309A) or 0x1AB0 <= cpt <= 0x1AFF
            or 0x1DC0 <= cpt <= 0x1DFF or 0x20D0 <= cpt <= 0x20FF or 0xFE20 <= cpt <= 0xFE2F):
        return 0
    if (0x1100 <= cpt <= 0x115F or (0x2E80 <= cpt <= 0xA4CF and cpt != 0x303F)
            or 0xAC00 <= cpt <= 0xD7A3 or 0xF900 <= cpt <= 0xFAFF
            or 0xFE10 <= cpt <= 0xFE19  # 縦書き用の句読点
            or 0xFE30 <= cpt <= 0xFE4F or 0xFF00 <= cpt <= 0xFF60 or 0xFFE0 <= cpt <= 0xFFE6
            or 0x20000 <= cpt <= 0x3FFFD or 0x1F300 <= cpt <= 0x1FAFF or 0x2600 <= cpt <= 0x27BF):
        return 2
    if 0x2018 <= cpt <= 0x201F or 0x25A0 <= cpt <= 0x25FF:  # ‘’“” と ■□▲△●○★☆等
        return 2
    return 1


def display_width(data):
    """UTF-8のバイト列1字分(Utf8::display_width(cp, len)と同じ引数の形)の表示幅。"""
    if len(data) <= 1:
        return 1
    return display_width_of_codepoint(ord(data.decode('utf-8', 'replace')[0]))


_CHAR_WIDTHS = {}  # 字→表示幅(char_width()が覚える)


def char_width(ch):
    """1字の表示幅(display_width_of_codepoint()を字ごとに覚えたもの、長いfileを組む時に速く)。"""
    w = _CHAR_WIDTHS.get(ch)
    if w is None:
        w = _CHAR_WIDTHS[ch] = display_width_of_codepoint(ord(ch))
    return w


def text_width(text):
    """文字列全体の表示幅(桁)。C++版では呼び出し側がsequence_length/display_widthで数えている所。"""
    if text.isascii():
        return len(text)
    return sum(char_width(ch) for ch in text)
