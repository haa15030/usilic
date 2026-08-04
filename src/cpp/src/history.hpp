#pragma once

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "charset.hpp"

namespace fm {

// Named cursor-position memory categories -- ported from LL's file/
// dir/man/mode fields (tcb.h). "Screen" is legacy's mode-keyed
// catch-all bucket (every screen that isn't naturally keyed by a
// pathname -- LOCATE/EGREP results, DU, the PROGRAM/MAN menus
// themselves, split-buffer identity, etc.), not just a literal
// display "mode".
enum class PositionCategory { File, Directory, ManPage, Screen };

// Plain first-occurrence-wins name sets -- ported from LL's grep/cmd
// fields as used by read_list()/write_list() specifically (the CUT
// and COMMAND sections of the history file). Deliberately distinct
// from Configuration's own GREP/PROGRAM seed lists, which are a
// different, editable feature that happens to reuse the same LIST/STR
// C structs in legacy but is never itself written to the history
// file -- see the class comment for why this class doesn't try to
// unify the two.
enum class NameSetCategory {
  CutPattern,        // " CUT" section: patterns used for CUT-mode extraction
  CommandModeLabel,  // " COMMAND" section: COMMAND-mode screen labels
};

// "Recently visited N" ring histories -- ported from SS's hist/hfile/
// hdir/hman/hlocate/hfgrep/hegrep fields.
enum class RingCategory {
  ShellCommand,        // " HISTORY": run shell commands / cd's / PROGRAM-mode commands
  RecentFile,          // " FILE_HISTORY"
  RecentDirectory,     // " DIR_HISTORY"
  RecentManPage,       // " MAN_HISTORY"
  RecentLocateQuery,   // " LOCATE_HISTORY"
  RecentFgrepPattern,  // " FGREP_HISTORY"
  RecentEgrepPattern,  // " EGREP_HISTORY"
};

// Persists cursor-position memory, recency lists, and a few
// session-level settings across runs. Ported from sub.c's
// read_list()/write_list() (the ~/.usilic/history file format) and
// the LIST (BST)/STR (linked-list) trees they populate via
// save_list()/load_list()/save_str()/save_history()/free_history().
//
// This is NOT a literal port. Several structural changes:
//   - File I/O is not done here, same split as Configuration's
//     loadRcText(): loadHistoryText()/serialize() take/return already
//     read/to-be-written text. Locating the file, the `.old` backup-
//     then-overwrite dance, and falling back to `.old` when the
//     primary looks truncated or corrupt are the caller's job (a
//     future Application class) -- this keeps parsing itself
//     unit-testable without a real $HOME. loadHistoryText() still
//     reports whether the text ends with the expected EOF sentinel
//     (see isComplete() below) so the caller can decide whether a
//     `.old` fallback is warranted, preserving legacy's corruption
//     signal without needing this class to touch the filesystem.
//   - LIST's binary search tree and STR's singly linked list are
//     genuinely just string-keyed maps and ordered string sequences
//     respectively (see sub.c: load_list() walks *both* children
//     unconditionally rather than using the sort order to prune,
//     making it an O(n) walk in tree's clothing; save_str()'s "tree"
//     usage in this file is always degenerate, name-only). Replaced
//     with std::map<std::string, CursorPosition> and
//     std::vector<std::string>/std::deque<std::string>, matching
//     the same "replace a hand-rolled data structure with the
//     standard one that does the same job" choice made for
//     ContentView's PP linked list and ArchiveEntry's fields.
//   - No F6/F10-style sentinel-pointer calling conventions (legacy's
//     save_history(F6, ...) overloading a real STR* parameter with a
//     magic (void*)-6 to mean "actually, configure the global
//     retention limit instead"). The retention count is a plain
//     constructor parameter, sourced from
//     Configuration::historyRetentionCount() by the caller.
//   - The retention count is applied directly and consistently. A
//     real, newly-found bug in the legacy C is deliberately NOT
//     reproduced here: save_history()'s F6 branch has two identical
//     `if (hist == F6)` conditions in a row, the second of which is
//     unreachable dead code (the first always wins). init_menu()
//     relies on that second branch to *read back* the configured
//     retention count, but since it's unreachable, the read-back
//     always yields the hardcoded default of 200 -- so a custom
//     `HISTORY` value in usilicrc was silently discarded and reset to
//     200 on every single run of the legacy binary. Since
//     Configuration already hands this class a clean integer
//     directly, there's no equivalent round-trip to get wrong.
//   - Charset restore precedence is explicit, not a hidden
//     env_charset() global check. loadHistoryText() takes
//     environmentCharsetExplicitlySet as a plain parameter (the
//     caller passes Configuration::charsetExplicitlySet()) rather
//     than reaching for that global itself -- this is the read-side
//     half of the CLAUDE.md history item 13 / item 3-Step-6 fix;
//     Configuration's own port already covers the "was
//     USILICCHARSET explicitly exported" half.
//
// Deliberately excluded (belongs to other classes):
//   - Rendering any of this as an on-screen "recent files/dirs/..."
//     menu: a future Buffer/ContentView rendering concern, not
//     persistence.
//   - Configuration's own editable GREP/PROGRAM seed lists (legacy's
//     rprog/rgrep SS fields and the delete-from-menu machinery that
//     rebuilds them): confirmed, by reading sub.c directly, that
//     those two fields never appear in read_list()/write_list() at
//     all -- not this class's concern even though the underlying C
//     structs are shared in legacy.
//   - Actually running programs/shells that generate history entries
//     (ChildProcess/Shell/future Mode): this class only records that
//     something happened, via calls from those subsystems.
//   - Resolving a program name string back into a real, runnable
//     command: SessionState::pendingCutProgram is stored as a plain
//     string (legacy: `lp->cut.chp->argv[0]`, re-resolved via
//     get_child() on load and check_ext() on save) -- resolving it
//     through Configuration is the caller's job, keeping History
//     decoupled from Configuration the same way ChildProcess/Shell
//     don't reach into Configuration themselves either.
class History {
 public:
  struct CursorPosition {
    int row = 0;
    int totalRows = 0;
    unsigned top = 0;
    int lineNumber = -1;
  };

  // The "Current buffer" line: where the previous session left off.
  // Field names mirror legacy's LINE struct (dir/old/arg/prev) plus
  // the CUT-mode buffer, not renamed further since these are already
  // reasonably self-descriptive and this project's own LINE-adjacent
  // code (Shell, etc.) doesn't otherwise touch this struct.
  struct SessionState {
    std::string previousDirectory;    // LINE::old
    std::string currentDirectory;     // LINE::dir
    std::string modeLineArg;          // LINE::arg
    std::string previousScreenMarker; // LINE::prev
    std::string lastOpenName;         // the name being viewed when the session ended
    std::string pendingCutBuffer;     // CUT::str -- in-progress CUT-mode extraction text
    std::string pendingCutProgram;    // CUT::chp's program name, unresolved (see class comment)
  };

  explicit History(int retentionCount = 200);

  // -- cursor position memory --
  std::optional<CursorPosition> positionFor(PositionCategory category, const std::string &name) const;
  void setPosition(PositionCategory category, const std::string &name, CursorPosition position);

  // -- plain name sets (first-occurrence-wins, unbounded, insertion order) --
  bool containsName(NameSetCategory category, const std::string &name) const;
  void rememberName(NameSetCategory category, const std::string &name);
  const std::vector<std::string> &namesIn(NameSetCategory category) const;

  // -- ring histories --
  // Pushes name, evicting the single oldest entry if the ring is
  // already at the retention count. Suppresses only an IMMEDIATE
  // repeat of the single most-recently-pushed name (ring.back()==
  // name) -- ported exactly from find_hist()'s own walk, which
  // compares against the LAST node only, not the whole list (a
  // 2026-08-04 correction: an earlier version of this comment/
  // implementation claimed whole-ring first-occurrence-wins dedup,
  // which was a mis-port). Re-visiting an OLDER entry (not the one
  // immediately before it) genuinely re-appends it, duplicates and
  // all -- e.g. A, B, A yields the raw ring [A, B, A]. This is by
  // design: this ring IS the raw, chronological access log ("HISTORY"
  // -- see Application::toggleListBrowserHistory()'s own comment);
  // callers that want a sorted, de-duplicated "unique recent items"
  // view (e.g. Application::startListBrowser()'s FILE/DIR picker) do
  // their own de-duplication over recentEntries() at display time.
  void pushRecent(RingCategory category, const std::string &name);
  const std::deque<std::string> &recentEntries(RingCategory category) const;  // oldest .. newest
  int retentionCount() const;

  // -- session-level trailer settings --
  bool smoothScrollEnabled() const;
  void setSmoothScrollEnabled(bool enabled);
  int lsMode() const;
  void setLsMode(int mode);
  // nullopt if unset (fresh history) or if the environment charset
  // won precedence on load (see loadHistoryText()) -- callers should
  // fall back to their own documented default (Utf8) in either case,
  // never to Euc; see the class comment on CLAUDE.md history item 13.
  std::optional<Charset::Encoding> savedCharset() const;
  void setSavedCharset(Charset::Encoding encoding);

  const SessionState &sessionState() const;
  void setSessionState(SessionState state);

  // -- serialization --
  // environmentCharsetExplicitlySet: pass
  // Configuration::charsetExplicitlySet() -- when true, the trailer's
  // saved charset is parsed but not applied (savedCharset() stays
  // nullopt), matching legacy's `if (!env_charset(-1)) vt_charset(...)`
  // restore-precedence check.
  bool loadHistoryText(const std::string &text, bool environmentCharsetExplicitlySet);
  // True once loadHistoryText() has seen the trailing EOF sentinel
  // that legacy's read_list() checks for before trusting the file
  // (ported from its `fseek(fp,-8,2); ... strchr(str, K_z)` check) --
  // false after a failed/partial load, signaling the caller that a
  // `.old` backup fallback may be warranted.
  bool isComplete() const;
  const std::vector<std::string> &errors() const;

  std::string serialize(const std::string &programName, const std::string &timestampText) const;

 private:
  std::map<PositionCategory, std::map<std::string, CursorPosition>> positions_;
  std::map<NameSetCategory, std::vector<std::string>> nameSets_;
  std::map<RingCategory, std::deque<std::string>> rings_;

  int retentionCount_;
  bool smoothScrollEnabled_ = false;
  int lsMode_ = 0;
  std::optional<Charset::Encoding> savedCharset_;
  SessionState sessionState_;

  bool complete_ = false;
  std::vector<std::string> errors_;
};

}  // namespace fm
