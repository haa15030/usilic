#include "native_ls.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace fm {

namespace {

namespace fs = std::filesystem;

struct Entry {
  std::string name;  // -Fの印を付ける前
  char type = '-';   // d・l・-
  std::string mode;  // rwxr-xr-x
  bool executable = false;
  std::uintmax_t size = 0;
  std::time_t mtime = 0;
};

std::time_t toTimeT(fs::file_time_type time) {
  // C++17にはfile_clockとsystem_clockの変換が無いので、今との差で直す。
  auto system = std::chrono::system_clock::now() +
                std::chrono::duration_cast<std::chrono::system_clock::duration>(time - fs::file_time_type::clock::now());
  return std::chrono::system_clock::to_time_t(system);
}

std::string modeText(fs::perms p) {
  auto bit = [&](fs::perms b, char c) { return (p & b) != fs::perms::none ? c : '-'; };
  std::string m;
  m += bit(fs::perms::owner_read, 'r');
  m += bit(fs::perms::owner_write, 'w');
  m += bit(fs::perms::owner_exec, 'x');
  m += bit(fs::perms::group_read, 'r');
  m += bit(fs::perms::group_write, 'w');
  m += bit(fs::perms::group_exec, 'x');
  m += bit(fs::perms::others_read, 'r');
  m += bit(fs::perms::others_write, 'w');
  m += bit(fs::perms::others_exec, 'x');
  return m;
}

Entry entryFor(const fs::path &path, const std::string &name) {
  Entry e;
  e.name = name;
  std::error_code ec;
  fs::file_status link = fs::symlink_status(path, ec);
  fs::file_status st = fs::status(path, ec);
  if (fs::is_symlink(link)) e.type = 'l';
  else if (fs::is_directory(st)) e.type = 'd';
  e.mode = modeText(st.permissions());
  e.executable = fs::is_regular_file(st) && (st.permissions() & fs::perms::owner_exec) != fs::perms::none;
#ifdef _WIN32
  // Windowsのstd::filesystemは実行の権限を返さないので、拡張子で決める。
  if (fs::is_regular_file(st)) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".com") {
      e.executable = true;
      e.mode[2] = e.mode[5] = e.mode[8] = 'x';
    }
  }
  if (fs::is_directory(st)) e.mode[2] = e.mode[5] = e.mode[8] = 'x';
#endif
  if (fs::is_regular_file(st)) e.size = fs::file_size(path, ec);
  if (ec) e.size = 0;
  auto time = fs::last_write_time(path, ec);
  if (!ec) e.mtime = toTimeT(time);
  return e;
}

std::string userName() {
  for (const char *var : {"USER", "USERNAME", "LOGNAME"})
    if (const char *v = std::getenv(var); v && *v) return v;
  return "user";
}

}  // namespace

std::string nativeLsAlF(const std::vector<std::string> &args, bool *ok) {
  bool byTime = false, bySize = false, reverse = false;
  std::string dir = ".";
  for (const auto &arg : args) {
    if (arg.size() > 1 && arg[0] == '-') {
      byTime = byTime || arg.find('t') != std::string::npos;
      bySize = bySize || arg.find('S') != std::string::npos;
      reverse = reverse || arg.find('r') != std::string::npos;
    } else {
      dir = arg;
    }
  }
  fs::path base(dir);
  std::error_code ec;
  fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    if (ok) *ok = false;
    return "";
  }
  std::vector<Entry> entries{entryFor(base / ".", "."), entryFor(base / "..", "..")};
  for (; it != fs::directory_iterator(); it.increment(ec)) {
    if (ec) break;
    entries.push_back(entryFor(it->path(), it->path().filename().string()));
  }
  // .と..は先頭(名前順でも先頭になる)、残りは名前(バイト順、LC_ALL=Cのlsと同じ)・時刻・大きさ。
  std::stable_sort(entries.begin() + 2, entries.end(), [&](const Entry &a, const Entry &b) {
    if (byTime && a.mtime != b.mtime) return a.mtime > b.mtime;
    if (bySize && a.size != b.size) return a.size > b.size;
    return a.name < b.name;
  });
  if (reverse) std::reverse(entries.begin(), entries.end());

  std::size_t sizeWidth = 1;
  for (const auto &e : entries) sizeWidth = std::max(sizeWidth, std::to_string(e.size).size());
  std::string user = userName();
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::time_t now = std::time(nullptr);
  std::string out = "total 0\n";
  for (const auto &e : entries) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &e.mtime);
#else
    localtime_r(&e.mtime, &tm);
#endif
    char date[32];
    // 半年より古い・未来の日付は時刻の代わりに年(lsと同じ)。
    bool recent = e.mtime <= now && now - e.mtime < 182 * 24 * 3600;
    if (recent) std::snprintf(date, sizeof date, "%s %2d %02d:%02d", kMonths[tm.tm_mon], tm.tm_mday, tm.tm_hour, tm.tm_min);
    else std::snprintf(date, sizeof date, "%s %2d  %4d", kMonths[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
    std::string size = std::to_string(e.size);
    std::string name = e.name;
    if (e.type == 'd') name += '/';
    else if (e.type == 'l') name += '@';
    else if (e.executable) name += '*';
    out += e.type + e.mode + "  1 " + user + " " + user + " " + std::string(sizeWidth - size.size(), ' ') + size + " " +
           date + " " + name + "\n";
  }
  if (ok) *ok = true;
  return out;
}

}  // namespace fm
