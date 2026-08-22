#pragma once

#include <string>
#include <vector>

#include <sys/types.h>  // pid_t

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
  //
  // 2026-08-22 addition (docs/CHANGES.md item 406, a direct user
  // request: "両方検索してマージを希望"): if ConfiguredRole::
  // LocateUserDb is also configured (macOS, see startUpdateDb()'s own
  // comment), its own results are appended after the system database's
  // own, de-duplicated -- a path present in both only appears once,
  // system-database order winning ties. The personal database not
  // existing yet (before its first build) is treated the same as "zero
  // matches", not a failure -- see LocateMode::search()'s own comment.
  bool search(const std::string &query);

  // Starts `updatedb` (to refresh the locate database, K-39, ^Jl) in
  // the BACKGROUND -- doesn't wait for it to finish. Not a literal
  // port -- see ConfiguredRole::UpdateDb's own comment
  // (configuration.cpp) for why this exists despite legacy having no
  // such feature. Touches neither query_/results_ nor view_: it's a
  // side-effecting maintenance action, not a search. 2026-08-14
  // change: was originally a blocking call (ChildProcess::run()),
  // which meant Application::runLocateUpdateDb() couldn't respond to
  // ^G until a real (potentially minutes-long) updatedb run finished
  // entirely -- a real user report ("フリーズ中ではないメッセージなの
  // で...常時^Gで...移行"). Runs detached instead (ChildProcess::
  // runDetached()); the caller polls ChildProcess::hasExited() on the
  // returned pid alongside its own keyboard input loop. Returns -1 if
  // the command couldn't be launched at all (not configured/installed).
  // 2026-08-22 addition (docs/CHANGES.md item 406): if
  // ConfiguredRole::LocateUserDbBuild is also configured (macOS,
  // Configuration::setUserLocateDatabasePath() -- unconfigured in
  // every existing test, so this is a pure no-op there), its build
  // command is chained after the system update's own, both inside ONE
  // detached `sh -c` child -- so this still returns exactly one pid
  // and Application::awaitingLocateUpdateDb()/pollLocateUpdateDbCompletion()
  // need no changes at all. Chained with `;`, not `&&`: the personal
  // database still gets (re)built even when the system update fails
  // outright (e.g. no prior manual `sudo`, see ConfiguredRole::
  // UpdateDb's own comment on why this port never wraps that in sudo
  // itself) -- the two databases are independent, one failing is not a
  // reason to skip the other.
  pid_t startUpdateDb();

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
