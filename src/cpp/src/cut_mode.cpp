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

  // 2026-08-23追加(docs/CHANGES.md 407番): GrepMode::search()自身の
  // 全く同じ修正と同じ理屈――unicodeNormalizationCandidates()自身の
  // コメント参照。CUTの固定/正規表現分割は全く同じfgrep/egrepへ
  // シェルアウトするので、全く同じバイトリテラルマッチングの死角を
  // 抱えている。
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
    if (result.exitStatus == 0) break;  // この候補で一致が見つかった――ここで終了
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
  std::sort(matchLines.begin(), matchLines.end());  // grep -nは既に順序通りだが、念のためソートしておく

  pattern_ = pattern;
  sourceLines_ = sourceLines;
  segments_.clear();

  int total = static_cast<int>(sourceLines_.size());
  if (matchLines.empty()) {
    if (total > 0) segments_.push_back({std::string(), 0, total});
  } else {
    // "--"(2026-08-12修正、ContentView自身が既に確立している
    // 「空/ラベル無し領域」の慣習に合わせている――setEmptyPlaceholder()
    // 自身のコメント参照): この「先頭」区間(最初のマッチより前の
    // 全て、例えばディレクトリ一覧自身の"./"から最初の"package*"の
    // ヒットの直前の行まで)には、下の他の全ての区間のように
    // (`sourceLines_[start].text`、マッチした行そのもの)ラベルとして
    // 使える専用のデリミタ行が無い。実際の報告+参考スクリーンショット
    // (=ah)により、代わりに完全に「空白」の最上行として表示されて
    // いることが判明した――これは、CUTの最初の移植の時点からここで
    // std::string()(空)のまま残されており、このギャップはこの
    // セッションの項目㉜/㊱(「最上行の「--」...が無い」)まで遡って
    // 既に指摘されていたが、今この時まで実際には修正されていなかった。
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
  frame.cutBuffer = saved;  // 不透明なハンドル、ModeStackFrameのCUT::buffマッピングと同じ
  modeStack_.push(std::move(frame));

  if (!split(sourceLines, pattern, syntax)) {
    modeStack_.pop();  // pushを取り消す、DirectoryMode::openSelected()の失敗時取り消しパターンと同じ
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
