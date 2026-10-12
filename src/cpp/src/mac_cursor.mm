#import <Cocoa/Cocoa.h>

#include "mac_cursor.hpp"

// 全体の経緯/理由についてはmac_cursor.hpp自身のコメント参照。
// NSCursor.openHandCursorのNSImageを、それ自身のネイティブなピクセル
// サイズでオフスクリーンのNSBitmapImageRepへレンダリングし、その後
// 生のRGBAバイトをコピーして取り出す――これは「NSImageを、自分で
// 読めるビットマップへレンダリングする」という標準的な手順である
// (NSImage自体は解像度に依存しない/ベクター由来であることがあり、
// 例えばシステムカーソルはPDF由来であるため、直接読めるbitmapDataを
// それ自身は持たない)。

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

void systemBeep() { NSBeep(); }

void activateThisApplication() {
  [[NSRunningApplication currentApplication] activateWithOptions:NSApplicationActivateIgnoringOtherApps];
  [NSApp activateIgnoringOtherApps:YES];
}

}  // namespace fm
