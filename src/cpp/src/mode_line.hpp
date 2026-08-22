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
  // Percent is based on currentLine's own position (currentLine ==
  // where the cursor/viewport-top actually is -- see ContentView::
  // modeLineText()/scrollModeLineText(), the two things that feed
  // this), NOT on how much content has scrolled past, so it always
  // reads 0-ish at the very top and climbs toward 100 as currentLine
  // approaches totalLines. currentLine==1 shows "TOP" and
  // currentLine>=totalLines shows "END" instead of a numeric percent
  // (2026-07-31, changed from legacy's own "viewport-bottom-based %,
  // no TOP label" shape at the user's request -- legacy had no GUI
  // drag-scroll to decouple viewport position from cursor position in
  // the first place, so this is a deliberate departure, not a
  // straight port). Format: "%s/%s %2d%%" / "%s/%s TOP" /
  // "%s/%s END", current zero-padded to total's own digit width (e.g.
  // "03/15 20%").
  static std::string format(int currentLine, int totalLines);

  // Ported from print_line()'s (disp.c) own len<=0 branch -- SHELL-
  // specific in legacy (disp_shell() is the only caller that ever
  // passes a negative len): a fixed-width zero-pad ("00002/00002
  // END"), independent of totalLines' own digit width, instead of
  // format()'s default "pad current to TOTAL's own width". Width is 5
  // (legacy's own C source actually zero-pads to 6 -- see
  // formatFixedWidth()'s own .cpp comment for why this port uses 5
  // instead, a direct user request). Same TOP/END/% suffix logic as
  // format(). See ContentView::setUseFixedWidthPosition()'s own
  // comment for how a view opts into this.
  static std::string formatFixedWidth(int currentLine, int totalLines);
};

}  // namespace fm
