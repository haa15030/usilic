#include "archive_mode.hpp"

#include <fstream>
#include <memory>

#include "child_process.hpp"

namespace fm {

namespace {

std::unique_ptr<ArchiveReader> readerFor(ArchiveKind kind) {
  switch (kind) {
    case ArchiveKind::Tar:
    case ArchiveKind::Deb:
      return std::make_unique<TarReader>();
    case ArchiveKind::Zip:
      return std::make_unique<ZipReader>();
    case ArchiveKind::Lha:
      return std::make_unique<LhaReader>();
    case ArchiveKind::Rpm:
      return std::make_unique<RpmReader>();
    case ArchiveKind::Bzip2:
    case ArchiveKind::None:
      return nullptr;
  }
  return nullptr;
}

}  // namespace

ArchiveMode::ArchiveMode(ContentView &view, History &history, const Configuration &configuration)
    : view_(view), history_(history), configuration_(configuration) {}

std::string ArchiveMode::formatEntryLine(const ArchiveEntry &entry) {
  std::string line = entry.isDirectory ? "d " : "- ";
  line += std::to_string(entry.size);
  line += "\t";
  line += entry.name;
  return line;
}

bool ArchiveMode::loadArchive(const std::string &path) {
  ArchiveKind kind = ArchiveKind::None;
  auto command = configuration_.programForSuffix(path, &kind);
  if (!command || kind == ArchiveKind::None) return false;

  auto reader = readerFor(kind);
  if (!reader) return false;

  ChildProcess::Result result;
  if (command->pipedFromArgv) {
    // Piped formats (e.g. .tgz: gzip -dc <file> | tar tvf -) read the
    // archive file as the FIRST stage's input, not the second's.
    std::vector<std::string> firstStage = *command->pipedFromArgv;
    firstStage.push_back(path);
    result = ChildProcess::runPipeline(firstStage, command->argv);
  } else {
    std::vector<std::string> argv = command->argv;
    argv.push_back(path);
    result = ChildProcess::run(argv);
  }
  if (!result.ok) return false;

  std::vector<ArchiveEntry> entries = reader->parseListing(result.output);

  currentArchive_ = path;
  archiveKind_ = kind;
  entries_ = std::move(entries);

  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  for (const auto &entry : entries_) lines.push_back({formatEntryLine(entry)});
  view_.setLines(std::move(lines));

  if (auto pos = history_.positionFor(PositionCategory::Screen, currentArchive_)) {
    int idx = pos->row;
    int last = static_cast<int>(entries_.size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  return true;
}

const std::string &ArchiveMode::currentArchive() const { return currentArchive_; }
const std::vector<ArchiveEntry> &ArchiveMode::entries() const { return entries_; }

const ArchiveEntry *ArchiveMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

std::optional<std::vector<unsigned char>> ArchiveMode::extractSelected() {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return std::nullopt;
  if (entries_[idx].isDirectory) return std::nullopt;

  auto reader = readerFor(archiveKind_);
  if (!reader) return std::nullopt;

  std::vector<unsigned char> data;
  switch (archiveKind_) {
    case ArchiveKind::Tar: {
      // tar entries sit at real, parseable header offsets within the
      // raw .tar file itself (TarReader::locateContent() walks real
      // tar headers) -- no decompression needed for a plain .tar
      // (a .tgz/.tar.bz2's ARCHIVE-directive pipedFromArgv already
      // decompressed it before TarReader ever saw the listing, but
      // extraction here only handles the plain-.tar case; see the
      // class comment).
      std::ifstream in(currentArchive_, std::ios::binary);
      if (!in) return std::nullopt;
      data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      break;
    }
    case ArchiveKind::Zip:
    case ArchiveKind::Lha: {
      // Unlike tar, zip/lha entries are individually compressed
      // within the raw archive, so -- matching legacy's own
      // alloc_unzip()/alloc_lha() design (see ZipReader/LhaReader's
      // own doc comments) -- locateContent() here is meant to walk
      // the DECOMPRESSED, concatenated output of the format's
      // extract-to-stdout command (`unzip -p`/`lha p`), not the raw
      // archive file bytes.
      ConfiguredRole role =
          (archiveKind_ == ArchiveKind::Zip) ? ConfiguredRole::UnzipExtract : ConfiguredRole::LhaExtract;
      ProgramCommand extractCommand = configuration_.commandFor(role);
      if (extractCommand.argv.empty()) return std::nullopt;
      std::vector<std::string> argv = extractCommand.argv;
      argv.push_back(currentArchive_);
      ChildProcess::Result result = ChildProcess::run(argv);
      if (!result.ok) return std::nullopt;
      data.assign(result.output.begin(), result.output.end());
      break;
    }
    default:
      // Rpm: no content-location support at all (see RpmReader).
      // Deb: its listing is the OUTER `ar` container (control.tar.gz/
      // data.tar.gz/debian-binary), not the inner payload -- reaching
      // an actual file inside data.tar.gz needs a multi-stage
      // extraction (`ar p file.deb data.tar.gz | gzip -dc | tar xf -`)
      // this slice doesn't build; left as future work rather than
      // guessed at.
      return std::nullopt;
  }

  reader->locateContent(data.data(), data.size(), entries_);

  const ArchiveEntry &entry = entries_[idx];
  if (entry.contentOffset < 0) return std::nullopt;

  std::size_t start = static_cast<std::size_t>(entry.contentOffset);
  if (start > data.size()) return std::nullopt;
  std::size_t len = static_cast<std::size_t>(entry.size);
  if (start + len > data.size()) len = data.size() - start;  // matches contentTruncated

  return std::vector<unsigned char>(data.begin() + static_cast<long>(start),
                                     data.begin() + static_cast<long>(start + len));
}

void ArchiveMode::saveCursorPosition() {
  if (currentArchive_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(entries_.size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::Screen, currentArchive_, pos);
}

}  // namespace fm
