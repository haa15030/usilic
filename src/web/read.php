<?php
require __DIR__ . '/common.php';

$name = $_GET['f'] ?? '';
$path = text_path($name);
if ($path === false) {
    http_response_code(404);
    exit('原稿がありません。');
}

// 行番号をEmacsと揃えるため、改行の種類によらず1行ずつ分ける。
// 末尾の改行の後ろは行として数えない。
$text = file_get_contents($path);
$text = preg_replace('/^\xEF\xBB\xBF/', '', $text);
$lines = preg_split('/\r\n|\r|\n/', $text);
if (count($lines) > 1 && end($lines) === '') {
    array_pop($lines);
}
?>
<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title><?= h($name) ?></title>
<link rel="stylesheet" href="reader.css">
</head>
<body class="reader">
<div id="text" data-file="<?= h($name) ?>">
<?php foreach ($lines as $i => $line): ?>
<p data-line="<?= $i + 1 ?>"><?= h($line) ?></p>
<?php endforeach; ?>
</div>
<div id="bar">
<a href="index.php">一覧</a>
<button id="mark">記録</button>
<button id="friction"></button>
<button id="smaller">A−</button>
<button id="larger">A＋</button>
<span id="where"></span>
</div>
<div id="toast"></div>
<script src="reader.js"></script>
</body>
</html>
