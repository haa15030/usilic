#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fm {

// One entry parsed from an archive's text listing (a "tar tvf"/
// "unzip -l"/"rpm -qplv" style line), plus (for formats that support
// it) where its content begins in the archive's raw bytes.
struct ArchiveEntry {
  std::string name;
  bool isDirectory = false;
  bool isSpecialDevice = false;  // tar/rpm device-file entries: no real size
  long size = 0;

  // Set by locateContent(). -1 means "not yet located" (or the
  // format doesn't support it at all, see RpmReader).
  long contentOffset = -1;
  // True if the archive's raw bytes end before this entry's declared
  // size is fully covered (a truncated/incomplete read) -- ported
  // from alloc_tar()/alloc_lha()/alloc_unzip()'s `split = -2` sentinel.
  bool contentTruncated = false;
};

// Parses an archive format's text listing into entries, and (for
// formats that support it) locates each entry's content within the
// archive's raw bytes. Ported from alloc.c's alloc_head() (the
// per-format listing-line parser) and alloc_tar()/alloc_lha()/
// alloc_unzip() (the per-format raw-byte content locators).
//
// Deliberately excluded (belongs to a future ChildProcess class, not
// here): actually running `tar`/`unzip`/`lha`/`rpm` and capturing
// their output, and the archive-format-specific decompression
// pipelines (e.g. a .deb's data.tar.gz needing `ar p | gzip -dc`
// first) -- ported from fork.c's fork_arc(), which interleaves
// dense, multi-level fork()/pipe() plumbing with format selection.
// That process-management concern is entirely orthogonal to what
// THIS class does (pure parsing of text/bytes already obtained by
// some means), and inventing a ChildProcess-shaped API here without
// that class actually existing yet would be speculative. Callers
// today are expected to obtain `listing` and `data` themselves and
// hand them to this class.
//
// Data model change from the legacy C: alloc_head() formatted each
// entry into a single display string (permission-style prefix + a
// fixed-width size field + the filename at a hardcoded byte offset,
// 20), and later alloc_tar()/alloc_lha()/alloc_unzip() re-extracted
// the filename by slicing that SAME formatted string back apart
// (`&pp->cs[20]`) to know what to search for in the raw bytes -- a
// format-then-reparse round trip that only existed because PP had
// just one string field to hold everything. ArchiveEntry stores name
// and size as their own distinct fields from the start, so
// locateContent() uses entry.name directly; there's no display
// string to build or re-parse here at all (formatting entries for
// on-screen display is ContentView's job, given these entries).
class ArchiveReader {
 public:
  virtual ~ArchiveReader() = default;

  virtual std::vector<ArchiveEntry> parseListing(const std::string &listing) const = 0;

  // entries must already have .size set (from parseListing()) --
  // this only fills in .contentOffset/.contentTruncated.
  virtual void locateContent(const unsigned char *data, std::size_t size,
                              std::vector<ArchiveEntry> &entries) const = 0;
};

// .tar and .deb (a .deb's data.tar.gz payload, once decompressed, is
// itself a tar stream -- legacy's alloc_head()/alloc_tar() shared the
// same T_tar/T_deb branch for exactly this reason, see init.c's
// T_tar/T_deb case both calling alloc_tar()).
class TarReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

class ZipReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

// Ported from documented header-format comments in alloc.c (a real
// `lha` binary wasn't available to capture live sample output while
// porting this -- see cpp/tests/test_archive_reader.cpp for the
// synthetic fixture used instead, built to match the documented
// column layout).
class LhaReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

// Listing only. Legacy never implemented content-location for RPM
// (init.c's T_rpm case calls none of alloc_tar/alloc_lha/alloc_unzip)
// -- an RPM entry can't be viewed without re-invoking rpm2cpio on the
// whole archive, which is squarely the not-yet-built ChildProcess's
// job, not something to fake a byte-offset for here.
class RpmReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *, std::size_t,
                      std::vector<ArchiveEntry> &) const override {}
};

// 2026-08-26 addition (a direct user request, testing a real .rar
// fixture): parses `lsar -l <archive>` output (see its own
// declaration comment on why lsar/unar, not the proprietary `unrar`).
// No legacy precedent (RAR postdates this project's original C era).
// locateContent() uses the SAME sequential-walk approach as
// ZipReader/LhaReader (see ZipReader::locateContent()'s own comment)
// -- `unar -f -o - -q <archive>` extracts every entry to stdout,
// concatenated in the SAME order lsar -l lists them (verified
// directly: the extracted stream's own total byte count exactly
// matches the sum of every listed file's own size, and the very
// first bytes match the first listed file's own known content).
class RarReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

// 2026-08-26 addition (a direct user request, testing a real .7z
// fixture): parses `7z l <archive>` output. No legacy precedent (7z
// postdates this project's original C era). locateContent() uses the
// SAME sequential-walk approach as ZipReader/LhaReader/RarReader --
// `7z x -so <archive>` extracts every entry to stdout, concatenated in
// the SAME order `7z l` lists them (verified the same way RarReader's
// own comment describes: total byte count and first-file content both
// match).
class SevenZipReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

}  // namespace fm
