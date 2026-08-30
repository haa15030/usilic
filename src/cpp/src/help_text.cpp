#include "help_text.hpp"

#include <algorithm>

namespace fm {

// help_text.hpp自身のコメント参照。<b>...</b>/<u>...</u>で囲まれた範囲を、
// PipeBuffer::wrapLine()が既に理解する重ね打ちバイト列(1文字ごとの
// "X\bX"=太字、"_\bX"=下線)へ展開する――逆(重ね打ちの解釈自体)は
// wrapLine()側に既にあるので、ここでは単純にその「生成」側を担う。
// タグの入れ子・重なりは想定しない(このヘルプ本文自身がそもそも
// 使っていない)ので、開始/終了タグを見つけたらその範囲だけを処理する
// 単純な逐次スキャンで十分。
std::string expandHelpMarkup(const std::string &text) {
  std::string out;
  out.reserve(text.size() * 2);
  std::size_t i = 0;
  const std::string boldOpen = "<b>", boldClose = "</b>";
  const std::string underlineOpen = "<u>", underlineClose = "</u>";
  while (i < text.size()) {
    bool isBold = text.compare(i, boldOpen.size(), boldOpen) == 0;
    bool isUnderline = !isBold && text.compare(i, underlineOpen.size(), underlineOpen) == 0;
    if (isBold || isUnderline) {
      const std::string &openTag = isBold ? boldOpen : underlineOpen;
      const std::string &closeTag = isBold ? boldClose : underlineClose;
      std::size_t contentStart = i + openTag.size();
      std::size_t closePos = text.find(closeTag, contentStart);
      if (closePos == std::string::npos) {
        // 対応する終了タグが無い――壊れたマークアップとして、開始タグ自体を
        // そのまま素通りさせる(無限ループ/データ消失より安全側に倒す)。
        out += openTag;
        i = contentStart;
        continue;
      }
      std::string content = text.substr(contentStart, closePos - contentStart);
      // 1文字ごとに重ね打ちを生成する――wrapLine()自身の検出ロジック
      // (help_text.hpp自身のコメント参照)が、複数文字をまとめてではなく
      // 1文字ずつのX\bX/_\bXしか認識しないため。UTF-8のマルチバイト文字は
      // シーケンス全体を「1文字」として扱う必要がある。
      std::size_t j = 0;
      while (j < content.size()) {
        unsigned char lead = static_cast<unsigned char>(content[j]);
        std::size_t len = 1;
        if ((lead & 0xE0) == 0xC0) len = 2;
        else if ((lead & 0xF0) == 0xE0) len = 3;
        else if ((lead & 0xF8) == 0xF0) len = 4;
        len = std::min(len, content.size() - j);
        std::string ch = content.substr(j, len);
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
    out += text[i];
    i++;
  }
  return out;
}

}  // namespace fm
