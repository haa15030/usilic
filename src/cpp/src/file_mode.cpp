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
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // pagerMode_ (2026-08-25, loadStream()'s own comment): a CLI pager
    // launch shows a genuinely different, simpler mode-line -- the
    // full path verbatim (or nothing at all, for stdin) as `name`, no
    // directory trailing2 field at all, and a rightWatermark fallback
    // (legacy's own disp_window() shows this whenever there's no
    // "previous mode" text to put in that slot; every OTHER File-mode
    // entry below always has one, which is why rightWatermark stays
    // empty there). name/no-trailing2/watermark confirmed directly
    // against the real legacy binary (PTY capture of a pager-launched
    // file). The position field and this trailing byte-count, however,
    // are a direct 2026-08-25 follow-up request ("SHELL同様、行数を5桁
    // とバイト数表示をよろしく") reusing Shell's own setUseFixedWidthPosition(true)
    // setup (set by loadStream() below -- see its own comment) rather
    // than replicating legacy's own pager mode-line exactly (no byte
    // count at all there, confirmed by the same PTY capture) -- an
    // intentional, requested deviation, not an oversight.
    //
    // 2026-08-25 correction (a direct user follow-up, having re-checked
    // the real legacy binary): pagerMode_/this whole branch is reached
    // ONLY by a genuine CLI pager launch (main.cpp's own positional-
    // file-argument/piped-stdin startup) or a SHELL-handoff PIPE
    // (Application::pollShellHandoff()'s own socket branch) -- a
    // SHELL-handoff of a plain FILE argument no longer reaches here at
    // all (pollShellHandoff() now calls the ordinary fileMode_.loadFile()
    // instead of loadStream(), so it falls through to the plain
    // Directory-reached-file branch below, unchanged, matching "SHELL
    // プロンプトでusilic起動したときの引数の画面になればいいだけ...
    // つまりはls画面で「.bashrc」を選択した場合と同じ").
    if (pagerMode_) {
      // 2026-08-25 correction (a direct user follow-up): this field
      // used to sum displayed-line text through the CURSOR's line,
      // mirroring Shell's own identical field -- but Shell's version
      // only looks that way because its cursor always sits at the
      // newest line as output streams in. This port's own pager cursor
      // can sit anywhere (e.g. after paging down then back up), and the
      // settled spec has nothing to do with the cursor at all: "実際に
      // 読み込まれたバイト数としたい...ページ戻しが起きたとしても
      // すでに読み込んだ分量としてのバイト数は変わらない" -- the raw
      // total streamBytesRead_ has actually pulled from the stream so
      // far (see its own declaration comment), monotonically
      // increasing regardless of scroll position.
      std::size_t bytes = streamBytesRead_;
      // pagerReturnsToShell_ (2026-08-25, its own declaration comment):
      // now ONLY ever true for a SHELL-handoff PIPE (see above) -- a
      // standalone CLI pager launch (file or stdin, from a real
      // terminal) has no previous mode at all, so rightWatermark stays
      // "usilic v1.5b" (legacy's own no-previous-mode fallback) exactly
      // as before. A SHELL-handoff pipe DOES have one -- the live SHELL
      // session -- but a direct user correction rejected reusing
      // rightWatermark's BOLD styling for that ("戻り値がハイライト
      // 表示の「SHELL」となる...でよろしく" -- HIGHLIGHTED/reverse-video,
      // not bold): trailing3 (see its own declaration comment; already
      // exactly "a third independently-highlighted trailing segment",
      // just previously only ever used by ArchiveMode) is the existing
      // mechanism that actually renders that way, so this uses THAT
      // instead, leaving rightWatermark empty here -- matching every
      // OTHER File-mode entry's "has a previous mode, so no watermark"
      // convention (see this whole comment's own opening paragraph).
      //
      // searchOriginDetail_ (2026-08-25, Application::pollShellHandoff()'s
      // own comment): empty for an ordinary CLI pager launch (unaffected,
      // no trailing2 shown there, matching the real legacy PTY capture);
      // set to "(stdin)" specifically for a SHELL-mode piped handoff, in
      // the same trailing2 slot Directory's "(ls)"/Archive's "(tar)"
      // badges occupy -- a direct user request, UNCHANGED by the above
      // correction (only the return-value display itself moved).
      ContentView::ModeLineFields fields;
      fields.name = currentFile_;
      fields.trailing = std::to_string(bytes);
      fields.rightWatermark = pagerReturnsToShell_ ? "" : "usilic v1.5b";
      fields.trailing2 = searchOriginDetail_;
      fields.trailing3 = pagerReturnsToShell_ ? "SHELL" : "";
      // 2026-08-25 fix (a direct user report citing HISTORY.md:9462,
      // "基本的に右端まで埋まっていないモードラインは全部不良"): trailing3
      // ALREADY has fillTrailingToEdge support (see its own declaration
      // comment, added earlier for ArchiveMode) -- just never turned on
      // here, so "SHELL" stopped short of the row's right edge instead
      // of being padded like every other mode's own last field. Scoped
      // to pagerReturnsToShell_ specifically (not unconditional): the
      // standalone-launch case has no trailing3 at all and relies on
      // rightWatermark's own separate moveCursor()-to-the-edge placement
      // instead, which already reaches the edge on its own -- turning
      // this on there too would newly reverse-video-fill the PLAIN gap
      // between the byte count and "usilic v1.5b" that was never part of
      // this report and never confirmed as wanted.
      fields.fillTrailingToEdge = pagerReturnsToShell_;
      return fields;
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
    // searchAnnotation_ (2026-08-28 addition, see setSearchAnnotation()'s
    // own comment): a THIRD tier, below searchOriginLabel_/Detail_ (which
    // still win outright when both are somehow set -- LOCATE/INFO's own
    // "this isn't really about the file's own name" framing takes
    // priority) -- unlike those two, this leaves `name` alone, only
    // substituting for the plain "containing directory" trailing2
    // fallback, mirroring DirectoryMode::searchAnnotation_'s identical
    // name/trailing2 split.
    std::string trailing2 =
        ec ? "" : (searchOriginLabel_.empty() ? (searchAnnotation_.empty() ? directory : searchAnnotation_) : searchOriginDetail_);
    // File mode's trailing field is legacy's padded-to-edge style, see
    // ModeLineFields::fillTrailingToEdge's own comment. No
    // rightWatermark here -- see its own comment on why File mode
    // never shows it. trailing2Kind=PathMiddle for both LOCATE
    // (searchOriginLabel_ set) and searchAnnotation_ -- see
    // ContentView::TrailingKind's own comment for why this is the one
    // exception to every other provider's tail-only truncation; the
    // fallback (plain containing directory PATH) uses Path instead, for
    // the full available width.
    // searchAnnotation_ (2026-08-28 fix, a direct user report + example:
    // "「- レガシーC実装...src/config.sh」...使えるだけ使う": QueryEcho's
    // own fixed 16-char tail cap truncated the matched line's text far
    // more tightly than the available width actually required -- a
    // short name like "CLAUDE.md" leaves plenty of room this field
    // should actually use. PathMiddle is budget-aware (middleEllipsisTruncate()
    // sizes to whatever's ACTUALLY left, via withTruncatedFields()'s own
    // pipeline) and already keeps both ends of the text, matching the
    // exact "- レガシーC実装...src/config.sh" shape requested -- and
    // splitLayoutWhenCrowded below (already unconditionally on for this
    // provider) is what kicks in automatically on the OTHER side, if
    // name+trailing2 together ever genuinely don't both fit.
    ContentView::ModeLineFields fields{name,
                                        trailing,
                                        /*fillTrailingToEdge=*/true,
                                        /*rightWatermark=*/"",
                                        trailing2,
                                        /*trailingPlain=*/false,
                                        /*trailing2RightJustified=*/false,
                                        /*trailing2Kind=*/(searchOriginLabel_.empty() && searchAnnotation_.empty())
                                            ? ContentView::TrailingKind::Path
                                            : ContentView::TrailingKind::PathMiddle};
    // splitLayoutWhenCrowded (2026-08-27 addition, a direct user
    // report/callback to their own earlier HISTORY.md:9608 proposal,
    // which archiveContentView_'s own provider already adopted -- see
    // its own comment in application.cpp): a real CJK filename long
    // enough to crowd out the byte-count field used to just silently
    // drop it entirely via fitModeLineFields()'s own ordinary all-or-
    // nothing tiers, same class of bug DirectoryMode's own `(ls)`
    // listing just got this identical fix for.
    fields.splitLayoutWhenCrowded = true;
    return fields;
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
  pagerReturnsToShell_ = false;  // defensive, see its own declaration comment; unused while pagerMode_ is false anyway
  view_.setUseFixedWidthPosition(false);  // see loadStream()'s own comment; every other File-mode entry stays plain

  view_.setLines(readAndWrap(path));
  currentFile_ = path;
  // a fresh load invalidates the old search -- see setSearchOrigin()'s own comment
  searchOriginLabel_.clear();
  searchOriginDetail_.clear();
  searchAnnotation_.clear();  // see setSearchAnnotation()'s own comment
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

void FileMode::setSearchAnnotation(const std::string &text) { searchAnnotation_ = text; }

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
    streamBytesRead_ += static_cast<std::size_t>(n);  // see its own declaration comment: the settled "actual bytes read" spec
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
  pagerReturnsToShell_ = false;  // see its own declaration comment; pollShellHandoff() flips this after this call
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
  streamBytesRead_ = 0;  // see its own declaration comment -- a fresh stream starts this back at 0
  streamPipe_ = std::make_unique<PipeBuffer>(encoding_, width_, 256L * 1024 * 1024);
  currentFile_ = displayName;
  searchOriginLabel_.clear();
  searchOriginDetail_.clear();
  if (!displayName.empty()) history_.pushRecent(RingCategory::RecentFile, displayName);
  readMoreIntoPipe(std::max(initialLineBudget, 1));
  view_.setLines(convertPipeLines());
  // 2026-08-25 bug fix (a real report): ContentView::setLines() only
  // CLAMPS the previous cursorIndex_ into the new content's bounds --
  // it does NOT reset it to 0 (correct for callers that WANT to
  // preserve a cursor position across a same-content refresh, see its
  // own comment) -- so a SECOND loadStream() call (e.g. a fresh
  // `cat x|usilic` typed again at the SAME live SHELL prompt) silently
  // inherited whatever cursorIndex_ the FIRST session's own navigation
  // left behind. A real repro: session 1 pressed ESC-> (go to bottom)
  // on a 119-line stream, landing cursorIndex_ at 118; session 2's own
  // stream only had 51 lines, so THAT inherited 118 got clamped down
  // to 50 -- the new content's own last line -- instead of starting
  // at 0, making a brand-new (stdin) session open already scrolled to
  // its own end ("冒頭行から始まらない"). loadFile() above already
  // gets this right via its own explicit setCursorIndex() call
  // (history-restored or 0) -- loadStream() never had the equivalent.
  // Every pager launch (standalone CLI, or a SHELL-handoff pipe) is
  // unconditionally a fresh start, matching the CUI's own convention
  // and the direct principle established for (stdin) specifically:
  // "常時真っ新から始まる" -- so this is always 0, never a restored
  // History position (pager sessions never save/restore cursor
  // position via History at all -- see loadFile()'s own currentFile_-
  // keyed lookup, which never applies here since currentFile_ is
  // either a throwaway CLI path or permanently empty for stdin).
  view_.setCursorIndex(0);
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
