// 読む画面の共通部分: 左右フリックの慣性スクロール、操作ボタン、記録と記録一覧。
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
//   r.attach(doc)   その文書(iframeの中など)のtouchも慣性スクロールへ回す
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
    function savePos() {
        if (!ready) {
            return;
        }
        clearTimeout(saveTimer);
        saveTimer = setTimeout(function () {
            var m = opt.topPos();
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

    // 1フレーム(約16.7ms)ごとに速さに掛ける値。1は減速しない(定速で流れ続ける)。
    var FRICTIONS = [
        { label: '減速 大', k: 0.99 },
        { label: '減速 中', k: 0.997 },
        { label: '減速 小', k: 0.999 },
        { label: '定速', k: 1 }
    ];
    var frictionIdx = Math.min(+load('friction', 1) || 0, FRICTIONS.length - 1);
    var frictionBtn = document.getElementById('friction');
    function showFriction() {
        frictionBtn.textContent = FRICTIONS[frictionIdx].label;
    }

    var MIN_SPEED = 0.003;  // px/ms。これより遅くなったら止める
    var vel = 0;            // px/ms。scrollLeftが増える向きを正とする
    var pos = 0;            // scrollLeftを小数のまま持つ(ゆっくりの時に端数が消えないように)
    var raf = 0;
    var lastT = 0;

    var target = null;      // スペース等で向かっている先のscrollLeft(向かっていなければnull)

    // 矢印キーの慣性の段ごとの速さ(px/ms)。usilicrcのARROWの既定(1ドット進むごとに待つusec:
    // 200000 100000 25000 10000 5000 2000 1000 500 200 100 50 20)と同じ。減速せず一定の速さで流れる。
    var ARROW = [200000, 100000, 25000, 10000, 5000, 2000, 1000, 500, 200, 100, 50, 20].map(function (us) {
        return 1000 / us;
    });
    var coastLevel = -1;    // 矢印キーの慣性の段(0始まり)。矢印キーの慣性でなければ-1

    function stopInertia() {
        if (raf) {
            cancelAnimationFrame(raf);
        }
        raf = 0;
        vel = 0;
        target = null;
        coastLevel = -1;
    }

    function forwardSign() {
        return opt.forward ? opt.forward() : -1;
    }

    // 矢印キーの慣性(usilicと同じ): 止まっていれば押した向きに最低速で始める。流れている向きの
    // キーなら1段速く、逆なら1段遅く、最低速からさらに逆なら逆向きの最低速。dirは先へ=1、戻る=-1。
    // フリック等の慣性中に押した時は、その速さに一番近い段から変える。
    function stepCoast(dir) {
        var cur = 0;
        if (raf && target === null && vel !== 0) {
            cur = vel * forwardSign() > 0 ? 1 : -1;
            if (coastLevel < 0) {
                var speed = Math.abs(vel), best = 0;
                ARROW.forEach(function (v, i) {
                    if (Math.abs(Math.log(v / speed)) < Math.abs(Math.log(ARROW[best] / speed))) {
                        best = i;
                    }
                });
                coastLevel = best;
            }
        }
        var level = coastLevel;
        if (cur === 0) {
            stopInertia();
            cur = dir;
            level = 0;
        } else if (cur === dir) {
            level = Math.min(level + 1, ARROW.length - 1);
        } else if (level > 0) {
            level--;
        } else {
            cur = dir;
        }
        var running = raf !== 0 && target === null;
        coastLevel = level;
        vel = cur * forwardSign() * ARROW[level];
        toast('速さ ' + (level + 1) + '/' + ARROW.length + (cur > 0 ? ' 先へ' : ' 戻る'));
        if (!running) {
            pos = scroller.scrollLeft;
            lastT = performance.now();
            raf = requestAnimationFrame(step);
        }
    }

    // 矢印キー: 先のscrollLeftへ、残りの距離の一定の割合ずつ近づく
    function glide() {
        var diff = target - pos;
        if (Math.abs(diff) < 0.5) {
            pos = target;
        } else {
            pos += diff * 0.2;
        }
        scroller.scrollLeft = pos;
        // 着いたか、本の端で止まった
        if (pos === target || Math.abs(scroller.scrollLeft - pos) > 1) {
            pos = scroller.scrollLeft;
            raf = 0;
            target = null;
            return;
        }
        raf = requestAnimationFrame(glide);
    }

    // 押すたびに先を延ばすので、押し続けると続けて流れる
    function glideBy(d) {
        if (target === null) {
            stopInertia();
            pos = scroller.scrollLeft;
            target = pos;
        }
        target += d;
        if (!raf) {
            raf = requestAnimationFrame(glide);
        }
    }

    function step(t) {
        var dt = Math.max(0, Math.min(t - lastT, 50));
        lastT = t;
        if (coastLevel < 0) {
            vel *= Math.pow(FRICTIONS[frictionIdx].k, dt / 16.67);
        }
        pos += vel * dt;
        scroller.scrollLeft = pos;
        // 本の端に着いた(思った位置まで動けなかった)か、十分遅くなったら止める
        if (Math.abs(scroller.scrollLeft - pos) > 1 || (coastLevel < 0 && Math.abs(vel) < MIN_SPEED)) {
            pos = scroller.scrollLeft;
            stopInertia();
            return;
        }
        raf = requestAnimationFrame(step);
    }

    function startInertia() {
        if (Math.abs(vel) < MIN_SPEED) {
            vel = 0;
            return;
        }
        pos = scroller.scrollLeft;
        lastT = performance.now();
        raf = requestAnimationFrame(step);
    }

    // 範囲指定の取っ手の近くを触った時は、取っ手の操作とみなしてスクロールしない
    function nearSelection(win, x, y) {
        var sel = win.getSelection();
        if (!sel || !sel.rangeCount || sel.isCollapsed) {
            return false;
        }
        var rects = sel.getRangeAt(0).getClientRects();
        if (!rects.length) {
            return false;
        }
        var m = 40;
        return [rects[0], rects[rects.length - 1]].some(function (r) {
            return x > r.left - m && x < r.right + m && y > r.top - m && y < r.bottom + m;
        });
    }

    // targetの文書のtouchとマウスのドラッグ・ホイール・矢印キーを慣性スクロールへ回す。
    // 動いた量は画面(screenX)の座標で測る。iframeの中の座標(clientX)は、スクロールすると
    // iframeごと動いてしまい、動いた量に跳ね返って本文が左右にぶれるため。
    // ドラッグ中の状態は文書(iframe)ごとではなく1つだけ持つ。マウスは章のiframeの境目を
    // またいで動かすことがあり、離した(mouseup)のが押したのと別の文書に届いても慣性を付けるため。
    var drag = null;

    function attach(target, win) {

        // 押した: 流れていれば止める。止めただけならtrue(範囲指定の解除などをさせない)
        // x, yは画面の座標、cx, cyはその文書の中の座標(範囲指定の取っ手との距離用)
        function down(x, y, cx, cy) {
            var moving = raf !== 0;
            stopInertia();
            drag = {
                x0: x,
                y0: y,
                x: x,
                t0: performance.now(),
                mode: nearSelection(win, cx, cy) ? 'native' : null,
                samples: []
            };
            pos = scroller.scrollLeft;
            return moving;
        }

        // 動いた: スクロールとして扱ったらtrue
        function move(x, y) {
            if (!drag) {
                return false;
            }
            var now = performance.now();
            if (drag.mode === null) {
                var dx = x - drag.x0, dy = y - drag.y0;
                if (Math.abs(dx) < 8 && Math.abs(dy) < 8) {
                    return false;
                }
                // 長押ししてから動かしたのは範囲指定とみなし、ブラウザに任せる
                drag.mode = now - drag.t0 < 400 && Math.abs(dx) >= Math.abs(dy) ? 'scroll' : 'native';
            }
            if (drag.mode !== 'scroll') {
                return false;
            }
            // 指(マウス)の動きに本文が付いてくる向き(右へ動かすと先へ進む)
            pos -= x - drag.x;
            drag.x = x;
            scroller.scrollLeft = pos;
            pos = scroller.scrollLeft;
            if (!drag.samples.length || drag.samples[drag.samples.length - 1].x !== x) {
                drag.samples.push({ x: x, t: now });
            }
            while (drag.samples.length > 2 && now - drag.samples[0].t > 100) {
                drag.samples.shift();
            }
            return true;
        }

        // 離した: 離す直前の速さで慣性を付ける
        // 離した: 離す直前の速さで慣性を付ける。止めてから離した(limit ms以上動かさなかった)時は付けない。
        // マウスは動かしながら離すのが難しいので、limitを長めにし、速さは止める前の動きから測る
        // (samplesは動いた時にしか積まないので、止めていた間は速さに入らない)。
        function up(limit) {
            var d = drag;
            drag = null;
            if (!d || d.mode !== 'scroll') {
                return;
            }
            var s = d.samples;
            if (s.length < 2 || performance.now() - s[s.length - 1].t > limit) {
                return;
            }
            var a = s[0], b = s[s.length - 1];
            if (b.t <= a.t) {
                return;
            }
            vel = -(b.x - a.x) / (b.t - a.t);
            startInertia();
        }

        target.addEventListener('touchstart', function (e) {
            if (e.touches.length !== 1) {
                drag = null;
                return;
            }
            var t = e.touches[0];
            if (down(t.screenX, t.screenY, t.clientX, t.clientY)) {
                e.preventDefault();
            }
        }, { passive: false });
        target.addEventListener('touchmove', function (e) {
            if (e.touches.length === 1 && move(e.touches[0].screenX, e.touches[0].screenY)) {
                e.preventDefault();
            }
        }, { passive: false });
        target.addEventListener('touchend', function () { up(100); });
        target.addEventListener('touchcancel', function () { drag = null; });

        // マウス: 素早く左右へドラッグするとスクロール。押したまま0.4秒待ってから動かすと範囲指定。
        target.addEventListener('mousedown', function (e) {
            if (e.button !== 0) {
                return;
            }
            if (down(e.screenX, e.screenY, e.clientX, e.clientY)) {
                e.preventDefault();
            }
        });
        target.addEventListener('mousemove', function (e) {
            if (!(e.buttons & 1)) {
                drag = null;
                return;
            }
            if (move(e.screenX, e.screenY)) {
                e.preventDefault();
                // 動かし始めの8pxで始まった範囲指定を消す
                win.getSelection().removeAllRanges();
            }
        });
        target.addEventListener('mouseup', function () { up(300); });
        // 画像や範囲指定した文字をつかんだ時の、ブラウザのドラッグ&ドロップを始めさせない
        // (始まるとmousemove/mouseupが来なくなり、スクロールも慣性も止まる)
        target.addEventListener('dragstart', function (e) {
            if (drag && drag.mode !== 'native') {
                e.preventDefault();
            }
        });

        // ホイール: 縦の回転も横のスクロールにする(下へ回すと先へ)
        target.addEventListener('wheel', function (e) {
            if (Math.abs(e.deltaY) <= Math.abs(e.deltaX)) {
                return;
            }
            e.preventDefault();
            stopInertia();
            var unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? scroller.clientWidth : 1;
            scroller.scrollLeft += e.deltaY * unit * (opt.forward ? opt.forward() : -1);
        }, { passive: false });

        // キー: 矢印は慣性(usilicと同じく、止まっている時は↓で先へ/↑で戻るを最低速で始め、
        // 流れている間は↓と先へ向かう横の矢印で速く、↑と逆の横の矢印で遅く)。
        // Shift+←→とスペースで画面1枚分、Escで止める。
        win.addEventListener('keydown', function (e) {
            if (e.metaKey || e.ctrlKey || e.altKey || /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) {
                return;
            }
            var w = scroller.clientWidth;
            var d;
            // 横の矢印のどちらが先へか(縦書き・右綴じは←)
            var fwdKey = forwardSign() < 0 ? 'ArrowLeft' : 'ArrowRight';
            if (e.shiftKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) {
                d = w * 0.9 * (e.key === 'ArrowLeft' ? -1 : 1);
            } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
                e.preventDefault();
                stepCoast(e.key === 'ArrowDown' ? 1 : -1);
                return;
            } else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
                // 横の矢印は流れている間だけ(止まっている時は何もしない)
                e.preventDefault();
                if (raf && target === null) {
                    stepCoast(e.key === fwdKey ? 1 : -1);
                }
                return;
            } else if (e.key === ' ') {
                d = w * 0.9 * (opt.forward ? opt.forward() : -1) * (e.shiftKey ? -1 : 1);
            } else if (e.key === 'Escape') {
                stopInertia();
                return;
            } else {
                return;
            }
            e.preventDefault();
            glideBy(d);
        });
    }
    attach(scroller, window);

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
                stopInertia();
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
    function setFont(size) {
        var keep = opt.topPos();
        fontSize = Math.min(font.max, Math.max(font.min, size));
        save(font.key, fontSize);
        font.apply(fontSize);
        if (keep) {
            opt.jump(keep, false);
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
    onTap(document.getElementById('smaller'), function () { setFont(fontSize - font.step); });
    onTap(document.getElementById('larger'), function () { setFont(fontSize + font.step); });

    showFriction();
    font.apply(fontSize);

    return {
        attach: attach,
        restore: restore,
        toast: toast,
        fontSize: function () { return fontSize; }
    };
};
