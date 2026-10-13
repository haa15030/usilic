<?php
require __DIR__ . '/common.php';

header('Content-Type: application/json; charset=utf-8');

function fail($msg)
{
    http_response_code(400);
    exit(json_encode(['ok' => false, 'error' => $msg], JSON_UNESCAPED_UNICODE));
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    fail('POSTのみ');
}
$name = $_POST['f'] ?? '';
if (text_path($name) === false) {
    fail('原稿がありません');
}
$line = filter_var($_POST['line'] ?? '', FILTER_VALIDATE_INT, ['options' => ['min_range' => 1]]);
$col = filter_var($_POST['col'] ?? '', FILTER_VALIDATE_INT, ['options' => ['min_range' => 1]]);
if ($line === false || $col === false) {
    fail('行・桁が不正');
}
// 1件を必ず1行に収める(改行・制御文字は空白へ)。
$text = (string)($_POST['text'] ?? '');
$text = preg_replace('/[\x00-\x1F\x7F]+/u', ' ', $text) ?? '';
// mbstringはphpパッケージに含まれないので正規表現で500文字に切る。
$text = preg_replace('/^(.{500}).+$/us', '$1', trim($text)) ?? '';
if ($text === '') {
    $text = '(位置のみ)';
}

$entry = "$name:$line:$col: $text\n";
if (file_put_contents($MARK_FILE, $entry, FILE_APPEND | LOCK_EX) === false) {
    http_response_code(500);
    exit(json_encode(['ok' => false, 'error' => '記録ファイルに書けません'], JSON_UNESCAPED_UNICODE));
}
echo json_encode(['ok' => true, 'line' => $line, 'col' => $col], JSON_UNESCAPED_UNICODE);
