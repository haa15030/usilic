#include "native_grep.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

namespace fm {

namespace {

std::string lowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

int nativeGrep(const std::vector<std::string> &args, const std::string &input, std::string &output) {
  char syntax = 'G';
  bool numbers = false, ignoreCase = false, invert = false;
  std::optional<std::string> pattern;
  std::vector<std::string> files;
  bool expectPattern = false;
  for (const auto &arg : args) {
    if (expectPattern) {
      pattern = arg;
      expectPattern = false;
      continue;
    }
    if (arg.size() > 1 && arg[0] == '-' && !pattern) {
      for (std::size_t i = 1; i < arg.size(); i++) {
        char c = arg[i];
        if (c == 'F' || c == 'E' || c == 'G') syntax = c;
        else if (c == 'n') numbers = true;
        else if (c == 'i') ignoreCase = true;
        else if (c == 'v') invert = true;
        else if (c == 'e') {
          if (i + 1 < arg.size()) pattern = arg.substr(i + 1);
          else expectPattern = true;
          break;
        }
      }
      continue;
    }
    if (!pattern) pattern = arg;
    else files.push_back(arg);
  }
  if (!pattern) return 2;

  std::optional<std::regex> re;
  std::string fixed = ignoreCase ? lowerAscii(*pattern) : *pattern;
  if (syntax != 'F') {
    try {
      auto flags = (syntax == 'E' ? std::regex::extended : std::regex::basic) | std::regex::optimize;
      if (ignoreCase) flags |= std::regex::icase;
      re.emplace(*pattern, flags);
    } catch (const std::regex_error &) {
      return 2;
    }
  }
  auto matches = [&](const std::string &line) {
    bool hit = re ? std::regex_search(line, *re)
                  : (ignoreCase ? lowerAscii(line) : line).find(fixed) != std::string::npos;
    return hit != invert;
  };

  bool any = false, failed = false;
  auto scan = [&](const std::string &text, const std::string &prefix) {
    std::size_t start = 0, number = 0;
    while (start < text.size()) {
      std::size_t end = text.find('\n', start);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(start, end - start);
      number++;
      if (matches(line)) {
        any = true;
        output += prefix;
        if (numbers) output += std::to_string(number) + ":";
        output += line + "\n";
      }
      start = end + 1;
    }
  };
  if (files.empty()) {
    scan(input, "");
  } else {
    for (const auto &file : files) {
      std::ifstream in(file, std::ios::binary);
      if (!in) {
        failed = true;
        continue;
      }
      std::ostringstream text;
      text << in.rdbuf();
      scan(text.str(), files.size() > 1 ? file + ":" : "");
    }
  }
  if (any) return 0;
  return failed ? 2 : 1;
}

}  // namespace fm
