#pragma once

#include <string>

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

  // Records the current cursor position into History under the
  // current file's path. Ported from legacy's save on leaving T_file
  // (mode.c/menu.c's save_ll()) -- exposed as an explicit method
  // rather than an automatic side effect of scrolling, since only the
  // caller knows when "leaving" actually happens (matching
  // DirectoryMode's identical saveCurrentCursorPosition() pattern,
  // called from openSelected()/goBack() there).
  void saveCursorPosition();

 private:
  ContentView &view_;
  History &history_;
  Charset::Encoding encoding_;
  int width_;
  std::string currentFile_;
};

}  // namespace fm
