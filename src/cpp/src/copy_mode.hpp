#pragma once

#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// COPY/PASTE (M_copy/^O^C + M_paste/^O^Y): freezes a plain-text-plus-
// attributes snapshot of whatever content area was on screen, lets the
// user move a real (row, column) caret over it and mark a same-row
// substring, and holds the result in one shared "yank buffer" that any
// later PASTE reuses. Ported from code.c's make_map() in spirit --
// legacy's own local variables (row/col/ci/col0/c0/f_copy, all scoped
// to that one function's call-stack for the duration of the COPY
// session) become this class's members instead, so Application never
// carries any of this state directly. This is a direct lesson from
// the SAME session's own prompt-history redesign (see CLAUDE.md's
// 2026-08-28/29 entries and .steering/20260828-prompt-history-redesign/):
// state that only makes sense while one feature's own sub-loop is
// running belongs inside that feature's own class, not scattered onto
// Application as ad hoc members.
//
// Unlike make_map(), which strips every color/highlight escape byte
// out of the snapshot before ever storing it, this class KEEPS each
// Line's AttributeRun data (Bold/Underline) exactly as it arrived --
// C++'s AttributeRun is already meaningful structured data (nroff
// overstrike detection), not a rendering-only escape sequence like
// legacy's K_h, so there's nothing to strip for on-screen display.
// That said, the EXTRACTED clipboard substring (clipboardText()) is
// always plain std::string with no attribute data attached: the only
// PASTE destination in this whole port (LineEditor::paste(), a single-
// line plain-text field) has no way to represent or display styled
// text at all, so carrying attributes past the clipboard boundary
// would have nowhere to go (see requirements.md's "装飾文字の扱い").
//
// Deliberately excluded (Application's job, same boundary every other
// *Mode class in this port draws): deciding WHEN to enter/leave this
// mode (^O^C/^O^Y key handling, remembering which ApplicationMode to
// return to), and taking the initial snapshot from
// Application::activeContentView().lines().
class CopyMode {
 public:
  explicit CopyMode(ContentView &view);

  // Takes ownership of a fresh snapshot (typically
  // Application::activeContentView().lines()): only `text` and
  // `attributes` survive from each input Line -- every other Line
  // field (highlightStartColumn, shellCommand, ellipsisEligible,
  // neverHighlighted) is mode-specific display metadata from the
  // ORIGIN mode that has no meaning once frozen here (mirrors
  // make_map()'s own PP->plain-text reduction). `initialRow` (default
  // 0) starts the cursor there instead -- a real user report
  // ("^Oc→ブロックカーソル位置が...正しくは直前のハイライト行位置")
  // confirmed legacy's own make_map() does the same (`row = rvp->row`,
  // right after taking the snapshot: the cursor starts on whichever
  // row was already selected/highlighted in the origin view, not
  // necessarily the top), so Application::startCopy() passes the
  // origin ContentView's own cursorIndex() here. Clamped to a valid
  // row; cancels any in-progress mark; does NOT clear clipboardText()
  // (legacy's rvp->copy persists across COPY sessions the same way).
  // Immediately (re)draws `view`.
  void loadSnapshot(std::vector<ContentView::Line> lines, int initialRow = 0);

  // -- cursor movement, bounds-checked (a no-op past either edge of
  // the snapshot) -- each one redraws `view`.
  //
  // moveUp()/moveDown(): a no-op while markActive() is true -- ported
  // from code.c's own M_up/M_dn (`if (!f_copy && row) vt_move(...)`):
  // legacy's row variable is only ever updated when NOT marking, so a
  // mark cannot be extended across rows at all (this class doesn't
  // "cancel the mark and move" -- it blocks the move outright, exactly
  // matching that condition). Preserves the current display column
  // across the row change when it IS allowed, same as legacy's own
  // col-preserving count_i()/count_h() round trip.
  void moveUp();
  void moveDown();
  // moveLeft()/moveRight()/moveToLineStart()/moveToLineEnd(): always
  // allowed regardless of markActive() (legacy's M_kprev/M_knext/^A/^E
  // cases never check f_copy) -- this is how a mark is actually
  // extended/shrunk once started.
  void moveLeft();
  void moveRight();
  void moveToLineStart();
  void moveToLineEnd();

  // M_mark toggle: first call anchors a selection at the current
  // cursor position; second call ends it. Ending with a non-empty
  // range (hi > lo) overwrites clipboardText(); ending with an empty
  // range (mark start immediately followed by mark end at the same
  // column) leaves the existing clipboardText() untouched, mirroring
  // legacy's own `if (*p) strcpy(rvp->copy, p)` guard.
  void toggleMark();
  bool markActive() const;

  // The shared "yank buffer" (legacy: RV::copy) -- always plain text,
  // see this class's own comment for why. Empty until the first
  // completed non-empty selection.
  const std::string &clipboardText() const;

 private:
  void cancelMark();
  void render();  // rebuilds view_'s Lines (folding in the live
                   // selection's own Standout AttributeRun, if any)
                   // and repositions view_'s real caret.

  ContentView &view_;
  std::vector<ContentView::Line> lines_;  // text + attributes only
  int cursorRow_ = 0;
  int cursorByte_ = 0;  // byte offset into lines_[cursorRow_].text,
                        // always landing on a UTF-8 sequence boundary
  bool markActive_ = false;
  int markAnchorByte_ = 0;  // valid only while markActive_
  std::string clipboardText_;
};

}  // namespace fm
