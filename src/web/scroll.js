// 慣性スクロール(本文の横、本棚のディレクトリ一覧の縦で共通)。
//
//   var s = Inertia(scroller, {
//       axis:     'x'(横、本文)か'y'(縦、一覧),
//       forward:  function () → 先へ進む時にスクロール位置が増える向き(1か-1),
//       friction: function () → 1フレーム(約16.7ms)ごとに速さに掛ける値(1なら減速しない),
//       toast:    function (msg) 短い知らせを出す,
//       active:   function () → キーを受け付けるか(省略時は常に)
//   });
//   s.attach(doc, win)  その文書(iframeの中など)のtouch・マウス・ホイール・キーも受ける
//   s.stop()            止める
//   s.moving()          流れているか
//
// 操作: フリック(マウスでは素早いドラッグ)で流れ、タップ(クリック)で止まる。押したまま0.4秒
// 待ってから動かすと、ブラウザの範囲指定に任せる。キーはusilicと同じく、止まっている時は
// スクロールと交差する向きの矢印(横なら↓先へ/↑、縦なら→先へ/←)で最低速で流し始め、流れている
// 間はそれに加えてスクロールの向きの矢印でも、先へ向かう方で1段速く、逆で1段遅くする。
// 最低速からさらに遅くすると逆向きの最低速。Shift+矢印(スクロールの向き)とスペースで画面1枚分、
// Escで止める。
var Inertia = function (scroller, opt) {
    'use strict';

    var horiz = opt.axis !== 'y';
    var DEBUG = /[?&]debug\b/.test(location.search);

    function getPos() {
        return horiz ? scroller.scrollLeft : scroller.scrollTop;
    }
    function setPos(v) {
        if (horiz) {
            scroller.scrollLeft = v;
        } else {
            scroller.scrollTop = v;
        }
    }
    function pageSize() {
        return horiz ? scroller.clientWidth : scroller.clientHeight;
    }
    function forwardSign() {
        return opt.forward ? opt.forward() : 1;
    }

    var MIN_SPEED = 0.003;  // px/ms。これより遅くなったら止める
    var vel = 0;            // px/ms。スクロール位置が増える向きを正とする
    var pos = 0;            // スクロール位置を小数のまま持つ(ゆっくりの時に端数が消えないように)
    var raf = 0;
    var lastT = 0;
    var glideTo = null;     // スペース等で向かっている先のスクロール位置(向かっていなければnull)

    // 矢印キーの慣性の段ごとの速さ(px/ms)。usilicrcのARROWの既定(1ドット進むごとに待つusec:
    // 200000 100000 25000 10000 5000 2000 1000 500 200 100 50 20)と同じ。減速せず一定の速さで流れる。
    var ARROW = [200000, 100000, 25000, 10000, 5000, 2000, 1000, 500, 200, 100, 50, 20].map(function (us) {
        return 1000 / us;
    });
    var coastLevel = -1;    // 矢印キーの慣性の段(0始まり)。矢印キーの慣性でなければ-1

    function stop() {
        if (raf) {
            cancelAnimationFrame(raf);
        }
        raf = 0;
        vel = 0;
        glideTo = null;
        coastLevel = -1;
    }

    function step(t) {
        var dt = Math.max(0, Math.min(t - lastT, 50));
        lastT = t;
        if (coastLevel < 0) {
            vel *= Math.pow(opt.friction(), dt / 16.67);
        }
        pos += vel * dt;
        setPos(pos);
        // 端に着いた(思った位置まで動けなかった)か、十分遅くなったら止める
        if (Math.abs(getPos() - pos) > 1 || (coastLevel < 0 && Math.abs(vel) < MIN_SPEED)) {
            pos = getPos();
            stop();
            return;
        }
        raf = requestAnimationFrame(step);
    }

    function start() {
        if (Math.abs(vel) < MIN_SPEED) {
            vel = 0;
            return;
        }
        pos = getPos();
        lastT = performance.now();
        raf = requestAnimationFrame(step);
    }

    // 矢印キーの慣性。dirは先へ=1、戻る=-1。
    // フリック等の慣性中に押した時は、その速さに一番近い段から変える。
    function stepCoast(dir) {
        var cur = 0;
        if (raf && glideTo === null && vel !== 0) {
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
            stop();
            cur = dir;
            level = 0;
        } else if (cur === dir) {
            level = Math.min(level + 1, ARROW.length - 1);
        } else if (level > 0) {
            level--;
        } else {
            cur = dir;
        }
        var running = raf !== 0 && glideTo === null;
        coastLevel = level;
        vel = cur * forwardSign() * ARROW[level];
        opt.toast('速さ ' + (level + 1) + '/' + ARROW.length + (cur > 0 ? ' 先へ' : ' 戻る'));
        if (!running) {
            pos = getPos();
            lastT = performance.now();
            raf = requestAnimationFrame(step);
        }
    }

    // スペース等: 先のスクロール位置へ、残りの距離の一定の割合ずつ近づく
    function glide() {
        var diff = glideTo - pos;
        if (Math.abs(diff) < 0.5) {
            pos = glideTo;
        } else {
            pos += diff * 0.2;
        }
        setPos(pos);
        // 着いたか、端で止まった
        if (pos === glideTo || Math.abs(getPos() - pos) > 1) {
            pos = getPos();
            raf = 0;
            glideTo = null;
            return;
        }
        raf = requestAnimationFrame(glide);
    }

    // 押すたびに先を延ばすので、押し続けると続けて流れる
    function glideBy(d) {
        if (glideTo === null) {
            stop();
            pos = getPos();
            glideTo = pos;
        }
        glideTo += d;
        if (!raf) {
            raf = requestAnimationFrame(glide);
        }
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

    // 動いた量は画面(screenX/Y)の座標で測る。iframeの中の座標(clientX)は、スクロールすると
    // iframeごと動いてしまい、動いた量に跳ね返って本文がぶれるため。
    // ドラッグ中の状態は文書(iframe)ごとではなく1つだけ持つ。マウスは章のiframeの境目を
    // またいで動かすことがあり、離した(mouseup)のが押したのと別の文書に届いても慣性を付けるため。
    var drag = null;
    var noClickUntil = 0;   // マウスでスクロールした直後のclickは、項目を選んだことにしない

    function attach(target, win) {

        // 押した: 流れていれば止める。止めただけならtrue(範囲指定の解除などをさせない)
        // x, yは画面の座標、cx, cyはその文書の中の座標(範囲指定の取っ手との距離用)
        function down(x, y, cx, cy) {
            var moving = raf !== 0;
            stop();
            drag = {
                x0: x,
                y0: y,
                p: horiz ? x : y,
                t0: performance.now(),
                mode: nearSelection(win, cx, cy) ? 'native' : null,
                samples: []
            };
            pos = getPos();
            return moving;
        }

        // 動いた: スクロールとして扱ったらtrue
        function move(x, y) {
            if (!drag) {
                return false;
            }
            var now = performance.now();
            if (drag.mode === null) {
                var dx = Math.abs(x - drag.x0), dy = Math.abs(y - drag.y0);
                if (dx < 8 && dy < 8) {
                    return false;
                }
                // 長押ししてから動かしたのは範囲指定とみなし、ブラウザに任せる
                drag.mode = now - drag.t0 < 400 && (horiz ? dx >= dy : dy >= dx) ? 'scroll' : 'native';
            }
            if (drag.mode !== 'scroll') {
                return false;
            }
            // 指(マウス)の動きに中身が付いてくる向き
            var p = horiz ? x : y;
            pos -= p - drag.p;
            drag.p = p;
            setPos(pos);
            pos = getPos();
            if (!drag.samples.length || drag.samples[drag.samples.length - 1].p !== p) {
                drag.samples.push({ p: p, t: now });
            }
            while (drag.samples.length > 2 && now - drag.samples[0].t > 100) {
                drag.samples.shift();
            }
            return true;
        }

        // 離した: 離す直前の速さで慣性を付ける。止めてから離した(limit ms以上動かさなかった)時は付けない。
        // マウスは動かしながら離すのが難しいので、limitを長めにし、速さは止める前の動きから測る
        // (samplesは動いた時にしか積まないので、止めていた間は速さに入らない)。
        function up(limit) {
            var d = drag;
            drag = null;
            if (!d || d.mode !== 'scroll') {
                return;
            }
            noClickUntil = performance.now() + 300;
            var s = d.samples;
            var gap = s.length ? performance.now() - s[s.length - 1].t : -1;
            var a = s[0], b = s[s.length - 1];
            var v = s.length >= 2 && b.t > a.t ? -(b.p - a.p) / (b.t - a.t) : 0;
            // 調べる用: URLに&debugを付けると、離した時に測った値を出す
            if (DEBUG) {
                opt.toast('見本' + s.length + ' 止めて' + Math.round(gap) + 'ms 速さ' + v.toFixed(3) + 'px/ms');
            }
            if (s.length < 2 || gap > limit || v === 0) {
                return;
            }
            vel = v;
            start();
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

        target.addEventListener('mousedown', function (e) {
            if (e.button !== 0) {
                return;
            }
            if (down(e.screenX, e.screenY, e.clientX, e.clientY)) {
                e.preventDefault();
            }
        });
        target.addEventListener('mousemove', function (e) {
            // ボタンが離れているのに来たmousemoveは、離したのと同じに扱う(mouseupより先に
            // ボタン無しのmousemoveが来ても、慣性を付けずに終わらせないように)
            if (!(e.buttons & 1)) {
                up(300);
                return;
            }
            if (move(e.screenX, e.screenY)) {
                e.preventDefault();
                // 動かし始めの8pxで始まった範囲指定を消す
                win.getSelection().removeAllRanges();
            }
        });
        target.addEventListener('mouseup', function () { up(300); });
        target.addEventListener('click', function (e) {
            if (performance.now() < noClickUntil) {
                e.preventDefault();
                e.stopPropagation();
            }
        }, true);
        // 画像や範囲指定した文字をつかんだ時の、ブラウザのドラッグ&ドロップを始めさせない
        // (始まるとmousemove/mouseupが来なくなり、スクロールも慣性も止まる)
        target.addEventListener('dragstart', function (e) {
            if (drag && drag.mode !== 'native') {
                e.preventDefault();
            }
        });

        // ホイール: 横のスクロールでは、縦の回転も横にする(下へ回すと先へ)。
        // 縦のスクロールはブラウザに任せ、流れていれば止めるだけ。
        target.addEventListener('wheel', function (e) {
            stop();
            if (!horiz || Math.abs(e.deltaY) <= Math.abs(e.deltaX)) {
                return;
            }
            e.preventDefault();
            var unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? pageSize() : 1;
            setPos(getPos() + e.deltaY * unit * forwardSign());
        }, { passive: false });

        win.addEventListener('keydown', function (e) {
            if (e.metaKey || e.ctrlKey || e.altKey || /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)
                || (opt.active && !opt.active())) {
                return;
            }
            // 調べる用: URLに&debugを付けると、押したキーと慣性の状態を出す
            if (DEBUG && /^Arrow/.test(e.key)) {
                opt.toast(e.key + (e.shiftKey ? '+Shift' : '') + ' 流れ' + (raf ? '中' : '無') +
                    ' 段' + coastLevel + ' 移動' + (glideTo === null ? '無' : '中') + ' 速さ' + vel.toFixed(3));
            }
            // 交差する向きの矢印(開始と調整)と、スクロールの向きの矢印(流れている間の調整と、
            // Shiftで画面1枚分)。それぞれ先へ向かう方を前に。
            var cross = horiz ? ['ArrowDown', 'ArrowUp'] : ['ArrowRight', 'ArrowLeft'];
            var along = horiz ? (forwardSign() < 0 ? ['ArrowLeft', 'ArrowRight'] : ['ArrowRight', 'ArrowLeft'])
                : ['ArrowDown', 'ArrowUp'];
            var d;
            if (e.shiftKey && along.indexOf(e.key) >= 0) {
                d = pageSize() * 0.9 * forwardSign() * (e.key === along[0] ? 1 : -1);
            } else if (cross.indexOf(e.key) >= 0) {
                e.preventDefault();
                stepCoast(e.key === cross[0] ? 1 : -1);
                return;
            } else if (along.indexOf(e.key) >= 0) {
                // スクロールの向きの矢印は流れている間だけ(止まっている時は何もしない)
                e.preventDefault();
                if (raf && glideTo === null) {
                    stepCoast(e.key === along[0] ? 1 : -1);
                }
                return;
            } else if (e.key === ' ') {
                d = pageSize() * 0.9 * forwardSign() * (e.shiftKey ? -1 : 1);
            } else if (e.key === 'Escape') {
                stop();
                return;
            } else {
                return;
            }
            e.preventDefault();
            glideBy(d);
        });
    }
    attach(scroller, window);

    return {
        attach: attach,
        stop: stop,
        moving: function () { return raf !== 0; }
    };
};
