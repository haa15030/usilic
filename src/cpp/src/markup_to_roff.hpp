#pragma once

#include <string>

namespace fm {

// usilic独自markup(<b>/<u>/<i>/<indentN>、"ラベル：説明"記法、
// QUICK.txt自身の実際の書式)で書かれたテキストを、legacy(旧C実装)の
// HELP.jpが実際に使っていたのと同じ「昔ながらのnroff重ね打ち」平文
// 形式へ変換する。`usilic -m <path>`(隠しCLIオプション、app/main.cpp
// 参照)の実体。2026-09-20、この関数自身が変換した内容を新たに正式な
// src/README.jp(旧src/doc/HELP.jp)/src/README(旧src/doc/HELP)へ
// 昇格したのに伴い、旧src/doc/HELP.jp自体はsrc/README.jpへ移動済み
// (od -cでの重ね打ちバイト裏付けは今もsrc/README.jp自身で確認できる)。
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
// 本物のlegacyのHELP.jp自身が実際に使っている、1文字ごとのオーバー
// ストライクバイト列("X\bX"=太字、"_\bX"=下線)と、PipeBuffer自身の
// readPortableIndentRun()(pipe_buffer.cpp参照)が解釈する不可視の
// U+FEFF単項マーカー(字下げ用)だけを使う――どちらも
// PipeBuffer::wrapLine()が「どんな」通常ファイルに対しても(HELP.txt/
// QUICK.txtという特別なファイル名判定を経ずに)既に検出・解釈できる、
// 本当にポータブルな平文表現である(file_mode.cpp自身のnroff
// オーバーストライク検出のテスト参照)。
//   - <b>X</b> → 1文字ごとに"X\bX"(本物のnroff太字の重ね打ち)。
//   - <u>X</u>と<i>X</i> → どちらも1文字ごとに"_\bX"(下線の重ね打ち)
//     ――legacy自身のnroff規約には斜体に相当する重ね打ちが存在しない
//     ため、usilic.1(手作業のroff版)自身の慣習に倣い、<i>も<u>と
//     同じ扱いにする。
//   - <indentN>...</indent> → U+FEFFのN+1個の連続(開始)/1個(終了)の
//     不可視マーカーへ変換するだけ(2026-09-20更新、直接のユーザー
//     報告"indentタグは...すべての行頭がindent位置に揃ってないと
//     意味が無い"を受けての作り直し――当初の「物理行ごとにリテラルな
//     半角スペースを焼き込む」方式は、折り返し継続行の字下げを再現
//     できず撤回した)。実際の字下げ(折り返し継続行への引き継ぎ含む)
//     自体はPipeBuffer::wrapLine()の動的な折り返し処理に委ねる――
//     readPortableIndentRun()自身のコメント参照。
// それ以外の文字(^/~/：/-/./'等)は一切escapeしない――groffのような
// 構文解析を経る中間形式ではなく、最終的な平文そのものだから。
std::string convertMarkupToLegacyHelpText(const std::string &markup);

}  // namespace fm
