#pragma once

#include <string>

namespace fm {

// usilic独自markup(<b>/<u>/<i>/<indentN>、"ラベル：説明"記法、
// QUICK.txt自身の実際の書式)で書かれたテキストを、src/doc/HELP.jpと
// 同じ「昔ながらのnroff重ね打ち」平文形式へ変換する。`usilic -m <path>`
// (隠しCLIオプション、app/main.cpp参照)の実体。
//
// 2026-09-20、直接のユーザー依頼("-mオプションだが、roffソースを
// 出力する必要はない。man-file風だが公式には無い裏タグ<indentNN>
// </indent>で作られているソース用の裏オプションとしてsrc/doc/HELP.jp
// のような形式を打ち出してほしい")により、当初のgroff -manマクロ
// ソース生成方式(コミット前に差し替え)から全面的に作り直した。
//
// <indentN>/<b>/<u>/<i>はいずれもusilic独自の発明であり、公式な
// roff/troffの記法ではない――既にこのポート自身のexpandHelpMarkup()
// (help_text.cpp)が、画面表示用にこれらを解釈しているが、そちらが
// 生成するのはusilic内部専用の制御バイト(\x01+桁=字下げ、\x02+桁=
// 斜体)であり、外部ファイルとしてそのまま保存しても(usilic自身の
// PipeBufferでなければ)正しく解釈されない。この関数はその代わりに、
// 本物のlegacy(旧C実装)のHELP.jp自身が実際に使っている、1文字ごとの
// オーバーストライクバイト列("X\bX"=太字、"_\bX"=下線、od -cで
// src/doc/HELP.jp自身を確認して裏付け済み)だけを使う――これは
// PipeBuffer::wrapLine()が「どんな」通常ファイルに対しても(HELP.txt/
// QUICK.txtという特別なファイル名判定を経ずに)既に検出・解釈できる、
// 本当にポータブルな平文表現である(file_mode.cpp自身のnroff
// オーバーストライク検出のテスト参照)。
//   - <b>X</b> → 1文字ごとに"X\bX"(本物のnroff太字の重ね打ち)。
//   - <u>X</u>と<i>X</i> → どちらも1文字ごとに"_\bX"(下線の重ね打ち)
//     ――legacy自身のnroff規約には斜体に相当する重ね打ちが存在しない
//     ため、usilic.1(手作業のroff版)自身の慣習に倣い、<i>も<u>と
//     同じ扱いにする。
//   - <indentN>...</indent> → 具体的な字下げ幅Nぶんの半角スペースを
//     このブロックの各行の先頭にそのまま書き込む(usilic内部専用の
//     \x01制御バイトではなく、本物の平文スペース文字――折り返し後の
//     継続行までは引き継がない、legacy自身のHELP.jpが元々そうである
//     のと同じ割り切り)。
// それ以外の文字(^/~/：/-/./'等)は一切escapeしない――groffのような
// 構文解析を経る中間形式ではなく、最終的な平文そのものだから。
std::string convertMarkupToLegacyHelpText(const std::string &markup);

}  // namespace fm
