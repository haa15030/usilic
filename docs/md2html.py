#!/usr/bin/env python3
# Markdownを1つのHTMLファイルへ変換する(外部のライブラリは使わない)。
# 使い方: python3 md2html.py BUILD.md [BUILD.html]
#
# docs/BUILD.mdで使っている分だけの実装。次はusilicの手順書のための扱い:
#   ```key    キー入力のコマンドの枠(斜体)
#   ```screen 画面に出る文字の枠
#   段落の中の改行は、日本語の隣なら空白にせずにつなぐ
import html
import re
import sys
from pathlib import Path

CSS = """
:root {
  --bg: #ffffff; --fg: #1f2328; --muted: #59636e; --line: #d0d7de; --box: #f6f8fa;
  --item: #0097a7; --item-orange: #d9480f; --value: #b35900; --link: #0969da;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #16191d; --fg: #e6e8eb; --muted: #9aa4ae; --line: #3a4048; --box: #22262b;
    --item: #26c6da; --item-orange: #ff8a50; --value: #f0a35e; --link: #58a6ff;
  }
}
:root[data-theme="dark"] {
  --bg: #16191d; --fg: #e6e8eb; --muted: #9aa4ae; --line: #3a4048; --box: #22262b;
  --item: #26c6da; --item-orange: #ff8a50; --value: #f0a35e; --link: #58a6ff;
}
* { box-sizing: border-box; }
body {
  margin: 0 auto; max-width: 56rem; padding: 1.5rem 16px 4rem;
  background: var(--bg); color: var(--fg);
  font-family: -apple-system, "Hiragino Sans", "Noto Sans CJK JP", "IPAGothic", sans-serif;
  line-height: 1.7; overflow-wrap: anywhere;
}
a { color: var(--link); }
h1 { font-size: 1.6rem; line-height: 1.3; }
h2 { font-size: 1.3rem; margin-top: 3rem; padding-bottom: .25rem; border-bottom: 1px solid var(--line); }
h3 { font-size: 1.05rem; margin-top: 2rem; }
p { margin: .7rem 0; }
ul, ol { padding-left: 1.5rem; }
li { margin: .3rem 0; }
nav { padding: .5rem 1rem; background: var(--box); border: 1px solid var(--line); border-radius: 6px; }
nav ul { margin: .2rem 0; list-style: none; padding: 0; }
.item { color: var(--item); }
.item-orange { color: var(--item-orange); }
strong { white-space: pre-wrap; }
code, em, pre { font-family: ui-monospace, Menlo, Consolas, "DejaVu Sans Mono", "IPAGothic", monospace; }
code { color: var(--value); font-size: .92em; }
em { font-size: .95em; }
pre {
  margin: .7rem 0; padding: .55rem .8rem; overflow-x: auto; overflow-wrap: normal;
  background: var(--box); border: 1px solid var(--line); border-radius: 6px; line-height: 1.5;
}
pre code { color: inherit; font-size: inherit; }
pre.key { font-style: italic; }
p.note { color: var(--muted); font-size: .92em; }
.table { overflow-x: auto; }
table { border-collapse: collapse; overflow-wrap: normal; }
th, td { padding: .4rem .7rem; border: 1px solid var(--line); text-align: left; vertical-align: top; }
th { background: var(--box); }
"""

ESCAPED = re.compile(r"\\([\\`*_{}\[\]()<>#+.!$@|-])")
SPAN = re.compile(r'&lt;span style="color:#([0-9a-fA-F]{6})"&gt;(.*?)&lt;/span&gt;')
# 設定項目名の色: FreeBSDのインストーラの水色と、Ubuntu・Debianのインストーラのオレンジ(2026-11-17)。
SPAN_CLASS = {"e95420": "item-orange"}
BULLET = re.compile(r"( *)- (.*)")
NUMBER = re.compile(r"( *)(\d+)\. (.*)")
FENCE = re.compile(r"\s*```(\S*)\s*$")
WIDE = re.compile(r"[　-鿿＀-￯]")


def inline(text):
    """1行の中の書式をHTMLにする。"""
    kept = []

    def keep(piece):
        kept.append(piece)
        return chr(0xE000 + len(kept) - 1)

    text = re.sub(r"`` (.+?) ``|`([^`]+)`",
                  lambda m: keep("<code>" + html.escape(m.group(1) or m.group(2), quote=False) + "</code>"), text)
    text = ESCAPED.sub(lambda m: keep(html.escape(m.group(1), quote=False)), text)
    text = re.sub(r"\[([^\]]+)\]\((https?://[^)\s]+)\)",
                  lambda m: keep('<a href="%s">' % html.escape(m.group(2))) + m.group(1) + keep("</a>"), text)
    text = re.sub(r"(?<![\"'(])https?://[A-Za-z0-9._~:/?#\[\]@!$&'+,;=%-]+",
                  lambda m: keep('<a href="%s">%s</a>' % (html.escape(m.group(0)), html.escape(m.group(0)))), text)
    text = html.escape(text, quote=False)
    text = SPAN.sub(lambda m: '<span class="%s">%s</span>' % (SPAN_CLASS.get(m.group(1).lower(), "item"), m.group(2)), text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"\*(?![\s*])(.+?)(?<![\s*])\*", r"<em>\1</em>", text)
    for _ in range(2):  # リンクの中のコード等、預けた物の中に預けた物がある時のため
        text = re.sub("[-]", lambda m: kept[ord(m.group()) - 0xE000], text)
    return text


def join_lines(lines):
    """段落の行をつなぐ。行末の空白2つは改行、日本語の隣は空白を入れない。"""
    result = ""
    for i, raw in enumerate(lines):
        line = raw.strip()
        result += line
        if i + 1 == len(lines):
            break
        following = lines[i + 1].strip()
        if raw.endswith("  "):
            result += "\uF000"
        elif not (WIDE.search(line[-1:]) or WIDE.search(following[:1])):
            result += " "
    return inline(result).replace("\uF000", "<br>\n")  # **が行をまたぐので、つないでから書式を見る


def indent_of(line):
    return len(line) - len(line.lstrip(" "))


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def starts_block(line):
    return bool(FENCE.match(line) or BULLET.match(line) or line.lstrip().startswith("#")
                or line.lstrip().startswith("|"))


def parse_list(lines, i):
    first = BULLET.match(lines[i])
    tag = "ul" if first else "ol"
    pattern = BULLET if first else NUMBER
    base = indent_of(lines[i])
    items = []
    while i < len(lines):
        m = pattern.match(lines[i])
        if not m or indent_of(lines[i]) != base:
            break
        content = [m.group(m.lastindex)]
        i += 1
        while i < len(lines):
            line = lines[i]
            if not line.strip():
                j = i
                while j < len(lines) and not lines[j].strip():
                    j += 1
                if j < len(lines) and indent_of(lines[j]) > base:
                    content.append("")
                    i += 1
                    continue
                break
            if indent_of(line) <= base:
                break
            content.append(line)
            i += 1
        width = min((indent_of(l) for l in content[1:] if l.strip()), default=0)
        inner = [content[0]] + [l[width:] for l in content[1:]]
        items.append(parse(inner, tight=True))
        while i < len(lines) and not lines[i].strip() and i + 1 < len(lines) and pattern.match(lines[i + 1]) \
                and indent_of(lines[i + 1]) == base:
            i += 1
    return "<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % item for item in items), tag), i


def parse(lines, tight=False, headings=None):
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        fence = FENCE.match(line)
        if fence:
            kind = fence.group(1)
            i += 1
            text = []
            while i < len(lines) and not FENCE.match(lines[i]):
                text.append(lines[i])
                i += 1
            i += 1
            css = ' class="%s"' % kind if kind in ("key", "screen") else ""
            out.append("<pre%s><code>%s</code></pre>" % (css, html.escape("\n".join(text), quote=False)))
            continue
        heading = re.match(r"(#{1,6}) (.*)", line)
        if heading:
            level = len(heading.group(1))
            title = inline(heading.group(2))
            if level == 2 and headings is not None:
                anchor = "s%d" % (len(headings) + 1)
                headings.append((anchor, title))
                out.append('<h2 id="%s">%s</h2>' % (anchor, title))
            else:
                out.append("<h%d>%s</h%d>" % (level, title, level))
            i += 1
            continue
        if line.lstrip().startswith("|"):
            rows = []
            while i < len(lines) and lines[i].lstrip().startswith("|"):
                rows.append(lines[i])
                i += 1
            head = "".join("<th>%s</th>" % inline(c) for c in cells(rows[0]))
            body = "".join("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in cells(r)) for r in rows[2:])
            out.append('<div class="table"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>'
                       % (head, body))
            continue
        if BULLET.match(line) or NUMBER.match(line):
            text, i = parse_list(lines, i)
            out.append(text)
            continue
        para = [line]
        i += 1
        while i < len(lines) and lines[i].strip() and not starts_block(lines[i]):
            para.append(lines[i])
            i += 1
        text = join_lines(para)
        if tight and not out and i >= len(lines):
            out.append(text)
        elif tight and not out:
            out.append(text)
        elif para[0].lstrip().startswith("※"):
            out.append('<p class="note">%s</p>' % text)
        else:
            out.append("<p>%s</p>" % text)
    return "\n".join(out)


def main():
    source = Path(sys.argv[1])
    target = Path(sys.argv[2]) if len(sys.argv) > 2 else source.with_suffix(".html")
    lines = source.read_text(encoding="utf-8").split("\n")
    headings = []
    body = parse(lines, headings=headings)
    nav = '<nav><ul>%s</ul></nav>' % "".join('<li><a href="#%s">%s</a></li>' % h for h in headings)
    body = body.replace("</h1>", "</h1>\n" + nav, 1)
    title = re.sub(r"^# ", "", lines[0])
    target.write_text(
        '<!doctype html>\n<html lang="ja">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        f"<title>{html.escape(title)}</title>\n<style>{CSS}</style>\n</head>\n<body>\n{body}\n</body>\n</html>\n",
        encoding="utf-8",
    )
    print(target)


if __name__ == "__main__":
    main()
