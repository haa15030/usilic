#include "markup_to_roff.hpp"

#include <cstdlib>

namespace fm {

// help_text.cpp自身のexpandHelpMarkup()と非常に近い、同じ逐次スキャン
// 方式(タグの入れ子・重なりは前提にしない)。違いは出力先が「usilic
// 内部専用の制御バイト」ではなく「legacy自身のHELP.jpが実際に使う、
// 本物のnroff重ね打ちバイト列+リテラルなスペース」である点――
// convertMarkupToLegacyHelpText()自身のコメント参照。
// 2026-09-20更新、直接のユーザー報告("indentタグは...すべての行頭が
// indent位置に揃ってないと意味が無い"): 当初は<indentN>を、この関数
// 自身が物理行ごとに1回だけリテラルな半角スペースへ焼き込む方式
// だったが、それでは端末幅を超えて折り返された継続行の字下げが
// 再現できなかった(usilic.manが折り返し時の再現手段を一切持てない
// ため)。続く直接のユーザー報告("小説の下書き文書を表示する場合...
// パラグラフの出だしを字下げするルールがある。今の方法だと全体が
// １文字下がってしまう")により、その場しのぎの代替案(PipeBuffer側で
// 行頭の空白から自動的に字下げを推測する)も、著者が本当に意図した
// <indentN>による字下げなのか、単なる段落先頭の慣習的な字下げなのか
// を区別できず誤りと判明した。
//
// 直接のユーザー提案("<indentN>のNが伝わればいいわけだから、それを
// タブ用とは別の不可視コードをN個並べておくってのはどうかな？
// usilicはそれを受け取ってindentタグと同じ作業をする")を採用し、
// <indentN>/</indent>自体をpipe_buffer.cpp自身のreadPortableIndentRun()
// が解釈する不可視マーカー(U+FEFF ZERO WIDTH NO-BREAK SPACEの連続、
// そちら自身のコメント参照)へ変換するだけにした――物理行やこの関数
// 自身では一切「字下げの実現」をせず、`\x01N`制御バイト方式(usilic
// 内部専用markup経由のexpandHelpMarkup())と全く同じ実行時の動的な
// 字下げ・折り返しの仕組みを、usilic.manのようなusilic独自の生の
// 制御バイトを持てないポータブルな平文からも起動できるようにする、
// というのがこの第2のエンコード方式の役割。連続するK個(K>=1)の
// U+FEFFで字下げ幅(K-1)を表す単項エンコード(K=1が</indent>のリセット
// に相当)――詳細はreadPortableIndentRun()自身のコメント参照。
std::string convertMarkupToLegacyHelpText(const std::string &markup) {
  std::string out;
  out.reserve(markup.size() * 2);
  std::size_t i = 0;
  const std::string boldOpen = "<b>", boldClose = "</b>";
  const std::string underlineOpen = "<u>", underlineClose = "</u>";
  const std::string italicOpen = "<i>", italicClose = "</i>";
  const std::string indentOpenPrefix = "<indent";
  const std::string indentClose = "</indent>";
  const std::string kPortableIndentMarker = "\xEF\xBB\xBF";  // U+FEFF

  while (i < markup.size()) {
    std::size_t numStart = i + indentOpenPrefix.size();
    std::size_t numEnd = numStart;
    while (numEnd < markup.size() && numEnd < numStart + 2 && markup[numEnd] >= '0' && markup[numEnd] <= '9')
      numEnd++;
    bool isIndentOpen = markup.compare(i, indentOpenPrefix.size(), indentOpenPrefix) == 0 && numEnd > numStart &&
                        numEnd < markup.size() && markup[numEnd] == '>';
    if (isIndentOpen) {
      int value = std::atoi(markup.substr(numStart, numEnd - numStart).c_str());
      for (int k = 0; k <= value; k++) out += kPortableIndentMarker;  // value+1個 -> readPortableIndentRun()がvalueと解釈
      i = numEnd + 1;
      // 開始タグ直後の改行は可読性のための改行であり、意図した本文の
      // 改行ではない(expandHelpMarkup()自身の同一の理由のコメント参照)。
      if (i < markup.size() && markup[i] == '\n') i++;
      continue;
    }
    if (markup.compare(i, indentClose.size(), indentClose) == 0) {
      // 終了タグ直前の改行も同じ理由で無視(既に出力済みなので取り消す)。
      if (!out.empty() && out.back() == '\n') out.pop_back();
      out += kPortableIndentMarker;  // 1個 -> 字下げ0(リセット)
      i += indentClose.size();
      continue;
    }
    if (markup[i] == '\n') {
      out += '\n';
      i++;
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

    out += markup[i];
    i++;
  }
  return out;
}

// convertMarkupToMsRoffSource()自身のmarkup_to_roff.hpp宣言コメント
// 参照。convertMarkupToLegacyHelpText()と同じ逐次スキャン方式・同じ
// タグ集合(<b>/<u>/<i>/<indentN>)だが、出力先が「usilic自身の
// PipeBufferが後で解釈する平文」ではなく「troffが構文解析する本物の
// roffソース」である点が異なる――そのため、そちら側には無かった
// 2つの処理が必要になる: (1)行頭の"."/"'"、リテラルな"\"のエスケープ
// (さもないと著者の本文がroffの制御行/エスケープと誤認される)、
// (2)空行を.PP(-msの「段落先頭字下げ」既定マクロ)へ変換する処理
// (小説の下書きのパラグラフ先頭字下げという、この機能自身の当初の
// 動機を、usilic独自の仕組みではなく-msマクロパッケージ自身の既定の
// 振る舞いとして実現する)。
std::string convertMarkupToMsRoffSource(const std::string &markup) {
  std::string out;
  out.reserve(markup.size() * 2);
  std::size_t i = 0;
  const std::string boldOpen = "<b>", boldClose = "</b>";
  const std::string underlineOpen = "<u>", underlineClose = "</u>";
  const std::string italicOpen = "<i>", italicClose = "</i>";
  const std::string indentOpenPrefix = "<indent";
  const std::string indentClose = "</indent>";
  bool atLineStart = true;

  auto ensureFreshLine = [&]() {
    if (!out.empty() && out.back() != '\n') out += '\n';
  };
  auto appendEscaped = [&](char c) {
    if (c == '\\') {
      out += "\\e";  // リテラルなバックスラッシュ自体のroffエスケープ
      return;
    }
    out += c;
  };

  while (i < markup.size()) {
    std::size_t numStart = i + indentOpenPrefix.size();
    std::size_t numEnd = numStart;
    while (numEnd < markup.size() && numEnd < numStart + 2 && markup[numEnd] >= '0' && markup[numEnd] <= '9')
      numEnd++;
    bool isIndentOpen = markup.compare(i, indentOpenPrefix.size(), indentOpenPrefix) == 0 && numEnd > numStart &&
                        numEnd < markup.size() && markup[numEnd] == '>';
    if (isIndentOpen) {
      int value = std::atoi(markup.substr(numStart, numEnd - numStart).c_str());
      ensureFreshLine();
      out += ".in " + std::to_string(value) + "n\n";
      i = numEnd + 1;
      if (i < markup.size() && markup[i] == '\n') i++;  // convertMarkupToLegacyHelpText()自身の同じ理由のコメント参照
      atLineStart = true;
      continue;
    }
    if (markup.compare(i, indentClose.size(), indentClose) == 0) {
      ensureFreshLine();
      out += ".in 0\n";
      i += indentClose.size();
      atLineStart = true;
      continue;
    }
    if (markup[i] == '\n') {
      // 空行(段落区切り) -> .PP。両方の改行文字をまとめて消費する
      // (そうしないと.PPの直後に本物の空行がもう1行残ってしまう)。
      if (i + 1 < markup.size() && markup[i + 1] == '\n') {
        ensureFreshLine();
        out += ".PP\n";
        i += 2;
        atLineStart = true;
        continue;
      }
      out += '\n';
      i++;
      atLineStart = true;
      continue;
    }
    // 行頭の"."/"'"はtroffの制御行の先頭と誤認されるため、"\&"
    // (幅ゼロの空文字列)を前置してエスケープする。
    if (atLineStart && (markup[i] == '.' || markup[i] == '\'')) out += "\\&";
    atLineStart = false;

    bool isBold = markup.compare(i, boldOpen.size(), boldOpen) == 0;
    bool isUnderline = !isBold && markup.compare(i, underlineOpen.size(), underlineOpen) == 0;
    bool isItalic = !isBold && !isUnderline && markup.compare(i, italicOpen.size(), italicOpen) == 0;
    if (isBold || isUnderline || isItalic) {
      // <u>と<i>はどちらも\fI(斜体)へ――convertMarkupToLegacyHelpText()
      // 自身のコメント参照(roff/troffに下線専用のプリミティブが無い
      // ため、legacy自身の慣習に倣い同じ扱いにする)。
      const std::string &openTag = isBold ? boldOpen : (isUnderline ? underlineOpen : italicOpen);
      const std::string &closeTag = isBold ? boldClose : (isUnderline ? underlineClose : italicClose);
      std::size_t contentStart = i + openTag.size();
      std::size_t closePos = markup.find(closeTag, contentStart);
      if (closePos == std::string::npos) {
        // 対応する終了タグが無い――壊れたmarkupとして、開始タグ自体を
        // そのまま素通りさせる(convertMarkupToLegacyHelpText()自身の
        // 同じ安全策)。
        out += openTag;
        i = contentStart;
        continue;
      }
      out += isBold ? "\\fB" : "\\fI";
      for (std::size_t j = contentStart; j < closePos; j++) appendEscaped(markup[j]);
      out += "\\fP";
      i = closePos + closeTag.size();
      continue;
    }

    appendEscaped(markup[i]);
    i++;
  }
  return out;
}

}  // namespace fm
