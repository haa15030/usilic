<?php
// 設置する場所に合わせて書き換える。どちらも公開ディレクトリの外でよい。

// 原稿(UTF-8の*.txt)の置き場所。Macからscp/rsyncで置く。
$TEXT_DIR = '/srv/usilic-web/texts';

// 校閲箇所の記録ファイル。apache(www-data)が書き込めること。
// 1行1件、「原稿.txt:行:桁: 文字列」(Emacsのcompilation-modeで飛べる形)。
$MARK_FILE = '/srv/usilic-web/marks/marks.txt';
