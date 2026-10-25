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
    '-g not available over ssh': 'ssh 越しでは -g は使えません',
    'Add it to PROGRAM in usilicrc (^Js restarts SHELL)': 'usilicrc の PROGRAM に加えてください（^Js で SHELL を再起動）',
    'Already in GUI': '既に GUI です',
    'Cannot open a terminal window': '端末のウィンドウを開けません',
    'Cannot use this font': 'このフォントは使えません',
    'CP/MV/RM: not supported with cmd.exe': 'CP/MV/RM: cmd.exe では使えません',
    'Ends with the parent': '親と一緒に終了します',
    'Font list: -g only': 'フォントの一覧: -g だけで使えます',
    'LETTER-SPACING: -g only': 'LETTER-SPACING: -g だけで使えます',
    'LINE-HEIGHT: -g only': 'LINE-HEIGHT: -g だけで使えます',
    'No Epub in URL (usilicrc)': 'usilicrc の URL に Epub がありません',
    'No fonts (fc-list)': 'フォントが見つかりません（fc-list）',
    'No GUI available': 'GUI が使えません',
    'No Open in URL (usilicrc)': 'usilicrc の URL に Open がありません',
    'Opened in a new window': '新しいウィンドウで開きました',
    'Parent not reachable': '親に届きません',
    'Wheel: normal': 'ホイール: 順',
    'Wheel: reversed': 'ホイール: 逆',
    'Window not reachable': 'ウィンドウに届きません',
    'Serif': '明朝体',
    'Sans-serif': 'ゴシック体',
    'Serif (-g only)': '明朝体（-g だけ）',
    'Sans-serif (-g only)': 'ゴシック体（-g だけ）',
    'Jump to register: ': 'レジスタの位置へ: ',
    'Point to register: ': 'レジスタへ記録: ',
    'Downloading...': 'ダウンロード中...',
    'Making PDF...': 'PDF を作成中...',
    'Record?(y/n)': '記録しますか？ (y/n)',
    'No String': '見つかりません',
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
    ('No such file or directory: {}', 'ファイルまたはディレクトリがありません: {}'),
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
    ('LINE-HEIGHT (vertical): {} dots ({})', 'LINE-HEIGHT（縦書き）: {} ドット（{}）'),
    ('LINE-HEIGHT: {} dots ({})', 'LINE-HEIGHT: {} ドット（{}）'),
    ('LETTER-SPACING (vertical): {} dots ({} dots per column)', 'LETTER-SPACING（縦書き）: {} ドット（1 桁 {} ドット）'),
    ('LETTER-SPACING: {} dots ({} dots per column)', 'LETTER-SPACING: {} ドット（1 桁 {} ドット）'),
    ('Cannot write {} to usilicrc', 'usilicrc に {} を書き込めません'),
    ('Bookmark "{}" set', 'ブックマーク「{}」を置きました'),
    ('Bookmark "{}" deleted', 'ブックマーク「{}」を削除しました'),
    ('Cannot write {}', '{} に書き込めません'),
    ('No books: {}', '本がありません: {}'),
    ('Recorded: {}', '記録しました: {}'),
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


def _match(text, english):
    """textが英語の型("{}"がいくつでも)に合えば、各"{}"に当たる部分のリスト。合わなければNone(C++のmatchPattern()と同じ)。"""
    parts = english.split('{}')
    if not text.startswith(parts[0]):
        return None
    pos = len(parts[0])
    values = []
    for i in range(1, len(parts)):
        literal = parts[i]
        if i == len(parts) - 1:  # 最後の固定部分は末尾に
            if len(text) < pos + len(literal) or not text.endswith(literal):
                return None
            at = len(text) - len(literal)
        else:
            at = text.find(literal, pos)
            if at < 0:
                return None
        if at == pos:  # "{}"は空でない
            return None
        values.append(text[pos:at])
        pos = at + len(literal)
    return values if pos == len(text) else None


def localized(text):
    """textの訳(日本語の時だけ)。まるごと一致する文、次に"{}"を含む型(localized()と同じ順)。"""
    if not japanese():
        return text
    if text in _EXACT:
        return _EXACT[text]
    for english, japanese_text in _TEMPLATES:
        values = _match(text, english)
        if values is None:
            continue
        parts = japanese_text.split('{}')
        out = parts[0]
        for i in range(1, len(parts)):
            out += (values[i - 1] if i - 1 < len(values) else '') + parts[i]
        return out
    return text
