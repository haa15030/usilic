#pragma once

#include <string>
#include <vector>

#include "charset.hpp"
#include "content_view.hpp"
#include "history.hpp"

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

 private:
  std::vector<ContentView::Line> readAndWrap(const std::string &path) const;

  ContentView &view_;
  History &history_;
  Charset::Encoding encoding_;
  int width_;
  std::string currentFile_;
};

}  // namespace fm
