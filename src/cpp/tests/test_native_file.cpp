#include "doctest.h"

#include <string>

#include "native_file.hpp"

using namespace fm;

// 2026-10-15(.steering/20261015-windows-native-gui/): Windowsのネイティブの内蔵file(nativeFile())が、先頭のバイトから
// よく見る種類を言い当て、-b・--mime-typeの形で答えること。
TEST_CASE("nativeFileType names common formats by their magic bytes") {
  CHECK(nativeFileType(std::string("PK\x03\x04", 4) + std::string(26, '\0') + "mimetypeapplication/epub+zip", false) ==
        "EPUB document");
  CHECK(nativeFileType(std::string("PK\x03\x04", 4) + std::string(40, '\0'), false) == "Zip archive data");
  CHECK(nativeFileType(std::string("PK\x03\x04", 4) + std::string(40, '\0'), true) == "application/zip");
  CHECK(nativeFileType("%PDF-1.7\n", true) == "application/pdf");
  CHECK(nativeFileType("\x89PNG\r\n\x1a\n....", true) == "image/png");
  CHECK(nativeFileType("\xff\xd8\xff\xe0", true) == "image/jpeg");
  CHECK(nativeFileType("hello\n", false) == "ASCII text");
  CHECK(nativeFileType("\xe6\x97\xa5\xe6\x9c\xac\n", false) == "Unicode text, UTF-8 text");
  CHECK(nativeFileType(std::string("ab\0cd", 5), false) == "data");
  CHECK(nativeFileType("", false) == "empty");
}

TEST_CASE("nativeFile prints names unless -b") {
  std::string out;
  CHECK(nativeFile({"-b", "--mime-type", "../web/sample/sample.epub"}, out) == 0);
  CHECK(out == "application/epub+zip\n");
  out.clear();
  nativeFile({"../web/sample/sample.epub"}, out);
  CHECK(out == "../web/sample/sample.epub: EPUB document\n");
}
