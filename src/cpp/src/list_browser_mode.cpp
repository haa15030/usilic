#include "list_browser_mode.hpp"

namespace fm {

ListBrowserMode::ListBrowserMode(ContentView &view) : view_(view) {}

void ListBrowserMode::show(const std::vector<std::string> &items) {
  items_ = items;
  std::vector<ContentView::Line> lines;
  lines.reserve(items_.size());
  for (const auto &item : items_) lines.push_back({item});
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
