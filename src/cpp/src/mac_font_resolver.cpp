#include "mac_font_resolver.hpp"

#ifdef __APPLE__

#include <fstream>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

namespace fm {

namespace {

// Terminal.app's default font on any reasonably recent macOS is
// Apple's own "SF Mono", but CTFontDescriptorCreateWithNameAndSize()
// below can NEVER resolve it (confirmed live, 2026-08-03): macOS
// deliberately refuses ordinary name-based lookup for this family --
// a real, live test on this machine returned an EMPTY path for
// "SFMono-Regular" (no error, just silent failure), and returned
// */System/Library/Fonts/Supplemental/Times New Roman.ttf* -- a
// bizarre, unrelated SUBSTITUTE font -- for the dot-prefixed private
// name variant ".SFMono-Regular", with Console logging: "Client
// requested name '.SFMono-Regular', it will get TimesNewRomanPSMT
// rather than the intended font. All system UI font access should be
// through proper APIs such as CTFontCreateUIFontForLanguage() or
// +[NSFont systemFontOfSize:]." -- i.e. this is Apple's own
// intentional API-misuse guard for a handful of reserved system font
// names, not a bug in this project's own lookup logic.
//
// resolveGuiFontPath()'s caller already has a generic fallback
// (Menlo.ttc) for when this whole function fails, so failing here was
// never a hard crash -- but a real user report (2026-08-03, "フォント
// の種類は合っているようだがサイズの取得がNG"): Menlo LOOKS similar
// to SF Mono (both are Apple's own monospace faces) but isn't
// metrically identical, so the GUI window's own cellWidth/cellHeight
// computed from Menlo's "M" glyph (confirmed live: 8px at 14pt) didn't
// match Terminal.app's own actual SF-Mono-based cell size (confirmed
// live: 9px at 14pt, matching the real window's own reported pixel
// dimensions far more closely) -- close enough to "look like the same
// font" but not close enough to produce a correctly-sized window.
//
// The proper sanctioned API the warning message points to
// (+[NSFont systemFontOfSize:] and friends) is Objective-C/AppKit,
// which this project's otherwise-plain-C++ codebase has no bridging
// for yet -- rather than adding an Objective-C++ (.mm) file just for
// this one lookup, SF Mono's own file is simply always installed at a
// FIXED, version-stable system path on any Mac that has it at all
// (confirmed live via `find /System/Library/Fonts -iname "*sfmono*"`),
// so checking for that path directly sidesteps the blocked API
// entirely. Only the Regular weight has its own separate file --
// Bold is applied as a runtime TTF_STYLE_BOLD flag on whichever font
// gets loaded (see GraphicalDisplay::renderCell()), not a separate
// font file, matching how every other font this project loads is
// already handled.
bool isSFMono(const std::string &fontName) {
  return fontName == "SFMono-Regular" || fontName == ".SFMono-Regular" || fontName == "SF Mono" ||
         fontName == "SF Mono Regular";
}

}  // namespace

std::string resolveMacFontPath(const std::string &fontName, double pointSize) {
  if (isSFMono(fontName)) {
    static const char *const path = "/System/Library/Fonts/SFNSMono.ttf";
    std::ifstream probe(path, std::ios::binary);
    if (probe.good()) return path;
  }

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
