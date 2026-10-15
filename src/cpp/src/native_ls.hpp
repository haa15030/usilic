#pragma once

#include <string>
#include <vector>

namespace fm {

// 2026-10-15(.steering/20261015-windows-native-gui/): Windowsのネイティブには`ls`が無いので、DirectoryModeが
// 読む`ls -alF`と同じ形の一覧をstd::filesystemで作る(ChildProcessのWindows版が、PATHにls.exeが無い時に
// `ls ...`の代わりに使う)。argsは"ls"の後の引数(-alF・-t・-S・-r、最後の一覧するディレクトリ)。
// 行の形は「drwxr-xr-x  1 user user     0 Oct 15 08:57 名前/」(月名の13字後に名前、
// DirectoryMode::parseLsLine()参照)。ディレクトリが読めなければ*okをfalseにする。
std::string nativeLsAlF(const std::vector<std::string> &args, bool *ok = nullptr);

}  // namespace fm
