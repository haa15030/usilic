#pragma once

#include <memory>
#include <optional>
#include <string>

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
  // filesystem on its own. Returns nullptr (rather than throwing or
  // exiting) if SDL2/SDL2_ttf initialization, window/renderer
  // creation, or font loading fails, so a caller can fall back to
  // staying in terminal mode instead of crashing.
  static std::unique_ptr<GraphicalDisplay> create(int rows, int columns, const std::string &fontPath);

  ~GraphicalDisplay() override;

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
  };
  // Returns and clears the next queued key press, or nullopt if none
  // is pending. Ordinary characters (including multi-byte UTF-8 text
  // typed via an input method) arrive as one KeyEvent per byte with
  // `special` unset, matching how KeyInput::readByte() and
  // Application::feedByte() already expect to consume input one byte
  // at a time regardless of source.
  std::optional<KeyEvent> consumeKeyEvent();

 private:
  GraphicalDisplay();

  struct Impl;
  std::unique_ptr<Impl> impl_;

  void putByte(unsigned char c);
  void commitGlyph(const std::string &utf8Bytes, int width);
  void renderCell(int row, int col);
};

}  // namespace fm
