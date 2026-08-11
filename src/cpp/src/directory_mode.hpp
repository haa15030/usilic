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
  // Byte offset into lsLine where `name` starts -- ported from
  // scroll.c's get_col()/init_col() (T_dir case), which the cursor-
  // line's own reverse-video highlight in legacy starts from instead
  // of column 0 (see stand_str()'s `&rvp->pp->cs[rvp->col]`). Computed
  // by parseLsLine() alongside `name` itself so ContentView::Line's
  // highlightStartColumn can be set without re-deriving it.
  int nameStartColumn = 0;
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
  // displays it in the owned ContentView. If consultHistory is true
  // (the default -- every existing caller: openSelected(), goBack(),
  // ^Or reload), restores a remembered cursor position from History if
  // one exists for this exact path; otherwise always starts at the top
  // regardless of any saved position. False if the `ls` invocation
  // failed (nonexistent/unreadable directory, no `ls` command
  // configured).
  //
  // consultHistory=false exists for exactly one caller: main()'s own
  // initial app.start() when launched with an EXPLICIT path argument
  // (including "."). History's own class comment documents it as
  // persisting cursor position "across runs" -- legacy's own
  // argument-less-launch resume feature -- but that's specifically an
  // argument-LESS-launch feature: typing an explicit path means "show
  // me this directory now," not "resume my last session," and
  // shouldn't silently pull in a position saved from some earlier,
  // unrelated visit to that same path (a real report: launching with
  // "." landed on a stale mid-list row instead of the top, confusing
  // since nothing about that particular launch was a history replay).
  bool enterDirectory(const std::string &path, bool consultHistory = true);

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

  // K-19 (^O^K, M_m): jumps straight to the parent of currentDirectory(),
  // pushing a ModeStack frame the same way openSelected() does for a
  // "../" entry -- this is that exact same navigation, just reachable
  // without first moving the cursor onto "../". Ported from m_dir()'s
  // own M_m case: at the root ("/"), legacy's own path-truncation logic
  // is a no-op (nothing before the one leading '/' to strip), which
  // this mirrors by refusing (false) instead of re-entering "/"
  // pointlessly.
  bool goToParent();

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
  // K-72 (^X^Y, "Mark all"): marks every entry in the current listing.
  // No exclusion for "./"/"../" -- toggleMark() itself has none
  // either, so this stays consistent with marking one entry at a
  // time. clearMarks() itself already covers K-73 ("Clear all").
  void markAll();

  // Marked entries in listing order, or -- if nothing is explicitly
  // marked -- just the single currently-selected entry (matching the
  // common file-manager convention that an action with no explicit
  // multi-selection acts on "whatever's under the cursor"). Empty if
  // there are no marks and no valid selection.
  std::vector<DirectoryEntry> targetEntries() const;

  // K-26 (^Ol, usilicrc's own LS section: "When the flag is set,
  // file-selector will display only filename"): switches each
  // displayed row between the full `ls -alF` line and just the bare
  // filename, WITHOUT re-running `ls` -- entries_ already carries
  // both DirectoryEntry::lsLine and ::name independently, so this is
  // purely a display-layer rebuild of view_'s lines from the already-
  // parsed entries (selection/marks/cursor position are all untouched,
  // since entries_ itself never changes). Persisted across runs the
  // same way ToggleScrollMode/K-25 is -- see Application's own
  // handling.
  void setFilenameOnlyMode(bool enabled);
  bool filenameOnlyMode() const;

  // Set by Application::openSelectedGrepMatch()/returnFromCut() when
  // the user comes back from an FGREP/EGREP/CUT search over this
  // listing -- shown as the mode line's trailing2 field ("FGREP:
  // pattern"/"CUT: pattern"), ported from mode.c's own T_grep case
  // (`sprintf(lp->prev, "%s: %s", ...)`, the "remembers which search
  // you just came from" annotation a real CUI screenshot confirmed;
  // legacy's own is_prev(prev, M_fgrep/M_egrep) checks in the T_dir
  // case are the same idea generalized to CUT here, since CUT can
  // land back on this SAME "prev" slot too). Cleared by
  // enterDirectory() (a fresh `ls` invalidates whatever search
  // produced the old listing this annotation referred to), NOT by
  // every cursor move -- matches legacy's own T_dir handling, which
  // only clears this on a genuine directory change. Deliberately
  // generic (not "grep-only" by name): Application tracks separately
  // which KIND of search set it (see its own lastDirectorySearchKind_)
  // so Back can route to the right results screen -- this class only
  // owns displaying the text, not what it means.
  void setSearchAnnotation(const std::string &text);
  bool hasSearchAnnotation() const { return !searchAnnotation_.empty(); }

 private:
  DirectoryEntry parseLsLine(const std::string &line) const;
  // Shared by openSelected()/goToParent(): saves the current cursor
  // position, pushes a ModeStack frame remembering where to come back
  // to, and enters `target` -- undoing the push on failure.
  bool descendTo(const std::string &target);
  void saveCurrentCursorPosition();
  static std::string parentDirectory(const std::string &dir);

  ContentView &view_;
  ModeStack &modeStack_;
  History &history_;
  const Configuration &configuration_;

  std::string currentDirectory_;
  std::vector<DirectoryEntry> entries_;
  std::set<int> markedIndices_;
  bool filenameOnlyMode_ = false;
  std::string searchAnnotation_;
};

}  // namespace fm
