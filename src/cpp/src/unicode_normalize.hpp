#pragma once

#include <string>
#include <vector>

namespace fm {

// Candidate byte-forms of `text` to try when doing a byte-literal
// substring match (FGREP/EGREP/CUT's own fgrep/egrep subprocess calls,
// LOCATE's own `locate` calls) against real filenames -- `text` itself
// first, then its NFC and/or NFD forms if they differ, de-duplicated.
//
// 2026-08-23 addition, a real user report: FGREP found "口座テスト"
// but not "マイナカード", both typed via the same IME into the same
// prompt, searching the same directory listing. The difference: the
// actual file "マイナカード表.png" stores its "ド" DECOMPOSED (NFD:
// "ト" + a combining dakuten, U+3099) on disk, while a normal IME
// always emits the single PRECOMPOSED codepoint (NFC) for "ド" -- the
// two are Unicode-canonically identical and render pixel-identical,
// but a literal byte-for-byte substring match (exactly what
// fgrep/egrep/locate all do) never matches across that boundary.
// "口座テスト" happened to match anyway only because none of its
// characters are decomposable (no dakuten/handakuten kana in it), so
// its NFC and NFD forms are byte-identical -- a coincidence, not
// evidence the underlying mechanism handles Japanese correctly in
// general (see docs/CHANGES.md/HISTORY.md for the full investigation,
// including the earlier -- separate, still valid -- TCC/iCloud-Drive-
// access finding this is NOT a duplicate of).
//
// APFS (unlike the older HFS+) is normalization-PRESERVING, not
// normalization-FORCING: a file's actual on-disk byte-form depends
// entirely on whatever created it, and BOTH forms genuinely coexist on
// the same machine (confirmed directly: two screenshot files sitting
// right next to each other, one NFD, one NFC). There is no single
// "always normalize to X" fix that covers every file -- trying the
// query in every plausible form and taking the first that matches
// does.
std::vector<std::string> unicodeNormalizationCandidates(const std::string &text);

}  // namespace fm
