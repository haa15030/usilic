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
//       forward:    function () → 先へ進む時のスクロール位置の増える向き(縦書き・右綴じは-1)
//                   (省略時は-1)
//       axis:       function () → 'x'(横に流す、省略時)か'y'(縦に流す。EPUBで最初の章が横書きの本、epub.js参照)
//   });
//   r.attach(doc, win)  その文書(iframeの中など)のtouch等も慣性スクロールへ回す
//   r.restore()     覚えていた位置へ戻る(本文を並べ終えた後に呼ぶ)
var Reader = function (opt) {
    'use strict';

    var scroller = opt.scroller;
    var file = scroller.dataset.file;
    var whereEl = document.getElementById('where');
    var toastEl = document.getElementById('toast');
    // スクロールの位置(横に流す時はscrollLeft、縦ならscrollTop)
    function vertical() {
        return opt.axis && opt.axis() === 'y';
    }
    function scrollPos() {
        return vertical() ? scroller.scrollTop : scroller.scrollLeft;
    }
    function setScrollPos(v) {
        if (vertical()) {
            scroller.scrollTop = v;
        } else {
            scroller.scrollLeft = v;
        }
    }
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
        return fontAnchor && Math.abs(scrollPos() - fontAnchorScroll) <= 1;
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

    // 2026-10-15、直接のユーザー依頼("減速ボタンは廃棄でよろしく"、"減り方は常時０でよろしく"): 「減速 大/中/小」の
    // ボタンは無くし、フリックした後は減速せず、離した時の速さのまま流れ続ける(タップか端で止まる)。
    // 2026-10-27、直接のユーザー依頼("スクロールの減衰も必要かなと。「V+」の右隣に減衰有り無しトグルボタン「減衰」を
    // 置いてみて。OFFの時は薄地で表示"): 「減衰」をONにすると、以前の「減速 中」と同じ割合で減速する。
    var decay = load('decay', '0') === '1';
    var decayBtn = document.getElementById('friction');
    function showDecay() {
        if (decayBtn) decayBtn.classList.toggle('off', !decay);
    }
    showDecay();

    var inertia = Inertia(scroller, {
        axis: 'x',
        forward: function () { return opt.forward ? opt.forward() : -1; },
        friction: function () { return decay ? 0.991 : 1; },  // 2026-10-27、直接のユーザー依頼"減衰は今の3倍くらいでいい"(0.997から)
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
    // 2026-10-15、直接のユーザー報告("iOSではSafariでもChromeでも拡大時画面が空白になった。スクロールすれば文字は
    // 戻る…Xiaomiでは起きない"): iOSのWebKitは、章の枠(iframe)の大きさと中の字の大きさを変えた後、スクロール
    // するまで描き直さないことがある。次のフレームで1px動かし、その次のフレームで元へ戻して描き直させる。
    var repaintFrame = 0;
    function repaint() {
        if (repaintFrame) {
            return;
        }
        repaintFrame = requestAnimationFrame(function () {
            var x = scrollPos();
            setScrollPos(x - 1);
            if (scrollPos() === x) {
                setScrollPos(x + 1);
            }
            requestAnimationFrame(function () {
                repaintFrame = 0;
                setScrollPos(x);
                if (fontAnchor) {
                    fontAnchorScroll = scrollPos();
                }
            });
        });
    }

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
            fontAnchorScroll = scrollPos();
        }
        repaint();
    }

    onTap(document.getElementById('mark'), record);
    onTap(document.getElementById('list'), openPanel);
    document.getElementById('panel-close').addEventListener('click', function () { panel.hidden = true; });
    // V−/V＋: 矢印キー(↑/↓)と同じく1段ずつ。止まっていればV＋で先へ、V−で戻るを最低速で流し始める。
    onTap(document.getElementById('slower'), function () { inertia.coast(-1); });
    onTap(document.getElementById('faster'), function () { inertia.coast(1); });
    if (decayBtn) {
        onTap(decayBtn, function () {
            decay = !decay;
            save('decay', decay ? '1' : '0');
            showDecay();
            toast(decay ? '減衰 ON' : '減衰 OFF');
        });
    }
    // ＜/＞: 画面1枚分、見た目の向きのまま(縦書きは＜で先へ)
    onTap(document.getElementById('page-left'), function () { inertia.page(-1); });
    onTap(document.getElementById('page-right'), function () { inertia.page(1); });
    onTap(document.getElementById('smaller'), function () { setFont(fontSize - font.step); });
    onTap(document.getElementById('larger'), function () { setFont(fontSize + font.step); });

    // 2026-10-15、直接のユーザー依頼("ピンチで文字拡大縮小ができるのは反応今イチのKinnopyだけなので（Kindle、
    // iBooksいずれも不能）助かる。よろしく"): 2本指の間隔の比で文字の大きさを変える。ブラウザ自身の拡大は止める。
    // 同日、直接のユーザー依頼("Kinoppyのようにリアルタイムの拡大／縮小はできる？指の開き/閉じに連動してフォント
    // サイズが変化し指を離すと確定"): 指を動かしている間も、1フレームに1回まで(requestAnimationFrame)大きさを
    // 変えて見せる。全部の組み直しは重いので、その間はfont.preview(EPUBは読んでいる章だけ測り直す、無ければ
    // font.apply)で、指を離した時にsetFont()で確定する。刻みはA−/A＋の半分(細かく追うため)。位置はA−/A＋と
    // 同じく変える前の所へ戻す。(指を動かしている間の組み直しは、下の同日の依頼で画面のtransformに替えた。)
    // 同日、直接のユーザー依頼("ピンチが右端で起きるのは位置の記憶用かもしれないが出来れば中央がいい…中央確定で
    // いい"): 基準は画面の右端(A−/A＋のfontAnchor)ではなく画面中央の字(pinchAnchor)。縦書きはその字を画面上の
    // 同じ横位置(pinchX)に留め、横書きの段組み(opt.charXがnull)はその字の段へjump()する。
    var pinchStart = null;  // {dist, size}
    var pinchAnchor = null, pinchX = 0;
    function keepPinchCenter() {
        if (!pinchAnchor) {
            return;
        }
        var x = opt.charX ? opt.charX(pinchAnchor) : null;
        if (x === null) {
            opt.jump(pinchAnchor, false);
        } else {
            scroller.scrollLeft += x - pinchX;
        }
    }
    // 同日、直接のユーザー依頼("Kinoppy仕様では画面全体が画像のように中央へ収縮・中央から拡大したのち指を離した後
    // タップで全体に広がる。タップは要らないので指を離した時点でそうなる"): 指を動かしている間は組み直さず、
    // 画面(scroller)を画像のように中央を中心にCSSのtransformで拡大・縮小して見せる。指を離した時に字の大きさを
    // 変えて組み直し、中央の字を保つ。
    var pinchSize = 0, pinchScale = 1, pinchFrame = 0;
    function touchDistance(e) {
        var a = e.touches[0], b = e.touches[1];
        return Math.hypot(a.screenX - b.screenX, a.screenY - b.screenY);
    }
    function showPinch() {
        pinchFrame = 0;
        if (pinchStart) {
            scroller.style.transform = 'scale(' + pinchScale + ')';
        }
    }
    function pinch(target) {
        target.addEventListener('touchstart', function (e) {
            if (e.touches.length === 2) {
                inertia.stop();
                pinchAnchor = opt.centerPos ? opt.centerPos() : opt.topPos();
                var x = pinchAnchor && opt.charX ? opt.charX(pinchAnchor) : null;
                pinchX = x === null ? 0 : x;
                pinchStart = { dist: Math.max(touchDistance(e), 1), size: fontSize };
                pinchSize = fontSize;
                pinchScale = 1;
                scroller.style.transformOrigin = '50% 50%';
                e.preventDefault();
            }
        }, { passive: false });
        target.addEventListener('touchmove', function (e) {
            if (!pinchStart || e.touches.length !== 2) {
                return;
            }
            e.preventDefault();
            var want = Math.min(font.max, Math.max(font.min, pinchStart.size * touchDistance(e) / pinchStart.dist));
            pinchScale = want / pinchStart.size;
            var unit = font.step / 2;
            var size = Math.min(font.max, Math.max(font.min, Math.round(want / unit) * unit));
            if (size !== pinchSize) {
                pinchSize = size;
                toast('文字の大きさ ' + size + (font.unit || (font.key === 'efs' ? '%' : 'px')));
            }
            if (!pinchFrame) {
                pinchFrame = requestAnimationFrame(showPinch);
            }
        }, { passive: false });
        function end(e) {
            if (!pinchStart || e.touches.length >= 2) {
                return;
            }
            var size = pinchSize;
            pinchStart = null;
            if (pinchFrame) {
                cancelAnimationFrame(pinchFrame);
                pinchFrame = 0;
            }
            scroller.style.transform = '';
            if (size !== fontSize) {
                fontSize = size;
                save(font.key, fontSize);
                font.apply(fontSize);
                keepPinchCenter();
            }
            repaint();
            pinchAnchor = null;
            fontAnchor = null;  // A−/A＋の基準は、次に押した時の画面から取り直す
        }
        target.addEventListener('touchend', end);
        target.addEventListener('touchcancel', end);
        // iOSのSafariは、viewportのuser-scalable=noがあってもピンチで拡大するので止める
        target.addEventListener('gesturestart', function (e) { e.preventDefault(); });
    }
    pinch(document);

    // ---- スクロールバー ----

    // 2026-10-26、直接のユーザー依頼("Kat.epub等の大きめファイルでは移動が大変なのでスクロールバーを作って欲しい。
    // 移動できさえすれば見た目はどうでもいい"): 操作ボタンの帯のすぐ上に、本の全体での位置のスライダーを置く。
    // 動かすとその位置へ飛び、スクロールすると追いかける。縦書き(先へ進むとscrollLeftが減る)は右から左へ。
    var bar = document.createElement('input');
    bar.type = 'range';
    bar.id = 'seek';
    bar.min = 0;
    bar.max = 1000;
    bar.step = 1;
    bar.value = 0;
    document.body.appendChild(bar);
    var seeking = false;
    function scrollMax() {
        return vertical() ? scroller.scrollHeight - scroller.clientHeight : scroller.scrollWidth - scroller.clientWidth;
    }
    function forwardSign() {
        if (vertical()) return 1;
        return opt.forward ? opt.forward() : -1;
    }
    function syncSeek() {
        if (seeking) return;
        var max = scrollMax();
        bar.style.direction = forwardSign() < 0 ? 'rtl' : 'ltr';
        bar.value = max > 0 ? Math.round(Math.abs(scrollPos()) / max * 1000) : 0;
    }
    bar.addEventListener('input', function () {
        seeking = true;
        inertia.stop();
        setScrollPos(forwardSign() * scrollMax() * bar.value / 1000);
    });
    bar.addEventListener('change', function () { seeking = false; syncSeek(); });
    ['pointerup', 'touchend'].forEach(function (t) {
        bar.addEventListener(t, function () { seeking = false; });
    });
    scroller.addEventListener('scroll', syncSeek);
    // 2026-10-27、直接のユーザー依頼("スクロールバーは、慣性スクロールが停止しているときだけ表示"): 慣性で流れている間は隠す。
    setInterval(function () {
        bar.style.visibility = inertia.moving() && !seeking ? 'hidden' : '';
    }, 150);
    window.addEventListener('resize', syncSeek);
    setTimeout(syncSeek, 500);

    font.apply(fontSize);

    return {
        attach: function (doc, win) {
            inertia.attach(doc, win);
            pinch(doc);
        },
        restore: restore,
        setAxis: function (axis) { inertia.setAxis(axis); },
        toast: toast,
        fontSize: function () { return fontSize; }
    };
};
