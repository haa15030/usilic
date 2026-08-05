#include "locate_mode.hpp"

#include <sstream>

#include "child_process.hpp"

namespace fm {

LocateMode::LocateMode(ContentView &view, const Configuration &configuration)
    : view_(view), configuration_(configuration) {}

bool LocateMode::search(const std::string &query) {
  if (query.empty()) return false;

  ProgramCommand command = configuration_.commandFor(ConfiguredRole::Locate);
  if (command.argv.empty()) return false;

  std::vector<std::string> argv = command.argv;
  argv.push_back(query);
  ChildProcess::Result result = ChildProcess::run(argv);
  // Real `locate` exits 1 when it simply found nothing; only treat a
  // genuine failure to run (or a real error exit) as this search
  // failing, same convention as GrepMode/CutMode's own fgrep/egrep
  // exit-status handling.
  if (result.exitStatus < 0 || result.exitStatus > 1) return false;

  std::vector<std::string> results;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line))
    if (!line.empty()) results.push_back(line);

  query_ = query;
  results_ = std::move(results);

  std::vector<ContentView::Line> lines;
  lines.reserve(results_.size());
  for (const auto &path : results_) lines.push_back({path, std::nullopt, std::nullopt, {}});
  view_.setLines(std::move(lines));
  view_.setCursorIndex(0);
  view_.redraw();
  return true;
}

const std::string &LocateMode::query() const { return query_; }
const std::vector<std::string> &LocateMode::results() const { return results_; }

const std::string *LocateMode::selectedResult() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(results_.size())) return nullptr;
  return &results_[idx];
}

}  // namespace fm
