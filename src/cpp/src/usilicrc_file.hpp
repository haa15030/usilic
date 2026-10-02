#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fm {

// 2026-10-02新設、直接のユーザー依頼(HELP/KEY/usilicrc再編、.steering/20261002-help-key-usilicrc-reorg/):
// usilicrcのファイルとしての扱い(どれを読むか、無ければコピー、節の値の書き換え)。中身の解釈は
// Configuration。
//
// 読むのは利用者の`~/.usilic/usilicrc`。無ければ、インストールされた既定のusilicrc
// (`~/.local/lib/usilic/usilicrc`、無ければ`/usr/local/lib/usilic/usilicrc`)をそこへコピーしてから
// 読む(「~/.usilic/usilicrcが無いときは$PREFIX/lib/usilic/usilicrcがコピーされる仕様」)。
namespace usilicrc {

std::string userPath(const std::string &home);
// インストールされた既定のusilicrc(上の順で最初に見つかったもの)。無ければnullopt。
std::optional<std::string> installedPath(const std::string &home);
// 利用者のusilicrcが無ければ既定のものをコピーする。利用者のusilicrcがある(コピーできた)ならtrue。
bool ensureUser(const std::string &home);

// 節`section`(行頭のキーワード)の値の行を`values`(1行=1要素、行頭のタブは付けて書く)で置き換える。
// 節の見出しと値の間のコメントは残す。節が無ければ、既定のusilicrcにある同じ節(コメントごと)を
// KEYの手前へ足してから置き換える(既定のものにも無ければ見出しだけ)。書き込みは一時ファイルへ
// 書いてからrenameする。利用者のusilicrcが無ければ先にensureUser()する。失敗したらfalse。
bool setSection(const std::string &home, const std::string &section, const std::vector<std::string> &values);

// 上の書き換えのテキスト版(ファイルを読み書きしない、テスト用にも公開)。
// `templateText`は既定のusilicrcの全文(節が無い時の雛形、空でもよい)。
std::string replaceSection(const std::string &text, const std::string &section, const std::vector<std::string> &values,
                           const std::string &templateText);

// MACRO(2026-10-02、^X^S): MACRO節の「--」の行(^X^Sが次に書き込む位置の印)を`replacement`の行で
// 置き換える(印が無ければ節の末尾へ足す。節も無ければKEYの後ろ=末尾へ足す)。テキスト版と、
// 利用者のusilicrcへ書くファイル版。
std::string replaceMacroMarker(const std::string &text, const std::vector<std::string> &replacement);
bool setMacroMarker(const std::string &home, const std::vector<std::string> &replacement);
// MACRO節で使われている最大のM番号+1(無ければ1)。
int nextMacroNumber(const std::string &text);
// 節の見出しの行番号(1始まり)。無ければnullopt。
std::optional<int> sectionLineNumber(const std::string &text, const std::string &section);
std::string readText(const std::string &path);

}  // namespace usilicrc

}  // namespace fm
