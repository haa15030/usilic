#include "unicode_normalize.hpp"

#ifdef __APPLE__

#include <cstring>

#include <CoreFoundation/CoreFoundation.h>

namespace fm {

namespace {

// mac_font_resolver.cpp自身のCoreFoundation往復処理(CFStringRefを
// 受け取り、サイズ調整済みstd::stringを返す)と同じ形――別の翻訳単位
// であり共有はしていない、このコードベース全体で既に重複している他の
// 小さなローカルヘルパー(例えばlocate_mode.cpp自身の
// shellQuoteArgForUpdateDb())と同じパターン。
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

// ヘッダー自身のコメント参照――このバグの根本原因(APFSの正規化を
// 「保持」する挙動)はmacOS固有である。ext4を含むほとんどのLinux
// ファイルシステムは実際には同じ「どちらの形式も共存しうる」という
// 性質を示さず、CFStringNormalize()自体もこのビルドに移植可能な
// 同等品が無いCoreFoundation APIである(mac_font_resolver.cpp自身の
// #elseスタブの慣習と同じ)。
std::vector<std::string> unicodeNormalizationCandidates(const std::string &text) { return {text}; }

}  // namespace fm

#endif
