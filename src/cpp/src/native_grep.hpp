#pragma once

#include <string>
#include <vector>

namespace fm {

// 2026-10-15(.steering/20261015-windows-native-gui/): Windowsのネイティブには`grep`が無いので、CUT/PART・KEY・
// EPUBの章分け・FGREP/EGREPが使う`grep -Fne/-Ene/-ne パターン`(本文は標準入力、出力は「行番号:行」)を
// usilicの中で行う(ChildProcessのWindows版が、PATHにgrep.exeが無い時に使う)。argsは"grep"の後の引数。
// 使える指定: -F(固定文字列)・-E(拡張正規表現)・-G(基本正規表現、既定)・-n・-i・-v・-e パターン。パターンの
// 後の引数はファイル(無ければinput)。戻り値はgrepの終了状態(0: 一致あり、1: 無し、2: パターンの誤り等)。
// 正規表現はstd::regex(バイト単位なので、和文の字を[]の中に書くと1字として扱われない)。
int nativeGrep(const std::vector<std::string> &args, const std::string &input, std::string &output);

}  // namespace fm
