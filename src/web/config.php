<?php
// 設置する場所に合わせて書き換える。どれも公開ディレクトリの外でよい。
// macOS(http://localhost/usilic-web/)では、このファイルの持ち主のホームの~/.local/usilic-webの下
// (usilicが本棚から落とした本を置く所と同じ、README参照)。

if (PHP_OS_FAMILY === 'Darwin') {
    $owner = function_exists('posix_getpwuid') ? posix_getpwuid(fileowner(__FILE__)) : false;
    $base = ($owner ? $owner['dir'] : getenv('HOME')) . '/.local/usilic-web';
    $TEXT_DIR = $base . '/texts';
    $BOOK_DIR = $base . '/books';
    $MARK_FILE = $base . '/marks/marks.txt';
    $EPUB_MARK_FILE = $base . '/marks/epub-marks.txt';
    return;
}

// Windows(PHP付属のサーバーで http://localhost/usilic-web/、README参照)では、%USERPROFILE%\.local\usilic-webの下
// (Windows版のusilicが本棚から落とした本を置く所と同じ)。make installが無いので、無ければここで作る。
if (PHP_OS_FAMILY === 'Windows') {
    $home = getenv('USERPROFILE') ?: (getenv('HOMEDRIVE') . getenv('HOMEPATH'));
    $base = str_replace('\\', '/', $home) . '/.local/usilic-web';
    foreach (['texts', 'books', 'marks'] as $d) {
        if (!is_dir("$base/$d")) {
            @mkdir("$base/$d", 0775, true);
        }
    }
    $TEXT_DIR = $base . '/texts';
    $BOOK_DIR = $base . '/books';
    $MARK_FILE = $base . '/marks/marks.txt';
    $EPUB_MARK_FILE = $base . '/marks/epub-marks.txt';
    return;
}

// 原稿(UTF-8の*.txt)と*.epubの置き場所。scpで置くか、一覧ページへドラッグする。
// ドラッグで置くには、apache(www-data)が書き込めること。
$TEXT_DIR = '/srv/usilic-web/texts';

// EPUBを展開しておく場所。apache(www-data)が書き込めること。
$BOOK_DIR = '/srv/usilic-web/books';

// .txtの校閲箇所の記録ファイル。apache(www-data)が書き込めること。
// 1行1件、「原稿.txt:行:桁: 文字列」(Emacsのcompilation-modeで飛べる形)。
$MARK_FILE = '/srv/usilic-web/marks/marks.txt';

// .epubの記録ファイル。1行1件、「本.epub:章のパス:位置: 文字列」
// (位置は章の本文の先頭からの文字数、1始まり)。
$EPUB_MARK_FILE = '/srv/usilic-web/marks/epub-marks.txt';
