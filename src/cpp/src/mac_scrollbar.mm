#import <Cocoa/Cocoa.h>
#import <SDL2/SDL.h>
#import <SDL2/SDL_syswm.h>

#include "mac_scrollbar.hpp"

// UsilicScrollerTarget: the NSScroller's target/action destination.
// A separate tiny NSObject rather than making MacScrollbar::Impl
// itself an Objective-C object, since Impl is a plain C++ struct
// shared with the (non-Objective-C) rest of this class's interface --
// splitting them keeps the ObjC/C++ boundary at exactly one small,
// obvious point. No ARC (this project doesn't build with -fobjc-arc;
// see the Makefile's own comment on why), so this uses classic
// manual retain/release throughout.
@interface UsilicScrollerTarget : NSObject {
 @public
  fm::MacScrollbar::Impl *owner;  // never owns/retains -- see ~MacScrollbar()'s own teardown order
}
- (void)scrollerAction:(id)sender;
@end

namespace fm {
struct MacScrollbar::Impl {
  NSScroller *scroller = nil;
  UsilicScrollerTarget *target = nil;
  std::function<void(double)> onUserScroll;
};
}  // namespace fm

@implementation UsilicScrollerTarget
- (void)scrollerAction:(id)sender {
  if (!owner || !owner->onUserScroll) return;
  NSScroller *scroller = (NSScroller *)sender;
  // NSScrollerKnob (dragging the thumb) and NSScrollerKnobSlot
  // (clicking the track) both leave -doubleValue already holding the
  // "jump to this fraction" AppKit itself computed -- forwarded as-is
  // to onUserScroll(). This is the modern macOS default click
  // behavior (System Settings > Appearance > "Click in the scroll
  // bar to: Jump to the spot that's clicked"); the classic
  // "Jump to the next page" alternative, and the up/down arrow hit
  // parts (NSScrollerDecrementLine/IncrementLine), do NOT get their
  // -doubleValue auto-updated by AppKit at all -- a real app is
  // expected to inspect -hitPart and compute the new value itself for
  // those. Left unhandled for this first version (same scope the
  // existing hand-drawn scrollbar has -- see ScrollbarEvent's own
  // comment: only ButtonDown/Motion/ButtonUp knob-drag gestures, no
  // page/line-step buttons either), rather than silently doing the
  // wrong thing for a gesture nobody asked for yet.
  NSScrollerPart part = [scroller hitPart];
  if (part == NSScrollerKnob || part == NSScrollerKnobSlot) {
    owner->onUserScroll([scroller doubleValue]);
  }
}
@end

namespace fm {

MacScrollbar::MacScrollbar() = default;

MacScrollbar::~MacScrollbar() {
  if (!impl_) return;
  // Order matters: detach from the view hierarchy (releases the
  // hierarchy's own retain) before releasing our own -- and clear
  // -setTarget:/-setAction: first, since NSControl's target is a
  // classic unretained (weak) reference under manual memory
  // management: nothing else un-sets it, so a scroller click arriving
  // mid-teardown (unlikely on this single-threaded event loop, but not
  // impossible if SDL_Quit() itself pumps one more event internally)
  // could otherwise message an already-freed target.
  if (impl_->scroller) {
    [impl_->scroller setTarget:nil];
    [impl_->scroller removeFromSuperview];
    [impl_->scroller release];
  }
  if (impl_->target) {
    [impl_->target release];
  }
}

int MacScrollbar::preferredWidth() {
  CGFloat width = [NSScroller scrollerWidthForControlSize:NSControlSizeRegular scrollerStyle:NSScrollerStyleLegacy];
  return width > 0 ? static_cast<int>(width + 0.5) : 15;
}

std::unique_ptr<MacScrollbar> MacScrollbar::create(void *sdlWindowPtr, std::function<void(double)> onUserScroll) {
  SDL_Window *sdlWindow = static_cast<SDL_Window *>(sdlWindowPtr);
  if (!sdlWindow) return nullptr;

  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(sdlWindow, &info)) return nullptr;
  if (info.subsystem != SDL_SYSWM_COCOA) return nullptr;
  NSWindow *nsWindow = info.info.cocoa.window;
  NSView *contentView = nsWindow ? [nsWindow contentView] : nil;
  if (!contentView) return nullptr;

  // NSScrollerStyleLegacy: an always-visible track+knob, matching a
  // real terminal's own scrollbar (which the user's own real-hardware
  // "Show scroll bars" System Settings preference controls for every
  // NSScroller-based app uniformly, Terminal.app/Emacs/this one alike)
  // -- the modern default (NSScrollerStyleOverlay) is a thin auto-
  // hiding sliver only shown transiently during a scroll gesture/
  // hover, which wouldn't visually fill (or make obviously
  // interactive) the space this class reserves for it. Width/position
  // colors are NOT set here at all -- see this file's own knob/track
  // color comment further down for why that's correct, not an
  // oversight.
  CGFloat width = preferredWidth();
  NSScroller *scroller = [[NSScroller alloc] initWithFrame:NSMakeRect(0, 0, width, 100)];
  [scroller setScrollerStyle:NSScrollerStyleLegacy];
  [scroller setEnabled:YES];
  [contentView addSubview:scroller];

  std::unique_ptr<MacScrollbar> result(new MacScrollbar());
  result->impl_ = std::make_unique<Impl>();
  result->impl_->scroller = scroller;
  result->impl_->onUserScroll = std::move(onUserScroll);

  UsilicScrollerTarget *target = [[UsilicScrollerTarget alloc] init];
  target->owner = result->impl_.get();
  result->impl_->target = target;
  [scroller setTarget:target];
  [scroller setAction:@selector(scrollerAction:)];

  return result;
}

void MacScrollbar::setFrame(int x, int y, int width, int height) {
  if (!impl_ || !impl_->scroller) return;
  // `x, y, width, height` is in this project's own established
  // convention (top-left origin, matching every other pixel
  // coordinate in graphical_display.cpp -- kMarginX/kMarginY etc.),
  // NOT AppKit's own bottom-left-origin one. Converted here (not by
  // the caller) so graphical_display.cpp never needs to know or care
  // that Cocoa coordinates are flipped -- the same boundary-drawing
  // principle mac_scrollbar.hpp's own class comment gives for keeping
  // Objective-C types out of that file entirely.
  NSView *superview = [impl_->scroller superview];
  CGFloat superHeight = superview ? [superview bounds].size.height : 0;
  CGFloat cocoaY = superHeight - y - height;
  [impl_->scroller setFrame:NSMakeRect(x, cocoaY, width, height)];
}

void MacScrollbar::setPosition(double position, double knobProportion) {
  if (!impl_ || !impl_->scroller) return;
  [impl_->scroller setDoubleValue:position];
  [impl_->scroller setKnobProportion:knobProportion];
}

void MacScrollbar::setVisible(bool visible) {
  if (!impl_ || !impl_->scroller) return;
  [impl_->scroller setHidden:!visible];
}

// No setKnobColor:/setTrackColor:-style method exists anywhere in this
// file, and deliberately so -- a real user request (2026-08-09,
// "つまみ色、背景色...をターミナル標準に合わせられる？") asked for
// exactly this, but a plain NSScroller has no public API for
// overriding its own knob/track color at all: it always draws using
// whatever the CURRENT SYSTEM APPEARANCE dictates (light/dark mode,
// accent color, increased-contrast accessibility settings...), the
// exact same rendering path Terminal.app/Finder/Emacs's own
// EmacsScroller all go through. That's not a gap to fill in -- it's
// the reason colors need zero code here at all: this scroller already
// matches "ターミナル標準" automatically, on every machine, in every
// appearance mode, for free, by virtue of being the same system
// control. The only way to get an INDEPENDENTLY colored knob/track
// would be to stop using a plain NSScroller and subclass it with a
// custom -drawKnob/-drawKnobSlotInRect:highlight: override (drawing
// pixels by hand again, the exact "custom drawing to get out of that
// business entirely" problem this whole class was written to avoid --
// see mac_scrollbar.hpp's own class comment) -- not attempted here.

}  // namespace fm
