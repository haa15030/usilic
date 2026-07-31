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

  void write(const std::string &text) override;
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;

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

  // -- GUI-specific, not part of Display's abstract interface --

  // Pumps pending SDL events for this window, queuing whatever mouse
  // clicks and key presses arrived (drained via consumeMouseClick()/
  // consumeKeyEvent()). Returns false once the window's own close
  // button was clicked (SDL_QUIT) -- a run loop's signal to fall back
  // to terminal mode, the same "flag a run loop polls" shape
  // Application::consumeGuiModeToggleRequest() already uses.
  bool pumpEvents();

  struct MouseClick {
    int row;
    int column;
    bool rightButton = false;  // captured now, unused until a future slice wires a context menu
  };
  // Returns and clears the next queued mouse click (pixel position
  // already translated into the same display row/column coordinate
  // space moveCursor() uses), or nullopt if none is pending.
  std::optional<MouseClick> consumeMouseClick();

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

  // See Display::renderScrolled()'s own comment for the general
  // contract (this is that method's real, pixel-granular
  // implementation). The caller is responsible for committing a real
  // topIndex change and resetting pixelOffsetY once a full row's
  // worth has accumulated (see main.cpp's commitRowCrossings()).
  // `modeLineText` is drawn fresh every call into the fixed bottom
  // row, in reverse video -- content itself never scrolls into that
  // row (see this method's own comment in the .cpp for why it used
  // to).
  void renderScrolled(const std::vector<std::string> &lines, int topIndex, int pixelOffsetY,
                       const std::string &modeLineText) override;

  int cellHeightPixels() const;

  // Swaps the window's cursor between the default arrow and a stand-
  // in "hand" shape (SDL_SYSTEM_CURSOR_HAND) while dragging -- a real
  // custom cat's-paw bitmap (SDL_CreateColorCursor(), already
  // technically verified feasible in an earlier design discussion, see
  // IDEAS.md) is future work once this sample's basic feel is
  // validated.
  void setHandCursor(bool active);

 private:
  GraphicalDisplay();

  struct Impl;
  std::unique_ptr<Impl> impl_;

  void putByte(unsigned char c);
  void commitGlyph(const std::string &utf8Bytes, int width);
  void renderCellBackground(int row, int col);
  void renderCell(int row, int col);
  void renderTextRun(int y, const std::string &line, Color fg);
};

}  // namespace fm
