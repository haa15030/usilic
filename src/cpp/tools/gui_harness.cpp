// -g(GUI版)の動作を、窓を出さずに確かめる試験用プログラム(2026-10-27、直接のユーザー依頼"入れておいて"、
// docs/CHANGES.md項目1104)。それまではアシスタントのセッションの一時置き場(scratchpad)にあった。
//
// app/main.cppのループの代わりにこのファイルのループでApplication・GraphicalDisplayを動かす。SDLのダミーの
// 映像ドライバ(SDL_VIDEODRIVER=dummy)なので窓は出ず、描画は裏の画像にだけ行われる。main.cppの慣性の引き継ぎ
// (ドラッグ・ホイール・1ドットずつのずらし)は通らないので、それだけで起きる不具合はここでは見えない。
//
// 組み立て: make harness(build/harness)。使い方:
//   ROWS=40 COLS=80 FSIZE=18 [LSH=-1] [LH0=1] [SNAPDIR=dir] [BMP=out.bmp] build/harness HOME FILE 操作...
//   HOME        設定・履歴を置くディレクトリ(本物の~を汚さないよう、空のディレクトリ等)
//   ROWS/COLS   窓の行と桁の数、FSIZE 字の大きさ(既定16)、FONT フォントのパス(既定Menlo)
//   LSH         横書きの字間(LETTER-SPACINGの横書き、縦書きは0)
//   LH0         行の高さを既定(LINE-HEIGHT無し)にする(無ければ横書き1.2・縦書き1.0)
//   SNAPDIR     coastの1歩ごとの画面をf1.bmp, f2.bmp…として保存、BMP 最後の画面を保存
//   操作: x1b等(16進の1バイト)、文字列(1字ずつ)、coast+N/coast-N(慣性の1歩をN回、先へ/前へ。1歩ごとに
//         STEP/TIMEの行を標準エラーへ)、tcoast+N/tcoast-N(1行・1列ずつ)、snap:path(その時の画面を保存)、flush
#include "application.hpp"
#include "graphical_display.hpp"
#include "key_input.hpp"
#include "history.hpp"
#include "configuration.hpp"
#include <cstdlib>
#include <cstdio>
#include <string>
#include <chrono>
#include <SDL.h>
using namespace fm;
static int frameNo = 0;
static void snap(const char *bmp) {
  SDL_Window *w = nullptr; for (Uint32 id = 0; id < 100 && !w; id++) w = SDL_GetWindowFromID(id);
  SDL_Renderer *r = w ? SDL_GetRenderer(w) : nullptr;
  int ww = 0, hh = 0;
  if (r) SDL_GetRendererOutputSize(r, &ww, &hh);
  SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, ww, hh, 32, SDL_PIXELFORMAT_ARGB8888);
  if (r && surf && SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0) SDL_SaveBMP(surf, bmp);
  if (surf) SDL_FreeSurface(surf);
}
int main(int argc, char **argv) {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (!std::getenv("LH0")) GraphicalDisplay::setLineHeightRatios(1.2, 1.0);
  auto gdp = GraphicalDisplay::create(std::getenv("ROWS") ? std::atoi(std::getenv("ROWS")) : 50, std::getenv("COLS") ? std::atoi(std::getenv("COLS")) : 100, std::getenv("FONT") ? std::getenv("FONT") : "/System/Library/Fonts/Menlo.ttc", std::getenv("FSIZE") ? std::atoi(std::getenv("FSIZE")) : 16);
  if (!gdp) { std::fprintf(stderr, "create failed\n"); return 1; }
  GraphicalDisplay &gd = *gdp;
  if (const char *ls = std::getenv("LSH")) GraphicalDisplay::setLetterSpacingExtras(std::atoi(ls), 0);
  gd.open();
  if (std::getenv("LSH")) gd.adjustLetterSpacing(0);
  std::fprintf(stderr, "rows=%d cols=%d\n", gd.rows(), gd.columns());
  KeyInput keyInput{gd};
  History history;
  Configuration configuration;
  ShellOptions options;
  options.argv = {"bash", "--norc", "--noprofile"};
  std::string home = argv[1];
  Application app{gd, keyInput, history, configuration, Charset::Encoding::Utf8, gd.columns(), options, home};
  app.startPagerFileList({argv[2]});
  gd.flush();
  for (int i = 3; i < argc; i++) {
    std::string k = argv[i];
    if (k == "flush") { gd.flush(); continue; }
    if (k.rfind("tcoast", 0) == 0) {
      int dir = k[6] == '+' ? 1 : -1;
      int n = std::atoi(k.c_str() + 7);
      bool columns = app.epubVerticalDragActive();
      for (int j = 0; j < n; j++) {
        if (columns) {
          if (!app.scrollEpubVertical(dir)) { std::fprintf(stderr, "TSTEP %d edge\n", j); break; }
          if (!app.epubVerticalDragActive()) columns = false;
          std::fprintf(stderr, "TSTEP %d V horiz=%d\n", j, app.epubHorizontalSectionActive());
          continue;
        }
        ContentView &view = app.activeContentView();
        int before = view.topIndex();
        view.setTopIndex(before + dir);
        if (view.topIndex() == before) {
          app.preloadPagerAhead(before + gd.rows() * 2);
          view.setTopIndex(before + dir);
        }
        if (view.topIndex() == before) { std::fprintf(stderr, "TSTEP %d STOP top=%d size=%d h=%d\n", j, before, (int)view.lines().size(), view.height()); break; }
        view.setCursorIndex(dir > 0 ? std::min(view.topIndex() + view.height() - 1, (int)view.lines().size() - 1) : view.topIndex());
        bool left = app.syncEpubHorizontalSection();
        if (left) { columns = app.epubVerticalDragActive(); }
        else view.redraw();
        std::fprintf(stderr, "TSTEP %d H top=%d left=%d size=%d\n", j, view.topIndex(), left, (int)view.lines().size());
      }
      continue;
    }
    if (k.rfind("coast", 0) == 0) {  // coast+N / coast-N
      int dir = k[5] == '+' ? 1 : -1;
      int n = std::atoi(k.c_str() + 6);
      for (int j = 0; j < n; j++) {
        auto t0 = std::chrono::steady_clock::now();
        if (const char *g = std::getenv("USILICGUIDEBUG")) { FILE *f = std::fopen(g, "a"); std::fprintf(f, "STEPMARK %d\n", j); std::fclose(f); }
        bool columns = app.epubVerticalDragActive();
        if (columns) {
          bool ok = app.scrollEpubVertical(dir);
          std::fprintf(stderr, "STEP %d V ok=%d horiz=%d\n", j, ok, app.epubHorizontalSectionActive());
          if (!ok && !app.epubHorizontalSectionActive()) break;
        } else {
          ContentView &v = app.activeContentView();
          bool ok = v.stepTopIndex(dir);
          v.redraw();
          auto tA = std::chrono::steady_clock::now();
          bool left = app.syncEpubHorizontalSection();
          std::fprintf(stderr, "  sync %lldms\n", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tA).count());
          ContentView &v2 = app.activeContentView();
          std::fprintf(stderr, "STEP %d H ok=%d left=%d top=%d cur=%d horiz=%d size=%d raw=%d\n", j, ok, left, v2.topIndex(), v2.cursorIndex(), app.epubHorizontalSectionActive(), (int)v2.lines().size(), v2.lines().empty() ? -1 : v2.lines()[v2.topIndex()].rawLineNumber.value_or(-1));
        }
        gd.setVerticalLineSpacing(app.verticalLayoutShown());
        if (gd.consumeSizeChanged()) { app.handleResize(); std::fprintf(stderr, "RESIZE rows=%d\n", gd.rows()); }
        gd.flush();
        std::fprintf(stderr, "TIME %d %lldms\n", j, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        if (const char *d = std::getenv("SNAPDIR")) snap((std::string(d) + "/f" + std::to_string(++frameNo) + ".bmp").c_str());
      }
      continue;
    }
    if (k.rfind("snap:", 0) == 0) { gd.flush(); gd.flush(); gd.flush(); snap(k.c_str() + 5); continue; }
    std::fprintf(stderr, "KEY %s\n", k.c_str());
    if (k.rfind("x", 0) == 0) { app.feedByte(std::strtol(k.c_str() + 1, nullptr, 16), std::nullopt); continue; }
    for (char c : k) app.feedByte(static_cast<unsigned char>(c), std::nullopt);
  }
  gd.flush();
  if (const char *bmp = std::getenv("BMP")) {
    SDL_Window *w = nullptr; for (Uint32 id = 0; id < 100 && !w; id++) w = SDL_GetWindowFromID(id);
    SDL_Renderer *r = w ? SDL_GetRenderer(w) : nullptr;
    int ww = 0, hh = 0;
    if (r) SDL_GetRendererOutputSize(r, &ww, &hh);
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, ww, hh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (r && surf && SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0) SDL_SaveBMP(surf, bmp);
    else std::fprintf(stderr, "readpixels failed w=%p r=%p %s\n", (void*)w, (void*)r, SDL_GetError());
  }
  return 0;
}
