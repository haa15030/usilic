#include "grep_mode.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "child_process.hpp"
#include "unicode_normalize.hpp"

namespace fm {

namespace {
// 2026-09-10追加、直接のユーザー提案: PipeBuffer::wrapLine()が(検索
// 対象のバッファ側で)全てのタブをU+2060(WORD JOINER)+従来通りの
// スペースへ変換するようになった(そちら自身のコメント参照)ことに
// 合わせ、検索する側のパターンに含まれる生のタブバイト(^Q^Iで
// 打ち込まれたもの)も同じU+2060へ変換してから照合する。history_
// への記録やNo String表示に使う`pattern_`自体は(タブがそのまま
// 反転表示のグリフとして見える方が「何を探したか」が伝わりやすい
// ため)変換前のまま――変換は実際の照合にだけ使う一時コピーに施す。
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

// 2026-09-12追加、直接のユーザー報告("CUT一覧のために必要な行には
// タブコードを行末に入れてあるんだが、^Sで検索してもマゼンタハイ
// ライトが出ない"): convertTabsForSearch()/PipeBuffer::wrapLine()自身の
// コメント参照の通り、タブはU+2060(WORD JOINER、幅0)+それに続く
// スペースへ変換されて検索対象になる――マッチ自体はU+2060の3バイトに
// 対して正しく成立するが、そのバイト範囲だけをハイライトのAttributeRun
// にしても、U+2060自体は表示幅0なので端末上には色が乗る「セル」が
// 一つも存在せず、結果としてハイライトが実際には何も見えない
// (実機PTYで確認済み: マゼンタの前景色/背景色を指定するエスケープ
// シーケンス自体は正しく送出されているが、それを引き継ぐべき可視文字
// が無いまま即座にリセットされてしまう)。マッチ範囲の終端がちょうど
// このU+2060マーカーの直後に来る場合、そのタブが実際に「見た目上」
// 占めている(タブ展開由来の)半角スペースの連続へ、終端をそこまで
// 広げる――これにより、タブへのマッチが、その視覚的な表現である
// 空白の帯として実際に着色されるようになる。
int widenSpanPastTabMarker(const std::string &line, int end) {
  static const std::string kTabMarker = "\xE2\x81\xA0";  // U+2060 WORD JOINER
  if (end < 3 || static_cast<std::size_t>(end) > line.size()) return end;
  if (line.compare(end - 3, 3, kTabMarker) != 0) return end;
  int p = end;
  while (p < static_cast<int>(line.size()) && line[p] == ' ') p++;
  return p;
}
}  // namespace

GrepMode::GrepMode(ContentView &view, const Configuration &configuration)
    : view_(view), configuration_(configuration) {}

bool GrepMode::search(const std::vector<std::string> &sourceLines, const std::string &pattern,
                       GrepSyntax syntax) {
  if (pattern.empty()) return false;
  const std::string searchPattern = convertTabsForSearch(pattern, syntax);

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
  std::string winningCandidate = searchPattern;
  for (const auto &candidate : unicodeNormalizationCandidates(searchPattern)) {
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
        int spanEnd = widenSpanPastTabMarker(target.text, static_cast<int>(pos + matchText.size()));
        target.matchSpans.push_back({static_cast<int>(pos), spanEnd});
        searchFrom = pos + std::max<std::size_t>(matchText.size(), 1);
      }
    }
  }

  pattern_ = pattern;
  showMatches();
  return true;
}

void GrepMode::setMatches(std::vector<GrepMatch> matches, const std::string &label) {
  matches_ = std::move(matches);
  pattern_ = label;
  showMatches();
}

void GrepMode::showMatches() {
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
  // 2026-09-11追加、直接のユーザー依頼("次は同じ手順の検索結果画面で
  // マッチ文字列のusilicをマゼンタハイライトにして欲しい"): 従来
  // ずっと`{}`(属性無し)のままだった――in-place GREP(項目437以降、
  // Directory/File等の元画面にマッチをその場でハイライトする方式)
  // 導入時にContentView::setGrepMatchHighlight()という同種の仕組みが
  // 新設されたが、こちら(専用の一覧画面、FGREP/EGREP自身の従来からの
  // 表示形式)には一度も適用されていなかった。ロジック自体は
  // setGrepMatchHighlight()と全く同じ(GrepMatch::matchSpans自身の
  // コメント参照: 取得に失敗していれば行全体をハイライトするフォール
  // バック)だが、あちらは「既存の一覧に後からハイライトを重ねる/
  // 外す」ための、前回のスナップショットを保持する仕組みであり、
  // ここは検索のたびに`viewLines`をゼロから作り直すだけなので、
  // その場で直接`AttributeRun`を組み立てる方が単純――
  // sourceLineIndexによる後からの逆引きも不要(matches_の各要素が
  // viewLinesの対応する要素とそのまま1対1)。
  std::vector<ContentView::Line> viewLines;
  viewLines.reserve(matches_.size());
  for (const auto &m : matches_) {
    std::vector<AttributeRun> attrs;
    if (m.matchSpans.empty()) {
      attrs.push_back(AttributeRun{0, static_cast<int>(m.text.size()), Display::Attribute::Highlight});
    } else {
      for (const auto &span : m.matchSpans)
        attrs.push_back(AttributeRun{span.first, span.second, Display::Attribute::Highlight});
    }
    viewLines.push_back({m.text, std::nullopt, std::nullopt, std::move(attrs)});
  }
  view_.setLines(std::move(viewLines));
  view_.setCursorIndex(0);
  if (redrawOnSearch_) view_.redraw();
}

// searchInProcess()自身の宣言コメント参照(2026-09-10追加、同日さらに
// regex→fgrep相当のリテラル部分文字列検索へ修正): `std::string::
// find()`を前進させながら繰り返すだけの単純な実装。patternが非空である
// ことは呼び出し元でも保証済みだが、find()の戻り値をnposへ導く空文字列
// はここでも無限ループの余地が無い(findFromを必ずpattern.size()(>=1)
// 分前進させる)。
bool GrepMode::searchInProcess(const std::vector<std::string> &sourceLines, const std::string &pattern) {
  if (pattern.empty()) return false;
  // convertTabsForSearch()自身のコメント参照(search()と同じ理由)。
  const std::string searchPattern = convertTabsForSearch(pattern, GrepSyntax::Fixed);

  matches_.clear();
  for (int i = 0; i < static_cast<int>(sourceLines.size()); i++) {
    const std::string &line = sourceLines[i];
    GrepMatch match;
    bool any = false;
    std::size_t findFrom = 0;
    while (true) {
      std::size_t pos = line.find(searchPattern, findFrom);
      if (pos == std::string::npos) break;
      if (!any) {
        match.sourceLineIndex = i;
        match.text = line;
        any = true;
      }
      int spanEnd = widenSpanPastTabMarker(line, static_cast<int>(pos + searchPattern.size()));
      match.matchSpans.push_back({static_cast<int>(pos), spanEnd});
      findFrom = pos + searchPattern.size();
    }
    if (any) matches_.push_back(std::move(match));
  }

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
