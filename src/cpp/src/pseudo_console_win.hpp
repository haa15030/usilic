#pragma once
// WindowsのネイティブのSHELL(2026-10-16、.steering/20261015-windows-native-gui/の第2段)の擬似端末。POSIX版の
// Shellがposix_openpt/forkで作るptyの代わりに、ConPTY(CreatePseudoConsole、Windows 10 1809以降)の上でシェルを
// 動かす。windows.hをshell.cppへ持ち込まないよう、ここでは不透明なポインタだけを見せる。
#ifdef _WIN32

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fm {

struct PseudoConsole;

// argvを、directory(空なら親と同じ)をカレントにして、rows×columnsのConPTYの上で起動する。env(名前・値)は子に
// だけ足す。失敗ならnullptr。pidOutはWindowsのプロセスID。
PseudoConsole *startPseudoConsole(const std::vector<std::string> &argv, int rows, int columns,
                                  const std::string &directory,
                                  const std::vector<std::pair<std::string, std::string>> &env, int &pidOut);
// 届いている出力を最大size バイト。>0は読めたバイト数、0は今は何も無い、-1はシェルが終わって出力も尽きた。
int readPseudoConsole(PseudoConsole *console, char *buf, std::size_t size);
// timeoutMsまで出力(か終わり)を待つ。届いていればtrue。
bool waitPseudoConsole(PseudoConsole *console, int timeoutMs);
bool writePseudoConsole(PseudoConsole *console, const std::string &bytes);
void resizePseudoConsole(PseudoConsole *console, int rows, int columns);
// シェルを終わらせ(まだ動いていれば)、全てを片付けてconsoleを解放する。
void closePseudoConsole(PseudoConsole *console);
// 実行ファイルを探す(SearchPathW、拡張子は.exe)。見つかればそのパス(/区切り)、無ければ空。
std::string findWindowsProgram(const std::string &program);

}  // namespace fm

#endif  // _WIN32
