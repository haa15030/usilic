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
ListBrowserMode::ListBrowserMode(ContentView &view) : view_(view) {}

void ListBrowserMode::show(const std::vector<std::string> &items, const std::vector<std::string> &badges) {
  items_ = items;
  std::vector<ContentView::Line> lines;
  lines.reserve(items_.size());
  for (std::size_t i = 0; i < items_.size(); i++) {
    std::string text = items_[i];
    if (i < badges.size() && !badges[i].empty()) text += "  " + badges[i];
    lines.push_back({text, std::nullopt, std::nullopt, {}});
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
