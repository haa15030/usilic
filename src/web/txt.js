// .txtの画面: 原稿の1行が1つの<p data-line>。記録の位置は行・桁(Emacsと同じく文字単位、1始まり)。
(function () {
    'use strict';

    var text = document.getElementById('text');
    var paras = Array.prototype.slice.call(text.querySelectorAll('p[data-line]'));

    // 画面上の横位置xにある段落の番号。縦書きなので段落は右から左へ並び、
    // 番号が増えるほど左にある。
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

    function paraOf(node) {
        var el = node.nodeType === 1 ? node : node.parentNode;
        var p = el && el.closest ? el.closest('p[data-line]') : null;
        return p && text.contains(p) ? p : null;
    }

    // 段落の先頭からnode/offsetまでの文字数+1
    function colOf(p, node, offset) {
        var r = document.createRange();
        r.setStart(p, 0);
        r.setEnd(node, offset);
        return Array.from(r.toString()).length + 1;
    }

    // 段落の中の桁colの位置(テキストノードとその中のオフセット)
    function pointAt(p, col) {
        var walker = document.createTreeWalker(p, NodeFilter.SHOW_TEXT);
        var left = col - 1, node;
        while ((node = walker.nextNode())) {
            var chars = Array.from(node.data);
            if (left <= chars.length) {
                return { node: node, offset: chars.slice(0, left).join('').length };
            }
            left -= chars.length;
        }
        return null;
    }

    function posAt(x, y) {
        var p = paras[paraAtX(x)];
        if (!p) {
            return null;
        }
        var r = document.caretRangeFromPoint ? document.caretRangeFromPoint(x, y) : null;
        var col = r && paraOf(r.startContainer) === p ? colOf(p, r.startContainer, r.startOffset) : 1;
        return { line: +p.dataset.line, col: col };
    }

    function innerRect() {
        var rc = text.getBoundingClientRect();
        var cs = getComputedStyle(text);
        return {
            left: rc.left + parseFloat(cs.paddingLeft),
            right: rc.right - parseFloat(cs.paddingRight),
            top: rc.top + parseFloat(cs.paddingTop),
            bottom: rc.bottom - parseFloat(cs.paddingBottom)
        };
    }

    var reader = Reader({
        scroller: text,
        markTarget: function () {
            var sel = getSelection();
            if (sel.rangeCount && !sel.isCollapsed) {
                var r = sel.getRangeAt(0);
                var p = paraOf(r.startContainer);
                if (p) {
                    return {
                        pos: { line: +p.dataset.line, col: colOf(p, r.startContainer, r.startOffset) },
                        text: sel.toString()
                    };
                }
            }
            // 範囲指定が無ければ画面中央の位置
            var rc = innerRect();
            var pos = posAt((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
            return pos ? { pos: pos, text: '' } : null;
        },
        jump: function (m, select) {
            var p = paras[Math.min(Math.max(m.line || 1, 1), paras.length) - 1];
            if (!p) {
                return;
            }
            var pt = pointAt(p, m.col || 1);
            if (!pt) {
                p.scrollIntoView({ block: 'start' });
                return;
            }
            var r = document.createRange();
            r.setStart(pt.node, pt.offset);
            r.collapse(true);
            // 長い段落は何列にも折り返すので、段落ではなくその文字の列を画面の右寄りに置く
            var rects = r.getClientRects();
            var cr = rects.length ? rects[0] : p.getBoundingClientRect();
            var rc = innerRect();
            text.scrollLeft += cr.right - rc.right;
            if (select && m.text && m.text !== '(位置のみ)') {
                var end = pointAt(p, (m.col || 1) + Array.from(m.text).length);
                if (end) {
                    r.setEnd(end.node, end.offset);
                    var sel = getSelection();
                    sel.removeAllRanges();
                    sel.addRange(r);
                }
            }
        },
        describe: function (m) {
            return m.line + '行' + m.col + '桁';
        },
        topPos: function () {
            var rc = innerRect();
            return posAt(rc.right - 2, rc.top + 2);
        },
        font: {
            key: 'fs', def: 22, min: 12, max: 48, step: 2,
            apply: function (size) { text.style.setProperty('--fs', size + 'px'); }
        }
    });
    reader.restore();
})();
