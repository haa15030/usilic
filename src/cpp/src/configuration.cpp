#include "configuration.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fm {

namespace {

const std::vector<std::string> &directiveKeywords() {
  // Ported from str.c's tcbrc[] table, same order (the order itself
  // has no semantic meaning -- directives may appear in usilicrc in
  // any order -- but is kept for easy cross-reference against str.c).
  static const std::vector<std::string> kKeywords = {
      "ARCHIVE", "FILE", "EXEC",   "EDITOR", "SHELL", "GREP", "PROGRAM", "BUFFER", "CODE",
      "HISTORY", "WAIT", "SCROLL", "LS",     "KEY",   "CP",   "MV",      "RM",
  };
  return kKeywords;
}

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

// A line that's blank, comment-only, or starts with whitespace is a
// "continuation" of the current section rather than a new directive
// keyword -- ported from init_config()'s `strchr(s_S(S_nop), *str)`
// check, s_S(S_nop) being the literal "# \t\n".
bool isContinuationLine(const std::string &line) {
  if (line.empty()) return true;
  char c = line[0];
  return c == '#' || c == ' ' || c == '\t';
}

std::string firstToken(const std::string &line) {
  std::size_t end = line.find_first_of(" \t");
  return (end == std::string::npos) ? line : line.substr(0, end);
}

// Ported from get_cp(): truncate at the first '#' (comment), then
// strip leading spaces/tabs. Returns nullopt for an empty result
// (blank or comment-only line). Note (matching legacy exactly, not a
// simplification): a '#' inside a quoted token is NOT protected here
// -- that only matters for the KEY section's get_str(), which this
// class doesn't parse (see the class comment).
std::optional<std::string> trimCommentAndLeadingWs(const std::string &rawLine) {
  std::string line = rawLine;
  std::size_t hash = line.find('#');
  if (hash != std::string::npos) line.erase(hash);
  std::size_t start = line.find_first_not_of(" \t");
  if (start == std::string::npos) return std::nullopt;
  return line.substr(start);
}

// Ported from save_child()'s per-token loop: a '...'- or "..."-quoted
// span (taken verbatim up to the next occurrence of the SAME quote
// character, no escaping), or a bare space-delimited token.
std::vector<std::string> tokenize(const std::string &s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && s[i] == ' ') i++;
    if (i >= s.size()) break;
    if (s[i] == '\'' || s[i] == '"') {
      char quote = s[i];
      i++;
      std::size_t start = i;
      while (i < s.size() && s[i] != quote) i++;
      out.push_back(s.substr(start, i - start));
      if (i < s.size()) i++;  // skip the closing quote
    } else {
      std::size_t start = i;
      while (i < s.size() && s[i] != ' ') i++;
      out.push_back(s.substr(start, i - start));
    }
  }
  return out;
}

// Consumes continuation lines until a non-blank/non-comment data line
// is found (returning its trimmed text and leaving `i` just past it),
// or a directive keyword line / EOF is reached (returning nullopt,
// `i` left pointing at that keyword line or == lines.size()).
std::optional<std::string> nextDataLine(const std::vector<std::string> &lines, std::size_t &i) {
  while (i < lines.size() && isContinuationLine(lines[i])) {
    auto trimmed = trimCommentAndLeadingWs(lines[i]);
    i++;
    if (trimmed) return trimmed;
  }
  return std::nullopt;
}

// Ported from get_val(): like nextDataLine(), but skips data lines
// that don't start with a decimal digit, and truncates the result at
// the first space/tab (so "27 trailing junk" becomes "27").
std::optional<std::string> nextDigitToken(const std::vector<std::string> &lines, std::size_t &i) {
  while (i < lines.size() && isContinuationLine(lines[i])) {
    auto trimmed = trimCommentAndLeadingWs(lines[i]);
    i++;
    if (!trimmed || trimmed->empty()) continue;
    if (!std::isdigit(static_cast<unsigned char>((*trimmed)[0]))) continue;
    std::size_t sp = trimmed->find_first_of(" \t");
    return (sp == std::string::npos) ? *trimmed : trimmed->substr(0, sp);
  }
  return std::nullopt;
}

// Ported from save_child()'s pipe-splitting: at most one '|' is
// allowed; the part before it becomes an upstream pipeline stage, the
// part after (reattached to the same suffix) is the main command.
struct SuffixLine {
  std::string suffix;
  ProgramCommand command;
};

std::optional<SuffixLine> parseSuffixLine(const std::string &trimmedLine, std::vector<std::string> &errors) {
  // Tokenize the WHOLE line (quote-aware) before splitting off the
  // suffix -- the suffix token itself can be quoted too (save_child()
  // doesn't special-case its first token's extraction), so splitting
  // on the first raw space first (as an earlier version of this
  // function did) breaks on lines like `'.pdf' 'xpdf' '-remote'`.
  auto allTokens = tokenize(trimmedLine);
  if (allTokens.size() < 2) return std::nullopt;  // suffix with no command at all
  std::string suffix = allTokens[0];
  std::vector<std::string> rest(allTokens.begin() + 1, allTokens.end());

  auto pipeIt = std::find(rest.begin(), rest.end(), "|");
  std::optional<std::vector<std::string>> pipedFrom;
  std::vector<std::string> argv;
  if (pipeIt != rest.end()) {
    if (std::find(std::next(pipeIt), rest.end(), "|") != rest.end()) {
      errors.push_back("more than one '|' in directive line: " + trimmedLine);
      return std::nullopt;
    }
    pipedFrom = std::vector<std::string>(rest.begin(), pipeIt);
    argv = std::vector<std::string>(std::next(pipeIt), rest.end());
  } else {
    argv = rest;
  }
  if (argv.empty()) return std::nullopt;
  SuffixLine result;
  result.suffix = suffix;
  result.command.argv = argv;
  result.command.pipedFromArgv = pipedFrom;
  return result;
}

}  // namespace

Configuration::Configuration() {
  // config.h's compiled-in default was "du -ab" (all-files, byte-
  // exact) -- GNU-only, so it failed to even start on macOS's BSD du
  // (see CLAUDE.md's "今後の大きな方向性" section, 2026-07-22 entry).
  // "-a" (all-files) is POSIX/portable, but "-b" (byte-exact) isn't,
  // and dropping it entirely would leave each platform's own default
  // block size (512 bytes on BSD, 1024 on GNU) silently disagreeing --
  // "-k" (kilobyte blocks) is POSIX-portable *and* gives DuMode a
  // single, consistent unit to parse on both platforms, so it's used
  // here instead of trying to keep byte-exact sizing on just one of
  // them.
  roleCommands_[ConfiguredRole::Du] = {{"du", "-ak"}, std::nullopt};
  // ls's exact configured default isn't given directly in config.h
  // (only DU/CP/MV/RM/CHARSET/TMP/LIB are) -- inferred from
  // get_col()'s (scroll.c) own documented assumption that `ls`
  // output is in `ls -alF` format (see CLAUDE.md history item 13).
  roleCommands_[ConfiguredRole::Ls] = {{"ls", "-alF"}, std::nullopt};
  roleCommands_[ConfiguredRole::Cp] = {{"cp", "-vRi"}, std::nullopt};   // config.h's CP default
  roleCommands_[ConfiguredRole::Mv] = {{"mv", "-vi"}, std::nullopt};    // config.h's MV default
  roleCommands_[ConfiguredRole::Rm] = {{"rm", "-vri"}, std::nullopt};   // config.h's RM default
  // groff's real legacy default involves runtime groff-version
  // sniffing (to pick -R vs -Tutf8-style flags) not reproduced here;
  // this is a reasonable fixed default, not a literal port.
  roleCommands_[ConfiguredRole::Groff] = {{"groff", "-Tutf8"}, std::nullopt};
  roleCommands_[ConfiguredRole::Man] = {{"man"}, std::nullopt};
  roleCommands_[ConfiguredRole::Locate] = {{"locate"}, std::nullopt};
  roleCommands_[ConfiguredRole::Egrep] = {{"egrep", "-n"}, std::nullopt};
  roleCommands_[ConfiguredRole::Fgrep] = {{"fgrep", "-ne"}, std::nullopt};
  roleCommands_[ConfiguredRole::GzipDecompress] = {{"gzip", "-dc"}, std::nullopt};
  roleCommands_[ConfiguredRole::Bzip2Decompress] = {{"bzip2", "-d"}, std::nullopt};
  roleCommands_[ConfiguredRole::Tar] = {{"tar"}, std::nullopt};
  roleCommands_[ConfiguredRole::LhaExtract] = {{"lha", "p"}, std::nullopt};
  roleCommands_[ConfiguredRole::UnzipExtract] = {{"unzip", "-p"}, std::nullopt};
  roleCommands_[ConfiguredRole::Rpm] = {{"rpm"}, std::nullopt};
  roleCommands_[ConfiguredRole::Rpm2Cpio] = {{"rpm2cpio"}, std::nullopt};
  roleCommands_[ConfiguredRole::DebExtract] = {{"ar", "p"}, std::nullopt};

  // Ported from str.c's compiled-in ARCHIVE[] table, verbatim
  // suffix->command mappings.
  auto addBuiltinArchive = [&](const std::string &suffix, ArchiveKind kind, ProgramCommand command) {
    SuffixEntry e;
    e.suffix = suffix;
    e.command = std::move(command);
    e.archiveKind = kind;
    builtinArchiveEntries_.push_back(e);
    suffixEntries_.push_back(e);
  };
  addBuiltinArchive(".tar", ArchiveKind::Tar, {{"tar", "tvf"}, std::nullopt});
  addBuiltinArchive(".tgz", ArchiveKind::Tar, {{"tar", "tvf", "-"}, std::vector<std::string>{"gzip", "-dc"}});
  addBuiltinArchive(".tar.bz2", ArchiveKind::Tar,
                     {{"tar", "tvf", "-"}, std::vector<std::string>{"bzip2", "-dc"}});
  // Legacy's own compiled-in table lists just ".lzh lha" (no explicit
  // listing flag) -- ported verbatim rather than guessed at, since no
  // `lha` binary was available anywhere in this project's history to
  // verify the real invocation against (see CLAUDE.md history item 22,
  // LhaReader's own synthetic-fixture exception for the same reason).
  addBuiltinArchive(".lzh", ArchiveKind::Lha, {{"lha"}, std::nullopt});
  addBuiltinArchive(".zip", ArchiveKind::Zip, {{"unzip", "-l"}, std::nullopt});
  addBuiltinArchive(".rpm", ArchiveKind::Rpm, {{"rpm", "-qplv"}, std::nullopt});
  addBuiltinArchive(".deb", ArchiveKind::Deb, {{"ar", "tv"}, std::nullopt});
}

std::optional<ProgramCommand> Configuration::findBuiltinArchiveAlias(const std::string &searchText) const {
  // Ported from conf_arc()'s tcbrc-driven branch: a user ARCHIVE line
  // is "<new-suffix> <search-text>", matched against the compiled-in
  // table by prefix-matching searchText against each built-in entry's
  // own "<suffix> <argv...>" formatted line. Not a from-scratch
  // command definition -- purely an alias onto an existing built-in.
  for (const auto &entry : builtinArchiveEntries_) {
    std::string formatted = entry.suffix;
    if (entry.command.pipedFromArgv) {
      for (const auto &tok : *entry.command.pipedFromArgv) formatted += " " + tok;
      formatted += " |";
    }
    for (const auto &tok : entry.command.argv) formatted += " " + tok;
    if (formatted.rfind(searchText, 0) == 0 || formatted.compare(0, searchText.size(), searchText) == 0) {
      return entry.command;
    }
  }
  return std::nullopt;
}

bool Configuration::loadRcText(const std::string &text, bool fileManagerMode) {
  auto lines = splitLines(text);
  const auto &keywords = directiveKeywords();
  std::size_t i = 0;

  while (true) {
    while (i < lines.size() && isContinuationLine(lines[i])) i++;
    if (i >= lines.size()) break;

    std::string keyword = firstToken(lines[i]);
    auto it = std::find(keywords.begin(), keywords.end(), keyword);
    if (it == keywords.end()) {
      errors_.push_back("unrecognized directive: " + lines[i]);
      return false;
    }
    int index = static_cast<int>(it - keywords.begin());
    i++;  // consume the keyword line itself

    switch (index) {
      case 0: {  // ARCHIVE
        while (auto data = nextDataLine(lines, i)) {
          std::size_t sp = data->find_first_of(" \t");
          if (sp == std::string::npos) continue;
          std::string newSuffix = data->substr(0, sp);
          std::string searchText = data->substr(sp + 1);
          std::size_t nonSpace = searchText.find_first_not_of(" \t");
          if (nonSpace == std::string::npos) continue;
          searchText = searchText.substr(nonSpace);
          if (auto aliased = findBuiltinArchiveAlias(searchText)) {
            SuffixEntry e;
            e.suffix = newSuffix;
            e.command = *aliased;
            for (const auto &builtin : builtinArchiveEntries_) {
              // Recover the aliased entry's ArchiveKind too.
              if (builtin.command.argv == aliased->argv &&
                  builtin.command.pipedFromArgv == aliased->pipedFromArgv) {
                e.archiveKind = builtin.archiveKind;
                break;
              }
            }
            suffixEntries_.push_back(e);
          }
        }
        break;
      }
      case 1:    // FILE
      case 2: {  // EXEC
        bool exec = (index == 2);
        while (auto data = nextDataLine(lines, i)) {
          if (auto parsed = parseSuffixLine(*data, errors_)) {
            SuffixEntry e;
            e.suffix = parsed->suffix;
            e.command = parsed->command;
            e.isExec = exec;
            suffixEntries_.push_back(e);
          }
        }
        break;
      }
      case 3: {  // EDITOR
        std::vector<std::string> last;
        while (auto data = nextDataLine(lines, i)) {
          auto tokens = tokenize(*data);
          if (!tokens.empty()) last = tokens;
        }
        if (!last.empty()) editorProgram_ = last;
        break;
      }
      case 4: {  // SHELL
        std::vector<std::string> last;
        while (auto data = nextDataLine(lines, i)) {
          auto tokens = tokenize(*data);
          if (!tokens.empty()) last = tokens;
        }
        if (!last.empty()) shellProgram_ = last;
        break;
      }
      case 5: {  // GREP
        // Ported from conf_grep(): for a QUOTED pattern, everything up
        // to the matching closing quote (already handled below); for
        // an UNQUOTED one, `while (*cp != ' ' && *cp != '\t') cp++;`
        // truncates at the first space OR TAB. This port previously
        // only had the quoted branch and used an unquoted line's data
        // WHOLESALE, unmodified -- missing this truncation entirely.
        // usilicrc's own GREP section right-aligns each pattern's `#
        // comment' with a run of tabs (e.g. "\/\*[a-z]*\*\/\t\t\t\t #
        // tcb source"), and nextDataLine()/trimCommentAndLeadingWs()
        // (a faithful port of the separate, generic get_cp(), used
        // for OTHER config sections too) only strips the comment and
        // LEADING whitespace -- exactly matching get_cp()'s own real
        // behavior, trailing whitespace included, which is fine for
        // every OTHER section (tokenize() strips it) but NOT fine
        // here, since an unquoted GREP pattern is used as -one wholesale
        // string with no further tokenization. Without this
        // truncation, every unquoted seed pattern carried its own
        // trailing tab run as literal bytes, corrupting both the
        // ^J^S/^J^C candidate list's own mode-line layout (the tabs'
        // real terminal tab-stop jumps threw off column alignment,
        // pushing the destination-directory field far to the right and
        // even wrapping) and, once such a candidate's text seeded an
        // actual editable prompt (feedRegexListByte()'s own edit-
        // transition), its embedded tab bytes rendered as reversed
        // "IIII"-style control-character glyphs (LineEditor::
        // renderVisible()'s own reversed `c + '@'` convention: TAB
        // 0x09 + 0x40 = 'I') the moment editing began -- a real report
        // (screenshots ㊹/㊺) where this was mistaken for a NEW display
        // bug ("文字を入力したとたんハイライトIIIIが出る") rather than
        // pre-corrupted seed data becoming visible for the first time.
        while (auto data = nextDataLine(lines, i)) {
          std::string pattern = *data;
          if (!pattern.empty() && (pattern[0] == '\'' || pattern[0] == '"')) {
            char openQuote = pattern[0];
            std::size_t close = pattern.find_first_of("'\"", 1);
            (void)openQuote;
            if (close != std::string::npos) pattern = pattern.substr(1, close - 1);
          } else {
            std::size_t ws = pattern.find_first_of(" \t");
            if (ws != std::string::npos) pattern = pattern.substr(0, ws);
          }
          grepSeedPatterns_.push_back(pattern);
        }
        break;
      }
      case 6: {  // PROGRAM
        while (auto data = nextDataLine(lines, i)) {
          if (fileManagerMode) programSeedCommands_.push_back(*data);
        }
        break;
      }
      case 7: {  // BUFFER
        if (auto v = nextDigitToken(lines, i)) {
          int mb = std::atoi(v->c_str());
          if (mb > 100 || mb == 0) {
            errors_.push_back("BUFFER: value out of range (1-100): " + *v);
            mb = 1;
          }
          bufferSizeLimitBytes_ = mb * 1024 * 1024;
        }
        break;
      }
      case 8: {  // CODE
        if (auto data = nextDataLine(lines, i)) {
          std::string result;
          std::size_t p = 0;
          while (p < data->size()) {
            while (p < data->size() && !std::isdigit(static_cast<unsigned char>((*data)[p]))) p++;
            if (p >= data->size()) break;
            std::string numStr;
            for (int k = 0; k < 2 && p < data->size() && std::isdigit(static_cast<unsigned char>((*data)[p]));
                 k++, p++)
              numStr.push_back((*data)[p]);
            int code = std::atoi(numStr.c_str());
            if (code < 32) {
              if (code == 0 && result.empty()) break;
              result.push_back(static_cast<char>(code));
            }
          }
          allowedControlBytes_ = result;
        }
        break;
      }
      case 9: {  // HISTORY
        if (auto v = nextDigitToken(lines, i)) historyRetentionCount_ = std::atoi(v->c_str());
        break;
      }
      case 10: {  // WAIT
        if (auto v = nextDigitToken(lines, i)) waitIntervalMicros_ = std::atoi(v->c_str()) * 10000;
        break;
      }
      case 11: {  // SCROLL
        if (auto v = nextDigitToken(lines, i)) smoothScrollEnabled_ = std::atoi(v->c_str()) != 0;
        break;
      }
      case 12: {  // LS
        if (auto v = nextDigitToken(lines, i)) lsMode_ = std::atoi(v->c_str());
        break;
      }
      case 13: {
        // KEY -- deliberately not parsed (see class comment). Its own
        // sub-grammar has unindented marker lines within the section
        // (F-0/K-?/M-?/P-? and similar) that the generic
        // isContinuationLine() indentation check would misread as the
        // next top-level directive, so skip by exact keyword match
        // instead: consume everything up to the next line whose first
        // token exactly equals one of the 17 known directive keywords.
        while (i < lines.size()) {
          if (std::find(keywords.begin(), keywords.end(), firstToken(lines[i])) != keywords.end()) break;
          i++;
        }
        break;
      }
      case 14:
      case 15:
      case 16: {  // CP / MV / RM
        ConfiguredRole role = (index == 14) ? ConfiguredRole::Cp : (index == 15) ? ConfiguredRole::Mv : ConfiguredRole::Rm;
        std::vector<std::string> last;
        while (auto data = nextDataLine(lines, i)) {
          auto tokens = tokenize(*data);
          if (!tokens.empty()) last = tokens;
        }
        if (!last.empty() && fileManagerMode) roleCommands_[role] = {last, std::nullopt};
        break;
      }
      default:
        while (i < lines.size() && isContinuationLine(lines[i])) i++;
        break;
    }
  }
  return true;
}

const std::vector<std::string> &Configuration::errors() const { return errors_; }

std::optional<ProgramCommand> Configuration::programForSuffix(const std::string &filename,
                                                                ArchiveKind *archiveKindOut) const {
  const SuffixEntry *best = nullptr;
  for (const auto &entry : suffixEntries_) {
    if (entry.suffix.size() > filename.size()) continue;
    if (!std::equal(entry.suffix.rbegin(), entry.suffix.rend(), filename.rbegin())) continue;
    if (!best || entry.suffix.size() >= best->suffix.size()) best = &entry;
  }
  if (!best) return std::nullopt;
  if (archiveKindOut) *archiveKindOut = best->archiveKind;
  return best->command;
}

bool Configuration::isExec(const std::string &filename) const {
  const SuffixEntry *best = nullptr;
  for (const auto &entry : suffixEntries_) {
    if (entry.suffix.size() > filename.size()) continue;
    if (!std::equal(entry.suffix.rbegin(), entry.suffix.rend(), filename.rbegin())) continue;
    if (!best || entry.suffix.size() >= best->suffix.size()) best = &entry;
  }
  return best && best->isExec;
}

std::vector<std::string> Configuration::editorArgv(const std::string &path, int lineNumber) const {
  std::vector<std::string> result = editorProgram_;
  result.push_back("+" + std::to_string(lineNumber));
  result.push_back(path);
  return result;
}

std::vector<std::string> Configuration::shellArgv() const {
  std::vector<std::string> result = shellProgram_.empty() ? fallbackShellArgv_ : shellProgram_;
  if (result.empty()) return result;
  result.push_back("-i");
  return result;
}

void Configuration::setFallbackShellArgv(std::vector<std::string> argv) { fallbackShellArgv_ = std::move(argv); }

ProgramCommand Configuration::commandFor(ConfiguredRole role) const {
  auto it = roleCommands_.find(role);
  return (it != roleCommands_.end()) ? it->second : ProgramCommand{};
}

const std::vector<std::string> &Configuration::grepSeedPatterns() const { return grepSeedPatterns_; }
const std::vector<std::string> &Configuration::programSeedCommands() const { return programSeedCommands_; }

const std::string &Configuration::allowedControlBytes() const { return allowedControlBytes_; }
int Configuration::bufferSizeLimitBytes() const { return bufferSizeLimitBytes_; }
int Configuration::historyRetentionCount() const { return historyRetentionCount_; }
int Configuration::waitIntervalMicros() const { return waitIntervalMicros_; }
bool Configuration::smoothScrollEnabled() const { return smoothScrollEnabled_; }
int Configuration::lsMode() const { return lsMode_; }

void Configuration::setEnvironmentCharset(std::optional<std::string> envCharsetValue) {
  envCharsetValue_ = std::move(envCharsetValue);
}

bool Configuration::charsetExplicitlySet() const { return envCharsetValue_.has_value(); }

Charset::Encoding Configuration::defaultCharset() const {
  if (envCharsetValue_) {
    const std::string &v = *envCharsetValue_;
    if (v == "ascii") return Charset::Encoding::Ascii;
    if (v == "euc") return Charset::Encoding::Euc;
    if (v == "sjis") return Charset::Encoding::Sjis;
    if (v == "jis") return Charset::Encoding::Jis;
    if (v == "utf8" || v == "utf-8") return Charset::Encoding::Utf8;
  }
  // Unset, empty, or unrecognized: the documented compiled-in default
  // (config.h's CHARSET="utf-8"), never a silent fallback to Euc --
  // see the class comment on CLAUDE.md history item 13.
  return Charset::Encoding::Utf8;
}

}  // namespace fm
