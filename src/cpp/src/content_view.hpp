#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "display.hpp"
#include "pipe_buffer.hpp"  // AttributeRun -- see Line::attributes' own comment

namespace fm {

// Scrollable list of text lines: renders a page at a time, tracks a
// cursor line, and scrolls (single-line or full-page) through the
// list, including Display's hardware-scroll-region path when
// available. Ported from disp.c's put_str()/disp_pp() and scroll.c's
// move_page()/move_row()/sr_row()/sf_row().
//
// Data model change from the legacy C: PP was a hand-rolled doubly-
// linked list (prev/next pointers), the only option available before
// std::vector existed. ContentView holds lines in a plain
// std::vector<Line> with index-based cursor/viewport tracking
// instead -- legacy's RV struct tracked three separate pointers (ps
// = viewport start, pe = viewport end, pp = cursor) because a linked
// list has no O(1) "how far is the end of my visible window", so it
// had to be walked and remembered explicitly; with a vector, the
// viewport end is just `topIndex_ + viewportHeight_`, a derived
// value, so there's nothing separate to keep in sync (and no
// PP::prev/next chain to keep correct as a side effect of scrolling).
//
// Deliberately excluded (belongs to future classes, not ContentView):
//   - disp_window()/print_line()/disp_prev()/disp_copy(): the bottom
//     mode-line/status-bar rendering (file-name truncation, line
//     counter with its own interactive line-jump input widget,
//     previous-mode breadcrumb, PROGRAM-name display). This is
//     status-bar rendering deeply specific to each mode's own
//     format, not part of showing a scrollable list of lines --
//     future StatusBar/Mode territory.
//   - get_col()/init_col(): per-mode column-offset calculation for
//     file selection (already fixed historically, see CLAUDE.md item
//     13) -- Mode's job, unrelated to scrolling/rendering.
//   - Per-character attribute runs (legacy PP::attr, used for e.g.
//     GREP-match highlighting and hex-dump-mode byte coloring) and
//     the K_a0 sentinel-byte handling in put_str() tied to that: both
//     depend on content preprocessing that doesn't exist yet (the
//     future Buffer/PipeBuffer classes' job to produce). What IS
//     ported here is put_str()'s other two rendering behaviors that
//     don't need that preprocessing: whole-line reverse-video for the
//     cursor line, and reverse-video for raw control bytes -- see
//     renderLine().
//   - Charset awareness: unlike LineEditor, ContentView never needs
//     to translate a byte offset into a screen column (it prints
//     whole lines and lets the terminal wrap/advance naturally), and
//     control-byte detection (< 0x20) is identical across every
//     encoding. There is nothing charset-dependent left to port here,
//     so this class takes no Charset::Encoding at all. (The mode-line
//     fields' own overflow handling, added 2026-07-31, stays
//     consistent with this: it omits a whole field rather than
//     truncating at a byte offset that could split a multi-byte
//     character -- see composeModeLineRow()'s own comment -- so this
//     class still never needs to know how wide a character is.)
class ContentView {
 public:
  struct Line {
    std::string text;
    // Set only for a SHELL prompt line that has an actually-submitted
    // command attached to it (Shell::submitLine()'s own use) -- the
    // exact text to resubmit if the user navigates a cursor back onto
    // this line and presses Enter, ported from legacy's shell.c
    // treating the whole scrollback+prompt history as ONE buffer a
    // cursor can move through (see Shell's own class comment). Every
    // other caller of ContentView (DirectoryMode, FileMode, GrepMode,
    // etc.) leaves this at its default nullopt; ContentView itself
    // never reads it -- purely opaque per-line data Shell attaches
    // and later inspects on its own lines_.
    std::optional<std::string> shellCommand;

    // Byte offset (== screen column; see class comment on why this
    // class never distinguishes the two) where the cursor line's
    // reverse-video highlight should START, when this line is the
    // cursor line -- nullopt (the default) means "from column 0", the
    // previous unconditional whole-line behavior every existing
    // caller still gets. Ported from scroll.c's get_col()/init_col():
    // legacy's cursor highlight for `ls -alF`-style directory listings
    // starts at the filename, not column 0 (stand_str()'s
    // `&rvp->pp->cs[rvp->col]`) -- found via a real side-by-side CUI
    // comparison screenshot (2026-08-03) showing this port's directory
    // listing wrongly reverse-videoing the whole permissions/size/date
    // prefix along with the name. DirectoryMode is the only caller
    // that sets this today; every other ContentView user (locate/grep/
    // history/man/... lists) legitimately highlights the whole line in
    // legacy too (init_col() returns 0 for those modes), so leaving
    // this nullopt for them is correct, not an oversight.
    std::optional<int> highlightStartColumn;

    // Bold/underline byte-ranges (2026-08-05), e.g. from nroff-style
    // overstrike ("X\bX" bold, "_\bX" underline) that PipeBuffer's own
    // overstrike-detection already parses out of raw file content into
    // AttributeRun{start,end,Display::Attribute} -- see PipeBuffer's
    // own class comment. Previously detected but discarded everywhere
    // (FileMode's loadFile()/reflow() kept only StyledLine::text,
    // CutMode didn't route content through PipeBuffer at all, so
    // HELP.jp's own bold section-number/heading decoration was invisible
    // both in File mode and in HELP/CUT) -- a real user question ("HELP.jp
    // 画面内文字列の装飾（ボールド・アンダーライン）は簡単？") prompted
    // wiring this through instead of leaving it as a known gap. Empty
    // for every OTHER caller (DirectoryMode, GrepMode, ListBrowserMode,
    // etc.) exactly like highlightStartColumn is -- those never had
    // overstrike-formatted content to begin with.
    std::vector<AttributeRun> attributes;
  };

  explicit ContentView(Display &display);

  // Where on screen this view lives (row0..row0+height-1) and how
  // many lines tall it is.
  void setViewport(int row0, int height);

  void setLines(std::vector<Line> lines);
  const std::vector<Line> &lines() const;

  int cursorIndex() const;
  void setCursorIndex(int index);

  // Maps an absolute DISPLAY row (the same coordinate space
  // moveCursor()/setViewport() use, not viewport-relative) to the
  // line index currently rendered there, or -1 if displayRow is
  // outside this view's own viewport or past the end of lines_ (a
  // blank row within the viewport). Added for mouse click handling
  // (Application::handleMouseClick(), see IDEAS.md's GUI-mode click-
  // to-select/open request) -- the inverse of what redraw() already
  // computes internally (row0_+row <-> topIndex_+row) but not
  // previously exposed since nothing needed it before a pointing
  // device existed.
  int lineIndexAtRow(int displayRow) const;

  // Whether `column` falls within lineIndex's own highlighted span --
  // [highlightStartColumn, text.size()), see Line::highlightStartColumn's
  // own comment for why a byte offset and a screen column are the same
  // number here. false if lineIndex is out of range. Added for
  // Application::handleMouseClick()'s own use (2026-08-05 user
  // request): a mouse press landing OUTSIDE this span -- DirectoryMode's
  // `ls`-style listing has a fixed-width left margin (permissions/
  // size/date) before the highlight even starts, and any left-aligned
  // list (recent-file/-directory pickers, etc.) can leave a variable-
  // width empty tail after a short entry name -- was never actually
  // "on" the entry visually. The user's own proposal for distinguishing
  // a genuine click on an entry from the press that starts a "cat's
  // paw" screen drag: rather than a timing/motion-based heuristic
  // (which turned out fragile, see Application::handleMouseClick()'s
  // own history), just check WHERE the press landed -- inside an
  // entry's own visible highlighted text, or in the surrounding margin
  // that was never part of it to begin with.
  bool isColumnWithinLineScope(int lineIndex, int column) const;

  // The inverse of lineIndexAtRow(): the absolute DISPLAY row
  // cursorIndex_ itself currently renders at, or -1 if it's scrolled
  // out of view. Added for Shell's own ^P/^N history browsing
  // (2026-08-04): with setCursorHighlightEnabled(false) (see its own
  // comment -- legacy's stand_str() skips SHELL's own whole-line
  // reverse-video highlight entirely, `f_M == M_shell` in its own
  // early-return), there's no colored bar left to show which line is
  // "current" while browsing -- the terminal's own plain text cursor,
  // moved here instead, is legacy's actual replacement.
  int cursorDisplayRow() const;

  // Explicitly asserts (or clears) the display's own visible cursor
  // (Display::setVisibleCursor()/hideVisibleCursor()) to match this
  // view's current cursorHighlightEnabled_ state: shows a plain text
  // cursor at cursorDisplayRow() when highlighting is OFF (File mode's
  // own case -- see setCursorHighlightEnabled()'s comment on why plain
  // file content has no whole-line highlight, yet a real 2026-08-04
  // user request explained the CURRENT line still needs to be visually
  // and programmatically knowable in both -c/-g: launching an editor
  // at the current line, and a future COPY mode's row+column-based
  // selection, both need a real cursor position the way a highlighted
  // selector screen's cursorIndex_ alone already provides), or clears
  // it when highlighting is ON (a highlighted selector already shows
  // "where you are" on its own -- a second, redundant cursor glyph on
  // top would be visual noise, and a STALE cursor left over from a
  // PREVIOUS mode that did use one, e.g. File mode's cursor still
  // sitting there after returning to Directory mode, is exactly the
  // kind of leftover-state bug hideVisibleCursor() exists to prevent
  // -- see Application::returnFromShell()'s own comment for the first
  // occurrence of this pattern).
  //
  // Deliberately NOT called automatically from redraw()/moveUp()/
  // moveDown()/etc. themselves: SHELL's own scrollback_ ALSO has
  // cursorHighlightEnabled_ off, but its meaningful cursor position
  // while actively editing the live prompt line is the prompt's own
  // EDIT COLUMN (set by LineEditor::redraw()'s own setVisibleCursor()
  // call), not column 0 of cursorDisplayRow() -- baking an automatic
  // call into redraw() itself would silently clobber that more precise
  // position every time ANY unrelated redraw() ran (the exact "-gの
  // SHELLでは...一瞬出て消えるパターン" bug this whole mechanism was
  // built to eliminate). Callers that represent "the user just
  // genuinely navigated" (FileMode::loadFile()/reflow(), Application's
  // generic MoveUp/MoveDown/PageUp/PageDown/GoToTop/GoToBottom/Redraw
  // dispatch) call this explicitly instead -- SHELL keeps its own
  // separate, already-correct wiring (moveDisplayCursorToShellHistory
  // Line()) and is never routed through Application's generic command
  // dispatch at all (see dispatchByte()'s own Shell-mode branch), so
  // the two mechanisms never actually collide in practice.
  void syncVisibleCursor();

  // The index of the topmost currently-visible line. Added alongside
  // setTopIndex() for the GUI drag-scroll prototype (see
  // GraphicalDisplay's own class comment) -- scrolling the viewport
  // WITHOUT moving cursorIndex_ (unlike moveUp()/moveDown(), which
  // always couple the two) matches how a real trackpad/mouse-wheel
  // scroll works in most applications: it repositions what's visible,
  // it doesn't change the selection.
  int topIndex() const;
  // Sets the topmost visible line directly (clamped to a valid
  // range), leaving cursorIndex_ untouched. Does not redraw --
  // callers driving a custom render loop (like the drag-scroll
  // prototype) own their own redraw timing.
  void setTopIndex(int index);

  // Moves topIndex_ by exactly one row in `direction` (>0 reveals
  // LATER content, <0 reveals EARLIER content -- same sign convention
  // GraphicalDisplay::renderScrolled()'s pixelOffsetY uses) AND pins
  // cursorIndex_ to the newly-revealed edge, same as scrollLineDown()/
  // scrollLineUp()'s own state update but without their drawing side
  // effects (a caller driving a custom render loop, e.g. the GUI
  // drag/coast-scroll physics in main.cpp, owns its own redraw
  // timing via renderScrolled(), same as setTopIndex()). Returns false
  // (a no-op) at a list boundary.
  //
  // Added to fix a real report: the GUI coast/drag path used to leave
  // cursorIndex_ frozen wherever it was BEFORE the scroll started
  // (setTopIndex() deliberately doesn't touch it, see its own
  // comment), syncing it to topIndex_ only once the scroll settled --
  // which, after coasting all the way to the list's end, put the
  // cursor at the TOP of the now-bottom-scrolled viewport instead of
  // following the bottom edge the way 'j' (scrollLineDown()) does.
  // Continuously tracking the edge via this method instead removes
  // the need for that end-of-scroll sync altogether (cursorIndex_ is
  // simply never stale to begin with).
  bool stepTopIndex(int direction);

  // Non-mutating peek at stepTopIndex()'s own boundary condition --
  // see its own .cpp comment for why (lets a caller check "is there
  // room to move this way" before committing to a sub-row pixel move,
  // rather than only discovering the boundary after the fact).
  bool canStepTopIndex(int direction) const;

  // Highlights an arbitrary line instead of cursorIndex_, without
  // moving the cursor/selection -- for the 2026-07-30 real-terminal
  // drag-scroll trigger (see CLAUDE.md): while coasting, the row under
  // the mouse pointer is highlighted so a click can open it directly,
  // but the actual keyboard cursor position shouldn't move just from
  // hovering. Pass nullopt (the default) to go back to highlighting
  // cursorIndex_ as usual. Does not redraw -- same "caller owns
  // render timing" convention as setTopIndex().
  void setHoverIndex(std::optional<int> index);
  std::optional<int> hoverIndex() const;

  // Whether the cursor/hover line gets whole-line reverse video at
  // all (default true). Legacy's put_str() only reverses a whole line
  // for CP/RM/MV/ARC's own marked-selection tracking (fmark()'s
  // f_M switch, ported: none of that applies to plain directory
  // listing or file viewing) -- this port had already been drawing a
  // cursor-line highlight everywhere as a basic "where am I" cue
  // before this flag existed, which a real CUI/GUI side-by-side
  // comparison confirmed matches Directory mode's real look but NOT
  // File mode's (a real screenshot: plain file content has no such
  // highlight at all). FileMode disables this on its own ContentView;
  // DirectoryMode leaves it at the default.
  void setCursorHighlightEnabled(bool enabled);
  bool cursorHighlightEnabled() const;

  // Text redraw() shows, highlighted, on an otherwise-empty list (see
  // redraw()'s own comment) -- default "--" (a directory/file-listing
  // convention: CUT reuses this same view for its own list-of-segments
  // display, where "--" already means "an empty region between two
  // matches", so an empty overall list reads consistently as "an empty
  // region" too). GrepMode overrides this to "No String" (2026-08-12
  // fix): a real user correction -- "EGREPはファイルの切り分けでは
  // ないので「--」ではなくて「No String」" (EGREP isn't about
  // splitting a file into regions, so an empty result should read "No
  // String", not "--") -- after a real report where a search with zero
  // matches showed "--" the same way CUT's own empty-region marker
  // does, which is misleading outside CUT's actual region-splitting
  // context.
  void setEmptyPlaceholderText(std::string text);

  // Whether fewer-than-height_ lines render flush against the BOTTOM
  // of the viewport (blank padding rows above them) instead of the
  // default flush-against-the-TOP (blank padding rows below) --
  // default false, every existing caller unaffected. Added for Shell's
  // own scrollback_ (2026-08-04): a live prompt fixed at exactly
  // row0_+height_ (the row right after this view's own content area)
  // only ever looks right if the last real content line is ALWAYS
  // immediately above it -- with the previous top-anchored default,
  // that's only true once scrollback_ has grown to completely fill
  // height_; before that point, the prompt either has to sit at a
  // fixed row far below a short amount of content (an unnatural gap)
  // or follow the content's own growing edge (Shell's own EARLIER
  // fix, which introduced its own re-sync bugs -- see Shell's class
  // comment for the fuller history). Bottom-anchoring instead means
  // the answer to "which row is right after the content" is the SAME
  // fixed row regardless of how little content exists, eliminating
  // the whole class of prompt/content re-sync bugs at the source
  // rather than patching each one as found.
  void setBottomAnchored(bool enabled);

  // -- scrolling --
  // Single-line cursor movement (M_up/M_dn/M_kup/M_kdn): moves the
  // cursor one line, scrolling the viewport by one line (using
  // Display's hardware scroll when smoothScrollEnabled(), a full
  // redraw otherwise) only if the cursor would otherwise leave it.
  void moveUp();
  void moveDown();

  // "Move window one line up/down" (usilicrc's own K-8/K-9
  // description, bound to 'j'/'k' by default) -- deliberately
  // DIFFERENT from moveUp()/moveDown(): those move the CURSOR by one
  // line and only scroll the viewport if the cursor would otherwise
  // leave it (usilicrc calls that pair, bound to ^N/^P, "move cursor
  // one line"); this pair always scrolls topIndex_ by exactly one
  // line (clamped at either end, same boundary-bell convention as
  // moveUp()/moveDown()) and moves cursorIndex_ to the newly-revealed
  // edge line, same as a single-line `less` scroll. An earlier version
  // of this port aliased 'j'/'k' to plain cursor movement instead
  // (Application::resolveCommand()'s own comment has the correction's
  // full story) -- confirmed against usilicrc directly, not present in
  // scroll.c's move_row() itself (no C source binds 'j'/'k' as literal
  // keys at all; only usilicrc's configurable key table does).
  void scrollLineDown();
  void scrollLineUp();

  // Full-page movement (M_back/M_forw): always a full redraw, ported
  // from move_page(). The legacy smooth-scroll path also animated
  // page moves one line at a time when hardware scrolling was
  // available; simplified here to a single redraw regardless -- see
  // the class comment on scope.
  void pageUp();
  void pageDown();

  // Jump to the first/last line (M_head/M_tail).
  void goToTop();
  void goToBottom();

  // -- rendering --
  void redraw();

  // The formatted "current/total NN%" text this view is currently
  // showing on its mode-line row (see renderModeLine()) -- exposed so
  // a custom render loop that bypasses redraw() (GraphicalDisplay's
  // drag-scroll path, see its own renderScrolled() overload) can draw
  // the same text itself without duplicating ModeLine's position math.
  // Percent is cursor-position-based (see ModeLine::format()'s own
  // comment): 0-ish/"TOP" at the very start, climbing to "END" at the
  // very end, regardless of where the viewport happens to be scrolled
  // to.
  std::string modeLineText() const;

  // Same formatting, but for the drag/coast-scroll render path
  // specifically: uses topIndex_ (the viewport's own top line) instead
  // of cursorIndex_. A mouse-drag/Shift+arrow coast deliberately never
  // moves cursorIndex_ (see moveUp()/moveDown() vs setTopIndex()'s own
  // comments -- scrolling the viewport isn't the same as moving the
  // selection), so during a coast modeLineText()'s cursorIndex_-based
  // number would sit frozen while the view visibly scrolled -- using
  // topIndex_ instead keeps the mode-line moving in step with what's
  // actually on screen during a scroll, at the cost of it no longer
  // meaning "the selected line" while actively scrolling (it goes
  // back to meaning that the moment modeLineText() takes over again,
  // once the scroll settles). Reaching the BOTTOM boundary (the
  // viewport's own bottom row shows the list's last line) reports END
  // same as modeLineText() does -- checked separately from topIndex_
  // itself, since topIndex_ caps out at total-height_ and so can never
  // reach totalLines the way cursorIndex_ can (a real report: coasting
  // to the very bottom stayed stuck on a percentage instead of
  // reaching END the way plain 'j'/'s' navigation already did).
  std::string scrollModeLineText() const;

  // Same composition as scrollModeLineText() (topIndex_-based current
  // line, same name/position/trailing fields), but as a sequence of
  // Display::ModeLineSegment instead of one flat string -- for
  // Display::renderScrolled() callers, which need to reproduce
  // renderModeLine()'s own "separate reverse-video runs with plain
  // gaps between fields" look (see ModeLineSegment's own comment for
  // the real screenshot bug this fixes: renderScrolled()'s mode-line
  // used to draw scrollModeLineText()'s flat string as ONE
  // uninterrupted highlight bar, with no gaps at all). Does not
  // reproduce renderModeLine()'s own separate right-aligned-bold-
  // watermark fallback (Directory mode, empty trailing) -- that
  // fallback never applies to any mode renderScrolled() itself is
  // used for (file-content scrolling always has a non-empty trailing
  // path), so it was deliberately left out rather than ported
  // untested.
  std::vector<Display::ModeLineSegment> scrollModeLineSegments() const;

  // The two mode-specific pieces legacy's disp_window() wraps around
  // the current/total position text (ported from its own field
  // layout, see the comment on setModeLineFieldsProvider() below):
  //   name     -- mode-line[1], drawn before the position (e.g. the
  //               directory's full path in Directory mode, or a
  //               file's bare name in File mode -- which one legacy
  //               shows depends on the mode, not on ContentView).
  //   trailing -- mode-line[6] ("previous-mode" in legacy's own
  //               comment), drawn after the position (e.g. "(ls)" in
  //               Directory mode, or "<size> <directory>/" in File
  //               mode). Empty means nothing is drawn there.
  //   fillTrailingToEdge -- legacy pads THIS field specifically with
  //               spaces out to the terminal's own right edge (see
  //               disp_window()'s own `for (...) prev[i] = ' '` loop),
  //               so its reverse-video run visually reaches the edge
  //               with no gap -- but only for modes that route their
  //               trailing text through that specific field (File
  //               mode's size+directory does; Directory mode's "(ls)"
  //               is a short, separate field that legacy does NOT pad,
  //               leaving a plain gap after it -- confirmed by
  //               comparing real CUI screenshots of both modes side by
  //               side, not by inspection of the C source alone, since
  //               the source distinguishes them only by which of two
  //               different disp_window() parameters (`arg` vs `prev0`)
  //               each mode.c call site happens to populate). Ignored
  //               when trailing is empty.
  //   rightWatermark -- disp_window()'s own fallback for when mode-
  //               line[6] has no "previous mode" text at all (prev0
  //               is the K_z/K_y sentinel in the C source): the
  //               program name and version, right-aligned in BOLD
  //               (not reverse-video) at the screen's own right edge,
  //               e.g. "usilic v1.5b" -- confirmed against a real CUI
  //               screenshot of the directory-listing screen
  //               specifically (a real report: this port's earlier
  //               attempt showed nothing there at all, having
  //               mistaken the "(ls)" field -- a completely separate
  //               piece, populated from a different disp_window()
  //               parameter -- for covering this slot too). File
  //               mode's own trailing field (fillTrailingToEdge)
  //               already fills the row and leaves no room, matching
  //               legacy: prev0 is non-empty there, so the fallback
  //               branch never runs in the C source for that mode
  //               either -- DirectoryMode is the only current caller
  //               that sets this. Drawn only if it fits alongside
  //               whatever's already been drawn (dropped, not
  //               truncated, same overflow policy as name/trailing).
  struct ModeLineFields {
    std::string name;
    std::string trailing;
    bool fillTrailingToEdge = false;
    std::string rightWatermark;
    // A SECOND independently-highlighted trailing segment, drawn after
    // `trailing` with its own plain (non-standout) single-space gap in
    // between -- added 2026-08-10 for FileMode's own byte-size/directory
    // split (a real report, screenshots: the byte size and directory
    // were one single unbroken highlight run with no gap, unlike every
    // other mode-line field pair -- "1108009  /Users/.../" should look
    // like "CLAUDE.md [gap] 00034/13761  0%" instead, two separate
    // boxes). Empty (the default) for every other mode's provider,
    // meaning no behavior change there. When both this and
    // `fillTrailingToEdge` are set, the edge-padding applies to THIS
    // field (the last one drawn), not `trailing` -- see
    // renderModeLine()'s/scrollModeLineSegments()'s own comments.
    std::string trailing2;
    // Draws `trailing` as plain (non-reverse-video) text instead of
    // the usual highlighted run -- added 2026-08-11 for the ^J^S/^J^C
    // candidate list's own "live preview" use (see listBrowserView_'s
    // own provider): a real report + reference screenshot showed the
    // currently-highlighted candidate's pattern text (e.g.
    // "^[0-9]*¥.") drawn reverse-video in the mode line, but that
    // position is meant to read as an EDITABLE TEXT AREA (what would
    // populate if you started typing), which every other editable
    // field in this port (promptEditor_'s own LineEditor) renders
    // plain, not highlighted -- highlighting it made it look like a
    // static label instead. Every other provider leaves this at its
    // default (false, the pre-existing always-highlighted behavior),
    // via aggregate-init's default-member-initializer fallback for
    // callers that don't list this 6th field explicitly.
    bool trailingPlain = false;
  };

  // Supplies this view's ModeLineFields, recomputed every time the
  // mode-line is about to be drawn (so it always reflects whichever
  // line is CURRENTLY selected/loaded, without ContentView needing to
  // be told again every time the cursor moves or a new file loads).
  // ContentView itself has no notion of a line's "full name" or a
  // mode's own identity (Line only carries the whole formatted
  // display row, e.g. ls -l's full column layout) -- that's Mode-
  // specific knowledge (DirectoryMode's currentDirectory(), FileMode's
  // currentFile(), etc), so this is a callback rather than a plain
  // string. A default-constructed (empty) provider means no extra
  // fields at all -- just the bare position text (every existing
  // mode's mode-line is unchanged from before this was added, until
  // its own Mode class opts in).
  using ModeLineFieldsProvider = std::function<ModeLineFields(int cursorIndex)>;
  void setModeLineFieldsProvider(ModeLineFieldsProvider provider);

  // Overrides which screen row the mode-line draws on, decoupling it
  // from the default row0_+height_ -- nullopt (the default) keeps
  // every existing caller's behavior unchanged. Added for Shell's own
  // scrollback_ view (2026-08-03): SHELL needs a THIRD row, permanently
  // reserved below the scrollable content, for its live prompt overlay
  // (see Shell::commandLineRow()'s own comment) -- without this, the
  // prompt and the mode-line would either collide with each other or
  // with genuine scrollback content, depending on how much scrollback
  // exists (a real side-by-side comparison against legacy found the
  // prompt and the true last content line fighting over the same row
  // once scrollback filled the viewport, see Shell's own class comment
  // for the full investigation).
  void setModeLineRow(std::optional<int> row);

 private:
  void renderLine(const Line &line, bool marked);
  void renderModeLine();
  void clampViewport();
  int bottomAnchorRowOffset() const;

  // Which of ModeLineFields' name/trailing pieces fit alongside
  // `positionText` in the display's own column count -- dropped,
  // trailing first then name (positionText itself is always kept),
  // rather than truncated: a partial field cut at an arbitrary byte
  // offset risks splitting a multi-byte UTF-8 character (this class
  // deliberately stays charset-ignorant otherwise, see the class
  // comment's own note on why -- silently dropping a whole field
  // sidesteps that instead of introducing width-aware truncation
  // here), and on a real terminal (TerminalDisplay::write() has no
  // bounds-checking of its own) an actual overflow would wrap into the
  // next screen row and corrupt it, not just look visually crowded.
  // Shared by composeModeLineRow() (the plain-string consumers, e.g.
  // GraphicalDisplay::renderScrolled()'s drag-scroll path) and
  // renderModeLine() (the segmented-standout-run real render path)
  // so both agree on what's shown.
  struct FieldFit {
    bool showName = false;
    bool showTrailing = false;
    // Independent of showTrailing (2026-08-11 fix): a real report
    // found the ^J^C candidate list's own long regex-pattern preview
    // (`trailing`) pushing the whole trailing region over the column
    // budget, which silently dropped trailing2 (the destination
    // directory) too even though trailing2 alone would easily fit --
    // see fitModeLineFields()'s own comment on the drop priority.
    bool showTrailing2 = false;
  };
  FieldFit fitModeLineFields(const ModeLineFields &fields, const std::string &positionText) const;

  // Assembles "name positionText trailing" (see fitModeLineFields())
  // into one plain string, for callers that don't render field-by-
  // field (composeModeLineRow()'s own consumers never call
  // display_.setAttribute() themselves, so unlike renderModeLine()
  // there's no separate reverse-video run per field here -- see
  // modeLineText()'s own comment on who those callers are).
  std::string composeModeLineRow(const std::string &positionText) const;

  Display &display_;

  std::vector<Line> lines_;
  int cursorIndex_ = 0;
  int topIndex_ = 0;
  std::optional<int> hoverIndex_;
  bool cursorHighlightEnabled_ = true;
  bool bottomAnchored_ = false;
  std::string emptyPlaceholderText_ = "--";

  int row0_ = 0, height_ = 0;
  ModeLineFieldsProvider modeLineFieldsProvider_;
  std::optional<int> modeLineRow_;
};

}  // namespace fm
