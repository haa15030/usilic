#pragma once

#include <string>
#include <vector>

namespace fm {

// 2026-10-15(.steering/20261015-windows-native-gui/): Windowsのネイティブには`unzip`が無いので、EPUBの本の中身の
// 取り出し(FileMode::epubMemberReader()の`unzip -p 本 中のファイル`)・ARCの一覧(`unzip -l`)・展開
// (`unzip -o 書庫 [中のファイル…] -d 先`)をusilicの中で行う(ChildProcessのWindows版が、PATHにunzip.exeが
// 無い時に使う)。argsは"unzip"の後の引数。中のファイルの名前の\によるエスケープ(*?[]{}等)は外して、そのままの
// 名前として扱う(ワイルドカードは扱わない)。圧縮は無圧縮(0)とDeflate(8、stb_imageのzlibで戻す)だけ。
// 戻り値はunzipの終了状態(0: 成功、9: zipでない、11: 中のファイルが見つからない)。
int nativeUnzip(const std::vector<std::string> &args, std::string &output);

}  // namespace fm
