#include "underline_mode.hpp"

namespace fm {

UnderlineMode::UnderlineMode(ContentView &view) : view_(view) {}

void UnderlineMode::show(std::vector<std::string> paths, bool cursorAtEnd) {
  entries_ = std::move(paths);

  // ellipsisEligible=true: LocateMode::search()自身の同一のコメント
  // 参照――各エントリは個々の独立した絶対パスであり、まさにこの機能が
  // 想定して設計された「Finder風の中間省略」のケースそのものである。
  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  for (const auto &path : entries_) lines.push_back({path, std::nullopt, std::nullopt, {}, true});
  view_.setLines(std::move(lines));
  view_.setCursorIndex(cursorAtEnd && !entries_.empty() ? static_cast<int>(entries_.size()) - 1 : 0);
  view_.redraw();
}

const std::vector<std::string> &UnderlineMode::entries() const { return entries_; }

const std::string *UnderlineMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

}  // namespace fm
