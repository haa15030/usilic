#include "list_browser_mode.hpp"

namespace fm {

ListBrowserMode::ListBrowserMode(ContentView &view) : view_(view) {
  // Mode-line fields, ported from disp_window()'s own "enter string at
  // mode-line" grouping for M_hist/T_hist/T_hfile/T_hdir/etc (see
  // legacy's own f_enter=1 switch case in disp.c): unlike Directory/
  // File mode's own STATIC name field, legacy actually makes this an
  // EDITABLE text field kept synced to whichever list row is
  // currently highlighted as the cursor moves. Full editable-sync is
  // future work; this is the minimal slice a real user report
  // (2026-08-01: "^X^D等ではハイライト表示箇所の文字列がモード
  // ラインに表示されない") actually asked for -- just SHOWING the
  // highlighted item's own string in the mode-line, read-only, as the
  // name field (mode-line[1], same left-of-position placement as
  // every other mode's own name field). A live callback (like
  // DirectoryMode's own provider) since it must track cursorIndex,
  // not a one-time snapshot.
  view_.setModeLineFieldsProvider([this](int cursorIndex) -> ContentView::ModeLineFields {
    if (cursorIndex < 0 || cursorIndex >= static_cast<int>(items_.size())) return {};
    return {items_[cursorIndex], "", false, ""};
  });
}

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
