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

  // Ported from legacy vt.c's own vt_init(-2)/vt_init(2) cases -- a
  // real user question (2026-08-20, docs/HISTORY.md:9023, raised
  // right after diagnosing this session's own SIGTERM/raw-mode work):
  // "-cでも起きるんだが、^Jp'vi CLAUDE.md'[return]は問題無い。less
  // とviの違いって何？" (why does the FIRST `less` launched from
  // PROGRAM mode render broken while `vi` never does, and why does
  // even a LATER `less` work fine once `vi` has run once). Root-
  // caused by reading legacy's own vt_init() source directly: leaving
  // raw mode for an external program (`case -2`) ALSO resets the
  // scroll region to the FULL screen via `vt_cs(vt_row(0) - 1, 0)`,
  // and re-entering (`case 2`) re-constrains it back to
  // `vt_row(0) - 2` (reserving the last row for usilic's own mode
  // line) via the same `vt_cs()` call -- this port's own KeyInput::
  // leaveRawMode()/reenterRawMode() (ProgramMode::run()'s own
  // pairing) only ported the termios half of vt_init(-2)/(2),
  // dropping the scroll-region half entirely. TerminalDisplay's own
  // applyScrollRegion() (see its own comment) constrains the scroll
  // region to rows 0..rows-2 at startup and never resets it before
  // handing off to an external program -- so `less` inherits that
  // same constrained region, unable to use the terminal's own last
  // row the way it normally would. `vi`/`vim` is well known for
  // unconditionally resetting the scroll region itself as part of its
  // own aggressive terminal (re)initialization on startup (via
  // terminfo, regardless of what it inherited) -- explaining both
  // halves of the report: `vi` always self-corrects regardless of
  // this gap, and once it has run once, the terminal's own (kernel/
  // terminal-emulator-level, not process-level) scroll-region state
  // stays reset to full-screen for whatever runs next, including a
  // LATER `less` -- until usilic's own next redraw re-establishes its
  // own constrained region via reenterFromExternalProgram() below,
  // completing the cycle. GraphicalDisplay has no scroll-region
  // concept at all (see its own no-op override) -- matches "-g起動
  // では最初からOK" (a real corroborating detail in the original
  // report) exactly, since -g's own PROGRAM launch path
  // (runProgramInLaunchingTerminal()) uses an entirely separate real
  // terminal window this port's own Display never touches.
  virtual void leaveForExternalProgram() = 0;
  virtual void reenterFromExternalProgram() = 0;

  // One piece of the mode-line row -- either a highlighted (reverse-
  // video/"standout") field (a filename, the position indicator, the
  // trailing path) or a plain, unhighlighted single-space gap between
  // two fields. See ContentView::scrollModeLineSegments()'s own
  // comment for why this exists as a SEQUENCE rather than one flat
  // string: a real screenshot comparison (2026-08-07) found
  // renderScrolled()'s mode-line drawn as one uninterrupted highlight
  // bar, while the normal (non-scrolling) ContentView::renderModeLine()
  // path correctly shows small black gaps between fields, matching
  // legacy's own disp_window() (separate vt_stand()/vt_endstand()
  // calls per field, not one call around the whole composed string).
  struct ModeLineSegment {
    std::string text;
    bool standout = true;
  };

  // Relocated here 2026-08-21 (was a free-standing fm::AttributeRun in
  // pipe_buffer.hpp) so renderScrolled() below can carry bold/underline
  // information through ScrolledLine without pipe_buffer.hpp's own
  // dependency on THIS header becoming circular (pipe_buffer.hpp
  // already includes display.hpp for Attribute; display.hpp can't
  // include it back). A direct user report ("マウスの左クリック
  // （画面ドラッグスタンバイ状態）中だと文字装飾が利かず" -- HELP's
  // own bold/underlined words render plain during an active drag,
  // confirmed by renderTextRun()'s own pre-existing comment: "No
  // per-glyph bold info reaches this function... always NORMAL")
  // finally gave this gap a concrete reason to close. `fm::AttributeRun`
  // (the namespace-scope alias just below this class) keeps every
  // existing caller (ContentView::Line::attributes, PipeBuffer's own
  // StyledLine, CutMode/GrepMode's own overstrike-attribute plumbing)
  // compiling unchanged.
  struct AttributeRun {
    int start;  // byte offset into the owning line's own text
    int end;
    Attribute attribute;
  };

  // A single renderScrolled() line: `text` plus the same bold/
  // underline byte-range runs ContentView::Line::attributes/
  // PipeBuffer::StyledLine::attributes already carry everywhere else
  // -- see AttributeRun's own comment for why this lives here instead
  // of reusing StyledLine itself (this header can't depend on
  // pipe_buffer.hpp).
  struct ScrolledLine {
    std::string text;
    std::vector<AttributeRun> attributes;
  };

  // -- smooth (drag/coast) scroll rendering --
  // Renders `lines` (the FULL content, not just what's visible) with
  // row `topIndex` positioned `pixelOffsetY` pixels below its normal
  // (offset==0) position, and `modeLineSegments` (see ContentView::
  // scrollModeLineSegments()) in the fixed bottom row -- the rendering
  // primitive behind the GUI drag-scroll feature (docs/ideas/IDEAS.md's
  // "cat's-paw" request). Moved onto this interface (2026-07-31,
  // previously GraphicalDisplay-only) so callers (main.cpp's
  // runGuiPhase()) go through the same Display abstraction every other
  // render call does, rather than reaching for a GraphicalDisplay-
  // specific method.
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
  virtual void renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                               const std::vector<ModeLineSegment> &modeLineSegments) = 0;
};

// See Display::AttributeRun's own comment: kept as a bare fm::AttributeRun
// alias so every existing caller (ContentView::Line::attributes,
// PipeBuffer::StyledLine::attributes, CutMode/GrepMode's own overstrike
// plumbing) keeps compiling unchanged after AttributeRun's own
// definition moved here from pipe_buffer.hpp.
using AttributeRun = Display::AttributeRun;

}  // namespace fm
