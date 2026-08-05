#include "list_browser_mode.hpp"

namespace fm {

// Mode-line fields: see Application's own constructor for why the
// actual ModeLineFieldsProvider is registered THERE instead of here.
// ListBrowserMode itself is deliberately unaware of RingCategory (see
// class comment's own "Deliberately excluded" list) -- but a real
// user report (2026-08-02: "^X^F（FILE）が置き換わってない") wants
// the mode-line to also show WHICH history category is being browsed
// ("FILE"/"DIR"/"HISTORY"/etc, matching legacy's own " FILE_HISTORY"-
// style naming -- see history.hpp's own RingCategory comments), which
// only Application knows at the point show() is called. Registering
// from here would mean either duplicating that knowledge or reaching
// back out to Application from this class, both worse than just
// letting the one caller who already has full context own it.
ListBrowserMode::ListBrowserMode(ContentView &view) : view_(view) {}

void ListBrowserMode::show(const std::vector<std::string> &items) {
  items_ = items;
  std::vector<ContentView::Line> lines;
  lines.reserve(items_.size());
  for (const auto &item : items_) lines.push_back({item, std::nullopt, std::nullopt, {}});
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
