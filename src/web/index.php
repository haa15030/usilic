<?php
require __DIR__ . '/common.php';

$files = glob($TEXT_DIR . '/*.txt') ?: [];
usort($files, function ($a, $b) { return filemtime($b) - filemtime($a); });
?>
<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>原稿一覧</title>
<link rel="stylesheet" href="reader.css">
</head>
<body class="list">
<h1>原稿一覧</h1>
<?php if (!$files): ?>
<p>原稿がありません(<?= h($TEXT_DIR) ?>)。</p>
<?php else: ?>
<ul>
<?php foreach ($files as $path): $name = basename($path); ?>
<li><a href="read.php?f=<?= rawurlencode($name) ?>"><?= h($name) ?></a>
<span class="date"><?= date('Y-m-d H:i', filemtime($path)) ?></span></li>
<?php endforeach; ?>
</ul>
<?php endif; ?>
</body>
</html>
