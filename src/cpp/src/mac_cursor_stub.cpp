#include "mac_cursor.hpp"

// Compiled instead of mac_cursor.mm on non-Darwin platforms (see the
// Makefile's own Darwin-only substitution) -- NSCursor/AppKit simply
// don't exist elsewhere. GraphicalDisplay::setHandCursor() checks
// CursorImage::width before using the result, so an empty (all-default)
// CursorImage here is a harmless "extraction unavailable" signal that
// leaves the cursor whatever it already was.

namespace fm {

CursorImage extractOpenHandCursorImage() { return CursorImage{}; }

}  // namespace fm
