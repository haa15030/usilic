#include "archive_reader.hpp"

#include <cctype>

namespace fm {

namespace {

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t eol = text.find('\n', pos);
    std::string line = (eol == std::string::npos) ? text.substr(pos) : text.substr(pos, eol - pos);
    if (!line.empty()) lines.push_back(line);
    pos = (eol == std::string::npos) ? text.size() : eol + 1;
  }
  return lines;
}

// Ported from alloc.c's alloc_head() T_tar/T_deb case: locates the
// SIZE field and filename start in one `ls -l`-ish tar-listing line.
// tar tvf has two, differently-shaped layouts depending on which tar
// wrote it -- GNU tar prints a combined "owner/group" field (with a
// '/'); BSD tar (macOS) prints owner and group as two separate
// fields, with no '/' anywhere in that region. This scans backward
// from the end of the line looking for that '/' first, and only
// falls back to a fixed 4-field skip from the start (mode, links,
// owner, group) if it never finds one.
ArchiveEntry parseTarLine(const std::string &rawLine) {
  ArchiveEntry entry;

  std::string line = rawLine;
  auto arrow = line.find(" -> ");
  if (arrow != std::string::npos) line.resize(arrow);  // strip "-> symlink-target"

  int n = static_cast<int>(line.size());
  int ce = n - 1;
  int cs = 0;

  for (int i = 0;; i++) {
    for (; ce > 0; ce--)
      if (line[ce] == ' ' || (i && line[ce] == '/')) break;
    if (i == 0) cs = ce + 1;  // filename starts right after the last space
    if (i && line[ce] == '/') {
      // GNU format: walk forward from "owner/group" to the size field.
      while (true) {
        while (ce < n && line[ce] != ' ') ce++;
        while (ce < n && line[ce] == ' ') ce++;
        int p = ce;
        while (p < n && std::isdigit(static_cast<unsigned char>(line[p]))) p++;
        if (p < n && (line[p] == ' ' || line[p] == ',')) break;
        ce = p;
      }
      break;
    } else if (ce == 0) {
      // BSD format: mode links owner group SIZE mon day time/year name.
      int k = 0;
      ce = 0;
      while (k < 4) {
        if (line[ce] == ' ') {
          while (ce + 1 < n && line[ce + 1] == ' ') ce++;
          k++;
        }
        ce++;
      }
      break;
    } else {
      while (ce > 0 && line[ce] == ' ') ce--;
    }
  }

  long size = 0;
  while (ce < n && line[ce] != ' ') {
    if (line[ce] == ',') entry.isSpecialDevice = true;
    else if (!entry.isSpecialDevice) size = size * 10 + (line[ce] - '0');
    ce++;
  }

  entry.name = line.substr(cs);
  entry.size = entry.isSpecialDevice ? 0 : size;
  entry.isDirectory = !line.empty() && (line[0] == 'd' || line[0] == 'l');
  return entry;
}

}  // namespace

std::vector<ArchiveEntry> TarReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  for (const auto &line : splitLines(listing)) entries.push_back(parseTarLine(line));
  return entries;
}

// Ported from find_tar_header(): tar headers are always 512-byte
// aligned from the start of the archive, so only alignment-matching
// positions are checked -- otherwise a hidden entry whose header text
// merely *contains* `name` as a substring (e.g. macOS's "._name"
// AppleDouble sidecar, which "tar tvf" doesn't list but is a real
// header in the byte stream) can be mistaken for it. See CLAUDE.md
// history item 4 for the original bug this fixed.
const unsigned char *findTarHeader(const unsigned char *cp, const unsigned char *base,
                                    const unsigned char *end, const std::string &name) {
  const unsigned char *p = base + (((cp - base) + 511) & ~511);
  for (; p < end; p += 512)
    if (std::string(reinterpret_cast<const char *>(p), name.size()) == name) return p;
  return end;
}

void TarReader::locateContent(const unsigned char *data, std::size_t size,
                               std::vector<ArchiveEntry> &entries) const {
  if (entries.empty() || !size) return;
  const unsigned char *end = data + size - 1;  // ported from alloc_tar()'s `end--`
  const unsigned char *cp = data;

  for (auto &entry : entries) {
    if (cp < end) cp = findTarHeader(cp, data, end, entry.name);
    cp += 512;
    if (cp > end) cp = end;
    entry.contentOffset = cp - data;
    if (entry.isDirectory) continue;
    long remaining = entry.size;
    while (remaining && cp < end) { cp++; remaining--; }
    if (cp == end) entry.contentTruncated = true;
  }
}

// Ported from alloc.c's alloc_head() T_unzip case, with two
// corrections found while testing this against real `unzip -l`
// output:
//
// 1. Directory detection: legacy re-used a mode-letter check
//    ('d'/'l' at the start of the formatted display line) that's
//    meaningful for tar/rpm's ls-style output but never true for
//    `unzip -l` lines (which have no such column at all). Detecting
//    a directory by its name ending in '/' instead -- the same
//    technique legacy already used for LHA (see alloc_head()'s
//    T_lha case) -- is simple, correct, and consistent with the
//    sibling reader.
//
// 2. The footer summary line (e.g. "       12          2 files",
//    printed after the closing "---------" rule) has the exact same
//    shape as a real entry line (leading spaces, then digits) and
//    was being parsed as a bogus entry with an empty name -- legacy
//    has this same weakness (its version produces a bogus entry
//    with garbage/empty content in the same spot). unzip -l's
//    listing has exactly two "---------" rule lines (opening and
//    closing the entry block); this stops at the second one instead
//    of only recognizing rule lines individually, so the footer
//    after it is never reached at all.
std::vector<ArchiveEntry> ZipReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  int ruleLinesSeen = 0;
  for (const auto &rawLine : splitLines(listing)) {
    std::size_t i = 0;
    while (i < rawLine.size() && rawLine[i] == ' ') i++;
    if (i < rawLine.size() && rawLine[i] == '-') {
      if (++ruleLinesSeen >= 2) break;  // closing rule -- stop before the footer
      continue;
    }
    if (ruleLinesSeen < 1) continue;  // still in the "Archive:"/column-header preamble
    if (i >= rawLine.size() || !std::isdigit(static_cast<unsigned char>(rawLine[i]))) continue;

    long size = 0;
    while (i < rawLine.size() && rawLine[i] != ' ') {
      size = size * 10 + (rawLine[i] - '0');
      i++;
    }
    // Skip the DATE and TIME columns to reach NAME.
    for (int field = 0; field < 2; field++) {
      while (i < rawLine.size() && rawLine[i] == ' ') i++;
      while (i < rawLine.size() && rawLine[i] != ' ') i++;
    }
    while (i < rawLine.size() && rawLine[i] == ' ') i++;

    ArchiveEntry entry;
    entry.name = rawLine.substr(i);
    entry.size = size;
    entry.isDirectory = !entry.name.empty() && entry.name.back() == '/';
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Ported from alloc_unzip(): the raw bytes are the decompressed
// concatenation of every entry's content, in listing order (matching
// what `unzip -p <archive>`, extracting every member to stdout back
// to back, actually produces) -- no per-entry header to search for,
// unlike tar.
void ZipReader::locateContent(const unsigned char *data, std::size_t size,
                               std::vector<ArchiveEntry> &entries) const {
  const unsigned char *end = data + size;
  long pos = 0;
  for (auto &entry : entries) {
    if (entry.isDirectory) continue;
    entry.contentOffset = pos;
    long remaining = entry.size;
    while (remaining && data + pos < end) { pos++; remaining--; }
    if (data + pos == end) entry.contentTruncated = true;
  }
}

// Ported from alloc.c's alloc_head() T_lha case. Unverified against a
// real `lha` binary's actual output (none was available while
// porting this) -- built to match the documented header-format
// comment in the legacy source ("PERMSSN UID GID SIZE RATIO STAMP
// NAME" / "----- ... -----"), and the parsing structure (find the
// ratio marker '%'/'*', walk forward past it to NAME, walk backward
// from it to SIZE) is carried over mechanically either way, so it
// should hold regardless of the exact column widths.
std::vector<ArchiveEntry> LhaReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  for (const auto &line : splitLines(listing)) {
    int n = static_cast<int>(line.size());
    int marker = 0;
    while (marker < n && marker < 40 && line[marker] != '%' && line[marker] != '*') marker++;
    if (marker >= 40 || marker >= n) continue;  // header/rule line, not an entry

    // Walk forward from the ratio marker, across STAMP (date + time),
    // to NAME.
    int ce = marker;
    for (int field = 0; field < 4; field++) {
      while (ce < n && line[ce] != ' ') ce++;
      while (ce < n && line[ce] == ' ') ce++;
    }

    // Walk backward from the ratio marker, across 2 fields (GID,
    // UID), to SIZE.
    int cs = marker;
    for (int field = 0; field < 2; field++) {
      while (cs > 0 && line[cs] == ' ') cs--;
      while (cs > 0 && line[cs] != ' ') cs--;
    }
    cs++;

    long size = 0;
    while (cs < n && line[cs] != ' ') {
      size = size * 10 + (line[cs] - '0');
      cs++;
    }

    ArchiveEntry entry;
    entry.name = (ce < n) ? line.substr(ce) : std::string();
    entry.size = size;
    entry.isDirectory = !entry.name.empty() && entry.name.back() == '/';
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Ported from alloc_lha(): each entry is preceded by a 3-line header
// ("::::::::" / filename / "::::::::") in the raw byte stream,
// except directories, which have none.
void LhaReader::locateContent(const unsigned char *data, std::size_t size,
                               std::vector<ArchiveEntry> &entries) const {
  const unsigned char *end = data + size;
  const unsigned char *cp = data;

  auto skipLine = [&]() {
    while (cp < end && *cp != '\n') cp++;
    if (cp < end) cp++;
  };

  for (auto &entry : entries) {
    if (!entry.isDirectory) { skipLine(); skipLine(); skipLine(); }
    entry.contentOffset = cp - data;
    if (entry.isDirectory) continue;
    long remaining = entry.size;
    while (remaining && cp < end) { cp++; remaining--; }
    if (cp == end) entry.contentTruncated = true;
  }
}

// Ported from alloc.c's alloc_head() T_rpm case, with one correction:
// legacy skipped only 3 space-delimited fields (mode, links, owner)
// before checking for a numeric SIZE field, landing on GROUP instead
// -- which is never numeric, so legacy's size was always 0 for every
// RPM entry. Confirmed against a real `rpm -qplv` line captured while
// porting this ("-rw-r--r--    1 root     root    6 Jul 28 01:01
// /usr/share/testpkg/file1.txt"): mode/links/owner/group are 4
// fields, matching the same layout TarReader's BSD-format branch
// already skips correctly. Skipping 4 fields here instead of 3 fixes
// it.
std::vector<ArchiveEntry> RpmReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  for (const auto &rawLine : splitLines(listing)) {
    std::string line = rawLine;
    auto arrow = line.find(" -> ");
    if (arrow != std::string::npos) line.resize(arrow);

    int n = static_cast<int>(line.size());
    int ce = n - 1;
    while (ce > 0 && line[ce] != ' ') ce--;
    int cs = ce + 1;

    ce = 0;
    for (int field = 0; field < 4; field++) {
      while (ce < n && line[ce] != ' ') ce++;
      while (ce < n && line[ce] == ' ') ce++;
    }

    long size = 0;
    bool special = false;
    if (ce < n && std::isdigit(static_cast<unsigned char>(line[ce]))) {
      while (ce < n && line[ce] != ' ') {
        if (line[ce] == ',') special = true;
        else if (!special) size = size * 10 + (line[ce] - '0');
        ce++;
      }
    }

    ArchiveEntry entry;
    entry.name = (cs <= n) ? line.substr(cs) : std::string();
    entry.size = special ? 0 : size;
    entry.isSpecialDevice = special;
    entry.isDirectory = !line.empty() && line[0] == 'd';
    entries.push_back(std::move(entry));
  }
  return entries;
}

}  // namespace fm
