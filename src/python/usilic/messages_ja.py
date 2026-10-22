"""mode-lineのメッセージの日本語訳(C++のmessages_ja.cpp、今回使う分だけ写したもの)。"""
import os

_EXACT = {
    'Exit? (y/n) ': '終了しますか？ (y/n) ',
    'Not available in pager': 'ページャーでは使えません',
    'Permission denied': '許可がありません',
    'No string': '見つかりません',
    'Not enough buffer': 'バッファが足りません',
    'No chapter': '章がありません',
    'Already in HELP': '既に HELP です',
    'Already in KEY': '既に KEY です',
    'Not found in HELP': 'HELP に見つかりません',
    'SHELL/prompt only': 'SHELL・プロンプトだけで使えます',
    'Reading...': '読み込み中...',
    'Disk usage...': '集計中...',
    'Loading archive...': 'アーカイブを読み込み中...',
    'Delete? (y/n) ': '削除しますか？ (y/n) ',
    'Destination? ': '転送先？',
    'No results': '見つかりません',
}

_TEMPLATES = [
    ('Cannot read: {}', '読めません: {}'),
    ('Cannot list: {}', '一覧できません: {}'),
    ('HELP not found: {} (make install)', 'HELP が見つかりません: {}（make install）'),
    ('{}: command not found', '{}: コマンドが見つかりません'),
    ('No manual entry for {}', '{} のマニュアルページはありません'),
    ("Can't open {}", '{} を開けません'),
    ('Cannot run: {}', '実行できません: {}'),
    ('No such EPUB: {}', 'EPUB がありません: {}'),
    ('Not a book: {}', '本ではありません: {}'),
]


def japanese():
    """LANG(またはLC_ALL・LC_MESSAGES)がjaか。C++版はHELPの言語(^Oj)で決めている所。"""
    for name in ('LC_ALL', 'LC_MESSAGES', 'LANG'):
        value = os.environ.get(name)
        if value:
            return value.startswith('ja')
    return False


def localized(text):
    """textの訳(日本語の時だけ)。まるごと一致する文、次に"{}"を含む型(localized()と同じ順)。"""
    if not japanese():
        return text
    if text in _EXACT:
        return _EXACT[text]
    for english, japanese_text in _TEMPLATES:
        head, tail = english.split('{}')
        if text.startswith(head) and text.endswith(tail) and len(text) >= len(head) + len(tail):
            return japanese_text.replace('{}', text[len(head):len(text) - len(tail)])
    return text
