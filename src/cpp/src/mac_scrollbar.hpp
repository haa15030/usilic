#pragma once

#include <functional>
#include <memory>

namespace fm {

// A REAL native macOS scrollbar (an NSScroller instance, not pixel-
// drawn) attached as a subview of the "実はすでにGUI" overlay's own
// SDL window. Added 2026-08-09 after a real user report pointed out
// that Emacs's own NS (Cocoa) port scrollbar "works properly" -- a
// WebSearch of Emacs's own (GPL) source confirmed why: nsterm.m's
// `EmacsScroller` is an `NSScroller` subclass added as a child view of
// the frame's own content view, not a hand-drawn bar at all. Every
// struggle this project's own PREVIOUS scrollbar attempt hit (docs/
// HISTORY.md 2026-08-03 on: pixel-vs-point/HiDPI conversion via
// USILICSCROLLBARSCALE, rounded-thumb proportions, an embedded static
// screenshot standing in for a live one) is something a real AppKit
// widget already handles correctly by construction -- the same reason
// Emacs's own scrollbar "just works" without any of that.
//
// Deliberately a tiny, opaque, platform-neutral C++ interface (no
// Objective-C/Cocoa types anywhere in this header) so every OTHER file
// in this project (graphical_display.cpp/hpp included) can use it
// without themselves needing Objective-C++ compilation -- only this
// class's own .mm implementation (macOS) needs that. `void *sdlWindow`
// is an `SDL_Window*` in disguise for the exact same reason: this
// header doesn't include SDL2/SDL.h either, so it stays includable
// from a plain C++ translation unit with zero new dependencies for the
// common (non-Apple) case.
//
// Only ever constructed by/owned by GraphicalDisplay, on macOS, for
// the "実はすでにGUI" overlay's own window -- see graphical_display.cpp's
// own call sites for exactly when/why. On any other platform, create()
// returns nullptr and GraphicalDisplay falls back to its existing
// hand-drawn scrollbar (drawScrollbar()/drawIdleScrollbarImage()) --
// see mac_scrollbar_stub.cpp, compiled instead of mac_scrollbar.mm on
// non-Darwin platforms (Objective-C++ syntax in the real .mm can't be
// compiled by a plain C++ toolchain, and macOS-only frameworks
// (AppKit) obviously don't exist there either).
class MacScrollbar {
 public:
  // `sdlWindow`: the SDL_Window* this scrollbar becomes a subview of.
  // `onUserScroll`: invoked (synchronously, on the same thread that
  // later calls GraphicalDisplay::pumpEvents(), from within an AppKit
  // event-tracking callback -- see the .mm's own comment on why that's
  // safe here) whenever the user clicks or drags the scroller, with a
  // fraction in [0.0, 1.0] -- 0.0 == scrolled all the way to the top,
  // 1.0 == all the way to the bottom -- for the caller to map onto its
  // own topIndex. Returns nullptr if the native widget couldn't be
  // created (non-Apple platform, or the SDL window has no accessible
  // native handle) -- never throws, matching GraphicalDisplay::create()'s
  // own "fall back, don't crash" convention.
  static std::unique_ptr<MacScrollbar> create(void *sdlWindow, std::function<void(double)> onUserScroll);

  // The width (logical points) a REAL system scrollbar of this same
  // style/control size actually needs on the current machine --
  // queried live via `+[NSScroller scrollerWidthForControlSize:
  // scrollerStyle:]` (a pure class-level query, no window/view
  // required, so this can be called before create() or even if
  // create() will go on to fail/return nullptr). Added 2026-08-09
  // after a real user request to match "ターミナル標準" scrollbar
  // proportions exactly: the constant this project used before
  // (GraphicalDisplay::Impl::kScrollbarWidth == 15) was always just "a
  // rough placeholder, not yet measured" (its own original comment) --
  // this replaces that guess with the SAME number Terminal.app/Finder/
  // every other NSScroller-based app's own scrollbar actually uses,
  // on whichever real machine this runs on (17pt on this sandbox's own
  // macOS/AppKit version -- confirmed via a standalone driver -- but
  // deliberately NOT hardcoded here, since this can legitimately vary
  // by OS version). Falls back to 15 (the old placeholder) on non-
  // Apple platforms, where it's cosmetic only -- see
  // mac_scrollbar_stub.cpp's own comment.
  static int preferredWidth();

  ~MacScrollbar();
  MacScrollbar(const MacScrollbar &) = delete;
  MacScrollbar &operator=(const MacScrollbar &) = delete;

  // Repositions/resizes the scroller. Coordinates are in the SAME
  // logical-point, window-relative convention GraphicalDisplay's own
  // kMarginX/kScrollbarWidth pixel math already uses (top-left
  // origin) -- NOT Cocoa's own bottom-left-origin AppKit coordinate
  // convention; the .mm implementation converts internally using the
  // content view's own height.
  void setFrame(int x, int y, int width, int height);

  // Reflects the CURRENT scroll state onto the widget: `position` in
  // [0,1] (see onUserScroll's own comment for the convention) and
  // `knobProportion` in (0,1] -- the visible fraction of the total
  // content (a full 1.0 means everything already fits, matching
  // NSScroller's own [NSScroller setKnobProportion:] semantics of "how
  // big the thumb should be drawn relative to the track").
  void setPosition(double position, double knobProportion);

  // Hides the widget entirely when nothing is scrollable (content
  // already fits in one screen) -- matches the existing hand-drawn
  // scrollbar's own "don't even show a strip with nothing to scroll"
  // convention (see GraphicalDisplay::Impl::kScrollbarWidth's sibling
  // logic) and the SAME-DAY mouse-drag fix (docs/HISTORY.md 2026-08-09,
  // "j/kは利かない。ところがマウスによる画面ドラッグはできてしまう")
  // that made keyboard/mouse scroll input agree on this exact
  // condition -- the native scrollbar shouldn't disagree with either.
  void setVisible(bool visible);

  // Public (not the usual private-pimpl convention) purely so
  // mac_scrollbar.mm's Objective-C `UsilicScrollerTarget` class -- a
  // plain C struct pointer field on an ObjC object, not a C++ friend
  // declaration, which Objective-C++ class syntax can't express inside
  // an @interface block -- can hold a raw `Impl*` back to its owner
  // without this header needing to expose (or even forward-declare)
  // any Objective-C type itself. Still fully opaque to every OTHER
  // caller: only mac_scrollbar.mm ever defines or dereferences it.
  struct Impl;

 private:
  MacScrollbar();

  std::unique_ptr<Impl> impl_;
};

}  // namespace fm
