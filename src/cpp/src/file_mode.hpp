#pragma once

#include <memory>
#include <string>
#include <vector>

#include "charset.hpp"
#include "content_view.hpp"
#include "history.hpp"
#include "pipe_buffer.hpp"

namespace fm {

// File-viewing mode: displays a regular file's content. Ported from
// mode.c's T_file case reaching into init.c's init_bin() (the
// read_buff()/check_type()/set_pp() chain) -- but only the "plain
// text file" path. Legacy's T_file handling is itself a small wrapper
// around a much bigger shared machine also used by M_error/M_log/
// M_tcb/M_du (check_type()'s content-sniffing, deciding whether to
// hand off to an external `file`(1)/roff/archive handler, and a
// binary-content hex-dump display mode) -- none of that content-type
// dispatch is ported here; see the class comment on scope.
//
// This is NOT a literal port of init_bin()/check_type()/set_pp().
// Those together re-derive charset detection/conversion, tab
// expansion, and nroff-overstrike bold/underline detection from
// scratch inline. All of that already has a clean, tested home in the
// already-ported PipeBuffer class -- FileMode is genuinely just: read
// the file's raw bytes, feed them to PipeBuffer, hand PipeBuffer's
// wrapped/converted lines to ContentView. No charset/overstrike logic
// is reimplemented here.
//
// Deliberately excluded (future work, not this slice):
//   - Content-type sniffing / dispatch (check_type()'s decision to
//     hand a file off to an external program, roff rendering, or
//     archive auto-detection instead of displaying it as plain text):
//     Mode/Application's job, built on top of Configuration's
//     programForSuffix()/archiveHandler-style lookups (already
//     ported) once that dispatch layer exists.
//   - Binary-content hex-dump display: legacy's C_bin case. PipeBuffer
//     already documents this exclusion for the same reason (depends
//     on Buffer-side preprocessing that doesn't exist yet); a binary
//     file's raw bytes are simply displayed as-is here, same as
//     PipeBuffer's own passthrough behavior for undetectable content.
//   - M_error/M_log/M_tcb/M_du's alternate data sources (a log file,
//     the About banner, `du` output): those aren't "view a regular
//     file" at all, just code paths that happened to share
//     init_bin()'s C implementation.
//   - Scrolling/paging: not this class's job at all -- callers drive
//     the same ContentView reference directly (moveUp()/moveDown()/
//     pageUp()/pageDown(), etc, already ported), matching how
//     DirectoryMode also doesn't wrap ContentView's own navigation.
//   - Returning to whatever mode was active before (DirectoryMode, or
//     a future caller): FileMode doesn't touch ModeStack itself, same
//     as DirectoryMode doesn't know how to return to ITS caller
//     either -- coordinating "T_dir opened this, so ^G should go back
//     to T_dir" across two mode classes is Mode/Application's job,
//     not either mode class's own.
class FileMode {
 public:
  FileMode(ContentView &view, History &history, Charset::Encoding encoding, int width);

  // Reads path's full contents, converts/wraps them via PipeBuffer,
  // and displays them, restoring a remembered cursor position from
  // History if one exists for this exact path. False if the file
  // can't be read (missing, permission denied, ...).
  bool loadFile(const std::string &path);

  const std::string &currentFile() const;

  // K-27 (^Oa, M_ascii, "Switch ascii mode"): changes the encoding
  // used by the NEXT loadFile()/reflow() call -- doesn't retroactively
  // re-decode whatever's already displayed on its own. A caller
  // switching charset mid-session should follow this with
  // reflow(width) to actually re-wrap the CURRENTLY open file under
  // the new encoding (Application's job -- see its own cycleCharset()).
  void setEncoding(Charset::Encoding encoding);

  // Records the current cursor position into History under the
  // current file's path. Ported from legacy's save on leaving T_file
  // (mode.c/menu.c's save_ll()) -- exposed as an explicit method
  // rather than an automatic side effect of scrolling, since only the
  // caller knows when "leaving" actually happens (matching
  // DirectoryMode's identical saveCurrentCursorPosition() pattern,
  // called from openSelected()/goBack() there).
  void saveCursorPosition();

  // Re-reads the current file and re-wraps it at newWidth, for a
  // terminal resize. Ported from alloc.c's alloc_winch() -- but NOT a
  // literal port: alloc_winch() reflows from the ALREADY-LOADED PP
  // list's own raw bytes, without touching the filesystem again; this
  // simply re-invokes the same read-and-wrap path loadFile() already
  // uses, at the new width, since there's no separate raw-bytes cache
  // to reflow from otherwise (a real, flagged difference -- content
  // that changed on disk between the initial load and this resize
  // will be picked up too, unlike legacy's own strictly-in-memory
  // reflow). Unlike loadFile(), does NOT consult History for a
  // remembered position -- the whole point of a resize-triggered
  // reflow is to preserve wherever the user's cursor already was in
  // THIS session, not jump back to a stale saved position -- so the
  // current cursor index is simply clamped to the new line count
  // instead (legacy's own set_rv()-after-alloc_winch() amounts to the
  // same row-index-based clamp in practice, not a semantic/identity-
  // based anchor). False (a no-op) if no file is currently loaded.
  bool reflow(int newWidth);

  // Overrides BOTH the mode-line's name field (normally the bare
  // filename) and its trailing2 field (normally the file's containing
  // directory, see the constructor's own comment) -- added for
  // Application::openSelectedLocateResult()'s File branch: a file
  // reached via LOCATE isn't necessarily anywhere near the directory
  // you were just browsing, so the plain containing-directory display
  // is less useful there than the search that actually found it (a
  // real user report: "旧版と違うので注意" -- legacy's own "戻り値"
  // field for this case). `label` becomes the name (e.g. "LOCATE",
  // matching that search's own results-screen name -- a direct user
  // correction, screenshot =aw: "「LOCATE: .txt」はモード名ではなく
  // 戻り値、モード名は「LOCATE」で" clarified that the search's own
  // label belongs in the NAME slot here, not prefixed onto detail).
  // `detail` becomes trailing2 verbatim (Finder-style middle-ellipsis
  // truncated to fit -- see ContentView::withTruncatedFields()'s
  // own comment -- matching a direct user request: "戻り値も検索一覧
  // 同様「/Applications/Adobe Acrob...source/.../HKSCS.txt」形式で"
  // -- the SAME truncation convention LOCATE's own results-list rows
  // already use, since a file's full absolute path is exactly the kind
  // of "long shared prefix, differing tail" text that convention
  // exists for). Cleared automatically at the top of every loadFile()
  // call (see its own comment).
  void setSearchOrigin(const std::string &label, const std::string &detail);

  // 2026-08-28 addition, a direct user report ("これでは^Gで（ls）に
  // 戻ってしまう。検索結果一覧へ戻るのが筋なので戻り値は「EGREP: ^-」
  // であるべき"): mirrors DirectoryMode::setSearchAnnotation() exactly
  // (same name, same "landed HERE via Enter on a GREP match, not a
  // direct search of this content" meaning) -- unlike setSearchOrigin()
  // above (which replaces BOTH name and trailing2, for LOCATE/INFO's
  // "this isn't really about the file's own name" framing), this
  // touches trailing2 ONLY: the file's own name (e.g. "CLAUDE.md")
  // stays exactly as-is, only the return-destination field changes, the
  // same split DirectoryMode's own name/searchAnnotation_ already keeps.
  void setSearchAnnotation(const std::string &text);
  bool hasSearchAnnotation() const { return !searchAnnotation_.empty(); }

  // 2026-08-25 (a direct user request comparing against both legacy
  // and less(1)): a CLI pager launch (main.cpp's own "通常ファイル
  // 引数"/stdin-pipe case, see Application::startPagerStream()'s own
  // comment) must NOT read its whole input upfront the way loadFile()
  // above does -- confirmed directly against the real legacy binary
  // (PTY capture, both a plain file argument AND a piped `seq 1
  // 100000`): the position counter only ever reaches ~23-25 total
  // lines (this port's own default viewport height) until the user
  // actually scrolls further, at which point more gets read. Critical
  // for a piped source specifically (an infinite/very long stream
  // must never be fully buffered before the first screen can even
  // draw), and matched for a real FILE argument too for behavioral
  // parity with what was actually observed.
  //
  // `fd`: an already-open, readable file descriptor -- EITHER a
  // regular file (Application::startPagerStream()'s own ::open() call)
  // or fd 0 itself for a piped stdin (KeyInput reads keyboard input
  // from a separately-opened "/dev/tty", never fd 0 -- see its own
  // class comment -- so fd 0 is entirely free to be consumed as pager
  // content here without any conflict). `ownsFd`: whether THIS call
  // should ::close() it once EOF is reached or a new load/loadFile()
  // replaces it (false for stdin -- closing fd 0 mid-process is not
  // this class's business). `displayName`: the mode-line's own name
  // field verbatim (the full path for a file argument, empty for
  // stdin -- see the constructor's own ModeLineFieldsProvider comment
  // for why this differs from loadFile()'s bare-filename convention);
  // ALSO the RecentFile history key, skipped entirely when empty (no
  // stable path to remember for a pipe). `initialLineBudget`: how many
  // lines to read up front (Application passes the current viewport
  // height plus a small margin).
  bool loadStream(int fd, bool ownsFd, const std::string &displayName, int initialLineBudget);
  // Reads more from the still-open stream if `targetLineIndex` is at
  // or past the currently-loaded end and the stream hasn't hit EOF --
  // called by Application right before delegating a scroll/movement
  // command to the view, ONLY while hasActiveStream() is true (every
  // OTHER File-mode entry point -- loadFile() above -- has nothing to
  // stream, so this is always a no-op there). False (a pure no-op)
  // once EOF is reached or no stream is active at all.
  bool ensureLoadedThrough(int targetLineIndex);
  bool hasActiveStream() const { return streamFd_ >= 0; }
  bool streamAtEof() const { return streamAtEof_; }
  // 2026-08-25 bug fix: `hasActiveStream()` above turns false once the
  // stream reaches real EOF (streamFd_ resets to -1, see
  // readMoreIntoPipe()'s own comment) -- correct for gating "is there
  // more to read", but WRONG for "was this file/stdin opened via a CLI
  // pager launch at all", which needs to keep answering true for as
  // long as the SAME loadStream() call's content is still on screen
  // (a real PTY bug: pressing ^G on an already-fully-read piped source
  // -- e.g. a short `ls -l /tmp | usilic` -- fell straight through to
  // returnToDirectory() and showed an empty Directory listing, since
  // hasActiveStream() had already gone false by the time ^G was
  // pressed). Use THIS for anything that needs to know "is this
  // pager-launched content" independent of read progress (Command::Back's
  // own File-mode branch); pagerMode_ itself is only ever reset by the
  // NEXT loadFile()/loadStream() call, never by reaching EOF.
  bool isPagerMode() const { return pagerMode_; }

  // 2026-08-25 (a direct user request, corrected same-day after a
  // re-check against the real legacy binary -- see
  // Application::pollShellHandoff()'s own comment on why a plain FILE
  // handoff no longer sets this at all): a standalone CLI pager launch
  // (real terminal, no live parent usilic) has no previous mode at all,
  // so ^G/'q' stay exactly as documented (^G inert, 'q' exits the whole
  // process, the "usilic v1.5b" rightWatermark fallback shown, all
  // confirmed against the real legacy binary). A SHELL-mode re-exec
  // handoff of PIPED STDIN specifically (the one remaining pagerMode_
  // case that still sets this true) DOES have a previous mode -- the
  // live SHELL session it was typed into -- so ^G/'q' return there
  // instead of doing nothing/quitting, and the mode-line shows a
  // highlighted (reverse-video, NOT the bold rightWatermark) "SHELL" in
  // trailing3 instead (a direct user correction: "戻り値がハイライト
  // 表示の「SHELL」であるべき" -- rightWatermark's bold styling was the
  // wrong slot for this). False by default, reset by every
  // loadFile()/loadStream() call -- pollShellHandoff()'s own piped-
  // stdin branch is the ONLY caller that ever sets it true.
  void setPagerReturnsToShell(bool value) { pagerReturnsToShell_ = value; }
  bool pagerReturnsToShell() const { return pagerReturnsToShell_; }

 private:
  std::vector<ContentView::Line> readAndWrap(const std::string &path) const;
  // Shared by loadStream()/ensureLoadedThrough(): reads from streamFd_
  // in bounded chunks, feeding each into streamPipe_, until either
  // streamPipe_->lines() reaches targetLineCount or EOF is hit (which
  // also flush()es streamPipe_, finalizing any still-pending trailing
  // partial line, and closes streamFd_ if streamOwnsFd_).
  void readMoreIntoPipe(int targetLineCount);
  std::vector<ContentView::Line> convertPipeLines() const;
  // Resets any prior stream state (closing an owned fd) -- called at
  // the top of loadStream() (a second CLI-launched file replacing the
  // first, e.g. Command::PagerNextFile) and of loadFile() (defensive:
  // an ordinary eager load should never leave a stale stream dangling).
  void closeStream();

  ContentView &view_;
  History &history_;
  Charset::Encoding encoding_;
  int width_;
  std::string currentFile_;
  std::string searchOriginLabel_;
  std::string searchOriginDetail_;
  // See setSearchAnnotation()'s own comment. Cleared on every fresh
  // loadFile() (a new file/reload invalidates whatever search landed us
  // on the PREVIOUS one), matching searchOriginLabel_/Detail_'s own
  // clearing just above in that same function.
  std::string searchAnnotation_;
  // pagerMode_ (2026-08-25): set by loadStream(), cleared by
  // loadFile() -- see the constructor's own ModeLineFieldsProvider
  // comment for why a pager-launched file's mode-line shows the full
  // path (or nothing, for stdin) with no size/directory trailing
  // fields, unlike an ordinary loadFile()-opened file.
  bool pagerMode_ = false;
  bool pagerReturnsToShell_ = false;
  int streamFd_ = -1;
  bool streamOwnsFd_ = false;
  bool streamAtEof_ = false;
  // 2026-08-25 (a direct user correction): the pager mode-line's own
  // trailing byte-count field used to sum displayed-line text up
  // through the CURSOR's line, mirroring Shell's own identical field --
  // but Shell's version only ever LOOKS that way because its cursor
  // always sits at the newest line as output streams in; this port's
  // own lazy-loaded pager can sit anywhere (a real report: paging back
  // UP after loading further down must NOT shrink the count -- "ページ
  // 戻しが起きたとしても...バイト数は変わらない"). The officially
  // settled spec ("実際に読み込まれたバイト数としたい") is simpler and
  // has nothing to do with the cursor at all: the raw byte total
  // readMoreIntoPipe() has actually pulled from streamFd_ so far,
  // monotonically increasing, tracked there directly (see its own
  // comment) rather than reconstructed from displayed line lengths.
  std::size_t streamBytesRead_ = 0;
  std::unique_ptr<PipeBuffer> streamPipe_;
};

}  // namespace fm
