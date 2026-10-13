<?php
require __DIR__ . '/common.php';

$name = $_GET['f'] ?? '';
if (doc_path($name, 'epub') === false || !is_dir($BOOK_DIR . '/' . $name)) {
    http_response_code(404);
    exit('本がありません。');
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
<div id="book" class="scroller" data-file="<?= h($name) ?>"
     data-base="file.php/<?= h(rawurlencode($name)) ?>/"></div>
<?php reader_bar(); ?>
<script src="reader.js"></script>
<script src="epub.js"></script>
</body>
</html>
