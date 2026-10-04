"""url2pdf(同じディレクトリのシェルスクリプト)の本体。

ページを画面のレイアウト(印刷用のCSSではなく)で開き、最後までスクロールして遅れて読み込まれる画像を
読ませてから、ページ全体の高さを用紙の高さにしてPDFにする(page.pdf()にはfull_pageのような引数は無い)。
PDFの1ページの大きさには上限(約200インチ)があり、それより長いページは複数のページに分かれる。
出力は第2引数のディレクトリ(無ければ~/Downloads、それも無ければホーム)の「ページの題名.pdf」。
urls2pdf.py(ページの中のリンク先をまとめてPDFにする)もopen_page()/save_pdf()を使う。
"""

import asyncio
import os
import re
import sys
from urllib.parse import urlparse

from playwright.async_api import async_playwright

WIDTH = 1280  # ページを組む幅(px)
MAX_HEIGHT = 200 * 96  # PDFの1ページの上限(200インチ、96px/インチ)


def output_path(directory: str, title: str, url: str) -> str:
    name = re.sub(r'[/\\:*?"<>|\x00-\x1f]', "_", title).strip(" .") or urlparse(url).netloc or "page"
    name = name[:80]
    path = os.path.join(directory, name + ".pdf")
    n = 2
    while os.path.exists(path):
        path = os.path.join(directory, f"{name}-{n}.pdf")
        n += 1
    return path


async def open_page(page, url: str) -> None:
    """urlを開き、遅れて読み込まれる画像のために最後までスクロールして戻る。"""
    try:
        await page.goto(url, wait_until="networkidle", timeout=30000)
    except Exception:
        pass  # 読み込みの終わらないページ(広告等)でも、そこまでの内容で作る
    await page.emulate_media(media="screen")
    await page.evaluate(
        """async () => {
             for (let y = 0; y < document.documentElement.scrollHeight; y += window.innerHeight) {
               window.scrollTo(0, y);
               await new Promise(r => setTimeout(r, 100));
             }
             window.scrollTo(0, 0);
           }"""
    )
    try:
        await page.wait_for_load_state("networkidle", timeout=5000)
    except Exception:
        pass


async def save_pdf(page, directory: str, url: str) -> str:
    """open_page()で開いたページを、ページ全体の高さの1ページのPDFにしてdirectoryへ保存する。"""
    height = await page.evaluate(
        "Math.max(document.documentElement.scrollHeight, document.body ? document.body.scrollHeight : 0)"
    )
    path = output_path(directory, await page.title(), url)
    await page.pdf(
        path=path,
        width=f"{WIDTH}px",
        height=f"{min(height + 1, MAX_HEIGHT)}px",
        print_background=True,
        margin={"top": "0", "bottom": "0", "left": "0", "right": "0"},
    )
    return path


async def main(url: str, directory: str) -> str:
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        page = await browser.new_page(viewport={"width": WIDTH, "height": 900})
        await open_page(page, url)
        path = await save_pdf(page, directory, url)
        await browser.close()
        return path


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: url2pdf URL [directory]", file=sys.stderr)
        sys.exit(2)
    target = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser("~/Downloads")
    if not os.path.isdir(target):
        target = os.path.expanduser("~")
    print(asyncio.run(main(sys.argv[1], target)))
