#pragma once

// 2026-09-20、直接のユーザー依頼("今のKEYをHELPに置き換え...正味の：
// カットによるHELPとしてほしい")により、このファイルが以前保持して
// いたHELP本文一式――日本語版kHelpTextJa(2026-08-30に旧src/doc/HELP.jp
// から装飾を保ったまま移植)・英語版kHelpTextEn・GUI専用章kHelpTextGuiJa/
// En・以前のクイックリファレンス版kKeyReferenceTextJa(2026-08-30時点で
// 既に未使用のまま残っていた)は全て削除した(詳しい経緯・編集手順は
// git履歴を参照)。新HELPは代わりに、実行時にusilic -m(markup_to_roff.hpp
// 参照)で生成される~/.local/lib/usilic/usilic.manの内容をそのまま
// 読み込む(Application::loadHelpText()、app/main.cpp)ので、このヘッダ
// 自身にHELP本文を埋め込む必要が無くなった。
//
// 引き続き残っているexpandHelpMarkup()は、usilic独自markup(<b>/<u>/<i>/
// <indentN>、QUICK.txt/HELP.txt自身が使う記法)をPipeBuffer::wrapLine()
// が理解する重ね打ち制御バイトへ変換する、汎用のユーティリティである
// ――HELP本文専用ではなく、file_mode.cpp自身のisHelpMarkupPagerFile()
// (HELP.txt/QUICK.txtという名前の通常ファイルを開いた時の展開)からも
// 引き続き使われている。

#include <string>

namespace fm {

// <b>X</b>/<u>X</u>/<i>X</i>/<indentN>...</indent>を、PipeBuffer::
// wrapLine()が理解する制御バイト(太字/下線は1文字ごとのX\bX/_\bX、
// 字下げ/斜体はusilic内部専用の\x01/\x02系)へ変換する。定義は
// help_text.cpp。
std::string expandHelpMarkup(const std::string &text);

}  // namespace fm
