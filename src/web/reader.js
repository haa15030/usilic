// 読む画面の共通部分: 左右フリックの慣性スクロール、操作ボタン、記録と記録一覧。
// .txt(txt.js)とEPUB(epub.js)が、位置の求め方・飛び方を渡して使う。
//
//   var r = Reader({
//       scroller:   横にスクロールする要素,
//       markTarget: function () → {pos: 送る位置(オブジェクト), text: 文字列} か null,
//       jump:       function (mark, select) 記録(marks.phpの一覧の1件)の位置へ飛ぶ,
//       describe:   function (mark) → 一覧に出す位置の説明,
//       topPos:     function () → 画面の右端(読んでいる所)の位置(markと同じ形),
//       font:       {key, def, min, max, step, unit, apply: function (size)}
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

    function stopInertia() {
        if (raf) {
            cancelAnimationFrame(raf);
        }
        raf = 0;
        vel = 0;
    }

    function step(t) {
        var dt = Math.max(0, Math.min(t - lastT, 50));
        lastT = t;
        vel *= Math.pow(FRICTIONS[frictionIdx].k, dt / 16.67);
        pos += vel * dt;
        scroller.scrollLeft = pos;
        // 本の端に着いた(思った位置まで動けなかった)か、十分遅くなったら止める
        if (Math.abs(scroller.scrollLeft - pos) > 1 || Math.abs(vel) < MIN_SPEED) {
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

    // targetの文書のtouchを慣性スクロールへ回す。座標はその文書の中の座標だが、
    // 使うのは動いた量だけなのでiframeの中でもそのまま使える。
    function attach(target, win) {
        var touch = null;

        target.addEventListener('touchstart', function (e) {
            if (e.touches.length !== 1) {
                touch = null;
                return;
            }
            var moving = raf !== 0;
            stopInertia();
            var t = e.touches[0];
            touch = {
                x0: t.clientX,
                y0: t.clientY,
                x: t.clientX,
                t0: performance.now(),
                mode: nearSelection(win, t.clientX, t.clientY) ? 'native' : null,
                samples: []
            };
            pos = scroller.scrollLeft;
            // 流れているのを止めるだけのタップでは、範囲指定の解除などをさせない
            if (moving) {
                e.preventDefault();
            }
        }, { passive: false });

        target.addEventListener('touchmove', function (e) {
            if (!touch || e.touches.length !== 1) {
                return;
            }
            var t = e.touches[0];
            var now = performance.now();
            if (touch.mode === null) {
                var dx = t.clientX - touch.x0, dy = t.clientY - touch.y0;
                if (Math.abs(dx) < 8 && Math.abs(dy) < 8) {
                    return;
                }
                // 長押ししてから動かしたのは範囲指定とみなし、Safariに任せる
                touch.mode = now - touch.t0 < 400 && Math.abs(dx) >= Math.abs(dy) ? 'scroll' : 'native';
            }
            if (touch.mode !== 'scroll') {
                return;
            }
            e.preventDefault();
            // 指の動きに本文が付いてくる向き(指を右へ動かすと先へ進む)
            pos -= t.clientX - touch.x;
            touch.x = t.clientX;
            scroller.scrollLeft = pos;
            pos = scroller.scrollLeft;
            touch.samples.push({ x: t.clientX, t: now });
            while (touch.samples.length > 2 && now - touch.samples[0].t > 100) {
                touch.samples.shift();
            }
        }, { passive: false });

        target.addEventListener('touchend', function () {
            var tc = touch;
            touch = null;
            if (!tc || tc.mode !== 'scroll') {
                return;
            }
            var s = tc.samples;
            // 指を止めてから離した時は慣性を付けない
            if (s.length < 2 || performance.now() - s[s.length - 1].t > 80) {
                return;
            }
            var a = s[0], b = s[s.length - 1];
            if (b.t <= a.t) {
                return;
            }
            vel = -(b.x - a.x) / (b.t - a.t);
            startInertia();
        });

        target.addEventListener('touchcancel', function () {
            touch = null;
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
