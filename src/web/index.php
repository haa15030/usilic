<?php
require __DIR__ . '/common.php';

$files = array_merge(glob($TEXT_DIR . '/*.txt') ?: [], glob($TEXT_DIR . '/*.epub') ?: []);
usort($files, function ($a, $b) { return filemtime($b) - filemtime($a); });

// usilic(^J^EのURL)用: 名前だけを1行に1つ(新しい順)。
if (isset($_GET['list'])) {
    header('Content-Type: text/plain; charset=utf-8');
    foreach ($files as $path) {
        echo basename($path), "\n";
    }
    exit;
}
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
ここへ.txtか.epub、またはディレクトリをドラッグ(またはタップして選ぶ)。同じ名前は上書きします。
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
<div id="dir" hidden>
<div id="dir-head">
<button id="dir-up">上へ</button>
<span id="dir-path"></span>
<button id="dir-send" disabled>送る</button>
<button id="dir-close">閉じる</button>
</div>
<ul id="dir-list"></ul>
</div>
<div id="toast"></div>
<script src="<?= asset('scroll.js') ?>"></script>
<script>
(function () {
    'use strict';
    var drop = document.getElementById('drop');
    var pick = document.getElementById('pick');
    var status = document.getElementById('status');
    var toastEl = document.getElementById('toast');

    var toastTimer = 0;
    function toast(msg) {
        toastEl.textContent = msg;
        toastEl.classList.add('show');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(function () { toastEl.classList.remove('show'); }, 2500);
    }

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

    // ---- 落としたディレクトリの一覧(パネル) ----
    // 中を潜って.txt/.epubを選び、選んだものだけ送る。選んだものはディレクトリを移っても残る。

    var dir = document.getElementById('dir');
    var dirList = document.getElementById('dir-list');
    var dirPath = document.getElementById('dir-path');
    var dirSend = document.getElementById('dir-send');
    var stack = [];          // 潜ったディレクトリ: {node, scroll}
    var chosen = new Map();  // fullPath → FileSystemFileEntry

    function wanted(name) {
        return /\.(txt|epub)$/.test(name) && name[0] !== '.';
    }

    // ディレクトリの中身を全部読む(readEntries()は一度に全部は返さないので、空になるまで繰り返す)
    function readAll(node) {
        if (node.virtual) {
            return Promise.resolve(node.entries);
        }
        var reader = node.createReader(), all = [];
        return new Promise(function (resolve, reject) {
            (function next() {
                reader.readEntries(function (batch) {
                    if (!batch.length) {
                        resolve(all);
                        return;
                    }
                    all = all.concat(batch);
                    next();
                }, reject);
            })();
        });
    }

    function updateSend() {
        dirSend.disabled = chosen.size === 0;
        dirSend.textContent = chosen.size ? '送る(' + chosen.size + ')' : '送る';
    }

    function show() {
        var top = stack[stack.length - 1];
        dirPath.textContent = stack.map(function (s) { return s.node.name; }).join('/') || '/';
        document.getElementById('dir-up').disabled = stack.length < 2;
        dirList.innerHTML = '<li class="empty">読み込み中…</li>';
        readAll(top.node).then(function (entries) {
            var dirs = entries.filter(function (e) { return e.isDirectory && e.name[0] !== '.'; });
            var files = entries.filter(function (e) { return e.isFile && wanted(e.name); });
            var byName = function (a, b) { return a.name.localeCompare(b.name, 'ja'); };
            dirs.sort(byName);
            files.sort(byName);
            dirList.innerHTML = '';
            dirs.forEach(function (e) {
                var li = document.createElement('li');
                li.className = 'sub';
                li.textContent = e.name + '/';
                li.addEventListener('click', function () {
                    top.scroll = dirList.scrollTop;
                    stack.push({ node: e, scroll: 0 });
                    show();
                });
                dirList.appendChild(li);
            });
            files.forEach(function (e) {
                var li = document.createElement('li');
                li.className = 'file' + (chosen.has(e.fullPath) ? ' on' : '');
                li.textContent = e.name;
                li.addEventListener('click', function () {
                    if (chosen.has(e.fullPath)) {
                        chosen.delete(e.fullPath);
                    } else {
                        chosen.set(e.fullPath, e);
                    }
                    li.classList.toggle('on', chosen.has(e.fullPath));
                    updateSend();
                });
                dirList.appendChild(li);
            });
            if (!dirs.length && !files.length) {
                dirList.innerHTML = '<li class="empty">.txt・.epubもディレクトリもありません</li>';
            }
            dirList.scrollTop = top.scroll;
        }).catch(function () {
            dirList.innerHTML = '<li class="empty">読めません</li>';
        });
    }

    function openDir(entries) {
        var root = entries.length === 1 && entries[0].isDirectory ? entries[0]
            : { virtual: true, name: '', entries: entries };
        stack = [{ node: root, scroll: 0 }];
        chosen.clear();
        updateSend();
        dir.hidden = false;
        show();
    }

    function closeDir() {
        list.stop();
        dir.hidden = true;
    }

    document.getElementById('dir-up').addEventListener('click', function () {
        if (stack.length > 1) {
            stack.pop();
            show();
        }
    });
    document.getElementById('dir-close').addEventListener('click', closeDir);
    dirSend.addEventListener('click', function () {
        var entries = Array.from(chosen.values());
        var names = entries.map(function (e) { return e.name; });
        var dup = names.filter(function (n, i) { return names.indexOf(n) !== i; });
        if (dup.length && !confirm('同じ名前があります(' + dup.join('、') + ')。後のものが上書きします。送りますか?')) {
            return;
        }
        Promise.all(entries.map(function (e) {
            return new Promise(function (resolve, reject) { e.file(resolve, reject); });
        })).then(function (files) {
            closeDir();
            upload(files);
        }).catch(function () { toast('ファイルを読めません'); });
    });

    // Escは、流れていれば止めるだけ(scroll.js)、止まっていればパネルを閉じる。
    // scroll.jsより先に登録して、止める前の状態を見る。
    window.addEventListener('keydown', function (e) {
        if (e.key === 'Escape' && !dir.hidden && !list.moving()) {
            closeDir();
        }
    });
    var list = Inertia(dirList, {
        axis: 'y',
        forward: function () { return 1; },
        friction: function () {
            var k = [0.99, 0.997, 0.999, 1];
            var i = 1;
            try {
                i = +(localStorage.getItem('friction') || 1);
            } catch (e) {
                // 既定のまま
            }
            return k[Math.min(Math.max(i, 0), k.length - 1)];
        },
        toast: toast,
        active: function () { return !dir.hidden; }
    });

    // ---- ドロップ ----

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
        // ディレクトリが混じっていれば一覧を出す(落としたファイルも一緒に並べる)。
        // webkitGetAsEntry()はdropの処理の中でしか使えないので、ここで取っておく。
        var entries = Array.prototype.map.call(e.dataTransfer.items || [], function (it) {
            return it.webkitGetAsEntry ? it.webkitGetAsEntry() : null;
        }).filter(Boolean);
        if (entries.some(function (en) { return en.isDirectory; })) {
            openDir(entries);
        } else {
            upload(e.dataTransfer.files);
        }
    });
    // 枠の外へ落としてもブラウザがファイルを開いてしまわないように
    window.addEventListener('dragover', function (e) { e.preventDefault(); });
    window.addEventListener('drop', function (e) { e.preventDefault(); });
    pick.addEventListener('change', function () { upload(pick.files); });
})();
</script>
</body>
</html>
