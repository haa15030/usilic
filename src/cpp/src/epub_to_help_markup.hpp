#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
// 2026-09-29追加、直接のユーザー依頼(EPUBの段階3、"扉(タイトルが画面中央に来るための
// 裏技)や奥付(横書き)等、章毎の縦横使い分け"): 縦書きの本の中で、縦書きで描かない行の
// 範囲(返り値の行番号、両端を含む)。
//   horizontal: CSSのwriting-modeで横書き(horizontal-tb)と明示された所の行(奥付等)。
//   centered: 横書きの中に置かれた縦書きの箱の行――横書きのbodyの中に左右autoの縦書きの
//             divを置いて、書名を画面の中央へ寄せる扉の作り方(Kbt.epubのSection001等)。
// writing-modeの指定が無い行は本全体の向きに従う(どちらにも入らない)。
//   alignEnd: CSSのtext-alignがright/endの行(2026-09-29、直接のユーザー依頼"著者名は画面
//             最下行で"――縦書きでは列の下端に寄せる)。
struct EpubLayoutHints {
  std::vector<std::pair<int, int>> horizontal;
  std::vector<std::pair<int, int>> centered;
  std::vector<int> alignEnd;
};

std::optional<std::string> epubToHelpMarkup(const EpubMemberReader &readMember,
                                            std::map<int, int> *links = nullptr,
                                            EpubLayoutHints *layoutHints = nullptr);

// 本文の"[image: <ファイル名>]"のファイル名から、OPFのmanifestにあるzip内の
// パスを探す(2026-09-28追加、直接のユーザー提案"CUTで出る目次だが、「表紙」を
// 「表紙 [image: title]」にして returnでクイックルックというのは？")。
std::optional<std::string> findEpubImageMember(const EpubMemberReader &readMember, const std::string &fileName);

// 2026-09-29追加、直接のユーザー依頼("キー操作での縦横切り替えは廃止。著者は、いずれかを
// cssファイルに設定してあるはずなのでそれで判断する"): 本全体の向き。OPFの
// <spine page-progression-direction="rtl">(右から左へめくる=縦書きの和書)ならtrue。
bool epubIsRightToLeft(const EpubMemberReader &readMember);

}  // namespace fm
