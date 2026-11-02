#include "key_bindings.hpp"

#include <algorithm>
#include <sstream>

namespace fm {

namespace {
constexpr int kCtrlX = 0x18, kCtrlJ = 0x0a, kCtrlO = 0x0f, kCtrlC = 0x03, kEsc = 0x1b;

bool isLeaderByte(int b) { return b == kCtrlX || b == kCtrlJ || b == kCtrlO || b == kEsc; }
}  // namespace

const std::vector<const char *> &KeyBindings::defaultPrefixSpecs() {
  static const std::vector<const char *> prefixes = {"C-x", "C-j", "C-o", "C-c", "C-j C-o"};
  return prefixes;
}

// K番号の組み込みの表。K-1〜K-69はTUTORIAL(1〜5章、6章の「Key」)に出てくる順、
// K-70〜K-89はTUTORIALの本文に出てこないキー、K-90〜K-92は^T/^H(HELP)、
// K-93〜K-95はマクロ――直接のユーザー依頼(requirements.md参照)。src/usilicrc/
// usilicrc.enのKEY節はこの表と同じ内容で、test_key_bindings.cppがそれを確かめる。
// 2026-09-26追記、直接のユーザー依頼("今のK-1、K-2、...とキーアサインとの関係を
// 内部で保持してもらってusilicrc上の順序を変えても組み合わせは不変みたいな感じで"):
// K番号は機能ごとの「固定のID」とし、今後も振り直さない(上の「TUTORIALに出てくる
// 順」は2026-09-25時点の初期割り当ての由来にすぎない)。機能を追加する時は
// K-96以降の続きの番号を付ける。usilicrc上では行を好きな順(例えばTUTORIALの
// 順)に並べてよい――configure()は番号で引くだけで、行の順序も番号の並びも
// 使わない。
const std::vector<KeyBindings::DefaultEntry> &KeyBindings::defaultEntries() {
  static const std::vector<DefaultEntry> entries = {
      // 2026-10-02、直接のユーザー依頼(.steering/20261002-help-key-usilicrc-reorg/): HELPの記載順に
      // 振り直した(それまでの番号は2026-09-25のTUTORIALの登場順に追加分を足したもの)。
      {1, "j"},          // j     画面を1行下へ
      {2, "k"},          // k     画面を1行上へ
      {3, "C-n"},        // ^N    カーソルを1行下へ
      {4, "C-p"},        // ^P    カーソルを1行上へ
      {5, "SPC"},        // ␣     ページ送り/クイックルック
      {6, "C-v"},        // ^V    ページ送り
      {7, "C-f"},        // ^F    ページ送り
      {8, "b"},          // b     ページ戻し
      {9, "C-b"},        // ^B    ページ戻し
      {10, "ESC v"},     // ^[v   ページ戻し
      {11, "ESC <"},     // ^[<   先頭へ
      {12, "ESC >"},     // ^[>   末尾へ
      {13, "ESC N"},     // ^[N   N行目へ
      {14, "F-0 F-0"},   // ^X^X  大きく移動する前の行と行き来
      {15, "n"},         // n     次のfile(pager)
      {16, "p"},         // p     前のfile(pager)
      {17, "x"},         // x     最初のfile(pager)
      {18, "f"},         // f     最後のfile(pager)
      {19, "q"},         // q     pagerの終了
      {20, "C-g"},       // ^G    前のモードへ戻る
      {21, "C-l"},       // ^L    再表示
      {22, "C-z"},       // ^Z    サスペンド
      {23, "F-2 f"},     // ^Of   mode-line の表示/非表示(2026-10-06、^Ohから。2026-10-13に^Osから)
      {24, "F-1 C-c"},   // ^J^C  カレントディレクトリへ
      {25, "C-]"},       // ^]    カレントディレクトリへ(^J^Cと同じ)
      {26, "F-1 C-b"},   // ^J^B  親ディレクトリへ(2026-10-02、^J^Kから)
      {27, "F-1 N"},     // ^JN   N番目のディレクトリへ
      {28, "C-m"},       // ^M    開く/表示
      {29, "F-2 C-m"},   // ^O^M  1行ずつ読む
      {30, "ESC C-m"},   // ^[^M  そのまま読む
      {31, "F-1 F-1"},   // ^J^J  以前のfileへ
      {32, "F-2 o"},     // ^Oo   ls の表示形式
      {33, "F-2 l"},     // ^Ol   ls のソートキー巡回(2026-10-05、^O^Lと交換)
      {34, "F-2 C-l"},   // ^O^L  再表示(SHELL内から)/ls のソート順を逆順に
      {35, "."},         // .     選択/非選択
      {36, "C-SPC"},     // ^SPC  選択/非選択
      {37, "F-2 y"},     // ^Oy   全選択
      {38, "F-2 n"},     // ^On   全解除
      // (39: ^[P 一時的な選択対象の貼り付け、2026-10-05廃止。番号は詰めない)
      {40, "F-2 c"},     // ^Oc   CP
      {41, "F-2 m"},     // ^Om   MV
      {42, "F-2 r"},     // ^Or   RM
      {43, "F-1 t"},     // ^Jt   TOOL: 入力(2026-10-10、PROGRAMから改名、^Jpから)
      {44, "F-1 C-t"},   // ^J^T  TOOL: 一覧(2026-10-10、^J^Pから)
      {45, "F-2 u"},     // ^Ou   選択したfileを外部ページャーのusilicで閲覧(2026-10-09)
      {46, "F-2 a"},     // ^Oa   ARC: 展開
      {47, "F-1 e"},     // ^Je   EPUB: 入力
      {48, "F-1 C-e"},   // ^J^E  EPUB: 一覧
      {49, "F-2 e"},     // ^Oe   EPUB: 縦書き/横書き
      {50, "C-r"},       // ^R    履歴一覧
      {51, "C-s"},       // ^S    逐次検索
      {52, "F-1 C-s"},   // ^J^S  EGREP
      {53, "F-1 p"},     // ^Jp   PART: バッファ(2026-10-10、CUTから改名、^Cbから)
      // (54: ^Cf CUT: file、2026-10-10廃止。番号は詰めない)
      {55, "F-1 d"},     // ^Jd   DU: 集計
      {56, "F-1 C-d"},   // ^J^D  DU: 結果へ戻る
      {57, "F-1 l"},     // ^Jl   LOCATE: データベース作成
      {58, "F-1 C-l"},   // ^J^L  LOCATE
      {59, "F-1 m"},     // ^Jm   MAN: 入力
      {60, "F-1 C-m"},   // ^J^M  MAN: 一覧
      {61, "F-0 f"},     // ^Xf   FILE: 入力
      {62, "F-0 C-f"},   // ^X^F  FILE: 一覧
      {63, "F-0 d"},     // ^Xd   DIR: 入力
      {64, "F-0 C-d"},   // ^X^D  DIR: 一覧
      {65, "F-2 C-c"},   // ^O^C  COPY
      {66, "F-2 C-y"},   // ^O^Y  ペースト
      {67, "ESC e"},     // ^[e    EDIT(2026-10-02、eから。eはusilicrcのMACRO)
      {68, "ESC s"},     // ^[s   SHELLへ
      {69, "F-1 s"},     // ^Js   SHELLを再起動(2026-10-02、^Osから)
      {70, "ESC l"},     // ^[l   SHELLのログ
      {71, "F-1 ."},     // ^J.   SHELL: カレントディレクトリへcd
      {72, "F-1 ,"},     // ^J,   SHELL: ls -l
      {73, "F-1 w"},     // ^Jw   SHELL: アクセス名
      {74, "F-1 C-w"},   // ^J^W  SHELL: バッファの内容
      {75, "F-2 /"},     // ^O/   検索(SHELL内から)
      {76, "F-2 C-f"},   // ^O^F  ページ送り(SHELL内から)
      {77, "F-2 C-b"},   // ^O^B  ページ戻し(SHELL内から)
      {78, "ESC u"},     // ^[u   USILIC: 目次
      {79, "F-1 C-u"},   // ^J^U  USILIC: 種類ごとの一覧
      {80, "F-1 u"},     // ^Ju   USILIC: 時系列
      {81, "F-0 C-w"},   // ^X^W  バッファを作業fileへ保存
      {82, "C-i"},       // ^I    INFO
      {83, "F-1 C-i"},   // ^J^I  INFO(SHELL内から、2026-10-03、^O^Iから)
      {84, "F-4 u"},     // ^J^Ou ~/.usilic/usilicrc
      {85, "F-4 e"},     // ^J^Oe ERROR
      {86, "F-4 s"},     // ^J^Os SHELLのログ(2026-09-27、^J^Olから)
      // (87: ^Oh HELPの目次(CUT)と平文の切り替え、2026-10-14廃止。番号は詰めない)
      {88, "F-4 d"},     // ^J^Od ~/.usilic/data/
      {89, "F-4 w"},     // ^J^Ow WORK
      {90, "C-h"},       // ^H    HELP
      {91, "F-1 C-h"},   // ^J^H  HELP
      {92, "F-2 j"},     // ^Oj   HELP の言語切替(2026-10-07、^Jaから。トグルは^Oに揃える)
      {93, "C-k"},       // ^K    KEY
      {94, "F-1 C-k"},   // ^J^K  KEY(SHELL・プロンプトから)
      {95, "F-0 ("},     // ^X(   マクロの記録開始
      {96, "F-0 )"},     // ^X)   マクロの記録終了
      {97, "F-0 e"},     // ^Xe   マクロの実行
      {98, "F-0 s"},     // ^Xs   usilicrcの読み直し・マクロの保存と一覧(2026-10-03、^X^Sと^Xsを一本化)
      {99, "F-0 C-c"},   // ^X^C  EXIT
      {100, "ESC ESC"},  // ^[^[  EXIT
      {101, "F-2 s"},    // ^Os   EPUB: 章区切り/連続スクロール(2026-10-03、既存のK番号をずらさないよう末尾へ。2026-10-13に^O^Eから)
      {102, "F-2 w"},    // ^Ow   EPUB: Webのリンクの対処を巡回(2026-10-04、usilicrcのURL)
      {103, "F-1 C-a"},  // ^J^A  ARC: 一覧(2026-10-05、アーカイブだけのFILE履歴)
      {104, "F-1 C-f"},  // ^J^F  -g: フォント一覧(2026-10-06、2026-10-07に^Ofから)
      {105, "h"},        // h     KEYから平文のHELPの当該位置へ(2026-10-08復活、2026-10-19に^J^Ohから)
      {106, "F-1 C-p"},  // ^J^P  PART: 一覧(2026-10-10、FILE履歴の「file "文字列"」だけ)
      {107, "F-2 d"},    // ^Od   ホイールの向きの切り替え(2026-10-11)
      {108, "F-2 C-e"},  // ^O^E  EPUB: 記録の一覧(2026-10-13、縦書きのCOPYで範囲指定して記録したもの)
      {109, "ESC ["},    // ^[[   -g: 行間を1ドット狭める(2026-10-17、2026-11-02に^[]と入れ替え、縦書きは列の間)
      {110, "ESC ]"},    // ^[]   -g: 行間を1ドット広げる(2026-10-17、2026-11-02に^[[と入れ替え、縦書きは列の間)
      {111, "F-2 t"},    // ^Ot   明朝体・ゴシック体の切り換え(2026-10-17、~/.usilic/historyに残す。2026-10-20からEPUB以外の和文も)
      {112, "ESC {"},    // ^[{   -g: 字間を1ドット狭める(2026-10-20、2026-11-02に^[}と入れ替え)
      {113, "ESC }"},    // ^[}   -g: 字間を1ドット広げる(2026-10-20、2026-11-02に^[{と入れ替え)
      // 2026-10-23(Scope参照): 今までコードに書いた値で受けていたキー。全体の番号。
      {114, "ESC g g"},      // ^[gg  指定行へ(Emacs M-g g、^[Nと同じ)
      {115, "C-u"},          // ^U    次のコマンドの回数
      {116, "F-0 z"},        // ^Xz   直前のコマンドをもう一度
      {117, "F-0 r m"},      // ^Xrm  ブックマークを置く
      {118, "F-0 r b"},      // ^Xrb  ブックマークへ飛ぶ
      {119, "F-0 r l"},      // ^Xrl  ブックマークの一覧
      {120, "F-0 r SPC"},    // ^Xr␣  今の場所をレジスタへ
      {121, "F-0 r C-SPC"},  // ^Xr^␣ 今の場所をレジスタへ(^Xr␣と同じ)
      {122, "F-0 r j"},      // ^Xrj  レジスタの場所へ飛ぶ
      {123, "F-2 C-s"},      // ^O^S  一致した行の一覧(FGREP)
      {124, "F-2 F-1 C-s"},  // ^O^J^S 一致した行の一覧(EGREP)
      {125, "%"},            // %     (ls): 名前が正規表現に合うファイルを選ぶ
      {126, "*"},            // *     (ls): 拡張子が同じファイルを選ぶ
      {127, "C-t n"},        // ^Tn   次の窓へ
      {128, "C-t p"},        // ^Tp   前の窓へ
      {129, "C-t C-h"},      // ^T^H  HELPの窓
      {130, "C-t C-k"},      // ^T^K  KEYの窓
      {131, "e", Scope::MacroRepeat},  // e  ^Xeの直後: マクロをもう一度
      // COPYの中だけの番号(^N/^P・^G・^@・.・^[N・^[gg・^[^[・^X^Cは全体の番号と共有)
      {132, "C-f", Scope::Copy},      // ^F    COPY: 1文字進む
      {133, "C-b", Scope::Copy},      // ^B    COPY: 1文字戻る
      {134, "C-a", Scope::Copy},      // ^A    COPY: 行頭へ
      {135, "C-e", Scope::Copy},      // ^E    COPY: 行末へ
      {136, "F-0 SPC", Scope::Copy},  // ^X␣   COPY: 矩形の範囲
      {137, "ESC w", Scope::Copy},    // ^[w   COPY: クリップボードへ
      {138, "ESC t", Scope::Copy},    // ^[t   COPY: TOOLのプロンプトへ
      // プロンプトの中だけの番号(^M・^G・^R・^N/^P・^S・^O^Yは全体の番号と共有)
      {139, "C-a", Scope::Prompt},    // ^A    入力: 行頭へ
      {140, "C-e", Scope::Prompt},    // ^E    入力: 行末へ
      {141, "C-f", Scope::Prompt},    // ^F    入力: 1文字進む
      {142, "C-b", Scope::Prompt},    // ^B    入力: 1文字戻る
      {143, "C-d", Scope::Prompt},    // ^D    入力: カーソルの字を削除
      {144, "C-h", Scope::Prompt},    // ^H    入力: 前の字を削除(DELも)
      {145, "C-u", Scope::Prompt},    // ^U    入力: 行頭まで削除
      {146, "C-k", Scope::Prompt},    // ^K    入力: 行末まで削除
      {147, "C-y", Scope::Prompt},    // ^Y    入力: 削除した字を入れ直す
      {148, "C-_", Scope::Prompt},    // ^_    入力: 取り消し
      {149, "C-q", Scope::Prompt},    // ^Q    入力: 次の字をそのまま入れる
      {150, "C-i", Scope::Prompt},    // ^I    入力: パスの補完
      {151, "C-w", Scope::Prompt},    // ^W    入力: 以前のディレクトリ(CP/MVの転送先)
      {152, "ESC f", Scope::Prompt},  // ^[f   入力: 1語進む
      {153, "ESC b", Scope::Prompt},  // ^[b   入力: 1語戻る
      {154, "ESC d", Scope::Prompt},  // ^[d   入力: 1語削除
      {155, "ESC C-h", Scope::Prompt},  // ^[^H 入力: 前の1語を削除(^[DELも)
  };
  return entries;
}

std::optional<KeyBindings::Sequence> KeyBindings::parseSpec(const std::string &spec,
                                                           const std::vector<Sequence> &prefixes,
                                                           std::string *error) {
  std::istringstream in(spec);
  std::string token;
  Sequence out;
  bool sawDigit = false;
  while (in >> token) {
    if (sawDigit) {
      if (error) *error = "N must be the last key: " + spec;
      return std::nullopt;
    }
    if (token.size() == 3 && token.compare(0, 2, "F-") == 0 && token[2] >= '0' && token[2] <= '9') {
      int n = token[2] - '0';
      if (n >= static_cast<int>(prefixes.size()) || prefixes[n].empty()) {
        if (error) *error = "undefined prefix " + token + ": " + spec;
        return std::nullopt;
      }
      out.insert(out.end(), prefixes[n].begin(), prefixes[n].end());
    } else if (token == "C-SPC" || token == "C-@") {
      out.push_back(0x80);  // KeyInputは^Space(NUL)を0x80として報告する
    } else if (token.size() == 3 && token.compare(0, 2, "C-") == 0) {
      char c = token[2];
      if (c >= 'a' && c <= 'z') out.push_back(c - 'a' + 1);
      else if (c == '[' || c == '\\' || c == ']' || c == '^' || c == '_') out.push_back(c - 0x40);
      else {
        if (error) *error = "invalid key " + token + ": " + spec;
        return std::nullopt;
      }
    } else if (token == "ESC") {
      out.push_back(kEsc);
    } else if (token == "SPC") {
      out.push_back(' ');
    } else if (token == "N") {
      out.push_back(kDigit);
      sawDigit = true;
    } else if (token.size() == 1 && static_cast<unsigned char>(token[0]) > 0x20 &&
               static_cast<unsigned char>(token[0]) < 0x7f) {
      out.push_back(static_cast<unsigned char>(token[0]));
    } else {
      if (error) *error = "invalid key " + token + ": " + spec;
      return std::nullopt;
    }
  }
  if (out.empty()) {
    if (error) *error = "empty";
    return std::nullopt;
  }
  return out;
}

namespace {
// 数字の族(kDigit)を考慮した、パターン同士の「同じキー列か」「一方が他方の
// 真の接頭か」の判定(configure()の重複/接頭の検査用)。
bool samePattern(const KeyBindings::Sequence &a, const KeyBindings::Sequence &b) { return a == b; }
bool patternIsProperPrefix(const KeyBindings::Sequence &shorter, const KeyBindings::Sequence &longer) {
  if (shorter.size() >= longer.size()) return false;
  for (std::size_t i = 0; i < shorter.size(); i++) {
    int s = shorter[i], l = longer[i];
    if (s == l) continue;
    bool sDigitMatchesL = s == KeyBindings::kDigit && l >= '0' && l <= '9';
    bool lDigitMatchesS = l == KeyBindings::kDigit && s >= '0' && s <= '9';
    if (!sDigitMatchesL && !lDigitMatchesS) return false;
  }
  return true;
}
std::string fnName(int n) { return "F-" + std::to_string(n); }
std::string knName(int n) { return "K-" + std::to_string(n); }
}  // namespace

std::string KeyBindings::label(const Sequence &seq) {
  std::string out;
  for (int b : seq) {
    if (b == kDigit) out += "N";
    else if (b == 0x80 || b == 0) out += "^\xE2\x90\xA3";  // ^␣(Ctrl-Space)
    else if (b == ' ') out += "\xE2\x90\xA3";              // ␣
    else if (b == 0x1b) out += "^[";
    else if (b < 0x20) out += std::string("^") + static_cast<char>(b + '@');
    else if (b == 0x7f) out += "^?";
    else out += static_cast<char>(b);
  }
  return out;
}

namespace {
// 表示幅(全角は2、U+FEFF等の幅0の印は0)。「キー：」の行の桁合わせ用。
int helpDisplayWidth(const std::string &s) {
  int width = 0;
  for (std::size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if (len == 3 && i + 2 < s.size()) {
      unsigned cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(s[i + 2]) & 0x3F);
      if (cp == 0xFEFF || cp == 0x2060) width += 0;
      else if (cp == 0x2423) width += 1;  // ␣
      else width += 2;
    } else {
      width += len == 1 ? 1 : 2;
    }
    i += len;
  }
  return width;
}
}  // namespace

std::string KeyBindings::rewriteHelpText(const std::string &text) const {
  if (changed_.empty()) return text;
  std::vector<std::pair<std::string, std::string>> caretLabels, keyColumnLabels;
  for (const auto &[from, to] : changed_) {
    std::string a = label(from), b = label(to);
    keyColumnLabels.push_back({a, b});
    if (a.size() >= 2 && a[0] == '^') caretLabels.push_back({a, b});
  }
  // 本文: 「^」で始まる表記を、一度の走査で最も長く一致するものから置き換える。
  auto rewriteCarets = [&](const std::string &s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
      const std::pair<std::string, std::string> *best = nullptr;
      if (s[i] == '^')
        for (const auto &kv : caretLabels)
          if (s.compare(i, kv.first.size(), kv.first) == 0 && (!best || kv.first.size() > best->first.size()))
            best = &kv;
      // 一致の後ろがさらに表記の続き(^x)なら、もっと長いキーの一部なので置き換えない。
      if (best) {
        std::size_t end = i + best->first.size();
        bool longer = end + 1 < s.size() && s[end] == '^' && s[end + 1] != ' ';
        if (!longer || best->first.find('N') != std::string::npos) {
          out += best->second;
          i = end;
          continue;
        }
      }
      out += s[i++];
    }
    return out;
  };
  static const std::string kColon = "\xEF\xBC\x9A";  // 「：」
  std::string out;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t nl = text.find('\n', start);
    std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    // 「キー：説明」の行: 行頭の空白(U+FEFFの印を含む)、キー欄、「：」。
    std::size_t keyStart = 0;
    while (keyStart < line.size()) {
      if (line[keyStart] == ' ') keyStart++;
      else if (line.compare(keyStart, 3, "\xEF\xBB\xBF") == 0) keyStart += 3;
      else break;
    }
    std::size_t colon = line.find(kColon, keyStart);
    std::string key = colon == std::string::npos ? std::string() : line.substr(keyStart, colon - keyStart);
    if (!key.empty() && key.find(' ') == std::string::npos) {
      std::string replaced = key;
      for (const auto &kv : keyColumnLabels)
        if (kv.first == key) replaced = kv.second;
      if (replaced == key) replaced = rewriteCarets(key);
      std::string lead = line.substr(0, keyStart);
      if (replaced != key) {
        // 「：」の桁を保つ: 表記の幅の差だけ、前の空白を詰める/足し、空白で足りなければ字下げの印
        // (U+FEFF、個数-1が字下げの幅、PipeBuffer参照)を減らす(1つは残す)。
        int diff = helpDisplayWidth(replaced) - helpDisplayWidth(key);
        int spaces = 0, marks = 0;
        for (std::size_t k = 0; k < lead.size();) {
          if (lead[k] == ' ') spaces++, k++;
          else marks++, k += 3;
        }
        if (diff < 0) spaces -= diff;
        else {
          int fromSpaces = std::min(spaces, diff);
          spaces -= fromSpaces;
          marks = std::max(marks > 0 ? 1 : 0, marks - (diff - fromSpaces));
        }
        lead.clear();
        for (int k = 0; k < marks; k++) lead += "\xEF\xBB\xBF";
        lead += std::string(static_cast<std::size_t>(spaces), ' ');
      }
      line = lead + replaced + rewriteCarets(line.substr(colon));
    } else {
      line = rewriteCarets(line);
    }
    out += line;
    if (nl == std::string::npos) break;
    out += '\n';
    start = nl + 1;
  }
  return out;
}

void KeyBindings::configure(const std::map<int, std::string> &userPrefixes,
                            const std::map<int, std::string> &userEntries, std::vector<std::string> *errors,
                            const std::vector<Macro> &macros) {
  rules_.clear();
  auto report = [&](const std::string &message) {
    if (errors) errors->push_back("KEY: " + message);
  };

  // 既定の接頭キー(F-n)と、利用者の接頭キー。F-nの中ではより小さい番号の
  // F-nを使ってよい(既定のF-4 = C-j C-oのように、自分自身の展開は不要)。
  std::vector<Sequence> defaultPrefixes(kPrefixCount), userPrefixSeqs(kPrefixCount);
  for (int n = 0; n < kPrefixCount; n++) {
    std::string error;
    defaultPrefixes[n] = *parseSpec(defaultPrefixSpecs()[n], defaultPrefixes, &error);
  }
  for (int n = 0; n < kPrefixCount; n++) {
    userPrefixSeqs[n] = defaultPrefixes[n];
    auto it = userPrefixes.find(n);
    // キーが空のF-nは既定のまま(接頭キーを空にするとその下の機能が全て使え
    // なくなるので意味が無く、旧書式のKEY節の"F-0"だけの行もこれに当たる)。
    if (it == userPrefixes.end() || it->second.find_first_not_of(" \t") == std::string::npos) continue;
    std::string error;
    auto seq = parseSpec(it->second, userPrefixSeqs, &error);
    if (!seq) report(fnName(n) + ": " + error);
    else if (std::find(seq->begin(), seq->end(), kDigit) != seq->end()) report(fnName(n) + ": N is not allowed");
    else userPrefixSeqs[n] = *seq;
  }
  for (const auto &kv : userPrefixes)
    if (kv.first < 0 || kv.first >= kPrefixCount) report("unknown " + fnName(kv.first));

  // 各K番号の既定のキー列と利用者のキー列(割り当て無しはnullopt)。
  struct Resolved {
    int number;
    Sequence defaultSeq;
    std::optional<Sequence> userSeq;
    Scope scope = Scope::Global;
  };
  std::vector<Resolved> resolved;
  for (const auto &entry : defaultEntries()) {
    std::string error;
    Resolved r{entry.number, *parseSpec(entry.spec, defaultPrefixes, &error), std::nullopt, entry.scope};
    auto it = userEntries.find(entry.number);
    if (it == userEntries.end()) {
      r.userSeq = parseSpec(entry.spec, userPrefixSeqs, &error);
    } else if (it->second.find_first_not_of(" \t") != std::string::npos) {
      auto seq = parseSpec(it->second, userPrefixSeqs, &error);
      if (!seq) {
        report(knName(entry.number) + ": " + error);
        r.userSeq = parseSpec(entry.spec, userPrefixSeqs, &error);
      } else {
        bool defaultHasDigit = std::find(r.defaultSeq.begin(), r.defaultSeq.end(), kDigit) != r.defaultSeq.end();
        bool userHasDigit = std::find(seq->begin(), seq->end(), kDigit) != seq->end();
        if (defaultHasDigit != userHasDigit) {
          report(knName(entry.number) + ": N must be used exactly when the default uses it");
          r.userSeq = parseSpec(entry.spec, userPrefixSeqs, &error);
        } else if (entry.scope == Scope::MacroRepeat && seq->size() != 1) {
          report(knName(entry.number) + ": must be one key");
          r.userSeq = parseSpec(entry.spec, userPrefixSeqs, &error);
        } else {
          r.userSeq = *seq;
        }
      }
    }
    resolved.push_back(std::move(r));
  }
  for (const auto &kv : userEntries) {
    bool known = std::any_of(defaultEntries().begin(), defaultEntries().end(),
                             [&](const DefaultEntry &e) { return e.number == kv.first; });
    if (!known) report("unknown " + knName(kv.first));
  }

  // 場面ごとの番号の組: 全体の番号と、COPY/プロンプトの番号+それと既定のキーが同じでない全体の番号(Scope参照)。
  auto setFor = [&](Scope scope) {
    std::vector<std::size_t> set;
    for (std::size_t i = 0; i < resolved.size(); i++) {
      if (resolved[i].scope == scope) set.push_back(i);
      if (resolved[i].scope != Scope::Global || scope == Scope::Global) continue;
      bool shadowed = std::any_of(resolved.begin(), resolved.end(), [&](const Resolved &o) {
        return o.scope == scope && o.defaultSeq == resolved[i].defaultSeq;
      });
      if (!shadowed) set.push_back(i);
    }
    std::sort(set.begin(), set.end());
    return set;
  };
  // 重なり(同じキー・あるキーが別のキーの頭)は場面ごとに見て、後の方を割り当て無しにする。COPY/プロンプトの組では、
  // その場面の番号が関わる重なりだけ(全体の番号どうしは全体の組で見る)。
  for (Scope scope : {Scope::Global, Scope::Copy, Scope::Prompt}) {
    std::vector<std::size_t> set = setFor(scope);
    for (std::size_t x = 0; x < set.size(); x++) {
      Resolved &ri = resolved[set[x]];
      if (!ri.userSeq) continue;
      for (std::size_t y = 0; y < x; y++) {
        const Resolved &rj = resolved[set[y]];
        if (!rj.userSeq) continue;
        if (scope != Scope::Global && ri.scope == Scope::Global && rj.scope == Scope::Global) continue;
        const Sequence &first = *rj.userSeq, &second = *ri.userSeq;
        if (samePattern(first, second) || patternIsProperPrefix(first, second) || patternIsProperPrefix(second, first)) {
          report(knName(ri.number) + " conflicts with " + knName(rj.number));
          ri.userSeq.reset();
          break;
        }
      }
    }
  }
  macroRepeatKey_ = -1;
  for (const auto &r : resolved)
    if (r.scope == Scope::MacroRepeat && r.userSeq && r.userSeq->size() == 1) macroRepeatKey_ = (*r.userSeq)[0];

  // HELPの書き換え(rewriteHelpText())は、全体の番号と、既定の表記が全体の番号に無い場面の番号(^[w等)だけ。
  changed_.clear();
  for (const auto &r : resolved) {
    if (!r.userSeq || *r.userSeq == r.defaultSeq || r.scope == Scope::MacroRepeat) continue;
    if (r.scope != Scope::Global &&
        std::any_of(resolved.begin(), resolved.end(),
                    [&](const Resolved &o) { return o.scope == Scope::Global && o.defaultSeq == r.defaultSeq; }))
      continue;
    changed_.push_back({r.defaultSeq, *r.userSeq});
  }

  std::vector<int> leaderFirstBytes = {kEsc};
  for (int n : {0, 1, 2, 4})
    if (!userPrefixSeqs[n].empty() && userPrefixSeqs[n][0] != kCtrlC) leaderFirstBytes.push_back(userPrefixSeqs[n][0]);
  auto leaderEligible = [&](const Sequence &from, const Sequence &defaultSeq) {
    return from.size() >= 2 && isLeaderByte(defaultSeq[0]) &&
           std::find(leaderFirstBytes.begin(), leaderFirstBytes.end(), from[0]) != leaderFirstBytes.end();
  };
  // プロンプトの中: ^X・^J・^O・ESCで始まるキー(LeaderOnlyと同じ)と、制御文字1字のキー(既定も制御文字1字か
  // ^X等で始まるもの)。打った字になる1字のキー(j・n等、利用者のキーが1字の字のもの)は置き換えない。
  auto isControl = [](int b) { return (b >= 0 && b < 0x20) || b == 0x7f || b == 0x80; };
  auto promptEligible = [&](const Sequence &from, const Sequence &defaultSeq) {
    if (leaderEligible(from, defaultSeq)) return true;
    bool fromControl = (from.size() == 1 && isControl(from[0])) ||
                       (from.size() >= 2 && std::find(leaderFirstBytes.begin(), leaderFirstBytes.end(), from[0]) !=
                                                leaderFirstBytes.end());
    bool defaultControl = (defaultSeq.size() == 1 && isControl(defaultSeq[0])) ||
                          (defaultSeq.size() >= 2 && isLeaderByte(defaultSeq[0]));
    return fromControl && defaultControl;
  };

  // 組ごとの規則: 利用者のキー列→既定のキー列。既定のキー列は、組の中で他の番号のキーとして使われていなければ無効化。
  auto buildRules = [&](const std::vector<std::size_t> &set) {
    std::vector<Rule> rules;
    for (std::size_t i : set) {
      const Resolved &r = resolved[i];
      if (r.userSeq && *r.userSeq == r.defaultSeq) continue;
      if (r.userSeq)
        rules.push_back({*r.userSeq, r.defaultSeq, leaderEligible(*r.userSeq, r.defaultSeq),
                         promptEligible(*r.userSeq, r.defaultSeq)});
      bool reused = std::any_of(set.begin(), set.end(), [&](std::size_t o) {
        return resolved[o].userSeq && *resolved[o].userSeq == r.defaultSeq;
      });
      if (!reused)
        rules.push_back({r.defaultSeq, std::nullopt, isLeaderByte(r.defaultSeq[0]) && r.defaultSeq.size() >= 2,
                         promptEligible(r.defaultSeq, r.defaultSeq)});
    }
    return rules;
  };
  rules_ = buildRules(setFor(Scope::Global));
  copyRules_ = buildRules(setFor(Scope::Copy));
  promptRules_ = buildRules(setFor(Scope::Prompt));

  std::vector<Sequence> macroKeys;
  for (const auto &macro : macros) {
    std::string name = "MACRO: M-" + std::to_string(macro.number);
    std::string error;
    auto from = parseSpec(macro.keySpec, userPrefixSeqs, &error);
    if (!from) {
      if (errors) errors->push_back(name + ": " + error);
      continue;
    }
    bool conflict = std::find(from->begin(), from->end(), kDigit) != from->end();
    for (const auto &r : resolved)  // マクロのキーは全体のキーとだけ比べる(COPY・プロンプト・^Xeの直後の番号は別の場面)
      if (r.scope == Scope::Global && r.userSeq &&
          (samePattern(*r.userSeq, *from) || patternIsProperPrefix(*r.userSeq, *from) ||
           patternIsProperPrefix(*from, *r.userSeq)))
        conflict = true;
    for (const auto &other : macroKeys)
      if (samePattern(other, *from) || patternIsProperPrefix(other, *from) || patternIsProperPrefix(*from, other))
        conflict = true;
    if (conflict) {
      if (errors) errors->push_back(name + ": key conflicts with another key");
      continue;
    }
    Sequence to;
    bool ok = true;
    for (const auto &item : macro.items) {
      if (item.keyNumber <= 0) {
        for (unsigned char c : item.literal) to.push_back(c);
        continue;
      }
      int number = item.keyNumber;
      auto it = std::find_if(resolved.begin(), resolved.end(), [&](const Resolved &r) { return r.number == number; });
      if (it == resolved.end() || std::find(it->defaultSeq.begin(), it->defaultSeq.end(), kDigit) != it->defaultSeq.end()) {
        if (errors) errors->push_back(name + ": unknown or unusable " + knName(number));
        ok = false;
        break;
      }
      to.insert(to.end(), it->defaultSeq.begin(), it->defaultSeq.end());
    }
    if (!ok || to.empty()) continue;
    macroKeys.push_back(*from);
    rules_.push_back({*from, to, leaderEligible(*from, *from)});
    promptRules_.push_back({*from, to, leaderEligible(*from, *from), leaderEligible(*from, *from)});
  }
}

bool KeyBindings::isIdentity() const { return rules_.empty() && copyRules_.empty() && promptRules_.empty(); }

std::vector<std::pair<int, KeyBindings::Sequence>> KeyBindings::defaultSequences() {
  std::vector<Sequence> prefixes(kPrefixCount);
  std::string error;
  for (int n = 0; n < kPrefixCount; n++) prefixes[n] = *parseSpec(defaultPrefixSpecs()[n], prefixes, &error);
  std::vector<std::pair<int, Sequence>> out;
  for (const auto &entry : defaultEntries()) {
    auto seq = parseSpec(entry.spec, prefixes, &error);
    if (seq && std::find(seq->begin(), seq->end(), kDigit) == seq->end()) out.push_back({entry.number, *seq});
  }
  return out;
}

bool KeyBindings::matches(const Sequence &pattern, const std::vector<int> &input) {
  if (pattern.size() != input.size()) return false;
  for (std::size_t i = 0; i < pattern.size(); i++) {
    if (pattern[i] == kDigit ? !(input[i] >= '0' && input[i] <= '9') : pattern[i] != input[i]) return false;
  }
  return true;
}

bool KeyBindings::isPrefixOf(const std::vector<int> &input, const Sequence &pattern) {
  if (input.size() >= pattern.size()) return false;
  for (std::size_t i = 0; i < input.size(); i++) {
    if (pattern[i] == kDigit ? !(input[i] >= '0' && input[i] <= '9') : pattern[i] != input[i]) return false;
  }
  return true;
}

std::vector<int> KeyBindings::feed(int byte, Context context, Pending &pending) const {
  if (isIdentity()) return {byte};
  if (pending.bytes.empty()) {
    if (context == Context::None) return {byte};
    pending.context = context;
  }
  const std::vector<Rule> &rules = pending.context == Context::Copy     ? copyRules_
                                    : pending.context == Context::Prompt ? promptRules_
                                                                         : rules_;
  pending.bytes.push_back(byte);
  auto eligible = [&](const Rule &rule) {
    if (pending.context == Context::Prompt) return rule.promptEligible;
    return pending.context != Context::LeaderOnly || rule.leaderEligible;
  };
  std::vector<int> out;
  // 最長一致: 溜めたキーがどれかの列の途中である間は待つ。行き止まりになったら、
  // 頭から最も長く一致する列を処理し(置き換える/捨てる)、一致が無ければ頭の
  // 1バイトをそのまま渡し、残りを改めて照合する。無効化された短い列(例えば
  // 接頭キーに変えられた^T)が、それで始まる長い列より先に捨てられないように
  // するため。
  while (!pending.bytes.empty()) {
    bool anyLonger = std::any_of(rules.begin(), rules.end(), [&](const Rule &rule) {
      return eligible(rule) && isPrefixOf(pending.bytes, rule.from);
    });
    if (anyLonger) return out;
    const Rule *best = nullptr;
    std::size_t bestLength = 0;
    for (const auto &rule : rules) {
      if (!eligible(rule) || rule.from.size() > pending.bytes.size() || rule.from.size() <= bestLength) continue;
      std::vector<int> head(pending.bytes.begin(), pending.bytes.begin() + rule.from.size());
      if (matches(rule.from, head)) {
        best = &rule;
        bestLength = rule.from.size();
      }
    }
    if (best) {
      if (best->to) {
        for (int k : *best->to) {
          // 数字の族: 既定の列のNの位置へ、実際に押された数字を入れる。
          out.push_back(k == kDigit ? pending.bytes[bestLength - 1] : k);
        }
      }
      pending.bytes.erase(pending.bytes.begin(), pending.bytes.begin() + bestLength);
    } else {
      out.push_back(pending.bytes.front());
      pending.bytes.erase(pending.bytes.begin());
    }
  }
  return out;
}

}  // namespace fm
