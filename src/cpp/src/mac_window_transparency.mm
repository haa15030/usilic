#import <Cocoa/Cocoa.h>

#include "mac_window_transparency.hpp"

// See mac_window_transparency.hpp's own comment for the full reasoning.
// Three things are needed for a real, working per-pixel-transparent
// NSWindow (confirmed against several independent real-world SDL2+Cocoa
// transparency writeups, since this project has no live display to
// verify the visual result against directly):
//   1. window.opaque = NO + a clear backgroundColor, so AppKit stops
//      treating the window's own backing store as always-fully-opaque.
//   2. The CONTENT VIEW's own CALayer (SDL creates one, wantsLayer=YES,
//      for its Metal/OpenGL renderer) ALSO needs layer.opaque = NO --
//      CoreAnimation's compositor otherwise ignores alpha=0 pixels
//      written into an opaque layer's backing store and paints them
//      solid regardless of what step 1 did at the NSWindow level.
//   3. hasShadow left at its default (YES) -- the window's drop shadow
//      is computed from its opaque content, not from this transparency
//      setting, and the vast majority of this window's own area stays
//      fully opaque (alpha=255) throughout (see flush()'s own comment
//      -- only the thin scrollbar strip, while idle, ever gets alpha=0),
//      so there is no reason to expect the shadow to look wrong.
namespace fm {

void enableWindowTransparency(void *nsWindowPtr) {
  NSWindow *window = (__bridge NSWindow *)nsWindowPtr;
  if (!window) return;
  window.opaque = NO;
  window.backgroundColor = [NSColor clearColor];
  NSView *contentView = window.contentView;
  if (!contentView) return;
  contentView.wantsLayer = YES;
  if (contentView.layer) contentView.layer.opaque = NO;
}

}  // namespace fm
