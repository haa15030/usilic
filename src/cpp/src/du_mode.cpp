#include "du_mode.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "child_process.hpp"

namespace fm {

DuMode::DuMode(ContentView &view, History &history, const Configuration &configuration)
    : view_(view), history_(history), configuration_(configuration) {}

std::string DuMode::formatEntryLine(const DuEntry &entry, int sizeWidth) {
  std::string sizeText = std::to_string(entry.sizeKb) + "K";
  if (static_cast<int>(sizeText.size()) < sizeWidth) sizeText.insert(0, static_cast<std::size_t>(sizeWidth) - sizeText.size(), ' ');
  return sizeText + "  " + entry.path;
}

bool DuMode::loadUsage(const std::string &path) {
  ProgramCommand du = configuration_.commandFor(ConfiguredRole::Du);
  if (du.argv.empty()) return false;

  // 2026-09-09修正、実際のユーザー報告("その一覧ではカレントディレク
  // トリと配下の境界が//になっている"): このコードベース全体の慣習
  // として、DirectoryMode::currentDirectory()(このプロンプトの既定の
  // 入力値)は常に末尾に'/'を持つ――macOSの`du`(BSD du)は、末尾に
  // '/'が付いたパスを渡されると、子エントリのパスをそのまま
  // "<path>/<child>"ではなく"<path>//<child>"として出力する(直接
  // 確認済み: `du -ak /tmp/dutest/`は"/tmp/dutest//sub/file.txt"を
  // 出力するが、`du -ak /tmp/dutest`は正しく"/tmp/dutest/sub/file.txt"
  // を出力する)。ルート"/"自体を空文字列に縮退させないよう、末尾の
  // '/'は「ルート以外」の場合にだけ取り除く。
  std::string normalizedPath = path;
  if (normalizedPath.size() > 1 && normalizedPath.back() == '/') normalizedPath.pop_back();

  std::vector<std::string> argv = du.argv;
  argv.push_back(normalizedPath);
  ChildProcess::Result result = ChildProcess::run(argv);
  if (!result.ok) return false;

  std::vector<DuEntry> entries;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    std::size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;

    DuEntry entry;
    entry.sizeKb = std::atol(line.substr(0, tab).c_str());
    entry.path = line.substr(tab + 1);
    std::error_code ec;
    entry.isDirectory = std::filesystem::is_directory(entry.path, ec) && !ec;
    entries.push_back(entry);
  }

  // sizeWidth: このDIAGNOSTIC一覧全体での最大桁数――formatEntryLine()
  // 自身のコメント参照。
  int sizeWidth = 0;
  for (const auto &entry : entries) {
    int width = static_cast<int>(std::to_string(entry.sizeKb).size()) + 1;  // +1: "K"接尾辞
    sizeWidth = std::max(sizeWidth, width);
  }
  std::vector<ContentView::Line> lines;
  lines.reserve(entries.size());
  for (const auto &entry : entries) {
    // ellipsisEligible=true: Line::ellipsisEligible自身のコメント参照。
    lines.push_back({formatEntryLine(entry, sizeWidth), std::nullopt, std::nullopt, {}, true});
  }

  currentPath_ = path;
  entries_ = std::move(entries);
  view_.setLines(std::move(lines));

  if (auto pos = history_.positionFor(PositionCategory::Screen, currentPath_)) {
    int idx = pos->row;
    int last = static_cast<int>(entries_.size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  return true;
}

const std::string &DuMode::currentPath() const { return currentPath_; }
const std::vector<DuEntry> &DuMode::entries() const { return entries_; }

const DuEntry *DuMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

void DuMode::saveCursorPosition() {
  if (currentPath_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(entries_.size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::Screen, currentPath_, pos);
}

}  // namespace fm
