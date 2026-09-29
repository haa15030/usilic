#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>

namespace fm {

// EPUB内のメンバー(zip内のパス、例えば"OEBPS/content.opf")の中身を返す関数。
// 無ければstd::nullopt。実物はFileMode側の`unzip -p`、テストでは連想配列。
using EpubMemberReader = std::function<std::optional<std::string>(const std::string &member)>;

// EPUB(2/3)の本文を、spine順にテキストへ変換し、help_text.hpp自身の
// expandHelpMarkup()が理解する<b>/<u>/<i>タグの文字列として返す(Markdownの
// markdownToHelpMarkup()と同じ二段構成)。冒頭に書名・著者。
//
// 2026-09-28追加、直接のユーザー依頼("EPUBファイルの縦書き表示とかCUIでも
// できる？"→docs/ideas/IDEAS.mdの「EPUBビューア」段階1"1をよろしく")。
// .steering/20260928-epub-viewer-stage1/design.md自身の対応表参照:
//   - rubyは青空文庫式の「漢字《かんじ》」、画像は"[image: <ファイル名>]"。画像だけの
//     章は「表紙 [image: title.jpg]」の1行にまとめる。
//   - 章の行(目次(nav/ncx)の項目が指す位置と、h1〜h6)は行末にタブを付ける
//     (QUICK.txtと同じ印。本文中のASCII空白は全て1個のスペースへ正規化する
//     ので、タブは章の行にしか現れない)――CUTの固定文字列"\t"で章の一覧になる。
// container.xml/OPFが読めなければstd::nullopt(呼び出し側は従来の表示へ戻る)。
//
// links(省略可): 本の中へのリンク(下線付き)がある行の行番号→リンク先の行番号
// (どちらも返す文字列の1始まりの行、1行に複数あれば最初のリンク)。2026-09-28
// 追加、直接のユーザー提案("選択できる章はタグジャンプとしてのアンダーラインを
// 付加し returnで当該行へ飛んではどうか")。下線はリンク専用で、h1や<u>には
// 付けない。
std::optional<std::string> epubToHelpMarkup(const EpubMemberReader &readMember,
                                            std::map<int, int> *links = nullptr);

// 本文の"[image: <ファイル名>]"のファイル名から、OPFのmanifestにあるzip内の
// パスを探す(2026-09-28追加、直接のユーザー提案"CUTで出る目次だが、「表紙」を
// 「表紙 [image: title]」にして returnでクイックルックというのは？")。
std::optional<std::string> findEpubImageMember(const EpubMemberReader &readMember, const std::string &fileName);

// 2026-09-29追加、直接のユーザー依頼("キー操作での縦横切り替えは廃止。著者は、いずれかを
// cssファイルに設定してあるはずなのでそれで判断する"): 本全体の向き。OPFの
// <spine page-progression-direction="rtl">(右から左へめくる=縦書きの和書)ならtrue。
bool epubIsRightToLeft(const EpubMemberReader &readMember);

}  // namespace fm
