<?php
require __DIR__ . '/config.php';

// 原稿の名前として受け付けるか。置き場の外を指せないよう、ディレクトリを含む名前・
// 隠しファイルは受け付けない。$extは'txt'か'epub'、nullならどちらでも。
function valid_name($name, $ext = null)
{
    if (!is_string($name) || $name === '' || $name !== basename($name)
        || $name[0] === '.' || strpos($name, ':') !== false
        || !preg_match('/\.(txt|epub)$/', $name, $m)) {
        return false;
    }
    return $ext === null || $m[1] === $ext;
}

// 原稿置き場の中の実際のパス。無ければfalse。
function doc_path($name, $ext = null)
{
    global $TEXT_DIR;
    if (!valid_name($name, $ext)) {
        return false;
    }
    $path = $TEXT_DIR . '/' . $name;
    return is_file($path) ? $path : false;
}

function text_path($name)
{
    return doc_path($name, 'txt');
}

// 静的なファイルのURLに更新時刻を付ける(置き換えた後もブラウザが古いのを使い続けないように)
function asset($file)
{
    return $file . '?v=' . @filemtime(__DIR__ . '/' . $file);
}

function h($s)
{
    return htmlspecialchars($s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

// 読む画面の下の操作ボタン(.txtとEPUBで共通)
function reader_bar()
{
    ?>
<div id="bar">
<a href="index.php">本棚</a>
<button id="mark">記録</button>
<button id="list">記録一覧</button>
<button id="slower" class="small">V−</button>
<button id="faster" class="small">V＋</button>
<button id="page-left" class="small">＜</button>
<button id="page-right" class="small">＞</button>
<button id="smaller" class="small">A−</button>
<button id="larger" class="small">A＋</button>
<span id="where"></span>
</div>
<div id="panel" hidden>
<div id="panel-head"><span>記録一覧</span><button id="panel-close">閉じる</button></div>
<ul id="panel-list"></ul>
</div>
<div id="toast"></div>
<?php
}

function json_out($data, $status = 200)
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    exit(json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES));
}

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

// 展開したEPUBの場所。まだ展開していない(scp・cp等で置いた)か、原稿置き場の.epubの方が新しければ展開し直す。
// 展開できなければfalse。
function book_dir($name)
{
    global $BOOK_DIR;
    $epub = doc_path($name, 'epub');
    if ($epub === false) {
        return false;
    }
    $dir = $BOOK_DIR . '/' . $name;
    if (is_dir($dir) && filemtime($dir) >= filemtime($epub)) {
        return $dir;
    }
    if (!class_exists('ZipArchive') || extract_epub($epub, $dir) !== null) {
        return false;
    }
    touch($dir);
    return $dir;
}
