#include "graphical_display.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "utf8.hpp"

namespace fm {

namespace {

struct Cell {
  std::string glyph;       // UTF-8 bytes for this cell's glyph, empty = blank
  bool bold = false;
  bool underline = false;
  bool standout = false;
  bool continuation = false;  // the "shadow" second cell of a 2-column-wide glyph -- not drawn on its own
};

// Decodes the leading UTF-8 sequence in `utf8Bytes` (already segmented
// to the right length by fm::Utf8::sequence_length() elsewhere) to a
// single Unicode scalar. Returns 0 for an empty string. Deliberately a
// small local decoder rather than an addition to fm::Utf8 (which only
// ever needed byte-length/display-width, not the actual codepoint) --
// this is the one place in the whole port that needs to ask a font
// "do you have a glyph for THIS specific character," see
// glyphProvidedByFont() below.
unsigned int decodeUtf8Codepoint(const std::string &utf8Bytes) {
  if (utf8Bytes.empty()) return 0;
  auto b = [&](std::size_t i) -> unsigned int { return static_cast<unsigned char>(utf8Bytes[i]); };
  unsigned int lead = b(0);
  if (lead < 0x80) return lead;
  if ((lead & 0xE0) == 0xC0 && utf8Bytes.size() >= 2) return ((lead & 0x1F) << 6) | (b(1) & 0x3F);
  if ((lead & 0xF0) == 0xE0 && utf8Bytes.size() >= 3)
    return ((lead & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
  if ((lead & 0xF8) == 0xF0 && utf8Bytes.size() >= 4)
    return ((lead & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
  return lead;  // malformed/truncated -- fall back to the raw lead byte, matches TTF's own "just render something"
}

bool glyphProvidedByFont(TTF_Font *font, unsigned int codepoint) {
  if (!font || codepoint == 0 || codepoint > 0xFFFF) return false;  // TTF_GlyphIsProvided is UCS-2 only
  return TTF_GlyphIsProvided(font, static_cast<Uint16>(codepoint)) != 0;
}

// Computes where to draw a glyph surface within its allocated
// boxX/boxY..maxW/maxH cell(s), UNIFORMLY scaled down (never up, and
// never independently in X vs Y) if it would otherwise overflow, then
// centered. Replaces an earlier version that stretched the surface's
// own natural size to EXACTLY fill maxW/maxH -- non-uniform stretching
// distorts a glyph's aspect ratio, and a CJK fallback font's natural
// glyph proportions don't necessarily match cellWidth*2 x cellHeight
// (measured from the PRIMARY font's Latin "M"), so that stretch was
// warping recognizable kanji/kana into different-looking, still-wrong
// shapes (confirmed from a real report: the SPECIFIC way Japanese
// filenames were garbled changed after adding that stretch, rather
// than being fixed by it). Scaling only ever shrinks (never enlarges
// a naturally-small glyph, which would blur it) and always preserves
// aspect ratio, so a glyph either renders crisp at its natural size
// (the common case, if it already fits) or shrinks uniformly with
// letterboxing -- never distorted.
SDL_Rect fitGlyphRect(int boxX, int boxY, int maxW, int maxH, int surfaceW, int surfaceH) {
  float scale = std::min(1.0f, std::min(static_cast<float>(maxW) / std::max(surfaceW, 1),
                                         static_cast<float>(maxH) / std::max(surfaceH, 1)));
  int drawW = static_cast<int>(surfaceW * scale);
  int drawH = static_cast<int>(surfaceH * scale);
  return SDL_Rect{boxX + (maxW - drawW) / 2, boxY + (maxH - drawH) / 2, drawW, drawH};
}

// Best-effort search for a CJK-capable font to fall back to when the
// PRIMARY font (resolved by main.cpp -- Menlo/Monaco/DejaVu/Liberation
// by default, none of which cover Japanese kana/kanji) is missing a
// glyph. Added after a real-machine report of Japanese filenames
// rendering as a string of "tofu" placeholder boxes (confirmed from
// the reporting screenshot itself -- not generic mojibake/wrong-byte
// decoding, just missing glyph coverage in whichever font main.cpp
// happened to load).
//
// Tries each candidate path in order, keeping the first one that both
// (a) loads at all and (b) actually has a glyph for U+3042 ('あ',
// hiragana -- picked because hiragana coverage reliably implies kanji
// coverage too in any font that bothers to ship Japanese support at
// all, but the reverse isn't true: some CJK fonts cover Han ideographs
// shared with Chinese without covering kana, which alone wouldn't fix
// the reported filenames). The exact file names/paths for Apple's
// Hiragino family have shifted across macOS versions and this project
// has no way to verify them against a real machine (same sandbox
// limitation noted throughout this port's GUI work) -- several
// plausible candidates are listed for exactly that reason, and a
// match failing here just means no CJK fallback (impl_->fallbackFont
// stays nullptr), not a startup failure; ordinary ASCII text is
// completely unaffected either way.
TTF_Font *loadCjkFallbackFont(int pointSize) {
  static const char *const candidates[] = {
      "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc",
      "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
      "/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc",
      "/System/Library/Fonts/Hiragino Sans GB.ttc",
      "/System/Library/Fonts/PingFang.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
  };
  for (const char *path : candidates) {
    TTF_Font *candidate = TTF_OpenFont(path, std::max(pointSize, 1));
    if (!candidate) continue;
    if (glyphProvidedByFont(candidate, 0x3042)) return candidate;
    TTF_CloseFont(candidate);
  }
  return nullptr;
}

}  // namespace

struct GraphicalDisplay::Impl {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  TTF_Font *font = nullptr;
  TTF_Font *fallbackFont = nullptr;  // see loadCjkFallbackFont()'s own comment; nullptr if none was found

  int rows = 0, columns = 0;
  int cellWidth = 0, cellHeight = 0;
  // Real terminal emulator windows (Terminal.app, iTerm, GNOME
  // Terminal...) all reserve a modest border of window background
  // around the character grid rather than butting text right up
  // against the window edge -- this class originally didn't, so a
  // window that otherwise looked identical still read as visibly
  // "off" side by side with a real terminal (a real user report,
  // 2026-08-03, via a Gemini-drafted technical answer the user asked
  // to have implemented as written: reserve margin pixels on every
  // side, grow the window by that amount, and offset every draw
  // position by it). Fixed pixel values (not e.g. a fraction of
  // cellWidth/cellHeight) because that's what a real terminal's own
  // margin is -- it doesn't visibly grow or shrink with font size.
  // 10, not Gemini's originally-suggested 15: a real side-by-side
  // screenshot comparison (2026-08-03 follow-up, "余白はやや広め")
  // measured the real Terminal.app window's own margin directly
  // (pixel-scanning the screenshot itself, since this sandbox has no
  // screen-capture access to measure a live window) -- about 9px
  // between the title bar's bottom edge and the first text row, about
  // 11px between the window's left edge and the first column -- close
  // enough to treat as one shared value rather than separate X/Y ones.
  static constexpr int kMarginX = 10;
  static constexpr int kMarginY = 10;
  // Reserved strip on the right edge for the scrollbar overlay (see
  // renderScrolled()'s own drawScrollbar() call) -- 2026-08-07, first
  // slice of the feature IDEAS.md recorded 2026-08-03 (docs/HISTORY.md
  // line 521's own request to start it): a real side-by-side
  // screenshot comparison found the GUI window narrower than the real
  // terminal it's meant to overlay by roughly a scrollbar's width once
  // the real terminal's own native scrollbar was accounted for.
  // UNLIKE kMarginX/Y above (pixel-scanned from a real screenshot),
  // this value is a rough placeholder, not yet measured against a
  // real terminal's actual native scrollbar width the same way --
  // this sandbox has no way to do that measurement itself (same
  // limitation noted throughout this port's GUI work); real-machine
  // verification and likely correction is expected as a follow-up.
  static constexpr int kScrollbarWidth = 15;
  std::vector<Cell> grid;

  SDL_Color fg{220, 220, 220, 255};
  SDL_Color bg{0, 0, 0, 255};

  int cursorRow = 0, cursorCol = 0;
  int savedRow = 0, savedCol = 0;
  bool standoutActive = false, boldActive = false, underlineActive = false;

  // The LOGICAL cursor position renderCursor() actually draws --
  // deliberately separate from cursorRow/cursorCol above (which is
  // purely "where does the next byte get written", constantly moved
  // around by ContentView::redraw()'s own per-row/mode-line writes).
  // Only setVisibleCursor() touches this; see Display::
  // setVisibleCursor()'s own comment for why the split exists.
  // hasVisibleCursor starts false so nothing is drawn before any
  // caller has explicitly asked for a cursor (avoids a stray block at
  // the default (0,0) the moment a window opens, before e.g. SHELL or
  // a prompt has positioned one anywhere meaningful).
  bool hasVisibleCursor = false;
  int visibleCursorRow = 0, visibleCursorCol = 0;

  std::string pendingUtf8;  // bytes of an incomplete multi-byte sequence, see putByte()

  bool outputEnabled = true;
  bool smoothScroll = false;

  std::deque<MouseClick> mouseQueue;
  std::deque<KeyEvent> keyQueue;
  std::deque<DragEvent> dragQueue;
  std::deque<HoverEvent> hoverQueue;
  std::deque<WheelEvent> wheelQueue;
  SDL_Cursor *handCursor = nullptr;
  bool handCursorActive = false;

  Cell &cellAt(int row, int col) { return grid[row * columns + col]; }
};

GraphicalDisplay::GraphicalDisplay() : impl_(std::make_unique<Impl>()) {}

std::unique_ptr<GraphicalDisplay> GraphicalDisplay::create(int rows, int columns, const std::string &fontPath,
                                                             int fontPointSize) {
  if (rows <= 0 || columns <= 0) return nullptr;
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return nullptr;
  if (TTF_Init() != 0) {
    SDL_Quit();
    return nullptr;
  }

  TTF_Font *font = TTF_OpenFont(fontPath.c_str(), std::max(fontPointSize, 1));
  if (!font) {
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
  int cellWidth = 0, cellHeight = 0;
  if (TTF_SizeUTF8(font, "M", &cellWidth, &cellHeight) != 0 || cellWidth <= 0 || cellHeight <= 0) {
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }

  // Loaded and measured BEFORE the window/cell grid are sized (not
  // lazily later) so the grid can be grown to fit both fonts up
  // front, rather than scaling either font's glyphs at render time to
  // fit a grid sized for the OTHER one. An earlier version did the
  // latter (uniform shrink-to-fit, added after a non-uniform-stretch
  // attempt visibly distorted CJK glyph shapes) but that still showed
  // up as a real report of "half-missing" Japanese characters --
  // shrinking a fallback-font glyph down to a primary-font-sized cell
  // can still clip/thin out fine strokes badly enough to look
  // incomplete. Growing the grid instead means BOTH fonts always draw
  // at their own natural, undistorted, unclipped size.
  TTF_Font *fallbackFont = loadCjkFallbackFont(fontPointSize);
  if (fallbackFont) {
    int cjkWidth = 0, cjkHeight = 0;
    if (TTF_SizeUTF8(fallbackFont, "\xE5\x9B\xBD" /* "国", a representative kanji */, &cjkWidth, &cjkHeight) == 0) {
      // A CJK glyph occupies 2 columns (see commitGlyph()'s
      // continuation-cell convention), so half its natural width is
      // what one column needs to hold; round up so it's never
      // slightly too narrow.
      cellWidth = std::max(cellWidth, (cjkWidth + 1) / 2);
      cellHeight = std::max(cellHeight, cjkHeight);
    }
  }

  // SDL_WINDOW_ALLOW_HIGHDPI: without it, on a Retina/HiDPI display
  // SDL creates the window's actual drawable at LOGICAL point
  // resolution, and the OS compositor upscales that lower-resolution
  // framebuffer to fill the real physical pixels -- exactly the kind
  // of soft/blurry text a real report described ("文字がぼやけ気味"),
  // and uniform across every draw call in this file (confirmed by
  // reading both renderCell() and renderTextRun(): they're otherwise
  // near-identical TTF_RenderUTF8_Blended()+fitGlyphRect() call
  // sequences, so a text-crispness difference between them isn't a
  // per-path rendering bug -- this compositor-level upscale is a
  // window/renderer-wide effect neither path could individually cause
  // or avoid).
  SDL_Window *window = SDL_CreateWindow(
      "usilic", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      cellWidth * columns + Impl::kMarginX * 2 + Impl::kScrollbarWidth, cellHeight * rows + Impl::kMarginY * 2,
      SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    if (fallbackFont) TTF_CloseFont(fallbackFont);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    SDL_DestroyWindow(window);
    if (fallbackFont) TTF_CloseFont(fallbackFont);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
  // The window was created in logical POINTS (cellWidth*columns etc,
  // used throughout this file for all cell/row/col pixel math); the
  // renderer's actual drawable is now in physical PIXELS (2x/3x on a
  // Retina display, thanks to ALLOW_HIGHDPI above). Scaling the
  // renderer by drawable/window keeps every existing pixel coordinate
  // in this file meaning "one logical point" while SDL internally
  // renders at the higher physical resolution -- the standard SDL2
  // pattern for adding HiDPI support without rewriting all downstream
  // pixel math to a different unit.
  int windowWidthPoints = 0, windowHeightPoints = 0;
  int drawableWidthPixels = 0, drawableHeightPixels = 0;
  SDL_GetWindowSize(window, &windowWidthPoints, &windowHeightPoints);
  SDL_GetRendererOutputSize(renderer, &drawableWidthPixels, &drawableHeightPixels);
  if (windowWidthPoints > 0 && windowHeightPoints > 0)
    SDL_RenderSetScale(renderer, static_cast<float>(drawableWidthPixels) / windowWidthPoints,
                        static_cast<float>(drawableHeightPixels) / windowHeightPoints);

  // std::unique_ptr<GraphicalDisplay>(new ...): the constructor is
  // private (callers must go through this factory, matching
  // TerminalDisplay::fromEnvironment()'s own shape), so make_unique
  // can't reach it from outside the class.
  std::unique_ptr<GraphicalDisplay> display(new GraphicalDisplay());
  display->impl_->window = window;
  display->impl_->renderer = renderer;
  display->impl_->font = font;
  display->impl_->fallbackFont = fallbackFont;
  display->impl_->cellWidth = cellWidth;
  display->impl_->cellHeight = cellHeight;
  display->impl_->rows = rows;
  display->impl_->columns = columns;
  display->impl_->grid.assign(static_cast<std::size_t>(rows) * columns, Cell{});
  SDL_StartTextInput();
  // Without this, a real user report (2026-08-03: "^Nが無反応→マウス
  // の左クリックも無反応→何度目かの左クリックでようやく表示") found
  // this window created but NOT focused: SDL_WINDOW_SHOWN alone maps
  // the window but doesn't reliably steal input focus away from
  // whichever window was already focused (the real terminal that just
  // launched usilic, in the "実はすでにGUI" launch path this bug was
  // found in) on every platform/WM -- keystrokes kept going to that
  // terminal instead, and the first several mouse clicks were consumed
  // just bringing the window to focus rather than being delivered to
  // this app as normal button-down events. SDL_RaiseWindow() is the
  // standard cross-platform fix recommended for exactly this class of
  // "window exists but isn't actually focused yet" symptom.
  SDL_RaiseWindow(window);
  return display;
}

GraphicalDisplay::~GraphicalDisplay() {
  // Matches create()'s own SDL_StartTextInput() call (needed so
  // SDL_TEXTINPUT events, including multi-byte UTF-8 typed via an
  // input method, arrive at all -- see pumpEvents()'s own comment).
  // Without a matching SDL_StopTextInput() before the window/renderer
  // are torn down, macOS's Input Method Kit framework is left holding
  // a reference to a text-input client that's about to disappear out
  // from under it -- a real 2026-08-04 user report (screenshot) showed
  // this printed to the terminal right after a -g session exited:
  // "error messaging the mach port for IMKCFRunLoopWakeUpReliable".
  // Benign (no crash, no functional effect either way -- this is an
  // OS-level diagnostic, not anything usilic itself detects or acts
  // on) but noisy; SDL_StopTextInput() here is the documented fix for
  // exactly this IMK teardown-ordering complaint.
  //
  // A 2026-08-05 follow-up report found the message still appeared,
  // but specifically when exiting via a KEYBOARD gesture typed while
  // the window had active text-input focus (ESC-ESC-Enter or ^X^C+y)
  // -- as opposed to, say, the window's own close button, a plain
  // mouse action. SDL_StopTextInput() alone only queues the
  // deactivation with SDL/Cocoa; it doesn't force it to actually run
  // before SDL_Quit()/window teardown proceeds immediately afterward.
  // If the OS's Input Method Kit hadn't fully processed that
  // notification yet -- more likely right after it was just actively
  // handling live keyboard/IME input than after an idle mouse click --
  // that's a plausible reason it would still complain. SDL_PumpEvents()
  // gives SDL's own event/run-loop machinery one more explicit pass to
  // actually deliver that notification before this destructor
  // continues into destroying the window. Still not independently
  // verified in this sandbox (no OS-level log capture available, same
  // limitation noted throughout this port's other GUI-teardown work)
  // -- please retest specifically via ESC-ESC-Enter and ^X^C+y after
  // rebuilding.
  SDL_StopTextInput();
  SDL_PumpEvents();
  if (impl_->handCursor) SDL_FreeCursor(impl_->handCursor);
  if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
  if (impl_->window) SDL_DestroyWindow(impl_->window);
  if (impl_->fallbackFont) TTF_CloseFont(impl_->fallbackFont);
  if (impl_->font) TTF_CloseFont(impl_->font);
  TTF_Quit();
  SDL_Quit();
}

void GraphicalDisplay::write(const std::string &text) {
  for (char c : text) putByte(static_cast<unsigned char>(c));
}

void GraphicalDisplay::putChar(int c) { putByte(static_cast<unsigned char>(c)); }

void GraphicalDisplay::putByte(unsigned char c) {
  if (!impl_->pendingUtf8.empty()) {
    impl_->pendingUtf8.push_back(static_cast<char>(c));
    int len = Utf8::sequence_length(static_cast<unsigned char>(impl_->pendingUtf8[0]));
    if (static_cast<int>(impl_->pendingUtf8.size()) >= len) {
      int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(impl_->pendingUtf8.data()), len);
      commitGlyph(impl_->pendingUtf8, std::max(width, 1));
      impl_->pendingUtf8.clear();
    }
    return;
  }

  if (c == 7) return;  // BEL -- no visual/audible bell in this slice, see class comment

  int len = Utf8::sequence_length(c);
  if (len > 1) {
    impl_->pendingUtf8.assign(1, static_cast<char>(c));
    return;
  }
  if (c < 0x20 || c == 0x7f) {
    commitGlyph(" ", 1);  // control-byte placeholder -- see class comment
    return;
  }
  commitGlyph(std::string(1, static_cast<char>(c)), 1);
}

void GraphicalDisplay::commitGlyph(const std::string &utf8Bytes, int width) {
  if (impl_->cursorRow < 0 || impl_->cursorRow >= impl_->rows) return;
  if (impl_->cursorCol < 0 || impl_->cursorCol >= impl_->columns) return;  // clip past the right edge

  Cell &cell = impl_->cellAt(impl_->cursorRow, impl_->cursorCol);
  cell.glyph = utf8Bytes;
  cell.bold = impl_->boldActive;
  cell.underline = impl_->underlineActive;
  cell.standout = impl_->standoutActive;
  cell.continuation = false;

  if (width == 2 && impl_->cursorCol + 1 < impl_->columns) {
    Cell &shadow = impl_->cellAt(impl_->cursorRow, impl_->cursorCol + 1);
    shadow = Cell{};
    shadow.continuation = true;
    shadow.bold = cell.bold;
    shadow.underline = cell.underline;
    shadow.standout = cell.standout;
  }
  impl_->cursorCol += std::max(width, 1);
}

// Paints just this cell's background rectangle -- split out from the
// glyph drawing below (and run as its own full pass over every cell
// in flush(), before any glyph pass runs) so that a wide glyph's
// SECOND (continuation) cell never paints its background AFTER the
// glyph from the cell to its left has already been drawn there. The
// two used to be interleaved column-by-column in a single pass, which
// meant: home cell's glyph is drawn spanning both columns (by design,
// via fitGlyphRect()'s maxW = cellWidth*2) -- then the very next
// renderCell() call, for the continuation cell, unconditionally
// painted a background rect over ITS column before returning,
// silently erasing the right half of the glyph just drawn. This
// looked exactly like the reported "half-missing"/squished CJK
// characters (every double-width glyph missing its right half), and
// survived both the display_width() fix and the font-selection fix
// because it's a pure draw-ORDER bug, unrelated to either -- found by
// noticing GraphicalDisplay::renderScrolled() (used only while
// dragging/coasting, and unaffected by this bug since it fills the
// WHOLE screen's background exactly once per frame rather than per
// cell) rendered the identical content correctly, per a real report
// that clicking and holding the mouse "fixed" the garbling until
// release.
void GraphicalDisplay::renderCellBackground(int row, int col) {
  if (!impl_->outputEnabled) return;
  const Cell &cell = impl_->cellAt(row, col);
  SDL_Rect box{Impl::kMarginX + col * impl_->cellWidth, Impl::kMarginY + row * impl_->cellHeight, impl_->cellWidth,
               impl_->cellHeight};
  SDL_Color bg = cell.standout ? impl_->fg : impl_->bg;
  SDL_SetRenderDrawColor(impl_->renderer, bg.r, bg.g, bg.b, 255);
  SDL_RenderFillRect(impl_->renderer, &box);
}

void GraphicalDisplay::renderCell(int row, int col) {
  if (!impl_->outputEnabled) return;
  const Cell &cell = impl_->cellAt(row, col);
  SDL_Rect box{Impl::kMarginX + col * impl_->cellWidth, Impl::kMarginY + row * impl_->cellHeight, impl_->cellWidth,
               impl_->cellHeight};

  SDL_Color fg = impl_->fg;
  SDL_Color bg = impl_->bg;
  if (cell.standout) std::swap(fg, bg);

  if (cell.continuation || cell.glyph.empty() || cell.glyph == " ") return;

  int style = TTF_STYLE_NORMAL;
  if (cell.bold) style |= TTF_STYLE_BOLD;
  if (cell.underline) style |= TTF_STYLE_UNDERLINE;

  // Whether this glyph occupies one cell or two (a CJK/full-width
  // character, see commitGlyph()'s continuation-cell convention).
  bool isWide = (col + 1 < impl_->columns) && impl_->cellAt(row, col + 1).continuation;

  // Fall back to a CJK-capable font (see loadCjkFallbackFont()'s own
  // comment) for whatever glyphs the primary font is missing --
  // without this, any character the primary font lacks renders as a
  // blank "tofu" placeholder box instead (confirmed from a real
  // report: Japanese filenames rendering as a string of boxes).
  //
  // For a WIDE cell specifically, the fallback is preferred even when
  // the primary (Latin monospace) font ALSO happens to provide a
  // glyph: Menlo, e.g., ships its own narrow glyphs for U+25BD (▽)
  // and the curly quotes U+201C/U+201D -- the exact "ambiguous width"
  // characters fm::Utf8::display_width() classifies as 2 columns wide
  // in a CJK context (see CLAUDE.md's 2026-07-30 note on why). Drawing
  // Menlo's narrow, differently-proportioned glyph into a slot every
  // surrounding cell already treats as double-width looks visually
  // inconsistent with its CJK neighbors -- confirmed via
  // TTF_GlyphIsProvided() that Menlo.ttc really does claim these three
  // codepoints, which a real report's persistent (post-item-72)
  // garbling around exactly these characters traces back to.
  unsigned int codepoint = decodeUtf8Codepoint(cell.glyph);
  TTF_Font *renderFont = impl_->font;
  bool fallbackHasIt = glyphProvidedByFont(impl_->fallbackFont, codepoint);
  if ((isWide && fallbackHasIt) || (!glyphProvidedByFont(impl_->font, codepoint) && fallbackHasIt))
    renderFont = impl_->fallbackFont;
  TTF_SetFontStyle(renderFont, style);

  SDL_Surface *surface = TTF_RenderUTF8_Blended(renderFont, cell.glyph.c_str(), fg);
  if (!surface) return;
  SDL_Texture *texture = SDL_CreateTextureFromSurface(impl_->renderer, surface);
  if (texture) {
    // Fit to the cell grid (isWide ? 2 : 1 cells) via fitGlyphRect()
    // -- see its own comment for why this shrinks-and-centers instead
    // of stretching to the fallback font's (mismatched) natural size.
    int maxW = isWide ? impl_->cellWidth * 2 : impl_->cellWidth;
    SDL_Rect dst = fitGlyphRect(box.x, box.y, maxW, impl_->cellHeight, surface->w, surface->h);
    SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
  }
  SDL_FreeSurface(surface);
}

// Draws a visible text cursor at (impl_->visibleCursorRow,
// impl_->visibleCursorCol) -- the LOGICAL cursor position set by
// setVisibleCursor(), NOT impl_->cursorRow/cursorCol (which is purely
// putByte()'s own "where does the next glyph go" bookkeeping, moved
// around by every incidental write -- see Display::
// setVisibleCursor()'s own comment for the full story on why these
// two had to be split apart).
//
// A first version of this method (2026-08-04) read impl_->cursorRow/
// cursorCol directly, reasoning that -c's real hardware cursor already
// visibly lands wherever the last moveCursor() call left it, so -g
// should just match that. That held for -c's own event-driven run
// loop (one input processed per iteration, nothing else touches the
// screen afterward) but not for -g's: runGuiPhase() polls at a steady
// ~60Hz and, within the SAME frame a caller like Shell's ^P/^N history
// browsing explicitly moved the cursor to a specific line, its own
// tail-end unconditionally re-runs a full ContentView::redraw() (whose
// internal per-row/mode-line moveCursor() calls are pure glyph-
// placement, landing wherever the mode-line text last ended) --
// silently dragging the "cursor" back to the bottom row a frame later.
// A real 2026-08-04 user report described exactly this: "-gのSHELLでは
// プロンプトと同じく一瞬出て消えるパターン...モードラインの行数は
// 追随するも位置は最下行のまま". Reading a SEPARATE piece of state
// that only setVisibleCursor() ever touches means incidental redraws
// physically cannot move it, regardless of a particular frame's call
// ordering -- the same "XOR block cursor kept as its own routine,
// decoupled from the main redraw" idea the user described from their
// own past GUI work (cb), adapted to reuse this port's authoritative
// Cell grid instead of needing true pixel XOR: erasing/redrawing the
// cursor is just re-rendering that one cell normally from Cell state,
// which a literal raster XOR toggle only ever approximated.
//
// Renders as a solid block in the foreground color (matching a real
// terminal's default block-cursor look) with whatever glyph already
// occupies that cell redrawn on top in the background color, so text
// under the cursor stays legible instead of being hidden -- the same
// idea Cell::standout/renderCell() already use for reverse-video, just
// forced on for this one cell regardless of its own standout state
// (a cursor should always read as "the cursor", even sitting on top of
// the mode-line's own already-reverse-video text).
void GraphicalDisplay::renderCursor() {
  if (!impl_->outputEnabled) return;
  if (!impl_->hasVisibleCursor) return;
  int row = impl_->visibleCursorRow;
  int col = impl_->visibleCursorCol;
  if (row < 0 || row >= impl_->rows || col < 0 || col >= impl_->columns) return;

  // If the cursor landed on the "shadow" second cell of a wide (CJK)
  // glyph, draw over the glyph's actual home cell instead and span
  // both columns -- matches how a real terminal's block cursor covers
  // a whole wide character rather than looking like it clipped it in
  // half.
  int homeCol = col;
  if (impl_->cellAt(row, col).continuation && col > 0) homeCol = col - 1;
  bool isWide = (homeCol + 1 < impl_->columns) && impl_->cellAt(row, homeCol + 1).continuation;

  SDL_Rect box{Impl::kMarginX + homeCol * impl_->cellWidth, Impl::kMarginY + row * impl_->cellHeight,
               impl_->cellWidth * (isWide ? 2 : 1), impl_->cellHeight};
  SDL_SetRenderDrawColor(impl_->renderer, impl_->fg.r, impl_->fg.g, impl_->fg.b, 255);
  SDL_RenderFillRect(impl_->renderer, &box);

  const Cell &cell = impl_->cellAt(row, homeCol);
  if (cell.glyph.empty() || cell.glyph == " ") return;

  int style = TTF_STYLE_NORMAL;
  if (cell.bold) style |= TTF_STYLE_BOLD;
  if (cell.underline) style |= TTF_STYLE_UNDERLINE;

  // Same primary/fallback-font glyph-coverage decision as renderCell()
  // -- see its own comment.
  unsigned int codepoint = decodeUtf8Codepoint(cell.glyph);
  TTF_Font *renderFont = impl_->font;
  bool fallbackHasIt = glyphProvidedByFont(impl_->fallbackFont, codepoint);
  if ((isWide && fallbackHasIt) || (!glyphProvidedByFont(impl_->font, codepoint) && fallbackHasIt))
    renderFont = impl_->fallbackFont;
  TTF_SetFontStyle(renderFont, style);

  SDL_Surface *surface = TTF_RenderUTF8_Blended(renderFont, cell.glyph.c_str(), impl_->bg);
  if (!surface) return;
  SDL_Texture *texture = SDL_CreateTextureFromSurface(impl_->renderer, surface);
  if (texture) {
    int maxW = isWide ? impl_->cellWidth * 2 : impl_->cellWidth;
    SDL_Rect dst = fitGlyphRect(box.x, box.y, maxW, impl_->cellHeight, surface->w, surface->h);
    SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
  }
  SDL_FreeSurface(surface);
}

void GraphicalDisplay::flush() {
  if (!impl_->outputEnabled) return;
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      std::fprintf(f, "---flush---\n");
      for (int row = 0; row < impl_->rows; row++) {
        std::string line;
        for (int col = 0; col < impl_->columns; col++) {
          const Cell &cell = impl_->cellAt(row, col);
          if (cell.continuation) line += "_";
          else if (cell.glyph.empty()) line += ".";
          else line += "[" + cell.glyph + "]";
        }
        std::fprintf(f, "%d: %s\n", row, line.c_str());
      }
      std::fclose(f);
    }
  }
  SDL_SetRenderDrawColor(impl_->renderer, impl_->bg.r, impl_->bg.g, impl_->bg.b, 255);
  SDL_RenderClear(impl_->renderer);
  for (int row = 0; row < impl_->rows; row++)
    for (int col = 0; col < impl_->columns; col++) renderCellBackground(row, col);
  for (int row = 0; row < impl_->rows; row++)
    for (int col = 0; col < impl_->columns; col++) renderCell(row, col);
  renderCursor();
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    // Same env var as the grid dump above: also save the actual
    // rendered pixels (read back from the renderer itself, not a
    // screen capture -- works without Screen Recording permission and
    // without a real visible window) as "<value>.bmp", overwritten
    // each flush() so it always holds the latest frame. This is what
    // let a real rendering bug (renderCellBackground()'s continuation-
    // cell fill erasing the right half of wide glyphs, see its own
    // comment) get confirmed from within this sandbox at all, since
    // screencapture itself was unavailable here.
    // Sized from the renderer's own OUTPUT (physical pixels, which can
    // now be larger than columns*cellWidth/rows*cellHeight's logical
    // POINTS since SDL_WINDOW_ALLOW_HIGHDPI -- see create()'s own
    // comment) rather than the logical cell grid: SDL_RenderReadPixels()
    // reads the actual drawable, and a destination surface sized for
    // the smaller logical dimensions would overflow/truncate against
    // its own pitch on a HiDPI display.
    int outputWidthPixels = 0, outputHeightPixels = 0;
    SDL_GetRendererOutputSize(impl_->renderer, &outputWidthPixels, &outputHeightPixels);
    SDL_Surface *shot =
        SDL_CreateRGBSurfaceWithFormat(0, outputWidthPixels, outputHeightPixels, 32, SDL_PIXELFORMAT_RGBA32);
    if (shot) {
      if (SDL_RenderReadPixels(impl_->renderer, nullptr, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0)
        SDL_SaveBMP(shot, (std::string(dbg) + ".bmp").c_str());
      SDL_FreeSurface(shot);
    }
  }
  SDL_RenderPresent(impl_->renderer);
}

void GraphicalDisplay::moveCursor(int row, int col) {
  impl_->cursorRow = row;
  impl_->cursorCol = col;
}

void GraphicalDisplay::setVisibleCursor(int row, int col) {
  impl_->hasVisibleCursor = true;
  impl_->visibleCursorRow = row;
  impl_->visibleCursorCol = col;
}

void GraphicalDisplay::hideVisibleCursor() { impl_->hasVisibleCursor = false; }

void GraphicalDisplay::cursorLeft() {
  if (impl_->cursorCol > 0) impl_->cursorCol--;
}

void GraphicalDisplay::saveCursor() {
  impl_->savedRow = impl_->cursorRow;
  impl_->savedCol = impl_->cursorCol;
}

void GraphicalDisplay::restoreCursor() {
  impl_->cursorRow = impl_->savedRow;
  impl_->cursorCol = impl_->savedCol;
}

void GraphicalDisplay::clearScreen() { impl_->grid.assign(impl_->grid.size(), Cell{}); }

void GraphicalDisplay::clearToEndOfLine() {
  if (impl_->cursorRow < 0 || impl_->cursorRow >= impl_->rows) return;
  for (int col = std::max(impl_->cursorCol, 0); col < impl_->columns; col++) impl_->cellAt(impl_->cursorRow, col) = Cell{};
}

void GraphicalDisplay::scrollForward() {
  for (int row = 0; row < impl_->rows - 1; row++)
    for (int col = 0; col < impl_->columns; col++) impl_->cellAt(row, col) = impl_->cellAt(row + 1, col);
  for (int col = 0; col < impl_->columns; col++) impl_->cellAt(impl_->rows - 1, col) = Cell{};
}

void GraphicalDisplay::scrollReverse() {
  for (int row = impl_->rows - 1; row > 0; row--)
    for (int col = 0; col < impl_->columns; col++) impl_->cellAt(row, col) = impl_->cellAt(row - 1, col);
  for (int col = 0; col < impl_->columns; col++) impl_->cellAt(0, col) = Cell{};
}

void GraphicalDisplay::setAttribute(Attribute attr) {
  switch (attr) {
    case Attribute::Underline:
      impl_->underlineActive = true;
      break;
    case Attribute::UnderlineOff:
      impl_->underlineActive = false;
      break;
    case Attribute::Bold:
      impl_->boldActive = true;
      break;
    case Attribute::BoldOff:
      impl_->boldActive = false;
      break;
    case Attribute::Standout:
      impl_->standoutActive = true;
      break;
    case Attribute::StandoutOff:
      impl_->standoutActive = false;
      break;
  }
}

// No real escape-sequence concept exists for a pixel-rendered window
// (legacy's only use was splicing raw termcap codes into a bash PS1
// string so bash itself would print them -- meaningless without a
// real terminal interpreting them), so this is always empty -- a
// documented, flagged gap (Shell's prompt loses its bold markers
// while GUI mode is active), not a silent omission.
std::string GraphicalDisplay::attributeEscape(Attribute) const { return ""; }

int GraphicalDisplay::rows() const { return impl_->rows; }
int GraphicalDisplay::columns() const { return impl_->columns; }

void GraphicalDisplay::setSize(int rows, int columns) {
  if (rows <= 0 || columns <= 0) return;
  impl_->rows = rows;
  impl_->columns = columns;
  impl_->grid.assign(static_cast<std::size_t>(rows) * columns, Cell{});
  if (impl_->window)
    SDL_SetWindowSize(impl_->window, impl_->cellWidth * columns + Impl::kMarginX * 2 + Impl::kScrollbarWidth,
                       impl_->cellHeight * rows + Impl::kMarginY * 2);
}

std::string GraphicalDisplay::keySequence(Key) const { return ""; }

bool GraphicalDisplay::outputEnabled() const { return impl_->outputEnabled; }
void GraphicalDisplay::setOutputEnabled(bool enabled) { impl_->outputEnabled = enabled; }

bool GraphicalDisplay::smoothScrollEnabled() const { return impl_->smoothScroll; }
void GraphicalDisplay::setSmoothScrollEnabled(bool enabled) { impl_->smoothScroll = enabled; }

void GraphicalDisplay::open() {}   // the window is already shown by create(); nothing further to do
void GraphicalDisplay::close() {}  // torn down by the destructor instead, see its own comment

namespace {

std::optional<Display::Key> translateScancode(SDL_Keycode key) {
  switch (key) {
    case SDLK_UP:
      return Display::Key::Up;
    case SDLK_DOWN:
      return Display::Key::Down;
    case SDLK_LEFT:
      return Display::Key::Left;
    case SDLK_RIGHT:
      return Display::Key::Right;
    case SDLK_PAGEUP:
      return Display::Key::PageUp;
    case SDLK_PAGEDOWN:
      return Display::Key::PageDown;
    default:
      return std::nullopt;
  }
}

}  // namespace

bool GraphicalDisplay::pumpEvents() {
  SDL_Event event;
  bool stillOpen = true;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        stillOpen = false;
        break;
      case SDL_MOUSEBUTTONDOWN: {
        if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) break;
        // max(0, ...) before dividing: a click that lands within the
        // margin border itself (see kMarginX/kMarginY's own comment)
        // would otherwise subtract past zero and, on most platforms,
        // truncate toward zero rather than floor -- clamping treats
        // the whole margin as "the nearest edge cell" instead of
        // wrapping to a bogus row/column.
        int col = std::max(0, event.button.x - Impl::kMarginX) / std::max(impl_->cellWidth, 1);
        int row = std::max(0, event.button.y - Impl::kMarginY) / std::max(impl_->cellHeight, 1);
        impl_->mouseQueue.push_back(MouseClick{row, col, event.button.button == SDL_BUTTON_RIGHT});
        if (event.button.button == SDL_BUTTON_LEFT) {
          SDL_CaptureMouse(SDL_TRUE);  // keep tracking motion even if the drag leaves the window
          impl_->dragQueue.push_back(DragEvent{DragEventType::ButtonDown, 0});
        }
        break;
      }
      case SDL_MOUSEBUTTONUP: {
        if (event.button.button == SDL_BUTTON_LEFT) {
          SDL_CaptureMouse(SDL_FALSE);
          impl_->dragQueue.push_back(DragEvent{DragEventType::ButtonUp, 0});
        }
        break;
      }
      case SDL_MOUSEMOTION: {
        if (event.motion.state & SDL_BUTTON_LMASK) {
          impl_->dragQueue.push_back(DragEvent{DragEventType::Motion, event.motion.yrel});
        } else {
          int col = std::max(0, event.motion.x - Impl::kMarginX) / std::max(impl_->cellWidth, 1);
          int row = std::max(0, event.motion.y - Impl::kMarginY) / std::max(impl_->cellHeight, 1);
          impl_->hoverQueue.push_back(HoverEvent{row, col});
        }
        break;
      }
      case SDL_KEYDOWN: {
        if (auto special = translateScancode(event.key.keysym.sym)) {
          impl_->keyQueue.push_back(KeyEvent{0, special});
          break;
        }
        // Control-key combos (Ctrl+A..Ctrl+_) arrive as SDLK_a.. with
        // KMOD_CTRL set, not as SDL_TEXTINPUT -- translate those here;
        // everything else (plain characters, including multi-byte
        // UTF-8 via an input method) arrives via SDL_TEXTINPUT below
        // instead, matching how a real terminal separates control
        // bytes from printable text.
        if (event.key.keysym.mod & KMOD_CTRL) {
          SDL_Keycode sym = event.key.keysym.sym;
          if (sym >= SDLK_a && sym <= SDLK_z) {
            impl_->keyQueue.push_back(KeyEvent{(sym - SDLK_a) + 1, std::nullopt});
          } else if (sym == SDLK_LEFTBRACKET) {
            impl_->keyQueue.push_back(KeyEvent{0x1b, std::nullopt});  // Ctrl+[ == ESC
          }
          break;
        }
        if (event.key.keysym.sym == SDLK_RETURN) impl_->keyQueue.push_back(KeyEvent{0x0d, std::nullopt});
        else if (event.key.keysym.sym == SDLK_BACKSPACE) impl_->keyQueue.push_back(KeyEvent{0x7f, std::nullopt});
        else if (event.key.keysym.sym == SDLK_TAB) impl_->keyQueue.push_back(KeyEvent{0x09, std::nullopt});
        else if (event.key.keysym.sym == SDLK_ESCAPE) impl_->keyQueue.push_back(KeyEvent{0x1b, std::nullopt});
        break;
      }
      case SDL_TEXTINPUT: {
        for (const char *p = event.text.text; *p; p++) impl_->keyQueue.push_back(KeyEvent{static_cast<unsigned char>(*p), std::nullopt});
        break;
      }
      case SDL_MOUSEWHEEL: {
        // SDL_MOUSEWHEEL_FLIPPED means the OS-level "natural scrolling"
        // preference is on (confirmed default on macOS trackpads) --
        // SDL reports the raw hardware delta either way and leaves
        // sign correction to the caller, so this negation is required,
        // not optional, for wheel direction to match what the user's
        // own OS scroll-direction setting says "up" means.
        int deltaY = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) deltaY = -deltaY;
        if (deltaY != 0) impl_->wheelQueue.push_back(WheelEvent{deltaY});
        break;
      }
      default:
        break;
    }
  }
  return stillOpen;
}

std::optional<GraphicalDisplay::MouseClick> GraphicalDisplay::consumeMouseClick() {
  if (impl_->mouseQueue.empty()) return std::nullopt;
  MouseClick click = impl_->mouseQueue.front();
  impl_->mouseQueue.pop_front();
  return click;
}

std::optional<GraphicalDisplay::HoverEvent> GraphicalDisplay::consumeHoverEvent() {
  if (impl_->hoverQueue.empty()) return std::nullopt;
  HoverEvent hover = impl_->hoverQueue.front();
  impl_->hoverQueue.pop_front();
  return hover;
}

std::optional<GraphicalDisplay::KeyEvent> GraphicalDisplay::consumeKeyEvent() {
  if (impl_->keyQueue.empty()) return std::nullopt;
  KeyEvent event = impl_->keyQueue.front();
  impl_->keyQueue.pop_front();
  return event;
}

std::optional<GraphicalDisplay::DragEvent> GraphicalDisplay::consumeDragEvent() {
  if (impl_->dragQueue.empty()) return std::nullopt;
  DragEvent event = impl_->dragQueue.front();
  impl_->dragQueue.pop_front();
  return event;
}

std::optional<GraphicalDisplay::WheelEvent> GraphicalDisplay::consumeWheelEvent() {
  if (impl_->wheelQueue.empty()) return std::nullopt;
  WheelEvent event = impl_->wheelQueue.front();
  impl_->wheelQueue.pop_front();
  return event;
}

int GraphicalDisplay::cellHeightPixels() const { return impl_->cellHeight; }

void GraphicalDisplay::setColors(Color foreground, Color background) {
  impl_->fg = SDL_Color{foreground.r, foreground.g, foreground.b, 255};
  impl_->bg = SDL_Color{background.r, background.g, background.b, 255};
}

void GraphicalDisplay::setWindowPosition(int x, int y) {
  if (impl_->window) SDL_SetWindowPosition(impl_->window, x, y);
}

void GraphicalDisplay::setHandCursor(bool active) {
  if (active == impl_->handCursorActive) return;
  impl_->handCursorActive = active;
  if (active) {
    if (!impl_->handCursor) impl_->handCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    if (impl_->handCursor) SDL_SetCursor(impl_->handCursor);
  } else {
    SDL_SetCursor(SDL_GetDefaultCursor());
  }
}

// Draws one already-UTF-8-decoded-per-glyph line of text starting at
// pixel position (0, y), used by both renderScrolled()'s per-content-
// line loop and its mode-line row below -- factored out so the mode-
// line (added 2026-07-31) doesn't need a third copy of the glyph-walk/
// font-fallback/fitGlyphRect() dance renderCell() already has its own
// (necessarily separate, see renderScrolled()'s own class comment on
// why this whole path is self-contained).
void GraphicalDisplay::renderTextRun(int y, const std::string &line, Color fg) {
  SDL_Color sdlFg{fg.r, fg.g, fg.b, 255};
  int x = 0;
  std::size_t pos = 0;
  while (pos < line.size() && x < impl_->columns) {
    unsigned char lead = static_cast<unsigned char>(line[pos]);
    int len = Utf8::sequence_length(lead);
    if (pos + len > line.size()) len = 1;
    int width = std::max(1, Utf8::display_width(reinterpret_cast<const unsigned char *>(line.data() + pos), len));
    std::string glyph = line.substr(pos, len);
    if (glyph != " " && lead >= 0x20) {
      // See renderCell()'s own comment on why a fallback font is
      // consulted here too.
      TTF_Font *renderFont = impl_->font;
      unsigned int codepoint = decodeUtf8Codepoint(glyph);
      if (!glyphProvidedByFont(impl_->font, codepoint) && glyphProvidedByFont(impl_->fallbackFont, codepoint))
        renderFont = impl_->fallbackFont;
      SDL_Surface *surface = TTF_RenderUTF8_Blended(renderFont, glyph.c_str(), sdlFg);
      if (surface) {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(impl_->renderer, surface);
        if (texture) {
          // Fit to `width` cells via fitGlyphRect() -- see
          // renderCell()'s own comment on why this shrinks-and-
          // centers instead of stretching.
          SDL_Rect dst = fitGlyphRect(Impl::kMarginX + x * impl_->cellWidth, Impl::kMarginY + y,
                                       width * impl_->cellWidth, impl_->cellHeight, surface->w, surface->h);
          SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
          SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
      }
    }
    x += width;
    pos += len;
  }
}

// First slice of the scrollbar overlay (docs/HISTORY.md line 521 /
// IDEAS.md's 2026-08-03 entry) -- only shown WHILE actively dragging/
// coasting, since this is only ever called from renderScrolled() (the
// scroll-specific render path); the static (non-scrolling) case needs
// a Display-interface extension, see this function's own caller
// comment. This matches -c's own reference behavior too, confirmed
// from a real screenshot (2026-08-07): a static, non-scrolling -c
// window shows no scrollbar at all either.
//
// Visual style went through two real-screenshot corrections
// (2026-08-07): the first version tinted track+thumb with impl_->fg
// (the terminal theme's own foreground), which on this green-on-black
// theme read as a solid saturated-green block even at low alpha. The
// SECOND version dropped the track entirely and used a thin
// translucent neutral-gray thumb only, guessing at macOS's modern
// "overlay" scrollbar style -- but a tightly-cropped screenshot of
// -c's own actual scrollbar (while scrolling) showed something
// different: a classic, fully OPAQUE light-gray track with a solid
// medium-gray rounded thumb, not a translucent overlay. This version
// matches that reference directly instead of a generic "what macOS
// scrollbars usually look like" assumption.
void GraphicalDisplay::drawScrollbar(double effectiveTop, int totalLines, int contentRows) {
  if (contentRows <= 0) return;
  int trackHeight = contentRows * impl_->cellHeight;
  int trackX = Impl::kMarginX + impl_->columns * impl_->cellWidth;

  SDL_Rect track{trackX, Impl::kMarginY, Impl::kScrollbarWidth, trackHeight};
  SDL_SetRenderDrawColor(impl_->renderer, 235, 235, 235, 255);
  SDL_RenderFillRect(impl_->renderer, &track);
  // Thin darker seam on the track's own left edge -- the reference
  // screenshot shows a subtle boundary between the content area and
  // the track, not a hard flat merge into it.
  SDL_SetRenderDrawColor(impl_->renderer, 200, 200, 200, 255);
  SDL_RenderDrawLine(impl_->renderer, trackX, Impl::kMarginY, trackX, Impl::kMarginY + trackHeight - 1);

  // totalLines <= contentRows means the whole document already fits
  // on screen at once -- the thumb fills the entire track (nothing to
  // scroll to), matching a real scrollbar's own convention rather
  // than dividing by a non-positive range below.
  int scrollableLines = std::max(totalLines - contentRows, 0);
  double visibleFraction = (totalLines > 0) ? std::min(1.0, static_cast<double>(contentRows) / totalLines) : 1.0;
  int thumbHeight = std::max(static_cast<int>(trackHeight * visibleFraction), std::min(trackHeight, 24));
  int maxThumbY = trackHeight - thumbHeight;
  int thumbY = (scrollableLines > 0)
                   ? std::clamp(static_cast<int>(maxThumbY * (effectiveTop / scrollableLines)), 0, maxThumbY)
                   : 0;
  constexpr int kThumbInset = 3;  // margin on each side within the reserved strip
  int thumbX = trackX + kThumbInset;
  int thumbWidth = std::max(Impl::kScrollbarWidth - kThumbInset * 2, 2);
  SDL_Rect thumb{thumbX, Impl::kMarginY + thumbY, thumbWidth, thumbHeight};
  SDL_SetRenderDrawColor(impl_->renderer, 150, 150, 150, 255);
  SDL_RenderFillRect(impl_->renderer, &thumb);
  // Rough rounded-corner approximation (SDL2 has no native rounded-
  // rect fill primitive): punch the 4 corner pixels back to the
  // track's own color so the thumb reads as a softly rounded pill
  // rather than a sharp-cornered block, matching the reference.
  SDL_SetRenderDrawColor(impl_->renderer, 235, 235, 235, 255);
  SDL_RenderDrawPoint(impl_->renderer, thumb.x, thumb.y);
  SDL_RenderDrawPoint(impl_->renderer, thumb.x + thumb.w - 1, thumb.y);
  SDL_RenderDrawPoint(impl_->renderer, thumb.x, thumb.y + thumb.h - 1);
  SDL_RenderDrawPoint(impl_->renderer, thumb.x + thumb.w - 1, thumb.y + thumb.h - 1);
}

// Drag-scroll prototype rendering -- see class comment and header
// comment on why this is a self-contained path, not layered on
// write()/putChar()/moveCursor(). Renders each of `lines` (already
// UTF-8-decoded per-glyph via the same fm::Utf8 helpers putByte()
// uses) at its own computed pixel Y position, offset by pixelOffsetY,
// including ONE extra row beyond whichever edge is being revealed.
//
// The bottom row is reserved for `modeLineText` (ContentView's own
// "current/total NN%" formatting, see ModeLine) and is NEVER part of
// the scrolled content -- content only ever occupies impl_->rows - 1
// rows here (rowCount below is sized accordingly), matching every
// ContentView's own viewport height (Application::resize()'s
// "contentHeight = rows()-1"). Before this, the content loop spanned
// the FULL window height, so dragging/coasting scrolled straight
// through where the mode-line belongs (a known gap noted in CLAUDE.md
// well before the mode-line existed to collide with). The mode-line
// itself is redrawn fresh every call (not offset by pixelOffsetY --
// it's a fixed row) so it live-updates during a drag/coast, which was
// the ORIGINAL motivation for wanting mode-line support at all (see
// CLAUDE.md's 2026-07-30 notes on reflecting scroll state there).
void GraphicalDisplay::renderScrolled(const std::vector<std::string> &lines, int topIndex, int pixelOffsetY,
                                       const std::string &modeLineText) {
  if (!impl_->outputEnabled) return;
  SDL_SetRenderDrawColor(impl_->renderer, impl_->bg.r, impl_->bg.g, impl_->bg.b, 255);
  SDL_RenderClear(impl_->renderer);

  TTF_SetFontStyle(impl_->font, TTF_STYLE_NORMAL);
  if (impl_->fallbackFont) TTF_SetFontStyle(impl_->fallbackFont, TTF_STYLE_NORMAL);

  int contentRows = std::max(impl_->rows - 1, 0);

  // Clip content rendering strictly to the margin boundary (2026-08-07,
  // a real user report): without this, SDL's default clip is the
  // whole render target, so a partially-scrolled row's glyphs (which
  // can legitimately extend to negative y or past the content/mode-
  // line boundary mid-scroll, see the loop below) rendered straight
  // through the thin kMarginY border with no stable edge -- the user
  // initially read this as "the top row disappears too early" before
  // realizing, comparing side-by-side against the real terminal, that
  // what they were actually seeing was content bleeding INTO what
  // should have been a fixed, never-scrolled margin, with no visible
  // boundary to signal that. Setting an explicit clip rect here makes
  // the margin a real, stable edge -- content is now guaranteed to
  // never render above/left of it or past the content/mode-line
  // boundary, regardless of pixelOffsetY.
  SDL_Rect contentClip{Impl::kMarginX, Impl::kMarginY, impl_->columns * impl_->cellWidth,
                        contentRows * impl_->cellHeight};
  SDL_RenderSetClipRect(impl_->renderer, &contentClip);

  // One extra row above when revealing upward (pixelOffsetY > 0,
  // content shifting down), one extra row below when revealing
  // downward (pixelOffsetY < 0, content shifting up) -- see header
  // comment for the sign convention.
  int firstRow = (pixelOffsetY > 0) ? topIndex - 1 : topIndex;
  int rowCount = contentRows + 1;

  for (int i = 0; i < rowCount; i++) {
    int lineIndex = firstRow + i;
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) continue;
    int y = i * impl_->cellHeight + pixelOffsetY - ((pixelOffsetY > 0) ? impl_->cellHeight : 0);
    if (y <= -impl_->cellHeight || y >= contentRows * impl_->cellHeight) continue;  // fully off-screen (or into the mode-line row)
    renderTextRun(y, lines[lineIndex], Color{impl_->fg.r, impl_->fg.g, impl_->fg.b});
  }

  SDL_RenderSetClipRect(impl_->renderer, nullptr);  // mode-line row below (and any future margin decoration) isn't bound by this

  // See drawScrollbar()'s own comment -- effectiveTop is topIndex
  // adjusted by the current sub-row pixelOffsetY so the thumb tracks
  // smoothly instead of jumping once per full row crossing.
  double effectiveTop = topIndex - static_cast<double>(pixelOffsetY) / std::max(impl_->cellHeight, 1);
  drawScrollbar(effectiveTop, static_cast<int>(lines.size()), contentRows);

  if (impl_->rows > 0) {
    int modeLineY = contentRows * impl_->cellHeight;
    SDL_Rect rowBox{Impl::kMarginX, Impl::kMarginY + modeLineY, impl_->columns * impl_->cellWidth,
                     impl_->cellHeight};
    SDL_SetRenderDrawColor(impl_->renderer, impl_->fg.r, impl_->fg.g, impl_->fg.b, 255);
    SDL_RenderFillRect(impl_->renderer, &rowBox);
    renderTextRun(modeLineY, modeLineText, Color{impl_->bg.r, impl_->bg.g, impl_->bg.b});
  }

  SDL_RenderPresent(impl_->renderer);
}

}  // namespace fm
