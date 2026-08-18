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

  // mode links owner group SIZE mon day time/year name -- the BSD tar
  // (macOS) fallback shape, factored out since both the ce==0 case
  // below AND the GNU-branch's own false-positive recovery (see its
  // comment) need to run this exact same 4-field skip.
  auto skipBsdFields = [n, &line]() {
    int c = 0, k = 0;
    while (k < 4 && c < n) {
      if (line[c] == ' ') {
        while (c + 1 < n && line[c + 1] == ' ') c++;
        k++;
      }
      c++;
    }
    return c;
  };

  for (int i = 0;; i++) {
    for (; ce > 0; ce--)
      if (line[ce] == ' ' || (i && line[ce] == '/')) break;
    if (i == 0) cs = ce + 1;  // filename starts right after the last space
    if (i && line[ce] == '/') {
      // GNU format: walk forward from "owner/group" to the size field.
      // 2026-08-19 fix, a real report + reproduction (a tar of a
      // directory whose OWN name reappears as a path prefix on every
      // entry, e.g. "utf8/foo.txt") -- the backward scan above has no
      // way to tell a genuine GNU "owner/group" separator apart from a
      // '/' that's simply part of the FILENAME itself (any entry
      // nested under a subdirectory has one). A false-positive match
      // sends this loop walking forward through arbitrary filename
      // bytes looking for a digit-then-space/comma pattern that may
      // not exist anywhere ahead of it -- previously an unconditional
      // `while (true)` with no bound once `ce`/`p` both reached `n`
      // (neither scan below advances further, and `p < n` stays false
      // forever), spinning at 100% CPU instead of erroring out. Bounded
      // now: on running off the end without ever finding a valid size
      // field, this was never real GNU-format "owner/group" data to
      // begin with -- fall back to the same reliable BSD field-skip
      // the ce==0 branch below already uses (this system's own `tar
      // tvf` output IS BSD-format; false positives only ever originate
      // from a filename's own '/', never a genuine format mismatch).
      bool foundSize = false;
      while (ce < n) {
        while (ce < n && line[ce] != ' ') ce++;
        while (ce < n && line[ce] == ' ') ce++;
        if (ce >= n) break;
        int p = ce;
        while (p < n && std::isdigit(static_cast<unsigned char>(line[p]))) p++;
        if (p < n && (line[p] == ' ' || line[p] == ',')) {
          foundSize = true;
          break;
        }
        ce = p;
      }
      if (!foundSize) ce = skipBsdFields();
      break;
    } else if (ce == 0) {
      ce = skipBsdFields();
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

  // Re-derive the filename start FORWARD from here (2026-08-19 fix,
  // found via the same reproduction as the GNU-branch hang above --
  // real filenames can and do contain literal spaces, e.g.
  // "utf8/報道ステーション 速報 森保監督..."). The `cs` computed at
  // i==0 above scans BACKWARD from the end of the line for the LAST
  // space, which is wrong whenever the filename itself contains one --
  // it lands mid-filename instead of at its true start, truncating the
  // displayed name (confirmed: this exact entry rendered as "森保監督
  // か会見…" instead of "utf8/報道ステーション　速報 森保監督が
  // 会見…", missing everything up through the embedded space). Exactly
  // 3 space-delimited fields (month, day, time-or-year) always sit
  // between the size field and the filename in both BSD and GNU tar's
  // `tar tvf` output, so counting FORWARD from the size field just
  // parsed -- a position already known to be correct -- past those 3
  // fields reaches the true filename start regardless of what the
  // filename itself contains. Only overrides `cs` when the line
  // actually has 3 more fields to skip; an unexpectedly short/malformed
  // line keeps the i==0 backward-scan guess instead of trusting a
  // partial forward count.
  {
    int fs = ce;
    for (int k = 0; k < 3 && fs < n; k++) {
      while (fs < n && line[fs] == ' ') fs++;
      while (fs < n && line[fs] != ' ') fs++;
    }
    while (fs < n && line[fs] == ' ') fs++;
    if (fs < n) cs = fs;
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

// Octal ASCII field (tar's own numeric encoding for size/mode/etc, up
// to `len` digit bytes, space/NUL-terminated).
long parseTarOctalField(const unsigned char *p, int len) {
  long v = 0;
  for (int i = 0; i < len; i++) {
    unsigned char c = p[i];
    if (c < '0' || c > '7') break;
    v = v * 8 + (c - '0');
  }
  return v;
}

// Extracts the "path" value out of a PAX extended-header data block --
// a sequence of "<byteLen> <key>=<value>\n" records (byteLen includes
// itself, the space, key, "=", value, and the trailing "\n"); every
// OTHER key this doesn't care about is simply skipped over via its own
// declared length. See findTarHeader()'s own comment for why this
// exists at all.
std::string extractPaxPath(const unsigned char *data, std::size_t dataSize) {
  std::size_t i = 0;
  while (i < dataSize) {
    std::size_t j = i;
    long recLen = 0;
    while (j < dataSize && data[j] >= '0' && data[j] <= '9') {
      recLen = recLen * 10 + (data[j] - '0');
      j++;
    }
    if (j >= dataSize || data[j] != ' ' || recLen <= 0) break;
    std::size_t recEnd = i + static_cast<std::size_t>(recLen);
    if (recEnd > dataSize) break;
    std::size_t eq = j + 1;
    while (eq < recEnd && data[eq] != '=') eq++;
    if (eq < recEnd) {
      std::string key(reinterpret_cast<const char *>(data + (j + 1)), eq - (j + 1));
      if (key == "path") {
        std::size_t valStart = eq + 1;
        std::size_t valEnd = recEnd;
        if (valEnd > valStart && data[valEnd - 1] == '\n') valEnd--;
        return std::string(reinterpret_cast<const char *>(data + valStart), valEnd - valStart);
      }
    }
    i = recEnd;
  }
  return "";
}

// Ported from find_tar_header(): tar headers are always 512-byte
// aligned from the start of the archive, so only alignment-matching
// positions are checked -- otherwise a hidden entry whose header text
// merely *contains* `name` as a substring (e.g. macOS's "._name"
// AppleDouble sidecar, which "tar tvf" doesn't list but is a real
// header in the byte stream) can be mistaken for it. See CLAUDE.md
// history item 4 for the original bug this fixed.
//
// PAX extended headers (2026-08-19 fix, a direct user report: opening
// any file with a long/Unicode name inside a tar produced completely
// blank content, "何も無し") -- classic ustar's own `name` field is
// only 100 bytes, nowhere near enough for a Japanese filename with a
// "utf8/" path prefix (this port's own real reproduction archive has
// entries whose name alone is 100-160+ UTF-8 bytes). macOS's bsdtar
// (this system's own `tar`) stores anything past that limit in a
// SEPARATE PAX extended-header record (typeflag 'x') immediately
// BEFORE the real header, holding the full name as a "path=<value>"
// line in its own data block -- the real header that follows still has
// a name field, but it's a truncated/placeholder value, never the
// genuine full name this function is asked to match against. The
// pre-existing raw byte-for-byte comparison below only ever checked
// classic short names directly in a header's own name field, so it
// silently walked every entry with a long name all the way to `end`
// without ever finding it, leaving contentOffset pointed at garbage
// (empty/blank content, no crash). Checked FIRST at every candidate
// position: if this 512-aligned block is itself a PAX extended header
// whose OWN "path=" value matches `name`, the REAL header is exactly
// the next 512-aligned position after its own data (computed from the
// PAX record's own declared size, offset 124 -- same octal field every
// tar header uses for size) -- return THAT position directly, since
// that's what locateContent()'s own `cp += 512` (to reach content)
// then correctly lands on.
const unsigned char *findTarHeader(const unsigned char *cp, const unsigned char *base,
                                    const unsigned char *end, const std::string &name) {
  const unsigned char *p = base + (((cp - base) + 511) & ~511);
  for (; p < end; p += 512) {
    if (p + 157 <= end && (p[156] == 'x' || p[156] == 'X')) {
      long paxSize = parseTarOctalField(p + 124, 12);
      const unsigned char *paxData = p + 512;
      if (paxSize > 0 && paxData + paxSize <= end) {
        if (extractPaxPath(paxData, static_cast<std::size_t>(paxSize)) == name) {
          std::size_t dataBlocks = (static_cast<std::size_t>(paxSize) + 511) / 512;
          return paxData + dataBlocks * 512;
        }
      }
    }
    if (std::string(reinterpret_cast<const char *>(p), name.size()) == name) return p;
  }
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
