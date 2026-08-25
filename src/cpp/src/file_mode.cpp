#include "file_mode.hpp"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "pipe_buffer.hpp"

namespace fm {

FileMode::FileMode(ContentView &view, History &history, Charset::Encoding encoding, int width)
    : view_(view), history_(history), encoding_(encoding), width_(width) {
  // Ported from put_str()'s own f_mark logic (see
  // ContentView::setCursorHighlightEnabled()'s comment): plain file
  // content never gets a whole-line reverse-video cursor highlight in
  // the real CUI, unlike Directory mode's listing (which this port
  // had already been highlighting, correctly, well before this flag
  // existed) -- confirmed by a real CUI/GUI side-by-side screenshot.
  view_.setCursorHighlightEnabled(false);

  // Mode-line fields, ported from disp_window()'s own field layout for
  // the pager case (name0 not starting with '/' takes the "copy as-
  // is" branch, see its comment): the bare filename on the left (NOT
  // the full path -- that's what the CUI actually shows), and the
  // file's byte size plus its containing directory on the right.
  // Registered once here rather than re-set on every loadFile() call
  // since it's a live callback into currentFile_, not a snapshot.
  view_.setModeLineFieldsProvider([this](int cursorIndex) -> ContentView::ModeLineFields {
    // pagerMode_ (2026-08-25, loadStream()'s own comment): a CLI pager
    // launch shows a genuinely different, simpler mode-line -- the
    // full path verbatim (or nothing at all, for stdin) as `name`, no
    // directory trailing2 field at all, and the "usilic v1.5b"
    // rightWatermark fallback (legacy's own disp_window() shows this
    // whenever there's no "previous mode" text to put in that slot --
    // exactly the pager-launch case, which has no previous mode; every
    // OTHER File-mode entry below always has one, which is why
    // rightWatermark stays empty there). name/no-trailing2/watermark
    // confirmed directly against the real legacy binary (PTY capture
    // of a pager-launched file). The position field and this trailing
    // byte-count, however, are a direct 2026-08-25 follow-up request
    // ("SHELL同様、行数を5桁とバイト数表示をよろしく") reusing Shell's
    // own two additions verbatim (setUseFixedWidthPosition(true), set
    // by loadStream() below -- see its own comment -- and a running
    // byte-count-through-the-cursor-line total, same +1-per-line
    // convention for the lost newline as Shell's own identical field)
    // rather than replicating legacy's own pager mode-line exactly (no
    // byte count at all there, confirmed by the same PTY capture) --
    // an intentional, requested deviation, not an oversight.
    if (pagerMode_) {
      std::size_t bytes = 0;
      const auto &contentLines = view_.lines();
      for (int i = 0; i <= cursorIndex && i < static_cast<int>(contentLines.size()); i++)
        bytes += contentLines[i].text.size() + 1;
      return {currentFile_, std::to_string(bytes), /*fillTrailingToEdge=*/false, /*rightWatermark=*/"usilic v1.5b"};
    }
    if (currentFile_.empty()) return {};
    std::filesystem::path path(currentFile_);
    std::string directory = path.parent_path().string();
    if (!directory.empty() && directory.back() != '/') directory += '/';

    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    // The byte size and the directory are TWO SEPARATE ModeLineFields::
    // trailing/trailing2 segments, not one string -- added 2026-08-10.
    // An EARLIER version of this fix tried to solve a real request
    // ("「1108009」と「/Users/.../」の間...「CLAUDE.md」と
    // 「00036/13736  0%」みたいにして欲しい") by just widening the
    // space INSIDE one single highlighted string, which (confirmed by
    // comparing the actual pixel colors of the two reference
    // screenshots) still left it as one unbroken reverse-video run --
    // the requested look has a genuine PLAIN (non-highlighted) gap
    // between "CLAUDE.md" and "00034/13761  0%", which only two
    // independently-standout'd segments (see trailing2's own comment)
    // can reproduce; a single string with extra spaces in it can't,
    // since drawSegment() highlights everything it's given uniformly.
    // The size itself stays a plain, ungrouped std::to_string() (a
    // separate, earlier misreading already established and reverted
    // that "ファイルサイズは「1108009」のままでいい").
    std::string trailing = ec ? directory : std::to_string(size);
    // searchOriginLabel_/searchOriginDetail_ (see setSearchOrigin()'s
    // own comment) take over the name/trailing2 fields whenever set --
    // same substitution DirectoryMode's own provider makes for
    // searchAnnotation_ vs. its "usilic v1.5b" rightWatermark, just
    // spanning two fields here instead of one.
    std::string name = searchOriginLabel_.empty() ? path.filename().string() : searchOriginLabel_;
    std::string trailing2 = ec ? "" : (searchOriginLabel_.empty() ? directory : searchOriginDetail_);
    // File mode's trailing field is legacy's padded-to-edge style, see
    // ModeLineFields::fillTrailingToEdge's own comment. No
    // rightWatermark here -- see its own comment on why File mode
    // never shows it. trailing2Kind=PathMiddle only when reached via
    // LOCATE (searchOriginLabel_ set) -- see ContentView::TrailingKind's
    // own comment for why this is the one exception to every other
    // provider's tail-only truncation; the not-via-LOCATE case
    // (trailing2 = the containing directory PATH, not a typed search
    // string) uses Path instead, for the full available width rather
    // than QueryEcho's fixed 16-char tail cap.
    return {name,
            trailing,
            /*fillTrailingToEdge=*/true,
            /*rightWatermark=*/"",
            trailing2,
            /*trailingPlain=*/false,
            /*trailing2RightJustified=*/false,
            /*trailing2Kind=*/searchOriginLabel_.empty() ? ContentView::TrailingKind::Path
                                                          : ContentView::TrailingKind::PathMiddle};
  });
}

std::vector<ContentView::Line> FileMode::readAndWrap(const std::string &path) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!in.good() && !in.eof()) return {};
  std::string content = buffer.str();

  PipeBuffer pipe(encoding_, width_, 64L * 1024 * 1024);
  pipe.feed(content);
  pipe.flush();

  std::vector<ContentView::Line> lines;
  lines.reserve(pipe.lines().size());
  // styled.attributes (2026-08-05): PipeBuffer's own overstrike
  // detection (nroff-style bold/underline) already produces this --
  // previously discarded here, the one and only place FileMode turns
  // PipeBuffer's StyledLines into what actually reaches the screen.
  for (const auto &styled : pipe.lines()) lines.push_back({styled.text, std::nullopt, std::nullopt, styled.attributes});
  return lines;
}

bool FileMode::loadFile(const std::string &path) {
  std::ifstream probe(path, std::ios::binary);
  if (!probe) return false;
  probe.close();

  // Reset to the plain-pager default on EVERY load (2026-08-18): the
  // constructor's own setCursorHighlightEnabled(false) only covers the
  // state before the FIRST load. Application::executeShowTcbBanner()
  // turns this back ON right after its own loadFile() call (the ^T
  // screen's WORK/arc.N lines need a visible highlight to be
  // selectable at all -- a real report, =cq, found them impossible to
  // pick without one), so this reset is what keeps that flag from
  // leaking into the NEXT ordinary file loaded afterward (including a
  // w.N entry re-opened FROM the banner via openSelectedBannerEntry()
  // itself, which goes through this exact same loadFile()).
  view_.setCursorHighlightEnabled(false);
  closeStream();  // defensive: an ordinary eager load must never leave a stale stream dangling
  pagerMode_ = false;
  view_.setUseFixedWidthPosition(false);  // see loadStream()'s own comment; every other File-mode entry stays plain

  view_.setLines(readAndWrap(path));
  currentFile_ = path;
  // a fresh load invalidates the old search -- see setSearchOrigin()'s own comment
  searchOriginLabel_.clear();
  searchOriginDetail_.clear();
  // K-46 (^X^F, "FILE: Select file name")'s own data source -- see
  // DirectoryMode::enterDirectory()'s identical pushRecent() call for
  // K-48's own RecentDirectory counterpart.
  history_.pushRecent(RingCategory::RecentFile, currentFile_);
  if (auto pos = history_.positionFor(PositionCategory::File, currentFile_)) {
    int idx = pos->row;
    int last = static_cast<int>(view_.lines().size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  // A real cursor at the current line, since plain file content has
  // no whole-line highlight to indicate it otherwise (see
  // ContentView::syncVisibleCursor()'s own comment) -- a 2026-08-04
  // user request: the current line still needs to be visibly and
  // programmatically knowable in both -c/-g (launching an editor at
  // the current line, and a future COPY mode's row+column selection,
  // both rely on a real cursor position).
  view_.syncVisibleCursor();
  return true;
}

bool FileMode::reflow(int newWidth) {
  if (currentFile_.empty()) return false;
  width_ = newWidth;
  int oldCursor = view_.cursorIndex();

  view_.setLines(readAndWrap(currentFile_));
  int last = static_cast<int>(view_.lines().size()) - 1;
  if (oldCursor < 0) oldCursor = 0;
  if (oldCursor > last) oldCursor = (last < 0) ? 0 : last;
  view_.setCursorIndex(oldCursor);
  view_.redraw();
  view_.syncVisibleCursor();
  return true;
}

const std::string &FileMode::currentFile() const { return currentFile_; }

void FileMode::setSearchOrigin(const std::string &label, const std::string &detail) {
  searchOriginLabel_ = label;
  searchOriginDetail_ = detail;
}

void FileMode::setEncoding(Charset::Encoding encoding) { encoding_ = encoding; }

void FileMode::saveCursorPosition() {
  if (currentFile_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(view_.lines().size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::File, currentFile_, pos);
}

void FileMode::closeStream() {
  if (streamFd_ >= 0 && streamOwnsFd_) ::close(streamFd_);
  streamFd_ = -1;
  streamOwnsFd_ = false;
  streamAtEof_ = false;
  streamPipe_.reset();
}

void FileMode::readMoreIntoPipe(int targetLineCount) {
  if (streamAtEof_ || streamFd_ < 0) return;
  // A deliberately SMALL read buffer (2026-08-25, tuned against a real
  // PTY capture): read() returns at most this many bytes per call, and
  // the loop below only checks streamPipe_->lines().size() BETWEEN
  // calls, not mid-chunk -- a large buffer on short-line content (e.g.
  // `seq`'s own output) could overshoot the requested targetLineCount
  // by far more than intended in a single gulp (confirmed: an 8192-byte
  // buffer pulled in ~1859 lines on the first call alone, against an
  // intended ~48-line initial page). 256 bytes keeps that overshoot
  // small regardless of line length, at the cost of more read() calls
  // for the same total bytes -- a negligible cost next to the actual
  // point of this method (never blocking on more than roughly one
  // screen's worth of input at a time, matching legacy's own observed
  // ~23-25-line initial load).
  char buf[256];
  while (static_cast<int>(streamPipe_->lines().size()) < targetLineCount) {
    ssize_t n = ::read(streamFd_, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR) continue;
      break;  // a real read error -- treat like EOF rather than looping forever
    }
    if (n == 0) {
      streamAtEof_ = true;
      streamPipe_->flush();  // finalizes any still-pending trailing partial line
      if (streamOwnsFd_) ::close(streamFd_);
      streamFd_ = -1;
      break;
    }
    streamPipe_->feed(std::string(buf, static_cast<std::size_t>(n)));
  }
}

std::vector<ContentView::Line> FileMode::convertPipeLines() const {
  std::vector<ContentView::Line> lines;
  lines.reserve(streamPipe_->lines().size());
  for (const auto &styled : streamPipe_->lines()) lines.push_back({styled.text, std::nullopt, std::nullopt, styled.attributes});
  return lines;
}

bool FileMode::loadStream(int fd, bool ownsFd, const std::string &displayName, int initialLineBudget) {
  closeStream();
  view_.setCursorHighlightEnabled(false);
  pagerMode_ = true;
  // 2026-08-25 (a direct user request: "SHELL同様、行数を5桁とバイト
  // 数表示をよろしく") -- reuses Shell's own identical setup
  // (Application::startShell()'s own setUseFixedWidthPosition(true)
  // call) for the SAME fixed 5-digit zero-padded "NNNNN/NNNNN END"
  // position format, instead of the variable-width default every
  // other File-mode entry uses.
  view_.setUseFixedWidthPosition(true);
  streamFd_ = fd;
  streamOwnsFd_ = ownsFd;
  streamAtEof_ = false;
  streamPipe_ = std::make_unique<PipeBuffer>(encoding_, width_, 256L * 1024 * 1024);
  currentFile_ = displayName;
  searchOriginLabel_.clear();
  searchOriginDetail_.clear();
  if (!displayName.empty()) history_.pushRecent(RingCategory::RecentFile, displayName);
  readMoreIntoPipe(std::max(initialLineBudget, 1));
  view_.setLines(convertPipeLines());
  return true;
}

bool FileMode::ensureLoadedThrough(int targetLineIndex) {
  if (streamAtEof_ || streamFd_ < 0) return false;
  int before = static_cast<int>(streamPipe_->lines().size());
  if (targetLineIndex < before) return false;  // already have enough loaded
  readMoreIntoPipe(targetLineIndex + 1);
  if (static_cast<int>(streamPipe_->lines().size()) == before) return false;  // nothing new arrived
  view_.setLines(convertPipeLines());
  return true;
}

}  // namespace fm
