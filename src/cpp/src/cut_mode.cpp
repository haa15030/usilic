#include <utility>
#include "cut_mode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>

#include "child_process.hpp"
#include "unicode_normalize.hpp"

namespace fm {

namespace {
// grep_mode.cpp自身のconvertTabsForSearch()と全く同じもの――クラス
// コメントが既に説明している「GrepModeへの依存を持つ代わりにマッチング
// ロジックを重複させている」という同じ方針にならい、ここでも複製する。
// PipeBuffer::wrapLine()が検索対象側の全タブをU+2060(WORD JOINER)+
// スペースへ変換するようになったことに合わせ、CUTのパターン側の生の
// タブバイト(^Q^Iで打ち込まれたもの)も同じU+2060へ変換してから照合
// する。pattern_(履歴/表示用)は変換前のまま。
// 2026-10-18、直接のユーザー報告(HELPの"^O^J^S^Q^I$^Mがそのとおりにならない"): 行の側ではU+2060の後ろに
// 桁をそろえる空白が続くので、正規表現(EGREP・GREP)では「U+2060と続く空白」に一致させる(^I$が行末のタブに
// 一致するように)。^I+のような繰り返しも1つのタブとして効くよう、括弧でまとめる。固定文字列はU+2060だけ。
std::string convertTabsForSearch(const std::string &pattern, GrepSyntax syntax) {
  const std::string replacement = (syntax == GrepSyntax::Regex)   ? "(\xE2\x81\xA0 *)"
                                  : (syntax == GrepSyntax::Basic) ? "\\(\xE2\x81\xA0 *\\)"
                                                                  : "\xE2\x81\xA0";
  // [^\t]のような角括弧の中は1文字の集まりなので、U+2060だけにする。
  std::string converted;
  bool inBracket = false;
  for (std::size_t i = 0; i < pattern.size(); i++) {
    char c = pattern[i];
    if (c == '\t') {
      converted += inBracket ? std::string("\xE2\x81\xA0") : replacement;
      continue;
    }
    converted += c;
    if (c == '\\' && !inBracket && i + 1 < pattern.size()) {
      converted += pattern[++i];
    } else if (c == '[' && !inBracket) {
      inBracket = true;
      if (i + 1 < pattern.size() && pattern[i + 1] == '^') converted += pattern[++i];
      if (i + 1 < pattern.size() && pattern[i + 1] == ']') converted += pattern[++i];  // 先頭の]は文字
    } else if (c == ']' && inBracket) {
      inBracket = false;
    }
  }
  return converted;
}

// 2026-09-15追加、HELP(kHelpPattern、application.cpp参照)が「行末の
// タブ1つ」を見出し行の分割点にする以上の必然的な帰結: PipeBuffer::
// wrapLine()はそのタブをU+2060(WORD JOINER、幅0)+次のタブストップ
// までのスペースへ展開する(detectOverstrike_有効時)ので、マッチした
// 行そのもの(sourceLines_[start].text)をそのままラベルにすると、
// 画面には見えない末尾の空白(最大8桁分)がラベル文字列自体には残って
// しまう――CUT一覧でこの区間にカーソルが乗った時の反転表示が、
// 実際に見えているテキストより最大8桁分右へ余分に伸びる(このラベルは
// 元々パス末尾ではなく行全体なので、feedbyte等での意図的な右パディング
// とは違い、正当化する理由が無い――「表示幅の切り詰め/パディング
// 判断基準」の方針通り)。行末のタブ以外の理由でU+2060がラベルに
// 混じることは無い(通常のCUT検索文字列がU+2060を含む入力を打てる
// 経路は無い)ので、末尾のスペース→直前のU+2060(あれば)という順で
// 無条件に切り詰めて構わない。
std::string stripTrailingTabPadding(std::string label) {
  constexpr const char *kTabMarker = "\xE2\x81\xA0";  // U+2060 WORD JOINER
  std::size_t end = label.size();
  while (end > 0 && label[end - 1] == ' ') end--;
  if (end >= 3 && label.compare(end - 3, 3, kTabMarker) == 0) end -= 3;
  label.erase(end);
  return label;
}
}  // namespace

CutMode::CutMode(ContentView &view, ModeStack &modeStack, const Configuration &configuration)
    : view_(view), modeStack_(modeStack), configuration_(configuration) {}

namespace {
// 2026-09-14修正、直接のユーザー依頼("CUTは一律grep仕様。Fixedは廃止。"):
// 以前はFixed(FGREP)以外を一律Egrep(EGREP)扱いする2択だったが、
// application.cpp自身のgrepSyntaxRole()と同じ3択(Fixed→FGREP、
// Regex→EGREP、Basic→GREP)へ揃えた――CUTの実際の呼び出し元
// (Application::startCutPrompt()/startCutFilePrompt())はもう
// GrepSyntax::Basicしか渡さないが、CutMode自体はFILE履歴の合成
// エントリ再生(openRecentFileEntry())等、他の値でも呼ばれ得る
// 汎用のクラスなので、判定自体は3値のまま正しく扱う。
ConfiguredRole cutSyntaxRole(GrepSyntax syntax) {
  switch (syntax) {
    case GrepSyntax::Fixed:
      return ConfiguredRole::Fgrep;
    case GrepSyntax::Regex:
      return ConfiguredRole::Egrep;
    case GrepSyntax::Basic:
      return ConfiguredRole::Grep;
  }
  return ConfiguredRole::Grep;
}
}  // namespace

bool CutMode::split(const std::vector<StyledLine> &sourceLines, const std::string &pattern,
                     GrepSyntax syntax) {
  labelHighlightEndMarker_ = std::exchange(nextLabelHighlightEndMarker_, std::string());
  labelHighlightable_ = std::exchange(nextLabelHighlightable_, nullptr);
  if (pattern.empty()) return false;
  const std::string searchPattern = convertTabsForSearch(pattern, syntax);

  ProgramCommand cmd = configuration_.commandFor(cutSyntaxRole(syntax));
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
  for (const auto &candidate : unicodeNormalizationCandidates(searchPattern)) {
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
  generation_++;  // generation()参照

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
      std::string label = (start < total) ? stripTrailingTabPadding(sourceLines_[start].text) : std::string();
      std::vector<AttributeRun> rubies;
      if (start < total)
        for (const auto &run : sourceLines_[start].attributes)
          if ((run.attribute == Display::Attribute::RubyBase || run.attribute == Display::Attribute::RubyText) &&
              run.start < static_cast<int>(label.size())) {
            AttributeRun clipped = run;
            clipped.end = std::min(clipped.end, static_cast<int>(label.size()));
            rubies.push_back(clipped);
          }
      segments_.push_back({label, start, end, std::move(rubies)});
    }
  }

  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(segments_.size());
  for (const auto &seg : segments_) viewLines.push_back(labelViewLine(seg));
  view_.setLines(std::move(viewLines));
  view_.setCursorIndex(0);
  view_.redraw();
  return true;
}

ContentView::Line CutMode::labelViewLine(const CutSegment &seg) const {
  ContentView::Line line{seg.label, std::nullopt, std::nullopt, seg.labelAttributes};
  if (!labelHighlightEndMarker_.empty()) {
    std::size_t at = seg.label.find(labelHighlightEndMarker_);
    if (at != std::string::npos) line.highlightEndColumn = static_cast<int>(at);
  }
  if (labelHighlightable_ && !labelHighlightable_(seg.label)) line.highlightEndColumn = 0;
  return line;
}

const std::string &CutMode::pattern() const { return pattern_; }
const std::vector<CutSegment> &CutMode::segments() const { return segments_; }

void CutMode::setSegmentLabel(std::size_t index, const std::string &label) {
  if (index >= segments_.size()) return;
  segments_[index].label = label;
  segments_[index].labelAttributes.clear();
  std::vector<ContentView::Line> viewLines = view_.lines();
  if (index < viewLines.size()) {
    viewLines[index].text = label;
    viewLines[index].attributes.clear();
    int cursor = view_.cursorIndex(), top = view_.topIndex();
    view_.setLines(std::move(viewLines));
    view_.setCursorIndex(cursor);
    view_.setTopIndex(top);
  }
}

void CutMode::dropLeadingFiller() {
  if (segments_.empty() || segments_.front().label != "--") return;
  segments_.erase(segments_.begin());
  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(segments_.size());
  for (const auto &seg : segments_) viewLines.push_back(labelViewLine(seg));
  view_.setLines(std::move(viewLines));
  view_.setCursorIndex(0);
}

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
  generation_++;  // generation()参照

  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(segments_.size());
  for (const auto &seg : segments_) viewLines.push_back(labelViewLine(seg));
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
