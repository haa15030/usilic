#include "grep_mode.hpp"

#include <cctype>
#include <cstdlib>
#include <regex.h>
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
  // 2026-09-10追加: 実際に一致した候補(NFC/NFDのどちらでヒットしたか、
  // unicodeNormalizationCandidates()自身のコメント参照)を覚えておく
  // ――下のmatchSpans算出の2回目の呼び出しで同じ文字列を渡すため。
  std::string winningCandidate = pattern;
  for (const auto &candidate : unicodeNormalizationCandidates(pattern)) {
    std::vector<std::string> argv = cmd.argv;
    argv.push_back(candidate);
    result = ChildProcess::run(argv, options);
    ranAtLeastOnce = true;
    winningCandidate = candidate;
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

  // 2026-09-10追加、直接のユーザー指摘("『マッチした文字列』だけを
  // ハイライト表示には"): 同じコマンド+勝った候補パターンに`-o`
  // (--only-matching)を足した2回目の呼び出しで、実際に一致した部分
  // 文字列そのものを得る――`-n`単独では行「全体」しか分からない。
  // `-o`は行ごとに複数マッチがあれば複数行の出力になる(同じ行番号が
  // 複数回現れる)。matches_自身もgrep自身の出力の行番号昇順のまま
  // なので、単純な2ポインタでこの出力と対応付けられる(mapは不要)。
  // 見つかった部分文字列テキストを、対応する行の`match.text`の中で
  // `find()`する(前回見つけた位置より後ろから探すことで、同じ行内の
  // 複数マッチ・同じ文字列の繰り返しにも対応する)――固定文字列/
  // 典型的な正規表現では実用上常に正しい対応が取れる。見つからない
  // 場合(まれ、UTF-8正規化の不一致等)は、そのマッチのmatchSpansが
  // 空のまま残る――呼び出し元(DirectoryMode/FileMode::
  // setGrepMatchHighlight())は、空の場合に行全体をハイライトする
  // フォールバックを持つ。
  if (!matches_.empty()) {
    std::vector<std::string> spanArgv = cmd.argv;
    spanArgv.insert(spanArgv.begin() + 1, "-o");
    spanArgv.push_back(winningCandidate);
    ChildProcess::Result spanResult = ChildProcess::run(spanArgv, options);
    if (spanResult.exitStatus == 0) {
      std::istringstream spanStream(spanResult.output);
      std::string spanLine;
      std::size_t matchIdx = 0;
      std::size_t searchFrom = 0;
      while (std::getline(spanStream, spanLine)) {
        std::size_t spanColon = spanLine.find(':');
        if (spanColon == std::string::npos || spanColon == 0) continue;
        std::string spanNumPart = spanLine.substr(0, spanColon);
        bool spanAllDigits = true;
        for (char c : spanNumPart)
          if (!std::isdigit(static_cast<unsigned char>(c))) spanAllDigits = false;
        if (!spanAllDigits) continue;
        int lineNum = std::atoi(spanNumPart.c_str()) - 1;
        std::string matchText = spanLine.substr(spanColon + 1);

        while (matchIdx < matches_.size() && matches_[matchIdx].sourceLineIndex < lineNum) {
          matchIdx++;
          searchFrom = 0;
        }
        if (matchIdx >= matches_.size() || matches_[matchIdx].sourceLineIndex != lineNum) continue;
        GrepMatch &target = matches_[matchIdx];
        std::size_t pos = target.text.find(matchText, searchFrom);
        if (pos == std::string::npos) continue;
        target.matchSpans.push_back({static_cast<int>(pos), static_cast<int>(pos + matchText.size())});
        searchFrom = pos + std::max<std::size_t>(matchText.size(), 1);
      }
    }
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

// searchInProcess()自身の宣言コメント参照(2026-09-10追加)。
// regcomp()に`REG_EXTENDED`を渡さない=POSIX BRE(grepのBasic構文と
// 同じ、GrepSyntax::Basicに揃えている――固定文字列/拡張正規表現の
// インクリメンタル版は今回のスコープ外)。1行内の複数マッチは
// `regexec()`をREG_NOTBOLを付けて前方から繰り返し呼ぶことで求める
// (search()自身の`-o`ベースの複数マッチ処理と同じ発想)。
bool GrepMode::searchInProcess(const std::vector<std::string> &sourceLines, const std::string &pattern) {
  if (pattern.empty()) return false;
  regex_t re;
  if (regcomp(&re, pattern.c_str(), 0) != 0) return false;

  matches_.clear();
  for (int i = 0; i < static_cast<int>(sourceLines.size()); i++) {
    const std::string &line = sourceLines[i];
    GrepMatch match;
    bool any = false;
    int offset = 0;
    while (offset <= static_cast<int>(line.size())) {
      regmatch_t pmatch[1];
      int flags = offset > 0 ? REG_NOTBOL : 0;
      if (regexec(&re, line.c_str() + offset, 1, pmatch, flags) != 0) break;
      int start = offset + static_cast<int>(pmatch[0].rm_so);
      int end = offset + static_cast<int>(pmatch[0].rm_eo);
      if (!any) {
        match.sourceLineIndex = i;
        match.text = line;
        any = true;
      }
      match.matchSpans.push_back({start, end});
      // ゼロ幅マッチ(例えば"$"のみ)で無限ループしないよう、必ず
      // 最低1バイトは前進する。
      offset = (end > offset) ? end : offset + 1;
    }
    if (any) matches_.push_back(std::move(match));
  }
  regfree(&re);

  pattern_ = pattern;
  if (matches_.empty()) {
    static const std::string kSuffix = "No String";
    std::string text = pattern_ + ": " + kSuffix;
    int underlineFrom = static_cast<int>(text.size() - kSuffix.size());
    view_.setEmptyPlaceholder(
        ContentView::Line{text, std::nullopt, std::nullopt,
                           {AttributeRun{underlineFrom, static_cast<int>(text.size()), Display::Attribute::Underline}}},
        /*marked=*/false);
  }
  // search()とは異なり、view_(一覧画面)のlines/cursorには一切触れ
  // ない――呼び出し元は一覧へは切り替えず、in-place方式で
  // matches()を直接読む(searchInProcess()自身の宣言コメント参照)。
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
