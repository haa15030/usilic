#include "markup_to_roff.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "utf8.hpp"

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
// (2)改行・空行の扱い(2026-09-25以降は元の1行=出力の1行の.brと、
// 空行の.sp――関数本体冒頭の同日のコメント参照)。
// 2026-10-02追加、直接のユーザー報告("usilic -nで出来たHELP.1を開くとインデントが利いていない行が
// ある"): 行長を78桁に決め打ちしていたため、それより長い行(HELP.txtは80桁で折り返してある、全角は
// 2桁)をgroffが折り返し、折り返した続きの行が字下げ無しで出ていた。タグを除いた元の行の表示幅
// (<indentN>の字下げ込み)の最大に合わせ、元の1行が折り返されないようにする(78桁未満なら78桁)。
// wrapRoffTextLines()で折り返す幅。HELP.txtの折り返しの幅(80桁、markupLineLengthForRoff()のコメント参照)。
constexpr int kRoffWrapWidth = 80;

static int markupLineLengthForRoff(const std::string &markup) {
  int longest = 78, width = 0, indent = 0;
  std::size_t i = 0;
  while (i < markup.size()) {
    if (markup[i] == '\n') {
      longest = std::max(longest, indent + width);
      width = 0;
      i++;
      continue;
    }
    bool tag = false;
    for (const char *t : {"<b>", "</b>", "<u>", "</u>", "<i>", "</i>", "</indent>"}) {
      std::size_t n = std::strlen(t);
      if (markup.compare(i, n, t) == 0) {
        if (t[1] == '/' && t[2] == 'i' && t[3] == 'n') indent = 0;
        i += n;
        tag = true;
        break;
      }
    }
    if (tag) continue;
    if (markup.compare(i, 7, "<indent") == 0) {
      std::size_t j = i + 7;
      while (j < markup.size() && j < i + 9 && std::isdigit(static_cast<unsigned char>(markup[j]))) j++;
      if (j > i + 7 && j < markup.size() && markup[j] == '>') {
        indent = std::atoi(markup.substr(i + 7, j - i - 7).c_str());
        i = j + 1;
        continue;
      }
    }
    int len = Utf8::sequence_length(static_cast<unsigned char>(markup[i]));
    if (i + len > markup.size()) len = 1;
    width += Utf8::display_width(reinterpret_cast<const unsigned char *>(markup.data() + i), len);
    i += len;
  }
  return std::max(longest, indent + width);
}

// 2026-10-06追加、直接のユーザー報告("「Boot options」で、の続きの「引数」の「引」だけ行頭にあって「数」が
// 字下げ箇所にある...みたいのがあちこちある"): groffは濁点・半濁点の付いた仮名(「で」等)を、分解した字形
// (u3066_3099)として1桁の幅に数えるので(-Zで確認: 「あ」は2桁)、そういう字の多い行は.llを超えて
// 出ていた。それを画面の幅で折り返すと、行末の1字だけが次の行の頭に出る。groffに折り返させず、本当の
// 表示幅で自分で折り返す: 本文の行(制御行以外)を、.in/.tiの字下げを数えながらwidth桁で切り、続きの
// 行は.inの字下げから(.brで改行)。続きの行の頭の空白は除く。
static std::string wrapRoffTextLines(const std::string &roff, int width) {
  std::string result;
  int indent = 0;
  std::optional<int> tempIndent;
  auto readNumber = [](const std::string &line, std::size_t from) {
    return std::atoi(line.c_str() + from);
  };
  std::size_t pos = 0;
  while (pos < roff.size()) {
    std::size_t end = roff.find('\n', pos);
    if (end == std::string::npos) end = roff.size();
    std::string line = roff.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line[0] == '.' && line.compare(0, 2, "\\&") != 0) {
      if (line.compare(0, 4, ".in ") == 0) indent = readNumber(line, 4);
      if (line.compare(0, 4, ".ti ") == 0) tempIndent = readNumber(line, 4);
      result += line + "\n";
      continue;
    }
    int column = tempIndent.value_or(indent);
    tempIndent.reset();
    std::string chunk;
    bool chunkHasText = false;
    for (std::size_t i = 0; i < line.size();) {
      std::size_t len;
      int w;
      if (line[i] == '\\' && i + 1 < line.size()) {
        char kind = line[i + 1];
        len = (kind == 'f' && i + 2 < line.size()) ? 3 : 2;  // \fB/\fI/\fP、\&、\e
        w = kind == 'e' ? 1 : 0;
      } else {
        len = static_cast<std::size_t>(Utf8::sequence_length(static_cast<unsigned char>(line[i])));
        if (len == 0 || i + len > line.size()) len = 1;
        w = Utf8::display_width(reinterpret_cast<const unsigned char *>(line.data() + i), static_cast<int>(len));
      }
      if (w > 0 && chunkHasText && column + w > width) {
        result += chunk + "\n.br\n";
        chunk.clear();
        chunkHasText = false;
        column = indent;
      }
      if (w > 0 && !chunkHasText && line[i] == ' ' && column == indent && !result.empty() &&
          result.compare(result.size() - 4, 4, ".br\n") == 0 && i > 0) {
        i += len;  // 折り返した続きの行の頭の空白
        continue;
      }
      std::string piece = line.substr(i, len);
      if (!chunkHasText && w > 0 && (piece == "." || piece == "'") && chunk.empty()) chunk += "\\&";
      chunk += piece;
      if (w > 0) chunkHasText = true;
      column += w;
      i += len;
    }
    result += chunk + "\n";
  }
  if (!roff.empty() && roff.back() != '\n' && !result.empty()) result.pop_back();
  return result;
}

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
  int currentIndent = 0;  // 今の.inの値(下の行の途中の<indentN>参照)

  // 2026-10-04、直接のユーザー報告("HELP.1にインデントの失敗がある"): 行の途中の<indentN>・</indent>
  // (「<indent8>Open</indent> : <indent15>説明</indent>」のようなぶら下げ、-mでは折り返した続きの行が
  // 説明の位置にそろう)を.inとして出していたので、.inの改行でキー・「:」・説明の3行に割れていた。
  // 行の途中のタグかどうか(行頭でなく、行末の</indent>でもない)。
  auto isMidLineTag = [&](std::size_t at, bool lineStarted) -> int {  // 長さ(タグでなければ0)
    if (!lineStarted) return 0;
    if (markup.compare(at, indentClose.size(), indentClose) == 0) {
      std::size_t after = at + indentClose.size();
      if (after >= markup.size() || markup[after] == '\n') return 0;  // 行末の</indent>は従来どおり
      return static_cast<int>(indentClose.size());
    }
    if (markup.compare(at, indentOpenPrefix.size(), indentOpenPrefix) != 0) return 0;
    std::size_t end = at + indentOpenPrefix.size();
    while (end < markup.size() && end < at + indentOpenPrefix.size() + 2 && std::isdigit(static_cast<unsigned char>(markup[end])))
      end++;
    if (end == at + indentOpenPrefix.size() || end >= markup.size() || markup[end] != '>') return 0;
    return static_cast<int>(end + 1 - at);
  };

  // 2026-09-25追加、直接のユーザー要望("-nの見た目が-mと違いすぎる。
  // -mと同じになるならしてほしい"): 以前は-ms既定のfillモード段落
  // (.PP)に任せていたため、groffに通すと(1)元の改行が詰められて1段落
  // が流し込みになり、(2)両端揃えで単語間に大きな空白が入り、(3)'^'
  // '\''/'~'/'`'/'-'が組版記号(ˆ ’ ˜ 等)に置き換わり、(4).PPが段落先頭
  // を字下げしていた――-m(元の行をそのまま保ち、長い行だけ字下げ付き
  // で折り返す)とは別物の見た目だった。-ms/-mandoc(usilic自身が.md
  // 以外のroffソースを自動整形する時の-mandoc、ConfiguredRole::Groff
  // 参照)のどちらでも同じになるよう、マクロパッケージ非依存の
  // プリミティブだけで-mの見た目へ寄せる: 左余白/ページ見出し無し、
  // 行長78桁(man既定と同じ)、両端揃え・ハイフネーション無し、上記の
  // ASCII記号はASCIIのまま、元の1行=出力の1行(行末ごとに.br)、空行は
  // .PPではなく素の.sp。
  // groffには折り返させない(wrapRoffTextLines()参照)ので、.llは本文のどの行より長くしておく。
  int lineLength = markupLineLengthForRoff(markup) + 100;
  out += ".\\\" usilic -n: -m(usilic自身の表示)と同じ見た目にするための前置き\n"
         ".nr PO 0\n.po 0\n.nr HM 0\n.nr FM 0\n.nr PL 100000v\n.pl 100000v\n.ds CH\n"
         ".nr LL " + std::to_string(lineLength) + "n\n.ll " + std::to_string(lineLength) + "n\n.nh\n.ad l\n"
         ".char - \\[u002D]\n.char ' \\[u0027]\n.char ^ \\[u005E]\n.char ~ \\[u007E]\n.char ` \\[u0060]\n"
         // 日本語の文字の間でも改行できるようにする(-mは任意の位置で
         // 折り返す)。groff 1.24のja.tmacの定義をそのまま取り込み、
         // -mjaを付けずに済むようにしている。
         ".class [CJKprepunct] , : ; > } \\[u3001] \\[u3002] \\[uFF0C] \\[uFF0E] \\[u30FB] \\[uFF1A] "
         "\\[uFF1B] \\[uFF1F] \\[uFF01] \\[uFF09] \\[u3015] \\[uFF3D] \\[uFF5D] \\[u300D] \\[u300F] "
         "\\[u3011] \\[u3041] \\[u3043] \\[u3045] \\[u3047] \\[u3049] \\[u3063] \\[u3083] \\[u3085] "
         "\\[u3087] \\[u30FC] \\[u30A1] \\[u30A3] \\[u30A5] \\[u30A7] \\[u30A9] \\[u30C3] \\[u30E3] "
         "\\[u30E5] \\[u30E7]\n"
         ".class [CJKpostpunct] \\[uFF08] \\[u3014] \\[uFF3B] \\[uFF5B] \\[u300C] \\[u300E] \\[u3010]\n"
         ".class [CJKnormal] \\[u3041]-\\[u3096] \\[u30A0]-\\[u30FF] \\[u4E00]-\\[u9FFF]\n"
         ".cflags 128 \\C'[CJKprepunct]'\n.cflags 256 \\C'[CJKpostpunct]'\n.cflags 512 \\C'[CJKnormal]'\n";

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
    if (int skip = isMidLineTag(i, !atLineStart)) {  // 行の途中のタグ(行頭で.in/.tiに済ませた)
      i += static_cast<std::size_t>(skip);
      continue;
    }
    if (isIndentOpen) {
      int value = std::atoi(markup.substr(numStart, numEnd - numStart).c_str());
      ensureFreshLine();
      out += ".in " + std::to_string(value) + "n\n";  // .inは自身で改行(break)を伴う
      currentIndent = value;
      i = numEnd + 1;
      if (i < markup.size() && markup[i] == '\n') i++;  // convertMarkupToLegacyHelpText()自身の同じ理由のコメント参照
      atLineStart = true;
      continue;
    }
    if (markup.compare(i, indentClose.size(), indentClose) == 0) {
      ensureFreshLine();
      out += ".in 0\n";  // .inは自身で改行(break)を伴う
      currentIndent = 0;
      i += indentClose.size();
      // 閉じタグの直後の改行は、閉じタグのあった行自体の行末として
      // 消費する(さもないと下の分岐で余分な空行(.sp)になる)。
      if (i < markup.size() && markup[i] == '\n') i++;
      atLineStart = true;
      continue;
    }
    if (markup[i] == '\n') {
      // 上の2026-09-25のコメント参照: 文字のある行の行末は.br(元の1行=
      // 出力の1行)、行頭での改行(=空行)は.sp。
      if (atLineStart) {
        ensureFreshLine();
        out += ".sp\n";
      } else {
        out += "\n.br\n";
      }
      i++;
      atLineStart = true;
      continue;
    }
    // 行頭の"."/"'"はtroffの制御行の先頭と誤認されるため、"\&"
    // (幅ゼロの空文字列)を前置してエスケープする。
    if (atLineStart) {
      // 行の途中の<indentN>・</indent>(isMidLineTag())があれば、続きの行の字下げ(.in)を最後のタグの値に、
      // この行の1行目だけ(.ti)を今の字下げにする。
      std::optional<int> continuation;
      for (std::size_t k = i; k < markup.size() && markup[k] != '\n';) {
        if (int len = isMidLineTag(k, k > i)) {
          continuation = markup[k + 1] == '/' ? 0 : std::atoi(markup.c_str() + k + indentOpenPrefix.size());
          k += static_cast<std::size_t>(len);
        } else {
          k++;
        }
      }
      if (continuation && *continuation != currentIndent) {
        ensureFreshLine();
        out += ".in " + std::to_string(*continuation) + "n\n.ti " + std::to_string(currentIndent) + "n\n";
        currentIndent = *continuation;
      }
    }
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
  // 前置きの.pl 100000v(ページ区切りの空行を起こさないため)の分だけ
  // 末尾が空行で埋まらないよう、最後にページ長を現在位置まで縮める
  // (man自身の連続表示と同じ定番の手法)。
  ensureFreshLine();
  out += ".br\n.pl \\n(nlu+1v\n";
  return wrapRoffTextLines(out, kRoffWrapWidth);
}

}  // namespace fm
