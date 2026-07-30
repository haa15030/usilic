#include "mac_font_resolver.hpp"

#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

namespace fm {

std::string resolveMacFontPath(const std::string &fontName, double pointSize) {
  CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, fontName.c_str(), kCFStringEncodingUTF8);
  if (!name) return "";
  CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithNameAndSize(name, static_cast<CGFloat>(pointSize));
  CFRelease(name);
  if (!descriptor) return "";

  CFURLRef url = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute));
  CFRelease(descriptor);
  if (!url) return "";

  char pathBuf[1024];
  bool ok = CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8 *>(pathBuf), sizeof(pathBuf));
  CFRelease(url);
  return ok ? std::string(pathBuf) : "";
}

}  // namespace fm

#else

namespace fm {

std::string resolveMacFontPath(const std::string &, double) { return ""; }

}  // namespace fm

#endif
