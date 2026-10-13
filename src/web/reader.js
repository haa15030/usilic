// 縦書きの原稿を、左右フリックの慣性スクロールで読み、校閲箇所を記録する。
(function () {
    'use strict';

    var text = document.getElementById('text');
    var file = text.dataset.file;
    var paras = Array.prototype.slice.call(text.querySelectorAll('p[data-line]'));
    var whereEl = document.getElementById('where');
    var toastEl = document.getElementById('toast');

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

    // ---- 位置 ----

    // 画面上の横位置xにある段落(=原稿の1行)の番号を返す。縦書きなので段落は
    // 右から左へ並び、番号が増えるほど左にある。最初の段落より右なら0。
    function paraAtX(x) {
        var lo = 0, hi = paras.length - 1, found = paras.length - 1;
        while (lo <= hi) {
            var mid = (lo + hi) >> 1;
            if (paras[mid].getBoundingClientRect().left < x) {
                found = mid;
                hi = mid - 1;
            } else {
                lo = mid + 1;
            }
        }
        return found;
    }

    // 読んでいる所 = 画面の右端に見えている段落
    function topPara() {
        var rc = text.getBoundingClientRect();
        var pad = parseFloat(getComputedStyle(text).paddingRight) || 0;
        return paras[paraAtX(rc.right - pad)];
    }

    function showPara(p) {
        if (p) {
            p.scrollIntoView({ block: 'start' });
        }
    }

    var saveTimer = 0;
    function savePos() {
        clearTimeout(saveTimer);
        saveTimer = setTimeout(function () {
            var p = topPara();
            if (!p) {
                return;
            }
            save('pos:' + file, p.dataset.line);
            whereEl.textContent = p.dataset.line + ' / ' + paras.length + '行';
        }, 200);
    }
    text.addEventListener('scroll', savePos);

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
        text.scrollLeft = pos;
        // 本の端に着いた(思った位置まで動けなかった)か、十分遅くなったら止める
        if (Math.abs(text.scrollLeft - pos) > 1 || Math.abs(vel) < MIN_SPEED) {
            pos = text.scrollLeft;
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
        pos = text.scrollLeft;
        lastT = performance.now();
        raf = requestAnimationFrame(step);
    }

    // 範囲指定の取っ手の近くを触った時は、取っ手の操作とみなしてスクロールしない
    function nearSelection(x, y) {
        var sel = getSelection();
        if (!sel.rangeCount || sel.isCollapsed) {
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

    var touch = null;

    text.addEventListener('touchstart', function (e) {
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
            mode: nearSelection(t.clientX, t.clientY) ? 'native' : null,
            samples: []
        };
        pos = text.scrollLeft;
        // 流れているのを止めるだけのタップでは、範囲指定の解除などをさせない
        if (moving) {
            e.preventDefault();
        }
    }, { passive: false });

    text.addEventListener('touchmove', function (e) {
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
        text.scrollLeft = pos;
        pos = text.scrollLeft;
        touch.samples.push({ x: t.clientX, t: now });
        while (touch.samples.length > 2 && now - touch.samples[0].t > 100) {
            touch.samples.shift();
        }
    }, { passive: false });

    text.addEventListener('touchend', function () {
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

    text.addEventListener('touchcancel', function () {
        touch = null;
    });

    // ---- 記録 ----

    function paraOf(node) {
        var el = node.nodeType === 1 ? node : node.parentNode;
        var p = el && el.closest ? el.closest('p[data-line]') : null;
        return p && text.contains(p) ? p : null;
    }

    // 段落の先頭からnode/offsetまでの文字数+1(Emacsの桁と同じく文字単位、1始まり)
    function colOf(p, node, offset) {
        var r = document.createRange();
        r.setStart(p, 0);
        r.setEnd(node, offset);
        return Array.from(r.toString()).length + 1;
    }

    // 範囲指定が無い時は、画面中央の位置を記録する
    function centerTarget() {
        var rc = text.getBoundingClientRect();
        var x = rc.left + rc.width / 2, y = rc.top + rc.height / 2;
        var p = paras[paraAtX(x)];
        if (!p) {
            return null;
        }
        var r = document.caretRangeFromPoint ? document.caretRangeFromPoint(x, y) : null;
        if (r && paraOf(r.startContainer) === p) {
            return { p: p, col: colOf(p, r.startContainer, r.startOffset), text: '' };
        }
        return { p: p, col: 1, text: '' };
    }

    function markTarget() {
        var sel = getSelection();
        if (sel.rangeCount && !sel.isCollapsed) {
            var r = sel.getRangeAt(0);
            var p = paraOf(r.startContainer);
            if (p) {
                return { p: p, col: colOf(p, r.startContainer, r.startOffset), text: sel.toString() };
            }
        }
        return centerTarget();
    }

    function record() {
        var m = markTarget();
        if (!m) {
            toast('記録する位置が見つかりません');
            return;
        }
        var line = m.p.dataset.line;
        var body = new URLSearchParams({ f: file, line: line, col: m.col, text: m.text });
        fetch('mark.php', { method: 'POST', body: body, credentials: 'same-origin' })
            .then(function (res) { return res.json(); })
            .then(function (j) {
                if (!j.ok) {
                    toast('記録できません: ' + j.error);
                    return;
                }
                var snippet = Array.from(m.text.replace(/\s+/g, ' ')).slice(0, 20).join('');
                toast(line + '行' + m.col + '桁を記録' + (snippet ? ' 「' + snippet + '」' : '(位置のみ)'));
                getSelection().removeAllRanges();
            })
            .catch(function () { toast('記録できません(通信)'); });
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

    var fontSize = +load('fs', 22) || 22;
    function applyFont(keep) {
        text.style.setProperty('--fs', fontSize + 'px');
        save('fs', fontSize);
        showPara(keep);
    }

    onTap(document.getElementById('mark'), record);
    onTap(frictionBtn, function () {
        frictionIdx = (frictionIdx + 1) % FRICTIONS.length;
        save('friction', frictionIdx);
        showFriction();
    });
    onTap(document.getElementById('smaller'), function () {
        var keep = topPara();
        fontSize = Math.max(12, fontSize - 2);
        applyFont(keep);
    });
    onTap(document.getElementById('larger'), function () {
        var keep = topPara();
        fontSize = Math.min(48, fontSize + 2);
        applyFont(keep);
    });

    // ---- 起動時 ----

    showFriction();
    text.style.setProperty('--fs', fontSize + 'px');
    var last = +load('pos:' + file, 1);
    showPara(paras[Math.min(Math.max(last, 1), paras.length) - 1]);
    savePos();
})();
