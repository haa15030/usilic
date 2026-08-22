#include "cut_mode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>

#include "child_process.hpp"
#include "unicode_normalize.hpp"

namespace fm {

CutMode::CutMode(ContentView &view, ModeStack &modeStack, const Configuration &configuration)
    : view_(view), modeStack_(modeStack), configuration_(configuration) {}

bool CutMode::split(const std::vector<StyledLine> &sourceLines, const std::string &pattern,
                     GrepSyntax syntax) {
  if (pattern.empty()) return false;

  ProgramCommand cmd =
      configuration_.commandFor(syntax == GrepSyntax::Fixed ? ConfiguredRole::Fgrep : ConfiguredRole::Egrep);
  if (cmd.argv.empty()) return false;

  std::string inputText;
  for (const auto &line : sourceLines) {
    inputText += line.text;
    inputText += '\n';
  }

  // 2026-08-23 addition (docs/CHANGES.md item 407): same reasoning as
  // GrepMode::search()'s own identical fix -- see
  // unicodeNormalizationCandidates()'s own comment. CUT's fixed/regex
  // split shells out to the exact same fgrep/egrep, so it has the
  // exact same byte-literal-matching blind spot.
  ChildProcess::Options options;
  options.input = &inputText;
  ChildProcess::Result result;
  bool ranAtLeastOnce = false;
  for (const auto &candidate : unicodeNormalizationCandidates(pattern)) {
    std::vector<std::string> argv = cmd.argv;
    argv.push_back(candidate);
    result = ChildProcess::run(argv, options);
    ranAtLeastOnce = true;
    if (result.exitStatus < 0 || result.exitStatus > 1) return false;
    if (result.exitStatus == 0) break;  // found matches with this candidate -- stop here
  }
  if (!ranAtLeastOnce) return false;

  std::vector<int> matchLines;
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
    matchLines.push_back(std::atoi(numPart.c_str()) - 1);
  }
  std::sort(matchLines.begin(), matchLines.end());  // grep -n is already ordered; sort defensively

  pattern_ = pattern;
  sourceLines_ = sourceLines;
  segments_.clear();

  int total = static_cast<int>(sourceLines_.size());
  if (matchLines.empty()) {
    if (total > 0) segments_.push_back({std::string(), 0, total});
  } else {
    // "--" (2026-08-12 fix, matching ContentView's own established
    // "empty/unlabeled region" convention -- see setEmptyPlaceholder()'s
    // own comment): this LEADING segment (everything before the FIRST
    // match, e.g. a directory listing's own "./" through whatever line
    // precedes the first "package*" hit) has no delimiter line of its
    // own to use as a label the way every OTHER segment below does
    // (`sourceLines_[start].text`, the matching line itself). A real
    // report + reference screenshot (=ah) found this shown as a
    // completely BLANK top row instead -- this was left as
    // std::string() (empty) here from the very first CUT port, and the
    // gap was flagged as far back as this session's item ㉜/㊱ ("最上行
    // の「--」...が無い") but never actually fixed until now.
    if (matchLines.front() > 0) segments_.push_back({"--", 0, matchLines.front()});
    for (std::size_t i = 0; i < matchLines.size(); i++) {
      int start = matchLines[i];
      int end = (i + 1 < matchLines.size()) ? matchLines[i + 1] : total;
      std::string label = (start < total) ? sourceLines_[start].text : std::string();
      segments_.push_back({label, start, end});
    }
  }

  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(segments_.size());
  for (const auto &seg : segments_) viewLines.push_back({seg.label, std::nullopt, std::nullopt, {}});
  view_.setLines(std::move(viewLines));
  view_.setCursorIndex(0);
  view_.redraw();
  return true;
}

const std::string &CutMode::pattern() const { return pattern_; }
const std::vector<CutSegment> &CutMode::segments() const { return segments_; }

const CutSegment *CutMode::selectedSegment() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(segments_.size())) return nullptr;
  return &segments_[idx];
}

std::vector<StyledLine> CutMode::segmentContent(const CutSegment &segment) const {
  std::vector<StyledLine> out;
  int end = std::min(segment.endLine, static_cast<int>(sourceLines_.size()));
  for (int i = std::max(segment.startLine, 0); i < end; i++) out.push_back(sourceLines_[i]);
  return out;
}

bool CutMode::splitSelected(const std::string &pattern, GrepSyntax syntax) {
  const CutSegment *selected = selectedSegment();
  if (!selected) return false;
  return pushSplit(segmentContent(*selected), pattern, syntax);
}

bool CutMode::pushSplit(const std::vector<StyledLine> &sourceLines, const std::string &pattern, GrepSyntax syntax) {
  auto saved = std::make_shared<SavedState>();
  saved->pattern = pattern_;
  saved->sourceLines = sourceLines_;
  saved->segments = segments_;
  saved->cursorIndex = view_.cursorIndex();

  ModeStackFrame frame;
  frame.cutBuffer = saved;  // opaque handle, matching ModeStackFrame's CUT::buff mapping
  modeStack_.push(std::move(frame));

  if (!split(sourceLines, pattern, syntax)) {
    modeStack_.pop();  // undo the push, matching DirectoryMode::openSelected()'s failure-undo pattern
    return false;
  }
  return true;
}

bool CutMode::goBack() {
  if (modeStack_.empty()) return false;
  auto frame = modeStack_.pop();
  if (!frame || !frame->cutBuffer) return false;
  auto saved = std::static_pointer_cast<SavedState>(frame->cutBuffer);

  pattern_ = saved->pattern;
  sourceLines_ = saved->sourceLines;
  segments_ = saved->segments;

  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(segments_.size());
  for (const auto &seg : segments_) viewLines.push_back({seg.label, std::nullopt, std::nullopt, {}});
  view_.setLines(std::move(viewLines));
  view_.setCursorIndex(saved->cursorIndex);
  view_.redraw();
  return true;
}

bool CutMode::canGoBack() const { return !modeStack_.empty(); }

std::optional<std::string> CutMode::parentPattern() const {
  const ModeStackFrame *frame = modeStack_.top();
  if (!frame || !frame->cutBuffer) return std::nullopt;
  auto saved = std::static_pointer_cast<SavedState>(frame->cutBuffer);
  return saved->pattern;
}

}  // namespace fm
