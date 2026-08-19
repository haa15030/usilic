#pragma once

#include <string>
#include <vector>

#include "content_view.hpp"

namespace fm {

// List/history browser (M_list/M_hist/M_cmd/T_hfile/T_hdir/T_hman/
// T_hlocate/T_hfgrep/T_hegrep): displays a simple list of strings --
// one of History's already-built ring/name-set categories, or
// Configuration's grep/program seed lists -- and lets the user pick
// one. Ported from menu.c's m_list() in spirit only: m_list() itself
// is pure mode-transition dispatch (deciding, from context, WHICH of
// the ~9 list-shaped things to browse), with no rendering logic of
// its own -- the actual "show these strings, track a selection" job
// is generic and identical across all ~9 call sites, which is exactly
// what this class factors out instead of leaving duplicated.
//
// This is NOT a literal port of any single legacy function -- there
// isn't one to port; init_list()/init_str() (alloc.c) build a PP list
// from a LIST/STR tree for on-screen display, but the trees
// themselves are already fully replaced by History's std::map/
// std::vector/std::deque-based categories and Configuration's
// std::vector-based seed lists, so there's no legacy tree-walking
// left to port either -- this class only needs to take the resulting
// flat list of strings, from wherever the caller got it, and display
// it.
//
// Deliberately excluded (future work, not this slice): deciding
// which underlying History/Configuration category to browse for a
// given key/mode, and what to do with a selected entry (jump to that
// directory, open that file, re-run that command, ...): Mode/
// Application's job, the same hand-off boundary every other *Mode
// class in this port already draws.
class ListBrowserMode {
 public:
  explicit ListBrowserMode(ContentView &view);

  // Displays items (e.g. from
  // History::recentEntries(RingCategory::RecentFile) or
  // Configuration::grepSeedPatterns() -- caller converts a
  // std::deque to a std::vector if needed).
  //
  // `badges` (2026-08-20 addition, optional, same length as `items` or
  // empty): a per-item DISPLAY-only suffix (e.g. "(tar)" for an
  // archive-recognized RecentFile path, see Application's own
  // archiveBadgeFor()) appended after each item's own text on screen.
  // Kept entirely separate from `items_` itself (still the exact,
  // unmodified strings selectedItem() returns) -- this class stays as
  // unaware of WHAT a badge means (RingCategory, archive detection,
  // ...) as its own class comment already establishes it should be;
  // the caller decides whether/what to badge, this just renders it.
  // An empty string for any given index (or a shorter/absent `badges`
  // vector entirely) means "no badge for this one", matching every
  // existing call site's unchanged behavior exactly.
  void show(const std::vector<std::string> &items, const std::vector<std::string> &badges = {});

  const std::vector<std::string> &items() const;
  const std::string *selectedItem() const;

 private:
  ContentView &view_;
  std::vector<std::string> items_;
};

}  // namespace fm
