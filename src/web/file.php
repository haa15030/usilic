<?php
// 展開したEPUBの中のファイルを返す: file.php/本.epub/中のパス
// PATH_INFOにしているのは、章のXHTMLの中の相対パス(CSS・画像)をブラウザがそのまま解決できるようにするため。
require __DIR__ . '/common.php';

$TYPES = [
    'xhtml' => 'application/xhtml+xml', 'html' => 'text/html', 'htm' => 'text/html',
    'css' => 'text/css', 'xml' => 'application/xml', 'opf' => 'application/xml',
    'ncx' => 'application/xml', 'svg' => 'image/svg+xml',
    'jpg' => 'image/jpeg', 'jpeg' => 'image/jpeg', 'png' => 'image/png', 'gif' => 'image/gif',
    'webp' => 'image/webp', 'otf' => 'font/otf', 'ttf' => 'font/ttf', 'woff' => 'font/woff',
    'woff2' => 'font/woff2', 'js' => 'text/javascript',
];

$parts = explode('/', ltrim($_SERVER['PATH_INFO'] ?? '', '/'), 2);
$name = $parts[0];
$inner = $parts[1] ?? '';
if ($inner === '' || book_dir($name) === false) {
    http_response_code(404);
    exit;
}
foreach (explode('/', $inner) as $seg) {
    if ($seg === '' || $seg === '.' || $seg === '..') {
        http_response_code(404);
        exit;
    }
}
$root = realpath(book_dir($name));
$path = $root === false ? false : realpath($root . '/' . $inner);
// Windowsのrealpath()は区切りが\なので、/にそろえてから比べる
$under = function ($p, $r) {
    return strpos(str_replace('\\', '/', $p), str_replace('\\', '/', $r) . '/') === 0;
};
if ($root === false || $path === false || !$under($path, $root) || !is_file($path)) {
    http_response_code(404);
    exit;
}

$ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
header('Content-Type: ' . ($TYPES[$ext] ?? 'application/octet-stream'));
header('Content-Length: ' . filesize($path));
header('Cache-Control: no-cache');
readfile($path);
