#pragma once

#include <string>
#include <vector>

namespace fm {

// 2026-10-15、直接のユーザー報告(Windowsで"NarouToEpub3-master.zipは`(^C) cannot execute 'file'`"、
// .steering/20261015-windows-native-gui/): Windowsのネイティブには`file`が無いので、バイナリのファイルを開いた時の
// 「(^X) 種類」の1行(FileMode::binaryTypeText()の`file -b`)と、クイックルックの判定(`file -b --mime-type`)を
// usilicの中で行う(ChildProcessのWindows版が、PATHにfile.exeが無い時に使う)。argsは"file"の後の引数。
// 中身の先頭のバイト(マジックナンバー)だけで、よく見る種類(書庫・圧縮・画像・PDF・実行ファイル・音声・動画等)を
// file(1)に似た言葉で答え、知らないものは"data"(テキストならUTF-8/ASCII text)。戻り値は終了状態(常に0)。
int nativeFile(const std::vector<std::string> &args, std::string &output);

// 1つのファイルの中身の先頭(headに最大数KB)から、file -bの言葉(mimeならMIMEタイプ)を返す(テスト用に公開)。
std::string nativeFileType(const std::string &head, bool mime);

}  // namespace fm
