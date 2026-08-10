#import <Cocoa/Cocoa.h>

#include "mac_cursor.hpp"

// See mac_cursor.hpp's own comment for why this exists. Deliberately
// tiny -- unlike mac_scrollbar.mm's NSScroller (a real subview with
// its own target/action wiring, superview attachment, and manual
// retain/release lifetime), NSCursor needs no window handle and no
// object to own/track at all: -[NSCursor set] is a single, stateless
// call that just tells the window server which cursor image to show
// next, so there's no Impl struct, no create()/destroy() pair, and no
// memory-management story here beyond what NSCursor already manages
// internally (its own class-level cursor instances are owned by
// AppKit, not by this code).

namespace fm {

void setOpenHandCursor() { [[NSCursor openHandCursor] set]; }

void clearOpenHandCursor() { [[NSCursor arrowCursor] set]; }

}  // namespace fm
