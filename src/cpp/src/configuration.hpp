#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "charset.hpp"

namespace fm {

// Which concrete unpacker an ARCHIVE-directive suffix maps to --
// ported from is_chp()'s program-name classification (tar=5, lha=4,
// zip=3, bzip2=2, rpm=8, deb=9 in the legacy numbering; renamed to an
// enum here since the numbers themselves carried no meaning). Lets a
// caller pick the matching already-ported ArchiveReader subclass
// (TarReader/LhaReader/ZipReader/RpmReader) for a given suffix.
enum class ArchiveKind { None, Tar, Lha, Zip, Bzip2, Rpm, Deb };

// A program to run, in the shape ChildProcess::run()/runPipeline()
// already expect: argv is the final (or only) stage; pipedFromArgv,
// if set, is an upstream stage whose stdout feeds this one's stdin
// (e.g. ".tgz": pipedFromArgv={"gzip","-dc"}, argv={"tar","tvf","-"}).
// Replaces legacy CHILD's own way of representing this -- a raw
// `(CHILD*)`-cast pointer stashed in a spare argv[] slot of the
// downstream command (save_child()'s pipe-splitting branch) -- with
// two ordinary fields.
struct ProgramCommand {
  std::vector<std::string> argv;
  std::optional<std::vector<std::string>> pipedFromArgv;
};

// Built-in programs looked up by role rather than by file suffix --
// ls/du/cp/mv/rm/groff/man/locate/egrep/fgrep/the archive-format
// decompress-and-extract helpers. Ported from the legacy child[]
// table (str.c), which was itself a positionally-indexed char*[]
// keyed by S_ls/S_du/... enum values -- named here instead, since the
// position-in-an-array-of-strings indirection was pure implementation
// detail, not meaningful config shape.
enum class ConfiguredRole {
  Ls,
  Du,
  Cp,
  Mv,
  Rm,
  Groff,
  Man,
  Locate,
  UpdateDb,
  Egrep,
  Fgrep,
  GzipDecompress,
  Bzip2Decompress,
  Tar,
  LhaExtract,
  UnzipExtract,
  Rpm,
  Rpm2Cpio,
  DebExtract,
  // LOCATE's own personal database (2026-08-22 addition, docs/CHANGES.md
  // item 406 -- see setUserLocateDatabasePath()'s own comment): macOS
  // only. The system-wide database (ConfiguredRole::UpdateDb/Locate) is
  // built by `locate.updatedb` running as `nobody`, which can't read
  // INTO the user's own home directory at all on modern macOS (new
  // accounts default to `750`, no "other" access) -- so a plain `.txt`
  // sitting directly under `~` never gets indexed no matter how often
  // the system database is refreshed. These two roles build/search a
  // second, personal database instead, run entirely under the real
  // user's own account (no root needed), covering exactly the blind
  // spot the system one has.
  LocateUserDb,       // search: `locate -d <personal db path>`
  LocateUserDbBuild,  // build: a `find`+`locate.mklocatedb` pipeline, macOS only
};

// Parses usilicrc (tcbrc) and answers configuration lookups via
// explicit accessor methods. Ported from str.c's init_config() and
// its conf_*() directive sub-parsers (conf_arc/conf_bin/conf_grep/
// conf_prog/conf_bsize/conf_code/conf_history/conf_wait/conf_scroll/
// conf_ls), plus child.c's save_child()/init_child() (the CHILD
// registry these directives populate).
//
// This is NOT a literal port. Three structural changes:
//   - Legacy's config storage IS the global s_S()/s_P() string/table
//     lookup that every already-ported class in this project has
//     deliberately avoided calling directly (KeyInput's
//     setPidReporter()/setErrorPending(), ChildProcess's
//     stderrLogPath, Shell's whole Options struct all exist partly to
//     keep from reaching into it). Configuration is where that
//     avoidance cashes out: it owns the parsed data as ordinary
//     member fields, exposed through named methods, with no
//     equivalent of s_S()/s_P()'s "one flat array indexed by an enum
//     from a 90-entry header" shape.
//   - File I/O is not done here. loadRcText() takes usilicrc's
//     contents as an already-read string; locating the file
//     (legacy's set_tcbrc(): $HOME/.usilic/usilicrc, else
//     LIB/usilicrc, else refuse to start) is the caller's job. This
//     keeps parsing itself pure and unit-testable without a real
//     filesystem/$HOME, matching how PipeBuffer/ArchiveReader take
//     bytes rather than reading files themselves.
//   - Charset bootstrap is explicit, not a hidden getenv() call.
//     setEnvironmentCharset() takes whatever getenv(ENV_CHARSET)
//     returned (or nullopt if unset) as a plain parameter, the same
//     pattern Shell/ChildProcess use for their Options. This directly
//     fixes CLAUDE.md history item 13: legacy's `if ((tcbcharset =
//     getenv(ENV_CHARSET)))` only tested for NULL, so `export
//     TCBCHARSET=` (explicitly exported but empty) silently fell
//     through an unrecognized-value branch to a EUC default instead
//     of the documented UTF-8 one. Here, "explicitly set" and "what
//     value" are tracked as two independent, honest facts --
//     charsetExplicitlySet() is true for an empty explicit export
//     too, and an empty or unrecognized value never falls back to
//     anything but the documented UTF-8 default.
//
// Deliberately excluded (belongs to other classes, or deferred):
//   - The KEY section (F-*/K-*/M-* function-key, command-key, and
//     M-0..M-19 macro bindings): its bindings target mode/command
//     numbers that don't exist yet in this C++ port (that's a future
//     Mode/Application concern) -- parsing it now would produce
//     tables nothing can consume yet. Deferred until Mode exists;
//     loadRcText() skips a KEY section's body without error (matching
//     every other directive's "just data, no runtime effect from
//     parsing alone" treatment) rather than rejecting it.
//   - Actually running a program (ChildProcess) or an interactive
//     shell (Shell): Configuration only decides *what* argv answers a
//     lookup, never runs anything itself -- both of those classes'
//     own headers already say as much.
//   - Content-sniffing file-type dispatch beyond a straight suffix
//     match (is_man()-style detection, the FILE directive's
//     is_chp()-based archive-kind cross-classification of whatever
//     file(1) reports): future Mode territory, working in combination
//     with this class's suffix table, not owned by it.
//   - Checking whether a configured program is actually installed
//     ($PATH scan, legacy's is_inst()): legacy silently dropped a
//     directive line whose program wasn't found and logged a
//     nonfatal warning. Not reproduced here -- a caller invoking a
//     nonexistent program via ChildProcess::run() already fails
//     cleanly (see ChildProcess's own tests), so this isn't a safety
//     gap, just a startup-time UX nicety not yet carried forward.
//   - ~/.usilic/history's own file format and cursor-position storage
//     (a future History class): confirmed, by reading both files,
//     to share essentially no parsing logic with usilicrc beyond both
//     living under ~/.usilic/ -- the one genuine crossover is the
//     HISTORY directive's retention-count integer, exposed here via
//     historyRetentionCount() for History to take as a constructor
//     parameter.
//   - Applying a parsed setting as a side effect of parsing it
//     (legacy's conf_scroll() calls vt_mode()/vt_cs() directly while
//     parsing SCROLL; KEY-section errors call key_err(), which
//     fclose()s and exit(1)s mid-parse). loadRcText() only ever
//     builds data and appends to errors() -- applying
//     smoothScrollEnabled() to a Display, say, is entirely the
//     caller's job.
class Configuration {
 public:
  Configuration();

  // Parses usilicrc's already-read text. Directives are applied in
  // the order they appear, overriding the constructor's built-in
  // defaults as they're seen. An unrecognized directive keyword stops
  // parsing at that point (matching legacy's fatal treatment for
  // exactly that one case -- every other malformed-line case, e.g. a
  // BUFFER value out of range or a program that fails the (not
  // reproduced here) installed-check, was non-fatal in legacy and
  // stays non-fatal here, recorded via errors() instead). Returns
  // false only for that one fatal case. fileManagerMode mirrors
  // legacy's is_fm(): PROGRAM/CP/MV/RM directives only take effect
  // when true (matching pager-mode's own narrower directive set).
  //
  // Format note (ported exactly from init_config()'s own line-
  // classification, not obvious from the directive keyword alone): a
  // directive's body/data lines MUST start with a space or tab (or be
  // blank, or a '#' comment) to be recognized as data belonging to
  // that section. A line starting at column 0 with anything else is
  // always treated as the next directive keyword, ending the current
  // section right there -- there is no separate end-of-section
  // marker. A well-formed usilicrc indents every data line; e.g.:
  //   FILE
  //    .pdf 'xpdf' '-remote'
  //   EXEC
  //    .html firefox
  bool loadRcText(const std::string &text, bool fileManagerMode);
  const std::vector<std::string> &errors() const;

  // -- suffix dispatch (ARCHIVE/FILE/EXEC directives) --
  // filename's suffix is matched against every registered suffix by
  // "filename ends with this suffix"; the LONGEST matching suffix
  // wins (so a dedicated ".tar.bz2" entry is preferred over a plainer
  // ".bz2" one for a "foo.tar.bz2" file). Among equal-length
  // suffixes, a usilicrc-supplied entry wins over a built-in one --
  // a deliberate choice, not a literal port of legacy's own
  // suffix-list traversal order (see the .cpp for why: get_child()'s
  // exact tie-break semantics weren't fully recoverable from the
  // source read for this port, and "the user's override wins" is the
  // only defensible behavior for a config override system regardless
  // of what legacy's traversal order actually did).
  std::optional<ProgramCommand> programForSuffix(const std::string &filename,
                                                  ArchiveKind *archiveKindOut = nullptr) const;
  bool isExec(const std::string &filename) const;

  // -- editor/shell (EDITOR/SHELL directives) --
  // Legacy seeded a fixed-width "+??????????" placeholder argv slot
  // and patched it in place at launch time (a 1990s workaround for
  // not being able to easily grow a string); this just builds the
  // real "+<lineNumber>" argument directly.
  std::vector<std::string> editorArgv(const std::string &path, int lineNumber) const;
  std::vector<std::string> shellArgv() const;
  // Used when usilicrc has no SHELL directive at all -- legacy fell
  // back to $SHELL/file(1)-sniffed login-shell detection in that
  // case, refusing to start if neither that nor a SHELL directive
  // resolved to bash/tcsh. Configuration doesn't call getenv() or
  // sniff the filesystem itself (same "no hidden global state"
  // principle as setEnvironmentCharset()); the caller resolves a
  // fallback and supplies it here. Not calling this at all leaves
  // shellArgv() empty when no SHELL directive was present.
  void setFallbackShellArgv(std::vector<std::string> argv);

  // Wires ConfiguredRole::LocateUserDb/LocateUserDbBuild's default
  // commands to `path` (main.cpp's own `usilicDir + "locate.db"`,
  // called right after loadRcText() -- same "caller resolves the
  // filesystem detail, Configuration just stores it" boundary as
  // setFallbackShellArgv() above, and for the same reason: Configuration
  // itself never calls getenv()/touches the filesystem). Not called at
  // all (as in every existing test that constructs a bare Configuration
  // without wiring this) leaves both roles unconfigured -- commandFor()
  // returns an empty ProgramCommand, and LocateMode already treats an
  // empty argv as "this feature is unavailable", so the personal
  // database is silently a no-op rather than a hard requirement.
  // Linux-only builds (`#else` branch): a no-op -- see the enum's own
  // comment on why this is macOS-specific (a direct user decision:
  // "macOS限定項目は、すでにGUIもあるので同様でいい").
  void setUserLocateDatabasePath(const std::string &path);

  // -- role-based built-in commands (ls/du/cp/mv/rm/groff/man/... ) --
  // Only Cp/Mv/Rm can be overridden by a usilicrc directive (CP/MV/RM
  // -- legacy's only "splice a new CHILD node in place of an existing
  // one" directives); every other role is fixed at its built-in
  // default.
  ProgramCommand commandFor(ConfiguredRole role) const;
  // Overrides a role's command directly, bypassing usilicrc parsing
  // entirely -- exists for test isolation from roles whose real
  // default programs are unsuitable for an automated test suite (e.g.
  // ConfiguredRole::UpdateDb's real macOS/Linux commands genuinely
  // scan the whole filesystem, which is both slow and typically needs
  // root to complete -- 2026-08-13, added after exactly that hung an
  // actual `make test` run once the macOS default started pointing at
  // a real, executable binary instead of a nonexistent one). NOT a
  // general "any role is now usilicrc-configurable" escape hatch --
  // see commandFor()'s own comment on which roles that applies to.
  void setCommandForTest(ConfiguredRole role, ProgramCommand command);

  // -- seed lists (GREP/PROGRAM directives) --
  // Canned patterns/commands offered as history/autocomplete
  // candidates in the FGREP/EGREP and PROGRAM prompts -- plain
  // ordered lists; legacy's parallel LIST*/STR* tree pair was purely
  // an internal lookup-structure optimization with no user-visible
  // shape beyond "an ordered sequence of these strings".
  const std::vector<std::string> &grepSeedPatterns() const;
  const std::vector<std::string> &programSeedCommands() const;

  // -- scalar settings --
  // CODE directive: despite the name, NOT a charset whitelist -- a
  // whitelist of raw control-byte VALUES (0-31) that binary-content
  // detection should tolerate rather than treat as "this looks
  // binary" evidence. Named for what it actually does, not what
  // legacy called it, to avoid re-introducing that mismatch.
  const std::string &allowedControlBytes() const;  // CODE
  int bufferSizeLimitBytes() const;                // BUFFER
  int historyRetentionCount() const;               // HISTORY
  int waitIntervalMicros() const;                  // WAIT
  bool smoothScrollEnabled() const;                // SCROLL
  int lsMode() const;                              // LS

  // -- charset bootstrap --
  // See the class comment for why this directly fixes CLAUDE.md
  // history item 13.
  void setEnvironmentCharset(std::optional<std::string> envCharsetValue);
  bool charsetExplicitlySet() const;
  Charset::Encoding defaultCharset() const;

 private:
  std::optional<ProgramCommand> findBuiltinArchiveAlias(const std::string &searchText) const;

  struct SuffixEntry {
    std::string suffix;
    ProgramCommand command;
    bool isExec = false;
    ArchiveKind archiveKind = ArchiveKind::None;
  };

  std::vector<SuffixEntry> suffixEntries_;
  std::vector<SuffixEntry> builtinArchiveEntries_;  // ARCHIVE[]'s defaults, kept separately for alias lookup

  std::vector<std::string> editorProgram_;
  std::vector<std::string> shellProgram_;
  std::vector<std::string> fallbackShellArgv_;

  std::map<ConfiguredRole, ProgramCommand> roleCommands_;

  std::vector<std::string> grepSeedPatterns_;
  std::vector<std::string> programSeedCommands_;

  std::string allowedControlBytes_;
  int bufferSizeLimitBytes_ = 1 * 1024 * 1024;
  int historyRetentionCount_ = 200;
  int waitIntervalMicros_ = 100000;  // conf_wait()'s static k_wait initializer (100000)
  bool smoothScrollEnabled_ = false;
  int lsMode_ = 0;

  std::optional<std::string> envCharsetValue_;

  std::vector<std::string> errors_;
};

}  // namespace fm
