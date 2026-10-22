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
    'Key: ': 'キー: ',
    'Set: ': '置く: ',
    'Jump: ': '移動: ',
    'Extension: ': '拡張子: ',
    'Regexp: ': '正規表現: ',
    'Mark set': 'マークを置きました',
    'No mark set': 'マークがありません',
    'No bookmarks': 'ブックマークがありません',
    "Can't set bookmark here": 'ここにはブックマークを置けません',
    "Can't set register here": 'ここではレジスタに記録できません',
    'Defining kbd macro...': 'キーボードマクロを記録しています...',
    'Keyboard macro defined': 'キーボードマクロを記録しました',
    'Already defining kbd macro': '既にキーボードマクロを記録しています',
    'Not defining kbd macro': 'キーボードマクロを記録していません',
    'No command to repeat': '繰り返すコマンドがありません',
    'Type e to repeat macro': 'e でマクロを繰り返します',
    'No marks': '記録がありません',
    'No other windows': 'ほかのウィンドウがありません',
    'Scroll: chapter': 'スクロール: 章ごと',
    'Scroll: continuous': 'スクロール: 連続',
    'Open only': 'Open だけです',
    'COPY mode only': 'COPY モードだけで使えます',
    'Plain text only': '平文だけで使えます',
    '^G to return from note': '^G で注釈から戻ります',
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
    ('Register {} set', 'レジスタ {} に記録しました'),
    ('Register {} is empty', 'レジスタ {} は空です'),
    ('{} files marked', '{} 個のファイルを選択しました'),
    ('{} file marked', '{} 個のファイルを選択しました'),
    ('Copied: {}', 'コピーしました: {}'),
    ('Saved: {}', '保存しました: {}'),
    ('Cannot write: {}', '書き込めません: {}'),
    ('Cannot get: {}', '取得できません: {}'),
    ('Not a Narou work: {}', 'なろうの作品ではありません: {}'),
    ('Not a book: {}', '本ではありません: {}'),
]


_override = None  # ^Ojで切り換えた後(Noneは環境変数のとおり)


def set_japanese(value):
    """^Oj: HELPの言語と一緒にメッセージの言語も切り換える(C++のsetMessagesJapanese())。"""
    global _override
    _override = value


def japanese():
    """LANG(またはLC_ALL・LC_MESSAGES)がjaか。C++版はHELPの言語(^Oj)で決めている所。"""
    if _override is not None:
        return _override
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
