#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "native_ls.hpp"

using namespace fm;

namespace {

// DirectoryMode::parseLsLine()と同じ決まり: 月名(前後が空白)の13字後から名前。
std::string nameAfterMonth(const std::string &line) {
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (const char *month : kMonths) {
    std::size_t pos = line.find(std::string(" ") + month + " ");
    if (pos != std::string::npos) return line.substr(pos + 1 + 13);
  }
  return "";
}

}  // namespace

// 2026-10-15(.steering/20261015-windows-native-gui/): Windowsのネイティブの(ls)の一覧(nativeLsAlF())が、
// `ls -alF`と同じくDirectoryModeの読み方で名前を取り出せる形であること。
TEST_CASE("nativeLsAlF: lines parse like ls -alF (names after the month, -F markers, . and .. first)") {
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "usilic-native-ls-test";
  fs::remove_all(dir);
  fs::create_directories(dir / "sub dir");
  std::ofstream(dir / "b.txt") << "12345";
  std::ofstream(dir / "a 日本語.txt") << "x";

  bool ok = false;
  std::string out = nativeLsAlF({"-alF", dir.string() + "/"}, &ok);
  REQUIRE(ok);
  std::istringstream in(out);
  std::string line;
  std::getline(in, line);
  CHECK(line.rfind("total ", 0) == 0);
  std::vector<std::string> names, firstChars;
  while (std::getline(in, line)) {
    names.push_back(nameAfterMonth(line));
    firstChars.push_back(line.substr(0, 1));
  }
  REQUIRE(names.size() == 5);
  CHECK(names[0] == "./");
  CHECK(names[1] == "../");
  CHECK(names[2] == "a 日本語.txt");
  CHECK(names[3] == "b.txt");
  CHECK(names[4] == "sub dir/");
  CHECK(firstChars[4] == "d");
  CHECK(firstChars[3] == "-");

  std::string bySize = nativeLsAlF({"-alF", "-S", dir.string()});
  CHECK(bySize.find("b.txt") < bySize.find("a 日本語.txt"));  // 大きい順

  CHECK(nativeLsAlF({"-alF", (dir / "missing").string()}, &ok).empty());
  CHECK_FALSE(ok);
  fs::remove_all(dir);
}
