<?php
// 原稿置き場の.txt/.epubをそのまま返す: get.php?f=名前(usilicが本棚から落として開く用)
require __DIR__ . '/common.php';

$name = $_GET['f'] ?? '';
$path = doc_path($name);
if ($path === false) {
    http_response_code(404);
    exit;
}
header('Content-Type: ' . (valid_name($name, 'epub') ? 'application/epub+zip'
    : (valid_name($name, 'html') ? 'text/html; charset=utf-8' : 'text/plain; charset=utf-8')));
header('Content-Length: ' . filesize($path));
header('Content-Disposition: attachment; filename*=UTF-8\'\'' . rawurlencode($name));
header('Cache-Control: no-cache');
readfile($path);
