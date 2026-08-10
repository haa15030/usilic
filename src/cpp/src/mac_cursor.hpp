#pragma once

namespace fm {

// The REAL macOS system "open hand" cursor (NSCursor.openHand), not a
// hand-drawn SDL_CreateColorCursor bitmap -- added 2026-08-10 after
// two rounds of hand-drawn attempts (first a blocky rectangle
// silhouette, then a rounded-capsule one) were both real user
// rejections ("ロボットの手みたいなやつは却下", then "さっき却下の
// ロボット指よりもひどくなった"). A same-day sharp catch pointed out
// the real reason no hand-drawn attempt was ever going to look right
// next to it: the POINTING-hand cursor already in use for scrollbar
// dragging isn't drawn by this project (or by SDL2) at all -- it's
// macOS's own real system cursor asset (confirmed live on this
// machine: /System/Library/Frameworks/ApplicationServices.framework/
// .../HIServices.framework/.../Resources/cursors/pointinghand/ is a
// real directory, the exact same kind of real system asset as
// .../cursors/openhand/ that a from-scratch approximation was being
// judged against). Deliberately a tiny, opaque, platform-neutral C++
// interface (no Objective-C/Cocoa types) so graphical_display.cpp
// doesn't itself need Objective-C++ compilation -- only this class's
// own .mm implementation (macOS) needs that, matching mac_scrollbar.hpp's
// own established pattern. A no-op on any other platform (see
// mac_cursor_stub.cpp, compiled instead of mac_cursor.mm there).
void setOpenHandCursor();

// The native counterpart to setOpenHandCursor() above -- restores the
// default arrow via the SAME real-NSCursor mechanism, kept deliberately
// symmetric rather than mixing this with SDL_SetCursor(SDL_GetDefaultCursor())
// (a different, SDL-tracked cursor path) for the "off" case.
void clearOpenHandCursor();

}  // namespace fm
