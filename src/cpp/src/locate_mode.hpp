#pragma once

#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"

namespace fm {

// LOCATE mode (T_locate): runs `locate <query>` and displays the
// matching paths. Ported from init.c's T_locate case (fork_bin() +
// Configuration::commandFor(Locate) + the query string as an
// argument) -- menu.c's own m_locate() is pure mode-transition/
// ModeStack-adjacent dispatch glue with no locate-specific logic of
// its own, so nothing there needed porting; see the class comment on
// every other *Mode class in this port for why (Mode/Application's
// job, not this class's).
//
// Unlike DirectoryMode's `ls` output, `locate`'s output needs no
// parsing at all -- one bare path per line -- so this class is even
// thinner than FileMode/ArchiveMode/DuMode.
//
// Deliberately excluded (future work, not this slice): deciding what
// to do with a selected result (handing off to DirectoryMode or
// FileMode depending on whether it's a directory or a file): the same
// hand-off boundary every other *Mode class in this port already
// draws.
class LocateMode {
 public:
  LocateMode(ContentView &view, const Configuration &configuration);

  // Runs `locate query` and displays the results. False if the
  // command couldn't be run (locate not configured/installed) or
  // query is empty. A successful search with zero matches is not a
  // failure -- results() is simply empty afterward.
  bool search(const std::string &query);

  const std::string &query() const;
  const std::vector<std::string> &results() const;
  const std::string *selectedResult() const;

 private:
  ContentView &view_;
  const Configuration &configuration_;

  std::string query_;
  std::vector<std::string> results_;
};

}  // namespace fm
