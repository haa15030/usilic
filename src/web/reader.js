// 読む画面の共通部分: 左右の慣性スクロール(scroll.js)、操作ボタン、記録と記録一覧。
// .txt(txt.js)とEPUB(epub.js)が、位置の求め方・飛び方を渡して使う。
//
//   var r = Reader({
//       scroller:   横にスクロールする要素,
//       markTarget: function () → {pos: 送る位置(オブジェクト), text: 文字列} か null,
//       jump:       function (mark, select) 記録(marks.phpの一覧の1件)の位置へ飛ぶ,
//       describe:   function (mark) → 一覧に出す位置の説明,
//       topPos:     function () → 画面の右端(読んでいる所)の位置(markと同じ形),
//       font:       {key, def, min, max, step, unit, apply: function (size)},
//       forward:    function () → 先へ進む時のscrollLeftの増える向き(縦書き・右綴じは-1)
//                   (省略時は-1)
//   });
//   r.attach(doc, win)  その文書(iframeの中など)のtouch等も慣性スクロールへ回す
//   r.restore()     覚えていた位置へ戻る(本文を並べ終えた後に呼ぶ)
var Reader = function (opt) {
    'use strict';

    var scroller = opt.scroller;
    var file = scroller.dataset.file;
    var whereEl = document.getElementById('where');
    var toastEl = document.getElementById('toast');
    var panel = document.getElementById('panel');
    var panelList = document.getElementById('panel-list');

    // localStorageはプライベートブラウズ等で使えないことがあるので、失敗しても動くようにする。
    function load(key, def) {
        try {
            var v = localStorage.getItem(key);
            return v === null ? def : v;
        } catch (e) {
            return def;
        }
    }
    function save(key, v) {
        try {
            localStorage.setItem(key, v);
        } catch (e) {
            // 覚えられないだけで、読むのには困らない
        }
    }

    var toastTimer = 0;
    function toast(msg) {
        toastEl.textContent = msg;
        toastEl.classList.add('show');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(function () { toastEl.classList.remove('show'); }, 2500);
    }

    // ---- 読んでいた位置 ----

    var ready = false;
    var saveTimer = 0;
    // 文字の大きさを変える前に読んでいた位置(setFont()参照)。そこから自分でスクロールするまで使う。
    var fontAnchor = null, fontAnchorScroll = 0;
    function anchored() {
        return fontAnchor && Math.abs(scroller.scrollLeft - fontAnchorScroll) <= 1;
    }
    function savePos() {
        if (!ready) {
            return;
        }
        clearTimeout(saveTimer);
        saveTimer = setTimeout(function () {
            var m = anchored() ? fontAnchor : opt.topPos();
            if (m) {
                save('pos:' + file, JSON.stringify(m));
                whereEl.textContent = opt.describe(m);
            }
        }, 200);
    }
    scroller.addEventListener('scroll', savePos);

    function restore() {
        var m = null;
        try {
            m = JSON.parse(load('pos:' + file, 'null'));
        } catch (e) {
            m = null;
        }
        if (m && typeof m === 'object') {
            opt.jump(m, false);
        }
        ready = true;
        savePos();
    }

    // ---- 慣性スクロール ----

    // 1フレーム(約16.7ms)ごとに速さに掛ける値。一定の速さで流すのはV−/V＋(矢印キーと同じ段)で。
    var FRICTIONS = [
        { label: '減速 大', k: 0.99 },
        { label: '減速 中', k: 0.997 },
        { label: '減速 小', k: 0.999 }
    ];
    var frictionIdx = Math.min(+load('friction', 1) || 0, FRICTIONS.length - 1);
    var frictionBtn = document.getElementById('friction');
    function showFriction() {
        frictionBtn.textContent = FRICTIONS[frictionIdx].label;
    }

    var inertia = Inertia(scroller, {
        axis: 'x',
        forward: function () { return opt.forward ? opt.forward() : -1; },
        friction: function () { return FRICTIONS[frictionIdx].k; },
        toast: toast
    });

    // ---- 記録 ----

    function post(params) {
        params.f = file;
        return fetch('marks.php', { method: 'POST', body: new URLSearchParams(params), credentials: 'same-origin' })
            .then(function (res) { return res.json(); })
            .then(function (j) {
                if (!j.ok) {
                    throw new Error(j.error);
                }
                return j;
            });
    }

    function snippet(s) {
        return Array.from(s.replace(/\s+/g, ' ')).slice(0, 20).join('');
    }

    function record() {
        var m = opt.markTarget();
        if (!m) {
            toast('記録する位置が見つかりません');
            return;
        }
        var params = Object.assign({ action: 'add', text: m.text }, m.pos);
        post(params)
            .then(function (j) {
                var s = snippet(m.text);
                toast(opt.describe(j.mark) + 'を記録' + (s ? ' 「' + s + '」' : '(位置のみ)'));
                clearSelections();
            })
            .catch(function (e) { toast('記録できません: ' + (e.message || '通信')); });
    }

    // 範囲指定を消す(iframeの中の分も)
    function clearSelections() {
        window.getSelection().removeAllRanges();
        Array.prototype.forEach.call(document.querySelectorAll('iframe'), function (f) {
            try {
                f.contentWindow.getSelection().removeAllRanges();
            } catch (e) {
                // 読み込み前など
            }
        });
    }

    // ---- 記録一覧 ----

    function openPanel() {
        panelList.innerHTML = '<li class="empty">読み込み中…</li>';
        panel.hidden = false;
        fetch('marks.php?f=' + encodeURIComponent(file), { credentials: 'same-origin' })
            .then(function (res) { return res.json(); })
            .then(function (j) {
                if (!j.ok) {
                    throw new Error(j.error);
                }
                showMarks(j.marks);
            })
            .catch(function (e) {
                panelList.innerHTML = '';
                panelList.appendChild(item('読み込めません: ' + (e.message || '通信'), 'empty'));
            });
    }

    function item(text, cls) {
        var li = document.createElement('li');
        li.textContent = text;
        if (cls) {
            li.className = cls;
        }
        return li;
    }

    function showMarks(marks) {
        panelList.innerHTML = '';
        if (!marks.length) {
            panelList.appendChild(item('記録はありません', 'empty'));
            return;
        }
        marks.forEach(function (m) {
            var li = document.createElement('li');
            var go = document.createElement('button');
            go.className = 'go';
            var where = document.createElement('span');
            where.className = 'pos';
            where.textContent = opt.describe(m);
            go.appendChild(where);
            go.appendChild(document.createTextNode(m.text));
            go.addEventListener('click', function () {
                panel.hidden = true;
                inertia.stop();
                opt.jump(m, true);
            });
            var del = document.createElement('button');
            del.className = 'del';
            del.textContent = '消す';
            del.addEventListener('click', function () {
                post({ action: 'delete', entry: m.entry })
                    .then(function () { li.remove(); })
                    .catch(function (e) { toast('消せません: ' + (e.message || '通信')); });
            });
            li.appendChild(go);
            li.appendChild(del);
            panelList.appendChild(li);
        });
    }

    // ---- ボタン ----

    // 押した時に範囲指定が消えないよう、touchstart/mousedownの既定動作を止める。
    // iOSではtouchstartを止めるとclickが来ないので、そこで実行する。
    function onTap(el, fn) {
        el.addEventListener('touchstart', function (e) {
            e.preventDefault();
            fn();
        }, { passive: false });
        el.addEventListener('mousedown', function (e) { e.preventDefault(); });
        el.addEventListener('click', fn);
    }

    var font = opt.font;
    var fontSize = +load(font.key, font.def) || font.def;
    // 2026-10-14、直接のユーザー報告("フォントサイズを変えると現在地が大幅にズレまくるので直前の位置を記憶して
    // おいてほしい"): 戻る先を押すたびに画面から取り直すと、戻った所のずれが積み重なる。最初に押す前の位置を
    // 覚えておき、自分でスクロールするまでは何度押してもそこへ戻る(読んでいた位置の記録もそれにする)。
    function setFont(size) {
        inertia.stop();
        if (!anchored()) {
            fontAnchor = opt.topPos();
        }
        fontSize = Math.min(font.max, Math.max(font.min, size));
        save(font.key, fontSize);
        font.apply(fontSize);
        if (fontAnchor) {
            opt.jump(fontAnchor, false);
            fontAnchorScroll = scroller.scrollLeft;
        }
    }

    onTap(document.getElementById('mark'), record);
    onTap(document.getElementById('list'), openPanel);
    document.getElementById('panel-close').addEventListener('click', function () { panel.hidden = true; });
    onTap(frictionBtn, function () {
        frictionIdx = (frictionIdx + 1) % FRICTIONS.length;
        save('friction', frictionIdx);
        showFriction();
    });
    // V−/V＋: 矢印キー(↑/↓)と同じく1段ずつ。止まっていればV＋で先へ、V−で戻るを最低速で流し始める。
    onTap(document.getElementById('slower'), function () { inertia.coast(-1); });
    onTap(document.getElementById('faster'), function () { inertia.coast(1); });
    // ＜/＞: 画面1枚分、見た目の向きのまま(縦書きは＜で先へ)
    onTap(document.getElementById('page-left'), function () { inertia.page(-1); });
    onTap(document.getElementById('page-right'), function () { inertia.page(1); });
    onTap(document.getElementById('smaller'), function () { setFont(fontSize - font.step); });
    onTap(document.getElementById('larger'), function () { setFont(fontSize + font.step); });

    // 2026-10-15、直接のユーザー依頼("ピンチで文字拡大縮小ができるのは反応今イチのKinnopyだけなので（Kindle、
    // iBooksいずれも不能）助かる。よろしく"): 2本指の間隔の比で文字の大きさを変える。組み直し(章の測り直し)は
    // 重いので、指を動かしている間は大きさをtoastに出すだけにし、離した時に1回だけsetFont()する
    // (位置はA−/A＋と同じく変える前の所へ戻る)。ブラウザ自身の拡大は止める。
    var pinchStart = null;  // {dist, size}
    var pinchSize = 0;
    function touchDistance(e) {
        var a = e.touches[0], b = e.touches[1];
        return Math.hypot(a.screenX - b.screenX, a.screenY - b.screenY);
    }
    function pinch(target) {
        target.addEventListener('touchstart', function (e) {
            if (e.touches.length === 2) {
                inertia.stop();
                pinchStart = { dist: Math.max(touchDistance(e), 1), size: fontSize };
                pinchSize = fontSize;
                e.preventDefault();
            }
        }, { passive: false });
        target.addEventListener('touchmove', function (e) {
            if (!pinchStart || e.touches.length !== 2) {
                return;
            }
            e.preventDefault();
            var size = Math.round(pinchStart.size * touchDistance(e) / pinchStart.dist / font.step) * font.step;
            size = Math.min(font.max, Math.max(font.min, size));
            if (size !== pinchSize) {
                pinchSize = size;
                toast('文字の大きさ ' + size + (font.unit || (font.key === 'efs' ? '%' : 'px')));
            }
        }, { passive: false });
        function end(e) {
            if (!pinchStart || e.touches.length >= 2) {
                return;
            }
            var size = pinchSize;
            pinchStart = null;
            if (size !== fontSize) {
                setFont(size);
            }
        }
        target.addEventListener('touchend', end);
        target.addEventListener('touchcancel', end);
        // iOSのSafariは、viewportのuser-scalable=noがあってもピンチで拡大するので止める
        target.addEventListener('gesturestart', function (e) { e.preventDefault(); });
    }
    pinch(document);

    showFriction();
    font.apply(fontSize);

    return {
        attach: function (doc, win) {
            inertia.attach(doc, win);
            pinch(doc);
        },
        restore: restore,
        toast: toast,
        fontSize: function () { return fontSize; }
    };
};
