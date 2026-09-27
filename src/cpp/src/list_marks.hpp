#pragma once

// 一覧の選択(マーク)の集合。2026-09-27新設、直接のユーザー依頼("（ls）で
// 可能なファイル操作（.[]）および^Jpプロンプトへのハイライト行挿入を、
// LOCATE、DU、FILE、USILIC でも可能にして"): LocateMode/DuMode/
// ListBrowserMode/UnderlineModeが共通に持つ。(ls)のDirectoryMode/ARCの
// ArchiveModeは、それぞれ独自の事情(ディレクトリの複数選択の禁止等)がある
// ので従来のまま。

#include <cstddef>
#include <set>

namespace fm {

class ListMarks {
 public:
  void clear() { indices_.clear(); }
  void toggle(int index, int size) {
    if (index < 0 || index >= size) return;
    if (!indices_.erase(index)) indices_.insert(index);
  }
  void markAll(int size) {
    for (int i = 0; i < size; i++) indices_.insert(i);
  }
  bool isMarked(int index) const { return indices_.count(index) > 0; }
  std::size_t count() const { return indices_.size(); }
  const std::set<int> &indices() const { return indices_; }

 private:
  std::set<int> indices_;
};

}  // namespace fm
