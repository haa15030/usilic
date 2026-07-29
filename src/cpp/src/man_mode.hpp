#pragma once

#include <optional>
#include <string>
#include <vector>

#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"

namespace fm {

// MAN-page mode (T_man): runs `man <name>` and displays its output.
// Ported from init.c's init_man()/fork_man() -- which turns out to
// just run `man`(1) and pipe its output through the exact same
// nroff-overstrike-bold-detection/charset-conversion/line-wrapping
// machinery as viewing a plain text file (alloc_pipe(), already
// ported as PipeBuffer) -- man pages use the same backspace-overstrike
// convention as e.g. this repo's own doc/README.jp (see PipeBuffer's
// own class comment and CLAUDE.md history item 8). This makes ManMode
// structurally identical to FileMode, just sourcing its bytes from a
// command's captured output instead of a file read -- see the .cpp.
//
// Deliberately excluded (future work, not this slice): the
// `ENVVAR=value manpage` prefix fork_man() special-cased (setting one
// environment variable before running `man`, e.g. for MANSECTION) --
// narrow enough, and this project has no evidence it was ever
// exercised in practice, that guessing at a clean API for it now
// would be speculative; a plain page name is what's supported here.
class ManMode {
 public:
  ManMode(ContentView &view, History &history, const Configuration &configuration, Charset::Encoding encoding,
          int width);

  // Runs `man name` and displays the result, restoring a remembered
  // cursor position from History if one exists for this exact page
  // (stored under PositionCategory::ManPage, legacy's own `ll.p.man`
  // category). False if the command failed to run (no such man page,
  // `man` not configured/installed).
  bool loadPage(const std::string &name);

  const std::string &currentPage() const;
  void saveCursorPosition();

  // Re-runs `man currentPage()` and re-wraps its output at newWidth,
  // for a terminal resize. See FileMode::reflow()'s own comment for
  // the shared design (not a literal alloc_winch() port, current-
  // session cursor preserved via clamp rather than a History lookup)
  // -- the one difference here is that, since there's no cached raw
  // `man` output to reflow from, this genuinely re-invokes `man`
  // itself rather than re-reading a file, a real (flagged) cost this
  // doesn't try to hide. False (a no-op) if no page is currently
  // loaded, or if re-running `man` fails.
  bool reflow(int newWidth);

 private:
  std::optional<std::vector<ContentView::Line>> runAndWrap(const std::string &name) const;

  ContentView &view_;
  History &history_;
  const Configuration &configuration_;
  Charset::Encoding encoding_;
  int width_;
  std::string currentPage_;
};

}  // namespace fm
