#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "display.hpp"

namespace fm {

// SDL2/SDL2_ttf-backed Display: renders the exact same character-cell
// grid TerminalDisplay writes to a real terminal into a real GUI
// window instead, pixel by pixel. Ported from nothing in legacy tcb
// (there is no GUI mode there at all) -- this exists purely to back
// docs/ideas/IDEAS.md's "cat's-paw" hidden GUI mode request, confirmed
// with the user to build in scoped slices: THIS class is slice one
// (a real window showing the same content, nothing more) -- the
// inertial "cat's paw" scroll physics and the mouse right-click
// PROGRAM-list menu are explicitly NOT here yet (see Application's
// own class comment for exactly what ^Og/handleMouseClick() cover in
// this slice).
//
// This is NOT a literal port of anything, since nothing to port from
// exists -- but it deliberately reuses as much already-ported
// machinery as it can: fm::Utf8::sequence_length()/display_width()
// (already built for exactly this "a multi-byte character spans more
// than one byte but not necessarily more than one column" problem,
// see its own class comment) decode the same UTF-8 byte stream
// write()/putChar() receive elsewhere, since unlike TerminalDisplay
// (which just forwards raw bytes to a real terminal that decodes UTF-8
// itself), this class owns pixel rendering directly and has to decode
// UTF-8 itself to know which glyph to draw and how many cells wide it
// is.
//
// Deliberately kept ignorant of KeyInput/Application entirely (no
// #include, no dependency): keyEvents() returns this class's OWN
// small KeyEvent shape (a byte, or one of Display::Key's existing 6
// values -- already part of this class's own inherited interface, no
// new enum needed) rather than KeyInput::SpecialKey, so translating
// that into an actual Application::feedByte() call is app/main.cpp's
// job (which already depends on both KeyInput and Display) -- the
// same boundary this whole port draws everywhere else (Display never
// depends on KeyInput, KeyInput never depends on Application).
//
// Deliberately excluded (future work, not this slice):
//   - The inertial "cat's paw" drag-scroll physics and cursor-shape
//     change: this class only ever draws a static character grid: no
//     sub-cell/partial-row pixel scrolling, no mouse-drag tracking, no
//     custom cursor bitmap. A future class built on top of this one's
//     raw pixel access (not yet exposed) would add that.
//   - The mouse right-click PROGRAM-list context menu: right-clicks
//     are detected (rightButton on MouseClick) but Application has
//     nothing wired to do with one yet (see its own class comment).
//   - Live OS-level window resize (dragging the window's own resize
//     handle): setSize() exists and correctly resizes the window/grid
//     if called, but nothing calls it in response to an SDL_WINDOWEVENT
//     yet -- the window's size is fixed at whatever rows/columns it
//     was constructed with (inherited from the terminal session's own
//     size at the moment ^Og switches, matching "seamless" continuity)
//     for this slice.
//   - Copy/paste, IME composition, and anything else a real terminal
//     emulator eventually grows: out of scope for a first slice
//     focused on proving out the basic look-and-feel.
class GraphicalDisplay : public Display {
 public:
  // fontPath must be a real, readable TTF/TTC font file (a monospace
  // face -- this class does not verify that, a proportional font will
  // just render with misaligned columns). Resolving an actual system
  // font path is the caller's job (main.cpp), matching this port's
  // standing "no hidden global state" rule already applied to
  // Configuration/History/HELP text -- this class never searches the
  // filesystem on its own. fontPointSize defaults to 16 (this class's
  // own long-standing hardcoded value, kept as the default for the
  // ^Og-triggered manual GUI entry, which has no real terminal font
  // size to match); the "実はすでにGUI" launch-time entry (see
  // main.cpp's queryTerminalAppColors()) passes the calling terminal's
  // own queried size instead -- a mismatched size is what made the
  // GUI window visibly LARGER than the terminal it was replacing (see
  // CLAUDE.md's 2026-07-30 notes), since row/column count alone
  // doesn't determine a window's pixel size, cell size does too.
  // Returns nullptr (rather than throwing or exiting) if SDL2/
  // SDL2_ttf initialization, window/renderer creation, or font
  // loading fails, so a caller can fall back to staying in terminal
  // mode instead of crashing.
  static std::unique_ptr<GraphicalDisplay> create(int rows, int columns, const std::string &fontPath,
                                                    int fontPointSize = 16);

  ~GraphicalDisplay() override;

  struct Color {
    unsigned char r = 0, g = 0, b = 0;
  };
  // Overrides the default foreground/background colors (220,220,220
  // on black) -- added for the "launch already looking like the
  // calling terminal" feature (docs/ideas/IDEAS.md's "実はすでにGUI"
  // request, see CLAUDE.md's 2026-07-30 notes): when the real
  // terminal's own colors can be queried (currently only macOS
  // Terminal.app, via AppleScript -- see main.cpp), the caller applies
  // them here before the first render so the window matches instead
  // of using this class's own hardcoded defaults, rather than trying
  // to make GraphicalDisplay itself terminal-aware. Takes effect on
  // the next render (flush()/renderScrolled()); doesn't repaint
  // immediately by itself.
  void setColors(Color foreground, Color background);

  // Moves the window to an absolute SCREEN position (top-left-origin
  // pixel coordinates, matching both SDL2's own cross-platform
  // convention and what AppleScript's "bounds of front window"
  // reports for a real terminal app -- see main.cpp's
  // queryTerminalAppWindowBounds()). Added for the "実はすでにGUI"
  // launch feature (docs/ideas/IDEAS.md): overlaying the GUI window
  // exactly where the calling terminal's own window already sits, so
  // switching backends is invisible to the user rather than the
  // window jumping to wherever SDL_WINDOWPOS_CENTERED happens to put
  // it (a real user request, 2026-08-03, following up on the margin
  // fix with "テキスト表示ウィンドウに重ねる形でGUIウィンドウ版を
  // 表示したい"). A no-op if the window hasn't been created.
  void setWindowPosition(int x, int y);

  // `width, height` is in the SAME "outer frame, title bar included"
  // convention setWindowPosition() takes -- NOT SDL's own native
  // content-area convention; converted internally the same way. See
  // the .cpp's own comment for why (matching a real terminal window's
  // own measured pixel size exactly, for the "実はすでにGUI" overlay
  // launch), overriding this class's own cellWidth*columns+margins+
  // scrollbar-width formula. A no-op if the window hasn't been
  // created.
  void setWindowSize(int width, int height);

  // Like setWindowSize(), but ALSO recomputes cellWidth/cellHeight
  // (overriding create()'s own font-metric-based computation) so the
  // known rows/columns count exactly fills the given outer dimensions
  // -- see the .cpp's own comment for why (a real terminal's line
  // spacing can't be reliably derived from font metrics alone across
  // different fonts/sizes, but IS exactly recoverable arithmetically
  // once the real terminal's own outer pixel size is already known,
  // as it is for the "実はすでにGUI" overlay launch). A no-op if the
  // window hasn't been created.
  void matchWindowSize(int outerWidth, int outerHeight);

  // Hides/shows this window without destroying it (SDL_HideWindow()/
  // SDL_ShowWindow()) -- used by the PROGRAM-mode-in-the-launching-
  // terminal feature (main.cpp's runProgramInLaunchingTerminal()) to
  // get this window out of the way for the duration of a delegated
  // PROGRAM command (`less`, `vi`, ...), then bring it back once that
  // real terminal command finishes. A no-op if the window hasn't been
  // created.
  void setVisible(bool visible);

  // Brings this window to the front and gives it input focus
  // (SDL_RaiseWindow()) -- used by the same feature once the delegated
  // PROGRAM command finishes, so control visibly returns to this
  // window rather than leaving focus wherever the OS happens to leave
  // it after the real terminal was raised. A no-op if the window
  // hasn't been created.
  void raise();

  void write(const std::string &text) override;
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;
  void setVisibleCursor(int row, int col) override;
  void hideVisibleCursor() override;

  void clearScreen() override;
  void clearToEndOfLine() override;
  void scrollForward() override;
  void scrollReverse() override;

  void setAttribute(Attribute attr) override;
  std::string attributeEscape(Attribute attr) const override;

  int rows() const override;
  int columns() const override;
  void setSize(int rows, int columns) override;

  // Always returns "" -- a GraphicalDisplay is never the target of
  // KeyInput::readEscapeSequence() (real terminal-capability-sequence
  // matching only applies to a real tty byte stream); GUI-mode
  // keyboard input goes through keyEvents() instead (see class
  // comment), not this method at all.
  std::string keySequence(Key key) const override;

  bool outputEnabled() const override;
  void setOutputEnabled(bool enabled) override;

  // Stored/returned like TerminalDisplay's own (default false); when
  // enabled, scrollForward()/scrollReverse() do a real one-row shift
  // of the character grid rather than requiring a full redraw() --
  // implemented properly despite not being needed for THIS slice's
  // own static-content use, since it costs nothing extra once the
  // grid is already row-addressable.
  bool smoothScrollEnabled() const override;
  void setSmoothScrollEnabled(bool enabled) override;

  void open() override;
  void close() override;

  // No-ops: -g has no scroll-region concept at all (see Display::
  // leaveForExternalProgram()'s own comment) -- its own PROGRAM launch
  // path (runProgramInLaunchingTerminal()) uses an entirely separate
  // real terminal window this class never touches.
  void leaveForExternalProgram() override {}
  void reenterFromExternalProgram() override {}

  // -- GUI-specific, not part of Display's abstract interface --

  // Pumps pending SDL events for this window, queuing whatever mouse
  // clicks and key presses arrived (drained via consumeMouseClick()/
  // consumeKeyEvent()). Returns false once the window's own close
  // button was clicked (SDL_QUIT) -- a run loop's signal to fall back
  // to terminal mode, the same "flag a run loop polls" shape
  // Application::consumeGuiModeToggleRequest() already uses.
  bool pumpEvents();

  // Returns and clears whether pumpEvents() saw an SDL_WINDOWEVENT_
  // EXPOSED since the last call (2026-08-24, docs/CHANGES.md item 422,
  // a real user report against the new `make app` .app bundle: "アプリ
  // 起動時のイントロ動作だけで以後は何も起きない" -- a solid BLACK
  // window that stayed black forever, reproduced directly: `sample`
  // showed the run loop idling normally in pumpEvents()/usleep(), not
  // hung, and re-running the SAME binary as a bare `./usilic -g`
  // (not through the bundle) rendered correctly every time, while
  // launching the bundle via `open`/double-click was intermittent).
  // Root cause: runGuiPhase()'s own `anyEventThisFrame` optimization
  // (added to stop redraw() firing 60x/sec while idle, see its own
  // comment) skips redraw() entirely on any frame where nothing was
  // "consumed" -- but the very first frame's own initial redraw can
  // race against the window actually being mapped/composited by
  // WindowServer, a race this port's own first look never needed to
  // handle since a bare-executable launch (no proper .app bundle) has
  // a much simpler, faster LaunchServices bootstrap than a real
  // bundle's (RunningBoard registration, ViewBridge, etc. -- all
  // visible in a real `log show` capture of a stuck instance). If that
  // race is lost, NOTHING ever synthesizes a follow-up redraw, since
  // one was never needed before -- SDL_WINDOWEVENT_EXPOSED is
  // AppKit/WindowServer's own "this window needs (re)painting" signal
  // for exactly this situation, and nothing in pumpEvents() ever
  // looked at SDL_WINDOWEVENT at all before this fix.
  bool consumeNeedsRedraw();

  struct MouseClick {
    int row;
    int column;
    bool rightButton = false;  // captured now, unused until a future slice wires a context menu
  };
  // Returns and clears the next queued mouse click (pixel position
  // already translated into the same display row/column coordinate
  // space moveCursor() uses), or nullopt if none is pending.
  std::optional<MouseClick> consumeMouseClick();

  struct HoverEvent {
    int row;
    int column;
  };
  // Returns and clears the next queued plain mouse-motion sample (no
  // button held -- see pumpEvents()'s own SDL_MOUSEMOTION case), or
  // nullopt if none is pending. Added after a real user report
  // (2026-08-02, comparing side by side against the real-terminal
  // mouse-tracking path's own applyHoverRowAndRedraw()/motionOnly
  // handling in main.cpp): plain hover-follow (highlighting whatever
  // row the pointer rests over, no click needed) worked over a real
  // terminal's own SGR mouse reporting but was never wired for GUI
  // mode at all -- SDL_MOUSEMOTION events were previously queued (as
  // DragEvent::Motion) ONLY while the left button was held, so a bare
  // hover was silently dropped outright.
  std::optional<HoverEvent> consumeHoverEvent();

  struct KeyEvent {
    int byte = 0;                    // valid when special is nullopt
    std::optional<Key> special;      // one of Display::Key's own 6 values, when this was an arrow/page key
    // No modifier tracking: originally had a `shift` field (whether
    // Shift was held with Up/Down) for the keyboard-driven inertial-
    // scroll trigger, removed 2026-07-31 when that trigger moved to
    // plain 'w'/'s' bytes instead (the user's own preference -- a
    // single keystroke is easier to reach for repeatedly than holding
    // a modifier -- see CLAUDE.md's 2026-07-31 note), which need no
    // modifier state at all.
  };
  // Returns and clears the next queued key press, or nullopt if none
  // is pending. Ordinary characters (including multi-byte UTF-8 text
  // typed via an input method) arrive as one KeyEvent per byte with
  // `special` unset, matching how KeyInput::readByte() and
  // Application::feedByte() already expect to consume input one byte
  // at a time regardless of source.
  std::optional<KeyEvent> consumeKeyEvent();

  // -- drag-scroll PROTOTYPE support (2026-07-30, feasibility sample) --
  // Added specifically to answer "can dot-level inertial drag-scroll
  // be built at all" before committing further to the rest of GUI
  // mode -- see app/main.cpp's runDragScrollDemo() for the actual
  // drag/inertia/row-commit logic that USES these. Deliberately a
  // separate, self-contained rendering path from write()/putChar()/
  // moveCursor() (the normal Display interface ContentView drives):
  // real production integration (a proper "scroll the active
  // ContentView smoothly" API) is a decision for AFTER this sample is
  // evaluated, not assumed here.
  struct MouseMotion {
    int deltaY = 0;  // pixels moved (SDL's own relative motion) since the last queued event
  };
  // Returns and clears the next queued left-button-down/up transition
  // or motion sample. Motion is only queued while the button is held
  // (SDL_CaptureMouse-backed, so motion is tracked even if the cursor
  // leaves the window mid-drag).
  enum class DragEventType { ButtonDown, ButtonUp, Motion };
  struct DragEvent {
    DragEventType type;
    int deltaY = 0;  // valid for Motion
  };
  std::optional<DragEvent> consumeDragEvent();

  // Mouse scroll-wheel ticks, queued from SDL_MOUSEWHEEL -- see
  // app/main.cpp's own wheel-consumption comment for what this is
  // used for (a per-tick, gradually-decelerating alternative to the
  // w/s coast-level keys' own "opposite direction resets straight to
  // level 1" jump, added at the user's own 2026-08-05 request: "s→w
  // だと高速度からsだと減速じゃなくて逆方向へ最低速" -- reversing mid-
  // coast with w/s is an abrupt floor-speed jump in the new direction,
  // not a deceleration). `deltaY` is SDL's own wheel.y, sign-corrected
  // for SDL_MOUSEWHEEL_FLIPPED so its sign always tracks the same
  // FIXED physical gesture direction regardless of the OS-level
  // "natural scrolling" setting (SDL's own documented meaning of the
  // correction) -- this struct does NOT decide which physical
  // direction means "reveal earlier" vs "reveal later" content; that
  // app-semantic mapping is main.cpp's own call (its wheel-consumption
  // loop), and was flipped there 2026-08-06 after real-hardware
  // testing showed the naive mapping was backwards for this project's
  // own natural-scrolling-enabled test machine -- see that comment,
  // not this one, for the current sign convention.
  struct WheelEvent {
    int deltaY = 0;
  };
  std::optional<WheelEvent> consumeWheelEvent();

  // A left-button press/drag/release that STARTED inside the
  // scrollbar strip reserved on the right edge (see drawScrollbar()'s
  // own comment) -- queued instead of MouseClick/DragEvent for
  // exactly that press, so dragging the thumb doesn't ALSO trigger
  // content selection or content drag-scrolling. Added 2026-08-07 at
  // the user's own request: now that the scrollbar is actually
  // visible, it needs real functionality (jump to the dragged-to
  // position), not just be a passive indicator. `y` is the absolute
  // window-coordinate pixel position (not relative deltaY like
  // DragEvent's own Motion) -- main.cpp maps it directly to a target
  // topIndex via marginYPixels()/cellHeightPixels(), since dragging a
  // scrollbar thumb is an ABSOLUTE "go here" gesture, not a relative
  // "move by this much" one.
  enum class ScrollbarEventType { ButtonDown, Motion, ButtonUp };
  struct ScrollbarEvent {
    ScrollbarEventType type;
    int y = 0;
  };
  std::optional<ScrollbarEvent> consumeScrollbarEvent();

  // Caches the current scroll position so flush() (the static, non-
  // scrolling render path) can draw a REAL, always-current thumb via
  // drawScrollbar() instead of a decorative placeholder -- an earlier
  // version pasted a static embedded screenshot here instead
  // (scrollbar_idle_image.hpp, since removed) specifically BECAUSE
  // flush() has no scroll-position parameters of its own (Display's
  // generic interface, shared with TerminalDisplay, has no such
  // concept) and threading real ContentView topIndex/totalLines
  // through it directly would need a Display-interface change (see
  // this method's own git history for that deferred alternative).
  // This sidesteps that: a real report (2026-08-09, "マウス操作ON/OFF
  // のたびに矩形つまみと張りぼて画像を切り替えるのは無意味な気がする。
  // つまみが消えてしまうので継続操作もできないし") found the swap
  // itself actively harmful -- the decorative image doesn't track
  // position, so it was impossible to see (and grab) where the thumb
  // currently was between drags. Call every runGuiPhase() iteration
  // (main.cpp), the same "call unconditionally, cheap" pattern
  // syncScrollbar() used before it was reverted -- BEFORE any redraw()
  // that might run that same iteration, so flush() always sees fresh
  // values when it does.
  void updateScrollPosition(int topIndex, int totalLines, int contentRows);

  // The fixed top-edge margin in pixels (see Impl::kMarginY's own
  // comment) -- exposed alongside the already-public cellHeightPixels()
  // so main.cpp's ScrollbarEvent handling can convert an absolute
  // window Y back into "how far down the content area" without
  // needing to know GraphicalDisplay's own Impl internals.
  int marginYPixels() const;

  // See Display::renderScrolled()'s own comment for the general
  // contract (this is that method's real, pixel-granular
  // implementation). The caller is responsible for committing a real
  // topIndex change and resetting pixelOffsetY once a full row's
  // worth has accumulated (see main.cpp's commitRowCrossings()).
  // `modeLineSegments` is drawn fresh every call into the fixed
  // bottom row -- each segment its own reverse-video run (or, for a
  // non-standout gap segment, left as plain background), matching
  // ContentView::renderModeLine()'s own per-field look rather than
  // one uninterrupted highlight bar (see Display::ModeLineSegment's
  // own comment for the real bug this fixes). Content itself never
  // scrolls into that row (see this method's own comment in the .cpp
  // for why it used to).
  void renderScrolled(const std::vector<Display::ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                       const std::vector<Display::ModeLineSegment> &modeLineSegments) override;

  int cellHeightPixels() const;

  // Swaps the window's cursor between the default arrow and the REAL
  // macOS open-hand cursor's pixels (extracted at runtime from
  // NSCursor.openHand via mac_cursor.hpp, then driven through SDL's
  // own SDL_SetCursor() like an ordinary SDL_Cursor -- see the .cpp's
  // own comment for why a real system cursor's pixels, not a
  // hand-drawn SDL_CreateColorCursor bitmap, and not a raw
  // [NSCursor set] call either, after two rejected hand-drawn attempts
  // and one cursor-reverts-mid-drag bug) while dragging CONTENT (the
  // "cat's paw" scroll gesture) -- NOT SDL's system SDL_SYSTEM_CURSOR_HAND
  // (a pointing index finger), which this method used before 2026-08-10
  // and which setPointingCursor() below now covers instead.
  void setHandCursor(bool active);

  // SDL's system pointing-hand (index finger) cursor -- reserved for
  // gestures more like operating a control than grabbing content: see
  // its own .cpp comment for the scrollbar-drag call site and the
  // (not yet wired) future hover-over-selectable-content one.
  void setPointingCursor(bool active);

 private:
  GraphicalDisplay();

  struct Impl;
  std::unique_ptr<Impl> impl_;

  void putByte(unsigned char c);
  void commitGlyph(const std::string &utf8Bytes, int width);
  void renderCellBackground(int row, int col);
  void renderCell(int row, int col);
  void renderCursor();
  // `startCol` (added 2026-08-07 for the mode-line segment rewrite --
  // see renderScrolled()'s own mode-line block) shifts where drawing
  // begins; every existing caller keeps passing 0 (a whole content
  // row always starts at the left edge) via the default.
  void renderTextRun(int y, const std::string &line, Color fg, int startCol = 0,
                      const std::vector<Display::AttributeRun> &attributes = {});
  // Sums the on-screen column width of `text` the same way
  // renderTextRun() advances `x` internally (multi-byte UTF-8 aware,
  // via the same fm::Utf8 helpers) -- used by the mode-line segment
  // loop to know how many columns each segment's own highlight rect
  // should span and where the NEXT segment's startCol begins, without
  // duplicating renderTextRun()'s own glyph-walk a second time.
  int textRunDisplayWidth(const std::string &text) const;
  // See renderScrolled()'s own call site comment. `effectiveTop` is a
  // continuous (fractional) topIndex -- topIndex adjusted by
  // pixelOffsetY/cellHeight -- so the thumb moves smoothly in sync
  // with sub-row scroll position, not just snapping once per full row.
  void drawScrollbar(double effectiveTop, int totalLines, int contentRows);
  // Composites the three embedded real -c-terminal scrollbar slices
  // (scrollbar_idle_image.hpp's own class comment -- top cap, a
  // stretchable plain middle, bottom cap) into the reserved strip so
  // the decoration always exactly fills the CURRENT profile's own
  // content height, no overflow/gap tradeoff -- called by flush() only
  // until the first real mouse interaction (see Impl::everInteracted's
  // own comment for why, and for what replaces this afterward).
  //
  // Added 2026-08-10, replacing an EARLIER single-image version (which
  // pasted one full screenshot at its own captured profile's native
  // pixel size, unscaled, deliberately overflowing/leaving a gap on
  // other profiles -- a real 2026-08-09 request had explicitly asked
  // for exactly that native-size, no-clamping tradeoff after a
  // uniform whole-image stretch/clamp attempt was tried and rejected
  // for distorting the thumb and rounded track ends). This 3-slice
  // version resolves the SAME problem a different way -- ordinary
  // 9-slice/3-slice UI scaling -- by only ever stretching the ONE
  // slice with no detail worth preserving (the plain middle track),
  // while the two detailed end slices still draw undistorted at their
  // own native size, so there is no longer any overflow/gap tradeoff
  // to accept at all.
  void drawIdleScrollbarImage();
};

}  // namespace fm
