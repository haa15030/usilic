// usilicrcのKEY節を本体に反映する仕組み(KeyBindings、2026-09-25)のテスト。
// .steering/20260925-usilicrc-key-bindings/参照。

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "configuration.hpp"
#include "doctest.h"
#include "key_bindings.hpp"

using fm::Configuration;
using fm::KeyBindings;
using Context = fm::KeyBindings::Context;

namespace {

constexpr int kJ = 0x0a, kO = 0x0f, kX = 0x18, kT = 0x14, kCtrlC = 0x03;

std::vector<KeyBindings::Sequence> defaultPrefixes() {
  std::vector<KeyBindings::Sequence> prefixes(KeyBindings::kPrefixCount);
  for (int n = 0; n < KeyBindings::kPrefixCount; n++) {
    std::string error;
    prefixes[n] = *KeyBindings::parseSpec(KeyBindings::defaultPrefixSpecs()[n], prefixes, &error);
  }
  return prefixes;
}

// 入力のバイト列を1つずつfeed()し、本体へ渡るバイト列をまとめて返す。
std::vector<int> run(const KeyBindings &bindings, const std::vector<int> &input, Context context = Context::All) {
  KeyBindings::Pending pending;
  std::vector<int> out;
  for (int b : input) {
    std::vector<int> part = bindings.feed(b, context, pending);
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

KeyBindings configured(const std::string &keySection, std::vector<std::string> *errors = nullptr) {
  Configuration config;
  REQUIRE(config.loadRcText("KEY\n" + keySection, true));
  if (errors) *errors = config.errors();
  return config.keyBindings();
}

std::string readSourceFile(const std::string &name) {
  std::string here = __FILE__;
  std::string dir = here.substr(0, here.find_last_of('/'));
  std::ifstream in(dir + "/../../" + name);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST_CASE("KeyBindings::parseSpec: F-n / C-x / C-SPC / ESC / SPC / one character / trailing N") {
  auto prefixes = defaultPrefixes();
  std::string error;
  CHECK(*KeyBindings::parseSpec("F-1 C-c", prefixes, &error) == KeyBindings::Sequence{kJ, kCtrlC});
  CHECK(*KeyBindings::parseSpec("F-4 e", prefixes, &error) == KeyBindings::Sequence{kJ, kO, 'e'});
  CHECK(*KeyBindings::parseSpec("C-SPC", prefixes, &error) == KeyBindings::Sequence{0x80});
  CHECK(*KeyBindings::parseSpec("ESC <", prefixes, &error) == KeyBindings::Sequence{0x1b, '<'});
  CHECK(*KeyBindings::parseSpec("SPC", prefixes, &error) == KeyBindings::Sequence{' '});
  CHECK(*KeyBindings::parseSpec("F-1 N", prefixes, &error) == KeyBindings::Sequence{kJ, KeyBindings::kDigit});
  CHECK_FALSE(KeyBindings::parseSpec("C-1", prefixes, &error));
  CHECK_FALSE(KeyBindings::parseSpec("N x", prefixes, &error));
  CHECK_FALSE(KeyBindings::parseSpec("F-7 x", prefixes, &error));
}

TEST_CASE("KeyBindings: no KEY section (or the defaults) is the identity -- every byte passes through as-is") {
  Configuration config;
  REQUIRE(config.loadRcText("LS\n\t0\n", true));
  CHECK(config.keyBindings().isIdentity());
  CHECK(run(config.keyBindings(), {kJ, kCtrlC, 'j'}) == std::vector<int>{kJ, kCtrlC, 'j'});
  CHECK(configured("\tK-24\tF-1 C-c\n\tF-1\tC-j\n").isIdentity());
}

TEST_CASE("KeyBindings: a changed K-n works with its new keys, and its old keys stop working") {
  std::vector<std::string> errors;
  KeyBindings b = configured("\tK-24\tF-1 z\n", &errors);  // ^J^C(カレントディレクトリ) -> ^Jz
  CHECK(errors.empty());
  CHECK(run(b, {kJ, 'z'}) == std::vector<int>{kJ, kCtrlC});
  CHECK(run(b, {kJ, kCtrlC}).empty());                       // 元のキーは捨てられる
  CHECK(run(b, {kJ, 'd'}) == std::vector<int>{kJ, 'd'});     // 変えていないキーはそのまま
}

TEST_CASE("KeyBindings: old keys given to another function do that function instead (swap)") {
  // ^J^C(K-16)と^J^B(K-17、2026-10-02に^J^Kから)を入れ替える。
  KeyBindings b = configured("\tK-24\tF-1 C-b\n\tK-26\tF-1 C-c\n");
  CHECK(run(b, {kJ, 0x02}) == std::vector<int>{kJ, kCtrlC});
  CHECK(run(b, {kJ, kCtrlC}) == std::vector<int>{kJ, 0x02});
}

TEST_CASE("KeyBindings: empty keys unassign a function") {
  KeyBindings b = configured("\tK-81\t\n");  // ^X^W(バッファ保存)を割り当て無しに
  CHECK(run(b, {kX, 0x17}).empty());
}

TEST_CASE("KeyBindings: changing a prefix (F-n) moves every function under it; a clash is a usilicrc error") {
  std::vector<std::string> errors;
  KeyBindings b = configured("\tF-1\tC-k\n", &errors);  // ^Jを^Kへ
  CHECK(run(b, {0x0b, kCtrlC}) == std::vector<int>{kJ, kCtrlC});
  CHECK(run(b, {0x0b, 'd'}) == std::vector<int>{kJ, 'd'});
  CHECK(run(b, {kJ, kCtrlC}).empty());
  // K-93(^K、KEY、2026-10-02)は新しい接頭キー^Kそのものと衝突するのでエラーになり、割り当て無し
  // (2026-10-02までは^Tだったが、^T(TUTORIAL)は廃止)。
  bool reported = false;
  for (const auto &e : errors) reported = reported || e.find("K-93") != std::string::npos;
  CHECK(reported);
}

TEST_CASE("KeyBindings: N (digit family) maps the typed digit through") {
  KeyBindings b = configured("\tK-27\tF-0 N\n");  // ^JN -> ^XN
  CHECK(run(b, {kX, '3'}) == std::vector<int>{kJ, '3'});
  CHECK(run(b, {kJ, '3'}).empty());
}

TEST_CASE("KeyBindings: one-character keys are only replaced where the app treats keys as commands") {
  KeyBindings b = configured("\tK-1\tJ\n");  // j(1行下へ) -> J
  CHECK(run(b, {'J'}) == std::vector<int>{'j'});
  CHECK(run(b, {'j'}).empty());
  // プロンプト入力中など(LeaderOnly)では、どちらも文字として入力される。
  CHECK(run(b, {'J'}, Context::LeaderOnly) == std::vector<int>{'J'});
  CHECK(run(b, {'j'}, Context::LeaderOnly) == std::vector<int>{'j'});
  // 接頭キーで始まるキーは、プロンプト入力中でも置き換わる。
  KeyBindings c = configured("\tK-24\tF-1 z\n");
  CHECK(run(c, {kJ, 'z'}, Context::LeaderOnly) == std::vector<int>{kJ, kCtrlC});
  CHECK(run(c, {kJ, 'z'}, Context::None) == std::vector<int>{kJ, 'z'});
}

TEST_CASE("KeyBindings: unknown numbers and unwritable keys are usilicrc errors") {
  std::vector<std::string> errors;
  configured("\tK-999\tx\n\tK-24\tC-1\n\tF-7\tC-t\n", &errors);
  CHECK(errors.size() == 3);
}

TEST_CASE("src/usilicrc and src/usilicrc.en: read without errors, their KEY sections equal the built-in table "
          "(so nothing is replaced), and they hold the same settings") {
  for (const char *name : {"usilicrc", "usilicrc.en"}) {
    CAPTURE(name);
    std::string text = readSourceFile(name);
    REQUIRE_FALSE(text.empty());
    Configuration config;
    REQUIRE(config.loadRcText(text, true));
    CHECK(config.errors().empty());
    // 2026-10-02: MACRO節(M-1〜)があるので置き換え表自体は空ではない。KEY節が既定と同じことは下で確かめる。
    CHECK(config.macros().size() == 6);  // 2026-10-19: 既定のMACROはM-1〜M-6(; : [ ] e l)
    CHECK(config.keyEntrySpecs().size() == KeyBindings::defaultEntries().size());
    for (const auto &entry : KeyBindings::defaultEntries()) {
      CAPTURE(entry.number);
      CHECK(config.keyEntrySpecs().at(entry.number) == entry.spec);
    }
    CHECK(config.grepPresets().size() == 25);  // 2026-10-08にタブの実例を追加
    CHECK(config.grepPresetLines().size() == config.grepPresets().size());
    CHECK(config.arrowSpeedValues().size() == 12);  // ARROW(2026-10-08)
    CHECK(config.allowedControlBytes().find('\x07') != std::string::npos);  // CODEのBEL(2026-09-25追加)
  }
}

// 2026-09-26追加、直接のユーザー依頼: K番号は機能の固定IDで、usilicrc上の行の
// 順序(番号の並び)は意味を持たない。
TEST_CASE("KeyBindings: the order of K-n lines in usilicrc does not matter") {
  CHECK(configured("\tK-3\tC-n\n\tK-20\tC-g\n\tK-4\tC-p\n").isIdentity());
  KeyBindings b = configured("\tK-26\tF-1 C-c\n\tK-24\tF-1 C-b\n");  // 入れ替えを逆順の行で書く
  CHECK(run(b, {kJ, 0x02}) == std::vector<int>{kJ, kCtrlC});
  CHECK(run(b, {kJ, kCtrlC}) == std::vector<int>{kJ, 0x02});
}

// 2026-10-02追加(KeyBindings::rewriteHelpText()参照)。
TEST_CASE("KeyBindings::rewriteHelpText: changed keys are shown in HELP, keeping the colon column") {
  // ^Xf(FILE: 入力、K-61)を^Xgへ、^Oy(全選択、K-37)と^On(全解除、K-38)を入れ替え、.(選択、K-35)を,へ。
  KeyBindings b = configured("\tK-61\tF-0 g\n\tK-37\tF-2 n\n\tK-38\tF-2 y\n\tK-35\t,\n");
  std::string help =
      "         ^Xf\xEF\xBC\x9A" "ファイル名を入力\n"
      "         ^Oy\xEF\xBC\x9A" "全行選択\n"
      "         ^On\xEF\xBC\x9A" "全行解除\n"
      "           .\xEF\xBC\x9A" "選択\n"
      "    ^Xf\"~/x\"^M で開き、. で選択します。\n";
  CHECK(b.rewriteHelpText(help) ==
        "         ^Xg\xEF\xBC\x9A" "ファイル名を入力\n"
        "         ^On\xEF\xBC\x9A" "全行選択\n"
        "         ^Oy\xEF\xBC\x9A" "全行解除\n"
        "           ,\xEF\xBC\x9A" "選択\n"
        "    ^Xg\"~/x\"^M で開き、. で選択します。\n");  // 本文の1文字のキーはそのまま
}

TEST_CASE("KeyBindings::rewriteHelpText: a longer key keeps the colon column by eating one leading space") {
  KeyBindings b = configured("\tK-61\tESC C-f\n");  // ^Xf -> ^[^F(1桁長い)
  CHECK(b.rewriteHelpText("         ^Xf\xEF\xBC\x9A" "x\n") == "        ^[^F\xEF\xBC\x9A" "x\n");
}

// 2026-10-23追加(KeyBindings::Scope参照、.steering/20261023-keys-all-in-usilicrc/): COPY・プロンプトの中だけの番号と、
// 全体の番号との共有。
TEST_CASE("KeyBindings: COPY numbers replace keys only in COPY, and COPY shares the global numbers") {
  constexpr int kEsc = 0x1b;
  KeyBindings b = configured("\tK-132\tESC f\n\tK-3\tESC n\n");  // COPYの1文字進む(^F)をESC fへ、^N(K-3)をESC nへ
  CHECK(run(b, {kEsc, 'f'}, Context::Copy) == std::vector<int>{0x06});
  CHECK(run(b, {0x06}, Context::Copy).empty());                       // COPYの^Fは空いたので無効
  CHECK(run(b, {0x06}, Context::All) == std::vector<int>{0x06});      // 全体の^F(K-7、ページ送り)はそのまま
  CHECK(run(b, {kEsc, 'n'}, Context::Copy) == std::vector<int>{0x0e});  // K-3の変更はCOPYの^Nにも
  CHECK(run(b, {0x0e}, Context::Copy).empty());
  CHECK(run(b, {kEsc, 'n'}, Context::All) == std::vector<int>{0x0e});
}

TEST_CASE("KeyBindings: prompt numbers and the prompt context") {
  constexpr int kEsc = 0x1b;
  KeyBindings b = configured("\tK-139\tESC a\n\tK-20\tC-\\\n\tK-1\tJ\n");  // 入力の行頭(^A)・^G(K-20)・j(K-1)を変える
  CHECK(run(b, {kEsc, 'a'}, Context::Prompt) == std::vector<int>{0x01});
  CHECK(run(b, {0x01}, Context::Prompt).empty());
  CHECK(run(b, {0x01}, Context::All) == std::vector<int>{0x01});    // 全体には^Aの番号が無い
  CHECK(run(b, {0x1c}, Context::Prompt) == std::vector<int>{0x07});  // ^G(K-20)はプロンプトでも共有
  CHECK(run(b, {'J'}, Context::Prompt) == std::vector<int>{'J'});    // 打った字になる1字のキーは置き換えない
  CHECK(run(b, {'J'}, Context::All) == std::vector<int>{'j'});
  CHECK(run(b, {0x1c}, Context::LeaderOnly) == std::vector<int>{0x1c});  // SHELL等は今までどおり接頭キーの列だけ
}

TEST_CASE("KeyBindings: conflicts are checked per scope, and the macro repeat key") {
  std::vector<std::string> errors;
  configured("\tK-132\tC-n\n", &errors);  // COPYの中ではK-3(^N)と重なる
  CHECK(errors.size() == 1);
  errors.clear();
  configured("\tK-139\tC-a\n\tK-134\tESC a\n", &errors);  // 別の場面どうしは重ならない
  CHECK(errors.empty());
  CHECK(configured("").macroRepeatKey() == 'e');
  CHECK(configured("\tK-131\tx\n").macroRepeatKey() == 'x');
  errors.clear();
  CHECK(configured("\tK-131\tF-0 x\n", &errors).macroRepeatKey() == 'e');  // 1文字だけ
  CHECK(errors.size() == 1);
}
