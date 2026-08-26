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

  // Re-runs `ls` on the CURRENT directory in place -- not a
  // navigation (no ModeStack frame, no history consultation), just a
  // refresh, for openSelected()'s own "./" case and Application::
  // goToFileSelector()'s own already-there Directory case (2026-08-23
  // additions, both ported from the same real user report). Restores
  // the cursor to whichever entry has the SAME NAME as whatever was
  // selected before the refresh (falling back to row 0 if that name
  // is gone, e.g. renamed/deleted by whatever external change
  // triggered the refresh) -- a direct user correction: consulting
  // saved history position here (enterDirectory()'s own default)
  // jumped the cursor to a STALE row from some earlier, unrelated
  // visit instead of keeping the user right where they currently
  // were ("現在行位置が更新されない...^P^P^P...^J^K（マイナカード表.
  // pngに戻る）" -- moving three rows away, then refreshing, snapped
  // back to the old position instead of staying put).
  bool refresh();

  // K-19 (^O^K, M_m): jumps straight to the parent of currentDirectory(),
  // pushing a ModeStack frame the same way openSelected() does for a
  // "../" entry -- this is that exact same navigation, just reachable
  // without first moving the cursor onto "../". Ported from m_dir()'s
  // own M_m case: at the root ("/"), legacy's own path-truncation logic
  // is a no-op (nothing before the one leading '/' to strip), which
  // this mirrors by refusing (false) instead of re-entering "/"
  // pointlessly.
  bool goToParent();

  // Shared by openSelected()/goToParent() (^G-navigable descent, NOT
  // a bare reload of the same directory): saves the current cursor
  // position, pushes a ModeStack frame remembering where to come back
  // to, and enters `target` -- undoing the push on failure. Made
  // public (2026-08-23 fix, a real report: "CPで/tmpへのコピーを
  // 終えた後の（ls）では^Gで/tmpから変わらない") so Application's own
  // CP/MV/RM-completion destination landing (pollPendingCpMvRmShellRun())
  // goes through the SAME history-pushing path ordinary navigation
  // does, instead of calling enterDirectory() directly -- which loads
  // a NEW listing but (by design, see its own comment) never touches
  // modeStack_ itself, leaving ^G with nothing to pop back to.
  bool descendTo(const std::string &target);

  // ^G: pops the most recent ModeStack frame (if any) and returns to
  // it, restoring its cursor position from History. Returns false if
  // the stack is empty (nothing to go back to). A popped FLOOR frame
  // (see resetNavigationToFloor()'s own comment) is handled specially:
  // goes to its previousDirectory instead of its own directory, and
  // re-pushes a floor with the two swapped, so repeated ^G presses
  // genuinely toggle back and forth rather than exhausting after one
  // pop.
  bool goBack();

  bool canGoBack() const;

  // K-48 (^X^D, "DIR: Select directory name" -- the history/recency
  // picker, NOT ordinary Enter-into-subdirectory descent): resets ^G's
  // own navigation stack to a fresh "floor" rooted at the
  // JUST-ENTERED directory (currentDirectory_ as of the call), so a
  // multi-level descent chain accumulated before the picker was
  // invoked doesn't leak into ^G's behavior afterward. Call AFTER
  // enterDirectory() has already updated currentDirectory_ to the
  // newly-picked directory. Ported from legacy's save_stack_dir()
  // sentinel (ModeStack::kFloorMarker) -- pushFloor()/isFloor() already
  // existed in ModeStack but nothing called them, the same
  // "existing but unwired" pattern found repeatedly elsewhere in this
  // port. Added 2026-08-13 per a user-specified, explicitly PROVISIONAL
  // ^G policy pending real-world verification (not a guaranteed-final
  // design): "「選択後の画面」以降のDIR...については履歴を廃棄して
  // 真っ新から始める...^Gの連打もDIRならカレントおよび以前の
  // ディレクトリを行き来".
  void resetNavigationToFloor(const std::string &previousDirectory);

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

  // Shows "Invalid mode" (ported verbatim from legacy mode.c's
  // do_edit(), S_errv -- see str.c's string table) underlined in the
  // mode line's `name` slot, in place of the current directory path,
  // until clearEditError() is next called -- ported from a direct
  // user request: pressing ^Je (EDIT) on a directory entry (rather
  // than a file) should show this "適切な" error there instead of
  // silently doing nothing (Application::executeEdit() previously
  // just returned false with no feedback at all). Called by
  // executeEdit(). A thin convenience wrapper around reportError()
  // below (its own original, and for a while only, use case).
  void reportEditError();

  // Same underlined mode-line-`name`-slot presentation as
  // reportEditError(), but with an arbitrary caller-supplied message --
  // generalized 2026-08-21 for CpMvRmMode::isWritable() failures
  // (executeMove()/executeRemove(), see Application's own comment): a
  // direct user request (docs/HISTORY.md:1840, "chown実行後のRM、MVでは
  // ファイルが選べない...その旨何らかのメッセージ表示があれば親切")
  // asked that a permission-denied RM/MV target explain itself instead
  // of legacy's silent mark-time beep. Reuses the exact presentation
  // docs/HISTORY.md:8912 already established for ^Je's "Invalid mode",
  // rather than inventing a second, differently-behaved status-message
  // mechanism.
  void reportError(const std::string &message);

  // Clears whatever reportEditError()/reportError() set, reverting the
  // mode line's `name` slot back to the current directory path on the next
  // redraw. Called once, unconditionally, at the top of Application::
  // handleCommand() for every dispatched command (not from the mode-
  // line provider itself on read -- see its own comment on why: -g's
  // render loop calls redraw() far more often than a real keypress
  // alone, which made a read-and-clear version of this flag vanish
  // almost instantly there, a real 2026-08-17 report; and not
  // dispatchByte(), since some real redraw triggers -- and some
  // callers/tests -- reach handleCommand() directly without going
  // through it). A no-op if nothing is currently showing.
  void clearEditError();

  // Shows `label` ("CP"/"MV"/"RM") plain-Standout in the mode line's
  // `name` slot, replacing the directory path -- see the mode-line
  // provider's own comment (directory_mode.cpp) for the full 2026-08-21
  // rationale. Called by Application::startCopyPrompt()/
  // startMovePrompt()/startRemovePrompt() when they arm an operation.
  void setPendingOperationLabel(const std::string &label);
  // Clears whatever setPendingOperationLabel() set. Called by
  // Application::startDestinationPrompt() once it consumes the arm
  // (whether it goes on to show the real prompt or not), and by
  // enterDirectory() itself (alongside its own markedIndices_.clear())
  // so a stale arm left over from navigating away doesn't keep
  // displaying a label for an operation that's no longer actually
  // armed.
  void clearPendingOperationLabel();

  // Shows "ARC: <archivePath>" as a highlighted trailing3 field
  // (2026-08-26, a direct user request/spec for landing here after a
  // successful ^X^M/^X a archive extraction, see Application::
  // pollPendingArchiveExtractShellRun()'s own comment) -- purely
  // informational ("where this directory's newly-extracted content
  // came from"), cleared the same way setPendingOperationLabel()'s own
  // arm is: by enterDirectory() itself, so it doesn't linger once the
  // user navigates on to somewhere else.
  void setArcExtractSourceAnnotation(const std::string &archivePath);
  // 2026-08-27 addition, a direct user proposal ("ディレクトリ（ls）の
  // 戻り値に「ARC:」等モードへの帰還があるときは^J^Jルーチンへ移行
  // じゃまずいの？"): lets Application::handleCommand(Command::Back)
  // check for this annotation the SAME way it already checks
  // hasSearchAnnotation() just above (that precedent routes Back to
  // returnToGrepResults()/returnToCutResults() instead of the generic
  // ModeStack-based goBack() -- this is the identical shape, just
  // routing to goToRecentFile()/^J^J instead, since a ModeStack frame
  // can't represent "go back to Archive mode" at all, see
  // pollPendingArchiveExtractShellRun()'s own comment).
  bool hasArcExtractSourceAnnotation() const { return !arcExtractSourceAnnotation_.empty(); }

  // Records the CURSOR's own row against currentDirectory_ in History
  // (PositionCategory::Directory), the same bookkeeping descendTo()/
  // goBack() already do internally before navigating away -- exposed
  // publicly (2026-08-27, a real report: "^J^K→カーソル位置が
  // 「usilicrc」じゃない") for Application::openSelectedEntry(),
  // which switches to File/Archive/Exec mode WITHOUT going through
  // either of those (it's not directory-to-directory navigation), so
  // the row the user had selected right before opening a file was
  // never actually saved -- a later goToFileSelector()/plain
  // enterDirectory() back into this same directory then had nothing
  // but a stale, possibly much older saved position to restore.
  void saveCurrentCursorPosition();

 private:
  DirectoryEntry parseLsLine(const std::string &line) const;
  // "./"/"../" are never valid CP/MV/RM targets -- see toggleMark()'s
  // own comment.
  bool isSelfOrParentEntry(int index) const;
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
  bool showEditError_ = false;
  std::string editErrorMessage_ = "Invalid mode";
  std::string pendingOperationLabel_;
  std::string arcExtractSourceAnnotation_;
};

}  // namespace fm
