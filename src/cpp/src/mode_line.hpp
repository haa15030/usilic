#pragma once

#include <string>

namespace fm {

// The scroll-position sliver of legacy disp.c's print_line() -- "the
// current-line and total-lines at mode-line". Deliberately just this
// one piece: print_line()/disp_window()/disp_prev()/disp_copy() also
// drove the current-path breadcrumb, the previous-mode string, a
// "Mark set" indicator, an interactive line-jump input widget, and the
// PROGRAM-name display, all specific to whichever mode/PP was active
// -- ContentView's own class comment already earmarked all of that as
// future StatusBar/Mode territory. This class is the first slice: a
// standalone, pure formatter so ContentView (the one place already
// reached by every mode's redraw()) can call it without owning the
// formatting rule itself.
class ModeLine {
 public:
  // currentLine/totalLines are 1-indexed (0 for an empty list).
  // bottomVisibleLine is the 1-indexed line number currently showing
  // in the viewport's last row -- when it reaches totalLines (the
  // view is scrolled all the way to the end), legacy showed "END"
  // instead of a percentage, ported as-is here. Format matches
  // legacy's "%s/%s %2d%%" / "%s/%s END", current zero-padded to
  // total's own digit width (e.g. "03/15 20%").
  static std::string format(int currentLine, int totalLines, int bottomVisibleLine);
};

}  // namespace fm
