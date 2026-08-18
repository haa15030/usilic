#include "archive_mode.hpp"

#include <algorithm>
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
    : view_(view), history_(history), configuration_(configuration) {
  // Mode-line fields (2026-08-19 REDESIGN, a direct user correction:
  // "旧ARCのモードラインはお手本じゃなく、正しくは「ARC」「01/14
  // TOP」「1774」「(tar)」「/Users/haa15030/git/usilic/src/utf8.tar」
  // でよろしく"): `name` is the fixed literal "ARC" (matching
  // Directory/File's own "DIR"/"FILE"-style mode-name convention, NOT
  // the archive's own basename as an earlier version here showed) --
  // trailing is still the listing's own total byte count (2026-08-15
  // addition, unchanged).
  //
  // trailing2 (2026-08-19 FOLLOW-UP fix, same day: "戻り値が非ハイ
  // ライト" -- a real report + screenshot found the archive's own path,
  // originally placed in rightWatermark below, drawn in plain BOLD text
  // instead of the reverse-video every OTHER highlighted field here
  // uses): rightWatermark is legacy's OWN "no previous-mode text to
  // show, so print the program name/version instead" fallback (see its
  // own struct comment) -- deliberately non-reverse-video THERE because
  // it's static branding, not real destination info. The archive's own
  // path is real, meaningful "戻り値" content (same category as every
  // OTHER mode's own highlighted trailing2), so it belongs in a
  // highlighted field instead.
  //
  // trailing2/trailing3 split (2026-08-19, SAME-DAY second follow-up:
  // "「(tar)」と「/Users/.../utf8.tar」の間に空白が無い"): the FIRST
  // attempt at this fix combined the "(<command>)" badge and the path
  // into ONE trailing2 string ("(tar) " + path). That string DID
  // contain a real ASCII space, but renderModeLine() draws a single
  // field as one uninterrupted Standout run (see its own comment on
  // why -- CUI's mode-line needs BLACK GAPS BETWEEN fields, not within
  // one), so the embedded space rendered as a same-color reverse-video
  // blank -- invisible against its own dark background, unlike the
  // genuine box breaks between every OTHER field pair here. Splitting
  // the badge (trailing2, matching DirectoryMode's own identical
  // "(ls)" convention) and the path (trailing3, see its own comment)
  // into two SEPARATE fields gives them a real plain-mode gap via
  // renderModeLine()'s own existing per-field rendering, no new
  // rendering logic needed.
  //
  // fillTrailingToEdge (2026-08-19, twice same day): initially set to
  // false here, since at THAT point trailing3 didn't support edge-fill
  // yet and leaving this true would have padded trailing2 ("(tar)")
  // all the way to the row's right edge, pushing trailing3 (the path)
  // off-screen. A real follow-up report (=dj: "戻り値が右端まで埋まっ
  // てない") wanted the path itself filled to the edge, same as every
  // other mode's own last trailing field -- trailing3 now honors
  // fillTrailingToEdge too (see its own renderModeLine() comment), so
  // this is back to true.
  //
  // archiveCommand_ (the actually-configured program, e.g.
  // "(tar)"/"(unzip)"/"(lha)"/"(rpm)", set at loadArchive() time, see
  // its own comment). For a NESTED archive, currentArchive_ is
  // Application::descendToNestedArchive()'s own mkdtemp()-generated
  // temp path rather than anything meaningful to a human, same known
  // tradeoff the OLD basename-only `name` field used to accept for the
  // exact same reason; not solved here since the user's own report was
  // specifically about a top-level (non-nested) archive.
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (currentArchive_.empty()) return {};
    std::string badge = archiveCommand_.empty() ? "" : "(" + archiveCommand_ + ")";
    ContentView::ModeLineFields fields{"ARC", std::to_string(view_.totalByteCount()),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
                                        /*trailing2=*/badge};
    fields.trailing3 = currentArchive_;
    return fields;
  });
}

// 2026-08-19 REDESIGN, a direct user correction: "パーミッションも=dd
// の一文字（d、-）も不要だが、「utf8/」は縦一列に揃えてほしい". The
// leading isDirectory 'd'/'-' marker is dropped entirely, and `size` is
// now right-justified to `sizeWidth` (the widest size actually present
// in THIS listing, computed once by the caller across every entry --
// see loadArchive()'s own comment) instead of the OLD "size + a single
// literal '\t'" shape -- ContentView::renderLine() only ever converts a
// raw '\t' byte to ONE plain space (see its own comment), not real
// tab-stop math, so names never actually lined up in a column despite
// looking like they might. A fixed two-space gap after the padded size
// keeps every entry's name starting at the exact same column,
// regardless of how many digits its own size happens to have.
std::string ArchiveMode::formatEntryLine(const ArchiveEntry &entry, std::size_t sizeWidth) {
  std::string sizeText = std::to_string(entry.size);
  std::string line(sizeWidth > sizeText.size() ? sizeWidth - sizeText.size() : 0, ' ');
  line += sizeText;
  line += "  ";
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
  archiveCommand_ = command->argv.empty() ? "" : command->argv.front();
  entries_ = std::move(entries);
  markedIndices_.clear();  // marks are scoped to the listing they were made in, matching DirectoryMode's own convention

  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  std::size_t sizeWidth = 0;
  for (const auto &entry : entries_) sizeWidth = std::max(sizeWidth, std::to_string(entry.size).size());
  // ellipsisEligible=true: see Line::ellipsisEligible's own comment.
  for (const auto &entry : entries_)
    lines.push_back({formatEntryLine(entry, sizeWidth), std::nullopt, std::nullopt, {}, true});
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
ArchiveKind ArchiveMode::archiveKind() const { return archiveKind_; }
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

void ArchiveMode::toggleMark(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  auto it = markedIndices_.find(index);
  if (it != markedIndices_.end())
    markedIndices_.erase(it);
  else
    markedIndices_.insert(index);
}

bool ArchiveMode::isMarked(int index) const { return markedIndices_.count(index) > 0; }

std::size_t ArchiveMode::markedCount() const { return markedIndices_.size(); }

void ArchiveMode::clearMarks() { markedIndices_.clear(); }

void ArchiveMode::markAll() {
  markedIndices_.clear();
  for (int i = 0; i < static_cast<int>(entries_.size()); i++) markedIndices_.insert(i);
}

std::vector<ArchiveEntry> ArchiveMode::targetEntries() const {
  std::vector<ArchiveEntry> result;
  if (!markedIndices_.empty()) {
    for (int idx : markedIndices_) result.push_back(entries_[idx]);  // std::set iterates in ascending order
    return result;
  }
  if (const ArchiveEntry *entry = selectedEntry()) result.push_back(*entry);
  return result;
}

}  // namespace fm
