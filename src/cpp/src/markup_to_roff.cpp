#include "markup_to_roff.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>

#include "help_text.hpp"
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
std::string convertMarkupToLegacyHelpText(const std::string &source) {
  const std::string markup = joinMarkupLinesAtBreaks(source);  // <br />(help_text.hpp参照)
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

// 2026-10-09、直接のユーザー依頼("HELPを不可視コードなしで表示できるようにしてみて"): 日本語の原稿も
// 自分では折り返さず、groff・manに今の画面の幅で組ませる(以前の78桁の決め打ちwrapRoffTextLines()は削除)。
// groff(1.24.1)の端末用のフォントdevutf8/Rは、濁点・半濁点の付いた仮名58字(「で」等、分解した字形
// u3066_3099)の幅を1桁に数え、行が画面の幅をはみ出す。groff同梱の日本語のフォントJPM(明朝)・JPG
// (ゴシック)ではどれも正しく2桁なので、その字だけJPM(太字の時はJPG、端末ではJPGが太字になる)で出す。
// 斜体(下線)の時もJPMなので、この58字だけ下線が外れる。
static bool isMiscountedKana(char32_t cp) {
  static const char32_t kKana[] = {
      0x304C, 0x304E, 0x3050, 0x3052, 0x3054, 0x3056, 0x3058, 0x305A, 0x305C, 0x305E, 0x3060, 0x3062,
      0x3065, 0x3067, 0x3069, 0x3070, 0x3071, 0x3073, 0x3074, 0x3076, 0x3077, 0x3079, 0x307A, 0x307C,
      0x307D, 0x3094, 0x309E, 0x30AC, 0x30AE, 0x30B0, 0x30B2, 0x30B4, 0x30B6, 0x30B8, 0x30BA, 0x30BC,
      0x30BE, 0x30C0, 0x30C2, 0x30C5, 0x30C7, 0x30C9, 0x30D0, 0x30D1, 0x30D3, 0x30D4, 0x30D6, 0x30D7,
      0x30D9, 0x30DA, 0x30DC, 0x30DD, 0x30F4, 0x30F7, 0x30F8, 0x30F9, 0x30FA, 0x30FE};
  for (char32_t k : kKana)
    if (k == cp) return true;
  return false;
}

// `text`(UTF-8)の中の上の58字を、`font`の字の時はJPM/JPGへ切り替えて出し、`font`へ戻す。
static std::string wrapMiscountedKana(const std::string &text, char font) {
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0xE3 && i + 2 < text.size()) {
      char32_t cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(text[i + 2]) & 0x3F);
      if (isMiscountedKana(cp)) {
        out += std::string(font == 'B' ? "\\f[JPG]" : "\\f[JPM]") + text.substr(i, 3) + "\\f[" + font + "]";
        i += 3;
        continue;
      }
    }
    out += text[i++];
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
std::string convertMarkupToMsRoffSource(const std::string &source) {
  const std::string markup = joinMarkupLinesAtBreaks(source);  // <br />(help_text.hpp参照)
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
  // 2026-10-06追加、直接のユーザー依頼(man(-mandoc)で見た時の見出しの「() ()」を、.THで「USILIC(1) ...」に):
  // 名前はUSILIC、章は1、日付は-nを実行した日、版はusilic 1.5b(モードラインの透かしと同じ決め打ち)、
  // 手引きの名前は日本語の原稿(仮名・漢字を含む)なら「ユーザーコマンド」、他は「User Commands」。-msで
  // 組む時は.THが無視されるだけ。.THは前置きより前(manのマクロが.po等を決めた後に前置きで上書きする)。
  // 2026-10-09から日本語の原稿も行長を決めない(英語と同じく、`man`が渡す画面の幅(MANWIDTH→-rLL)やusilicの
  // HELPが渡す-rLLでgroffが折り返す)。数え違える仮名はwrapMiscountedKana()でJPM/JPGにする。
  bool japanese = false;
  for (std::size_t k = 0; k + 2 < markup.size() && !japanese; k++) {
    unsigned char c = static_cast<unsigned char>(markup[k]);
    japanese = c == 0xE3 || (c >= 0xE4 && c <= 0xE9);  // U+3000-U+9FFF(仮名・漢字)
  }
  // 日付の"-"はハイフンの記号(‐)にならないよう\[u002D]で(前置きの.char -は.THの後なので効かない)。
  char date[64] = "";
  std::time_t now = std::time(nullptr);
  std::strftime(date, sizeof(date), "%Y\\[u002D]%m\\[u002D]%d", std::localtime(&now));
  out += ".TH USILIC 1 \"" + std::string(date) + "\" \"usilic 1.5b\" \"" +
         (japanese ? wrapMiscountedKana("ユーザーコマンド", 'R') : "User Commands") + "\"\n";
  out += ".\\\" usilic -n: -m(usilic自身の表示)と同じ見た目にするための前置き\n"
         ".nr PO 0\n.po 0\n.nr HM 0\n.nr FM 0\n.nr PL 100000v\n.pl 100000v\n.ds CH\n"
         ".nh\n.ad l\n"
         // 2026-10-09: 行頭・行末のタブ(HELPの目次・ページャー起動のKEYのCUTの目印)は、groffを通すと空白に
         // なって消えるので、usilicのHELPが組む時(-rusilic-help=1)だけ印U+E000を出す(usilicが組んだ後で
         // タブに戻す、Application::renderHelpRoff()参照)。manで読む時は何も出ない。
         ".ie r usilic-help .ds usilic-tab \\[uE000]\n.el .ds usilic-tab\n"
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
    bool firstOnLine = atLineStart;
    atLineStart = false;

    // 行頭・行末のタブは印(前置きのusilic-tab参照)。行末のタブの後の空白は捨てる。
    if (markup[i] == '\t') {
      std::size_t rest = markup.find_first_not_of(" \t", i + 1);
      std::size_t afterTags = rest;  // 行末の</indent>(1つ以上)の後も行末とみなす
      while (afterTags != std::string::npos && markup.compare(afterTags, indentClose.size(), indentClose) == 0)
        afterTags = markup.find_first_not_of(" \t", afterTags + indentClose.size());
      bool atLineEnd = afterTags == std::string::npos || markup[afterTags] == '\n';
      if (firstOnLine || atLineEnd) {
        out += "\\*[usilic-tab]";
        if (atLineEnd) {
          i = rest == std::string::npos ? markup.size() : rest;
          continue;
        }
      }
    }

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
      // 終わりは\\fPではなく\\fR(中の仮名のJPM/JPGへの切り替えで「前のフォント」が変わるため)。
      std::string span;
      for (std::size_t j = contentStart; j < closePos; j++) span += markup[j] == '\\' ? std::string("\\e") : std::string(1, markup[j]);
      out += isBold ? "\\fB" : "\\fI";
      out += japanese ? wrapMiscountedKana(span, isBold ? 'B' : 'I') : span;
      out += "\\fR";
      i = closePos + closeTag.size();
      continue;
    }

    if (japanese && static_cast<unsigned char>(markup[i]) == 0xE3 && i + 2 < markup.size()) {
      out += wrapMiscountedKana(markup.substr(i, 3), 'R');
      i += 3;
      continue;
    }
    appendEscaped(markup[i]);
    i++;
  }
  // 前置きの.pl 100000v(ページ区切りの空行を起こさないため)の分だけ
  // 末尾が空行で埋まらないよう、最後にページ長を現在位置まで縮める
  // (man自身の連続表示と同じ定番の手法)。
  ensureFreshLine();
  // 末尾の行(.TH)は見出しと同じ幅で。-mandoc(man)で組む時は、末尾の行をmanのマクロ自身が最後に出すので
  // ページ長を縮めない(縮めると末尾の行が出ない)。an-footerはgroffのan.tmacのマクロ。
  out += ".br\n.if !d an-footer .pl \\n(nlu+1v\n";
  return out;
}

}  // namespace fm
