#include "markup_to_roff.hpp"

#include <cstdlib>

namespace fm {

// help_text.cpp自身のexpandHelpMarkup()と非常に近い、同じ逐次スキャン
// 方式(タグの入れ子・重なりは前提にしない)。違いは出力先が「usilic
// 内部専用の制御バイト」ではなく「legacy自身のHELP.jpが実際に使う、
// 本物のnroff重ね打ちバイト列+リテラルなスペース」である点――
// convertMarkupToLegacyHelpText()自身のコメント参照。
std::string convertMarkupToLegacyHelpText(const std::string &markup) {
  std::string out;
  out.reserve(markup.size() * 2);
  std::size_t i = 0;
  const std::string boldOpen = "<b>", boldClose = "</b>";
  const std::string underlineOpen = "<u>", underlineClose = "</u>";
  const std::string italicOpen = "<i>", italicClose = "</i>";
  const std::string indentOpenPrefix = "<indent";
  const std::string indentClose = "</indent>";

  int currentIndent = 0;
  // この行にまだ実文字を1つも書いていなければtrue――その場合、次に
  // 実文字(重ね打ち含む)を書く直前に、現在の字下げ幅ぶんのスペースを
  // まず書き込む。pipe_buffer.cpp自身のseedIndentIfAtLineStart()と
  // 同じ発想だが、あちらが表示のたび毎回動的に行うのに対し、これは
  // 生成時に1回だけ、リテラルなスペース文字として書き込む。
  bool atLineStart = true;
  auto seedIndentIfNeeded = [&]() {
    if (atLineStart && currentIndent > 0) out.append(static_cast<std::size_t>(currentIndent), ' ');
    atLineStart = false;
  };

  while (i < markup.size()) {
    std::size_t numStart = i + indentOpenPrefix.size();
    std::size_t numEnd = numStart;
    while (numEnd < markup.size() && numEnd < numStart + 2 && markup[numEnd] >= '0' && markup[numEnd] <= '9')
      numEnd++;
    bool isIndentOpen = markup.compare(i, indentOpenPrefix.size(), indentOpenPrefix) == 0 && numEnd > numStart &&
                        numEnd < markup.size() && markup[numEnd] == '>';
    if (isIndentOpen) {
      currentIndent = std::atoi(markup.substr(numStart, numEnd - numStart).c_str());
      i = numEnd + 1;
      // 開始タグ直後の改行は可読性のための改行であり、意図した本文の
      // 改行ではない(expandHelpMarkup()自身の同一の理由のコメント参照)。
      if (i < markup.size() && markup[i] == '\n') i++;
      continue;
    }
    if (markup.compare(i, indentClose.size(), indentClose) == 0) {
      // 終了タグ直前の改行も同じ理由で無視(既に出力済みなので取り消す)。
      if (!out.empty() && out.back() == '\n') out.pop_back();
      currentIndent = 0;
      i += indentClose.size();
      continue;
    }
    if (markup[i] == '\n') {
      out += '\n';
      i++;
      atLineStart = true;
      continue;
    }

    bool isBold = markup.compare(i, boldOpen.size(), boldOpen) == 0;
    bool isUnderline = !isBold && markup.compare(i, underlineOpen.size(), underlineOpen) == 0;
    bool isItalic = !isBold && !isUnderline && markup.compare(i, italicOpen.size(), italicOpen) == 0;
    if (isBold || isUnderline || isItalic) {
      // <i>と<u>はどちらも同じ"_\bX"下線重ね打ちへ――このファイル冒頭の
      // コメント参照(legacy自身のnroff規約に斜体相当が無いため)。
      const std::string &openTag = isBold ? boldOpen : (isUnderline ? underlineOpen : italicOpen);
      const std::string &closeTag = isBold ? boldClose : (isUnderline ? underlineClose : italicClose);
      std::size_t contentStart = i + openTag.size();
      std::size_t closePos = markup.find(closeTag, contentStart);
      if (closePos == std::string::npos) {
        // 対応する終了タグが無い――壊れたmarkupとして、開始タグ自体を
        // そのまま素通りさせる(expandHelpMarkup()自身の同じ安全策)。
        seedIndentIfNeeded();
        out += openTag;
        i = contentStart;
        continue;
      }
      std::string content = markup.substr(contentStart, closePos - contentStart);
      std::size_t j = 0;
      while (j < content.size()) {
        unsigned char lead = static_cast<unsigned char>(content[j]);
        std::size_t len = 1;
        if ((lead & 0xE0) == 0xC0) len = 2;
        else if ((lead & 0xF0) == 0xE0) len = 3;
        else if ((lead & 0xF8) == 0xF0) len = 4;
        len = std::min(len, content.size() - j);
        std::string ch = content.substr(j, len);
        seedIndentIfNeeded();
        if (isBold) {
          out += ch;
          out += '\b';
          out += ch;
        } else {
          out += '_';
          out += '\b';
          out += ch;
        }
        j += len;
      }
      i = closePos + closeTag.size();
      continue;
    }

    seedIndentIfNeeded();
    out += markup[i];
    i++;
  }
  return out;
}

}  // namespace fm
