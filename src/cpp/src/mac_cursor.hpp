#pragma once

#include <vector>

namespace fm {

// Extracted pixel data + click-point ("hotspot") of a real macOS system
// cursor, RGBA8888 top-left-origin, sized rgba.size() == width*height*4.
// width==0 means extraction failed (caller should leave the cursor
// unchanged rather than build a garbage SDL_Cursor from it).
struct CursorImage {
  std::vector<unsigned char> rgba;
  int width = 0;
  int height = 0;
  int hotspotX = 0;
  int hotspotY = 0;
};

// Extracts the REAL macOS "open hand" cursor's (NSCursor.openHand)
// pixels at runtime, so the caller can wrap them in an SDL_Cursor
// (SDL_CreateColorCursor) and drive it through SDL's own SDL_SetCursor()
// tracking -- added 2026-08-10, replacing an EARLIER version of this
// file that called [[NSCursor openHandCursor] set] directly. That
// direct approach displayed correctly on mouse-down but reverted to
// the plain arrow the instant a drag actually started moving (a real
// report: "画面ドラッグを開始したとたんに矢印に戻る"): SDL's own Cocoa
// backend re-asserts ITS OWN tracked SDL_Cursor on every mouse-motion
// event, which stomps any cursor set through a side channel SDL
// doesn't know about. setPointingCursor() (graphical_display.cpp)
// never had this problem because SDL_CreateSystemCursor()+SDL_SetCursor()
// keeps SDL's own tracked state in sync. Routing the open-hand cursor
// through the same SDL_SetCursor() path fixes it the same way, while
// still showing the exact real system image (extracted live from the
// running OS) rather than a hand-drawn approximation -- two rounds of
// hand-drawn attempts (a blocky rectangle, then a rounded capsule)
// were both real user rejections ("ロボットの手みたいなやつは却下",
// then "さっき却下のロボット指よりもひどくなった"), and a same-day
// sharp catch established that the reference cursor being compared
// against (the system pointing-hand) was itself a real, undrawn OS
// asset the whole time (confirmed live on this machine:
// /System/Library/Frameworks/ApplicationServices.framework/.../
// HIServices.framework/.../Resources/cursors/pointinghand/ is a real
// directory, same as .../cursors/openhand/).
//
// Deliberately extracts pixels at runtime rather than embedding a copy
// of Apple's cursor.pdf (or any rasterization of it) anywhere in this
// repository -- redistributing a copy of Apple's own UI asset in a
// third-party repo (eventually public on GitHub) was judged a
// licensing concern (2026-08-10); reading/reproducing it live from the
// running OS at each launch avoids that entirely.
//
// A no-op stub returning an empty (width==0) CursorImage on any other
// platform (see mac_cursor_stub.cpp, compiled instead of mac_cursor.mm
// there) -- NSCursor/AppKit simply don't exist elsewhere.
CursorImage extractOpenHandCursorImage();

}  // namespace fm
