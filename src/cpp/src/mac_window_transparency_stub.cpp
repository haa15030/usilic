#include "mac_window_transparency.hpp"

// Compiled instead of mac_window_transparency.mm on non-Darwin
// platforms (see the Makefile's own Darwin-only substitution) --
// NSWindow/AppKit simply don't exist elsewhere, and matchWindowSize()
// (this function's only caller) is itself only ever invoked from the
// macOS-only "実はすでにGUI" overlay launch path in the first place. A
// harmless no-op here.

namespace fm {

void enableWindowTransparency(void *) {}

}  // namespace fm
