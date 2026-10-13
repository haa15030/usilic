<?php
// 一覧ページから送られた.txt/.epubを原稿置き場へ保存する(同じ名前は上書き)。
// .epubは$BOOK_DIRへ展開し直す。
require __DIR__ . '/common.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_out(['ok' => false, 'error' => 'POSTのみ'], 405);
}
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

function remove_tree($dir)
{
    if (!is_dir($dir) || is_link($dir)) {
        return;
    }
    foreach (scandir($dir) as $e) {
        if ($e === '.' || $e === '..') {
            continue;
        }
        $p = $dir . '/' . $e;
        if (is_dir($p) && !is_link($p)) {
            remove_tree($p);
        } else {
            unlink($p);
        }
    }
    rmdir($dir);
}

// EPUBを$dirへ展開する。失敗したら理由を返す。
// 別の場所へ展開してから入れ替え、読んでいる途中の本が壊れないようにする。
function extract_epub($zipPath, $dir)
{
    $zip = new ZipArchive();
    if ($zip->open($zipPath) !== true) {
        return 'EPUBを開けません';
    }
    // ..や絶対パスを含む項目は捨てる(展開先の外へ書かせない)
    $names = [];
    for ($i = 0; $i < $zip->numFiles; $i++) {
        $n = $zip->getNameIndex($i);
        if ($n === false || $n === '' || $n[0] === '/' || strpos($n, '\\') !== false
            || preg_match('#(^|/)\.\.(/|$)#', $n)) {
            continue;
        }
        $names[] = $n;
    }
    if (!in_array('META-INF/container.xml', $names, true)) {
        $zip->close();
        return 'EPUBではありません(META-INF/container.xmlが無い)';
    }
    $new = $dir . '.new-' . bin2hex(random_bytes(4));
    if (!mkdir($new, 0775, true) || !$zip->extractTo($new, $names)) {
        $zip->close();
        remove_tree($new);
        return 'EPUBを展開できません(展開先に書けない?)';
    }
    $zip->close();
    $old = $dir . '.old-' . bin2hex(random_bytes(4));
    if (is_dir($dir)) {
        rename($dir, $old);
    }
    rename($new, $dir);
    remove_tree($old);
    return null;
}
