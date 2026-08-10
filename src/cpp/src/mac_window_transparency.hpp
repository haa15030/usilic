#pragma once

namespace fm {

// Configures a real macOS window (once, right after creation/sizing)
// to support genuine per-pixel transparency -- added 2026-08-10 as part
// of the "覗き見" (peek-through) scrollbar redesign: see
// GraphicalDisplay::matchWindowSize()'s own comment for the full
// reasoning (replacing an earlier, real-hardware-rejected design that
// physically resized the live SDL window instead). Without this call,
// an ordinary NSWindow is fully opaque -- SDL_RenderClear()'s alpha
// channel is simply ignored by the window server, so a "transparent"
// clear still paints solid pixels. This flips the window (and its
// content view's backing layer, which SDL creates for its own
// Metal/GL rendering and which independently defaults to opaque for
// performance) to actually respect per-pixel alpha, so flush() can
// leave the scrollbar strip's pixels at alpha=0 while the rest of the
// window stays fully opaque (alpha=255) as always -- a real per-pixel
// "hole" showing whatever real window sits behind this one, not a
// uniform SDL_SetWindowOpacity()-style whole-window fade (which would
// affect visibility everywhere, not just the strip).
//
// `nsWindow`: the native NSWindow*, obtained by the caller via SDL's
// own SDL_GetWindowWMInfo() (info.info.cocoa.window) and passed here
// as an opaque void* so this header stays free of Objective-C/Cocoa
// types, matching mac_cursor.hpp's/mac_scrollbar.hpp's own established
// pattern (only the .mm implementation needs Objective-C++
// compilation). A null/no-op on any other platform (see
// mac_window_transparency_stub.cpp, compiled instead of the .mm
// there) -- NSWindow/AppKit simply don't exist elsewhere, and this
// whole feature only ever applies to the macOS-only "実はすでにGUI"
// overlay launch in the first place.
void enableWindowTransparency(void *nsWindow);

}  // namespace fm
