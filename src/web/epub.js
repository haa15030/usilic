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

    // 画面の高さに合わせ、中身の幅だけ横に広げる
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
            book.scrollLeft += rtl ? fr.right - br.right : fr.left - br.left;
            return;
        }
        var r = doc.createRange();
        r.setStart(pt.node, pt.offset);
        r.collapse(true);
        var rects = r.getClientRects();
        var cr = rects.length ? rects[0] : (pt.node.parentNode.getBoundingClientRect
            ? pt.node.parentNode.getBoundingClientRect() : null);
        if (cr) {
            // 縦書きはその文字の列を画面の右端へ、横書きはその文字の段を画面に収める
            if (ch.vertical) {
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
        forward: function () { return rtl ? -1 : 1; },
        describe: function (m) {
            var i = chapters.findIndex(function (ch) { return ch.path === m.href; });
            return (i < 0 ? '?' : i + 1) + '/' + chapters.length + '章 ' + m.off + '字目';
        },
        topPos: function () {
            var br = book.getBoundingClientRect();
            return posAt(rtl ? br.right - 12 : br.left + 12, br.top + br.height * 0.1);
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
