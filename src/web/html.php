<?php
// 既定の本棚の手順書(BUILD.html等、common.phpのbundled_docs())をそのまま見せる: html.php?f=名前
// 2026-11-07(bundled_docs()参照)。手順書はスクリプトを使わないので止め、外のものも読ませない(file.phpと同じ考え)。
require __DIR__ . '/common.php';

$name = $_GET['f'] ?? '';
$path = valid_name($name, 'html') ? doc_path($name, 'html') : false;
if ($path === false) {
    http_response_code(404);
    exit;
}
header('Content-Type: text/html; charset=utf-8');
header("Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; img-src 'self' data:; base-uri 'none'; form-action 'none'");
header('X-Content-Type-Options: nosniff');
header('Content-Length: ' . filesize($path));
header('Cache-Control: no-cache');
readfile($path);
