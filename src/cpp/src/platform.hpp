#pragma once

#include <string>

namespace fm {

// 2026-10-05新設(.steering/20261005-windows-wsl2/): WSL2(Windowsの中のLinux)で、外で開く・
// クリップボードをWindows側へつなぐための小さな関数。WSLかどうかはビルドしたマシンではなく
// 動かしているマシンで決まるので、#ifdefではなく実行時に判定する(macOSでは常にfalse)。

// WSLの中か(/proc/sys/fs/binfmt_misc/WSLInteropがあるか、/proc/versionに"microsoft"を含むか)。
// 一度だけ求めて覚える。
bool isWsl();
// /proc/versionの内容からWSLかを判定する(isWsl()の中身、テスト用に分けた)。
bool procVersionLooksLikeWsl(const std::string &procVersion);
// MSYS2/Cygwinの実行環境か(Git Bash・MSYS2の端末。2026-10-05、.steering/20261005-windows-gitbash/)。
// こちらはビルドしたマシン=動かすマシンの実行環境(msys-2.0.dll)なのでコンパイル時に決まる。
bool isMsys();
// Windows側のアプリ・クリップボードへつなぐか(WSL2かMSYS)。呼び出し元はこれで分岐する。
bool hasWindowsHost();
// `wslpath -w path`(MSYSでは`cygpath -w path`)の結果(Windowsのパス、例: \\wsl.localhost\Ubuntu\home\u\a.mp4)。失敗したら空。
std::string windowsPathFor(const std::string &path);
// URL(http://・https://)はそのまま、ファイルはWindowsのパスにして、Windowsの既定のアプリで開く。
// WSL・MSYSともexplorer.exe(成功しても終了コード1を返すことがあるので、起動できたかだけを見る)。
// `cmd.exe /c start`はURLの&等をcmdが解釈してしまうので使わない。
bool openOnWindowsHost(const std::string &target);
// UTF-8をUTF-16LE(BOM付き)へ。clip.exeはUTF-8をそのまま渡すと日本語が化けるため。
std::string utf8ToUtf16LeWithBom(const std::string &utf8);
// Windowsのクリップボードへ(WSLはclip.exe、MSYSは/dev/clipboardへUTF-8のまま書く)。
bool copyToWindowsClipboard(const std::string &utf8);

}  // namespace fm
