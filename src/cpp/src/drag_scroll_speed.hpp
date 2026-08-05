#pragma once

#include <vector>

namespace fm {

// Release-speed -> per-dot-step delay mapping for the GUI drag-scroll
// "inertia" (docs/ideas/IDEAS.md's cat's-paw scroll request), modeled
// directly on the author's own earlier FM-TOWNS app "cb" per the
// 2026-07-30 correction in CLAUDE.md: cb's speed control used usleep()
// for the lowest two speeds and a busy-loop spin (iteration count, not
// a fixed sleep) for everything faster -- because OS sleep granularity
// can't reliably hit the sub-millisecond per-dot delays the faster
// levels need. Each ScrollStep here is the delay for ONE DOT of
// scroll (pixelOffset moving by 1), not a per-frame delay -- this is
// what makes the mechanism the real thing rather than an approximated
// "jump and call it a blur": a caller doing many of these steps in a
// tight loop reproduces the same "next row's glyph image completes
// exactly as the current row's finishes scrolling out" effect cb had,
// just driven by GraphicalDisplay::renderScrolled()'s pixel-offset
// compositing instead of manual VRAM row-shifting.
//
// Deliberately NOT calibrated to feel identical on every machine: cb's
// own top speed being faster than legible was itself a property of the
// i486SX it ran on, not something the original tried to normalize away
// (per the user's own account, this was well-received, not a bug) --
// on faster modern hardware the top level(s) are simply faster still,
// consistent with that heritage rather than something to correct.
struct ScrollStep {
  bool busyLoop;        // false: usleep(microseconds) between dots; true: spin busyIterations instead
  long microseconds;     // used when !busyLoop
  long busyIterations;   // used when busyLoop
};

// Index 0 is a placeholder ({false,0,0}) and never used as an active
// speed -- callers use level 0 to mean "not coasting" (see
// levelForReleaseDistance()), so keeping it as a real (inert) table
// entry avoids an off-by-one at every call site.
//
// Deliberately NOT const (unlike everything else in this file):
// applyScrollSpeedOverride() below (the "-u" CLI flag, app/main.cpp)
// mutates this table's usleep()-driven entries in place at startup,
// before any real scrolling happens -- same "tune without a
// recompile" motivation as USILICSCROLLDEBUG just above, but for the
// actual delay values rather than which level gets selected.
extern std::vector<ScrollStep> kScrollSpeedLevels;

// Overrides the microsecond delay of each usleep()-driven level (see
// kScrollSpeedLevels' own comment: levels 1..4, !busyLoop) with
// `values`, assigned in order starting at level 1. Fewer values than
// there are usleep levels leaves the remaining trailing ones at their
// compiled-in default; values beyond that count are ignored (the
// busy-loop levels 5+ are deliberately NOT usleep-driven -- see the
// class-level comment on why -- so there is nothing for extra values
// to override). No-op if `values` is empty. Values <= 0 are skipped
// (leaving that level's compiled-in default) rather than passed to
// usleep(), which treats 0 as "no delay at all" -- a real level, but
// one already reachable via the busy-loop levels, not something a
// caller silently passing a bad/placeholder value should land on by
// accident.
void applyScrollSpeedOverride(const std::vector<long> &values);

// Maps the summed pixel distance the drag moved in the short window
// just before release to a discrete level: 0 (too small to count as a
// flick -- a plain release with no residual motion just stops, same
// as resting a finger on a real trackpad) through
// kScrollSpeedLevels.size()-1 (fastest). Thresholds scale with
// cellHeightPixels so the same physical flick feels similarly fast
// regardless of font size. Direction is the caller's job (sign of the
// same summed distance) -- this function only looks at magnitude.
int levelForReleaseDistance(int pixels, int cellHeightPixels);

// Scales a per-PIXEL ScrollStep (see kScrollSpeedLevels, calibrated
// for GraphicalDisplay's dot-by-dot renderScrolled()) up to a per-ROW
// delay for the real-terminal coast (app/main.cpp's runTerminalCoast/
// runCoastLoop), which has no sub-row addressing at all -- crossing
// one row there is ONE step, not cellHeightPixels of them, so its
// delay needs to be roughly dotsPerRow times longer to produce the
// same rows/second rate the pixel table produces in GUI mode.
// dotsPerRow defaults to 16, the "characters are 16 dots tall"
// convention this whole project has used since the original FM-TOWNS
// "cb" (see CLAUDE.md) -- real font metrics aren't available in a
// real terminal (no framebuffer access at all, see CLAUDE.md's
// 2026-07-30 note on why GraphicalDisplay exists), so this is
// necessarily an approximation. Found and added after the user
// reported the terminal coast's minimum speed felt far too fast
// compared to the GUI mouse-drag's own minimum speed, which they
// called "just right" -- unscaled, level 1's 90ms delay was being
// spent on a whole row instead of a single dot, i.e. roughly 16x too
// fast.
ScrollStep scaledForRow(const ScrollStep &pixelStep, int dotsPerRow = 16);

}  // namespace fm
