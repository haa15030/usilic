#include "mac_cursor.hpp"

// Compiled instead of mac_cursor.mm on non-Darwin platforms (see the
// Makefile's own Darwin-only substitution) -- NSCursor/AppKit simply
// don't exist elsewhere. GraphicalDisplay::setHandCursor() calls these
// unconditionally (no #ifdef __APPLE__ at its own call site); both are
// harmless no-ops here, leaving the cursor whatever it already was.

namespace fm {

void setOpenHandCursor() {}
void clearOpenHandCursor() {}

}  // namespace fm
