#include "unicode_normalize.hpp"

#ifdef __APPLE__

#include <cstring>

#include <CoreFoundation/CoreFoundation.h>

namespace fm {

namespace {

// Same shape as mac_font_resolver.cpp's own CoreFoundation round-trip
// (CFStringRef in, resized std::string out) -- separate translation
// unit, not shared, same as every other small local helper already
// duplicated across this codebase (see e.g. locate_mode.cpp's own
// shellQuoteArgForUpdateDb()).
std::string normalize(const std::string &utf8, CFStringNormalizationForm form) {
  CFStringRef cf = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(utf8.data()),
                                            static_cast<CFIndex>(utf8.size()), kCFStringEncodingUTF8, false);
  if (!cf) return utf8;
  CFMutableStringRef mutableStr = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cf);
  CFRelease(cf);
  if (!mutableStr) return utf8;
  CFStringNormalize(mutableStr, form);
  CFIndex length = CFStringGetLength(mutableStr);
  CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::string result(static_cast<std::size_t>(maxBytes), '\0');
  if (!CFStringGetCString(mutableStr, result.data(), maxBytes, kCFStringEncodingUTF8)) {
    CFRelease(mutableStr);
    return utf8;
  }
  CFRelease(mutableStr);
  result.resize(std::strlen(result.c_str()));
  return result;
}

}  // namespace

std::vector<std::string> unicodeNormalizationCandidates(const std::string &text) {
  std::vector<std::string> candidates{text};
  std::string nfc = normalize(text, kCFStringNormalizationFormC);
  if (nfc != text) candidates.push_back(nfc);
  std::string nfd = normalize(text, kCFStringNormalizationFormD);
  bool alreadyHaveIt = false;
  for (const auto &c : candidates)
    if (c == nfd) alreadyHaveIt = true;
  if (!alreadyHaveIt) candidates.push_back(nfd);
  return candidates;
}

}  // namespace fm

#else

namespace fm {

// See the header's own comment -- this bug's root cause (APFS's
// normalization-PRESERVING behavior) is macOS-specific; ext4/most
// Linux filesystems don't exhibit the same "either form, coexisting"
// property in practice, and CFStringNormalize() itself is a
// CoreFoundation API with no portable equivalent linked into this
// build (matching mac_font_resolver.cpp's own #else stub convention).
std::vector<std::string> unicodeNormalizationCandidates(const std::string &text) { return {text}; }

}  // namespace fm

#endif
