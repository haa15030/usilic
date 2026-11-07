<?php
// 記録の追加・一覧・削除。
//   .txt : $MARK_FILE      へ「原稿.txt:行:桁: 文字列」(Emacsのcompilation-modeで飛べる形)
//   .epub: $EPUB_MARK_FILE へ「本.epub:章のパス:位置: 文字列」
// GET  ?f=名前                    → その原稿の記録の一覧
// POST action=add    f, 位置, text → 1行追記
// POST action=delete f, entry      → 内容が一致する最初の1行を消す
require __DIR__ . '/common.php';

function fail($msg, $status = 400)
{
    json_out(['ok' => false, 'error' => $msg], $status);
}

$name = $_REQUEST['f'] ?? '';
if (doc_path($name) === false) {
    fail('原稿がありません');
}
$isEpub = valid_name($name, 'epub');
$file = $isEpub ? $EPUB_MARK_FILE : $MARK_FILE;

// 1行を位置と文字列に分ける。この原稿の行でなければnull。
function parse_entry($line, $name, $isEpub)
{
    $q = preg_quote($name, '/');
    if ($isEpub) {
        if (!preg_match('/^' . $q . ':([^:]+):(\d+): (.*)$/u', $line, $m)) {
            return null;
        }
        return ['entry' => $line, 'href' => $m[1], 'off' => (int)$m[2], 'text' => $m[3]];
    }
    if (!preg_match('/^' . $q . ':(\d+):(\d+): (.*)$/u', $line, $m)) {
        return null;
    }
    return ['entry' => $line, 'line' => (int)$m[1], 'col' => (int)$m[2], 'text' => $m[3]];
}

function read_lines($file)
{
    if (!is_file($file)) {
        return [];
    }
    return preg_split('/\n/', rtrim(file_get_contents($file), "\n"), -1, PREG_SPLIT_NO_EMPTY);
}

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    $list = [];
    foreach (read_lines($file) as $line) {
        $e = parse_entry($line, $name, $isEpub);
        if ($e) {
            $list[] = $e;
        }
    }
    json_out(['ok' => true, 'marks' => $list]);
}

require_same_origin();
$action = $_POST['action'] ?? '';

if ($action === 'add') {
    $num = ['options' => ['min_range' => 1]];
    if ($isEpub) {
        $href = (string)($_POST['href'] ?? '');
        $off = filter_var($_POST['off'] ?? '', FILTER_VALIDATE_INT, $num);
        if ($href === '' || preg_match('/[:\x00-\x1F]/', $href) || $off === false) {
            fail('位置が不正');
        }
        $pos = "$href:$off";
    } else {
        $line = filter_var($_POST['line'] ?? '', FILTER_VALIDATE_INT, $num);
        $col = filter_var($_POST['col'] ?? '', FILTER_VALIDATE_INT, $num);
        if ($line === false || $col === false) {
            fail('行・桁が不正');
        }
        $pos = "$line:$col";
    }
    // 1件を必ず1行に収める(改行・制御文字は空白へ)。
    // mbstringはphpパッケージに含まれないので正規表現で500文字に切る。
    $text = (string)($_POST['text'] ?? '');
    $text = preg_replace('/[\x00-\x1F\x7F]+/u', ' ', $text) ?? '';
    $text = preg_replace('/^(.{500}).+$/us', '$1', trim($text)) ?? '';
    if ($text === '') {
        $text = '(位置のみ)';
    }
    $entry = "$name:$pos: $text";
    if (file_put_contents($file, $entry . "\n", FILE_APPEND | LOCK_EX) === false) {
        fail('記録ファイルに書けません', 500);
    }
    json_out(['ok' => true, 'mark' => parse_entry($entry, $name, $isEpub)]);
}

if ($action === 'delete') {
    $entry = (string)($_POST['entry'] ?? '');
    if (parse_entry($entry, $name, $isEpub) === null) {
        fail('記録が不正');
    }
    $fp = fopen($file, 'c+');
    if (!$fp || !flock($fp, LOCK_EX)) {
        fail('記録ファイルに書けません', 500);
    }
    $lines = preg_split('/\n/', rtrim(stream_get_contents($fp), "\n"), -1, PREG_SPLIT_NO_EMPTY);
    $i = array_search($entry, $lines, true);
    if ($i !== false) {
        array_splice($lines, $i, 1);
        ftruncate($fp, 0);
        rewind($fp);
        fwrite($fp, $lines ? implode("\n", $lines) . "\n" : '');
    }
    flock($fp, LOCK_UN);
    fclose($fp);
    json_out(['ok' => true]);
}

fail('actionが不正');
