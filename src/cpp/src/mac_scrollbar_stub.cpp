#include "mac_scrollbar.hpp"

// Compiled instead of mac_scrollbar.mm on non-Darwin platforms (see
// the Makefile's own Darwin-only substitution) -- Objective-C++ syntax
// and the AppKit/Cocoa frameworks the real implementation needs simply
// don't exist elsewhere. GraphicalDisplay always calls
// MacScrollbar::create() unconditionally (no #ifdef __APPLE__ at its
// own call site) and treats a nullptr return as "fall back to the
// existing hand-drawn scrollbar" -- exactly what happens here, always.
// The other three methods are never reachable on a null MacScrollbar
// (every caller null-checks first, same convention as
// GraphicalDisplay::create() itself), so they're never called in
// practice -- still defined (rather than left undefined) purely so the
// class stays linkable if a future caller ever forgot that check.

namespace fm {

// std::unique_ptr<Impl>'s own destructor needs Impl to be a COMPLETE
// type wherever ~MacScrollbar()/the default constructor are actually
// instantiated (the classic pimpl requirement) -- this stub never
// constructs a real one (create() always returns nullptr below), so
// an empty placeholder is enough to satisfy that requirement here.
struct MacScrollbar::Impl {};

MacScrollbar::MacScrollbar() = default;
MacScrollbar::~MacScrollbar() = default;

std::unique_ptr<MacScrollbar> MacScrollbar::create(void *, std::function<void(double)>) { return nullptr; }

// UNLIKE create()/setFrame()/setPosition()/setVisible() above, this
// IS actually called on every platform (GraphicalDisplay::create()
// reads it unconditionally, before even attempting the native
// scrollbar, to size its own hand-drawn fallback strip identically
// either way) -- 15 is the same placeholder value this project's
// reserved scrollbar strip already used before this class existed at
// all (GraphicalDisplay::Impl::kScrollbarWidth's own original
// comment: "a rough placeholder, not yet measured"), kept here as the
// non-Apple fallback since there's no native widget to query a real
// value from on this platform.
int MacScrollbar::preferredWidth() { return 15; }

void MacScrollbar::setFrame(int, int, int, int) {}
void MacScrollbar::setPosition(double, double) {}
void MacScrollbar::setVisible(bool) {}

}  // namespace fm
