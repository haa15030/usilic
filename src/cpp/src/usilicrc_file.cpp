#include "usilicrc_file.hpp"

#include "platform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fm {
namespace usilicrc {

namespace {

std::vector<std::string> splitKeepingLines(const std::string &text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

std::string join(const std::vector<std::string> &lines) {
  std::string out;
  for (const auto &line : lines) out += line + "\n";
  return out;
}

// 行頭がキーワード(Configurationの節の見出し)の行か。空行・コメント・空白で始まる行は節の続き
// (Configuration::isContinuationLine()と同じ判定)。
bool isKeywordLine(const std::string &line) {
  return !line.empty() && line[0] != '#' && line[0] != ' ' && line[0] != '\t';
}

std::string firstToken(const std::string &line) { return line.substr(0, line.find_first_of(" \t")); }

std::optional<std::size_t> findHeader(const std::vector<std::string> &lines, const std::string &section) {
  for (std::size_t i = 0; i < lines.size(); i++)
    if (isKeywordLine(lines[i]) && firstToken(lines[i]) == section) return i;
  return std::nullopt;
}

std::size_t sectionEnd(const std::vector<std::string> &lines, std::size_t header) {
  std::size_t e = header + 1;
  while (e < lines.size() && !isKeywordLine(lines[e])) e++;
  return e;
}

// 値の行: 空白で始まり、中身がコメントでない行。
bool isValueLine(const std::string &line) {
  if (line.empty() || (line[0] != ' ' && line[0] != '\t')) return false;
  std::size_t p = line.find_first_not_of(" \t");
  return p != std::string::npos && line[p] != '#';
}

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

std::string userPath(const std::string &home) { return home + "/.usilic/usilicrc"; }

namespace {
std::string &installLibDirectory() {
  static std::string dir;
  return dir;
}
}  // namespace

void setInstallLibDirectory(const std::string &dir) { installLibDirectory() = dir; }

std::optional<std::string> installedPath(const std::string &home) {
  std::vector<std::string> candidates;
  if (!home.empty()) candidates.push_back(home + "/.local/lib/usilic/usilicrc");
  if (!installLibDirectory().empty()) {
    std::string dir = installLibDirectory();
    if (dir.back() != '/') dir += '/';
    candidates.push_back(dir + "usilicrc");
  }
  candidates.push_back("/usr/local/lib/usilic/usilicrc");
  for (const auto &path : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) return path;
  }
  return std::nullopt;
}

bool ensureUser(const std::string &home) {
  if (home.empty()) return false;
  std::string user = userPath(home);
  std::error_code ec;
  if (std::filesystem::exists(user, ec)) return true;
  auto installed = installedPath(home);
  if (!installed) return false;
  std::filesystem::create_directories(home + "/.usilic", ec);
  return std::filesystem::copy_file(*installed, user, ec) && !ec;
}

namespace {
bool writeAtomically(const std::string &path, const std::string &text);  // 下で定義

// 節の値の行(前後の空白を除いたもの)。節が無ければnullopt。
std::optional<std::vector<std::string>> sectionValues(const std::string &text, const std::string &section) {
  std::vector<std::string> lines = splitKeepingLines(text);
  auto header = findHeader(lines, section);
  if (!header) return std::nullopt;
  std::vector<std::string> values;
  for (std::size_t i = *header + 1; i < sectionEnd(lines, *header); i++) {
    if (!isValueLine(lines[i])) continue;
    std::string value = lines[i].substr(lines[i].find_first_not_of(" \t"));
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t'))
      value.pop_back();
    values.push_back(value);
  }
  return values;
}
}  // namespace

std::string updateUserIfOlder(const std::string &home) {
  if (home.empty()) return {};
  std::string user = userPath(home);
  auto installed = installedPath(home);
  if (!installed) return {};
  std::error_code ec;
  auto userTime = std::filesystem::last_write_time(user, ec);
  if (ec) return {};
  auto installedTime = std::filesystem::last_write_time(*installed, ec);
  if (ec || installedTime <= userTime) return {};
  std::time_t now = std::time(nullptr);
  char day[16], stamp[32];
  std::strftime(day, sizeof day, "%Y%m%d", std::localtime(&now));
  std::strftime(stamp, sizeof stamp, "%Y%m%d%H%M%S", std::localtime(&now));
  std::string backup = user + "." + day + ".bak";
  if (std::filesystem::exists(backup, ec)) backup = user + "." + stamp + ".bak";
  if (!std::filesystem::copy_file(user, backup, ec) || ec) return {};
  // 2026-10-16、直接のユーザー報告(Windows、"^J^Fの設定が保存されてなかったみたい"): usilic自身が書き込む節
  // (^J^FのFONT・SANS-SERIF・SERIF、-s等の起動オプションで保存するもの、setSection()の呼び出し元)は、
  // 置き換えても前の値を引き継ぐ。以前は新しい版(Windowsではzipを展開し直すたび)で起動すると消えていた。
  std::string userText = readFile(user), installedText = readFile(*installed), merged = installedText;
  for (const char *section : {"FONT", "SANS-SERIF", "SERIF", "SPEED", "DRAG", "WAIT", "BUFFER", "LINE"}) {
    auto mine = sectionValues(userText, section);
    if (mine && *mine != sectionValues(installedText, section).value_or(std::vector<std::string>{}))
      merged = replaceSection(merged, section, *mine, installedText);
  }
  if (!writeAtomically(user, merged)) return {};
  return backup;
}

std::string replaceSection(const std::string &text, const std::string &section, const std::vector<std::string> &values,
                           const std::string &templateText) {
  std::vector<std::string> lines = splitKeepingLines(text);
  auto header = findHeader(lines, section);
  if (!header) {
    // 既定のusilicrcの同じ節(見出しから次の見出しの手前まで)を雛形に、KEYの手前へ足す。
    std::vector<std::string> block;
    std::vector<std::string> templateLines = splitKeepingLines(templateText);
    if (auto th = findHeader(templateLines, section))
      block.assign(templateLines.begin() + static_cast<long>(*th),
                   templateLines.begin() + static_cast<long>(sectionEnd(templateLines, *th)));
    if (block.empty()) block = {section, ""};
    if (!block.back().empty()) block.push_back("");
    std::size_t at = findHeader(lines, "KEY").value_or(lines.size());
    if (at == lines.size() && !lines.empty() && !lines.back().empty()) lines.push_back(""), at++;
    lines.insert(lines.begin() + static_cast<long>(at), block.begin(), block.end());
    header = at;
  }
  std::size_t end = sectionEnd(lines, *header);
  std::optional<std::size_t> insertAt;
  for (std::size_t i = end; i-- > *header + 1;) {
    if (!isValueLine(lines[i])) continue;
    lines.erase(lines.begin() + static_cast<long>(i));
    end--;
    insertAt = i;
  }
  std::vector<std::string> valueLines;
  for (const auto &value : values) valueLines.push_back("\t" + value);
  if (!insertAt) {
    // 値の行が無かった: 節の最後の空でない行(コメント)の後に、空行を挟んで足す。
    std::size_t last = *header;
    for (std::size_t i = *header + 1; i < end; i++)
      if (!lines[i].empty()) last = i;
    insertAt = last + 1;
    if (last != *header) valueLines.insert(valueLines.begin(), "");
  }
  lines.insert(lines.begin() + static_cast<long>(*insertAt), valueLines.begin(), valueLines.end());
  return join(lines);
}

namespace {
bool writeAtomically(const std::string &path, const std::string &text) {
  std::string temp = path + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    if (!out) return false;
  }
  return replaceFile(temp, path);  // Windowsでも置き換える(platform.hpp参照)
}
}  // namespace

std::string readText(const std::string &path) { return readFile(path); }

std::optional<int> sectionLineNumber(const std::string &text, const std::string &section) {
  auto header = findHeader(splitKeepingLines(text), section);
  if (!header) return std::nullopt;
  return static_cast<int>(*header) + 1;
}

std::string replaceMacroMarker(const std::string &text, const std::vector<std::string> &replacement) {
  std::vector<std::string> lines = splitKeepingLines(text);
  auto header = findHeader(lines, "MACRO");
  if (!header) {
    if (!lines.empty() && !lines.back().empty()) lines.push_back("");
    lines.push_back("MACRO");
    header = lines.size() - 1;
  }
  std::size_t end = sectionEnd(lines, *header);
  for (std::size_t i = *header + 1; i < end; i++) {
    std::size_t p = lines[i].find_first_not_of(" \t");
    if (p != std::string::npos && lines[i].compare(p, 2, "--") == 0) {
      lines.erase(lines.begin() + static_cast<long>(i));
      lines.insert(lines.begin() + static_cast<long>(i), replacement.begin(), replacement.end());
      return join(lines);
    }
  }
  std::size_t last = *header;
  for (std::size_t i = *header + 1; i < end; i++)
    if (!lines[i].empty()) last = i;
  lines.insert(lines.begin() + static_cast<long>(last + 1), replacement.begin(), replacement.end());
  return join(lines);
}

bool setMacroMarker(const std::string &home, const std::vector<std::string> &replacement) {
  if (!ensureUser(home)) return false;
  std::string user = userPath(home);
  return writeAtomically(user, replaceMacroMarker(readFile(user), replacement));
}

int nextMacroNumber(const std::string &text) {
  std::vector<std::string> lines = splitKeepingLines(text);
  auto header = findHeader(lines, "MACRO");
  int next = 1;
  if (!header) return next;
  std::size_t end = sectionEnd(lines, *header);
  for (std::size_t i = *header + 1; i < end; i++) {
    std::size_t p = lines[i].find_first_not_of(" \t");
    if (p == std::string::npos || lines[i].compare(p, 2, "M-") != 0) continue;
    next = std::max(next, std::atoi(lines[i].c_str() + p + 2) + 1);
  }
  return next;
}

namespace {
// MACRO節の1行を読む。M-nの行か「--」の行でなければnullopt。
std::optional<MacroLine> parseMacroLine(const std::string &raw) {
  std::size_t p = raw.find_first_not_of(" \t");
  if (p == std::string::npos) return std::nullopt;
  auto word = [&]() {
    while (p < raw.size() && (raw[p] == ' ' || raw[p] == '\t')) p++;
    std::size_t start = p;
    while (p < raw.size() && raw[p] != ' ' && raw[p] != '\t') p++;
    return raw.substr(start, p - start);
  };
  auto trim = [](const std::string &s) {
    std::size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
  };
  // 要素の並びとコメントを分ける(「"..."」の中の#はコメントの始まりではない)。
  auto splitRest = [&](MacroLine &line) {
    std::size_t hash = std::string::npos;
    bool quoted = false;
    for (std::size_t i = p; i < raw.size(); i++) {
      if (raw[i] == '"') quoted = !quoted;
      else if (raw[i] == '#' && !quoted) {
        hash = i;
        break;
      }
    }
    line.items = trim(raw.substr(p, hash == std::string::npos ? std::string::npos : hash - p));
    if (hash != std::string::npos) line.comment = trim(raw.substr(hash + 1));
  };
  MacroLine line;
  std::string head = word();
  if (head == "--") {
    splitRest(line);
    return line;
  }
  if (head.size() < 3 || head.compare(0, 2, "M-") != 0 || head.find_first_not_of("0123456789", 2) != std::string::npos)
    return std::nullopt;
  line.number = std::atoi(head.c_str() + 2);
  line.key = word();
  if (line.key.empty()) return std::nullopt;
  splitRest(line);
  return line;
}
}  // namespace

std::vector<MacroLine> macroLines(const std::string &text) {
  std::vector<MacroLine> out;
  std::vector<std::string> lines = splitKeepingLines(text);
  auto header = findHeader(lines, "MACRO");
  if (!header) return out;
  std::size_t end = sectionEnd(lines, *header);
  for (std::size_t i = *header + 1; i < end; i++)
    if (auto line = parseMacroLine(lines[i])) out.push_back(*line);
  return out;
}

std::string replaceMacroLine(const std::string &text, int number, const std::string &line) {
  std::vector<std::string> lines = splitKeepingLines(text);
  auto header = findHeader(lines, "MACRO");
  if (!header) return text;
  std::size_t end = sectionEnd(lines, *header);
  for (std::size_t i = *header + 1; i < end; i++) {
    auto parsed = parseMacroLine(lines[i]);
    if (parsed && parsed->number == number) {
      lines[i] = line;
      return join(lines);
    }
  }
  return text;
}

bool setMacroLine(const std::string &home, int number, const std::string &line) {
  if (!ensureUser(home)) return false;
  std::string user = userPath(home);
  std::string text = readFile(user);
  std::string replaced = replaceMacroLine(text, number, line);
  if (replaced == text) return false;
  return writeAtomically(user, replaced);
}

bool setSection(const std::string &home, const std::string &section, const std::vector<std::string> &values) {
  if (!ensureUser(home)) return false;
  std::string user = userPath(home);
  std::string templateText;
  if (auto installed = installedPath(home)) templateText = readFile(*installed);
  return writeAtomically(user, replaceSection(readFile(user), section, values, templateText));
}

}  // namespace usilicrc
}  // namespace fm
