#pragma once

#include <optional>
#include <string>

#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "directory_mode.hpp"
#include "display.hpp"
#include "file_mode.hpp"
#include "grep_mode.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "line_editor.hpp"
#include "mode_stack.hpp"

namespace fm {

// Application-level command, already resolved from a raw keypress --
// what get_m()'s giant switch(m) case label meant, without any of the
// byte/escape-sequence recognition that produced it (KeyInput's job)
// or any of the ~50 case labels this port hasn't reached yet.
enum class Command { None, MoveUp, MoveDown, PageUp, PageDown, Enter, Back, StartFgrep, StartEgrep };

// Which *Mode is currently on screen. Ported from legacy's np->M (a
// T_* mode constant); scoped down to exactly the modes this slice
// knows about.
enum class ApplicationMode { Directory, File, Grep };

// The top-level controller: owns the ModeStack and every *Mode
// instance, maps keypresses to Command, and dispatches each Command
// to whichever *Mode is current. Ported from menu.c's get_m() (the
// keystroke-to-command switch) + m_cr() (the "Enter was pressed, do
// the right thing" routing) + m_bl() (^G) + m_row()/m_page() (cursor
// movement) + m_grep() (^S/^J^S entering search) + menu()/init_menu()
// (the outer run loop and startup) -- together the ~3300-line
// mode.c/menu.c "Mode" layer this whole project has been building
// toward one class at a time. This is an early slice of it (Directory
// + File navigation, plus FGREP/EGREP search), see "Deliberately
// excluded" below for what's intentionally not here yet.
//
// This is NOT a literal port. Legacy's get_m()/m_cr()/m_bl()/m_grep()
// are one intertwined state machine over a dozen shared mutable
// structs (HEAP/ARRAY/FLAG/NUM/LINE/RV/PN/CHILD, see CLAUDE.md and
// every already-ported *Mode class's own header for why this project
// keeps choosing not to carry that forward) -- here the equivalent
// state is just currentMode_ (an enum), a couple of small prompt/
// leader-key bookkeeping fields, and each owned ContentView's own
// already-encapsulated cursor state. In particular:
//   - Legacy's single shared PP buffer (the 12-slot hp->base[] pool
//     DirectoryMode's own header flagged as an unsolved gap) is
//     resolved here by giving Directory, File, and Grep modes each
//     their own ContentView, all wrapping the same Display. Switching
//     modes is just "stop calling redraw() on one view, start calling
//     it on another" -- a view not currently shown keeps its lines/
//     cursor state exactly as it was, in memory, with nothing to save
//     or restore. This is why returning from File/Grep to whatever
//     was active before doesn't need ModeStack at all (see
//     handleCommand()'s Back case): the prior mode's own state never
//     left.
//   - ModeStack is still used, but only for what DirectoryMode itself
//     already needed it for (directory-to-directory descent, ported
//     in DirectoryMode's own first slice) -- Application doesn't push
//     anything onto it for Directory<->File<->Grep transitions; a
//     plain "which mode did we start this excursion from" field is
//     sufficient because neither File nor Grep mode recurses into
//     anything else in this slice.
//   - Legacy's `^J` is a genuine two-byte leader key (^Jw, ^Jc, ^Jl,
//     ^Jm, ^J^S, ...), not a synonym for Enter/^M -- a subtlety this
//     class's very first slice got wrong (an earlier revision folded
//     0x0a into the Enter case for convenience; fixed once EGREP's
//     ^J^S binding made the conflict concrete). resolveCommand()
//     stays a pure single-byte function; the stateful leader-byte
//     accumulation across two feedByte() calls lives in feedByte()
//     itself, the one piece of this class that has to remember
//     anything between keystrokes besides which mode is active.
//   - Legacy's search-pattern text entry (read_m()'s klen/prompt
//     machinery once np->M == M_fgrep/M_egrep) is represented here by
//     a small explicit prompt state (promptKind_) plus an owned
//     LineEditor -- feedByte() routes raw bytes to the editor instead
//     of resolveCommand() while a prompt is active, submitting on ^M
//     and cancelling on ^G, mirroring get_m()'s own read_m()-vs-
//     dispatch split without needing read_m()'s hand-rolled klen/cname
//     table (LineEditor already owns text editing; Application only
//     decides when input goes there instead of through Command
//     dispatch).
//
// Deliberately excluded (future work, not this slice):
//   - Every mode besides Directory/File/Grep: CUT, MAN, PROGRAM,
//     LOCATE, archive browsing, SHELL entry, DU, CP/MV/RM execution,
//     history/list browsers, help/error/log screens -- each already
//     has its own *Mode class built; wiring each one in is the same
//     shape of change (extend Command/ApplicationMode, extend
//     feedByte()'s leader table, extend handleCommand()) repeated per
//     mode, deferred to keep each slice reviewable.
//   - GREP's own further actions: opening/jumping to a matched line's
//     original context, nested search (searching within results),
//     highlighting the matched substring, and recording the pattern
//     into History's hfgrep/hegrep rings -- all explicitly out of
//     GrepMode's own scope too (see its class comment); Enter on a
//     grep result is a no-op in this slice.
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
//     either. feedByte()/handleCommand() are the tested, terminal-
//     independent primitives such a loop would call.
//   - Selection-mark-driven CP/MV/RM/ARC dispatch (M_cp/M_mv/M_rm/
//     M_arc, m_arc()/m_dest()/m_yn()): DirectoryMode's marking and
//     CpMvRmMode both already exist and are usable directly by a
//     caller today; wiring them into Command/handleCommand() is
//     future-slice work, not this one.
//   - Selection marking's own keys (^Space etc) and file-jump (M_jump):
//     same reason, not part of the commands this slice maps.
class Application {
 public:
  Application(Display &display, History &history, const Configuration &configuration,
              Charset::Encoding encoding, int width);

  // Lists `directory` (DirectoryMode::enterDirectory()) and makes it
  // the active mode. Ported from init_menu()'s initial directory
  // load. False if the initial `ls` fails.
  bool start(const std::string &directory);

  // Resolves a single, non-leader-prefixed keypress to a Command.
  // Ported from the movement/Enter/^G/^S slice of get_m()'s
  // switch(m) -- see the table in the .cpp for exact byte/SpecialKey
  // mappings and their legacy provenance. `special` is whatever
  // KeyInput::readEscapeSequence() returned after an ESC byte, or
  // nullopt for an ordinary byte. Does NOT recognize ^J-leader
  // sequences (e.g. ^J^S for EGREP) -- those need state across two
  // calls, which this pure function deliberately can't hold; use
  // feedByte() for real key-by-key input, which handles both.
  static Command resolveCommand(int byte, std::optional<KeyInput::SpecialKey> special);

  // Executes command against whichever *Mode is current. Returns
  // false when the command was a no-op given the current state (e.g.
  // Back with nothing to go back to, Enter on an empty listing) --
  // mirrors get_m()'s handlers, which likewise silently no-op rather
  // than erroring on such cases.
  bool handleCommand(Command command);

  // The real entry point for driving this class byte-by-byte (what a
  // KeyInput-backed run loop would call): handles ^J-leader
  // accumulation and prompt text entry, falling through to
  // resolveCommand()+handleCommand() for everything else. Returns
  // false only when the resulting action (if any) was itself a
  // no-op; a byte consumed into leader/prompt state with the action
  // still pending returns true.
  bool feedByte(int byte, std::optional<KeyInput::SpecialKey> special);

  bool promptActive() const;
  const std::string &promptText() const;

  ApplicationMode currentMode() const;
  DirectoryMode &directoryMode();
  FileMode &fileMode();
  GrepMode &grepMode();

 private:
  bool openSelectedEntry();
  bool returnToDirectory();
  bool returnFromGrep();
  ContentView &activeView();

  bool startGrepPrompt(GrepSyntax syntax);
  bool feedPromptByte(int byte);
  bool submitPrompt();
  void cancelPrompt();
  bool executeGrep(const std::string &pattern);

  ModeStack modeStack_;
  ContentView directoryView_;
  ContentView fileView_;
  ContentView grepView_;
  LineEditor promptEditor_;
  DirectoryMode directoryMode_;
  FileMode fileMode_;
  GrepMode grepMode_;

  ApplicationMode currentMode_ = ApplicationMode::Directory;
  ApplicationMode grepReturnMode_ = ApplicationMode::Directory;
  GrepSyntax grepSyntax_ = GrepSyntax::Fixed;

  bool leaderJPending_ = false;  // saw ^J, waiting for the byte that completes a two-byte command

  enum class PromptKind { None, GrepPattern };
  PromptKind promptKind_ = PromptKind::None;
};

}  // namespace fm
