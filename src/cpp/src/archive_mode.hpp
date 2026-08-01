#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "archive_reader.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"

namespace fm {

// Archive-browsing mode: lists an archive's contents (.tar/.tgz/
// .tar.bz2/.zip/.lha/.rpm/.deb) and extracts a selected entry's raw
// bytes. Ported from mode.c's T_tar/T_lha/T_unzip/T_rpm/T_deb cases
// reaching into init.c's init_arc() (which itself calls fork_arc() to
// run the listing command, then alloc.c's alloc_head()/alloc_tar()/
// alloc_lha()/alloc_unzip() to parse it) -- but this is genuinely just
// glue over already-ported classes, not a fresh port: Configuration
// already resolves which command lists/decompresses a given suffix
// (programForSuffix(), including the ARCHIVE-directive pipe-splitting
// for .tgz/.tar.bz2), ChildProcess already runs it (run()/
// runPipeline()), and the ArchiveReader family already parses the
// listing text and locates entry content in raw bytes. ArchiveMode's
// only real job is wiring those three together and feeding the result
// to ContentView -- the same shape as FileMode wiring PipeBuffer to
// ContentView.
//
// Deliberately excluded (future work, not this slice):
//   - RPM/.deb content extraction: RpmReader::locateContent() is
//     intentionally a no-op (see archive_reader.hpp) -- an RPM
//     entry's content isn't a byte range in the .rpm file the way
//     tar/zip/lha entries are, it needs rpm2cpio re-invoked against
//     the whole archive. A .deb's listing is the OUTER `ar` container
//     (control.tar.gz/data.tar.gz/debian-binary) rather than the
//     inner payload, so reaching a real file inside it needs a
//     multi-stage pipeline (`ar p | gzip -dc | tar xf -`) this slice
//     doesn't build. extractSelected() returns nullopt for both.
//   - Nested archives (opening an archive found inside another
//     archive's extracted content) and handing extracted content off
//     to FileMode/further viewing: Mode/Application's job to
//     coordinate, same boundary DirectoryMode/FileMode already
//     documented for their own hand-offs.
//   - Actually decompressing to a real temporary file for huge
//     archives that don't fit comfortably in memory: this slice reads
//     the whole archive file into memory for locateContent(), matching
//     ArchiveReader's own existing API shape (data+size, not a stream).
class ArchiveMode {
 public:
  ArchiveMode(ContentView &view, History &history, const Configuration &configuration);

  // Resolves path's listing (and, for piped formats like .tgz, its
  // decompression stage) via Configuration::programForSuffix(), runs
  // it, and parses the result with the matching ArchiveReader
  // subclass. Restores a remembered cursor position from History if
  // one exists for this exact path (stored under
  // PositionCategory::Screen -- the catch-all category legacy itself
  // used for archive-listing cursor memory, see History's own class
  // comment). False if path's suffix isn't a configured archive
  // format, or the listing command failed.
  bool loadArchive(const std::string &path);

  const std::string &currentArchive() const;
  // The concrete format of currentArchive() (None before any
  // loadArchive() call) -- exposed for K-57/K-58 (^X^Ka/^Xa, "ARC:
  // Expand archive"), which needs to know the format to build the
  // right extraction command (Application's job, see its own
  // descendToNestedArchive()/K-57 comments -- ArchiveMode itself only
  // exposes the format, it doesn't run any extraction-to-destination
  // command of its own).
  ArchiveKind archiveKind() const;
  const std::vector<ArchiveEntry> &entries() const;
  const ArchiveEntry *selectedEntry() const;

  // Extracts the selected entry's content: for .tar, by parsing real
  // tar headers directly in the raw archive file's bytes; for .zip/
  // .lha, by running the format's extract-to-stdout command
  // (`unzip -p`/`lha p`) and locating the entry within its
  // decompressed output instead (see the .cpp for why these two
  // formats need different data sources). nullopt if there's no
  // selection, the selection is a directory entry, the format doesn't
  // support this at all (RPM/.deb, see the class comment), or reading/
  // running the underlying command failed.
  std::optional<std::vector<unsigned char>> extractSelected();

  void saveCursorPosition();

  // -- selection marking (K-57/K-58, ^X^Ka/^Xa's own "mark specific
  // entries to extract" mode -- HELP.jp 2.11's own example marks two
  // TOC entries with ^M before giving a Destination) --
  // Mirrors DirectoryMode::toggleMark()/isMarked()/clearMarks()/
  // markAll()/targetEntries() exactly (this port's own established
  // ^Space-based simplification of legacy's real per-mode marking
  // mechanism -- see DirectoryMode's own class comment on why marks
  // here are indices into entries_, scoped to the CURRENT listing,
  // not legacy's pp->split in-place marking). Cleared whenever
  // loadArchive() loads a new listing (including a nested-archive
  // descent -- marks from the parent archive's TOC don't carry over).
  void toggleMark(int index);
  bool isMarked(int index) const;
  std::size_t markedCount() const;
  void clearMarks();
  void markAll();

  // Marked entries in listing order, or -- if nothing is explicitly
  // marked -- just the single currently-selected entry (same
  // "acts on whatever's under the cursor" convention
  // DirectoryMode::targetEntries() already established). Empty if
  // there are no marks and no valid selection.
  std::vector<ArchiveEntry> targetEntries() const;

 private:
  static std::string formatEntryLine(const ArchiveEntry &entry);

  ContentView &view_;
  History &history_;
  const Configuration &configuration_;

  std::string currentArchive_;
  ArchiveKind archiveKind_ = ArchiveKind::None;
  std::vector<ArchiveEntry> entries_;
  std::set<int> markedIndices_;
};

}  // namespace fm
