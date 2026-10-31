#include "platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <vector>

#include "child_process.hpp"

namespace fm {

#ifndef _WIN32  // Windowsはplatform_win.cpp(MoveFileExW)
bool replaceFile(const std::string &from, const std::string &to) {
  return std::rename(from.c_str(), to.c_str()) == 0;
}
#endif

#ifndef _WIN32  // Windowsはplatform_win.cpp(レジストリ)
namespace {
#ifdef __APPLE__  // macOSでしか呼ばない(他ではclangがunused-functionの警告を出す、FreeBSDで発見)
// macOSの版の名前(メジャー番号→名前)。知らない番号は名前無し。
std::string macOSReleaseName(const std::string &version) {
  int major = std::atoi(version.c_str());
  int minor = 0;
  std::size_t dot = version.find('.');
  if (dot != std::string::npos) minor = std::atoi(version.c_str() + dot + 1);
  switch (major) {
    case 10:
      return minor == 13 ? "High Sierra" : minor == 14 ? "Mojave" : minor == 15 ? "Catalina" : "";
    case 11: return "Big Sur";
    case 12: return "Monterey";
    case 13: return "Ventura";
    case 14: return "Sonoma";
    case 15: return "Sequoia";
    case 26: return "Tahoe";
    default: return "";
  }
}
#endif

#ifndef __APPLE__
// /etc/os-releaseの「KEY=値」の値(引用符を外す)。
std::string osReleaseValue(const std::string &text, const std::string &key) {
  std::size_t at = 0;
  while (at < text.size()) {
    std::size_t nl = text.find('\n', at);
    std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    if (line.compare(0, key.size() + 1, key + "=") == 0) {
      std::string value = line.substr(key.size() + 1);
      if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
        value = value.substr(1, value.size() - 2);
      return value;
    }
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  return "";
}
#endif
}  // namespace

std::string osDisplayName() {
#ifdef __APPLE__
  ChildProcess::Result result = ChildProcess::run({"sw_vers", "-productVersion"});
  std::string version = result.ok ? result.output : "";
  while (!version.empty() && (version.back() == '\n' || version.back() == '\r' || version.back() == ' '))
    version.pop_back();
  if (version.empty()) return "macOS";
  std::string name = macOSReleaseName(version);
  return "macOS " + (name.empty() ? "" : name + " ") + version;
#else
  std::ifstream in("/etc/os-release");
  if (!in) in.open("/usr/lib/os-release");
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  // 「Ubuntu」+「26.10 (Stonking Stingray)」→「Ubuntu 26.10 Stonking Stingray」。VERSIONが無ければPRETTY_NAME。
  std::string name = osReleaseValue(text, "NAME"), version = osReleaseValue(text, "VERSION");
  std::string out;
  if (!name.empty() && !version.empty()) {
    out = name + " ";
    for (char c : version)
      if (c != '(' && c != ')' && c != ',') out += c;
    while (out.find("  ") != std::string::npos) out.erase(out.find("  "), 1);
  } else {
    out = osReleaseValue(text, "PRETTY_NAME");
  }
  if (out.empty()) {
    ChildProcess::Result result = ChildProcess::run({"uname", "-sr"});
    out = result.ok ? result.output : "";
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  }
  return out;
#endif
}
#endif

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

bool isMsys() {
#if defined(__MSYS__) || defined(__CYGWIN__)
  return true;
#else
  return false;
#endif
}

bool hasWindowsHost() { return isWsl() || isMsys() || isNativeWindows(); }

std::string windowsPathFor(const std::string &path) {
#ifdef _WIN32  // ネイティブ(.steering/20261015-windows-native-gui/): もうWindowsのパス、区切りを\に
  std::string native = path;
  std::replace(native.begin(), native.end(), '/', '\\');
  return native;
#else
  ChildProcess::Result result = ChildProcess::run({isMsys() ? "cygpath" : "wslpath", "-w", path});
  if (!result.ok) return "";
  std::string out = result.output;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
#endif
}

bool openOnWindowsHost(const std::string &target) {
#ifdef _WIN32
  return openWithWindowsShell(target);
#else
  std::string arg = target;
  if (target.compare(0, 7, "http://") != 0 && target.compare(0, 8, "https://") != 0) {
    arg = windowsPathFor(target);
    if (arg.empty()) return false;
  }
  ChildProcess::Result result = ChildProcess::run({"explorer.exe", arg});
  return result.exitStatus != -1 && result.exitStatus != 127;  // 127: 起動できなかった(コマンドが無い)
#endif
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
  if (isMsys()) {
    std::ofstream out("/dev/clipboard", std::ios::binary);
    out << utf8;
    return static_cast<bool>(out);
  }
  std::string utf16 = utf8ToUtf16LeWithBom(utf8);
  ChildProcess::Options opts;
  opts.input = &utf16;
  ChildProcess::Result result = ChildProcess::run({"clip.exe"}, opts);
  return result.ok;
}

}  // namespace fm
