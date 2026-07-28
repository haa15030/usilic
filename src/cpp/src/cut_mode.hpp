#pragma once

#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"
#include "grep_mode.hpp"

namespace fm {

// One delimiter-bounded segment: [startLine, endLine) into the lines
// split() was given. label is the delimiter line itself (the line
// the search pattern matched on) -- per the user's own description of
// this feature (confirmed directly, since the legacy C source alone
// left this genuinely ambiguous -- see the class comment), the
// pattern is typically constant across matches (e.g. LOG mode always
// splits on the literal "tcb(usilic)-1.5b" shell prompt marker) while
// the FULL LINE it appears on varies (the prompt line also carries
// e.g. the command that follows), which is what makes a list of
// labels actually useful for picking a segment.
struct CutSegment {
  std::string label;
  int startLine = 0;
  int endLine = 0;  // exclusive
};

// CUT mode (^Jc/^J^C in file-content view, ^Jb/^J^B in a directory
// listing without first opening the file, and LOG's own built-in
// use): splits a buffer's lines into segments at each line matching a
// search pattern, and displays a list of segment labels (the matched
// delimiter lines) to pick from. Ported from mode.c's M_cut/T_cut/
// T_split handling in spirit, not literally -- see below.
//
// The legacy C source alone (mode.c/menu.c's heavily cascading
// M_egrep/T_hfgrep/T_hegrep/M_fgrep/M_cut/M_cut2 switch fallthrough,
// see menu.c's get_m()) does not by itself make clear whether the
// text typed into the CUT prompt is a search pattern, a save-as
// filename, or something else -- confirmed directly with the user
// rather than guessed at: it's a search pattern (fixed string via
// ^Jc/^Jb, or regex via ^J^C/^J^B), used to split the current
// buffer's content at each matching line, with a list of those
// matching lines offered as segment labels to pick from.
//
// This is NOT a literal port. Given that description, CUT's matching
// mechanism is identical to GrepMode's (both are, at bottom, "run the
// real fgrep/egrep binary over these lines and get back matching line
// numbers" -- ported from the same fork_grep() legacy uses for both
// features) -- CutMode duplicates that small amount of matching logic
// rather than taking a GrepMode dependency, since GrepMode's own
// ContentView is for displaying MATCHED LINES themselves, not segment
// labels, and forcing CutMode to own an unused second ContentView just
// to reuse GrepMode's search() would be more awkward than the
// duplication it would save.
//
// Deliberately excluded (future work, not this slice):
//   - The ^Jb/^J^B "split without first opening the file view"
//     shortcut and LOG mode's own built-in prompt-marker split:
//     both are just split() called with a specific pattern from a
//     specific source -- Mode/Application's job to wire up once a
//     directory-listing/log-viewing orchestrator exists, not
//     CutMode's own concern (matching how FileMode doesn't know who
//     opens it either).
//   - Recording cut/search patterns into History: same boundary
//     GrepMode already draws.
//   - Nested cut (splitting an already-split segment further) and
//     ModeStack coordination for returning to the pre-split view:
//     Mode/Application's job.
class CutMode {
 public:
  CutMode(ContentView &view, const Configuration &configuration);

  // Splits sourceLines into segments at each line matching pattern
  // (via the real fgrep/egrep binary, same mechanism as GrepMode --
  // see the class comment), and displays the segment labels. False
  // only if the search command itself couldn't be run, or pattern is
  // empty. Content before the first match (if any) becomes its own
  // unlabeled leading segment rather than being silently dropped; no
  // matches at all means the whole input is one segment.
  bool split(const std::vector<std::string> &sourceLines, const std::string &pattern, GrepSyntax syntax);

  const std::string &pattern() const;
  const std::vector<CutSegment> &segments() const;
  const CutSegment *selectedSegment() const;

  // The lines making up `segment`, sliced from the sourceLines
  // split() was called with.
  std::vector<std::string> segmentContent(const CutSegment &segment) const;

 private:
  ContentView &view_;
  const Configuration &configuration_;

  std::string pattern_;
  std::vector<std::string> sourceLines_;
  std::vector<CutSegment> segments_;
};

}  // namespace fm
