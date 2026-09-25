#include "underline_mode.hpp"

namespace fm {

UnderlineMode::UnderlineMode(ContentView &view) : view_(view) {
  // 選択行の見た目はDirectoryMode/ArchiveModeと同じ(カーソル行の反転と
  // 区別するため下線)。toggleMark()自身の宣言コメント参照。
  view_.setMarkedPredicate([this](int index) { return isMarked(index); });
  view_.setMarkedRowUsesUnderline(true);
}

void UnderlineMode::show(std::vector<std::string> paths, bool cursorAtEnd, int cursorIndex) {
  entries_ = std::move(paths);
  markedIndices_.clear();

  // ellipsisEligible=true: LocateMode::search()自身の同一のコメント
  // 参照――各エントリは個々の独立した絶対パスであり、まさにこの機能が
  // 想定して設計された「Finder風の中間省略」のケースそのものである。
  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  for (const auto &path : entries_) lines.push_back({path, std::nullopt, std::nullopt, {}, true});
  view_.setLines(std::move(lines));
  int last = static_cast<int>(entries_.size()) - 1;
  view_.setCursorIndex(cursorIndex >= 0 && cursorIndex <= last ? cursorIndex : (cursorAtEnd && last >= 0 ? last : 0));
  view_.redraw();
}

const std::vector<std::string> &UnderlineMode::entries() const { return entries_; }

const std::string *UnderlineMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

void UnderlineMode::toggleMark(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  if (!markedIndices_.erase(index)) markedIndices_.insert(index);
}

bool UnderlineMode::isMarked(int index) const { return markedIndices_.count(index) > 0; }

std::vector<std::string> UnderlineMode::targetEntries() const {
  std::vector<std::string> result;
  for (int index : markedIndices_) result.push_back(entries_[index]);
  if (result.empty())
    if (const std::string *entry = selectedEntry()) result.push_back(*entry);
  return result;
}

}  // namespace fm
