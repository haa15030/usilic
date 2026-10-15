// EPUBの画面: spineの各章を、本のCSSのままiframeに描き、横に並べる(右綴じなら右から)。
// 記録の位置は章のパス(EPUBの中のパス)と、章の本文の先頭からの文字数(1始まり、ルビの読みも数える)。
(function () {
    'use strict';

    var book = document.getElementById('book');
    var base = book.dataset.base;
    var XHTML = 'http://www.w3.org/1999/xhtml';
    // {path, box, frame, vertical}。boxは章のiframeを包むdiv(画面の高さ)。横書きの章は、usilicと同じく上下に
    // 流す(2026-10-15、直接のユーザー依頼"奥付は縦方向、縦書きに切り替わると横方向、が理想"): iframeを中身の
    // 高さにし、boxを画面の幅にして、boxを縦にスクロールする(scroll.jsのInertiaのinner・enter)。
    var chapters = [];
    var rtl = true;
    var fontPct = 100;

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

    // 縦書きの章は画面の高さに合わせ、中身の幅だけ横に広げる。横書きの章は画面の幅にし、中身の高さだけ
    // 縦に伸ばす(boxが画面の高さで上下にスクロールする)。
    function measure(ch) {
        var doc = docOf(ch);
        if (!doc || !doc.documentElement) {
            return;
        }
        var h = book.clientHeight, w = book.clientWidth;
        var root = doc.documentElement;
        ch.frame.style.height = h + 'px';
        ch.frame.style.width = w + 'px';
        ch.box.style.height = h + 'px';
        root.style.fontSize = fontPct + '%';
        var wm = getComputedStyle(doc.body || root).writingMode || '';
        ch.vertical = wm.indexOf('vertical') === 0;
        if (ch.vertical) {
            ch.frame.style.width = Math.max(root.scrollWidth, 1) + 'px';
            ch.box.style.width = ch.frame.style.width;
            ch.box.scrollTop = 0;
        } else {
            ch.box.style.width = w + 'px';
            ch.frame.style.height = Math.max(root.scrollHeight, (doc.body || root).scrollHeight, h) + 'px';
        }
    }

    // 横書きの章で、boxが画面にぴったり収まっている(左右のずれが2px以内)もの
    function alignedInner() {
        var br = book.getBoundingClientRect();
        for (var i = 0; i < chapters.length; i++) {
            var ch = chapters[i];
            if (ch.vertical) {
                continue;
            }
            var r = ch.box.getBoundingClientRect();
            if (Math.abs(r.left - br.left) <= 2 && r.width > 0) {
                return ch.box;
            }
        }
        return null;
    }

    // 箱boxの章の先(fwd=1)か前(-1)の隣の章も横書きなら、その箱と、それが画面にぴったりになるscrollLeft
    function nextInner(box, fwd) {
        var i = chapters.findIndex(function (ch) { return ch.box === box; });
        var nb = i < 0 ? null : chapters[i + (fwd > 0 ? 1 : -1)];
        if (!nb || nb.vertical) {
            return null;
        }
        return { el: nb.box, left: book.scrollLeft + nb.box.getBoundingClientRect().left - book.getBoundingClientRect().left };
    }

    // 横に流れて、前の位置prevから今の位置nowまでの間に、横書きの章のboxがぴったりになる位置を越えたか
    function enterInner(prev, now) {
        if (prev === now) {
            return null;
        }
        var br = book.getBoundingClientRect();
        for (var i = 0; i < chapters.length; i++) {
            var ch = chapters[i];
            if (ch.vertical) {
                continue;
            }
            var left = now + ch.box.getBoundingClientRect().left - br.left;  // boxがぴったりになるscrollLeft
            if ((prev - left) * (now - left) < 0 || (now === left && prev !== left)) {
                return { el: ch.box, left: left };
            }
        }
        return null;
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
        reader.attach(doc, ch.frame.contentWindow, function () { return ch.vertical ? null : ch.box; });
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
            var bx = ch.box.getBoundingClientRect();
            book.scrollLeft += rtl ? bx.right - br.right : bx.left - br.left;
            return;
        }
        var r = doc.createRange();
        r.setStart(pt.node, pt.offset);
        r.collapse(true);
        var rects = r.getClientRects();
        var cr = rects.length ? rects[0] : (pt.node.parentNode.getBoundingClientRect
            ? pt.node.parentNode.getBoundingClientRect() : null);
        if (cr) {
            // 縦書きはその文字の列を画面の右端へ、横書きはその章のboxを画面にそろえ、その文字の行を上の方へ
            if (ch.vertical) {
                book.scrollLeft += fr.left + cr.right - (br.right - 24);
            } else {
                book.scrollLeft += ch.box.getBoundingClientRect().left - br.left;
                ch.box.scrollTop = Math.max(0, cr.top - br.height * 0.1);
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
        forward: function () { return rtl ? -1 : 1; },
        inner: alignedInner,
        enter: enterInner,
        next: nextInner,
        describe: function (m) {
            var i = chapters.findIndex(function (ch) { return ch.path === m.href; });
            return (i < 0 ? '?' : i + 1) + '/' + chapters.length + '章 ' + m.off + '字目';
        },
        topPos: function () {
            var br = book.getBoundingClientRect();
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
            var doc = ch && ch.vertical ? docOf(ch) : null;
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
                var box = document.createElement('div');
                var ch = { path: path, box: box, frame: frame, vertical: true };
                chapters.push(ch);
                box.className = 'chapter-box';
                box.addEventListener('scroll', function () { reader.savePos(); });
                box.appendChild(frame);
                frame.className = 'chapter';
                frame.setAttribute('scrolling', 'no');
                frame.addEventListener('load', function () {
                    setup(ch);
                    resolveLoad();
                }, { once: true });
                frame.src = base + encodePath(path);
                book.appendChild(box);
            });
        }));
    }).then(function () {
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
            var keep = posAt(rtl ? br.right - 12 : br.left + 12, br.top + br.height * 0.1);
            measureAll();
            if (keep) {
                jump(keep, false);
            }
        }, 300);
    });
})();
