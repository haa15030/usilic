#import <Cocoa/Cocoa.h>

#include "mac_cursor.hpp"

// See mac_cursor.hpp's own comment for the full history/reasoning.
// Renders NSCursor.openHandCursor's NSImage into an off-screen
// NSBitmapImageRep at its own native pixel size, then copies the raw
// RGBA bytes out -- this is the standard "render an NSImage into a
// bitmap I can read" recipe (an NSImage itself may be resolution-
// independent/vector-backed, e.g. PDF-backed for system cursors, so it
// has no bitmapData of its own to read directly).

namespace fm {

CursorImage extractOpenHandCursorImage() {
  CursorImage result;

  NSCursor *cursor = [NSCursor openHandCursor];
  NSImage *image = [cursor image];
  if (!image) return result;

  NSSize size = [image size];
  int width = static_cast<int>(size.width);
  int height = static_cast<int>(size.height);
  if (width <= 0 || height <= 0) return result;

  NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:NULL
                     pixelsWide:width
                     pixelsHigh:height
                  bitsPerSample:8
                samplesPerPixel:4
                       hasAlpha:YES
                       isPlanar:NO
                 colorSpaceName:NSDeviceRGBColorSpace
                    bytesPerRow:0
                   bitsPerPixel:0];
  if (!bitmap) return result;

  NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
  if (!context) return result;

  [NSGraphicsContext saveGraphicsState];
  [NSGraphicsContext setCurrentContext:context];
  [image drawInRect:NSMakeRect(0, 0, width, height)
            fromRect:NSZeroRect
           operation:NSCompositingOperationCopy
            fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];

  unsigned char *data = [bitmap bitmapData];
  if (!data) return result;

  result.rgba.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  result.width = width;
  result.height = height;

  NSPoint hotspot = [cursor hotSpot];
  result.hotspotX = static_cast<int>(hotspot.x);
  result.hotspotY = static_cast<int>(hotspot.y);
  return result;
}

}  // namespace fm
