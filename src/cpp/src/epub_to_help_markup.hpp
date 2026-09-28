#pragma once

#include <functional>
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
//   - rubyは青空文庫式の「漢字《かんじ》」、画像は"[image: alt]"。
//   - 章の行(目次(nav/ncx)の項目が指す位置と、h1〜h6)は行末にタブを付ける
//     (QUICK.txtと同じ印。本文中のASCII空白は全て1個のスペースへ正規化する
//     ので、タブは章の行にしか現れない)――CUTの固定文字列"\t"で章の一覧になる。
// container.xml/OPFが読めなければstd::nullopt(呼び出し側は従来の表示へ戻る)。
std::optional<std::string> epubToHelpMarkup(const EpubMemberReader &readMember);

}  // namespace fm
