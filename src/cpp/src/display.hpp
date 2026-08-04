#pragma once

#include <string>
#include <vector>

namespace fm {

// Abstract screen: text output, cursor movement, attributes, and
// terminal geometry. Ported from tcb/src/tc.c + vt.c, which is
// already a de facto vtable in C (see the VT struct in tcb.h) -- this
// class formalizes that shape so a second implementation (a future
// SDL2-based GraphicalDisplay for the "cat's-paw" inertial-scroll GUI
// mode, see docs/ideas/IDEAS.md) can be dropped in without touching
// any caller.
//
// Deliberately excluded (belongs to future classes, not Display):
//   - Raw termios/raw-mode setup and reading keyboard bytes (vt_init(),
//     vt_getch(), vt_tfd()) -- that's about how input is read, not how
//     output is drawn; goes to a future KeyInput class.
//   - vt_cursor()/tc_cursor(): despite the name, this doesn't touch
//     cursor visibility at all -- it records which child SHELL pid
//     should be forwarded SIGWINCH (key_winch(-f_cursor) in the
//     legacy C). That's terminal-resize-forwarding bookkeeping for a
//     future Shell class, not a Display concern.
//   - The legacy 'W' vt_mode flag (SIGWINCH received while a SHELL
//     child is alive) -- same reason, future Shell/KeyInput territory.
//   - The A_md2/A_md3/A_us2 sub-tags legacy code used to remember
//     *why* an overstrike span was detected bold/underlined (see
//     alloc.c's save_attr()) -- Display only needs to know "turn bold
//     on", not which nroff overstrike pattern produced that request;
//     that classification is a Buffer/ContentView concern.
class Display {
 public:
  enum class Attribute {
    Underline,
    UnderlineOff,
    Bold,
    BoldOff,
    Standout,     // reverse video. Legacy had two code paths for this
                  // (vt_stand()/vt_endstand() calling so/se directly,
                  // and vt_seta(A_cs)/vt_seta(A_ce) doing the exact
                  // same thing) -- confirmed identical by reading
                  // tc_geta()'s A_cs/A_ce cases, so they're unified
                  // into one Attribute here instead of two APIs.
    StandoutOff,
  };

  enum class Key { Up, Down, Left, Right, PageUp, PageDown };

  virtual ~Display() = default;

  // -- raw output --
  virtual void write(const std::string &text) = 0;
  virtual void putChar(int c) = 0;
  virtual void flush() = 0;

  // -- cursor --
  virtual void moveCursor(int row, int col) = 0;
  virtual void cursorLeft() = 0;
  virtual void saveCursor() = 0;     // no-op if the terminal lacks this
  virtual void restoreCursor() = 0;  // capability (legacy: sc/rc optional)

  // Marks (row, col) as where the LOGICAL/meaningful cursor -- the one
  // a user would actually call "the cursor" (the live prompt-editing
  // caret, the SHELL scrollback line currently being browsed via
  // ^P/^N, and eventually a pager's current line / COPY mode's
  // character-range endpoint once those grow cursor-position-aware
  // GUI support) -- should visually rest, as opposed to moveCursor(),
  // which ALSO doubles as "where the next byte gets written" (every
  // ContentView::redraw() row-by-row write, every mode-line draw, ...
  // all move the cursor purely as writing side effects, with no
  // intent to park a visible cursor there).
  //
  // On a real terminal (TerminalDisplay) this distinction doesn't
  // matter: nothing about -c's rendering model needs a SEPARATE
  // "flush the whole screen" step the way -g does (every byte is
  // already visible the instant it's written), and -c's own run loop
  // processes one input event per iteration with nothing else
  // touching the screen afterward -- so the plain "last moveCursor()
  // call before the terminal goes idle" a real hardware cursor already
  // uses is enough, and TerminalDisplay's own implementation is just
  // moveCursor(row, col).
  //
  // GraphicalDisplay's -g run loop, though, polls at a steady ~60Hz
  // and unconditionally re-issues a full ContentView::redraw() (whose
  // OWN internal moveCursor() calls are pure glyph-placement, landing
  // wherever the mode-line's own text last ended) within the SAME
  // frame a caller like Shell's SHELL-history ^P/^N handling explicitly
  // asked for the browsed-to line -- without this separate concept,
  // that later incidental redraw was silently clobbering the
  // deliberately-set cursor position moments after it appeared (a real
  // 2026-08-04 user report: "-gのSHELLではプロンプトと同じく一瞬出て
  // 消えるパターン"). Tracking this as its own explicit state, updated
  // ONLY by callers that mean "show the cursor HERE" rather than by
  // every incidental text-placement moveCursor() call, structurally
  // prevents that class of clobbering regardless of a particular
  // frame's call ordering (see GraphicalDisplay's own
  // renderCursor()/Impl::visibleCursorRow comment for how it's used).
  virtual void setVisibleCursor(int row, int col) = 0;

  // Clears whatever setVisibleCursor() last asserted, so a stale
  // position doesn't linger visible across a mode switch into
  // something that never calls setVisibleCursor() itself (e.g.
  // leaving SHELL, whose ^P/^N history browsing/live prompt editing
  // both use it, back to a plain directory listing, which relies on
  // ContentView's own row-highlight instead and has no cursor position
  // of its own to assert). A no-op on TerminalDisplay/RecordingDisplay:
  // -c's real hardware cursor and this test double's own moveCursor()-
  // based recording never accumulated separate persistent "visible
  // cursor" state in the first place (see setVisibleCursor()'s own
  // comment for why only GraphicalDisplay needed the split at all), so
  // there's nothing to clear.
  virtual void hideVisibleCursor() = 0;

  // -- erasing / scrolling --
  virtual void clearScreen() = 0;
  virtual void clearToEndOfLine() = 0;
  virtual void scrollForward() = 0;
  virtual void scrollReverse() = 0;

  // -- attributes --
  virtual void setAttribute(Attribute attr) = 0;
  // Raw escape string for the attribute, for callers that need to
  // embed it inline in text built elsewhere (legacy use: shell.c
  // splices vt_geta(A_md)/vt_geta(A_me) directly into the bash PS1
  // string so bash itself prints the bold-on/off around it). Returns
  // an empty string if the terminal doesn't support the attribute.
  virtual std::string attributeEscape(Attribute attr) const = 0;

  // -- terminal geometry --
  virtual int rows() const = 0;
  virtual int columns() const = 0;
  virtual void setSize(int rows, int columns) = 0;

  // Raw escape sequence a terminal sends for the given key, for a
  // future KeyInput's raw-byte-stream parser. Empty if unsupported.
  virtual std::string keySequence(Key key) const = 0;

  // Global output-suppression switch (legacy: vt_mode('D')/'d'/-'d').
  // While disabled, no method on this interface may touch the actual
  // display (no stdout writes, no OS/terminal-library calls) --
  // implementations must guard every I/O-performing method on this,
  // not just some of them (the legacy C was inconsistent about this;
  // see terminal_display.cpp for where that's tightened up).
  virtual bool outputEnabled() const = 0;
  virtual void setOutputEnabled(bool enabled) = 0;

  // "Smooth scroll" (legacy: vt_mode('H')/'h'/-'h', toggled by the
  // ^Oh key and by the SCROLL setting in usilicrc). Changes how much
  // of the screen the scroll region covers; the exact row arithmetic
  // is an implementation detail hidden behind this setter instead of
  // being recomputed by every caller (legacy repeated `vt_cs(vt_row(0)
  // - (vt_mode('H') ? 1 : 2), 0)` at every one of its ~6 call sites).
  virtual bool smoothScrollEnabled() const = 0;
  virtual void setSmoothScrollEnabled(bool enabled) = 0;

  // Enter/leave the display (legacy: entering/leaving the terminal's
  // alternate screen via the ti/te capabilities). Note: legacy tc.c
  // captured te (exit_ca_mode) but never actually called it anywhere
  // -- usilic always left the alternate screen active on exit. close()
  // corrects that; see terminal_display.cpp.
  virtual void open() = 0;
  virtual void close() = 0;

  // -- smooth (drag/coast) scroll rendering --
  // Renders `lines` (the FULL content, not just what's visible) with
  // row `topIndex` positioned `pixelOffsetY` pixels below its normal
  // (offset==0) position, and `modeLineText` (see ContentView::
  // modeLineText()/scrollModeLineText()) in the fixed bottom row --
  // the rendering primitive behind the GUI drag-scroll feature (docs/
  // ideas/IDEAS.md's "cat's-paw" request). Moved onto this interface
  // (2026-07-31, previously GraphicalDisplay-only) so callers
  // (main.cpp's runGuiPhase()) go through the same Display abstraction
  // every other render call does, rather than reaching for a
  // GraphicalDisplay-specific method.
  //
  // `pixelOffsetY` is fundamentally pixel/dot-granular: positive
  // shifts everything DOWN (revealing earlier content, a downward
  // drag), negative shifts UP; must satisfy
  // -cellHeightPixels() < pixelOffsetY < cellHeightPixels() for an
  // implementation that has one (GraphicalDisplay). A real terminal
  // has no sub-row addressing at all -- the real-terminal drag-scroll
  // fallback (main.cpp's runCoastLoop()/runTerminalMouseDrag(), see
  // CLAUDE.md's 2026-07-30 notes) is row-granular by necessity and
  // drives ContentView::redraw() directly rather than through this
  // method, so nothing currently calls TerminalDisplay's own
  // implementation -- it's still a real, correct, row-granular
  // rendering (pixelOffsetY has no effect there beyond its sign) kept
  // independently usable rather than left as a stub, for whatever
  // future caller wants a Display-agnostic smooth-scroll render.
  virtual void renderScrolled(const std::vector<std::string> &lines, int topIndex, int pixelOffsetY,
                               const std::string &modeLineText) = 0;
};

}  // namespace fm
