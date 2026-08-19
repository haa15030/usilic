#include "graphical_display.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "scrollbar_idle_image.hpp"
#include "mac_cursor.hpp"
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

// The inverse of decodeUtf8Codepoint() -- needed by
// composeDakutenCodepoint() below (composeGraphemeCluster()'s own
// caller re-encodes the composed scalar back into a cell's `glyph`
// string). Every codepoint this port ever composes this way is <=
// 0xFFFF (BMP-only Hiragana/Katakana), so the 4-byte case is dead in
// practice but included for completeness/symmetry with the decoder.
std::string encodeUtf8Codepoint(unsigned int codepoint) {
  std::string out;
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return out;
}

// 2026-08-20 addition, a direct user follow-up after the U+3099/U+309A
// display_width() fix ("=dsよりはマシだが=dvには負ける...自前描画なら
// 何とかなる？"): merging a combining dakuten/handakuten mark into the
// PREVIOUS cell's `glyph` string as raw, still-decomposed UTF-8 bytes
// (commitGlyph()'s existing width==0 branch) makes that cell hold a
// TWO-codepoint string ("ト"+U+3099), which TTF_RenderUTF8_Blended()
// then has to shape correctly for it to look like one fused "ド" --
// but SDL_ttf/FreeType do simple linear glyph layout, not real Unicode
// text shaping (no GPOS mark-attachment positioning the way CoreText,
// -c's own real terminal emulator, already does), so the mark's own
// advance/position isn't guaranteed to land exactly on top of the base
// glyph. Since -g owns its rendering entirely (unlike -c, which just
// hands raw bytes to a real shaping engine), the fix available here is
// to sidestep shaping altogether: precompose the pair into its single
// NFC codepoint (e.g. "ト"+U+3099 -> "ド" U+30C9) BEFORE storing it in
// the cell, so the cell ends up holding one perfectly ordinary kana
// character with normal glyph metrics -- rendered exactly like any
// other single character, no combining-mark shaping involved at all.
// A closed, unchanging table (every Hiragana/Katakana dakuten/
// handakuten pair Unicode defines a canonical NFC composition for --
// this is NOT general Unicode normalization, just this one finite,
// well-known correspondence) rather than pulling in a full
// normalization library for one narrow case. Falls back to the OLD
// raw-append behavior (returns 0, caller keeps its prior behavior) for
// any base/mark combination outside this table -- still zero-width
// per display_width(), just not something this port knows how to
// precompose, so the decomposed form (imperfect shaping, but not
// visibly a separate glyph either, per Utf8::display_width()'s own
// 0-width classification) is the best available fallback.
unsigned int composeDakutenCodepoint(unsigned int base, unsigned int mark) {
  if (mark != 0x3099 && mark != 0x309a) return 0;
  bool handakuten = mark == 0x309a;
  // う/ウ -> ゔ/ヴ (dakuten only; no handakuten form exists)
  if (!handakuten && base == 0x3046) return 0x3094;
  if (!handakuten && base == 0x30a6) return 0x30f4;
  // か-gyou through と-gyou (dakuten only; no handakuten forms exist),
  // Hiragana then Katakana (Katakana codepoints are a fixed +0x60
  // offset from their Hiragana counterparts throughout this range,
  // consistent enough to check post-offset below rather than list
  // every Katakana codepoint a second time).
  if (!handakuten) {
    static const unsigned int kDakutenOnlyBases[] = {0x304b, 0x304d, 0x304f, 0x3051, 0x3053, 0x3055, 0x3057,
                                                       0x3059, 0x305b, 0x305d, 0x305f, 0x3061, 0x3064, 0x3066,
                                                       0x3068};
    for (unsigned int hiraganaBase : kDakutenOnlyBases) {
      if (base == hiraganaBase || base == hiraganaBase + 0x60) return base + 1;
    }
  }
  // は-gyou (both dakuten AND handakuten forms exist), Hiragana then
  // Katakana (same +0x60 offset).
  static const unsigned int kHaGyouBases[] = {0x306f, 0x3072, 0x3075, 0x3078, 0x307b};
  for (unsigned int hiraganaBase : kHaGyouBases) {
    if (base == hiraganaBase || base == hiraganaBase + 0x60) return base + (handakuten ? 2 : 1);
  }
  // Rare/obsolete Katakana (dakuten only): ワヰヱヲ -> ヷヸヹヺ, and the
  // Katakana/Hiragana iteration marks ヽ/ゝ -> ヾ/ゞ. NOT a fixed
  // offset from their base (outside the contiguous block the other
  // cases share), so listed explicitly.
  if (!handakuten) {
    switch (base) {
      case 0x30ef: return 0x30f7;  // ワ -> ヷ
      case 0x30f0: return 0x30f8;  // ヰ -> ヸ
      case 0x30f1: return 0x30f9;  // ヱ -> ヹ
      case 0x30f2: return 0x30fa;  // ヲ -> ヺ
      case 0x30fd: return 0x30fe;  // ヽ -> ヾ
      case 0x309d: return 0x309e;  // ゝ -> ゞ
      default: break;
    }
  }
  return 0;
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

// 2026-08-11: darkens a freshly-rendered glyph surface's partially-
// covered (anti-aliased edge) pixels, to make thin strokes read as
// visually heavier without touching the font's hinting or style --
// item 264's TTF_HINTING_NONE experiment turned out to be moot for
// "SF Mono Regular" specifically: inspecting the font file directly
// (fontTools) showed it has NO fpgm/prep tables and 0 bytes of
// instructions across every glyph, so there was never a native hint
// program for TTF_HINTING_NORMAL/LIGHT/NONE to select between in the
// first place -- consistent with a real report that toggling hinting
// mode produced no visible change for this font at all.
//
// TTF_RenderUTF8_Blended() returns a surface whose RGB is the flat
// foreground color everywhere; only the alpha channel varies, encoding
// how much of each pixel the glyph outline covers (0 = background,
// 255 = fully inside the stroke, partial values = the antialiased
// edge). Raising alpha through a gamma curve (alpha' = 255 *
// (alpha/255)^gamma, gamma < 1) pushes partially-covered edge pixels
// toward fully-opaque, which is the same idea professional rasterizers
// call "stem darkening" -- a deliberate boost to compensate for
// unhinted small text reading as too thin, applied as a pixel-level
// post-process instead of relying on any hint program (native or
// FreeType's autohinter), so it works identically regardless of
// whether the font has one.
void boostGlyphAlpha(SDL_Surface *surface, double gamma) {
  if (!surface) return;
  if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) return;
  Uint32 *pixels = static_cast<Uint32 *>(surface->pixels);
  int count = surface->w * surface->h;
  for (int i = 0; i < count; ++i) {
    Uint8 r, g, b, a;
    SDL_GetRGBA(pixels[i], surface->format, &r, &g, &b, &a);
    if (a == 0 || a == 255) continue;
    double boosted = 255.0 * std::pow(a / 255.0, gamma);
    Uint8 newAlpha = static_cast<Uint8>(std::min(255.0, std::max(0.0, boosted)));
    pixels[i] = SDL_MapRGBA(surface->format, r, g, b, newAlpha);
  }
  if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
}

// Gamma < 1 boosts partial-coverage alpha upward (darkens); picked by
// eye as a starting point, not measured against a specific target --
// see boostGlyphAlpha()'s own comment.
constexpr double kNarrowGlyphAlphaGamma = 0.65;

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
  // The font's own TRUE, never-overwritten native metrics -- captured
  // ONCE in create(), from the SAME measurement create()'s own
  // cellWidth/cellHeight locals are seeded from (before matchWindowSize()
  // ever runs and starts overwriting the ABOVE cellWidth/cellHeight with
  // the real terminal's own arithmetic-derived target values instead).
  // Added 2026-08-10 alongside matchWindowSize()'s own font-rescaling
  // (see its own comment) specifically so that method can always
  // compute "how far has the target size drifted from the font's
  // ORIGINAL native size" from a stable baseline, even across multiple
  // calls (a live font-size change mid-session, `^Og` re-entry, etc.)
  // -- reading the ABOVE cellWidth/cellHeight for this instead would
  // compound: after the FIRST matchWindowSize() call already replaced
  // them with a target value, a SECOND call would then be scaling
  // relative to the PREVIOUS target, not the font's real original size,
  // drifting further off with each call.
  int baseFontPointSize = 16;
  int baseFontMetricCellWidth = 0, baseFontMetricCellHeight = 0;
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
  // this value was originally a rough, unmeasured placeholder (15).
  // Corrected 2026-08-09 to 17: the embedded reference screenshot this
  // project captured early on (scrollbar_idle_image.hpp, since removed
  // -- see updateScrollPosition()'s own comment) was a real -c
  // terminal scrollbar crop measuring exactly 17px wide
  // (kScrollbarIdleImageWidth, that file's own constant); a completely
  // independent same-day measurement (querying `+[NSScroller
  // scrollerWidthForControlSize:scrollerStyle:]` directly, during a
  // later, since-reverted attempt at a real NSScroller widget) returned
  // the SAME 17pt on this machine. Two independent real measurements
  // agreeing is a much stronger basis than the original guess. A real
  // user request (2026-08-09, "今の張りぼて画像は縮小されていて本来は
  // Home brewくらいなので、つまみ幅も同じくらいにして...色々使ってから
  // 正式に決めたい") asked for the thumb width to match that original,
  // un-shrunk reference size -- treated as provisional (their own
  // words), not a final measurement, pending more real-world use.
  static constexpr int kScrollbarWidth = 17;
  // Cached by updateScrollPosition() (called every runGuiPhase()
  // iteration, main.cpp) so flush() can draw a real, always-current
  // thumb via drawScrollbar() instead of the removed decorative
  // placeholder -- see updateScrollPosition()'s own comment.
  int scrollTopIndex = 0, scrollTotalLines = 0, scrollContentRows = 0;
  // False from window creation until the FIRST mouse interaction of
  // any kind (set true in pumpEvents(), never reset back to false) --
  // a real request (2026-08-09, restoring the idle image removed one
  // round earlier: "-g起動後、マウスに触れるまでは画像が必要（一度でも
  // 触れればつまみが永続的に表示され、以後は画像も不要となる）") wants
  // the "実はすでにGUI" illusion's own reasoning applied one level
  // deeper than before: it only matters up to the moment the user
  // actually starts operating the window (a real mouse touch is
  // unambiguous proof of that), not merely "at launch" as the earlier,
  // now-reverted always-image-until-drag version treated it. Before
  // that first touch, flush() pastes the static reference image
  // (indistinguishable from -- because it IS -- a real captured
  // terminal scrollbar); after it, flush() switches permanently to the
  // real, always-current thumb (drawScrollbar()), matching
  // renderScrolled()'s own active-drag rendering.
  bool everInteracted = false;
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
  std::deque<ScrollbarEvent> scrollbarQueue;
  // True from a left-button-down that started inside the scrollbar
  // strip until the matching button-up -- see ScrollbarEvent's own
  // comment. Distinguishes a MOTION event that should be routed to
  // scrollbarQueue (started as a thumb drag) from one that should go
  // to the normal content dragQueue/hoverQueue, since SDL_MOUSEMOTION
  // itself carries no memory of where the ORIGINAL button-down was.
  bool scrollbarDragActive = false;
  // handCursorActive: tracks whether the open-hand cursor (handCursor
  // below) is currently showing, for dragging CONTENT itself (the
  // "cat's paw" scroll gesture). pointingCursor: SDL's own system
  // pointing-hand (index finger), used for scrollbar-thumb dragging --
  // see setPointingCursor()'s own comment. Split into two cursors
  // 2026-08-10 after a real request ("画面ドラッグでは手の形をこれに
  // して欲しい...現状の人差し指は、スクロールバーのドラッグ...のみ
  // での表示よろしく") pointed out that a single "pointing hand for
  // every drag" cursor didn't distinguish the two gestures the way a
  // real trackpad/mouse OS convention does (open hand while panning
  // content, pointing hand only for a clickable control like a
  // scrollbar).
  //
  // handCursor: an SDL_Cursor built (lazily, on first use) from the
  // REAL macOS open-hand cursor's pixels (mac_cursor.hpp's
  // extractOpenHandCursorImage()), driven through SDL_SetCursor() just
  // like pointingCursor below -- NOT a raw [[NSCursor openHandCursor]
  // set] call. An earlier version of this field called that directly
  // and skipped SDL entirely; it displayed correctly on mouse-down but
  // reverted to the plain arrow the instant a drag actually started
  // moving (a real report: "画面ドラッグを開始したとたんに矢印に戻る"),
  // because SDL's own Cocoa backend re-asserts ITS OWN tracked
  // SDL_Cursor on every mouse-motion event, stomping any cursor set
  // through a side channel SDL doesn't know about. Routing it through
  // SDL_SetCursor() like pointingCursor fixes that the same way
  // pointingCursor never had the problem in the first place -- see
  // setHandCursor()'s own comment.
  bool handCursorActive = false;
  SDL_Cursor *handCursor = nullptr;
  SDL_Cursor *pointingCursor = nullptr;
  bool pointingCursorActive = false;

  Cell &cellAt(int row, int col) { return grid[row * columns + col]; }
};

GraphicalDisplay::GraphicalDisplay() : impl_(std::make_unique<Impl>()) {}

std::unique_ptr<GraphicalDisplay> GraphicalDisplay::create(int rows, int columns, const std::string &fontPath,
                                                             int fontPointSize) {
  if (rows <= 0 || columns <= 0) return nullptr;
  // Without this hint, SDL_RenderCopy() scales textures with nearest-
  // neighbor sampling. fitGlyphRect() scales almost every glyph bitmap
  // by some small fractional factor (the font-rendered pixel size
  // essentially never lands exactly on the target cell box), so nearest-
  // neighbor turns FreeType's already-antialiased glyph into visibly
  // blocky/staircased edges -- this is what read as "not vector, thin
  // half-width text" next to -c's smooth CoreText rendering. "linear"
  // makes SDL_RenderCopy's scaling smooth instead.
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
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
  // TTF_SizeUTF8()'s height above is "M"'s own tight glyph bounding
  // box (ascent+descent for THAT specific glyph only) -- narrower than
  // the font's actual recommended LINE height (ascent+|descent|+line
  // gap across the whole font, the metric a real terminal emulator
  // uses for line spacing). A real side-by-side screenshot comparison
  // (2026-08-08, "行間が狭い", -c/-g windows placed next to each
  // other) confirmed this window's line spacing visibly tighter than
  // the real terminal's own. TTF_FontHeight() returns that proper
  // line-height metric directly; take the larger of the two (rather
  // than replacing outright) so an unusual font where TTF_FontHeight()
  // somehow reports smaller than a single glyph's own bounding box
  // still can't clip that glyph.
  cellHeight = std::max(cellHeight, TTF_FontHeight(font));

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

  // Escape hatch if the TTF_FontHeight()-based fix above still doesn't
  // fully close the gap to a real terminal's own line spacing for a
  // particular font (line-height conventions vary more by font/app
  // than the title-bar-height case above did) -- same "tunable via
  // env var instead of another code-and-rebuild round" pattern as
  // USILICTITLEBARHEIGHT/USILICSCROLLBARSCALE. Overrides the fully-
  // computed cellHeight (post CJK-fallback-max) outright when set.
  if (const char *env = std::getenv("USILICLINEHEIGHT")) {
    int parsed = std::atoi(env);
    if (parsed > 0) cellHeight = parsed;
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
  // SDL_WINDOW_HIDDEN, not SDL_WINDOW_SHOWN -- see open()'s own
  // comment for why the window is deliberately left unshown until
  // open() is called, not here.
  SDL_Window *window = SDL_CreateWindow(
      "usilic", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      cellWidth * columns + Impl::kMarginX * 2 + Impl::kScrollbarWidth, cellHeight * rows + Impl::kMarginY * 2,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);
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
  display->impl_->baseFontPointSize = fontPointSize;
  display->impl_->baseFontMetricCellWidth = cellWidth;
  display->impl_->baseFontMetricCellHeight = cellHeight;
  display->impl_->rows = rows;
  display->impl_->columns = columns;
  display->impl_->grid.assign(static_cast<std::size_t>(rows) * columns, Cell{});
  SDL_StartTextInput();
  // Deliberately NOT shown/raised here (a previous version called
  // SDL_RaiseWindow() right after SDL_CreateWindow(SDL_WINDOW_SHOWN,
  // ...) at this exact point) -- see open()'s own comment for why
  // showing the window is now deferred there instead.
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
  if (impl_->pointingCursor) SDL_FreeCursor(impl_->pointingCursor);
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
      // 2026-08-20 fix, found via temporary per-glyph USILICGUIDEBUG
      // tracing added while chasing why the width==0 combining-mark
      // merge in commitGlyph() (both the original 09c6981 fix and this
      // same day's composeDakutenCodepoint() follow-up) never actually
      // fired: this `std::max(width, 1)` clamp was ERASING the 0
      // before commitGlyph() ever saw it -- the trace showed U+3099
      // arriving there as `width=1`, not `width=0`, despite
      // Utf8::display_width() itself correctly returning 0 (confirmed
      // independently, both via a standalone doctest and a standalone
      // throwaway program run directly on the real archive's own raw
      // bytes). commitGlyph() already has its own `std::max(width, 1)`
      // clamp on its NORMAL (non-zero-width) path, applied AFTER its
      // width==0 special case already had a chance to run -- clamping
      // again HERE, before that check ever sees the value, made the
      // whole width==0 branch permanently unreachable from this call
      // site (the only one that ever passes a real computed width;
      // the other two commitGlyph() call sites always pass a literal
      // 1 for plain ASCII/control bytes, never 0, so this bug was
      // invisible from testing calls). Passing `width` straight
      // through restores the signal commitGlyph() was always designed
      // to act on.
      commitGlyph(impl_->pendingUtf8, width);
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

  // Zero-width combining character (Utf8::display_width()'s own
  // "combining characters (0幅)" case -- dakuten/handakuten marks
  // U+3099/U+309A chief among them, common in macOS's own NFD-
  // decomposed filenames, e.g. "デスクトップ" stored on disk as
  // "テ"+U+3099+"スクトッフ"+U+309A+"...") -- 2026-08-20 fix, a real
  // report + screenshot (=dl): "久々に-g起動したら重ね打ちじゃなく
  // なってる", showing exactly this ("テ゛スクトッフ゜" instead of
  // "デスクトップ" in a real directory listing). Every call site below
  // used to unconditionally advance the cursor by `std::max(width, 1)`
  // -- even a genuinely 0-width mark got its OWN fresh cell, so it drew
  // as an independent, un-stacked glyph next to its base character
  // instead of combining with it.
  if (width == 0 && impl_->cursorCol > 0) {
    int prevCol = impl_->cursorCol - 1;
    // A wide (CJK) character's own continuation shadow cell carries no
    // real glyph of its own (see the width==2 branch below) -- the
    // actual glyph this mark should attach to is one column further
    // back in that case.
    if (impl_->cellAt(impl_->cursorRow, prevCol).continuation && prevCol > 0) prevCol--;
    Cell &target = impl_->cellAt(impl_->cursorRow, prevCol);
    // 2026-08-20 follow-up (a direct user report comparing screenshots:
    // "=dsよりはマシだが=dvには負ける...自前描画なら何とかなる？" --
    // still visibly not quite fused, next to -c's own perfect result):
    // the FIRST version of this fix just appended the mark's raw UTF-8
    // bytes here, leaving the cell's glyph as a still-decomposed TWO-
    // codepoint string for TTF_RenderUTF8_Blended() to shape -- but
    // SDL_ttf/FreeType do simple linear glyph layout, not real Unicode
    // text shaping (no GPOS mark-attachment positioning the way -c's
    // real terminal emulator's shaping engine already does), so the
    // mark's own advance/position isn't guaranteed to land exactly on
    // the base glyph. composeDakutenCodepoint() sidesteps shaping
    // entirely by precomposing the pair into its single NFC codepoint
    // first (e.g. "ト"+U+3099 -> "ド"), so the cell ends up holding one
    // perfectly ordinary character -- rendered with normal glyph
    // metrics, no combining-mark shaping involved at all. Falls back
    // to the old raw-append behavior for any pair outside that finite,
    // well-known table (still 0-width per display_width(), just not a
    // composition this port's own table knows).
    unsigned int composed = composeDakutenCodepoint(decodeUtf8Codepoint(target.glyph), decodeUtf8Codepoint(utf8Bytes));
    if (composed) target.glyph = encodeUtf8Codepoint(composed);
    else target.glyph += utf8Bytes;
    return;
  }

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

  // A blank cell (empty/space glyph) still needs to fall through to
  // the real TTF_RenderUTF8_Blended() call below when underlined --
  // SDL_ttf draws the underline decoration spanning the glyph's own
  // advance width independently of whether that glyph has any visible
  // ink, so rendering " " with TTF_STYLE_UNDERLINE set genuinely does
  // produce a surface containing just the underline stroke. Skipping
  // this early-return ONLY for underlined blanks (not e.g. bold ones,
  // where there's nothing bold about invisible ink to draw) keeps the
  // original performance intent (most blank cells have nothing to
  // render at all) while fixing a real gap: a real report + screenshot
  // (=ci) found the space between "Invalid"/"mode" in DirectoryMode's
  // own underlined error message (ModeLineFields::nameUnderlined) had
  // NO underline under it in -g specifically, breaking what should
  // read as one continuous line -- this early-return, unconditional
  // for ANY blank cell regardless of attributes, was silently dropping
  // it before ever reaching the underline-style code just below.
  bool blank = cell.glyph.empty() || cell.glyph == " ";
  if (cell.continuation || (blank && !cell.underline)) return;

  // Whether this glyph occupies one cell or two (a CJK/full-width
  // character, see commitGlyph()'s continuation-cell convention).
  bool isWide = (col + 1 < impl_->columns) && impl_->cellAt(row, col + 1).continuation;

  int style = TTF_STYLE_NORMAL;
  if (cell.bold) style |= TTF_STYLE_BOLD;
  if (cell.underline) style |= TTF_STYLE_UNDERLINE;

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
  // 2026-08-11: TTF_HINTING_LIGHT (item 263) turned out font-dependent
  // too -- a real report showed it fully fixed the default profile's
  // "MS Gothic" pairing (confirmed via fontTools: msgothic.ttc's gasp
  // table is {24: GASP_GRIDFIT, 65535: GASP_GRIDFIT|GASP_DOGRAY}, an
  // exact ppem=24 threshold matching the report's own "24pt fixes it,
  // 18pt doesn't"), but made NO visible difference for RedSands' "SF
  // Mono Regular" pairing. SF Mono's own file (SFNSMono.ttf) has NO
  // gasp table at all -- consistent with modern Apple system fonts
  // generally shipping unhinted, since CoreText's own rasterizer
  // (what -c actually renders through) has not used TrueType grid-
  // fitting since early Mac OS X, relying purely on antialiased/
  // subpixel-positioned outlines regardless of point size. TTF_HINTING_
  // LIGHT still runs FreeType's autohinter (a synthetic hint program
  // this font was never designed to need), which doesn't reproduce
  // that "no grid-fitting at all" behavior -- TTF_HINTING_NONE (skip
  // hinting entirely, scale the raw outline and just antialias it) is
  // the closer structural match to how -c actually rasterizes text,
  // for any font. Applied only to narrow (half-width) glyphs, matching
  // the original report's scope; wide (CJK) glyphs keep normal
  // hinting.
  TTF_SetFontHinting(renderFont, isWide ? TTF_HINTING_NORMAL : TTF_HINTING_NONE);

  // An EMPTY glyph string (as opposed to an actual " " space
  // character) has zero advance width, so TTF_RenderUTF8_Blended()
  // would produce a zero-width surface with nothing for the underline
  // decoration to span even after the blank-cell early-return above
  // was relaxed for underlined cells -- substitute a real space so
  // there's an actual cell-width run for the underline to be drawn
  // across.
  const std::string &renderGlyph = cell.glyph.empty() ? std::string(" ") : cell.glyph;
  SDL_Surface *surface = TTF_RenderUTF8_Blended(renderFont, renderGlyph.c_str(), fg);
  if (!surface) return;
  if (!isWide) boostGlyphAlpha(surface, kNarrowGlyphAlphaGamma);
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
  // Same half-width-hinting adjustment as renderCell() -- see its own
  // comment.
  TTF_SetFontHinting(renderFont, isWide ? TTF_HINTING_NORMAL : TTF_HINTING_NONE);

  SDL_Surface *surface = TTF_RenderUTF8_Blended(renderFont, cell.glyph.c_str(), impl_->bg);
  if (!surface) return;
  if (!isWide) boostGlyphAlpha(surface, kNarrowGlyphAlphaGamma);
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

  // Static reference image until the first real mouse touch, the real
  // always-current thumb from then on -- see Impl::everInteracted's
  // own comment. effectiveTop is a plain int here when the real thumb
  // path runs (no active sub-row pixel offset outside renderScrolled()'s
  // own drag/coast path), unlike renderScrolled()'s own fractional
  // effectiveTop.
  if (!impl_->everInteracted) {
    drawIdleScrollbarImage();
  } else {
    drawScrollbar(static_cast<double>(impl_->scrollTopIndex), impl_->scrollTotalLines, impl_->scrollContentRows);
  }

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

// Shows (and raises/focuses) the window created hidden by create() --
// deliberately split from create() itself so a caller that needs to
// position/resize the window FIRST (setWindowPosition()/
// matchWindowSize(), the "実はすでにGUI" overlay launch) can do so
// while it's still invisible, then reveal it already in its final
// place. A previous version showed (SDL_WINDOW_SHOWN) and raised the
// window immediately inside create(), before the overlay launch call
// site had a chance to reposition/resize it -- a real report
// (2026-08-09: "横幅が狭いは解消されたものの重ね打ち画面が出る前に
// 一旦は狭い画面が表示されてしまうので重ね打ちだとわかってしまう")
// confirmed this as a visible flash: the font-metric-sized, centered
// window from create() briefly on screen, then jumping to match the
// real terminal's position/size a moment later -- exactly the kind of
// seam this whole feature exists to avoid. The plain `-g` and `^Og`
// call sites (no real terminal to match, nothing to reposition first)
// still call open() unconditionally right after create() too, so
// their own visible behavior is unchanged by this split.
//
// SDL_RaiseWindow() itself is unchanged from create()'s own previous
// call -- see the removed comment there (still applicable): without
// it, a real 2026-08-03 report found a merely-shown window doesn't
// reliably steal input focus from whichever window was already
// focused on every platform/WM.
void GraphicalDisplay::open() {
  if (!impl_->window) return;
  // Clear to the already-set bg color (setColors(), called by the
  // overlay launch call site before this) and present once BEFORE
  // showing the window -- SDL_CreateRenderer() never implicitly
  // clears its own buffer, so without this the window's first-ever
  // visible frame would be whatever undefined content happened to be
  // sitting in the renderer's backbuffer (typically solid black, but
  // not guaranteed) rather than the real terminal's own matched
  // background -- a smaller version of the exact "briefly shows the
  // wrong thing before snapping to correct" seam this whole open()
  // split (see its own comment) exists to close. The real character
  // grid itself isn't drawn here (flush() runs that; content isn't
  // even wired to this backend yet at this point in the "実はすでに
  // GUI" launch, see main()'s own call-site comment) -- only the
  // background color, which is enough to avoid a jarring flash and is
  // all a real terminal's own idle background looks like anyway in
  // the instant before the first prompt/listing paints.
  SDL_SetRenderDrawColor(impl_->renderer, impl_->bg.r, impl_->bg.g, impl_->bg.b, 255);
  SDL_RenderClear(impl_->renderer);
  SDL_RenderPresent(impl_->renderer);
  SDL_ShowWindow(impl_->window);
  SDL_RaiseWindow(impl_->window);
}
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
        // See Impl::everInteracted's own comment -- any button press
        // (even one this switch otherwise ignores below) counts as
        // "touched" and permanently flips flush() over from the
        // static idle image to the real thumb.
        impl_->everInteracted = true;
        if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) break;
        // A left press that starts inside the scrollbar strip is a
        // thumb-drag gesture, not a content click/drag -- routed to
        // scrollbarQueue instead, so it neither selects/opens a file
        // (MouseClick) nor starts a content drag-scroll (DragEvent).
        // Right-clicks in that strip fall through to the normal
        // MouseClick path below unchanged (no right-click behavior is
        // defined for the scrollbar; this just avoids silently
        // dropping the event).
        int scrollbarStripX = Impl::kMarginX + impl_->columns * impl_->cellWidth;
        if (event.button.button == SDL_BUTTON_LEFT && event.button.x >= scrollbarStripX) {
          impl_->scrollbarDragActive = true;
          SDL_CaptureMouse(SDL_TRUE);
          impl_->scrollbarQueue.push_back(ScrollbarEvent{ScrollbarEventType::ButtonDown, event.button.y});
          break;
        }
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
          if (impl_->scrollbarDragActive) {
            impl_->scrollbarDragActive = false;
            impl_->scrollbarQueue.push_back(ScrollbarEvent{ScrollbarEventType::ButtonUp, event.button.y});
          } else {
            impl_->dragQueue.push_back(DragEvent{DragEventType::ButtonUp, 0});
          }
        }
        break;
      }
      case SDL_MOUSEMOTION: {
        if (impl_->scrollbarDragActive) {
          impl_->scrollbarQueue.push_back(ScrollbarEvent{ScrollbarEventType::Motion, event.motion.y});
        } else if (event.motion.state & SDL_BUTTON_LMASK) {
          impl_->dragQueue.push_back(DragEvent{DragEventType::Motion, event.motion.yrel});
        } else if (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_INPUT_FOCUS) {
          // Gated on keyboard/input focus (NOT SDL_WINDOW_MOUSE_FOCUS,
          // which would be true for ANY window the cursor happens to
          // be over, active or not -- the whole point of this check).
          // SDL delivers SDL_MOUSEMOTION for whichever window is under
          // the cursor regardless of which window is actually active,
          // unlike a real terminal's own SGR mouse reporting (which a
          // background/unfocused terminal simply never receives at
          // all). A real report + screenshot: "ウィンドウが非アクティブ
          // （左上の赤黄緑がみな灰色）なのにマウスがウィンドウ内に入る
          // とカーソルが追随" -- the cursor-follow highlight was
          // tracking the mouse even while this window sat behind
          // another, unfocused one.
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
        impl_->everInteracted = true;  // see Impl::everInteracted's own comment
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

std::optional<GraphicalDisplay::ScrollbarEvent> GraphicalDisplay::consumeScrollbarEvent() {
  if (impl_->scrollbarQueue.empty()) return std::nullopt;
  ScrollbarEvent event = impl_->scrollbarQueue.front();
  impl_->scrollbarQueue.pop_front();
  return event;
}

int GraphicalDisplay::cellHeightPixels() const { return impl_->cellHeight; }
int GraphicalDisplay::marginYPixels() const { return Impl::kMarginY; }

void GraphicalDisplay::setColors(Color foreground, Color background) {
  impl_->fg = SDL_Color{foreground.r, foreground.g, foreground.b, 255};
  impl_->bg = SDL_Color{background.r, background.g, background.b, 255};
}

namespace {

// Estimated macOS title bar height (points) for a normal titled,
// closable, resizable window. Used ONLY as a fallback when
// SDL_GetWindowBordersSize() itself fails, which a real report
// (2026-08-08, "変化無し" -- the border-size-based correction produced
// NO visible change at all on real hardware) points to as the actual,
// silent root cause: SDL2's own documented behavior is that a failed
// SDL_GetWindowBordersSize() call leaves the output at 0/0/0/0, and
// this project's own web research turned up exactly this as a known
// Cocoa/macOS limitation ("the -1 return value on macOS Cocoa
// indicates that the window border size information is either not
// supported on that platform") -- confirmed directly on this machine
// too (a standalone driver calling SDL_GetWindowBordersSize() printed
// `rc=-1 ... error=That operation is not supported`). Falling back to
// 0 (this function's first version) silently skipped the whole
// correction -- exactly the "no change" symptom reported. This
// constant keeps the SAME two call sites (setWindowPosition()/
// setWindowSize()) working even when the "authoritative" OS query
// itself doesn't.
//
// The literal value isn't from Apple documentation; nothing in this
// sandbox can measure a real macOS title bar directly -- it's been
// tuned entirely from real user feedback: 28 (initial estimate) ->
// "縦が2,3ドット長い" (still ~2-3pt too SMALL, making the window that
// much too tall overall) -> 31 -> confirmed exactly right at 32 via
// USILICTITLEBARHEIGHT=32 (the env var override, same pattern as
// USILICSCROLLBARSCALE/-u elsewhere in this file/main.cpp, added
// specifically so this kind of tuning doesn't need a fresh
// code-and-rebuild round each time).
constexpr int kDefaultTitleBarHeight = 32;

int estimatedTitleBarHeight() {
  if (const char *env = std::getenv("USILICTITLEBARHEIGHT")) {
    int parsed = std::atoi(env);
    if (parsed > 0) return parsed;
  }
  return kDefaultTitleBarHeight;
}

// Shared by setWindowPosition()/setWindowSize() -- both need the same
// top-border (title bar) height to convert between AppleScript's
// "bounds of window" (outer frame) coordinate convention and SDL's
// own (content area) one. Prefers the real, OS-reported value
// (SDL_GetWindowBordersSize()) when it's actually available, falling
// back to estimatedTitleBarHeight() otherwise (see its own comment).
// left/bottom/right are effectively always 0 on macOS (no visible
// side/bottom chrome on a normal titled window beyond the title bar
// itself), but still queried/returned for completeness and so a
// future non-macOS Display backend isn't silently wrong here either.
struct WindowBorders {
  int top = 0, left = 0, bottom = 0, right = 0;
};

WindowBorders realOrEstimatedWindowBorders(SDL_Window *window) {
  WindowBorders borders;
  if (window && SDL_GetWindowBordersSize(window, &borders.top, &borders.left, &borders.bottom, &borders.right) == 0 &&
      borders.top > 0) {
    return borders;
  }
  borders = WindowBorders{};
  borders.top = estimatedTitleBarHeight();
  return borders;
}

}  // namespace

// `x, y` is expected in the SAME "outer frame, title bar included"
// coordinate convention AppleScript's "bounds of window" uses (see
// queryTerminalAppWindowBounds()'s own comment in main.cpp) -- NOT
// SDL_SetWindowPosition()'s own native convention. SDL's own
// documented behavior is that position/size describe the window's
// CONTENT area, with OS-drawn decorations (the title bar) added
// OUTSIDE/ABOVE that -- passing the outer-frame `y` straight through
// unmodified (the previous version of this function) would place this
// window's CONTENT top where the real terminal's OUTER top (title
// bar) sits, making this window's own title bar float
// title-bar-height above where the real terminal's title bar actually
// is (a real report, 2026-08-07, screenshots ⑮⑯: "表示開始位置の縦
// 方向へのズレが共通してある", the same magnitude regardless of which
// Terminal.app profile/color scheme was used, consistent with a fixed
// geometric offset rather than anything profile-specific). Corrected
// by adding this window's own top border (title bar) height -- see
// realOrEstimatedWindowBorders()'s own comment on why that isn't
// simply SDL_GetWindowBordersSize()'s raw result -- to `y` before
// handing it to SDL.
void GraphicalDisplay::setWindowPosition(int x, int y) {
  if (!impl_->window) return;
  WindowBorders borders = realOrEstimatedWindowBorders(impl_->window);
  x += borders.left;
  y += borders.top;
  SDL_SetWindowPosition(impl_->window, x, y);
}

// `width, height` is expected in the SAME "outer frame, title bar
// included" convention setWindowPosition() takes (see its own
// comment) -- NOT SDL_SetWindowSize()'s own native convention, which
// (like SetWindowPosition) describes the CONTENT area only. A first
// version of this function passed `width`/`height` straight through
// unconverted, which -- combined with setWindowPosition()'s correct
// title-bar-height addition to `y` -- made this window's OUTER frame
// end up title-bar-height TALLER than the real terminal's own outer
// frame (a real report, 2026-08-08, screenshot: "ウィンドウ操作部の
// 分だけ縦長かつ表示行数が少なめ"): setting the CONTENT height to the
// real terminal's OUTER height (title bar included) makes this
// window's content area itself already title-bar-height taller than
// it needs to be, so the whole window (that oversized content area,
// plus this window's OWN title bar on top of it) ends up one full
// title-bar-height taller overall. Corrected the same way
// setWindowPosition() converts its own coordinate: subtract the
// border sizes from realOrEstimatedWindowBorders() (see its own
// comment) from the given outer dimensions before handing them to
// SDL, converting outer-frame size to the content-area size SDL
// actually wants.
//
// Beyond that conversion, this still overrides whatever this class's
// own cellWidth*columns+margins+scrollbar-width formula computed --
// the same 2026-08-07 report found that formula's own computed width
// consistently a point or two narrower than the real terminal across
// every profile tried (Basic/Homebrew/Ocean/Red Sands/Man Page).
// Rather than chase the exact source of that small, cross-profile-
// constant discrepancy (font-metric rounding in cellWidth, or a fixed
// margin/scrollbar-width constant off by a point or two), matching
// the real terminal's own already-measured size directly is both
// simpler and guaranteed exact, at the cost of a possible sliver of
// empty background (or, in the unlikely case the real terminal is
// somehow the SMALLER of the two, a point or two of content clipped
// at the right/bottom edge) -- imperceptible next to the alternative
// of a visibly mismatched window size.
void GraphicalDisplay::setWindowSize(int width, int height) {
  if (!impl_->window) return;
  WindowBorders borders = realOrEstimatedWindowBorders(impl_->window);
  width -= (borders.left + borders.right);
  height -= (borders.top + borders.bottom);
  if (width > 0 && height > 0) SDL_SetWindowSize(impl_->window, width, height);
}

// Overrides both the SDL window's outer size AND this class's own
// cellWidth/cellHeight, so that columns*cellWidth+margins+scrollbar-
// width and rows*cellHeight+margins exactly fill the given outer
// dimensions -- a more direct fix for the same "行間が狭い" line-
// spacing mismatch the TTF_FontHeight()-based cellHeight computation
// in create() (and USILICLINEHEIGHT) were added for. A real report
// (2026-08-08, font sizes 13/13/14/18/14pt across 5 different real
// Terminal.app profiles, "フォントの大きさによる。同サイズから適正値
// を一律に算出できればUSILICLINEHEIGHTは不要かと") found the needed
// correction scales with font size in a way no single fixed
// constant -- or even TTF_FontHeight() vs the raw glyph metrics --
// reliably captures across different fonts/sizes. Since the「実は
// すでにGUI」launch already KNOWS the real terminal's exact outer
// pixel dimensions (via AppleScript bounds) and the exact row/column
// count it needs to display, this derives cellWidth/cellHeight
// DIRECTLY from that arithmetic instead of estimating from font
// metrics at all, guaranteeing a pixel-exact match regardless of
// which font/size the real terminal happens to be using. The window's
// own outer size is set from the RAW (unrounded) target dimensions
// rather than recomputed from the (integer-divided, so possibly
// rounded-down-by-a-pixel-or-two) cellWidth/cellHeight, so the outer
// frame still matches exactly even if that leaves a pixel or two of
// slack at the content's own right/bottom edge -- imperceptible next
// to a mismatched overall window size.
//
// Font-metric-based cellWidth/cellHeight (create()'s own computation)
// remains the right fallback for launches with no real terminal to
// match against (plain `-g`, or `^Og` mid-session) -- this method is
// only called from the "実はすでにGUI" overlay path, where a real
// target size is always available.
void GraphicalDisplay::matchWindowSize(int outerWidth, int outerHeight) {
  if (!impl_->window || impl_->rows <= 0 || impl_->columns <= 0) return;
  WindowBorders borders = realOrEstimatedWindowBorders(impl_->window);
  int contentWidth = outerWidth - (borders.left + borders.right);
  int contentHeight = outerHeight - (borders.top + borders.bottom);
  if (contentWidth <= 0 || contentHeight <= 0) return;
  // Rounded to the NEAREST integer cell size, not floored -- a real
  // report (2026-08-09, screenshot "⑰", Man Page profile, 48 rows)
  // showed the previous plain-truncating division (contentHeight/rows)
  // reappearing as the exact same class of bug this whole "実はすでに
  // GUI" feature's setWindowSize() originally fixed (docs/HISTORY.md
  // 2026-08-07 "⑯": the real terminal visibly peeking out below this
  // window because it's shorter) -- because the window's own size is
  // now always derived FROM cellHeight (see below), truncating (always
  // rounding cellHeight DOWN) always makes the resulting grid, and so
  // the window itself, shorter than contentHeight by the division's
  // full remainder (up to rows-1px, not "at most one cell" as an
  // earlier version of this comment assumed -- confirmed live: 48 rows
  // left a 45px gap). Rounding to nearest instead minimizes |rows*
  // cellHeight - contentHeight| directly (the best any single uniform
  // integer cell height can do), bounding the mismatch to roughly half
  // that (~rows/2 px worst case, and often far less in practice -- the
  // real Man Page numbers above round 13.9375 up to 14, landing the
  // window a mere 3px TALLER than the real terminal instead of 45px
  // SHORTER). Ties round up (`+denom/2` before truncating), fine here
  // since both directions are an equally acceptable few-px mismatch.
  int cellWidth = (contentWidth - Impl::kMarginX * 2 - Impl::kScrollbarWidth + impl_->columns / 2) / impl_->columns;
  int cellHeight = (contentHeight - Impl::kMarginY * 2 + impl_->rows / 2) / impl_->rows;
  if (cellWidth <= 0 || cellHeight <= 0) return;
  // Read from the STABLE base fields (Impl::baseFontMetricCellWidth/
  // Height, captured once in create()), NOT impl_->cellWidth/cellHeight
  // -- the latter get overwritten with the real terminal's own target
  // values a few lines below, so on a SECOND matchWindowSize() call
  // (a live font-size change mid-session, `^Og` re-entry, etc.) they'd
  // no longer hold the font's own true native metric at all. See
  // Impl::baseFontMetricCellWidth's own comment for the full reasoning.
  int fontMetricCellWidth = impl_->baseFontMetricCellWidth;
  int fontMetricCellHeight = impl_->baseFontMetricCellHeight;
  // BOTH cellWidth and cellHeight are now the PLAIN ARITHMETIC values,
  // UNFLOORED -- height's own font-metric floor (originally added for
  // a real report, 2026-08-08, two of five real terminal profiles at
  // 13pt: "garbled/overlapping text, adjacent rows visibly smeared
  // into each other") was removed 2026-08-10, the same day and for the
  // same reason width's own floor already was (see the class-wide
  // reasoning below, ported from that fix): a real report (screenshots,
  // pixel-measured: 1119x813 for -g vs 1119x690 for -c, SAME 22-line
  // content, at a real terminal profile's line-spacing set to 0.8x)
  // found -g refusing to shrink below its own font-metric floor while
  // -c's real line spacing shrank normally, making -g visibly TALLER
  // ("縦長") than the real terminal it's supposed to overlay -- the
  // exact same class of divergence the width floor caused for 24pt
  // "Novel", just triggered by a sub-1.0 line-spacing MULTIPLIER
  // instead of a large font SIZE this time (both ultimately mean "the
  // real terminal's own actual per-row/per-column pixel measurement
  // differs from this class's own font-metric-based estimate of it").
  //
  // Whether removing the floor risks reintroducing the ORIGINAL 13pt
  // "smeared" bug this floor was added for was checked before doing
  // this, not assumed: renderCell()'s own fitGlyphRect() (used for
  // EVERY glyph drawn, confirmed by re-reading it here again) always
  // uniformly downscales a glyph surface larger than its own cell
  // (scale = min(1.0, maxW/glyphW, maxH/glyphH) -- applies to height
  // exactly like it does to width, never upscales, never clips) --
  // meaning a smaller-than-font-metric cellHeight shrinks each row's
  // own glyphs to fit their own row's box, it does not let them bleed
  // into an ADJACENT row's box the way un-uniform positioning could.
  // The original 13pt report's real root cause was very likely the
  // SAME class of window/grid SIZE MISMATCH item 241's width bug (and
  // this file's own matching comment on it) turned out to be, not
  // literal unsafe glyph overflow -- matching the real terminal's own
  // measured pixel height EXACTLY (this class's own established
  // "誤差ゼロ" standard, item 227) removes that mismatch by
  // construction, the same way it already did for width.
  //
  // Re-render the font itself at (approximately) the RIGHT size for
  // this target cellHeight, rather than leaving it fixed at create()'s
  // own original fontPointSize -- added 2026-08-10, a real report
  // ("18ドットだと行が重なる...24ドットにしてみて文字が細すぎるから
  // だとわかった。-gだとベクトルフォントにならないのはなぜ？") caught
  // by comparing -g against -c at two sizes: -g's text looked
  // noticeably THINNER/more aliased than -c's real terminal rendering
  // at 24pt, and outright illegible at 18pt -- NOT a hinting/AA
  // setting difference, but this exact method's own doing: every
  // matchWindowSize() call before this one changed impl_->cellWidth/
  // cellHeight (the LAYOUT grid the WINDOW is sized from) but left the
  // FONT itself untouched at its original create()-time point size, so
  // whenever the target cell size (now always the real terminal's own
  // exact arithmetic-derived value, items 254/258) differed EVEN
  // SLIGHTLY from that original size, fitGlyphRect() (renderCell()'s
  // own glyph-fit helper) had to bitmap-rescale an already-rasterized
  // glyph texture via SDL_RenderCopy -- a blurry/thin raster resize,
  // not a real vector re-render at the correct size, and MUCH more
  // frequently needed now that items 254/258 stopped flooring
  // cellWidth/cellHeight up to the font's own native size (which used
  // to make scale==1.0, i.e. no rescaling at all, essentially always).
  // TTF_SetFontSize() (SDL_ttf 2.0.18+) re-renders the font's own
  // internal glyph cache at a NEW point size natively -- scaling from
  // Impl::baseFontPointSize/baseFontMetricCellHeight (the font's own
  // STABLE, never-overwritten original measurement, not the
  // now-target-holding impl_->cellHeight) keeps this accurate across
  // repeated calls the same way fontMetricCellWidth/Height's own read
  // above does. Height (not width) drives the ratio because line-
  // height is the more reliable/conventional metric most fonts scale
  // point size from; the resulting native width won't necessarily
  // land exactly on the target cellWidth too, but fitGlyphRect() still
  // catches that small residual safely, just far less drastically than
  // before. A no-op (skipped entirely) when the target already matches
  // the font's own current native size, avoiding a redundant
  // TTF_SetFontSize() call (not free -- it rebuilds the font's glyph
  // cache) on every single matchWindowSize() call when nothing about
  // the font size actually needs to change.
  int newPointSize = impl_->baseFontPointSize;
  if (fontMetricCellHeight > 0 && cellHeight != fontMetricCellHeight) {
    newPointSize = std::max(
        1, (impl_->baseFontPointSize * cellHeight + impl_->baseFontMetricCellHeight / 2) / impl_->baseFontMetricCellHeight);
    TTF_SetFontSize(impl_->font, newPointSize);
    if (impl_->fallbackFont) TTF_SetFontSize(impl_->fallbackFont, newPointSize);
  }
  impl_->cellWidth = cellWidth;
  impl_->cellHeight = cellHeight;
  int width = contentWidth;
  int height = contentHeight;
  // Same opt-in USILICGUIDEBUG file flush() already uses, reused here
  // for exactly the same reason: this sandbox
  // has no real Terminal.app window to compare against, so root-
  // causing a real-hardware report (this exact dump is what found the
  // 45px-remainder bug above, "Man Pageが余白大", 2026-08-08) can't be
  // done blind -- kept in place so any FUTURE mismatch report can be
  // diffed against real numbers immediately too. flooredCellWidth/
  // flooredCellHeight are computed here PURELY for this diagnostic
  // line (to see how far the font-metric estimate would have
  // diverged) -- neither drives any decision above anymore.
  int flooredCellWidth = std::max(cellWidth, fontMetricCellWidth);
  int flooredCellHeight = std::max(cellHeight, fontMetricCellHeight);
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      int rescaledW = 0, rescaledH = 0;
      TTF_SizeUTF8(impl_->font, "M", &rescaledW, &rescaledH);
      std::fprintf(f,
                    "---matchWindowSize--- rows=%d columns=%d outer=%dx%d borders(t/l/b/r)=%d/%d/%d/%d "
                    "content=%dx%d arithCell=%dx%d fontMetricCell=%dx%d flooredCell=%dx%d finalWindow=%dx%d "
                    "newPointSize=%d rescaledFontCell=%dx%d\n",
                    impl_->rows, impl_->columns, outerWidth, outerHeight, borders.top, borders.left, borders.bottom,
                    borders.right, contentWidth, contentHeight, cellWidth, cellHeight, fontMetricCellWidth,
                    fontMetricCellHeight, flooredCellWidth, flooredCellHeight, width, height, newPointSize, rescaledW,
                    rescaledH);
      std::fclose(f);
    }
  }
  SDL_SetWindowSize(impl_->window, width, height);
}

void GraphicalDisplay::setVisible(bool visible) {
  if (!impl_->window) return;
  if (visible) SDL_ShowWindow(impl_->window);
  else SDL_HideWindow(impl_->window);
}

void GraphicalDisplay::raise() {
  if (!impl_->window) return;
  SDL_ShowWindow(impl_->window);
  SDL_RaiseWindow(impl_->window);
}

// The REAL macOS system open-hand cursor, extracted live from
// NSCursor.openHand (mac_cursor.hpp's extractOpenHandCursorImage())
// and wrapped in an SDL_Cursor -- not a hand-drawn SDL_CreateColorCursor
// bitmap, and NOT a raw [[NSCursor openHandCursor] set] call either
// (an earlier version of this function did that and it reverted to
// the plain arrow the instant a drag started moving -- see
// mac_cursor.hpp's own comment for the full story of why routing
// through SDL_SetCursor(), like setPointingCursor() below already
// does, was the fix). Two earlier, since-abandoned attempts at
// drawing this shape ourselves (a flat-rectangle silhouette, then a
// rounded-capsule one) were both real rejections ("ロボットの手みたい
// なやつは却下", then "さっき却下のロボット指よりもひどくなった") --
// a same-day sharp catch explained why neither could ever look right:
// the pointing-hand cursor this project ALREADY shows for scrollbar
// dragging (setPointingCursor() below) was never actually drawn by
// this project or by SDL2 either -- it's macOS's own real system
// cursor asset (SDL_SYSTEM_CURSOR_HAND maps directly to
// `[NSCursor pointingHandCursor]` on macOS, confirmed by reading
// SDL2's own upstream source, src/video/cocoa/SDL_cocoamouse.m), so
// any hand-drawn approximation sitting next to it was always going to
// look inferior by comparison. Extracting NSCursor.openHand's own
// pixels (confirmed to exist as a real, well-formed vector cursor on
// this machine: /System/Library/Frameworks/ApplicationServices.
// framework/Versions/A/Frameworks/HIServices.framework/Versions/A/
// Resources/cursors/openhand/cursor.pdf) via a tiny Objective-C++
// (.mm) bridge (mac_cursor.mm) shows the exact same real asset instead
// -- reconsidered from the earlier "avoid .mm" preference (the
// scrollbar's own NSScroller work) specifically because the payoff
// here is concrete and already demonstrated, unlike that more
// speculative attempt.
void GraphicalDisplay::setHandCursor(bool active) {
  if (active == impl_->handCursorActive) return;
  impl_->handCursorActive = active;
  if (active) {
    if (!impl_->handCursor) {
      CursorImage img = extractOpenHandCursorImage();
      if (img.width > 0 && img.height > 0) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            img.rgba.data(), img.width, img.height, 32, img.width * 4, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
          impl_->handCursor = SDL_CreateColorCursor(surface, img.hotspotX, img.hotspotY);
          SDL_FreeSurface(surface);
        }
      }
    }
    if (impl_->handCursor) SDL_SetCursor(impl_->handCursor);
  } else {
    SDL_SetCursor(SDL_GetDefaultCursor());
  }
}

// SDL's own system pointing-hand (index finger) -- what setHandCursor()
// used to show for EVERY drag gesture, before a real request
// (2026-08-10, see setHandCursor()'s own comment) split content-
// dragging off to the real macOS open-hand cursor instead. Reserved
// now for gestures that really are more like clicking/operating a
// control than grabbing content: scrollbar-thumb dragging (see its
// own call site in main.cpp) -- and, per that same request ("可能なら
// ファイルおよび文字列選択でのスコープ範囲内...のみでの表示"), a
// future hover-over-selectable-content indicator, not yet wired here
// (no continuous hover-vs-selectable-region tracking exists yet to
// drive it -- left for a follow-up, see CLAUDE.md's own note on this
// request for the reasoning).
void GraphicalDisplay::setPointingCursor(bool active) {
  if (active == impl_->pointingCursorActive) return;
  impl_->pointingCursorActive = active;
  if (active) {
    if (!impl_->pointingCursor) impl_->pointingCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    if (impl_->pointingCursor) SDL_SetCursor(impl_->pointingCursor);
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
void GraphicalDisplay::renderTextRun(int y, const std::string &line, Color fg, int startCol) {
  SDL_Color sdlFg{fg.r, fg.g, fg.b, 255};
  int x = startCol;
  std::size_t pos = 0;
  while (pos < line.size() && x < impl_->columns) {
    unsigned char lead = static_cast<unsigned char>(line[pos]);
    int len = Utf8::sequence_length(lead);
    if (pos + len > line.size()) len = 1;
    int width = std::max(1, Utf8::display_width(reinterpret_cast<const unsigned char *>(line.data() + pos), len));
    std::string glyph = line.substr(pos, len);
    if (glyph != " " && lead >= 0x20) {
      // See renderCell()'s own comment on why a fallback font is
      // consulted here too -- including the `isWide` check below,
      // ADDED 2026-08-10 (a real report, "慣性スクロール中は日本語も
      // ビットマップになる"): this function was missing renderCell()'s
      // own `isWide && fallbackHasIt` preference, using the PRIMARY
      // (Latin) font instead of the CJK fallback for the small set of
      // "ambiguous width" codepoints the primary font ALSO happens to
      // claim (▽ U+25BD, curly quotes U+201C/U+201D -- see renderCell()'s
      // own comment for why classifying these as CJK-fallback-only
      // matters: Menlo's own narrow glyphs for them look visually
      // inconsistent, and stretched into a full 2-column box via
      // fitGlyphRect() far MORE drastically than a properly-proportioned
      // CJK glyph would need, reading as noticeably blurrier/more
      // "bitmap" -- exactly the reported symptom, and specific to the
      // SCROLLING render path since only THIS function had the gap).
      TTF_Font *renderFont = impl_->font;
      unsigned int codepoint = decodeUtf8Codepoint(glyph);
      bool isWide = width >= 2;
      bool fallbackHasIt = glyphProvidedByFont(impl_->fallbackFont, codepoint);
      if ((isWide && fallbackHasIt) || (!glyphProvidedByFont(impl_->font, codepoint) && fallbackHasIt))
        renderFont = impl_->fallbackFont;
      // This function previously never called TTF_SetFontStyle() at
      // all, silently inheriting whatever style renderCell()/
      // renderCursor() last left the font in; now made explicit so the
      // scrolling render path matches the static one. No per-glyph bold
      // info reaches this function (renderTextRun() only gets a flat
      // fg color, not a Cell), so this is always NORMAL.
      TTF_SetFontStyle(renderFont, TTF_STYLE_NORMAL);
      // Same half-width-hinting adjustment as renderCell() -- see its
      // own comment (item 264, replacing item 263's TTF_HINTING_LIGHT).
      TTF_SetFontHinting(renderFont, isWide ? TTF_HINTING_NORMAL : TTF_HINTING_NONE);
      SDL_Surface *surface = TTF_RenderUTF8_Blended(renderFont, glyph.c_str(), sdlFg);
      if (surface) {
        // Same alpha-gamma boost as renderCell() -- see boostGlyphAlpha()'s own comment (item 265).
        if (!isWide) boostGlyphAlpha(surface, kNarrowGlyphAlphaGamma);
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

int GraphicalDisplay::textRunDisplayWidth(const std::string &text) const {
  int width = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    unsigned char lead = static_cast<unsigned char>(text[pos]);
    int len = Utf8::sequence_length(lead);
    if (pos + len > text.size()) len = 1;
    width += std::max(1, Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data() + pos), len));
    pos += len;
  }
  return width;
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
// Visual style: solid impl_->fg (2026-08-07, replacing the previous
// translucent neutral-gray-thumb/no-track version), same color and
// full opacity as the mode-line's own reverse-video bar (see
// renderModeLine()/the mode-line-drawing block below) -- the user's
// own reasoning: matching the mode-line specifically (rather than
// picking yet another fixed gray) means the scrollbar automatically
// looks right under ANY color theme, not just this project's default
// green-on-black. Plain rectangle, no rounded corners this time --
// the mode-line bar itself is a plain rectangle, and the user asked
// for "the same shape, a rectangle of the same width" once the color
// was going to match it anyway. (The FIRST attempt at using impl_->fg,
// 2026-08-07 earlier the same day, was rejected for reading as "a
// solid saturated-green block" -- but that version filled a
// near-full-height TRACK with it; a small thumb-sized rect, the
// mode-line's own bar height, doesn't have the same "wall of color"
// problem.)
void GraphicalDisplay::drawScrollbar(double effectiveTop, int totalLines, int contentRows) {
  if (contentRows <= 0) return;
  int trackHeight = contentRows * impl_->cellHeight;

  // totalLines <= contentRows means the whole document already fits
  // on screen at once -- the thumb fills the entire track (nothing to
  // scroll to), matching a real scrollbar's own convention rather
  // than dividing by a non-positive range below.
  int scrollableLines = std::max(totalLines - contentRows, 0);
  double visibleFraction = (totalLines > 0) ? std::min(1.0, static_cast<double>(contentRows) / totalLines) : 1.0;
  int thumbHeight = std::max(static_cast<int>(trackHeight * visibleFraction), std::min(trackHeight, 20));
  int maxThumbY = trackHeight - thumbHeight;
  int thumbY = (scrollableLines > 0)
                   ? std::clamp(static_cast<int>(maxThumbY * (effectiveTop / scrollableLines)), 0, maxThumbY)
                   : 0;
  // Widened again, 2->1 (2026-08-07, a real side-by-side comparison
  // against a normal Directory-mode selection highlight bar -- "ハイ
  // ライトとの比較ではつまみ部の幅がやや狭い"): still a 1px margin on
  // each side within the reserved strip (kThumbInset 0 would use the
  // whole strip, no visual edge against the mode-line/content area at
  // all) rather than the full width.
  constexpr int kThumbInset = 1;
  // The extra trailing `+ Impl::kMarginX`: this class's own window-
  // sizing formula (`kMarginX*2 + columns*cellWidth + kScrollbarWidth`,
  // used throughout this file) reserves the scrollbar strip BEFORE a
  // trailing kMarginX-wide gap of plain background between it and the
  // window's own true right edge -- a real terminal's own native
  // scrollbar instead sits flush against that true right edge, no
  // trailing gap. A real user request (2026-08-09, made while briefly
  // trying a real NSScroller widget instead of this hand-drawn thumb,
  // but restated as still wanting to apply once that attempt was
  // reverted: "位置だけは元からあるスクロールバーと同じにしたい")
  // asked for this shift explicitly.
  int thumbX = Impl::kMarginX + impl_->columns * impl_->cellWidth + Impl::kMarginX + kThumbInset;
  int thumbWidth = std::max(Impl::kScrollbarWidth - kThumbInset * 2, 2);
  int thumbTop = Impl::kMarginY + thumbY;

  // Plain rectangle -- FINAL, per the user's own explicit 2026-08-07
  // call ("つまみは矩形採用で完了"). A same-day rounded-pill revival
  // (briefly reintroduced here, verified via a synthetic standalone
  // render) was based on a misreading: the user's own question at the
  // time ("矩形描画だけだと丸みを帯びたつまみ部が描けないのでは...
  // SDL描画でもできるの？") was about the SEPARATE, not-yet-built idle-
  // state decoration discussed alongside it, not about revisiting this
  // already-settled ACTIVE thumb -- reverted back to a plain rect once
  // that was clarified. Left unrounded going forward; this thumb's
  // shape is not to be revisited again without a new, explicit
  // request.
  SDL_Rect thumb{thumbX, thumbTop, thumbWidth, thumbHeight};
  SDL_SetRenderDrawColor(impl_->renderer, impl_->fg.r, impl_->fg.g, impl_->fg.b, 255);
  SDL_RenderFillRect(impl_->renderer, &thumb);
}

// Caches the current scroll position -- see this method's own header
// comment for why flush() needs this at all (it has no scroll-position
// parameters of its own, unlike renderScrolled()) -- only used once
// Impl::everInteracted is true (see its own comment); before that,
// flush() calls drawIdleScrollbarImage() below instead. A same-day
// real report ("マウス操作ON/OFFのたびに矩形つまみと張りぼて画像を
// 切り替えるのは無意味な気がする。つまみが消えてしまうので継続操作も
// できない") found unconditionally swapping between the two on every
// drag start/stop (this method's own first version) actively harmful;
// gating the swap on a one-time "has the user ever touched it" flag
// instead keeps the illusion intact up to the moment it stops
// mattering (the user's own words, restoring this image one round
// later: "「実はすでにGUI」が意味を持つのは最初だけ"), then never
// switches back.
void GraphicalDisplay::updateScrollPosition(int topIndex, int totalLines, int contentRows) {
  impl_->scrollTopIndex = topIndex;
  impl_->scrollTotalLines = totalLines;
  impl_->scrollContentRows = contentRows;
}

// Composites the three embedded slices (scrollbar_idle_image.hpp's own
// class comment explains why three, not one) to fill the SAME vertical
// span drawScrollbar()'s real track occupies (Impl::kMarginY to
// Impl::kMarginY + contentRows*cellHeight, contentRows == rows-1 since
// the last row is reserved for the mode line -- its own trackHeight
// formula) -- unlike the original single-image version, this ALWAYS exactly fills
// the current profile's own content height, no overflow/gap tradeoff
// to accept anymore: only the plain, textureless middle slice (drawn
// via a dst rect taller than its own native size, letting SDL_RenderCopy
// scale it) ever gets resized, while the top/bottom slices -- the only
// parts with real detail (the thumb/grip area, the rounded track end)
// -- always draw at their own native, undistorted pixel size.
//
// drawSlice(): a small local helper (not its own method -- used only
// here, three times) matching the mechanics of the original 2026-08-07
// single-image version: embedded BMP bytes decoded fresh every call
// (matching this file's existing per-frame-texture style, see
// renderCell()'s own glyph texture) via SDL2's native BMP reader, no
// runtime file path or new library dependency.
void GraphicalDisplay::drawIdleScrollbarImage() {
  auto drawSlice = [&](const unsigned char *bmp, size_t bmpSize, const SDL_Rect &dst) {
    SDL_RWops *rw = SDL_RWFromConstMem(bmp, static_cast<int>(bmpSize));
    if (!rw) return;
    SDL_Surface *surface = SDL_LoadBMP_RW(rw, 1);  // 1 == SDL closes/frees rw for us
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(impl_->renderer, surface);
    if (texture) {
      SDL_RenderCopy(impl_->renderer, texture, nullptr, &dst);
      SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
  };

  // Same trailing `+ Impl::kMarginX` shift as drawScrollbar()'s own
  // thumbX -- see its comment. Keeps this idle-state composite aligned
  // with where the real thumb sits once it takes over.
  int stripX = Impl::kMarginX + impl_->columns * impl_->cellWidth + Impl::kMarginX;

  // kDefaultIdleImageYOffset: a real report (2026-08-09, screenshots,
  // "2ミリほど下がるけど下にはみ出さない") found the (then single-image)
  // decoration pasted a few mm LOWER than it should sit -- plausibly
  // the embedded screenshot's own top edge already includes a few
  // pixels of the real scrollbar's own rounded/faded top-of-track
  // appearance above its visually "solid" portion, so pasting it
  // starting exactly at kMarginY reads as sitting that much lower than
  // a real terminal's own scrollbar, which has no such lead-in. -11
  // was confirmed exactly on real hardware
  // ("USILICIDLEIMAGEYOFFSET=-11でピッタリ") for that original single
  // image; applied to this 3-slice composite's own TOP edge (the top
  // slice is a direct re-crop of that same original image's own top
  // region, so the same lead-in reasoning still applies) -- AND,
  // symmetrically, to the BOTTOM edge too (see trackBottom's own
  // comment below for the real-hardware data that confirmed this).
  // USILICIDLEIMAGEYOFFSET (same "tunable knob instead of another
  // code-and-rebuild round" pattern as USILICTITLEBARHEIGHT/
  // USILICSCROLLBARSCALE) still overrides this default outright, kept
  // for the same reason those other knobs are: display/font/profile
  // variation this sandbox can't itself verify against.
  constexpr int kDefaultIdleImageYOffset = -11;
  int yOffset = kDefaultIdleImageYOffset;
  if (const char *env = std::getenv("USILICIDLEIMAGEYOFFSET")) {
    yOffset = std::atoi(env);
  }
  int trackTop = Impl::kMarginY + yOffset;
  // trackBottom -- two real reports (screenshots, 2026-08-10) got this
  // wrong in two different ways before landing here:
  //   1. An EARLIER version computed trackBottom from trackTop (i.e.
  //      applying yOffset there too), dragging the bottom slice up by
  //      the same amount the top slice is nudged for its own gradient
  //      lead-in -- wrong, "bar-3位置が高め".
  //   2. The NEXT version anchored trackBottom to the track's plain
  //      unshifted bottom edge (Impl::kMarginY + contentRows*cellHeight,
  //      contentRows == rows-1 since the last row is reserved for the
  //      mode line -- matching drawScrollbar()'s own trackHeight
  //      formula) with no correction at all -- still wrong, "さっきより
  //      2,3ドット高い", meaning bar-3.jpg's own bottom edge ALSO has a
  //      few pixels of real-scrollbar lead-in below its visually
  //      "solid" portion, the same class of thing yOffset already
  //      corrects for at the top.
  // A real USILICIDLEBOTTOMOFFSET tuning session across TWO very
  // different profiles (a small-font one needing +30, and 24pt "Novel"
  // -- cellHeight 19 vs 29 respectively -- needing +40) revealed the
  // correction is not a fixed pixel count at all: it's exactly
  // `cellHeight - yOffset` in BOTH cases (19-(-11)=30, 29-(-11)=40, an
  // exact match, not a coincidence -- the two ends of the original
  // source screenshot most likely have matching-magnitude lead-in
  // margins, so shifting the WHOLE composite kMarginY+rows*cellHeight
  // range outward by |yOffset| at both ends, symmetrically, is exactly
  // what both real data points independently confirm). This makes
  // trackBottom simply `Impl::kMarginY + rows*cellHeight - yOffset`
  // (equivalently: the mode-line row's own top edge, PLUS one more
  // cellHeight to account for stepping from contentRows back to the
  // full row count, MINUS yOffset) -- no separate tunable knob needed;
  // USILICIDLEIMAGEYOFFSET alone now drives both ends symmetrically.
  int trackBottom = Impl::kMarginY + impl_->rows * impl_->cellHeight - yOffset;
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      int windowHeight = Impl::kMarginY * 2 + impl_->rows * impl_->cellHeight;
      int contentRows = std::max(impl_->rows - 1, 0);
      std::fprintf(f,
                    "---drawIdleScrollbarImage--- rows=%d cellHeight=%d yOffset=%d trackTop=%d "
                    "trackBottom=%d windowHeight=%d modeLineRowTop=%d\n",
                    impl_->rows, impl_->cellHeight, yOffset, trackTop, trackBottom, windowHeight,
                    Impl::kMarginY + contentRows * impl_->cellHeight);
      std::fclose(f);
    }
  }

  SDL_Rect topDst{stripX, trackTop, kScrollbarIdleTopWidth, kScrollbarIdleTopHeight};
  drawSlice(kScrollbarIdleTopBmp, sizeof(kScrollbarIdleTopBmp), topDst);

  SDL_Rect bottomDst{stripX, trackBottom - kScrollbarIdleBottomHeight, kScrollbarIdleBottomWidth,
                      kScrollbarIdleBottomHeight};
  drawSlice(kScrollbarIdleBottomBmp, sizeof(kScrollbarIdleBottomBmp), bottomDst);

  // The gap between the top and bottom slices, filled by stretching
  // the plain middle slice to whatever height is needed -- negative
  // (top/bottom slices would overlap) only for a pathologically short
  // window (well under the two slices' own combined 68px), not a real
  // profile in practice, so simply skipped rather than clamped.
  int middleTop = trackTop + kScrollbarIdleTopHeight;
  int middleHeight = (trackBottom - kScrollbarIdleBottomHeight) - middleTop;
  if (middleHeight > 0) {
    SDL_Rect middleDst{stripX, middleTop, kScrollbarIdleMiddleWidth, middleHeight};
    drawSlice(kScrollbarIdleMiddleBmp, sizeof(kScrollbarIdleMiddleBmp), middleDst);
  }
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
                                       const std::vector<Display::ModeLineSegment> &modeLineSegments) {
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
    // Drawn as SEPARATE per-segment reverse-video runs, not one
    // continuous bar across the whole row -- see Display::
    // ModeLineSegment's own comment for the real screenshot bug this
    // fixes (this used to fill+draw the whole composed string as one
    // uninterrupted highlight, unlike ContentView::renderModeLine()'s
    // own per-field gaps). A non-standout segment (the plain single-
    // space gaps composeModeLineSegments() inserts between fields)
    // draws no fill at all, leaving the already-cleared background
    // showing through -- exactly the small black gap the reference
    // screenshot shows.
    int modeLineY = contentRows * impl_->cellHeight;
    int col = 0;
    for (const auto &segment : modeLineSegments) {
      int segCols = textRunDisplayWidth(segment.text);
      if (segment.standout) {
        SDL_Rect segBox{Impl::kMarginX + col * impl_->cellWidth, Impl::kMarginY + modeLineY, segCols * impl_->cellWidth,
                         impl_->cellHeight};
        SDL_SetRenderDrawColor(impl_->renderer, impl_->fg.r, impl_->fg.g, impl_->fg.b, 255);
        SDL_RenderFillRect(impl_->renderer, &segBox);
        renderTextRun(modeLineY, segment.text, Color{impl_->bg.r, impl_->bg.g, impl_->bg.b}, col);
      }
      col += segCols;
    }
  }

  // Same USILICGUIDEBUG pixel-dump flush() itself has (see its own
  // comment on why: SDL_RenderReadPixels() works headlessly, without
  // Screen Recording permission or a real visible window, so it was
  // built specifically for verifying rendering bugs from within a
  // sandbox that has no other way to see the real pixels). Extended
  // here (2026-08-07) since renderScrolled() is a fully separate
  // render path with its own SDL_RenderPresent() -- flush()'s own
  // dump never fires during an active drag/coast at all, which meant
  // the scrollbar/mode-line-segment work earlier this same session had
  // no way to be verified this way despite the mechanism already
  // existing. ".scrolled.bmp" suffix (vs. flush()'s own plain ".bmp")
  // so the two don't overwrite each other.
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    int outputWidthPixels = 0, outputHeightPixels = 0;
    SDL_GetRendererOutputSize(impl_->renderer, &outputWidthPixels, &outputHeightPixels);
    SDL_Surface *shot =
        SDL_CreateRGBSurfaceWithFormat(0, outputWidthPixels, outputHeightPixels, 32, SDL_PIXELFORMAT_RGBA32);
    if (shot) {
      if (SDL_RenderReadPixels(impl_->renderer, nullptr, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0)
        SDL_SaveBMP(shot, (std::string(dbg) + ".scrolled.bmp").c_str());
      SDL_FreeSurface(shot);
    }
  }

  SDL_RenderPresent(impl_->renderer);
}

}  // namespace fm
