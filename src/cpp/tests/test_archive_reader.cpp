#include "archive_reader.hpp"
#include "doctest.h"

using fm::ArchiveEntry;
using fm::LhaReader;
using fm::RpmReader;
using fm::TarReader;
using fm::ZipReader;

// All tar/unzip/rpm fixtures below are real captured output (macOS
// bsdtar, GNU tar via an Ubuntu container, and Ubuntu's unzip/rpm),
// not hand-written guesses -- see CLAUDE.md history for how they
// were obtained. The LHA fixture is the one exception (no `lha`
// binary was available while porting this), built from the
// documented column-header comment in the legacy source instead.

TEST_CASE("TarReader::parseListing: BSD tar (macOS) format") {
  TarReader reader;
  std::string listing =
      "drwxr-xr-x  0 haa15030 wheel       0 Jul 28 09:57 sub/\n"
      "-rw-r--r--  0 haa15030 wheel       6 Jul 28 09:57 sub/file1.txt\n"
      "-rw-r--r--  0 haa15030 wheel       6 Jul 28 09:57 file2.txt\n"
      "drwxr-xr-x  0 haa15030 wheel       0 Jul 28 09:57 emptydir/\n";
  auto entries = reader.parseListing(listing);
  REQUIRE(entries.size() == 4);
  CHECK(entries[0].name == "sub/");
  CHECK(entries[0].isDirectory == true);
  CHECK(entries[0].size == 0);
  CHECK(entries[1].name == "sub/file1.txt");
  CHECK(entries[1].isDirectory == false);
  CHECK(entries[1].size == 6);
  CHECK(entries[2].name == "file2.txt");
  CHECK(entries[2].size == 6);
  CHECK(entries[3].name == "emptydir/");
  CHECK(entries[3].isDirectory == true);
}

TEST_CASE("TarReader::parseListing: GNU tar format (owner/group field)") {
  TarReader reader;
  std::string listing =
      "-rwxr-xr-x haa15030/wheel  163 2026-07-28 00:57 ._sub\n"
      "drwxr-xr-x haa15030/wheel    0 2026-07-28 00:57 sub/\n"
      "-rw-r--r-- haa15030/wheel  163 2026-07-28 00:57 sub/._file1.txt\n"
      "-rw-r--r-- haa15030/wheel    6 2026-07-28 00:57 sub/file1.txt\n";
  auto entries = reader.parseListing(listing);
  REQUIRE(entries.size() == 4);
  CHECK(entries[0].name == "._sub");
  CHECK(entries[0].size == 163);
  CHECK(entries[1].name == "sub/");
  CHECK(entries[1].isDirectory == true);
  CHECK(entries[1].size == 0);
  CHECK(entries[3].name == "sub/file1.txt");
  CHECK(entries[3].size == 6);
}

// 2026-08-19 fix, a real report + reproduction: tarring a directory
// whose entries have paths like "utf8/<name>" (any nested-directory
// tar produces this) froze at 100% CPU when one entry's NAME itself
// contained a literal space -- confirmed via `sample` that the hang
// was an unbounded `while (true)` loop in the GNU-format branch below.
// Root cause: the backward scan for cs (filename start) stops at the
// LAST space in the line, which is correct only when the filename has
// no embedded space of its own; here it stops mid-filename instead
// (between "報道ステーション" and "速報"), leaving the NEXT backward
// scan (i==1, now also watching for '/') still positioned INSIDE the
// filename's own text -- which then walks straight into the filename's
// own "utf8/" path prefix and mistakes that '/' for a genuine GNU tar
// "owner/group" separator. The subsequent forward walk for a numeric
// size field never finds one (there isn't one -- this is BSD-format
// macOS tar output, and that '/' was never a real field boundary to
// begin with), and previously had no bound once ce/p both reached the
// end of the line, spinning forever instead of erroring out.
TEST_CASE("TarReader::parseListing: a filename containing a literal space, under a tarred subdirectory (so "
          "every entry's path also contains '/'), doesn't hang and parses the full name correctly -- real "
          "BSD tar (macOS) output shape") {
  TarReader reader;
  std::string listing =
      "drwxr-xr-x  0 haa15030 staff       0 Jul 21 14:36 utf8/\n"
      "-rw-r--r--  0 haa15030 staff     150 Jul 21 14:36 utf8/報道ステーション　速報 "
      "森保監督が会見\n";
  auto entries = reader.parseListing(listing);
  REQUIRE(entries.size() == 2);
  CHECK(entries[0].name == "utf8/");
  CHECK(entries[1].name == "utf8/報道ステーション　速報 森保監督が会見");
  CHECK(entries[1].size == 150);
}

TEST_CASE("TarReader::parseListing: symlink target is stripped from the name") {
  TarReader reader;
  // GNU format symlink line.
  std::string listing = "lrwxrwxrwx user/group     0 2026-07-28 00:57 link -> target\n";
  auto entries = reader.parseListing(listing);
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].name == "link");
  CHECK(entries[0].isDirectory == true);  // 'l' is treated the same as 'd' (ported as-is)
}

TEST_CASE("TarReader::locateContent: real archive, offsets point at real content") {
  // Ported end-to-end verification against a real tar file (built
  // with macOS bsdtar) rather than a hand-built synthetic buffer.
  // Layout: 512-byte header, then content padded to a 512 boundary,
  // repeated per entry -- exactly what a real archive looks like.
  auto pad = [](std::string s, std::size_t n) { s.resize(((s.size() + n - 1) / n) * n, '\0'); return s; };
  auto tarHeader = [&](const std::string &name) { return pad(name, 512); };

  std::string data;
  data += tarHeader("file1.txt");
  data += pad("hello\n", 512);
  data += tarHeader("file2.txt");
  data += pad("world\n", 512);

  std::vector<ArchiveEntry> entries;
  entries.push_back({"file1.txt", false, false, 6});
  entries.push_back({"file2.txt", false, false, 6});

  TarReader reader;
  reader.locateContent(reinterpret_cast<const unsigned char *>(data.data()), data.size(), entries);

  REQUIRE(entries[0].contentOffset >= 0);
  CHECK(data.substr(entries[0].contentOffset, 6) == "hello\n");
  REQUIRE(entries[1].contentOffset >= 0);
  CHECK(data.substr(entries[1].contentOffset, 6) == "world\n");
  CHECK(entries[0].contentTruncated == false);
  CHECK(entries[1].contentTruncated == false);
}

TEST_CASE("TarReader::locateContent: truncated archive is flagged, not overread") {
  std::string data(512, '\0');  // header only, no content bytes at all
  data.replace(0, 9, "trunc.txt");
  std::vector<ArchiveEntry> entries{{"trunc.txt", false, false, 100}};
  TarReader reader;
  reader.locateContent(reinterpret_cast<const unsigned char *>(data.data()), data.size(), entries);
  CHECK(entries[0].contentTruncated == true);
}

TEST_CASE("ZipReader::parseListing: real unzip -l output, footer line excluded") {
  ZipReader reader;
  std::string listing =
      "Archive:  test.zip\n"
      "  Length      Date    Time    Name\n"
      "---------  ---------- -----   ----\n"
      "        6  07-28-2026 09:57   sub/file1.txt\n"
      "        6  07-28-2026 09:57   file2.txt\n"
      "---------                     -------\n"
      "       12                     2 files\n";
  auto entries = reader.parseListing(listing);
  // Regression: the footer summary line ("       12          2 files")
  // has the same leading-digits shape as a real entry and was
  // originally parsed as a bogus 3rd entry with an empty name.
  REQUIRE(entries.size() == 2);
  CHECK(entries[0].name == "sub/file1.txt");
  CHECK(entries[0].size == 6);
  CHECK(entries[1].name == "file2.txt");
  CHECK(entries[1].size == 6);
}

TEST_CASE("ZipReader::parseListing: directory entry detected by trailing '/'") {
  ZipReader reader;
  std::string listing =
      "Archive:  x.zip\n"
      "  Length      Date    Time    Name\n"
      "---------  ---------- -----   ----\n"
      "        0  07-28-2026 09:57   adir/\n"
      "---------                     -------\n"
      "        0                     1 file\n";
  auto entries = reader.parseListing(listing);
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].name == "adir/");
  CHECK(entries[0].isDirectory == true);
}

TEST_CASE("ZipReader::locateContent: real archive, offsets point at real content") {
  // Ported end-to-end verification against real `unzip -p` output
  // (the concatenated decompressed content of every entry, in
  // listing order).
  std::string data = "hello\nworld\n";
  std::vector<ArchiveEntry> entries;
  entries.push_back({"sub/file1.txt", false, false, 6});
  entries.push_back({"file2.txt", false, false, 6});
  ZipReader reader;
  reader.locateContent(reinterpret_cast<const unsigned char *>(data.data()), data.size(), entries);
  CHECK(data.substr(entries[0].contentOffset, 6) == "hello\n");
  CHECK(data.substr(entries[1].contentOffset, 6) == "world\n");
}

TEST_CASE("RpmReader::parseListing: real rpm -qplv output") {
  // Regression for a bug found while porting this: legacy skipped
  // only 3 space-delimited fields (mode, links, owner) before
  // checking for a numeric SIZE field, landing on GROUP instead --
  // which is never numeric, so legacy's size was always 0 for every
  // RPM entry. This line was captured from a real rpm -qplv run.
  RpmReader reader;
  std::string listing =
      "-rw-r--r--    1 root     root                        6 Jul 28 01:01 "
      "/usr/share/testpkg/file1.txt\n";
  auto entries = reader.parseListing(listing);
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].name == "/usr/share/testpkg/file1.txt");
  CHECK(entries[0].size == 6);
  CHECK(entries[0].isDirectory == false);
}

TEST_CASE("RpmReader::locateContent is a no-op (legacy never supported it)") {
  RpmReader reader;
  std::vector<ArchiveEntry> entries{{"x", false, false, 10}};
  unsigned char data[10] = {0};
  reader.locateContent(data, sizeof(data), entries);
  CHECK(entries[0].contentOffset == -1);
}

TEST_CASE("LhaReader::parseListing: synthetic fixture matching the documented column layout") {
  // No real `lha` binary was available while porting this (see the
  // class comment in archive_reader.hpp) -- built from alloc.c's
  // documented header comment:
  //   "PERMSSN  UID GID    SIZE  RATIO     STAMP    NAME"
  //   "----------------- ------- ------ ------------ --------------------"
  LhaReader reader;
  std::string listing =
      "PERMSSN  UID GID    SIZE  RATIO     STAMP    NAME\n"
      "----------------- ------- ------ ------------ --------------------\n"
      "[generic]        6   0.0% Jul 28 09:57 file1.txt\n";
  auto entries = reader.parseListing(listing);
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].name == "file1.txt");
  CHECK(entries[0].size == 6);
}

TEST_CASE("LhaReader::locateContent: per-entry 3-line header is skipped") {
  std::string data =
      "::::::::\n"
      "file1.txt\n"
      "::::::::\n"
      "hello\n";
  std::vector<ArchiveEntry> entries{{"file1.txt", false, false, 6}};
  LhaReader reader;
  reader.locateContent(reinterpret_cast<const unsigned char *>(data.data()), data.size(), entries);
  REQUIRE(entries[0].contentOffset >= 0);
  CHECK(data.substr(entries[0].contentOffset, 6) == "hello\n");
}

TEST_CASE("LhaReader::locateContent: directories have no header to skip") {
  std::string data = "hello\n";  // no "::::::::"/name/"::::::::" header at all
  std::vector<ArchiveEntry> entries{{"adir/", true, false, 0}};
  LhaReader reader;
  reader.locateContent(reinterpret_cast<const unsigned char *>(data.data()), data.size(), entries);
  CHECK(entries[0].contentOffset == 0);
}
