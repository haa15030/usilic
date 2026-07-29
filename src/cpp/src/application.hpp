#pragma once

#include <optional>
#include <string>

#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "directory_mode.hpp"
#include "display.hpp"
#include "file_mode.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "mode_stack.hpp"

namespace fm {

// Application-level command, already resolved from a raw keypress --
// what get_m()'s giant switch(m) case label meant, without any of the
// byte/escape-sequence recognition that produced it (KeyInput's job)
// or any of the ~50 case labels this port hasn't reached yet.
enum class Command { None, MoveUp, MoveDown, PageUp, PageDown, Enter, Back };

// Which *Mode is currently on screen. Ported from legacy's np->M (a
// T_* mode constant); scoped down to exactly the two modes this slice
// knows about.
enum class ApplicationMode { Directory, File };

// The top-level controller: owns the ModeStack and every *Mode
// instance, maps keypresses to Command, and dispatches each Command
// to whichever *Mode is current. Ported from menu.c's get_m() (the
// keystroke-to-command switch) + m_cr() (the "Enter was pressed, do
// the right thing" routing) + m_bl() (^G) + m_row()/m_page() (cursor
// movement) + menu()/init_menu() (the outer run loop and startup) --
// together the ~3300-line mode.c/menu.c "Mode" layer this whole
// project has been building toward one class at a time. This is the
// first slice of it, see "Deliberately excluded" below for what's
// intentionally not here yet.
//
// This is NOT a literal port. Legacy's get_m()/m_cr()/m_bl() are one
// intertwined state machine over a dozen shared mutable structs
// (HEAP/ARRAY/FLAG/NUM/LINE/RV/PN/CHILD, see CLAUDE.md and every
// already-ported *Mode class's own header for why this project keeps
// choosing not to carry that forward) -- here the equivalent state is
// just currentMode_ (an enum) plus the two owned ContentViews' own
// cursor state and DirectoryMode's/FileMode's own already-encapsulated
// state. In particular:
//   - Legacy's single shared PP buffer (the 12-slot hp->base[] pool
//     DirectoryMode's own header flagged as an unsolved gap) is
//     resolved here by giving Directory and File modes each their own
//     ContentView, both wrapping the same Display. Switching modes is
//     just "stop calling redraw() on one view, start calling it on the
//     other" -- the view not currently shown keeps its lines/cursor
//     state exactly as it was, in memory, with nothing to save or
//     restore. This is why returning from File to Directory doesn't
//     need ModeStack at all (see goBack()): DirectoryMode's own state
//     never left.
//   - ModeStack is still used, but only for what DirectoryMode itself
//     already needed it for (directory-to-directory descent, ported
//     in DirectoryMode's own first slice) -- Application doesn't push
//     anything onto it for the Directory<->File transition; a plain
//     two-state toggle is sufficient because File mode never itself
//     recurses into anything else in this slice (no stack depth to
//     track beyond "which directory was active").
//
// Deliberately excluded (future work, not this slice):
//   - Every mode besides Directory/File: GREP, CUT, MAN, PROGRAM,
//     LOCATE, archive browsing, SHELL entry, DU, CP/MV/RM execution,
//     history/list browsers, help/error/log screens -- each already
//     has its own *Mode class built; wiring each one in is exactly
//     the same shape of change (extend Command, extend
//     resolveCommand(), extend handleCommand()) repeated per mode,
//     deferred to keep this slice reviewable.
//   - Exiting the program (M_exit/T_exit): m_exit()'s superuser-safety
//     confirmation dialog and screen-stack unwind isn't ported yet;
//     there is intentionally no Command::Quit in this slice. A caller
//     driving Application in a real run loop currently has to stop
//     the loop by its own means (e.g. an external signal), same as
//     this class provides no run()/main loop of its own yet either --
//     see below.
//   - A real terminal run loop (legacy's menu()): reading bytes from
//     a live KeyInput, translating escape sequences, and looping
//     until exit needs the not-yet-ported M_exit flow to have
//     somewhere to break out to, so it's deliberately not built here
//     either. handleCommand() is the tested, terminal-independent
//     primitive such a loop would call.
//   - Selection-mark-driven CP/MV/RM/ARC dispatch (M_cp/M_mv/M_rm/
//     M_arc, m_arc()/m_dest()/m_yn()): DirectoryMode's marking and
//     CpMvRmMode both already exist and are usable directly by a
//     caller today; wiring them into Command/handleCommand() is next-
//     slice work, not this one.
//   - Selection marking's own keys (^Space etc) and file-jump (M_jump):
//     same reason, not part of the five commands this slice maps.
class Application {
 public:
  Application(Display &display, History &history, const Configuration &configuration,
              Charset::Encoding encoding, int width);

  // Lists `directory` (DirectoryMode::enterDirectory()) and makes it
  // the active mode. Ported from init_menu()'s initial directory
  // load. False if the initial `ls` fails.
  bool start(const std::string &directory);

  // Resolves a keypress to a Command. Ported from the movement/Enter/
  // ^G slice of get_m()'s switch(m) -- see the table in the .cpp for
  // exact byte/SpecialKey mappings and their legacy provenance.
  // `special` is whatever KeyInput::readEscapeSequence() returned
  // after an ESC byte, or nullopt for an ordinary byte.
  static Command resolveCommand(int byte, std::optional<KeyInput::SpecialKey> special);

  // Executes command against whichever *Mode is current. Returns
  // false when the command was a no-op given the current state (e.g.
  // Back with nothing to go back to, Enter on an empty listing) --
  // mirrors get_m()'s handlers, which likewise silently no-op rather
  // than erroring on such cases.
  bool handleCommand(Command command);

  ApplicationMode currentMode() const;
  DirectoryMode &directoryMode();
  FileMode &fileMode();

 private:
  bool openSelectedEntry();
  bool returnToDirectory();

  ModeStack modeStack_;
  ContentView directoryView_;
  ContentView fileView_;
  DirectoryMode directoryMode_;
  FileMode fileMode_;
  ApplicationMode currentMode_ = ApplicationMode::Directory;
};

}  // namespace fm
