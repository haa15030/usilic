<?php
require __DIR__ . '/config.php';

// 原稿の名前を検査し、原稿置き場の中の実際のパスを返す。不正ならfalse。
// 置き場の外を指せないよう、ディレクトリを含む名前・隠しファイルは受け付けない。
function text_path($name)
{
    global $TEXT_DIR;
    if (!is_string($name) || $name === '' || $name !== basename($name)
        || $name[0] === '.' || !preg_match('/\.txt$/', $name)) {
        return false;
    }
    $path = $TEXT_DIR . '/' . $name;
    return is_file($path) ? $path : false;
}

function h($s)
{
    return htmlspecialchars($s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}
