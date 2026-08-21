#pragma once

#include <string>
#include <vector>

#include "charset.hpp"
#include "display.hpp"

namespace fm {

// AttributeRun itself now lives in display.hpp (Display::AttributeRun,
// aliased back to plain fm::AttributeRun there) -- see its own comment
// for why (renderScrolled()'s new ScrolledLine needed it, and
// pipe_buffer.hpp already depends on display.hpp, not the reverse).

struct StyledLine {
  std::string text;
  std::vector<AttributeRun> attributes;
  bool endsWithNewline = true;  // false: wrapped because it hit `width`, not a real line break
};

// Turns raw bytes (file/pipe content) into width-wrapped, charset-
// normalized, attribute-annotated lines. Ported from alloc.c's
// alloc_pipe() (the width-wrap/tab/overstrike-detection state
// machine) and linein() (per-chunk charset detection/conversion,
// which now just calls the already-ported Charset class instead of
// alloc_pipe()'s own init_code()/toeuc()/jistoeuc()/euctoutf8_buf()
// calls).
//
// This is NOT a literal port. Legacy's alloc_pipe() did two
// unrelated things in one function: build the PP list AND drive the
// display directly as data streamed in (vt_move()/put_str()/
// print_line() calls interleaved with the parsing, for the "watch a
// long pipe's output arrive progressively" use case). PipeBuffer only
// does the first half -- feed() takes bytes, lines() returns what's
// been parsed so far. A future Mode/Application class drives the
// "show it as it arrives" behavior itself, by calling feed() in a
// loop and pushing lines() into a ContentView + redraw() after each
// call -- keeping "parse bytes into lines" and "show lines on
// screen" as separate concerns, matching the same split already
// established between Buffer-shaped classes and ContentView/Display.
//
// Deliberately excluded (see the class's individual method comments
// for exactly where): the K_a0 hex-dump-mode formatting (ContentView
// already excludes this for the same "depends on Buffer-side
// preprocessing that doesn't exist" reason -- this class doesn't
// produce it either), the "show non-ASCII bytes as <XX> hex when in
// pure ASCII display mode" feature, and A_md3 (a single-backspace
// overstrike whose repeated byte is non-ASCII) -- legacy's own
// CLAUDE.md history already flagged A_md3 as never-reproduced-in-a-
// real-file and left unfixed even there; ported here as the same
// known, intentional gap rather than a new one.
class PipeBuffer {
 public:
  // width: wrap column (legacy: vt_col(0)). maxBytes: total content
  // budget across all lines (legacy: `bsize`); overBudget() goes true
  // once exceeded, matching alloc_pipe()'s "not enough buffer" abort.
  PipeBuffer(Charset::Encoding encoding, int width, long maxBytes);

  void feed(const std::string &bytes);
  // Finalizes whatever partial line is still pending (end of input).
  void flush();

  const std::vector<StyledLine> &lines() const;
  bool overBudget() const;

 private:
  void processPending();
  std::string convertChunk(const std::string &rawChunk);
  void wrapLine(const std::string &convertedLine);
  int byteLengthAt(const std::string &s, int pos) const;
  int widthAt(const std::string &s, int pos, int charBytes) const;

  Charset::Encoding encoding_;
  int width_;
  long maxBytes_;
  long usedBytes_ = 0;
  bool overBudget_ = false;

  std::string rawPending_;      // bytes not yet charset-converted (no '\n' found yet)
  std::string wrapText_;        // current output line under construction
  std::vector<AttributeRun> wrapAttrs_;
  int wrapCol_ = 0;

  std::vector<StyledLine> lines_;
};

}  // namespace fm
