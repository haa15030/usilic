#pragma once

#include <string>
#include <vector>

namespace fm {

// 2026-10-14、直接のユーザー依頼("HELPモードを正式にHELP.epubとしたい（注釈を見れるので）。usilic -e HELP.txt >
// ~/.local/lib/usilic/HELP.epub の実装よろしく"): usilic -nが作るHELPのroff(convertMarkupToMsRoffSource())を
// 横書きのEPUB3(zipのバイト列)にする。tools/help2epub.py(試作)のC++版。
// 章(行頭に字下げの無い見出し)ごとに1ファイル、字下げのある見出し(INFO等)は目次の入れ子。字下げ(.in/.ti)は
// padding-left/text-indent(1n=0.5em)。「usilicrc の URL」等の設定項目の名前は巻末の注釈(KDPの推奨どおり
// aside epub:type="footnote"と両方向のリンク)へのリンクにする。注釈の中身はusilicrcText(usilicrc)のその項目の
// 見出しの直後の「#」の行、notesText(doc/HELP.notes)の"@項目"に書いてあればその文。目次(nav)はspineに入れない。
// summaryには、章・注釈の数等の一行(標準エラー出力用)を入れる。
// unnotedには、注釈の無い斜体の名前(最初に出てきた順、重複なし)を入れる。HELP.notesに本文の無い"@名前"が
// あれば、注釈の要らない斜体(アンダーラインの代わりのman(1)等)として扱い、入れない。
std::string convertMsRoffToHelpEpub(const std::string &roff, const std::string &usilicrcText,
                                    const std::string &notesText, std::string *summary = nullptr,
                                    std::vector<std::string> *unnoted = nullptr, const std::string &coverPng = {});
// coverPng(2026-10-20): 表紙の画像(PNGのバイト列、usilic.appのアイコン)。空でなければ最初のページを表紙にする。
// 空なら、roffにusilic -nが埋め込んだ表紙(".\" usilic-cover-line …"、base64)を使う(2026-10-29)。

// 表紙の画像をHELP.1に埋め込むためのbase64(2026-10-29)。decodeBase64()は'='・改行等の字を読み飛ばす。
std::string encodeBase64(const std::string &bytes);
std::string decodeBase64(const std::string &text);

}  // namespace fm
