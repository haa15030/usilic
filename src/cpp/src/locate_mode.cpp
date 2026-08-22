#include "locate_mode.hpp"

#include <sstream>
#include <unordered_set>

#include "child_process.hpp"

namespace fm {

namespace {

// Runs `role`'s command with `query` appended (same shape as the
// system ConfiguredRole::Locate always used) and returns its output
// lines. `errorsToDevNull`: the personal database's own search
// (ConfiguredRole::LocateUserDb) needs this before its first ever
// build -- `locate -d <not-yet-built path>` fails with a real
// "No such file or directory" on stderr every time otherwise, which
// would otherwise flash onto the real terminal (ChildProcess::run()
// leaves stderr inherited by default, see its own class comment) on
// every LOCATE search until the user has pressed ^Jl's update once.
// The system role never hits this (its database is macOS/Linux's own
// pre-existing one, present or not regardless of this port), so it
// stays on the default (inherited stderr) for that role.
std::vector<std::string> runLocateRole(const Configuration &configuration, ConfiguredRole role,
                                        const std::string &query, bool errorsToDevNull) {
  ProgramCommand command = configuration.commandFor(role);
  if (command.argv.empty()) return {};
  std::vector<std::string> argv = command.argv;
  argv.push_back(query);
  static const std::string kDevNull = "/dev/null";
  ChildProcessOptions options;
  if (errorsToDevNull) options.stderrLogPath = &kDevNull;
  ChildProcess::Result result = ChildProcess::run(argv, options);
  // Real `locate` exits 1 when it simply found nothing (or, for
  // LocateUserDb before its first build, when the database file
  // doesn't exist yet); only treat a genuine failure to run (or a real
  // error exit) as this search failing, same convention as GrepMode/
  // CutMode's own fgrep/egrep exit-status handling.
  if (result.exitStatus < 0 || result.exitStatus > 1) return {};

  std::vector<std::string> lines;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line))
    if (!line.empty()) lines.push_back(line);
  return lines;
}

}  // namespace

LocateMode::LocateMode(ContentView &view, const Configuration &configuration)
    : view_(view), configuration_(configuration) {}

bool LocateMode::search(const std::string &query) {
  if (query.empty()) return false;

  std::vector<std::string> results = runLocateRole(configuration_, ConfiguredRole::Locate, query, false);
  bool systemConfigured = !configuration_.commandFor(ConfiguredRole::Locate).argv.empty();
  std::vector<std::string> userResults = runLocateRole(configuration_, ConfiguredRole::LocateUserDb, query, true);
  // Neither database configured at all: same "couldn't run" failure
  // search() always returned for a missing ConfiguredRole::Locate,
  // unchanged from before this function also learned about
  // LocateUserDb -- LocateUserDb being unconfigured (every existing
  // test, and every non-macOS build) never turns a real Locate failure
  // into a false success.
  if (!systemConfigured && configuration_.commandFor(ConfiguredRole::LocateUserDb).argv.empty()) return false;

  if (!userResults.empty()) {
    std::unordered_set<std::string> seen(results.begin(), results.end());
    for (auto &path : userResults)
      if (seen.insert(path).second) results.push_back(std::move(path));
  }

  query_ = query;
  results_ = std::move(results);

  std::vector<ContentView::Line> lines;
  lines.reserve(results_.size());
  // ellipsisEligible=true: see Line::ellipsisEligible's own comment --
  // each result is one discrete path, exactly the "Finder-style middle
  // ellipsis" case that feature was designed for.
  for (const auto &path : results_) lines.push_back({path, std::nullopt, std::nullopt, {}, true});
  view_.setLines(std::move(lines));
  view_.setCursorIndex(0);
  view_.redraw();
  return true;
}

namespace {

// Same POSIX single-quote-escaping shape as application.cpp's own
// shellQuoteArg()/configuration.cpp's own shellQuoteSinglePath() --
// separate translation units, not shared, same as those two.
std::string shellQuoteArgForUpdateDb(const std::string &arg) {
  std::string quoted = "'";
  for (char c : arg) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return quoted;
}

std::string shellQuoteArgvForUpdateDb(const std::vector<std::string> &argv) {
  std::string line;
  for (const auto &arg : argv) {
    if (!line.empty()) line += " ";
    line += shellQuoteArgForUpdateDb(arg);
  }
  return line;
}

}  // namespace

pid_t LocateMode::startUpdateDb() {
  ProgramCommand systemCommand = configuration_.commandFor(ConfiguredRole::UpdateDb);
  ProgramCommand userCommand = configuration_.commandFor(ConfiguredRole::LocateUserDbBuild);
  if (systemCommand.argv.empty() && userCommand.argv.empty()) return -1;
  if (userCommand.argv.empty()) return ChildProcess::runDetached(systemCommand.argv);
  if (systemCommand.argv.empty()) return ChildProcess::runDetached(userCommand.argv);
  // Both configured (macOS, Configuration::setUserLocateDatabasePath()
  // called): chain into ONE detached child -- see this function's own
  // header comment for why (Application's single-pid await/poll
  // machinery needs no changes). userCommand.argv is already its own
  // `{"sh", "-c", "<script>"}` (Configuration::setUserLocateDatabasePath())
  // -- quoting and re-joining it as three separate words inside this
  // OUTER `sh -c` and letting that outer shell parse+exec them is
  // exactly equivalent to running it directly, the same trick
  // Application::shellQuoteArgv() callers already rely on elsewhere in
  // this port to safely reconstruct a command line from an argv.
  std::string combined = shellQuoteArgvForUpdateDb(systemCommand.argv) + " ; " + shellQuoteArgvForUpdateDb(userCommand.argv);
  return ChildProcess::runDetached({"sh", "-c", combined});
}

const std::string &LocateMode::query() const { return query_; }
const std::vector<std::string> &LocateMode::results() const { return results_; }

const std::string *LocateMode::selectedResult() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(results_.size())) return nullptr;
  return &results_[idx];
}

}  // namespace fm
