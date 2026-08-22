#include "grep_mode.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "child_process.hpp"
#include "unicode_normalize.hpp"

namespace fm {

GrepMode::GrepMode(ContentView &view, const Configuration &configuration)
    : view_(view), configuration_(configuration) {}

bool GrepMode::search(const std::vector<std::string> &sourceLines, const std::string &pattern,
                       GrepSyntax syntax) {
  if (pattern.empty()) return false;

  ProgramCommand cmd =
      configuration_.commandFor(syntax == GrepSyntax::Fixed ? ConfiguredRole::Fgrep : ConfiguredRole::Egrep);
  if (cmd.argv.empty()) return false;

  std::string inputText;
  for (const auto &line : sourceLines) {
    inputText += line;
    inputText += '\n';
  }

  // 2026-08-23 addition (docs/CHANGES.md item 407, a real user report:
  // FGREP found "口座テスト" but not "マイナカード" against the SAME
  // directory listing) -- see unicodeNormalizationCandidates()'s own
  // comment for why a real filename's bytes can be either Unicode
  // normalization form regardless of what a normal IME types. `pattern`
  // as typed is always tried FIRST (so an already-matching search costs
  // nothing extra and, more importantly, so `pattern_`/history below
  // still records exactly what the user typed either way); only tries
  // NFC/NFD as a fallback when the literal bytes alone don't match.
  ChildProcess::Options options;
  options.input = &inputText;
  ChildProcess::Result result;
  bool ranAtLeastOnce = false;
  for (const auto &candidate : unicodeNormalizationCandidates(pattern)) {
    std::vector<std::string> argv = cmd.argv;
    argv.push_back(candidate);
    result = ChildProcess::run(argv, options);
    ranAtLeastOnce = true;
    // Exit 0 = matches found, exit 1 = ran fine but found none -- both
    // are a successful RUN (not necessarily a successful search: exit
    // 1 falls through to the next candidate, if any). Anything else
    // (bad pattern, exec failure, signaled) is a real failure -- bail
    // out immediately rather than trying further candidates with a
    // command that's already shown it can't run at all.
    if (result.exitStatus < 0 || result.exitStatus > 1) return false;
    if (result.exitStatus == 0) break;  // found matches with this candidate -- stop here
  }
  if (!ranAtLeastOnce) return false;

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
  // Empty-result placeholder (2026-08-12, two rounds of user
  // correction -- see ContentView::setEmptyPlaceholder()'s own
  // comment for the full history): "<pattern>: No String", with ONLY
  // "No String" underlined (not the pattern prefix) and no full
  // reverse-video "marked" treatment at all (that read as "this is a
  // selectable string", which it isn't). Set fresh on every search --
  // legacy has no concept of "the last pattern that found nothing"
  // persisting once a NEW search runs, and neither should this.
  if (matches_.empty()) {
    static const std::string kSuffix = "No String";
    std::string text = pattern_ + ": " + kSuffix;
    int underlineFrom = static_cast<int>(text.size() - kSuffix.size());
    view_.setEmptyPlaceholder(
        ContentView::Line{text, std::nullopt, std::nullopt,
                           {AttributeRun{underlineFrom, static_cast<int>(text.size()), Display::Attribute::Underline}}},
        /*marked=*/false);
  }
  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(matches_.size());
  for (const auto &m : matches_) viewLines.push_back({m.text, std::nullopt, std::nullopt, {}});
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
