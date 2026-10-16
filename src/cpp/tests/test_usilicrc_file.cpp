#include "doctest.h"
#include "usilicrc_file.hpp"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

using fm::usilicrc::replaceSection;

TEST_CASE("usilicrc::replaceSection: replaces the value lines and keeps the comments") {
  std::string text =
      "WAIT\n"
      "# 書式: wait\n"
      "\n"
      "\t1\n"
      "\n"
      "KEY\n"
      "\tK-1\tC-g\n";
  CHECK(replaceSection(text, "WAIT", {"5"}, "") ==
        "WAIT\n"
        "# 書式: wait\n"
        "\n"
        "\t5\n"
        "\n"
        "KEY\n"
        "\tK-1\tC-g\n");
}

TEST_CASE("usilicrc::replaceSection: a commented-out value is not replaced, the value goes after the comments") {
  std::string text =
      "FONT\n"
      "# 書式: フォントファイル\n"
      "\n"
      "#\t/a.ttf\n"
      "\n"
      "KEY\n";
  CHECK(replaceSection(text, "FONT", {"/b.ttf"}, "") ==
        "FONT\n"
        "# 書式: フォントファイル\n"
        "\n"
        "#\t/a.ttf\n"
        "\n"
        "\t/b.ttf\n"
        "\n"
        "KEY\n");
}

TEST_CASE("usilicrc::replaceSection: a missing section is copied from the template in front of KEY") {
  std::string text =
      "WAIT\n"
      "\t1\n"
      "\n"
      "KEY\n"
      "\tK-1\tC-g\n";
  std::string templateText =
      "SPEED\n"
      "# 書式: usec ...\n"
      "\n"
      "\t90000 20000\n"
      "\n"
      "KEY\n";
  CHECK(replaceSection(text, "SPEED", {"1 2 3"}, templateText) ==
        "WAIT\n"
        "\t1\n"
        "\n"
        "SPEED\n"
        "# 書式: usec ...\n"
        "\n"
        "\t1 2 3\n"
        "\n"
        "KEY\n"
        "\tK-1\tC-g\n");
}

TEST_CASE("usilicrc::replaceSection: several value lines collapse into the new ones") {
  std::string text =
      "SPEED\n"
      "\t1\n"
      "\t2\n"
      "\n"
      "KEY\n";
  CHECK(replaceSection(text, "SPEED", {"9"}, "") ==
        "SPEED\n"
        "\t9\n"
        "\n"
        "KEY\n");
}

TEST_CASE("usilicrc::replaceMacroMarker / nextMacroNumber: ^X^S writes at the -- marker") {
  std::string text =
      "MACRO\n"
      "\tM-1\t;\tK-1 K-2 K-14\n"
      "\t--\n";
  CHECK(fm::usilicrc::nextMacroNumber(text) == 2);
  CHECK(fm::usilicrc::replaceMacroMarker(text, {"\tM-2\tx\tK-3", "\t--"}) ==
        "MACRO\n"
        "\tM-1\t;\tK-1 K-2 K-14\n"
        "\tM-2\tx\tK-3\n"
        "\t--\n");
}

// 2026-10-16(updateUserIfOlder()のコメント参照): 新しい版のusilicrcに置き換えても、usilic自身が書き込む節(^J^Fの
// FONT・SANS-SERIF・SERIF等)は前の値を引き継ぎ、それ以外の節は新しい版のものになる。
TEST_CASE("usilicrc::updateUserIfOlder: keeps the sections usilic itself writes") {
  namespace fs = std::filesystem;
  fs::path home = fs::temp_directory_path() / ("usilicrc-update-test-" + std::to_string(::getpid()));
  fs::remove_all(home);
  fs::create_directories(home / ".usilic");
  fs::create_directories(home / ".local/lib/usilic");
  auto write = [](const fs::path &path, const std::string &text) { std::ofstream(path, std::ios::binary) << text; };
  write(home / ".usilic/usilicrc", "FONT\n# comment\n\n\tC:/Windows/Fonts/consola.ttf\n\nSERIF\n\n\tC:/Windows/Fonts/msmincho.ttc\n\n"
                                   "EDITOR\n\n\tvi\n\nKEY\n");
  write(home / ".local/lib/usilic/usilicrc", "FONT\n# new comment\n\nSERIF\n# serif\n\nEDITOR\n# editor\n\n\tvim\n\nKEY\n");
  fs::last_write_time(home / ".usilic/usilicrc", fs::file_time_type::clock::now() - std::chrono::hours(1));
  std::string backup = fm::usilicrc::updateUserIfOlder(home.string());
  CHECK(!backup.empty());
  std::ifstream in(home / ".usilic/usilicrc", std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(text.find("# new comment") != std::string::npos);
  CHECK(text.find("\tC:/Windows/Fonts/consola.ttf") != std::string::npos);
  CHECK(text.find("\tC:/Windows/Fonts/msmincho.ttc") != std::string::npos);
  CHECK(text.find("\tvim") != std::string::npos);  // usilic自身が書かない節は新しい版のもの
  CHECK(text.find("\tvi\n") == std::string::npos);
  fs::remove_all(home);
}
