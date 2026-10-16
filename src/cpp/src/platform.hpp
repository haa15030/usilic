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
// Windowsのネイティブ(MinGW-w64・MSVC、_WIN32。MSYS2/Cygwinは含まない、.steering/20261015-windows-native-gui/)。
constexpr bool isNativeWindows() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}
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
// fromをtoへ名前を変える。toが既にあれば置き換える(2026-10-16: Windowsのrename()は、toがあると失敗する。
// 一時ファイルに書いてから置き換える保存(history等)が、Windowsでは最初の1回の後ずっと失敗していた)。
bool replaceFile(const std::string &from, const std::string &to);

#ifdef _WIN32
// Windowsのネイティブだけのもの(platform_win.cpp)。パスはUTF-8で、区切りは/にする。
std::string windowsExecutablePath();  // このusilic.exe
std::string windowsTempDirectory();   // %TEMP%(末尾の/無し)
// 起動の最初に呼ぶ: HOMEが無ければUSERPROFILEを入れる(~・~/.usilic/をPOSIXと同じく扱うため)。
void prepareWindowsEnvironment();
// 既定のアプリ(ブラウザ・エクスプローラ等)で開く(ShellExecuteW)。
bool openWithWindowsShell(const std::string &target);
// コンソールの無いusilic.exeで、起動できない理由をメッセージボックスで知らせる。
void showWindowsError(const std::string &message);
// Windows 10以降に付いているtar.exe(%SystemRoot%\System32\tar.exe、libarchiveのbsdtar)。ARCの一覧・中身・取り出しを
// tar・圧縮したtar・zip・7z・rar・lzhのどれもこれで行う(2026-10-16)。cmd.exeは/で書いたプログラムのパスを
// 読めないので、区切りは\のまま。
std::string windowsTarCommand();
// tar.exe等の出力がUTF-8でなければ、システムのANSIのコードページ(日本語版ではCP932)として読んでUTF-8にする
// (usilic.exe自身はマニフェストでUTF-8だが、子のプログラムはシステムのコードページで書く)。
std::string windowsSystemTextToUtf8(const std::string &text);
#endif

}  // namespace fm
