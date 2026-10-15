#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "archive_reader.hpp"
#include "child_process.hpp"
#include "native_unzip.hpp"

using namespace fm;

// 2026-10-15(.steering/20261015-windows-native-gui/): Windowsのネイティブの内蔵unzip(nativeUnzip())が、本物の
// unzipと同じく、EPUBの中身(-p)を取り出し、ARCが読める一覧(-l)を作り、展開(-o … -d)できること。
TEST_CASE("nativeUnzip extracts EPUB members, lists and extracts like unzip") {
  const std::string epub = "../web/sample/sample.epub";
  REQUIRE(std::filesystem::exists(epub));
  std::string listing;
  REQUIRE(nativeUnzip({"-l", epub}, listing) == 0);
  ChildProcess::Result realListing = ChildProcess::run({"unzip", "-l", epub});
  auto ours = ZipReader().parseListing(listing);
  auto theirs = ZipReader().parseListing(realListing.output);
  REQUIRE(ours.size() == theirs.size());
  REQUIRE(!ours.empty());
  for (std::size_t i = 0; i < ours.size(); i++) {
    CHECK(ours[i].name == theirs[i].name);
    CHECK(ours[i].size == theirs[i].size);
    if (ours[i].isDirectory) continue;
    std::string ourData;
    CHECK(nativeUnzip({"-p", epub, ours[i].name}, ourData) == 0);
    CHECK(ourData == ChildProcess::run({"unzip", "-p", epub, ours[i].name}).output);
  }
  std::string missing;
  CHECK(nativeUnzip({"-p", epub, "no/such/member"}, missing) == 11);
  CHECK(nativeUnzip({"-p", "../usilicrc", "x"}, missing) == 9);  // zipでない

  std::filesystem::path dir = std::filesystem::temp_directory_path() / "usilic-native-unzip-test";
  std::filesystem::remove_all(dir);
  std::string log;
  CHECK(nativeUnzip({"-o", epub, "mimetype", "-d", dir.string()}, log) == 0);
  std::ifstream in(dir / "mimetype", std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  CHECK(text.str() == "application/epub+zip");
  std::filesystem::remove_all(dir);
}
