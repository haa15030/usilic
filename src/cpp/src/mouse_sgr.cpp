#include "mouse_sgr.hpp"

#include <cstdio>

namespace fm {

std::optional<MouseSgrEvent> parseMouseSgr(const std::string &raw) {
  if (raw.empty()) return std::nullopt;
  char terminator = raw.back();
  if (terminator != 'M' && terminator != 'm') return std::nullopt;

  int cb = 0, cx = 0, cy = 0;
  std::string params = raw.substr(0, raw.size() - 1);
  if (std::sscanf(params.c_str(), "%d;%d;%d", &cb, &cx, &cy) != 3) return std::nullopt;
  if (cx < 1 || cy < 1) return std::nullopt;

  MouseSgrEvent event;
  event.column = cx - 1;
  event.row = cy - 1;
  event.button = cb & 0x3;
  if (cb & 0x40) {  // ホイール(MouseSgrEvent::wheel参照)。66/67(横)は向きを持たせない
    event.button = -1;
    if ((cb & 0x2) == 0) event.wheel = (cb & 0x1) ? 1 : -1;
  }
  event.motionOnly = (cb & 0x20) != 0;
  event.pressed = (terminator == 'M');
  return event;
}

}  // namespace fm
