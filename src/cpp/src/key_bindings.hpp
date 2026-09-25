#pragma once

// usilicrcのKEY節(キーの割り当て)を本体に反映する仕組み――2026-09-25新設、
// 直接のユーザー依頼(.steering/20260925-usilicrc-key-bindings/参照)。
//
// 本体のキー処理(Application::dispatchByte()/dispatchLeaderByte()/
// feedShellByte()/...)は既定のキー列で書かれている。それには手を付けず、
// Application::feedByte()の入口で「利用者が押したキー列」を「既定のキー列」
// へ置き換えてから渡す。usilicrcのKEY節が既定と同じなら置き換え表は空で、
// 何もしない(挙動は一切変わらない)。
//
// KEY節の書式(1行1項目、行頭はタブ):
//     F-0	C-x		# ^X
//     K-16	F-1 C-c		# ^J^C カレントディレクトリへ移動
// キー表記: F-n(接頭キーF-nの展開)、C-x(Ctrl+x)、C-SPC、ESC、SPC、任意の
// 1文字、N(末尾のみ、数字0〜9のいずれか)。キー表記が空なら割り当て無し。

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fm {

class KeyBindings {
 public:
  // 接頭キーF-0〜F-4と、K番号の組み込みの表(TUTORIALの登場順)。
  static constexpr int kPrefixCount = 5;
  struct DefaultEntry {
    int number;        // K-number
    const char *spec;  // 既定のキー表記
  };
  static const std::vector<const char *> &defaultPrefixSpecs();  // F-0..F-4
  static const std::vector<DefaultEntry> &defaultEntries();

  // キー列の要素: 0〜255はそのバイト、kDigitは数字0〜9のいずれか(N)。
  static constexpr int kDigit = -1;
  using Sequence = std::vector<int>;

  // キー表記をキー列へ展開する。prefixesはF-0〜F-4の展開済みのキー列。
  // 書けない表記ならnulloptを返し、errorに理由を入れる。
  static std::optional<Sequence> parseSpec(const std::string &spec, const std::vector<Sequence> &prefixes,
                                           std::string *error);

  // usilicrcの指定(未指定の番号は既定のまま)から置き換え表を組む。
  // userPrefixes: F番号→キー表記、userEntries: K番号→キー表記(空=割り当て無し)。
  // 問題があればerrorsへ追記し、その指定は既定のままにする。
  void configure(const std::map<int, std::string> &userPrefixes, const std::map<int, std::string> &userEntries,
                 std::vector<std::string> *errors);

  // 置き換え/無効化が1つも無い(=既定のまま)ならtrue。
  bool isIdentity() const;

  // 入力1バイトを照合する。context: 照合を始めてよい範囲(Application::
  // keyBindingContext()参照)。戻り値は本体へ渡すバイト列(空なら、まだ
  // 溜めている途中か、無効化されたキーとして捨てた)。
  enum class Context { None, LeaderOnly, All };
  // 照合の途中状態(溜めているキー)。置き換え表(このクラス)はConfigurationが
  // 持ち、途中状態は入力を受け取る側(Application)が持つ――usilicrcを後から
  // 読み直しても、表と状態が食い違わないようにするため。
  struct Pending {
    std::vector<int> bytes;
    Context context = Context::None;
  };
  std::vector<int> feed(int byte, Context context, Pending &pending) const;

 private:
  struct Rule {
    Sequence from;              // 利用者のキー列(照合する側)
    std::optional<Sequence> to; // 既定のキー列(nulloptなら捨てる=無効化)
    bool leaderEligible;        // プロンプト/SHELL等(LeaderOnly)でも対象か
  };
  std::vector<Rule> rules_;

  static bool matches(const Sequence &pattern, const std::vector<int> &input);
  static bool isPrefixOf(const std::vector<int> &input, const Sequence &pattern);
};

}  // namespace fm
