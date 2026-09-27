#include "list_browser_mode.hpp"

namespace fm {

// モードライン用フィールド: 実際のModeLineFieldsProviderがここではなく
// Applicationのコンストラクタ「そちら」で登録されている理由は、
// Application自身のコンストラクタを参照。ListBrowserMode自体は
// RingCategoryを意図的に知らない(クラスコメント自身の「意図的に
// 除外」一覧を参照)――しかし実際のユーザー報告(2026-08-02:
// 「^X^F（FILE）が置き換わってない」)により、モードラインにも
// どの履歴カテゴリを閲覧中か("FILE"/"DIR"/"HISTORY"等、legacy自身の
// " FILE_HISTORY"のような命名に合わせる――history.hpp自身の
// RingCategoryのコメント参照)を表示してほしい、という要望があり、
// これはshow()が呼ばれる時点ではApplicationだけが知っている情報
// である。ここから登録するとなると、その知識を重複させるか、この
// クラスからApplicationへ逆に手を伸ばすかのどちらかになり、どちらも
// 「既に全ての文脈を持っている唯一の呼び出し元にそのまま持たせる」
// より劣る。
ListBrowserMode::ListBrowserMode(ContentView &view) : view_(view) {
  // 選択行の見た目は(ls)と同じ(2026-09-27、list_marks.hpp参照)。
  view_.setMarkedPredicate([this](int index) { return marks_.isMarked(index); });
  view_.setMarkedRowUsesUnderline(true);
}

void ListBrowserMode::show(const std::vector<std::string> &items, const std::vector<std::string> &badges) {
  items_ = items;
  marks_.clear();
  std::vector<ContentView::Line> lines;
  lines.reserve(items_.size());
  for (std::size_t i = 0; i < items_.size(); i++) {
    std::string text = items_[i];
    if (i < badges.size() && !badges[i].empty()) text += "  " + badges[i];
    // ellipsisEligible=true(2026-09-11追加、直接のユーザー報告:
    // "^X^F/^X^Dでも^S/^[iをLOCATEと同じように使いたい。Resの3文字が
    // `...`にあるやつ"): 以前はellipsisEligibleが未設定(既定false)
    // だったため、長いパス(^X^F/^X^Dが表示するRecentFile/
    // RecentDirectoryの実体)は画面幅で単純に右側が切り落とされる
    // だけで、Finder風の中間省略"..."が一切出なかった――このため
    // in-place GREP(^S/^[i、ListBrowserは既にisGrepInPlaceEligibleMode()
    // 対象)自体はマッチ件数を正しく算出するにも関わらず、マッチが
    // 画面幅を超えた場所(切り落とされた部分)にあるとハイライトが
    // 一切見えなかった。LocateMode/DuMode自身のLine構築と同じ
    // `ellipsisEligible=true`にすることで、長い項目はLOCATE同様に
    // 中間省略され、省略された"..."自体の中にマッチがある場合は
    // それ自体がハイライトされる(content_view.cpp自身の
    // EllipsisBoundaries/remapByteOffsetThroughEllipsis()、項目438
    // 参照)――ListBrowserModeは約9箇所の呼び出し元(FILE/DIR/PROGRAM/
    // MAN/LOCATE/FGREP/EGREP等の各履歴一覧)全てで共有されるが、短い
    // 項目(コマンド名等)には無害(画面幅未満なら省略自体が発生しない)
    // なので、無条件に有効にする。
    lines.push_back({text, std::nullopt, std::nullopt, {}, true});
  }
  view_.setLines(std::move(lines));
  view_.setCursorIndex(0);
  view_.redraw();
}

const std::vector<std::string> &ListBrowserMode::items() const { return items_; }

const std::string *ListBrowserMode::selectedItem() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(items_.size())) return nullptr;
  return &items_[idx];
}

}  // namespace fm
