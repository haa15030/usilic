#include "grep_mode.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "child_process.hpp"

namespace fm {

GrepMode::GrepMode(ContentView &view, const Configuration &configuration)
    : view_(view), configuration_(configuration) {}

bool GrepMode::search(const std::vector<std::string> &sourceLines, const std::string &pattern,
                       GrepSyntax syntax) {
  if (pattern.empty()) return false;

  ProgramCommand cmd =
      configuration_.commandFor(syntax == GrepSyntax::Fixed ? ConfiguredRole::Fgrep : ConfiguredRole::Egrep);
  if (cmd.argv.empty()) return false;
  std::vector<std::string> argv = cmd.argv;
  argv.push_back(pattern);

  std::string inputText;
  for (const auto &line : sourceLines) {
    inputText += line;
    inputText += '\n';
  }

  ChildProcess::Options options;
  options.input = &inputText;
  ChildProcess::Result result = ChildProcess::run(argv, options);
  // Exit 0 = matches found, exit 1 = ran fine but found none -- both
  // are a successful search. Anything else (bad pattern, exec
  // failure, signaled) is a real failure.
  if (result.exitStatus < 0 || result.exitStatus > 1) return false;

  matches_.clear();
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    std::size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) continue;
    std::string numPart = line.substr(0, colon);
    bool allDigits = true;
    for (char c : numPart)
      if (!std::isdigit(static_cast<unsigned char>(c))) allDigits = false;
    if (!allDigits) continue;

    GrepMatch match;
    match.sourceLineIndex = std::atoi(numPart.c_str()) - 1;
    match.text = line.substr(colon + 1);
    matches_.push_back(match);
  }

  pattern_ = pattern;
  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(matches_.size());
  for (const auto &m : matches_) viewLines.push_back({m.text, std::nullopt, std::nullopt});
  view_.setLines(std::move(viewLines));
  view_.setCursorIndex(0);
  view_.redraw();
  return true;
}

const std::string &GrepMode::pattern() const { return pattern_; }
const std::vector<GrepMatch> &GrepMode::matches() const { return matches_; }

const GrepMatch *GrepMode::selectedMatch() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(matches_.size())) return nullptr;
  return &matches_[idx];
}

}  // namespace fm
