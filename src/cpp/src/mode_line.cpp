#include "mode_line.hpp"

#include <algorithm>
#include <cstdio>

namespace fm {

std::string ModeLine::format(int currentLine, int totalLines) {
  std::string total = std::to_string(std::max(totalLines, 0));
  std::string current = std::to_string(std::max(currentLine, 0));
  if (current.size() < total.size()) current.insert(0, total.size() - current.size(), '0');

  // TOPの判定より先にチェックすることで、1行しかない一覧
  // (currentLine == totalLines == 1)ではTOPではなくENDと表示される
  // ――同じ行である場合、「先頭にいる」より「すべて見終えた」が優先。
  if (currentLine >= totalLines) return current + "/" + total + " END";
  if (currentLine <= 1) return current + "/" + total + " TOP";

  int percent = totalLines > 0 ? (currentLine * 100 / totalLines) : 0;
  char percentBuf[16];  // intの最大桁+"%"でも切れない(GCCの-Wformat-truncation)
  std::snprintf(percentBuf, sizeof(percentBuf), "%2d%%", percent);
  return current + "/" + total + " " + percentBuf;
}

// print_line()(disp.c)自身のlen<=0分岐からの移植――負のlenを渡す
// 唯一の呼び出し元はdisp_shell()なので、固定幅ゼロ埋め
// ("00002/00002 END")はSHELL固有のlegacy由来の慣習であり、他の全
// モードが使うformat()自身の「currentをTOTAL自身の桁数に合わせて
// パディングする」既定とは別物。TOP/END/%の接尾辞ロジックはformat()
// と同じ(この移植自身の2026-07-31のGUIドラッグスクロール由来の
// legacyからの逸脱を、この呼び出し元1箇所だけlegacy自身の素の
// 「ENDか%のみ、TOP無し」の形に戻すのではなく、ここでも一貫して
// 適用している――この選択に反する反例は出ていない)。幅自体は5
// (2026-08-24、ユーザーからの直接依頼: 「行数6桁は多過ぎなので
// 5桁でよろしく」――legacy自身のCソース(disp.c)は実際には6桁に
// ゼロ埋めしていると直接確認済みだが、ユーザーはそれを1桁広すぎると
// 感じ、具体的に5桁を依頼した。幅についてもlegacyをそのまま移植せず
// 意図的に変えている、すぐ上のTOP/END/%の逸脱と同じ精神)。
std::string ModeLine::formatFixedWidth(int currentLine, int totalLines) {
  constexpr std::size_t kWidth = 5;
  std::string total = std::to_string(std::max(totalLines, 0));
  std::string current = std::to_string(std::max(currentLine, 0));
  if (total.size() < kWidth) total.insert(0, kWidth - total.size(), '0');
  if (current.size() < kWidth) current.insert(0, kWidth - current.size(), '0');

  if (currentLine >= totalLines) return current + "/" + total + " END";
  if (currentLine <= 1) return current + "/" + total + " TOP";

  int percent = totalLines > 0 ? (currentLine * 100 / totalLines) : 0;
  char percentBuf[16];  // intの最大桁+"%"でも切れない(GCCの-Wformat-truncation)
  std::snprintf(percentBuf, sizeof(percentBuf), "%2d%%", percent);
  return current + "/" + total + " " + percentBuf;
}

}  // namespace fm
