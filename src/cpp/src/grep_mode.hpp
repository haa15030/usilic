#pragma once

#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"

namespace fm {

enum class GrepSyntax { Fixed, Regex };  // fgrep (fixed string) vs egrep (extended regex)

struct GrepMatch {
  int sourceLineIndex = -1;  // 0-based index into the lines search() was given
  std::string text;
};

// Search mode (FGREP/EGREP): searches a set of lines for a pattern and
// displays the matching lines. Ported from fork.c's fork_grep() --
// which, perhaps surprisingly, is not a hand-rolled matching loop at
// all: it forks a real `fgrep`/`egrep` binary, writes the searched
// buffer's lines to its stdin, and captures matching lines back from
// its stdout. GrepMode is a near-direct port of exactly that shape,
// now that ChildProcess (piped stdin, already built) and
// Configuration (the fgrep/egrep command lines, already built) exist
// to do it cleanly: search() runs the real binary via
// ChildProcess::run() with `-n` (already baked into Configuration's
// Fgrep/Egrep role commands) so each output line is
// "<1-based line number>:<matched text>", letting matches be mapped
// straight back to their original line index.
//
// This is NOT a literal port of mode()'s M_egrep/M_fgrep/T_grep
// handling. Legacy's version is entangled with the two-parallel-stack
// coordination ModeStack's own class comment already explained
// deferring (the "steal a STACK frame's contents to seed a GREP node"
// dance) -- GrepMode itself has no opinion on what happens when a
// match is opened or the search is dismissed; see the class comment
// on scope.
//
// Deliberately excluded (future work, not this slice):
//   - Nested search (searching within an already-filtered result
//     set) and the ModeStack/GREP-chain coordination for returning to
//     whatever was being viewed before the search: Mode/Application's
//     job, the same boundary DirectoryMode/FileMode/ArchiveMode
//     already draw for their own hand-offs -- GrepMode doesn't touch
//     ModeStack or History at all.
//   - Highlighting the matched substring within a line (legacy's
//     T_grep rendering bolds the actual match): ContentView's
//     per-character attribute runs don't exist yet (already flagged
//     as a gap in ContentView's own class comment); this class just
//     shows the matched lines plainly.
//   - Recording search patterns into History's hfgrep/hegrep rings:
//     Mode/Application's job (the same as it is for GrepMode's
//     sibling classes not touching History for anything beyond
//     cursor-position categories they directly own).
class GrepMode {
 public:
  GrepMode(ContentView &view, const Configuration &configuration);

  // Searches sourceLines for pattern using the real fgrep/egrep
  // binary (Configuration::commandFor(Fgrep/Egrep) + ChildProcess,
  // feeding sourceLines as piped stdin). Displays matches (possibly
  // zero -- grep exiting "no matches found" is not a search failure).
  // False only if the search command itself couldn't be run (missing
  // binary, bad pattern the tool itself rejected, ...) or pattern is
  // empty.
  bool search(const std::vector<std::string> &sourceLines, const std::string &pattern, GrepSyntax syntax);

  const std::string &pattern() const;
  const std::vector<GrepMatch> &matches() const;
  const GrepMatch *selectedMatch() const;

 private:
  ContentView &view_;
  const Configuration &configuration_;

  std::string pattern_;
  std::vector<GrepMatch> matches_;
};

}  // namespace fm
