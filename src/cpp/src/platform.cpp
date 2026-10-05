#include "platform.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <vector>

#include "child_process.hpp"

namespace fm {

bool procVersionLooksLikeWsl(const std::string &procVersion) {
  std::string lower = procVersion;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  return lower.find("microsoft") != std::string::npos;
}

bool isWsl() {
  static const bool wsl = [] {
#ifdef __APPLE__
    return false;
#else
    struct stat st;
    if (::stat("/proc/sys/fs/binfmt_misc/WSLInterop", &st) == 0) return true;
    std::ifstream in("/proc/version");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return procVersionLooksLikeWsl(text);
#endif
  }();
  return wsl;
}

std::string windowsPathFor(const std::string &path) {
  ChildProcess::Result result = ChildProcess::run({"wslpath", "-w", path});
  if (!result.ok) return "";
  std::string out = result.output;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

bool openOnWindowsHost(const std::string &target) {
  std::string arg = target;
  if (target.compare(0, 7, "http://") != 0 && target.compare(0, 8, "https://") != 0) {
    arg = windowsPathFor(target);
    if (arg.empty()) return false;
  }
  ChildProcess::Result result = ChildProcess::run({"explorer.exe", arg});
  return result.exitStatus != -1 && result.exitStatus != 127;  // 127: 起動できなかった(コマンドが無い)
}

std::string utf8ToUtf16LeWithBom(const std::string &utf8) {
  std::string out = "\xFF\xFE";
  auto put = [&out](unsigned unit) {
    out.push_back(static_cast<char>(unit & 0xFF));
    out.push_back(static_cast<char>((unit >> 8) & 0xFF));
  };
  std::size_t i = 0;
  while (i < utf8.size()) {
    unsigned char c = static_cast<unsigned char>(utf8[i]);
    unsigned cp;
    int len;
    if (c < 0x80) cp = c, len = 1;
    else if ((c & 0xE0) == 0xC0) cp = c & 0x1F, len = 2;
    else if ((c & 0xF0) == 0xE0) cp = c & 0x0F, len = 3;
    else if ((c & 0xF8) == 0xF0) cp = c & 0x07, len = 4;
    else cp = 0xFFFD, len = 1;  // 壊れたバイトは置換文字
    if (len > 1) {
      if (i + len > utf8.size()) {
        cp = 0xFFFD;
        len = 1;
      } else {
        for (int k = 1; k < len; k++) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
      }
    }
    if (c < 0x80 && c == '\n') put('\r');  // Windowsの改行
    if (cp >= 0x10000) {
      cp -= 0x10000;
      put(0xD800 + (cp >> 10));
      put(0xDC00 + (cp & 0x3FF));
    } else {
      put(cp);
    }
    i += len;
  }
  return out;
}

bool copyToWindowsClipboard(const std::string &utf8) {
  std::string utf16 = utf8ToUtf16LeWithBom(utf8);
  ChildProcess::Options opts;
  opts.input = &utf16;
  ChildProcess::Result result = ChildProcess::run({"clip.exe"}, opts);
  return result.ok;
}

}  // namespace fm
