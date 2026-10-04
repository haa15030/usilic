"""urls2pdf(同じディレクトリのシェルスクリプト)の本体(2026-10-04)。

Webのページ(目次のページ)の中のリンク先を、url2pdfと同じ縦長の1ページのPDFにして保存し、1本のPDFに
まとめる(章ごとにしおり付き)。リンクはChromiumで描画した後のページから集めるので、JavaScriptで作られる
リンクも取れる。既定では本文(<main>・<article>・role="main"の中、無ければ見出しを含みサイドバー等と合流
する手前の要素、COLLECT_LINKS参照)の、目次と同じ
サイトのリンクだけを、出てきた順に取る(#以降は無視、同じものは1回)。それで足りない・多すぎる時は
-m(URLの正規表現)・-a(ページ全体)・-x(他のサイトも)で選ぶ。-lで取るリンクの一覧だけを見られる。
robots.txtで禁止されたリンクは取らず、1件ごとに-w秒(既定1秒)待つ。ログインが要るページは取れない。

出力: 1件ごとのPDFは-dのディレクトリ(既定は~/Downloads/目次の題名/)、まとめたPDFは-oのパス(既定は
~/Downloads/目次の題名.pdf)。最後にまとめたPDFのパス(--no-mergeなら1件ごとのPDFのパス)を標準出力へ
書き、途中経過は標準エラーへ。
"""

import argparse
import asyncio
import os
import re
import sys
import urllib.request
import urllib.robotparser
from urllib.parse import urldefrag, urljoin, urlparse

from playwright.async_api import async_playwright

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from url2pdf import WIDTH, open_page, output_path, save_pdf  # noqa: E402

USER_AGENT = "Mozilla/5.0 (usilic urls2pdf)"

COLLECT_LINKS = """(all) => {
  // 本文の範囲: <main>・<article>・role="main"があればその中。無ければ見出し(h1、無ければh2)から親を
  // たどり、リンクの数が跳ね上がる(サイドバー等と合流する)手前の要素。見出しも無ければページ全体。
  let roots = [];
  if (!all) {
    roots = [...document.querySelectorAll('main, article, [role=main]')];
    const heading = roots.length ? null : document.querySelector('h1') || document.querySelector('h2');
    if (heading) {
      const count = e => e.querySelectorAll('a[href]').length;
      let e = heading;
      while (e.parentElement && e.parentElement !== document.body) {
        const here = count(e), up = count(e.parentElement);
        if (here > 0 && up > here * 2 + 10) break;
        e = e.parentElement;
      }
      roots = [e];
    }
  }
  if (!roots.length) roots = [document];
  const links = [];
  for (const root of roots)
    for (const a of root.querySelectorAll('a[href]'))
      links.push({href: a.href, text: (a.innerText || a.title || '').trim()});
  return links;
}"""


def robots_for(url: str):
    parts = urlparse(url)
    parser = urllib.robotparser.RobotFileParser()
    try:
        request = urllib.request.Request(f"{parts.scheme}://{parts.netloc}/robots.txt", headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(request, timeout=10) as response:
            parser.parse(response.read().decode("utf-8", "replace").splitlines())
    except Exception:
        return None  # 読めなければ制限無しとみなす
    return parser


def pick_links(links, index_url: str, pattern, any_host: bool):
    index = urldefrag(index_url)[0]
    host = urlparse(index).netloc
    seen, picked = {index}, []
    for link in links:
        url = urldefrag(link["href"])[0]
        parts = urlparse(url)
        if parts.scheme not in ("http", "https") or url in seen:
            continue
        if not any_host and parts.netloc != host:
            continue
        if pattern and not re.search(pattern, url):
            continue
        seen.add(url)
        picked.append((url, link["text"]))
    return picked


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


async def main(args) -> int:
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        page = await browser.new_page(viewport={"width": WIDTH, "height": 900})
        await open_page(page, args.url)
        title = await page.title()
        links = pick_links(await page.evaluate(COLLECT_LINKS, args.all), args.url, args.match, args.any_host)
        if args.list:
            for url, text in links:
                print(f"{url}\t{text}")
            await browser.close()
            return 0
        if not links:
            log("No links (try -a, -x or -m)")
            await browser.close()
            return 1

        base = re.sub(r'[/\\:*?"<>|\x00-\x1f]', "_", title).strip(" .")[:80] or urlparse(args.url).netloc
        downloads = os.path.expanduser("~/Downloads")
        if not os.path.isdir(downloads):
            downloads = os.path.expanduser("~")
        directory = args.dir or os.path.join(downloads, base)
        os.makedirs(directory, exist_ok=True)

        robots = robots_for(args.url)
        saved = []  # (パス, しおりの名前)
        if not args.no_index:
            path = await save_pdf(page, directory, args.url)
            saved.append((path, title))
            log(f"[0/{len(links)}] {path}")
        for n, (url, text) in enumerate(links, 1):
            if robots and not robots.can_fetch(USER_AGENT, url):
                log(f"[{n}/{len(links)}] skipped (robots.txt): {url}")
                continue
            await asyncio.sleep(args.wait)
            try:
                await open_page(page, url)
                path = await save_pdf(page, directory, url)
            except Exception as error:
                log(f"[{n}/{len(links)}] failed: {url}: {error}")
                continue
            saved.append((path, await page.title() or text))
            log(f"[{n}/{len(links)}] {path}")
        await browser.close()

    if args.no_merge:
        for path, _ in saved:
            print(path)
        return 0
    from pypdf import PdfWriter

    output = args.output or output_path(downloads, title, args.url)
    writer = PdfWriter()
    for path, name in saved:
        writer.append(path, outline_item=name)
    with open(output, "wb") as f:
        writer.write(f)
    print(output)
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="urls2pdf", description="ページの中のリンク先をまとめてPDFにする")
    parser.add_argument("url", help="目次のページのURL")
    parser.add_argument("-m", "--match", help="取るリンクのURLの正規表現(例: 'help/topic')")
    parser.add_argument("-a", "--all", action="store_true", help="本文だけでなくページ全体のリンクから取る")
    parser.add_argument("-x", "--any-host", action="store_true", help="他のサイトへのリンクも取る")
    parser.add_argument("-l", "--list", action="store_true", help="取るリンクの一覧を出すだけ")
    parser.add_argument("-d", "--dir", help="1件ごとのPDFの置き場所(既定: ~/Downloads/目次の題名/)")
    parser.add_argument("-o", "--output", help="まとめたPDF(既定: ~/Downloads/目次の題名.pdf)")
    parser.add_argument("-w", "--wait", type=float, default=1.0, help="1件ごとに待つ秒数(既定: 1)")
    parser.add_argument("--no-index", action="store_true", help="目次のページ自身を入れない")
    parser.add_argument("--no-merge", action="store_true", help="1本にまとめない")
    sys.exit(asyncio.run(main(parser.parse_args())))
