<?php
require __DIR__ . '/common.php';

$files = array_merge(glob($TEXT_DIR . '/*.txt') ?: [], glob($TEXT_DIR . '/*.epub') ?: []);
usort($files, function ($a, $b) { return filemtime($b) - filemtime($a); });
?>
<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>本棚</title>
<link rel="stylesheet" href="<?= asset('reader.css') ?>">
</head>
<body class="list">
<h1>本棚</h1>
<label id="drop">
ここへ.txtか.epubをドラッグ(またはタップして選ぶ)。同じ名前は上書きします。
<input type="file" id="pick" accept=".txt,.epub,text/plain,application/epub+zip" multiple hidden>
</label>
<p id="status"></p>
<?php if (!$files): ?>
<p>原稿がありません(<?= h($TEXT_DIR) ?>)。</p>
<?php else: ?>
<ul>
<?php foreach ($files as $path):
    $name = basename($path);
    $url = (valid_name($name, 'epub') ? 'book.php' : 'read.php') . '?f=' . rawurlencode($name); ?>
<li><a href="<?= h($url) ?>"><?= h($name) ?></a>
<span class="date"><?= date('Y-m-d H:i', filemtime($path)) ?></span></li>
<?php endforeach; ?>
</ul>
<?php endif; ?>
<script>
(function () {
    'use strict';
    var drop = document.getElementById('drop');
    var pick = document.getElementById('pick');
    var status = document.getElementById('status');

    // 1つずつ順に送り、全部終わったら一覧を読み直す
    function upload(files) {
        files = Array.prototype.slice.call(files);
        if (!files.length) {
            return;
        }
        var failed = false;
        files.reduce(function (p, file) {
            return p.then(function () {
                status.textContent = file.name + ' を送っています…';
                var fd = new FormData();
                fd.append('file', file);
                return fetch('upload.php', { method: 'POST', body: fd, credentials: 'same-origin' })
                    .then(function (res) { return res.json(); })
                    .then(function (j) {
                        if (!j.ok) {
                            throw new Error(j.error);
                        }
                    })
                    .catch(function (e) {
                        failed = true;
                        status.textContent = file.name + ': ' + (e.message || '送れません');
                    });
            });
        }, Promise.resolve()).then(function () {
            if (!failed) {
                location.reload();
            }
        });
    }

    ['dragenter', 'dragover'].forEach(function (t) {
        drop.addEventListener(t, function (e) {
            e.preventDefault();
            drop.classList.add('over');
        });
    });
    drop.addEventListener('dragleave', function () { drop.classList.remove('over'); });
    drop.addEventListener('drop', function (e) {
        e.preventDefault();
        drop.classList.remove('over');
        upload(e.dataTransfer.files);
    });
    // 枠の外へ落としてもブラウザがファイルを開いてしまわないように
    window.addEventListener('dragover', function (e) { e.preventDefault(); });
    window.addEventListener('drop', function (e) { e.preventDefault(); });
    pick.addEventListener('change', function () { upload(pick.files); });
})();
</script>
</body>
</html>
