#include "doctest.h"

#include "platform.hpp"

using namespace fm;

TEST_CASE("procVersionLooksLikeWsl recognizes WSL2's kernel string") {
  CHECK(procVersionLooksLikeWsl("Linux version 5.15.153.1-microsoft-standard-WSL2 (root@1ff5ab3f7e1e) ..."));
  CHECK(procVersionLooksLikeWsl("Linux version 4.4.0-19041-Microsoft (Microsoft@Microsoft.com)"));
  CHECK_FALSE(procVersionLooksLikeWsl("Linux version 6.8.0-45-generic (buildd@lcy02-amd64-115) ..."));
}

TEST_CASE("utf8ToUtf16LeWithBom: BOM, Japanese, CRLF, and a surrogate pair") {
  CHECK(utf8ToUtf16LeWithBom("") == std::string("\xFF\xFE", 2));
  CHECK(utf8ToUtf16LeWithBom("A") == std::string("\xFF\xFE" "A\0", 4));
  CHECK(utf8ToUtf16LeWithBom("あ") == std::string("\xFF\xFE" "\x42\x30", 4));  // U+3042
  CHECK(utf8ToUtf16LeWithBom("\n") == std::string("\xFF\xFE" "\r\0\n\0", 6));
  CHECK(utf8ToUtf16LeWithBom("\xF0\x9F\x98\x80") == std::string("\xFF\xFE" "\x3D\xD8\x00\xDE", 6));  // U+1F600
}
