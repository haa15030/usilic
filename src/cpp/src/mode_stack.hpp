#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace fm {

// A saved navigation frame: everything needed to return to a screen
// the user descended away from (a directory listing, an archive
// entry, a CUT/man/prog/list excursion, or a nested grep-results
// search). Ported from legacy's STACK and GREP structs (tcb.h),
// unified into one payload type -- see ModeStack's class comment for
// why.
//
// Field-by-field mapping to legacy (tcb.h's STACK/LINE/CUT/GREP):
//   mode              <- STACK.f.M / GREP.f_M (an opaque mode
//                         identifier; this class assigns it no
//                         meaning of its own -- interpreting it is
//                         Mode/Application's job)
//   previousMarker    <- LINE::prev (the mode-line breadcrumb one
//                         level further back; ModeStack::kFloorMarker
//                         is legacy's "nothing above this" sentinel,
//                         see pushFloor())
//   name              <- STACK::name / GREP::name (current item's
//                         path/label)
//   directory         <- LINE::dir
//   previousDirectory <- LINE::old
//   modeLineArg       <- LINE::arg
//   cutName           <- CUT::name
//   cutSearchText     <- CUT::str
//   cutBuffer         <- CUT::buff (an opaque, type-erased handle --
//                         see the class comment on buffer ownership)
//   cutProgram        <- CUT::chp, but resolved to a plain program
//                         name string rather than a shared pointer,
//                         matching History::SessionState's identical
//                         simplification for this same legacy field
//                         (resolving it back through Configuration is
//                         the caller's job either way)
//   view              <- STACK::bp / GREP::bp (an opaque, type-erased
//                         handle to whichever buffer/view was active)
//   searchPattern     <- GREP::str, the ONE field legacy's GREP had
//                         that STACK didn't -- nullopt for ordinary
//                         navigation frames, set only when this frame
//                         represents "you were viewing grep results
//                         for this pattern"
struct ModeStackFrame {
  int mode = 0;
  std::string previousMarker;
  std::string name;
  std::string directory;
  std::string previousDirectory;
  std::string modeLineArg;
  std::string cutName;
  std::string cutSearchText;
  std::shared_ptr<void> cutBuffer;
  std::string cutProgram;
  std::shared_ptr<void> view;
  std::optional<std::string> searchPattern;
};

// A LIFO stack of navigation frames -- "what to return to" when the
// user backs out of wherever they descended into (a subdirectory, an
// archive entry, a CUT/man/prog/list excursion, nested grep results).
// Ported from sub.c's save_stack()/load_stack()/save_stack_dir()/
// read_stack() (the STACK mechanism) and save_grep()/load_grep()
// (the GREP mechanism).
//
// This is NOT a literal port. The two biggest changes:
//   - Legacy has TWO parallel LIFO chains (STACK and GREP), with a
//     hand-rolled "steal a STACK frame's contents to seed a new GREP
//     node, then push a copy back onto STACK" dance at the moment
//     grep is entered (mode.c's mode(), case M_egrep/M_fgrep) to keep
//     them in sync. Reading both structs side by side (tcb.h) shows
//     GREP's payload is exactly STACK's payload plus one extra field
//     (the search pattern) -- confirmed independently during this
//     class's design, and matching what this project's own earlier
//     CRC-card design session had already concluded (CLAUDE.md
//     history item 18: "GREPは実質STACKの特殊ケース"). Unified here
//     into one stack whose frame type has an optional search-pattern
//     field, eliminating the second chain and the frame-stealing
//     trick entirely -- callers just push a frame with
//     searchPattern set when entering grep results, same as any
//     other push.
//   - Legacy's buffer-lifetime management (STACK::f.bp, a hand-set
//     bit meaning "this frame's bp points into the shared
//     hp->base[12] reusable-slot pool, don't free() it on pop" vs.
//     "this bp was calloc'd just for this one excursion, DO free()
//     it") is replaced by ordinary std::shared_ptr<void> reference
//     counting on ModeStackFrame::view/cutBuffer. A caller that wants
//     "pool slot, never actually destroyed" semantics wraps the slot
//     in a shared_ptr with a no-op deleter; a caller that wants
//     "one-off, free when no longer referenced" semantics uses a
//     normal owning shared_ptr. Either way, ModeStack itself never
//     needs to know which case it's holding or make a manual
//     free()-or-not decision -- the exact class of bug this project
//     has repeatedly found and fixed elsewhere (a hand-tracked
//     ownership flag getting out of sync with reality) structurally
//     can't happen here.
//
// Deliberately excluded:
//   - Row/column/scroll cursor position: NOT this class's job and
//     never was legacy's STACK/GREP's job either -- confirmed by
//     reading both structs, neither carries a PN position at all.
//     That's LL's job, and LL is already fully covered by the
//     already-ported History class (positionFor()/setPosition()) --
//     no gap, no overlap.
//   - Persistence to disk: unlike History, legacy's STACK/GREP are
//     purely in-memory/per-session (confirmed: read_list()/
//     write_list() never reference either). ModeStack has no file
//     I/O of its own.
//   - clear_stack()'s bulk-unwind-with-stopping-condition logic
//     (mode.c): its six stopping conditions reference concrete mode
//     constants and UI concepts (M_fgrep/M_egrep prev-marker checks,
//     a T_pipe2 name check, a specific help-buffer pool slot) that
//     don't exist anywhere in this C++ port yet, and encoding them
//     now -- before Mode/Application exists to give them meaning --
//     would just be guessing. ModeStack exposes the primitives
//     (push/pop/peek/find) that such a bulk-unwind needs; the policy
//     of "keep popping while such-and-such is true" is Mode/
//     Application's job to build on top, the same way KeyInput
//     reports raw keys without deciding what a mode does with them.
//   - Actual mode-dispatch/screen-drawing logic triggered by push/pop
//     (what `^G` does, what gets redrawn): Mode/Application's job;
//     ModeStack only remembers frames.
class ModeStack {
 public:
  // Legacy's "nothing above this" sentinel (save_stack_dir()'s
  // literal K_z-then-NUL string), exposed as a named constant so
  // callers/tests don't need to know the magic byte. A frame's
  // previousMarker equals this exactly when it's the floor frame --
  // see isFloor().
  static const std::string kFloorMarker;

  void push(ModeStackFrame frame);

  // Removes and returns the top (most-recently-pushed) frame, or
  // nullopt if the stack is empty. Ported from load_stack() minus
  // its buffer-ownership bookkeeping (see the class comment) and
  // minus the GREP-chain coordination (no longer needed, see above).
  std::optional<ModeStackFrame> pop();

  // The top frame without removing it, or nullptr if empty.
  const ModeStackFrame *top() const;

  bool empty() const;
  std::size_t size() const;

  // Establishes the floor of the stack at session start -- ported
  // from save_stack_dir(). Clears any existing frames first (a fresh
  // floor always starts a fresh stack, matching legacy's one-shot use
  // at init_menu()).
  void pushFloor(const std::string &directory, const std::string &previousDirectory,
                 std::shared_ptr<void> view, int mode);

  bool isFloor(const ModeStackFrame &frame) const;

  // Read-only existence checks -- ported from read_stack(), which
  // legacy used purely to answer "is such-and-such frame already
  // somewhere on the stack" (e.g. to jump to an existing frame
  // instead of pushing a duplicate). Scans from the bottom (oldest)
  // frame, matching legacy's own scan direction; returns the first
  // match or nullptr. viewPtr is compared by identity (frame.view.get()
  // == viewPtr), not by value.
  const ModeStackFrame *findByView(const void *viewPtr) const;
  const ModeStackFrame *findByName(const std::string &name) const;

 private:
  std::deque<ModeStackFrame> frames_;  // back() is the top of stack
};

}  // namespace fm
