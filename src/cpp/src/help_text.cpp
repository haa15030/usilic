#include "help_text.hpp"

#include <algorithm>
#include <cctype>

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
  // 2026-09-04追加、直接のユーザー報告("src/HELP.txtは冒頭でちゃんと
  // 分割文字列がマッチしているのに`--`エリアがあるのはおかしい"):
  // kHelpTextJa等の生文字列リテラルは、`R"HELPTXT(`の直後で改行してから
  // 本文(例えば"  1.  Introduction")を書き始める慣習(このファイルの
  // 全ての生文字列で一貫、可読性のため)のせいで、実際には常に先頭に
  // 1つだけ余分な空行を持っている――CutMode::split()自身の"--"区間
  // (kHelpPattern自身の最初のマッチより「前」に何らかの内容がある
  // 場合に生成される、cut_mode.cppのコメント参照)から見ると、これは
  // 「本物の、章1より前の内容」に見えてしまい、目次の最上段に
  // 見せかけの空の"--"エントリを作ってしまっていた――実際には章1が
  // 見た目上「冒頭で」始まっているように見えるにも関わらず。ここで
  // その先頭の1個の改行だけを読み飛ばすことで、この意図しない空行を
  // 本文の一部として扱わないようにする(2個以上の空行はそのまま――
  // 意図的な空行かもしれないので、あくまで1個だけを特別扱いする)。
  if (!text.empty() && text[0] == '\n') i = 1;
  const std::string boldOpen = "<b>", boldClose = "</b>";
  const std::string underlineOpen = "<u>", underlineClose = "</u>";
  // 2026-09-04追加、直接のユーザー依頼("行頭にパディングする全角
  // スペースの個数を指定するだけのusilic特化マークアップ書式"、
  // EPUBの<div class="indent_1em">相当――折り返した継続行にも
  // 同じ幅の字下げを引き継ぐ): <indentN>...</indent>(Nは1桁の
  // 全角スペース個数)。太字/下線(1文字ごとの重ね打ちバイト列)とは
  // 別系統の制御バイト\x01+数字1桁で表す――\x01<N>(N=1-9)で
  // 開始(以後の折り返しでN個の全角スペースを引き継ぐ)、\x01""0""で
  // 終了(引き継ぎをやめる)。対応するデコーダはPipeBuffer::wrapLine()
  // 自身のコメント参照。中身(段落本文)自体はここでは一切変換せず、
  // 開始/終了マーカーの間をこの関数自身の通常のループにそのまま
  // 委ねる(入れ子の<b>/<u>もそのまま機能する)。
  const std::string indentOpenPrefix = "<indent";
  const std::string indentClose = "</indent>";
  while (i < text.size()) {
    bool isIndentOpen = text.compare(i, indentOpenPrefix.size(), indentOpenPrefix) == 0 &&
                         i + indentOpenPrefix.size() + 1 < text.size() &&
                         std::isdigit(static_cast<unsigned char>(text[i + indentOpenPrefix.size()])) &&
                         text[i + indentOpenPrefix.size() + 1] == '>';
    if (isIndentOpen) {
      out += '\x01';
      out += text[i + indentOpenPrefix.size()];
      i += indentOpenPrefix.size() + 2;  // "<indent" + digit + ">"
      continue;
    }
    if (text.compare(i, indentClose.size(), indentClose) == 0) {
      out += '\x01';
      out += '0';
      i += indentClose.size();
      continue;
    }
    // 2026-09-04追加、直接のユーザー依頼("今後は<i>プログラム名</i>
    // で記載したいので対応よろしく"): <i>...</i>(斜体)。太字/下線が
    // 使っている1文字ごとの重ね打ちバイト列("X\bX"等)はlegacyの
    // nroff規約からの移植であり、斜体にはそもそも対応する重ね打ち
    // 規約が存在しない――このC++移植自身の新規追加なので、<indentN>
    // と同じ「トグル式の制御バイト」方式(\x02+数字1桁、\x011桁は
    // 既に字下げに使用済みなので別系統)を採用する: \x021で開始、
    // \x020で終了。PipeBuffer::wrapLine()側は、この区間内の「素の」
    // (太字/下線の重ね打ちに該当しない)各文字ごとに、Display::
    // Attribute::Italicの1文字幅AttributeRunを生成する――太字/下線
    // 自身の1文字ごとのAttributeRunの粒度に合わせている(decode_help.cpp
    // 自身のコメント参照、隣接するrunは別ツールが後でまとめる)。
    // 中身自体はここでは一切変換せず、開始/終了マーカーの間を通常の
    // ループにそのまま委ねる(入れ子の<b>/<u>もそのまま機能するが、
    // 同じ文字が斜体と太字/下線を同時に持つケースはwrapLine()側の
    // 制約により非対応――そちら自身のコメント参照)。
    const std::string italicOpen = "<i>", italicClose = "</i>";
    if (text.compare(i, italicOpen.size(), italicOpen) == 0) {
      out += '\x02';
      out += '1';
      i += italicOpen.size();
      continue;
    }
    if (text.compare(i, italicClose.size(), italicClose) == 0) {
      out += '\x02';
      out += '0';
      i += italicClose.size();
      continue;
    }
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
