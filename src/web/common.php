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
<button id="friction"></button>
<button id="smaller">A−</button>
<button id="larger">A＋</button>
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
