#include "markdown_to_help_markup.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace fm {

namespace {

bool isBlank(const std::string &line) {
  for (char c : line)
    if (!std::isspace(static_cast<unsigned char>(c))) return false;
  return true;
}

std::string trimLeft(const std::string &line, std::size_t *widthOut = nullptr) {
  std::size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
  if (widthOut) *widthOut = i;
  return line.substr(i);
}

std::string trimRight(const std::string &s) {
  std::size_t end = s.size();
  while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
  return s.substr(0, end);
}

// 見出し(`# `〜`###### `)の判定。マッチすればレベル(1-6)と本文を返す。
bool matchHeading(const std::string &line, int *level, std::string *body) {
  std::size_t i = 0;
  while (i < line.size() && i < 6 && line[i] == '#') i++;
  if (i == 0 || i >= line.size()) return false;
  if (i < line.size() && line[i] != ' ') return false;  // "#foo"は見出しではない
  *level = static_cast<int>(i);
  *body = trimRight(trimLeft(line.substr(i)));
  return true;
}

// 水平線(`---`/`***`/`___`、間に空白を挟んでもよい、同じ文字が3つ以上)の判定。
bool isHorizontalRule(const std::string &trimmed) {
  if (trimmed.empty()) return false;
  char marker = trimmed[0];
  if (marker != '-' && marker != '*' && marker != '_') return false;
  int count = 0;
  for (char c : trimmed) {
    if (c == marker) count++;
    else if (c != ' ' && c != '\t')
      return false;
  }
  return count >= 3;
}

// コードフェンス(```/~~~、3文字以上)の判定。
bool isFence(const std::string &trimmed, char *marker) {
  if (trimmed.size() < 3) return false;
  char c = trimmed[0];
  if (c != '`' && c != '~') return false;
  std::size_t i = 0;
  while (i < trimmed.size() && trimmed[i] == c) i++;
  if (i < 3) return false;
  *marker = c;
  return true;
}

// 箇条書きの先頭記号(`-`/`*`/`+` + 空白、または`数字.`/`数字)` + 空白)の判定。
// マッチすれば、そのままの見た目(記号)をmarkerへ、残りの本文をbodyへ返す。
bool matchListItem(const std::string &line, std::string *marker, std::string *body) {
  if (line.empty()) return false;
  if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line.size() > 1 && line[1] == ' ') {
    *marker = line.substr(0, 2);
    *body = line.substr(2);
    return true;
  }
  std::size_t i = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) i++;
  if (i > 0 && i < line.size() && (line[i] == '.' || line[i] == ')') && i + 1 < line.size() && line[i + 1] == ' ') {
    *marker = line.substr(0, i + 2);
    *body = line.substr(i + 2);
    return true;
  }
  return false;
}

// 引用(`>` + 任意個の空白)の判定。ネストの深さ(`>`の個数)も返す。
bool matchBlockquote(const std::string &line, int *depth, std::string *body) {
  if (line.empty() || line[0] != '>') return false;
  int d = 0;
  std::size_t i = 0;
  while (i < line.size() && line[i] == '>') {
    d++;
    i++;
    if (i < line.size() && line[i] == ' ') i++;
  }
  *depth = d;
  *body = line.substr(i);
  return true;
}

std::string indentOpen(int width) {
  if (width < 0) width = 0;
  if (width > 99) width = 99;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "<indent%02d>", width);
  return buf;
}
constexpr const char *kIndentClose = "</indent>";

// 2026-09-22書き直し、直接のユーザー依頼("CommonMarkの強調解決アルゴリズム
// 対応が普通なら実装してほしい"): 太字/斜体の判定を、単純な「最初に見つかった
// 閉じ記号まで」という貪欲な1パスから、CommonMark仕様6.2節の
// delimiter run(区切り記号の連続)+左右フランキング規則+区切り記号スタックに
// よる解決アルゴリズムへ置き換えた。これにより、`*italic **bold** italic*`の
// ような、同じ記号(`*`)を斜体と太字の両方に使った入れ子や、`foo_bar_baz`の
// ような単語内`_`が強調にならない(CommonMarkの`_`専用規則)、といった実際の
// .mdファイルで一般的なパターンを正しく扱える。PipeBuffer::wrapLine()側の
// currentBold_/currentUnderline_追加(pipe_buffer.hpp参照)により、<b>の中に
// <i>等が入れ子になっても壊れなくなったため、この書き直しが安全にできる
// ようになった。
//
// 実装しているもの: 左右フランキング規則、`_`の単語内強調禁止規則、
// 区切り記号スタックによる後方探索、「3の倍数」規則(同じ記号が開始・終了の
// 両方になり得る場合の、`**foo*bar**baz*`のような紛らわしいケースの誤対応を
// 防ぐ)、部分消費(長さ3の区切り記号の一部だけを1回の対応に使い、残りを
// 次の対応に回す)。
// 実装していないもの(既知の限界): リンクの表示文字列の中の強調(意図的な
// 簡略化、リンクラベルは無変換のまま)、Unicodeの句読点分類(ASCII記号のみを
// 「句読点」として扱う――日本語の句読点や全角記号は「文字」側として扱われる
// ため、その境界でのフランキング判定は英語の句読点ほど厳密ではない)。

namespace {

bool isAsciiPunct(unsigned char c) {
  static const std::string punct = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  return punct.find(static_cast<char>(c)) != std::string::npos;
}

// 位置posの直前/直後の"実際の文字"の種類を見て、その位置にある区切り記号の連続が
// 左フランキング/右フランキングかを判定する(CommonMark 6.2節)。行の先頭・末尾は
// 空白とみなす。UTF-8の継続バイト(0x80以上)は句読点でも空白でもない「文字」
// として扱う(日本語の地の文の直後の`*`等が、素直に強調の対象になるように)。
struct FlankInfo {
  bool canOpen = false;
  bool canClose = false;
};

FlankInfo computeFlanking(const std::string &text, std::size_t start, std::size_t end, char delimChar) {
  auto classify = [&](long pos) -> int {  // 0=空白相当、1=句読点、2=文字
    if (pos < 0 || pos >= static_cast<long>(text.size())) return 0;
    unsigned char c = static_cast<unsigned char>(text[pos]);
    if (c == ' ' || c == '\t') return 0;
    if (c < 0x80 && isAsciiPunct(c)) return 1;
    return 2;
  };
  int before = classify(static_cast<long>(start) - 1);
  int after = classify(static_cast<long>(end));
  bool leftFlanking = after != 0 && (after != 1 || before == 0 || before == 1);
  bool rightFlanking = before != 0 && (before != 1 || after == 0 || after == 1);
  FlankInfo info;
  if (delimChar == '*') {
    info.canOpen = leftFlanking;
    info.canClose = rightFlanking;
  } else {  // '_': 単語内(前後とも「文字」)では強調にならない、CommonMark自身の規則
    info.canOpen = leftFlanking && (!rightFlanking || before == 1);
    info.canClose = rightFlanking && (!leftFlanking || after == 1);
  }
  return info;
}

// 1つの区切り記号の連続(**** 等)、または既に解決済みの内容(平文/コード/
// リンク/入れ子の強調)を表す。isDelimがfalseならtextが最終的な出力そのもの、
// trueならdelimChar/lenが未解決の区切り記号(lenが0になれば実質「無い」)。
struct Piece {
  bool isDelim = false;
  std::string text;
  char delimChar = 0;
  int len = 0;
  bool canOpen = false;
  bool canClose = false;
};

std::vector<Piece> tokenize(const std::string &text) {
  std::vector<Piece> pieces;
  std::size_t i = 0;
  const std::size_t n = text.size();
  std::string plain;
  auto flushPlain = [&]() {
    if (!plain.empty()) {
      pieces.push_back({false, plain, 0, 0, false, false});
      plain.clear();
    }
  };
  while (i < n) {
    char c = text[i];
    if (c == '\\' && i + 1 < n) {
      plain += text[i + 1];
      i += 2;
      continue;
    }
    if (c == '`') {
      std::size_t close = text.find('`', i + 1);
      if (close != std::string::npos) {
        flushPlain();
        pieces.push_back({false, "<u>" + text.substr(i + 1, close - i - 1) + "</u>", 0, 0, false, false});
        i = close + 1;
        continue;
      }
    }
    if (c == '!' && i + 1 < n && text[i + 1] == '[') {
      std::size_t altEnd = text.find(']', i + 2);
      if (altEnd != std::string::npos && altEnd + 1 < n && text[altEnd + 1] == '(') {
        std::size_t urlEnd = text.find(')', altEnd + 2);
        if (urlEnd != std::string::npos) {
          flushPlain();
          std::string alt = text.substr(i + 2, altEnd - i - 2);
          std::string url = text.substr(altEnd + 2, urlEnd - altEnd - 2);
          pieces.push_back({false, "[image] <u>" + alt + "</u> (" + url + ")", 0, 0, false, false});
          i = urlEnd + 1;
          continue;
        }
      }
    }
    if (c == '[') {
      std::size_t textEnd = text.find(']', i + 1);
      if (textEnd != std::string::npos && textEnd + 1 < n && text[textEnd + 1] == '(') {
        std::size_t urlEnd = text.find(')', textEnd + 2);
        if (urlEnd != std::string::npos) {
          flushPlain();
          std::string label = text.substr(i + 1, textEnd - i - 1);
          std::string url = text.substr(textEnd + 2, urlEnd - textEnd - 2);
          pieces.push_back({false, "<u>" + label + "</u> (" + url + ")", 0, 0, false, false});
          i = urlEnd + 1;
          continue;
        }
      }
    }
    if (c == '*' || c == '_') {
      flushPlain();
      std::size_t start = i;
      while (i < n && text[i] == c) i++;
      FlankInfo flank = computeFlanking(text, start, i, c);
      pieces.push_back({true, "", c, static_cast<int>(i - start), flank.canOpen, flank.canClose});
      continue;
    }
    plain += c;
    i++;
  }
  flushPlain();
  return pieces;
}

// 区切り記号スタックによる解決(CommonMark 6.2節の簡略版)。openerを後方探索
// する際、候補が「両方向フランキング」(開始にも終了にもなり得る)なら、
// 3の倍数規則(区切り記号の合計長が3の倍数で、かつ双方が3の倍数でない場合は
// 対応させない)を適用してスキップする。
void resolveEmphasis(std::vector<Piece> &pieces) {
  std::size_t i = 0;
  while (i < pieces.size()) {
    if (pieces[i].isDelim && pieces[i].canClose && pieces[i].len > 0) {
      long j = static_cast<long>(i) - 1;
      long foundJ = -1;
      for (; j >= 0; j--) {
        const Piece &p = pieces[static_cast<std::size_t>(j)];
        if (!p.isDelim || p.delimChar != pieces[i].delimChar || !p.canOpen || p.len <= 0) continue;
        bool bothFlank = (p.canOpen && p.canClose) || (pieces[i].canOpen && pieces[i].canClose);
        bool ruleOf3Blocks = bothFlank && ((p.len + pieces[i].len) % 3 == 0) && !(p.len % 3 == 0 && pieces[i].len % 3 == 0);
        if (!ruleOf3Blocks) {
          foundJ = j;
          break;
        }
      }
      if (foundJ >= 0) {
        std::size_t openIdx = static_cast<std::size_t>(foundJ);
        int strength = (pieces[openIdx].len >= 2 && pieces[i].len >= 2) ? 2 : 1;
        std::string inner;
        for (std::size_t k = openIdx + 1; k < i; k++) {
          inner += pieces[k].isDelim ? std::string(static_cast<std::size_t>(pieces[k].len), pieces[k].delimChar)
                                      : pieces[k].text;
        }
        std::string wrapped = (strength == 2 ? "<b>" : "<i>") + inner + (strength == 2 ? "</b>" : "</i>");
        pieces[openIdx].len -= strength;
        pieces[i].len -= strength;
        pieces.erase(pieces.begin() + static_cast<long>(openIdx) + 1, pieces.begin() + static_cast<long>(i));
        i = openIdx + 1;
        pieces.insert(pieces.begin() + static_cast<long>(i), Piece{false, wrapped, 0, 0, false, false});
        i++;  // 挿入した解決済みテキストの次、すなわちcloser自身の位置(残りがあれば再挑戦)
        continue;
      }
    }
    i++;
  }
}

std::string piecesToString(const std::vector<Piece> &pieces) {
  std::string out;
  for (const Piece &p : pieces)
    out += p.isDelim ? std::string(static_cast<std::size_t>(p.len), p.delimChar) : p.text;
  return out;
}

}  // namespace

// Markdownのインライン記法(太字/斜体/インラインコード/リンク・画像/バックスラッシュ
// エスケープ)を、1行分のプレーンテキスト(既に見出し・箇条書き等のブロック記号を
// 剥がした残り)に対して処理する。UTF-8のマルチバイト文字は、continuationバイトが
// 全て0x80以上でASCII記号(*_`[]()\)と衝突しないため、バイト単位の走査で安全に扱える。
std::string renderInline(const std::string &text) {
  std::vector<Piece> pieces = tokenize(text);
  resolveEmphasis(pieces);
  return piecesToString(pieces);
}

}  // namespace

std::string markdownToHelpMarkup(const std::string &text) {
  // 行ごとに分割する(末尾の改行の有無を記憶して、最後だけ復元する)。
  std::vector<std::string> lines;
  {
    std::string cur;
    for (char c : text) {
      if (c == '\n') {
        lines.push_back(cur);
        cur.clear();
      } else {
        cur += c;
      }
    }
    if (!cur.empty() || text.empty() || text.back() != '\n') lines.push_back(cur);
  }

  std::string out;
  bool inFence = false;
  char fenceMarker = '`';
  // 段落(空行・ブロック要素で区切られた、連続する平文の行)を1つにまとめてから
  // インライン変換する――**太字**等が、ソース上の(ソフトラップされた)行の
  // 境界をまたいでいても正しく認識されるようにするため(段落内はMarkdownの
  // 慣習どおり、行末2スペースの場合だけ改行を保ち、それ以外は空白1つで
  // つなぐ)。
  std::vector<std::string> paragraph;
  auto flushParagraph = [&]() {
    if (paragraph.empty()) return;
    std::string joined;
    for (std::size_t k = 0; k < paragraph.size(); k++) {
      bool hardBreak = paragraph[k].size() >= 2 && paragraph[k].substr(paragraph[k].size() - 2) == "  ";
      joined += trimRight(paragraph[k]);
      if (k + 1 < paragraph.size()) joined += hardBreak ? "\n" : " ";
    }
    out += renderInline(joined);
    out += '\n';
    paragraph.clear();
  };

  for (std::size_t li = 0; li < lines.size(); li++) {
    const std::string &raw = lines[li];
    std::size_t leadWidth = 0;
    std::string trimmed = trimLeft(raw, &leadWidth);
    char marker;
    if (isFence(trimRight(trimmed), &marker)) {
      flushParagraph();
      if (!inFence) {
        inFence = true;
        fenceMarker = marker;
      } else if (marker == fenceMarker) {
        inFence = false;
      }
      continue;  // フェンス行自体(```や言語名)は出力しない
    }
    if (inFence) {
      // コードブロックの中身はインライン記法を解釈せず、字下げだけ与える。
      out += indentOpen(4) + raw + kIndentClose + '\n';
      continue;
    }
    if (isBlank(raw)) {
      flushParagraph();
      out += '\n';
      continue;
    }
    int level;
    std::string body;
    if (matchHeading(trimmed, &level, &body)) {
      flushParagraph();
      // 2026-09-22修正: <b>/<u>がトグル方式になり入れ子が安全になった(pipe_buffer.hpp
      // 自身のcurrentBold_/currentUnderline_参照)ので、見出し本文にも通常の
      // renderInline()を適用する(本文中の**強調**等が正しく効くようになった)。
      std::string rendered = renderInline(body);
      if (level == 1) {
        out += "<u><b>" + rendered + "</b></u>\n";  // 最上位は太字+下線で強調
      } else if (level == 2) {
        out += "<b>" + rendered + "</b>\n";
      } else {
        out += indentOpen((level - 2) * 2) + "<b>" + rendered + "</b>" + kIndentClose + '\n';
      }
      continue;
    }
    if (isHorizontalRule(trimRight(trimmed))) {
      flushParagraph();
      out += std::string(60, '-') + '\n';
      continue;
    }
    int depth;
    std::string quoteBody;
    if (matchBlockquote(trimmed, &depth, &quoteBody)) {
      flushParagraph();
      out += indentOpen(depth * 2) + "<i>" + renderInline(trimRight(quoteBody)) + "</i>" + kIndentClose + '\n';
      continue;
    }
    std::string listMarker, listBody;
    if (matchListItem(trimmed, &listMarker, &listBody)) {
      flushParagraph();
      int nestLevel = static_cast<int>(leadWidth / 2);  // 2スペースごとに1段
      out += indentOpen(nestLevel * 2) + listMarker + renderInline(trimRight(listBody)) + kIndentClose + '\n';
      continue;
    }
    // それ以外は平文――段落として蓄積し、空行/ブロック要素まで結合を待つ。
    paragraph.push_back(raw);
  }
  flushParagraph();
  // 元がちょうどNの行の末尾に改行を含まなかった場合、flushParagraph()/各分岐が
  // 常に付ける末尾の'\n'が1つ余分になる――行分割自体が「最後の空でない断片」を
  // 末尾の改行の有無に関わらず1行として数えているため、この余分な'\n'を除く。
  if (!text.empty() && text.back() != '\n' && !out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

}  // namespace fm
