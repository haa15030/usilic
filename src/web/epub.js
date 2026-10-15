// EPUBの画面: spineの各章を、本のCSSのままiframeに描き、横に並べる(右綴じなら右から)。
// 記録の位置は章のパス(EPUBの中のパス)と、章の本文の先頭からの文字数(1始まり、ルビの読みも数える)。
(function () {
    'use strict';

    var book = document.getElementById('book');
    var base = book.dataset.base;
    var XHTML = 'http://www.w3.org/1999/xhtml';
    var chapters = [];   // {path, frame, vertical}
    var rtl = true;
    var fontPct = 100;
    // 2026-10-15、直接のユーザー判断("連続スクロールのためには途中で縦横切り替わりを未実装にして、最初の章が横なら
    // 横と決め打ちスクロールしかないかもね"): 本ごとに流す向きを決める。最初の章が縦書きなら'x'(章を横に並べて
    // 左右に流す、横書きの章は段組みで横へ)、横書きなら'y'(全部の章を縦に並べて上下に続けて流す、HELP.epub等)。
    // 章のiframeを並べる向きはCSS(#book.flow-y)で変えるだけ(iframeを別の要素へ移すと読み込み直しになるため)。
    var axis = 'x';

    function encodePath(path) {
        return path.split('/').map(encodeURIComponent).join('/');
    }

    // EPUBの中のパスrefを、パスfromのファイルから見た相対パスとして解決する
    function resolve(from, ref) {
        var u = new URL(ref, 'http://epub/' + encodePath(from));
        return decodeURIComponent(u.pathname.slice(1));
    }

    function fetchXml(path) {
        return fetch(base + encodePath(path), { credentials: 'same-origin' })
            .then(function (res) {
                if (!res.ok) {
                    throw new Error(path + 'がありません');
                }
                return res.text();
            })
            .then(function (s) { return new DOMParser().parseFromString(s, 'application/xml'); });
    }

    function byTag(doc, tag) {
        return Array.prototype.slice.call(doc.getElementsByTagNameNS('*', tag));
    }

    // container.xml → OPF → spineの順に、章のパスの並びを得る
    function loadSpine() {
        return fetchXml('META-INF/container.xml').then(function (c) {
            var rootfile = byTag(c, 'rootfile')[0];
            if (!rootfile) {
                throw new Error('container.xmlにrootfileがありません');
            }
            var opfPath = rootfile.getAttribute('full-path');
            return fetchXml(opfPath).then(function (opf) {
                var items = {};
                byTag(opf, 'item').forEach(function (it) {
                    items[it.getAttribute('id')] = resolve(opfPath, it.getAttribute('href'));
                });
                var spine = byTag(opf, 'spine')[0];
                rtl = !spine || spine.getAttribute('page-progression-direction') !== 'ltr';
                return byTag(opf, 'itemref').map(function (ref) {
                    return items[ref.getAttribute('idref')];
                }).filter(Boolean);
            });
        });
    }

    // ---- 章の枠 ----

    function docOf(ch) {
        try {
            return ch.frame.contentDocument;
        } catch (e) {
            return null;
        }
    }

    // 横に流す本('x')は、画面の高さに合わせ、中身の幅だけ横に広げる。縦に流す本('y')は、画面の幅に合わせ、
    // 中身の高さだけ縦に伸ばす(縦書きの章が混じっていれば、その章は画面1枚分に切れる)。
    function measure(ch) {
        var doc = docOf(ch);
        if (!doc || !doc.documentElement) {
            return;
        }
        var h = book.clientHeight, w = book.clientWidth;
        var root = doc.documentElement;
        ch.frame.style.height = h + 'px';
        ch.frame.style.width = w + 'px';
        root.style.fontSize = fontPct + '%';
        var wm = getComputedStyle(doc.body || root).writingMode || '';
        ch.vertical = wm.indexOf('vertical') === 0;
        if (axis === 'y') {
            // 横に流す本として測った時(読み込んだ直後)の段組みを外す
            root.style.height = '';
            root.style.columnWidth = '';
            root.style.columnGap = '';
            root.style.columnFill = '';
            root.style.direction = '';
            if (doc.body) {
                doc.body.style.direction = '';
            }
            // 2026-10-15、直接のユーザー報告("箱の空行領域は切り詰められない？"): scrollHeightは枠(iframe)の高さより
            // 小さくならず、短い章も画面1枚分になっていた。章の末尾の空行(空の段落等)も含んでいた。最後の字か画像の
            // 下端までにし、次の章との間を少し空ける。
            ch.frame.style.height = (ch.vertical ? h : contentBottom(doc) + 24) + 'px';
            return;
        }
        if (!ch.vertical) {
            // 横書きの章は段組みで横へ流す。右綴じの本では段も右から並べる
            // (段の並びは段組みの要素のdirectionで決まるので、本文の向きはbodyで元に戻す)。
            var gap = 48;
            root.style.height = h + 'px';
            root.style.columnWidth = (w - gap * 2) + 'px';
            root.style.columnGap = gap + 'px';
            root.style.columnFill = 'auto';
            root.style.direction = rtl ? 'rtl' : '';
            if (doc.body && rtl) {
                doc.body.style.direction = 'ltr';
            }
        }
        ch.frame.style.width = Math.max(root.scrollWidth, 1) + 'px';
    }

    // 文書の、空白でない字・画像の下端(文書の先頭から、px)
    function contentBottom(doc) {
        var body = doc.body || doc.documentElement;
        var bottom = 0;
        var walker = doc.createTreeWalker(body, NodeFilter.SHOW_TEXT);
        var range = doc.createRange();
        var node;
        while ((node = walker.nextNode())) {
            if (!/\S/.test(node.data)) {
                continue;
            }
            range.selectNodeContents(node);
            bottom = Math.max(bottom, range.getBoundingClientRect().bottom);
        }
        Array.prototype.forEach.call(body.querySelectorAll('img, svg, video, table, hr'), function (el) {
            bottom = Math.max(bottom, el.getBoundingClientRect().bottom);
        });
        var win = doc.defaultView;
        return Math.ceil(bottom + (win ? win.scrollY || 0 : 0));
    }

    function measureAll() {
        chapters.forEach(measure);
    }

    function setup(ch) {
        var doc = docOf(ch);
        if (!doc || !doc.documentElement) {
            return;
        }
        // 章の中はスクロールさせず、touchは外の慣性スクロールへ回す
        var style = doc.createElementNS(XHTML, 'style');
        style.textContent = 'html{overflow:hidden!important;touch-action:none;-webkit-text-size-adjust:100%}';
        (doc.head || doc.documentElement).appendChild(style);
        reader.attach(doc, ch.frame.contentWindow);
        measure(ch);
        if (ch.vertical) {
            sidelineRight(doc);
        }
    }

    // 縦書きの下線(リンクの既定の下線等)は左側に引かれるが、日本語の傍線は右側が主流
    // (Kindle・Kinoppy・ブック。usilicも右側)。日本語を含む要素の下線は、縦書きで右側に
    // 引かれる上線に替える。英字だけの要素(横倒しの英文)は下線のまま左側に残す。
    function sidelineRight(doc) {
        var win = doc.defaultView;
        Array.prototype.forEach.call((doc.body || doc.documentElement).querySelectorAll('*'), function (el) {
            var line = win.getComputedStyle(el).textDecorationLine || '';
            if (line.indexOf('underline') < 0 || !/[\u3000-\u30ff\u3400-\u9fff\uf900-\ufaff\uff00-\uffef]/.test(el.textContent)) {
                return;
            }
            el.style.textDecorationLine = line.replace('underline', line.indexOf('overline') < 0 ? 'overline' : '').trim();
        });
    }

    // ---- 位置 ----

    function chapterOf(path) {
        for (var i = 0; i < chapters.length; i++) {
            if (chapters[i].path === path) {
                return chapters[i];
            }
        }
        return null;
    }

    // 章の本文の先頭からnode/offsetまでの文字数+1
    function offsetOf(doc, node, offset) {
        var r = doc.createRange();
        r.setStart(doc.body || doc.documentElement, 0);
        r.setEnd(node, offset);
        return Array.from(r.toString()).length + 1;
    }

    // 文字数offの位置(テキストノードとその中のオフセット)
    function pointAt(doc, off) {
        var walker = doc.createTreeWalker(doc.body || doc.documentElement, NodeFilter.SHOW_TEXT);
        var left = off - 1, node, last = null;
        while ((node = walker.nextNode())) {
            var chars = Array.from(node.data);
            if (left <= chars.length) {
                return { node: node, offset: chars.slice(0, left).join('').length };
            }
            left -= chars.length;
            last = node;
        }
        return last ? { node: last, offset: last.data.length } : null;
    }

    // 画面上の点(x, y)にある章と文字の位置
    function posAt(x, y) {
        var el = document.elementFromPoint(x, y);
        var ch = null;
        for (var i = 0; i < chapters.length; i++) {
            if (chapters[i].frame === el) {
                ch = chapters[i];
            }
        }
        if (!ch) {
            return null;
        }
        var doc = docOf(ch);
        var fr = ch.frame.getBoundingClientRect();
        var r = doc && doc.caretRangeFromPoint ? doc.caretRangeFromPoint(x - fr.left, y - fr.top) : null;
        return { href: ch.path, off: r ? offsetOf(doc, r.startContainer, r.startOffset) : 1 };
    }

    function jump(m, select) {
        var ch = chapterOf(m.href);
        if (!ch) {
            return;
        }
        var doc = docOf(ch);
        var br = book.getBoundingClientRect();
        var fr = ch.frame.getBoundingClientRect();
        var pt = doc ? pointAt(doc, m.off || 1) : null;
        if (!pt) {
            if (axis === 'y') {
                book.scrollTop += fr.top - br.top;
            } else {
                book.scrollLeft += rtl ? fr.right - br.right : fr.left - br.left;
            }
            return;
        }
        var r = doc.createRange();
        r.setStart(pt.node, pt.offset);
        r.collapse(true);
        var rects = r.getClientRects();
        var cr = rects.length ? rects[0] : (pt.node.parentNode.getBoundingClientRect
            ? pt.node.parentNode.getBoundingClientRect() : null);
        if (cr) {
            // 縦に流す本はその文字の行を画面の上の方へ。横に流す本の縦書きはその文字の列を画面の右端へ、
            // 横書きはその文字の段を画面に収める
            if (axis === 'y') {
                book.scrollTop += fr.top + cr.top - br.top - br.height * 0.1;
            } else if (ch.vertical) {
                book.scrollLeft += fr.left + cr.right - (br.right - 24);
            } else {
                // 段の間隔はmeasure()の段組みと同じ(段の幅 = 画面の幅 - 2*48、間 = 48)
                var pitch = br.width - 48;
                var col = Math.floor(cr.left / pitch);
                book.scrollLeft += fr.left + col * pitch - br.left;
            }
        }
        if (select && m.text && m.text !== '(位置のみ)') {
            var end = pointAt(doc, (m.off || 1) + Array.from(m.text).length);
            if (end) {
                r.setEnd(end.node, end.offset);
                var sel = ch.frame.contentWindow.getSelection();
                sel.removeAllRanges();
                sel.addRange(r);
            }
        }
    }

    var reader = Reader({
        scroller: book,
        markTarget: function () {
            for (var i = 0; i < chapters.length; i++) {
                var win = chapters[i].frame.contentWindow;
                var sel = win && win.getSelection();
                if (sel && sel.rangeCount && !sel.isCollapsed) {
                    var r = sel.getRangeAt(0);
                    return {
                        pos: { href: chapters[i].path, off: offsetOf(docOf(chapters[i]), r.startContainer, r.startOffset) },
                        text: sel.toString()
                    };
                }
            }
            // 範囲指定が無ければ画面中央の位置
            var br = book.getBoundingClientRect();
            var p = posAt(br.left + br.width / 2, br.top + br.height / 2);
            return p ? { pos: p, text: '' } : null;
        },
        jump: jump,
        forward: function () { return axis === 'y' ? 1 : rtl ? -1 : 1; },
        axis: function () { return axis; },
        describe: function (m) {
            var i = chapters.findIndex(function (ch) { return ch.path === m.href; });
            return (i < 0 ? '?' : i + 1) + '/' + chapters.length + '章 ' + m.off + '字目';
        },
        topPos: function () {
            var br = book.getBoundingClientRect();
            if (axis === 'y') {
                return posAt(br.left + br.width / 2, br.top + 12);
            }
            return posAt(rtl ? br.right - 12 : br.left + 12, br.top + br.height * 0.1);
        },
        // ピンチの基準(reader.jsのpinch参照): 画面中央の字と、その字の画面上の横位置。横書きの章は段組みで
        // 1段ずつ送るので、横位置は返さない(nullならreader.jsはその字の段へjump()する)。
        centerPos: function () {
            var br = book.getBoundingClientRect();
            return posAt(br.left + br.width / 2, br.top + br.height / 2);
        },
        charX: function (m) {
            var ch = chapterOf(m.href);
            var doc = ch && ch.vertical && axis === 'x' ? docOf(ch) : null;
            var pt = doc ? pointAt(doc, m.off || 1) : null;
            if (!pt) {
                return null;
            }
            var r = doc.createRange();
            r.setStart(pt.node, pt.offset);
            r.collapse(true);
            var rects = r.getClientRects();
            if (!rects.length) {
                return null;
            }
            return ch.frame.getBoundingClientRect().left + (rects[0].left + rects[0].right) / 2;
        },
        font: {
            key: 'efs', def: 100, min: 60, max: 250, step: 10,
            apply: function (pct) {
                fontPct = pct;
                measureAll();
            }
        }
    });

    book.style.direction = rtl ? 'rtl' : 'ltr';
    loadSpine().then(function (paths) {
        book.style.direction = rtl ? 'rtl' : 'ltr';
        return Promise.all(paths.map(function (path) {
            return new Promise(function (resolveLoad) {
                var frame = document.createElement('iframe');
                var ch = { path: path, frame: frame, vertical: true };
                chapters.push(ch);
                frame.className = 'chapter';
                frame.setAttribute('scrolling', 'no');
                frame.addEventListener('load', function () {
                    setup(ch);
                    resolveLoad();
                }, { once: true });
                frame.src = base + encodePath(path);
                book.appendChild(frame);
            });
        }));
    }).then(function () {
        // 流す向きは最初の章で決める(axis参照)
        if (chapters.length && !chapters[0].vertical) {
            axis = 'y';
            book.classList.add('flow-y');
            book.style.direction = 'ltr';
            reader.setAxis('y');
        }
        measureAll();
        reader.restore();
    }).catch(function (e) {
        reader.toast('本を開けません: ' + e.message);
    });

    // 画面の向きが変わったら測り直し、読んでいた所へ戻る
    var resizeTimer = 0;
    window.addEventListener('resize', function () {
        clearTimeout(resizeTimer);
        resizeTimer = setTimeout(function () {
            var br = book.getBoundingClientRect();
            var keep = axis === 'y' ? posAt(br.left + br.width / 2, br.top + 12)
                : posAt(rtl ? br.right - 12 : br.left + 12, br.top + br.height * 0.1);
            measureAll();
            if (keep) {
                jump(keep, false);
            }
        }, 300);
    });
})();
