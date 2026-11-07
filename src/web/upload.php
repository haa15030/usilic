<?php
// 一覧ページから送られた.txt/.epubを原稿置き場へ保存する(同じ名前は上書き)。
// .epubは$BOOK_DIRへ展開し直す。
require __DIR__ . '/common.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_out(['ok' => false, 'error' => 'POSTのみ'], 405);
}
require_same_origin();
$f = $_FILES['file'] ?? null;
if (!$f || ($f['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK) {
    $err = $f['error'] ?? UPLOAD_ERR_NO_FILE;
    $msg = ($err === UPLOAD_ERR_INI_SIZE || $err === UPLOAD_ERR_FORM_SIZE)
        ? 'ファイルが大きすぎます(upload_max_filesize)' : '受け取れませんでした(' . $err . ')';
    json_out(['ok' => false, 'error' => $msg], 400);
}
$name = $f['name'];
if (!valid_name($name)) {
    json_out(['ok' => false, 'error' => '.txtか.epubだけです'], 400);
}
$isEpub = valid_name($name, 'epub');
if ($isEpub && !class_exists('ZipArchive')) {
    json_out(['ok' => false, 'error' => 'php-zipが入っていません'], 500);
}

// 一時ファイルに置いてから入れ替える(書き込み途中の原稿を読まれないように)
$dest = $TEXT_DIR . '/' . $name;
$tmp = $TEXT_DIR . '/.upload-' . bin2hex(random_bytes(4));
if (!move_uploaded_file($f['tmp_name'], $tmp)) {
    json_out(['ok' => false, 'error' => '原稿置き場に書けません'], 500);
}
chmod($tmp, 0664);

if ($isEpub) {
    $err = extract_epub($tmp, $BOOK_DIR . '/' . $name);
    if ($err !== null) {
        unlink($tmp);
        json_out(['ok' => false, 'error' => $err], 400);
    }
}
if (!rename($tmp, $dest)) {
    unlink($tmp);
    json_out(['ok' => false, 'error' => '原稿置き場に書けません'], 500);
}
json_out(['ok' => true, 'name' => $name]);
