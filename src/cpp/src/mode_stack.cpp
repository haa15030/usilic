#include "mode_stack.hpp"

namespace fm {

const std::string ModeStack::kFloorMarker = "\x1a";

void ModeStack::push(ModeStackFrame frame) { frames_.push_back(std::move(frame)); }

std::optional<ModeStackFrame> ModeStack::pop() {
  if (frames_.empty()) return std::nullopt;
  ModeStackFrame frame = std::move(frames_.back());
  frames_.pop_back();
  return frame;
}

const ModeStackFrame *ModeStack::top() const { return frames_.empty() ? nullptr : &frames_.back(); }

bool ModeStack::empty() const { return frames_.empty(); }
std::size_t ModeStack::size() const { return frames_.size(); }

void ModeStack::pushFloor(const std::string &directory, const std::string &previousDirectory,
                           std::shared_ptr<void> view, int mode) {
  frames_.clear();
  ModeStackFrame floor;
  floor.mode = mode;
  floor.previousMarker = kFloorMarker;
  floor.name = directory;
  floor.directory = directory;
  floor.previousDirectory = previousDirectory;
  floor.modeLineArg = kFloorMarker;  // ported from save_stack_dir()'s `arg = strdup(str)` (same sentinel string)
  floor.view = std::move(view);
  frames_.push_back(std::move(floor));
}

bool ModeStack::isFloor(const ModeStackFrame &frame) const { return frame.previousMarker == kFloorMarker; }

const ModeStackFrame *ModeStack::findByView(const void *viewPtr) const {
  for (const auto &frame : frames_)
    if (frame.view.get() == viewPtr) return &frame;
  return nullptr;
}

const ModeStackFrame *ModeStack::findByName(const std::string &name) const {
  for (const auto &frame : frames_)
    if (frame.name == name) return &frame;
  return nullptr;
}

}  // namespace fm
