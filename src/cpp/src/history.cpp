#include "history.hpp"

#include <algorithm>
#include <limits>

#include <cctype>
#include <cstdlib>

namespace fm {

namespace {

constexpr char kFormFeed = '\x0c';   // legacyのK_l: セクションラベルのマーカー
constexpr char kSubEof = '\x1a';     // legacyのK_z: ファイル終端の番兵
constexpr char kSpaceMark = '\x01';  // legacyのK_a: リテラルな空白を符号化する
constexpr char kCtrlMark = '\x0f';   // legacyのK_o: 符号化された制御バイトを導入する

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      if (start < text.size()) lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

std::vector<std::string> splitWhitespace(const std::string &s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    if (i >= s.size()) break;
    std::size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) i++;
    out.push_back(s.substr(start, i - start));
  }
  return out;
}

// sub.cのsspc()からの移植: ディスク上では全てのフィールドが空白で
// 区切られる(プレーンな%sスタイルのトークン化でパースされる)ので、
// リテラルな空白はフォーマットを壊してしまう――代わりにkSpaceMark
// として符号化される。制御バイトは、kCtrlMarkに(バイト + '@')を
// 続けたものとして符号化される。このコードベースの他所で、制御
// バイトの値をプレーンテキストで可視/可逆にするために使われている
// のと同じ慣習。
//
// legacyからの逸脱: 空のフィールドは、単独のkCtrlMarkバイトへ符号化
// される(legacy自身の符号化がそれ以外では決して生み出さないトークン
// ――kCtrlMarkの他の全ての使用は、即座にもう1バイトが続く)。
// legacyのsscanf("%s ...")ベースの読み取り器は、空のフィールドを
// 全く表現できない: 本当に空のトークンは、空白区切りのトークン化の
// 下では完全に消え去り、それ以降の全フィールドを黙って1つ左へ
// ずらしてしまう。それは元のフォーマットにある実際の、潜在的な
// 脆弱性(str1/argv0、CUTバッファのフィールドは、実際には日常的に
// 空である)であり、この移植は再現する必要が無い。符号化/復号化の
// 両方がこの同じクラスによって所有されているため。
std::string encodeField(const std::string &s) {
  if (s.empty()) return std::string(1, kCtrlMark);
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (c && c < ' ') {
      out.push_back(kCtrlMark);
      out.push_back(static_cast<char>(c + '@'));
    } else if (c == ' ') {
      out.push_back(kSpaceMark);
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

// sub.cのrspc()に、上の空フィールドの番兵を加えたものからの移植。
std::string decodeField(const std::string &s) {
  if (s.size() == 1 && s[0] == kCtrlMark) return "";
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    if (c == static_cast<unsigned char>(kCtrlMark) && i + 1 < s.size()) {
      out.push_back(static_cast<char>(static_cast<unsigned char>(s[i + 1]) - '@'));
      i += 2;
    } else if (c == static_cast<unsigned char>(kSpaceMark)) {
      out.push_back(' ');
      i++;
    } else {
      out.push_back(static_cast<char>(c));
      i++;
    }
  }
  return out;
}

// legacyの数値による文字コードコード(usilic.h: C_ascii=300、
// C_bin=301、C_sjis=302、C_euc=303、C_jis=304、...、C_utf8=310)、
// トレーラーの3番目のフィールドにのみ使われる。
Charset::Encoding decodeCharsetCode(int code) {
  switch (code) {
    case 300:
      return Charset::Encoding::Ascii;
    case 301:
      return Charset::Encoding::Binary;
    case 302:
      return Charset::Encoding::Sjis;
    case 303:
      return Charset::Encoding::Euc;
    case 304:
      return Charset::Encoding::Jis;
    case 310:
      return Charset::Encoding::Utf8;
    default:
      // 0(フィールドが無い/このトレーラーフィールドより前の形式)
      // または他の何らかの認識できない値: 文書化されたデフォルトで
      // あり、Eucでは決してない――クラスコメントのCLAUDE.mdの
      // 履歴項目13参照。
      return Charset::Encoding::Utf8;
  }
}

int encodeCharsetCode(Charset::Encoding encoding) {
  switch (encoding) {
    case Charset::Encoding::Ascii:
      return 300;
    case Charset::Encoding::Binary:
      return 301;
    case Charset::Encoding::Sjis:
      return 302;
    case Charset::Encoding::Euc:
      return 303;
    case Charset::Encoding::Jis:
      return 304;
    case Charset::Encoding::Utf8:
      return 310;
  }
  return 310;
}

bool isSectionBoundary(const std::string &line) {
  return line.empty() || line[0] == kFormFeed || line[0] == kSubEof;
}

}  // namespace

History::History(int retentionCount) : retentionCount_(retentionCount) {}

std::optional<History::CursorPosition> History::positionFor(PositionCategory category,
                                                              const std::string &name) const {
  auto catIt = positions_.find(category);
  if (catIt == positions_.end()) return std::nullopt;
  auto it = catIt->second.find(name);
  if (it == catIt->second.end()) return std::nullopt;
  return it->second;
}

void History::setPosition(PositionCategory category, const std::string &name, CursorPosition position) {
  journal_.emplace_back(
      [category, name, position](History &h) { h.setPositionImpl(category, name, position); });
  setPositionImpl(category, name, position);
}

void History::setPositionImpl(PositionCategory category, const std::string &name, CursorPosition position) {
  positions_[category][name] = position;
}

bool History::containsName(NameSetCategory category, const std::string &name) const {
  auto it = nameSets_.find(category);
  if (it == nameSets_.end()) return false;
  for (const auto &n : it->second)
    if (n == name) return true;
  return false;
}

void History::rememberName(NameSetCategory category, const std::string &name) {
  journal_.emplace_back([category, name](History &h) { h.rememberNameImpl(category, name); });
  rememberNameImpl(category, name);
}

void History::rememberNameImpl(NameSetCategory category, const std::string &name) {
  auto &names = nameSets_[category];
  for (const auto &n : names)
    if (n == name) return;  // 最初の出現優先、save_str()からの移植
  names.push_back(name);
}

const std::vector<std::string> &History::namesIn(NameSetCategory category) const {
  static const std::vector<std::string> kEmpty;
  auto it = nameSets_.find(category);
  return (it != nameSets_.end()) ? it->second : kEmpty;
}

void History::setOnRingPushed(std::function<void(RingCategory)> callback) { onRingPushed_ = std::move(callback); }

void History::pushRecent(RingCategory category, const std::string &name) {
  journal_.emplace_back([category, name](History &h) { h.pushRecentImpl(category, name); });
  pushRecentImpl(category, name);
  journal_.emplace_back([category, name](History &h) { h.mirrorToUsilicImpl(category, name); });
  mirrorToUsilicImpl(category, name);
  if (onRingPushed_) onRingPushed_(category);
}

void History::mirrorToUsilicImpl(RingCategory source, const std::string &name) {
  std::string entry;
  switch (source) {
    case RingCategory::RecentFile:
      entry = name;
      break;
    case RingCategory::RecentDirectory:
      entry = name;
      // 外部起動の印付き("パス/ (usilic)")は、"/"を印の前に付けて記録済み。
      if (!hasUsilicTag(entry) && (entry.empty() || entry.back() != '/')) entry += '/';
      break;
    case RingCategory::RecentManPage:
      entry = name + " (man)";
      break;
    default:
      return;
  }
  auto &ring = rings_[RingCategory::Usilic];
  if (!ring.empty() && ring.back() == entry) return;
  ring.push_back(entry);
  // 3種(FILE/DIR/MAN)を1本にまとめるので、保持数もそれに合わせて3倍にする。
  if (ring.size() > limitFor(RingCategory::Usilic)) ring.pop_front();
}

void History::pushRecentImpl(RingCategory category, const std::string &name) {
  // 訂正(2026-08-04): sub.cのfind_hist()をより注意深く読み直すと、
  // それが`name`をリストの「最後の」ノードとだけ比較しており(走査
  // `for (...; hist->next; hist = hist->next)`はまさにそこで止まる)、
  // 全てのエントリとではないことが分かる――そのためsave_history()の
  // 重複排除は、単一の最も最近追加されたエントリの「直接の」繰り返し
  // だけを抑制し、それより古いものへの再訪問は抑制しない。ユーザーに
  // よる明確化(2026-08-04、^R自身の「HISTORY」時系列表示の文脈で)が、
  // これが実際に重要であることを確認した: (直前のものではなく)より
  // 「古い」エントリへの再訪問は、重複も何もかも含めて本当に再度
  // 追記されることが期待される――例えばAにアクセスし、次にB、次に
  // 再びAにアクセスすると、生のログは[A, B]ではなく[A, B, A]になる。
  // この関数の以前のバージョン(リング全体での、最初の出現優先の
  // 重複排除)は、まさにこの詳細の誤った移植だった。全域的な「一意な
  // 名前のみ」のビュー(例えばApplication::startListBrowser()の
  // ソートされたFILE/DIRピッカー)は、このリングが決して繰り返しを
  // 含まないことに頼るのではなく、今は表示時に自力で重複排除を行う。
  auto &ring = rings_[category];
  if (!ring.empty() && ring.back() == name) return;
  // 2026-10-17、直接のユーザー報告("^J^T^Rの履歴一覧には同じ内容は登録しない"): TOOL・SHELLで実行した
  // コマンドの履歴は、同じものが前にあればそれを消してから末尾へ積む(実行順の一覧に同じ行が並ばない)。
  if (category == RingCategory::ShellCommand) ring.erase(std::remove(ring.begin(), ring.end(), name), ring.end());
  ring.push_back(name);
  if (ring.size() > limitFor(category)) ring.pop_front();

  // 2026-08-27の追加、直接のユーザー報告("旧では同時にDIR履歴に
  // も所在地記載がなされる...^Tに限らずLOCATE、DUその他（ls）経由でな
  // いファイルアクセスであっても在籍値のDIR履歴同時記帳は反映して欲
  // しい"): legacyは、ファイルがどのように到達されたかに関わらず、
  // ファイル自身を記録するのと同時にそのファイル自身の含む
  // ディレクトリをDIR履歴に記録する――通常の`ls`の後にEnterする
  // 閲覧は既に別途RecentDirectoryに到達している(DirectoryMode::
  // enterDirectory()自身のpushRecent()呼び出しが、その場合はこれより
  // 「前に」発火するので、上の重複排除チェックはここでは単に何も
  // しない)が、^Tの固定ファイルエントリ、LOCATE結果等は、
  // enterDirectory()を全く通らない――それら自身の含むディレクトリは
  // 一度も記録されなかったので、ユーザーがあらゆる意味のある意味で
  // まさにそのディレクトリに「いた」にも関わらず、
  // goToRecentDirectory()(^Gの空スタックのフォールバック)自身の
  // 走査には見つけるものが何も無かった。この同じ関数へ再帰する
  // (単なるrings_[...]へのpushではなく)ことで、派生したディレクトリ
  // のpushに対しても、上の同一の重複排除/保持上限ロジックが再利用
  // される。
  // 2026-10-10: 外部起動の印付き(kUsilicTag)は子のusilicで開いたもので、このusilicはその親ディレクトリに
  // いたわけではないので、DIRへは足さない。
  if (category == RingCategory::RecentFile && !hasUsilicTag(name)) {
    // 2026-09-14修正、実際の再現(TUTORIALの真っ新起動時の事前登録
    // エントリを検証中に発覚): application.cpp自身のbuildCutFileHistoryEntry()
    // が積む「パス "パターン"」/「パス /パターン/」という合成形の
    // 場合、上のslash導出をそのまま`name`(合成文字列そのもの)に
    // 適用すると、スラッシュ区切り(GrepSyntax::Regex)ではその末尾の
    // 閉じスラッシュ自身が「最後の'/'」になってしまい、導出される
    // 「ディレクトリ」が本当のパスの親ではなく合成文字列全体に化ける
    // (引用符区切りのFixedはパターン自身にスラッシュを含まない限り
    // 偶然無事だった)。parseCutFileHistoryEntry()自身のコメントと
    // 同じ最も単純な形式チェック(末尾が`"`/`/`で閉じ、その直前に
    // ` "`/` /`が見つかる)で合成形を検出し、本当のファイルパス部分
    // だけをここでのスラッシュ探索の対象にする――Historyクラス自体は
    // application.cpp側のGrepSyntax等を一切知らないので、完全な
    // パース(parseCutFileHistoryEntry()自身)は複製せず、ディレクトリ
    // 導出に必要な最小限(パス部分の切り出し)だけをここに持つ。
    std::string pathPart = name;
    if (pathPart.size() >= 4) {
      char delim = pathPart.back();
      if (delim == '"' || delim == '/') {
        std::string openMarker = std::string(" ") + delim;
        std::string::size_type openPos = pathPart.rfind(openMarker, pathPart.size() - 2);
        if (openPos != std::string::npos) pathPart = pathPart.substr(0, openPos);
      }
    }
    std::string::size_type slash = pathPart.find_last_of('/');
    if (slash != std::string::npos) pushRecentImpl(RingCategory::RecentDirectory, pathPart.substr(0, slash + 1));
  }
}

const std::deque<std::string> &History::recentEntries(RingCategory category) const {
  static const std::deque<std::string> kEmpty;
  auto it = rings_.find(category);
  return (it != rings_.end()) ? it->second : kEmpty;
}

int History::retentionCount() const { return retentionCount_; }

void History::setRetentionCount(int count) {
  retentionCount_ = std::max(0, count);
  for (auto &kv : rings_) {
    while (kv.second.size() > limitFor(kv.first)) kv.second.pop_front();
  }
}

// 種類ごとの保持数の上限。0(無制限、2026-09-25からの既定)なら上限なし。
// USILICはFILE/DIR/MANの3種を1本にまとめるので3倍。
std::size_t History::limitFor(RingCategory category) const {
  if (retentionCount_ <= 0) return std::numeric_limits<std::size_t>::max();
  return static_cast<std::size_t>(category == RingCategory::Usilic ? retentionCount_ * 3 : retentionCount_);
}

bool History::smoothScrollEnabled() const { return smoothScrollEnabled_; }
void History::setSmoothScrollEnabled(bool enabled) {
  journal_.emplace_back([enabled](History &h) { h.setSmoothScrollEnabledImpl(enabled); });
  setSmoothScrollEnabledImpl(enabled);
}
void History::setSmoothScrollEnabledImpl(bool enabled) { smoothScrollEnabled_ = enabled; }

bool History::modeLineVisible() const { return modeLineVisible_; }
void History::setModeLineVisible(bool visible) {
  journal_.emplace_back([visible](History &h) { h.setModeLineVisibleImpl(visible); });
  setModeLineVisibleImpl(visible);
}
void History::setModeLineVisibleImpl(bool visible) { modeLineVisible_ = visible; }

bool History::epubSansSerif() const { return epubSansSerif_; }
void History::setEpubSansSerif(bool sansSerif) {
  journal_.emplace_back([sansSerif](History &h) { h.setEpubSansSerifImpl(sansSerif); });
  setEpubSansSerifImpl(sansSerif);
}
void History::setEpubSansSerifImpl(bool sansSerif) { epubSansSerif_ = sansSerif; }

int History::lsMode() const { return lsMode_; }
void History::setLsMode(int mode) {
  journal_.emplace_back([mode](History &h) { h.setLsModeImpl(mode); });
  setLsModeImpl(mode);
}
void History::setLsModeImpl(int mode) { lsMode_ = mode; }

std::optional<Charset::Encoding> History::savedCharset() const { return savedCharset_; }
void History::setSavedCharset(Charset::Encoding encoding) {
  journal_.emplace_back([encoding](History &h) { h.setSavedCharsetImpl(encoding); });
  setSavedCharsetImpl(encoding);
}
void History::setSavedCharsetImpl(Charset::Encoding encoding) { savedCharset_ = encoding; }

History::GuiPreferences History::guiPreferences() const { return guiPreferences_; }
void History::setGuiPreferences(const GuiPreferences &preferences) {
  journal_.emplace_back([preferences](History &h) { h.setGuiPreferencesImpl(preferences); });
  setGuiPreferencesImpl(preferences);
}
void History::setGuiPreferencesImpl(const GuiPreferences &preferences) { guiPreferences_ = preferences; }

const History::SessionState &History::sessionState() const { return sessionState_; }
void History::setSessionState(SessionState state) {
  journal_.emplace_back([state](History &h) { h.setSessionStateImpl(state); });
  setSessionStateImpl(std::move(state));
}
void History::setSessionStateImpl(SessionState state) { sessionState_ = std::move(state); }

// replayJournalOnto()自身の宣言コメント参照。
void History::replayJournalOnto(History &target) const {
  for (const auto &op : journal_) op(target);
}

bool History::isComplete() const { return complete_; }
const std::vector<std::string> &History::errors() const { return errors_; }

namespace {

struct PositionSectionSpec {
  std::string label;
  PositionCategory category;
};
struct NameSetSectionSpec {
  std::string label;
  NameSetCategory category;
};
struct RingSectionSpec {
  std::string label;
  RingCategory category;
};

}  // namespace

bool History::loadHistoryText(const std::string &text, bool environmentCharsetExplicitlySet) {
  complete_ = false;
  errors_.clear();
  auto lines = splitLines(text);
  std::size_t i = 0;

  while (i < lines.size() && (lines[i].empty() || lines[i][0] == '#')) i++;

  std::string bufferLabel = std::string(1, kFormFeed) + " Current buffer";
  if (i >= lines.size() || lines[i] != bufferLabel) {
    errors_.push_back("missing 'Current buffer' marker");
    return false;
  }
  i++;
  if (i >= lines.size()) {
    errors_.push_back("truncated after 'Current buffer' marker");
    return false;
  }
  {
    auto toks = splitWhitespace(lines[i]);
    SessionState state;
    if (toks.size() > 0) state.previousDirectory = decodeField(toks[0]);
    if (toks.size() > 1) state.currentDirectory = decodeField(toks[1]);
    if (toks.size() > 2) state.modeLineArg = decodeField(toks[2]);
    if (toks.size() > 3) state.previousScreenMarker = decodeField(toks[3]);
    if (toks.size() > 4) state.lastOpenName = decodeField(toks[4]);
    // toks[5]は、もし存在すればlegacyが常にリテラルの0として書き
    // 込み、読み取り時に捨てる予約フィールド――保存されない。
    if (toks.size() > 6) state.pendingCutBuffer = decodeField(toks[6]);
    if (toks.size() > 7) state.pendingCutProgram = decodeField(toks[7]);
    if (toks.size() > 8) state.epubVertical = toks[8] == "1";
    sessionState_ = std::move(state);
  }
  i++;

  auto expectLabel = [&](const std::string &label) -> bool {
    std::string full = std::string(1, kFormFeed) + label;
    if (i >= lines.size() || lines[i] != full) {
      errors_.push_back("expected section '" + label + "'" +
                         (i < lines.size() ? (", found: " + lines[i]) : " but reached end of file"));
      return false;
    }
    i++;
    return true;
  };

  auto readPositionSection = [&](PositionCategory category) {
    while (i < lines.size() && !isSectionBoundary(lines[i])) {
      auto toks = splitWhitespace(lines[i]);
      i++;
      if (toks.size() < 5) continue;
      CursorPosition pos;
      std::string name = decodeField(toks[0]);
      pos.row = std::atoi(toks[1].c_str());
      pos.totalRows = std::atoi(toks[2].c_str());
      int top = std::atoi(toks[3].c_str());
      pos.lineNumber = std::atoi(toks[4].c_str());
      if (pos.row < 0) pos.row = 0;
      if (top < 0) top = 0;
      if (pos.totalRows < 0) pos.totalRows = 0;
      if (pos.lineNumber == 0) pos.lineNumber = -1;
      pos.top = static_cast<unsigned>(top);
      positions_[category][name] = pos;
    }
  };
  auto readNameSetSection = [&](NameSetCategory category) {
    // replayJournalOnto()自身のコメント参照: ファイルからの読み込みは
    // 「このセッションが新たに行った変更」ではないので、journal_を
    // 汚さないよう公開rememberName()ではなくImpl版を直接呼ぶ
    // (readRingSection()が既にrings_を直接操作しているのと同じ理屈)。
    while (i < lines.size() && !isSectionBoundary(lines[i])) {
      rememberNameImpl(category, decodeField(lines[i]));
      i++;
    }
  };
  auto readRingSection = [&](RingCategory category) {
    // read_str()のリング履歴経路(save_history(..., f_find=0))から
    // の移植: エントリはファイル順でpushされ、保持上限は適用される
    // が、稼働中のpush時の重複排除チェックは無い(その後保持数が
    // 下げられた状態で書かれたファイル、または偶然の重複行を含む
    // ファイルは、上限までは書かれた通りにラウンドトリップする)
    // ――公開APIがなぜ重複排除を行い、このローダーが意図的にそれを
    // 再利用しないのかについてはpushRecent()自身のドキュメント
    // コメント参照。
    auto &ring = rings_[category];
    while (i < lines.size() && !isSectionBoundary(lines[i])) {
      // コマンドの履歴は同じものを1つだけ(pushRecentImpl()参照、前に積み上がった分も新しい方だけ残す)。
      if (category == RingCategory::ShellCommand)
        ring.erase(std::remove(ring.begin(), ring.end(), decodeField(lines[i])), ring.end());
      ring.push_back(decodeField(lines[i]));
      if (ring.size() > limitFor(category)) ring.pop_front();
      i++;
    }
  };

  static const std::vector<PositionSectionSpec> kLeadingPositions = {
      {" FILE", PositionCategory::File},
      {" DIR", PositionCategory::Directory},
      {" MAN", PositionCategory::ManPage},
  };
  for (const auto &spec : kLeadingPositions) {
    if (!expectLabel(spec.label)) return false;
    readPositionSection(spec.category);
  }

  static const std::vector<NameSetSectionSpec> kNameSets = {
      {" CUT", NameSetCategory::CutPattern},
      {" COMMAND", NameSetCategory::CommandModeLabel},
  };
  for (const auto &spec : kNameSets) {
    if (!expectLabel(spec.label)) return false;
    readNameSetSection(spec.category);
  }

  static const std::vector<RingSectionSpec> kRings = {
      {" HISTORY", RingCategory::ShellCommand},
      {" FILE_HISTORY", RingCategory::RecentFile},
      {" DIR_HISTORY", RingCategory::RecentDirectory},
      {" MAN_HISTORY", RingCategory::RecentManPage},
      {" LOCATE_HISTORY", RingCategory::RecentLocateQuery},
      {" FGREP_HISTORY", RingCategory::RecentFgrepPattern},
      {" EGREP_HISTORY", RingCategory::RecentEgrepPattern},
      {" USILIC_HISTORY", RingCategory::Usilic},
  };
  for (const auto &spec : kRings) {
    // USILIC_HISTORYはこのセクションを持たない旧形式のファイルも読めるよう
    // 省略可能とする(2026-09-21追加のセクションのため)。
    if (spec.category == RingCategory::Usilic &&
        (i >= lines.size() || lines[i] != std::string(1, kFormFeed) + spec.label))
      continue;
    if (!expectLabel(spec.label)) return false;
    readRingSection(spec.category);
  }

  if (!expectLabel(" MODE")) return false;
  readPositionSection(PositionCategory::Screen);

  if (!expectLabel(" USILIC")) return false;
  if (i >= lines.size()) {
    errors_.push_back("truncated at USILIC trailer");
    return false;
  }
  {
    auto toks = splitWhitespace(lines[i]);
    i++;
    int hs = 0, ls = 1, charsetCode = 0;
    if (toks.size() > 0) hs = std::atoi(toks[0].c_str());
    if (toks.size() > 1) ls = std::atoi(toks[1].c_str());
    if (toks.size() > 2) charsetCode = std::atoi(toks[2].c_str());
    smoothScrollEnabled_ = (hs != 0);
    lsMode_ = ls;
    // `if (!env_charset(-1)) vt_charset(...)`からの移植: 環境が既に
    // 明示的に優先していない場合にだけ保存された文字コードを適用
    // する。クラスコメント/CLAUDE.mdの履歴項目13参照。
    savedCharset_ = environmentCharsetExplicitlySet ? std::nullopt
                                                     : std::make_optional(decodeCharsetCode(charsetCode));
    // GuiPreferences(2026-08-24、項目427)――上の3つの元々の
    // フィールド(hs/ls/charsetCode、legacyのusilic自身のトレーラーから
    // 継承したディスク上のフォーマットの一部)の「間に」挿入する
    // のではなく「後に」追加されている。これにより、古い履歴
    // ファイル(または、原理上、このクラスが書いたファイルを読む
    // legacyのusilic自身)は、これらの追加トークンを単に無いものとして
    // 見る――下のtoks.size()によるガードは、上のcharsetCode自身の
    // トークン欠如時のデフォルトとまさに同じように、全ての
    // GuiPreferencesフィールドをそれ自身の「未設定」値(0/false)
    // にデフォルトさせる。
    GuiPreferences prefs;
    if (toks.size() > 3) prefs.fontPointSize = std::atoi(toks[3].c_str());
    if (toks.size() > 4) prefs.rows = std::atoi(toks[4].c_str());
    if (toks.size() > 5) prefs.columns = std::atoi(toks[5].c_str());
    if (toks.size() > 6) prefs.hasColors = std::atoi(toks[6].c_str()) != 0;
    if (toks.size() > 7) prefs.fgR = static_cast<unsigned char>(std::atoi(toks[7].c_str()));
    if (toks.size() > 8) prefs.fgG = static_cast<unsigned char>(std::atoi(toks[8].c_str()));
    if (toks.size() > 9) prefs.fgB = static_cast<unsigned char>(std::atoi(toks[9].c_str()));
    if (toks.size() > 10) prefs.bgR = static_cast<unsigned char>(std::atoi(toks[10].c_str()));
    if (toks.size() > 11) prefs.bgG = static_cast<unsigned char>(std::atoi(toks[11].c_str()));
    if (toks.size() > 12) prefs.bgB = static_cast<unsigned char>(std::atoi(toks[12].c_str()));
    // fontPath(2026-08-30、項目444)――同じ理由(古い履歴ファイルとの
    // 前方互換)で末尾に追加。encodeField()/decodeField()でリテラルな
    // 空白を含むパスも安全に往復できる。
    if (toks.size() > 13) prefs.fontPath = decodeField(toks[13]);
    guiPreferences_ = prefs;
    // modeLineVisible_(2026-09-12、項目⑫)――同じ理由(古い履歴
    // ファイルとの前方互換)でさらに末尾に追加。無い場合の既定は
    // 「表示」(旧来からの唯一の挙動と一致させる)。
    modeLineVisible_ = toks.size() > 14 ? std::atoi(toks[14].c_str()) != 0 : true;
    // terminalFontPath(2026-10-16)――同じ理由でさらに末尾に。
    if (toks.size() > 15) guiPreferences_.terminalFontPath = decodeField(toks[15]);
    // epubSansSerif_(2026-10-17)――同じ理由でさらに末尾に。無ければ明朝体。
    epubSansSerif_ = toks.size() > 16 && std::atoi(toks[16].c_str()) != 0;
  }

  complete_ = (i < lines.size() && !lines[i].empty() && lines[i][0] == kSubEof);
  if (!complete_) errors_.push_back("missing EOF sentinel -- file may be truncated");
  return true;
}

std::string History::serialize(const std::string &programName, const std::string &timestampText) const {
  std::string out;
  out += "# ~/." + programName + "/history: " + programName + "\n";
  // "E\bEX\bXI\bIT\bT"はlegacyのnroffスタイルのオーバーストライク
  // 太字のトリック(各文字がバックスペースを挟んで2重になっている)
  // であり、これによりオーバーストライク対応のページャーを通すと
  // 太字の「EXIT」として読める――ディスク上での互換性/馴染みの
  // ために保持されているが、このクラスやその読み取り器のどちらも
  // これに依存していない。
  out += std::string("# E\bEX\bXI\bIT\bT: ") + timestampText + "\n";
  out += "# Note: The lines begin with the character \\014 or \\032 mustn't be deleted.\n\n";

  out += std::string(1, kFormFeed) + " Current buffer\n";
  out += encodeField(sessionState_.previousDirectory) + " " + encodeField(sessionState_.currentDirectory) +
         " " + encodeField(sessionState_.modeLineArg) + " " +
         encodeField(sessionState_.previousScreenMarker) + " " + encodeField(sessionState_.lastOpenName) +
         " 0 " + encodeField(sessionState_.pendingCutBuffer) + " " +
         encodeField(sessionState_.pendingCutProgram) + (sessionState_.epubVertical ? " 1" : " 0") + "\n";

  auto writePositionSection = [&](const std::string &label, PositionCategory category) {
    out += std::string(1, kFormFeed) + label + "\n";
    auto it = positions_.find(category);
    if (it == positions_.end()) return;
    // ここでの走査順序は名前でソートされている(std::map)。legacy
    // 自身のディスク上の順序(そのSTR「書き込み順」ミラーリストの
    // 挿入順、そのミラーが空の場合にだけそのLIST BSTのソート順へ
    // フォールバックする)とは異なる――このフォーマットを読み戻す
    // 何ものも、セクション内のエントリ順序を気にしないので無害。
    // クラスコメント参照。
    for (const auto &[name, pos] : it->second) {
      out += encodeField(name) + " " + std::to_string(pos.row) + " " + std::to_string(pos.totalRows) + " " +
             std::to_string(pos.top) + " " + std::to_string(pos.lineNumber) + "\n";
    }
  };
  auto writeNameSetSection = [&](const std::string &label, NameSetCategory category) {
    out += std::string(1, kFormFeed) + label + "\n";
    auto it = nameSets_.find(category);
    if (it == nameSets_.end()) return;
    for (const auto &name : it->second) out += encodeField(name) + "\n";
  };
  auto writeRingSection = [&](const std::string &label, RingCategory category) {
    out += std::string(1, kFormFeed) + label + "\n";
    auto it = rings_.find(category);
    if (it == rings_.end()) return;
    for (const auto &name : it->second) out += encodeField(name) + "\n";  // 古い順..新しい順、ファイル順に一致
  };

  writePositionSection(" FILE", PositionCategory::File);
  writePositionSection(" DIR", PositionCategory::Directory);
  writePositionSection(" MAN", PositionCategory::ManPage);
  writeNameSetSection(" CUT", NameSetCategory::CutPattern);
  writeNameSetSection(" COMMAND", NameSetCategory::CommandModeLabel);
  writeRingSection(" HISTORY", RingCategory::ShellCommand);
  writeRingSection(" FILE_HISTORY", RingCategory::RecentFile);
  writeRingSection(" DIR_HISTORY", RingCategory::RecentDirectory);
  writeRingSection(" MAN_HISTORY", RingCategory::RecentManPage);
  writeRingSection(" LOCATE_HISTORY", RingCategory::RecentLocateQuery);
  writeRingSection(" FGREP_HISTORY", RingCategory::RecentFgrepPattern);
  writeRingSection(" EGREP_HISTORY", RingCategory::RecentEgrepPattern);
  writeRingSection(" USILIC_HISTORY", RingCategory::Usilic);
  writePositionSection(" MODE", PositionCategory::Screen);

  out += std::string(1, kFormFeed) + " USILIC\n";
  int charsetCode = savedCharset_ ? encodeCharsetCode(*savedCharset_) : 0;
  // GuiPreferencesのフィールドはcharsetCodeの後に追加されている
  // ――理由(単なる対称性ではなく、ディスク上のフォーマット互換性)
  // についてはloadHistoryText()自身の対応するコメント参照。
  out += std::to_string(smoothScrollEnabled_ ? 1 : 0) + " " + std::to_string(lsMode_) + " " +
         std::to_string(charsetCode) + " " + std::to_string(guiPreferences_.fontPointSize) + " " +
         std::to_string(guiPreferences_.rows) + " " + std::to_string(guiPreferences_.columns) + " " +
         std::to_string(guiPreferences_.hasColors ? 1 : 0) + " " + std::to_string(guiPreferences_.fgR) + " " +
         std::to_string(guiPreferences_.fgG) + " " + std::to_string(guiPreferences_.fgB) + " " +
         std::to_string(guiPreferences_.bgR) + " " + std::to_string(guiPreferences_.bgG) + " " +
         std::to_string(guiPreferences_.bgB) + " " + encodeField(guiPreferences_.fontPath) + " " +
         std::to_string(modeLineVisible_ ? 1 : 0) + " " + encodeField(guiPreferences_.terminalFontPath) + " " +
         std::to_string(epubSansSerif_ ? 1 : 0) + "\n";
  out += std::string(1, kSubEof) + "\n";
  return out;
}

}  // namespace fm
