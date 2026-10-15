#include "native_unzip.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>

#include "third_party/stb_image.h"  // stbi_zlib_decode_noheader_malloc()(実装はstb_image_impl.cpp)

namespace fm {

namespace {

struct ZipEntry {
  std::string name;
  int method = 0;
  std::uint32_t compressedSize = 0, size = 0, localOffset = 0;
  std::uint16_t dosTime = 0, dosDate = 0;
};

std::uint32_t u16(const std::string &d, std::size_t p) {
  return static_cast<unsigned char>(d[p]) | (static_cast<unsigned char>(d[p + 1]) << 8);
}
std::uint32_t u32(const std::string &d, std::size_t p) { return u16(d, p) | (u16(d, p + 2) << 16); }

// 最後に読んだ書庫(EPUBは章ごとに何度も読むので、同じファイル・同じ更新時刻なら読み直さない)。
std::mutex g_cacheMutex;
std::string g_cachePath;
std::filesystem::file_time_type g_cacheTime;
std::string g_cacheData;

std::optional<std::string> readArchive(const std::string &path) {
  std::error_code ec;
  auto time = std::filesystem::last_write_time(path, ec);
  if (ec) return std::nullopt;
  std::lock_guard<std::mutex> lock(g_cacheMutex);
  if (path == g_cachePath && time == g_cacheTime) return g_cacheData;
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream data;
  data << in.rdbuf();
  g_cachePath = path;
  g_cacheTime = time;
  g_cacheData = data.str();
  return g_cacheData;
}

// 中央ディレクトリ(書庫の終わりの目次)から並びを読む。zipでなければnullopt。
std::optional<std::vector<ZipEntry>> readDirectory(const std::string &d) {
  if (d.size() < 22) return std::nullopt;
  std::size_t eocd = std::string::npos;
  std::size_t lowest = d.size() > 22 + 65535 ? d.size() - 22 - 65535 : 0;
  for (std::size_t p = d.size() - 22 + 1; p-- > lowest;)
    if (u32(d, p) == 0x06054b50) {
      eocd = p;
      break;
    }
  if (eocd == std::string::npos) return std::nullopt;
  std::uint32_t count = u16(d, eocd + 10), offset = u32(d, eocd + 16);
  std::vector<ZipEntry> entries;
  std::size_t p = offset;
  for (std::uint32_t i = 0; i < count; i++) {
    if (p + 46 > d.size() || u32(d, p) != 0x02014b50) return std::nullopt;
    ZipEntry e;
    e.method = static_cast<int>(u16(d, p + 10));
    e.dosTime = static_cast<std::uint16_t>(u16(d, p + 12));
    e.dosDate = static_cast<std::uint16_t>(u16(d, p + 14));
    e.compressedSize = u32(d, p + 20);
    e.size = u32(d, p + 24);
    std::uint32_t nameLen = u16(d, p + 28), extraLen = u16(d, p + 30), commentLen = u16(d, p + 32);
    e.localOffset = u32(d, p + 42);
    if (p + 46 + nameLen > d.size()) return std::nullopt;
    e.name = d.substr(p + 46, nameLen);
    entries.push_back(e);
    p += 46 + nameLen + extraLen + commentLen;
  }
  return entries;
}

std::optional<std::string> extract(const std::string &d, const ZipEntry &e) {
  std::size_t p = e.localOffset;
  if (p + 30 > d.size() || u32(d, p) != 0x04034b50) return std::nullopt;
  std::size_t data = p + 30 + u16(d, p + 26) + u16(d, p + 28);
  if (data + e.compressedSize > d.size()) return std::nullopt;
  if (e.method == 0) return d.substr(data, e.compressedSize);
  if (e.method != 8) return std::nullopt;
  if (e.size == 0) return std::string();
  int length = 0;
  char *out = stbi_zlib_decode_noheader_malloc(d.data() + data, static_cast<int>(e.compressedSize), &length);
  if (!out) return std::nullopt;
  std::string result(out, static_cast<std::size_t>(length));
  std::free(out);
  return result;
}

std::string unescapeMember(const std::string &name) {
  std::string out;
  for (std::size_t i = 0; i < name.size(); i++) {
    if (name[i] == '\\' && i + 1 < name.size()) i++;
    out += name[i];
  }
  return out;
}

bool selected(const ZipEntry &e, const std::vector<std::string> &members) {
  if (members.empty()) return true;
  for (const auto &m : members)
    if (e.name == m) return true;
  return false;
}

}  // namespace

int nativeUnzip(const std::vector<std::string> &args, std::string &output) {
  bool pipe = false, list = false;
  std::string archive, destination = ".";
  std::vector<std::string> members;
  for (std::size_t i = 0; i < args.size(); i++) {
    const std::string &a = args[i];
    if (a == "-d" && i + 1 < args.size()) {
      destination = args[++i];
    } else if (a.size() > 1 && a[0] == '-') {
      pipe = pipe || a.find('p') != std::string::npos;
      list = list || a.find('l') != std::string::npos;
    } else if (archive.empty()) {
      archive = a;
    } else {
      members.push_back(unescapeMember(a));
    }
  }
  auto data = readArchive(archive);
  if (!data) return 9;
  auto entries = readDirectory(*data);
  if (!entries) return 9;

  if (list) {
    output += "Archive:  " + archive + "\n  Length      Date    Time    Name\n---------  ---------- -----   ----\n";
    unsigned long long total = 0;
    int files = 0;
    for (const auto &e : *entries) {
      if (!selected(e, members)) continue;
      char line[64];
      std::snprintf(line, sizeof line, "%9lu  %02u-%02u-%04u %02u:%02u   ", static_cast<unsigned long>(e.size),
                    (e.dosDate >> 5) & 15u, e.dosDate & 31u, (e.dosDate >> 9) + 1980u, e.dosTime >> 11,
                    (e.dosTime >> 5) & 63u);
      output += line + e.name + "\n";
      total += e.size;
      files++;
    }
    char footer[96];
    std::snprintf(footer, sizeof footer, "---------                     -------\n%9llu                     %d file%s\n",
                  total, files, files == 1 ? "" : "s");
    output += footer;
    return 0;
  }

  bool found = false;
  for (const auto &e : *entries) {
    if (!selected(e, members)) continue;
    found = true;
    if (!e.name.empty() && e.name.back() == '/') {
      if (!pipe) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(destination) / e.name, ec);
      }
      continue;
    }
    auto content = extract(*data, e);
    if (!content) return 2;
    if (pipe) {
      output += *content;
      continue;
    }
    std::filesystem::path target = std::filesystem::path(destination) / e.name;
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out << *content;
    if (!out) return 2;
    output += "  inflating: " + target.string() + "\n";
  }
  return found ? 0 : 11;
}

}  // namespace fm
