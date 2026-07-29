#pragma once

#include <set>
#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"
#include "mode_stack.hpp"

namespace fm {

// One entry from a directory listing, as parsed from one line of
// `ls -alF` output.
struct DirectoryEntry {
  std::string name;    // filename only (ls -F's trailing "/"/"*"/"@" kept, matching legacy)
  std::string lsLine;   // the full formatted ls line, for display
  bool isDirectory = false;
};

// Directory-browsing mode: the "home base" screen every other
// excursion (file viewing, archive browsing, GREP, CUT, ...) returns
// to. This is a deliberately narrow first slice of what legacy calls
// "Mode" (mode.c/menu.c's mode()/menu()/get_m(), by far the largest
// and most tangled part of the whole port -- ~3300 lines across the
// two files, a giant switch over ~170 mode constants). Only T_dir
// (directory browsing) and the ^G "go back" action are ported here;
// every other mode (file/archive/GREP/CUT/MAN/PROGRAM/SHELL-glue/
// CP-MV-RM/...) is future work, layered on top of the same
// already-ported building blocks this class already demonstrates
// composing correctly: ContentView (rendering), ModeStack
// (navigation history), History (cursor-position memory),
// Configuration (the `ls` command line), ChildProcess (running it).
//
// This is NOT a literal port of mode()'s T_dir case or menu.c's
// m_bl()/m_dir(). Those functions are exactly the kind of legacy
// design this project has repeatedly chosen not to carry forward
// literally: m_bl() alone is a single ~120-line function handling
// CUT/T_grep/T_man/T_pipe4/T_pipe5/T_dir/T_du/M_shell branches all at
// once via shared mutable buffers (ap->name/lp->dir/lp->old), because
// mode() and m_bl() together were the ONE place every mode's "return"
// behavior had to be threaded through. Now that ModeStack/History/
// ContentView each own their own slice of what used to be crammed
// into those shared buffers, going back is just: save the cursor
// position, pop a frame, re-list that directory, restore its cursor
// position -- no shared-buffer juggling needed.
//
// Deliberately excluded (future work, not this slice):
//   - Opening a regular file (openSelected() no-ops on one -- file
//     viewing isn't ported yet).
//   - Everything else in mode()'s switch: GREP, CUT, MAN, PROGRAM,
//     archive browsing, SHELL entry, DU, LOCATE, history/list
//     browsers, help/error/log screens, exit confirmation. Selection
//     marking (toggleMark()/targetEntries(), below) IS in scope, since
//     CP/MV/RM (a separate CpMvRmMode class) needs somewhere to get
//     its target file list from -- but actually running cp/mv/rm on
//     the marked entries is CpMvRmMode's job, not this class's.
//   - ModeStack's "floor" concept (pushFloor()/isFloor()): with only
//     one mode implemented, there's nothing yet for a floor frame to
//     usefully distinguish from "the stack is simply empty" -- an
//     empty ModeStack already means "nothing to go back to" for this
//     slice's purposes. Revisit once a second mode exists.
//   - The 12-slot reusable buffer pool (legacy's HEAP.base[N_base]):
//     not needed with only one ContentView in play; a real gap to
//     solve once file-viewing or any second concurrent buffer is
//     ported (flagged during this class's design, not silently
//     glossed over).
class DirectoryMode {
 public:
  DirectoryMode(ContentView &view, ModeStack &modeStack, History &history, const Configuration &configuration);

  // Lists `path` (via ChildProcess + Configuration::commandFor(Ls),
  // forcing LC_ALL=C -- see parseLsLine()'s comment on why) and
  // displays it in the owned ContentView, restoring a remembered
  // cursor position from History if one exists for this exact path.
  // False if the `ls` invocation failed (nonexistent/unreadable
  // directory, no `ls` command configured).
  bool enterDirectory(const std::string &path);

  const std::string &currentDirectory() const;
  const std::vector<DirectoryEntry> &entries() const;

  // The entry currently under the cursor, or nullptr if the listing
  // is empty.
  const DirectoryEntry *selectedEntry() const;

  // Enter/M_cr on the selected entry: if it's a directory, saves the
  // current cursor position, pushes a ModeStack frame remembering
  // where to come back to, and descends into it. Resolves ".."/"."
  // the same way legacy's T_dir case does (by directory string
  // manipulation, not by trusting the filesystem to have a real ".."
  // entry -- matters for e.g. the root directory). Returns false
  // (no-op) if the selection is a regular file or the listing is
  // empty.
  bool openSelected();

  // ^G: pops the most recent ModeStack frame (if any) and returns to
  // it, restoring its cursor position from History. Returns false if
  // the stack is empty (nothing to go back to).
  bool goBack();

  bool canGoBack() const;

  // -- selection marking (for CP/MV/RM, ^Om-style) --
  // Ported from mode.c's fmark()/nmark() -- legacy marked entries in
  // place on the PP list itself (pp->split == -f_mark); marks here
  // are indices into entries_ instead, scoped to the CURRENT listing
  // (cleared whenever enterDirectory() reloads it, matching the
  // ordinary file-manager workflow of "look at a listing, mark
  // several entries, act on them" within one view rather than marks
  // somehow surviving a fresh `ls`).
  void toggleMark(int index);
  bool isMarked(int index) const;
  std::size_t markedCount() const;
  void clearMarks();

  // Marked entries in listing order, or -- if nothing is explicitly
  // marked -- just the single currently-selected entry (matching the
  // common file-manager convention that an action with no explicit
  // multi-selection acts on "whatever's under the cursor"). Empty if
  // there are no marks and no valid selection.
  std::vector<DirectoryEntry> targetEntries() const;

 private:
  DirectoryEntry parseLsLine(const std::string &line) const;
  void saveCurrentCursorPosition();
  static std::string parentDirectory(const std::string &dir);

  ContentView &view_;
  ModeStack &modeStack_;
  History &history_;
  const Configuration &configuration_;

  std::string currentDirectory_;
  std::vector<DirectoryEntry> entries_;
  std::set<int> markedIndices_;
};

}  // namespace fm
