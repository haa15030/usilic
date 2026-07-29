#pragma once

#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"

namespace fm {

struct DuEntry {
  std::string path;
  long sizeKb = 0;
  bool isDirectory = false;
};

// Disk-usage mode (T_du): runs `du` over a directory and displays
// each file/subdirectory's size. Ported from mode.c's T_du case --
// which itself is mostly path-string bookkeeping (deciding whether
// the result lands back in T_dir or T_file, chdir()ing, formatting
// the mode-line breadcrumb) around a `du` invocation that already had
// a real command line resolved elsewhere (menu.c's `M_du` prompt
// entry, not covered by this class -- see below). DuMode is, like
// FileMode/ArchiveMode, mostly wiring: Configuration already resolves
// the `du` command line (commandFor(ConfiguredRole::Du)), ChildProcess
// already runs it, so this class's only real job is parsing `du`'s
// tab-separated "<size>\t<path>" output into entries and displaying
// them.
//
// Deliberately excluded (future work, not this slice):
//   - The M_du prompt (typing/confirming which directory to measure)
//     and the decision of whether opening a DU entry lands in
//     DirectoryMode or FileMode: Mode/Application's job, the same
//     hand-off boundary every other *Mode class in this port already
//     draws.
//   - chdir()/cwd bookkeeping: legacy's T_du case chdir()s the whole
//     process as a side effect of measuring a directory; not
//     reproduced here (no hidden global state -- see e.g.
//     Configuration/Shell's own class comments for this project's
//     standing rule on this).
class DuMode {
 public:
  DuMode(ContentView &view, History &history, const Configuration &configuration);

  // Runs `du` on path (via Configuration::commandFor(Du) + real
  // filesystem type checks for isDirectory) and displays the results,
  // restoring a remembered cursor position from History if one exists
  // for this exact path. False if the command failed to run.
  bool loadUsage(const std::string &path);

  const std::string &currentPath() const;
  const std::vector<DuEntry> &entries() const;
  const DuEntry *selectedEntry() const;

  void saveCursorPosition();

 private:
  static std::string formatEntryLine(const DuEntry &entry);

  ContentView &view_;
  History &history_;
  const Configuration &configuration_;

  std::string currentPath_;
  std::vector<DuEntry> entries_;
};

}  // namespace fm
