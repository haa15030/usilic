#include "history.hpp"

#include <cctype>
#include <cstdlib>

namespace fm {

namespace {

constexpr char kFormFeed = '\x0c';   // legacy K_l: section-label marker
constexpr char kSubEof = '\x1a';     // legacy K_z: end-of-file sentinel
constexpr char kSpaceMark = '\x01';  // legacy K_a: encodes a literal space
constexpr char kCtrlMark = '\x0f';   // legacy K_o: introduces an encoded control byte

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

// Ported from sub.c's sspc(): every field is whitespace-delimited on
// disk (parsed with plain %s-style tokenizing), so a literal space
// would corrupt the format -- encoded as kSpaceMark instead. Control
// bytes are encoded as kCtrlMark followed by (byte + '@'), the same
// convention used elsewhere in this codebase for making a control
// byte's value visible/reversible in plain text.
//
// Deviation from legacy: an empty field encodes to a lone kCtrlMark
// byte (a token legacy's own encoding never otherwise produces --
// every other use of kCtrlMark is immediately followed by one more
// byte). Legacy's sscanf("%s ...")-based reader simply cannot
// represent an empty field at all: a genuinely empty token vanishes
// entirely under whitespace-delimited tokenizing, silently shifting
// every subsequent field left by one. That's a real, latent fragility
// in the original format (str1/argv0, the CUT-buffer fields, are
// routinely empty in practice) that this port doesn't need to
// reproduce, since encode/decode are both owned by this same class.
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

// Ported from sub.c's rspc(), plus the empty-field sentinel above.
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

// Legacy numeric charset codes (tcb.h: C_ascii=300, C_bin=301,
// C_sjis=302, C_euc=303, C_jis=304, ..., C_utf8=310), used only for
// the trailer's third field.
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
      // 0 (field absent/predates this trailer field) or any other
      // unrecognized value: the documented default, never Euc -- see
      // the class comment on CLAUDE.md history item 13.
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
  auto &names = nameSets_[category];
  for (const auto &n : names)
    if (n == name) return;  // first-occurrence-wins, ported from save_str()
  names.push_back(name);
}

const std::vector<std::string> &History::namesIn(NameSetCategory category) const {
  static const std::vector<std::string> kEmpty;
  auto it = nameSets_.find(category);
  return (it != nameSets_.end()) ? it->second : kEmpty;
}

void History::pushRecent(RingCategory category, const std::string &name) {
  // Correction (2026-08-04): a closer re-read of sub.c's find_hist()
  // shows it only ever compares `name` against the LAST node of the
  // list (the walk `for (...; hist->next; hist = hist->next)` stops
  // exactly there), not every entry -- so save_history()'s dedup only
  // suppresses an IMMEDIATE repeat of the single most-recently-added
  // entry, not a re-visit of any older one. A user clarification
  // (2026-08-04, in the context of ^R's own "HISTORY" chronological
  // view) confirmed this matters in practice: revisiting an OLDER
  // entry (not the immediately-previous one) is expected to genuinely
  // re-append it, duplicates and all -- e.g. accessing A, then B, then
  // A again yields the raw log [A, B, A], not [A, B]. The earlier
  // version of this function (whole-ring, first-occurrence-wins dedup)
  // was a mis-port of this exact detail. Global "unique names only"
  // views (e.g. Application::startListBrowser()'s sorted FILE/DIR
  // picker) now do their own de-duplication at display time instead
  // of relying on this ring never containing repeats.
  auto &ring = rings_[category];
  if (!ring.empty() && ring.back() == name) return;
  ring.push_back(name);
  if (static_cast<int>(ring.size()) > retentionCount_) ring.pop_front();

  // 2026-08-27 addition, a direct user report ("旧では同時にDIR履歴に
  // も所在地記載がなされる...^Tに限らずLOCATE、DUその他（ls）経由でな
  // いファイルアクセスであっても在籍値のDIR履歴同時記帳は反映して欲
  // しい"): legacy records a file's own containing directory into DIR
  // history at the same time it records the file itself, regardless of
  // how that file was reached -- ordinary `ls`-then-Enter browsing
  // already reaches RecentDirectory separately (DirectoryMode::
  // enterDirectory()'s own pushRecent() call, which fires BEFORE this
  // one in that case, so the dedup check above just no-ops here), but
  // ^T's fixed-file entries, LOCATE results, etc. never go through
  // enterDirectory() at all -- their own containing directory was never
  // recorded, leaving goToRecentDirectory()'s (^G's empty-stack
  // fallback) own walk with nothing to find even though the user HAD
  // just been "at" that directory in every meaningful sense. Recursing
  // back into this same function (not a bare rings_[...] push) reuses
  // the identical dedup/retention-cap logic above for the derived
  // directory push too.
  if (category == RingCategory::RecentFile) {
    std::string::size_type slash = name.find_last_of('/');
    if (slash != std::string::npos) pushRecent(RingCategory::RecentDirectory, name.substr(0, slash + 1));
  }
}

const std::deque<std::string> &History::recentEntries(RingCategory category) const {
  static const std::deque<std::string> kEmpty;
  auto it = rings_.find(category);
  return (it != rings_.end()) ? it->second : kEmpty;
}

int History::retentionCount() const { return retentionCount_; }

bool History::smoothScrollEnabled() const { return smoothScrollEnabled_; }
void History::setSmoothScrollEnabled(bool enabled) { smoothScrollEnabled_ = enabled; }
int History::lsMode() const { return lsMode_; }
void History::setLsMode(int mode) { lsMode_ = mode; }
std::optional<Charset::Encoding> History::savedCharset() const { return savedCharset_; }
void History::setSavedCharset(Charset::Encoding encoding) { savedCharset_ = encoding; }
History::GuiPreferences History::guiPreferences() const { return guiPreferences_; }
void History::setGuiPreferences(const GuiPreferences &preferences) { guiPreferences_ = preferences; }

const History::SessionState &History::sessionState() const { return sessionState_; }
void History::setSessionState(SessionState state) { sessionState_ = std::move(state); }

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
    // toks[5], if present, is a reserved field legacy always writes
    // as literal 0 and discards on read -- not stored.
    if (toks.size() > 6) state.pendingCutBuffer = decodeField(toks[6]);
    if (toks.size() > 7) state.pendingCutProgram = decodeField(toks[7]);
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
    while (i < lines.size() && !isSectionBoundary(lines[i])) {
      rememberName(category, decodeField(lines[i]));
      i++;
    }
  };
  auto readRingSection = [&](RingCategory category) {
    // Ported from read_str()'s ring-history path (save_history(...,
    // f_find=0)): entries are pushed in file order with the
    // retention cap enforced, but WITHOUT the live-push dedup check
    // (a file written under a since-lowered retention count, or one
    // with an accidental duplicate line, still round-trips as
    // written up to the cap) -- see pushRecent()'s own doc comment
    // for why the public API does dedup and this loader deliberately
    // doesn't reuse it.
    auto &ring = rings_[category];
    while (i < lines.size() && !isSectionBoundary(lines[i])) {
      ring.push_back(decodeField(lines[i]));
      if (static_cast<int>(ring.size()) > retentionCount_) ring.pop_front();
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
  };
  for (const auto &spec : kRings) {
    if (!expectLabel(spec.label)) return false;
    readRingSection(spec.category);
  }

  if (!expectLabel(" MODE")) return false;
  readPositionSection(PositionCategory::Screen);

  if (!expectLabel(" TCB")) return false;
  if (i >= lines.size()) {
    errors_.push_back("truncated at TCB trailer");
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
    // Ported from `if (!env_charset(-1)) vt_charset(...)`: only
    // apply the saved charset when the environment didn't already
    // win explicitly. See the class comment / CLAUDE.md history item
    // 13.
    savedCharset_ = environmentCharsetExplicitlySet ? std::nullopt
                                                     : std::make_optional(decodeCharsetCode(charsetCode));
    // GuiPreferences (2026-08-24, item 427) -- appended AFTER the three
    // original fields above (hs/ls/charsetCode, part of the on-disk
    // format inherited from legacy tcb's own trailer) rather than
    // inserted among them, so an old history file (or, in principle,
    // legacy tcb itself reading a file this class wrote) simply sees
    // these extra tokens as absent -- toks.size() guards below default
    // every GuiPreferences field to its own "unset" value (0/false)
    // exactly like charsetCode's own missing-token default above.
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
    guiPreferences_ = prefs;
  }

  complete_ = (i < lines.size() && !lines[i].empty() && lines[i][0] == kSubEof);
  if (!complete_) errors_.push_back("missing EOF sentinel -- file may be truncated");
  return true;
}

std::string History::serialize(const std::string &programName, const std::string &timestampText) const {
  std::string out;
  out += "# ~/." + programName + "/history: " + programName + "\n";
  // "E\bEX\bXI\bIT\bT" is legacy's nroff-style overstrike-bold trick
  // (each letter doubled around a backspace) so this reads as bold
  // "EXIT" through an overstrike-aware pager -- kept for on-disk
  // compatibility/familiarity, though nothing in this class or its
  // reader depends on it.
  out += std::string("# E\bEX\bXI\bIT\bT: ") + timestampText + "\n";
  out += "# Note: The lines begin with the character \\014 or \\032 mustn't be deleted.\n\n";

  out += std::string(1, kFormFeed) + " Current buffer\n";
  out += encodeField(sessionState_.previousDirectory) + " " + encodeField(sessionState_.currentDirectory) +
         " " + encodeField(sessionState_.modeLineArg) + " " +
         encodeField(sessionState_.previousScreenMarker) + " " + encodeField(sessionState_.lastOpenName) +
         " 0 " + encodeField(sessionState_.pendingCutBuffer) + " " +
         encodeField(sessionState_.pendingCutProgram) + "\n";

  auto writePositionSection = [&](const std::string &label, PositionCategory category) {
    out += std::string(1, kFormFeed) + label + "\n";
    auto it = positions_.find(category);
    if (it == positions_.end()) return;
    // Iteration order here is sorted-by-name (std::map), unlike
    // legacy's own on-disk order (its STR "write order" mirror
    // list's insertion order, falling back to its LIST BST's sorted
    // order only when that mirror was empty) -- harmless, since nothing
    // reading this format back cares about entry order within a
    // section; see the class comment.
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
    for (const auto &name : it->second) out += encodeField(name) + "\n";  // oldest..newest, matches file order
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
  writePositionSection(" MODE", PositionCategory::Screen);

  out += std::string(1, kFormFeed) + " TCB\n";
  int charsetCode = savedCharset_ ? encodeCharsetCode(*savedCharset_) : 0;
  // GuiPreferences fields appended after charsetCode -- see
  // loadHistoryText()'s own matching comment for why (on-disk format
  // compatibility, not just symmetry).
  out += std::to_string(smoothScrollEnabled_ ? 1 : 0) + " " + std::to_string(lsMode_) + " " +
         std::to_string(charsetCode) + " " + std::to_string(guiPreferences_.fontPointSize) + " " +
         std::to_string(guiPreferences_.rows) + " " + std::to_string(guiPreferences_.columns) + " " +
         std::to_string(guiPreferences_.hasColors ? 1 : 0) + " " + std::to_string(guiPreferences_.fgR) + " " +
         std::to_string(guiPreferences_.fgG) + " " + std::to_string(guiPreferences_.fgB) + " " +
         std::to_string(guiPreferences_.bgR) + " " + std::to_string(guiPreferences_.bgG) + " " +
         std::to_string(guiPreferences_.bgB) + "\n";
  out += std::string(1, kSubEof) + "\n";
  return out;
}

}  // namespace fm
