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

  // 2026-09-05追加(直接のユーザー依頼、"新たにGREP（grep）を新設"):
  // GrepSyntax::Basic(プレーンなgrep)を含む3択――application.cpp自身の
  // grepSyntaxRole()と同じ対応付け(重複を避けるためここでも同じ
  // switchを持つ、grep_mode.cppはapplication.cppのファイルローカルな
  // 匿名namespaceヘルパーを呼べないため)。
  ConfiguredRole role = ConfiguredRole::Grep;
  if (syntax == GrepSyntax::Fixed) role = ConfiguredRole::Fgrep;
  else if (syntax == GrepSyntax::Regex) role = ConfiguredRole::Egrep;
  ProgramCommand cmd = configuration_.commandFor(role);
  if (cmd.argv.empty()) return false;

  std::string inputText;
  for (const auto &line : sourceLines) {
    inputText += line;
    inputText += '\n';
  }

  // 2026-08-23追加(docs/CHANGES.md 407番、実際のユーザー報告: FGREPで
  // 「口座テスト」は見つかったが、「同じ」ディレクトリ一覧に対して
  // 「マイナカード」は見つからなかった)――実際のファイル名のバイト
  // 列が、通常のIMEが何を入力するかに関わらずどちらのUnicode正規化
  // 形式にもなりうる理由についてはunicodeNormalizationCandidates()
  // 自身のコメント参照。入力された通りの`pattern`を常に「最初に」
  // 試す(そうすることで、既に一致する検索には余計なコストが掛から
  // ず、さらに重要な点として、下の`pattern_`/履歴がどちらの場合でも
  // ユーザーが実際に入力した通りを記録し続ける)。文字通りのバイト
  // 列だけでは一致しない場合にのみ、NFC/NFDをフォールバックとして
  // 試す。
  ChildProcess::Options options;
  options.input = &inputText;
  ChildProcess::Result result;
  bool ranAtLeastOnce = false;
  for (const auto &candidate : unicodeNormalizationCandidates(pattern)) {
    std::vector<std::string> argv = cmd.argv;
    argv.push_back(candidate);
    result = ChildProcess::run(argv, options);
    ranAtLeastOnce = true;
    // 終了コード0=一致あり、終了コード1=正常に実行されたが一致無し
    // ――どちらも「実行」としては成功(検索として成功とは限らない:
    // 終了コード1の場合は、あれば次の候補へフォールスルーする)。
    // それ以外(不正なパターン、exec失敗、シグナルによる終了)は本物の
    // 失敗――既に全く実行できないと分かっているコマンドで残りの候補を
    // 試すのではなく、直ちに諦める。
    if (result.exitStatus < 0 || result.exitStatus > 1) return false;
    if (result.exitStatus == 0) break;  // この候補で一致が見つかった――ここで終了
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
  // 結果0件時のプレースホルダ(2026-08-12、2回のユーザー訂正を経た
  // もの――経緯全体はContentView::setEmptyPlaceholder()自身のコメント
  // 参照): "<pattern>: No String"、下線が付くのは"No String"部分
  // だけ(パターン部分の接頭辞には付かない)で、反転表示の「選択済み」
  // 扱いは一切行わない(それだと「これは選択可能な文字列だ」という
  // 見た目になってしまうが、実際はそうではない)。検索のたびに新しく
  // 設定する――legacyには「新しい検索が走った後も、最後に何も
  // 見つからなかったパターン」が居座り続けるという概念は無く、
  // ここでもそうすべきではない。
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
