#pragma once

#include <string>
#include <vector>

#include "child_process.hpp"
#include "configuration.hpp"
#include "key_input.hpp"

namespace fm {

enum class WriteCheck {
  Plain,             // an ordinary RM/MV source: symlink, W_OK, or owned-by-current-user
  MustBeDirectory,    // MV's destination check when it must already exist as a directory
  MissingIsWritable,  // MV's destination check when a nonexistent target is fine (will be created)
};

// CP/MV/RM (mode.c's command(), the shell-script-building half of
// T_cp/T_mv/T_rm): copies, moves, or deletes a set of target files.
// Ported from command() + is_write() -- but see below for what's
// deliberately not carried forward.
//
// This is NOT a literal port of command(). Legacy's version builds a
// full shell script ("cd '<dir>'\nif ! cp -vRi ... \n") piped into a
// forked `sh`, because it needs to (a) chdir into the right directory
// first (its target list is a set of bare filenames relative to
// whichever buffer is currently displayed), and (b) handle several
// buffer-source-specific cases inline (T_arc/T_arc3 members being
// extracted out of an archive rather than copied from a real path on
// disk, T_du's paths being relative to a separately-measured `du`
// subdirectory). This class instead takes already-resolved, absolute
// paths (DirectoryMode::targetEntries() already gives real paths
// relative to DirectoryMode's own currentDirectory()) and calls
// cp/mv/rm directly via ChildProcess -- no chdir, no generated shell
// script, no string-building-then-reparsing.
//
// Deliberately excluded (future work, not this slice):
//   - Copying/moving/deleting entries that live inside an archive
//     (T_arc/T_arc3) or a `du`-measured subdirectory (T_du): both
//     need a resolved, on-disk source path to hand to cp/mv/rm in the
//     first place, which those buffer sources don't have without
//     extra work (extracting the archive member to a temp file, or
//     resolving the du-relative path) not built yet.
//   - The interactive confirmation prompt (cp/mv/rm's own `-i` flag,
//     already baked into Configuration's default CP/MV/RM role
//     commands, handles this at the OS level by prompting on stdin --
//     but this class uses ChildProcess::run(), which doesn't attach a
//     real terminal; a caller wanting interactive -i confirmation
//     should route through ChildProcess::runInteractive() instead,
//     the same execution shape ProgramMode/EditMode/ExecMode use).
//   - do_rm() (mode.c): despite the name, that function has nothing
//     to do with filesystem deletion -- it removes a saved entry from
//     one of the LIST-browser picker menus (recent files/dirs/man
//     pages/programs/grep patterns), a completely different feature
//     tied to Configuration's/History's editable list categories.
//     Not this class's concern; noted here only to head off the
//     obvious name confusion.
class CpMvRmMode {
 public:
  // keyInput: used only by the *Interactive() variants below (K-51/
  // K-53/K-55, "with SHELL window") to leave/re-enter raw mode around
  // ChildProcess::runInteractive(), same shape as ExecMode/ProgramMode/
  // EditMode already use -- see their own comments.
  CpMvRmMode(const Configuration &configuration, KeyInput &keyInput);

  // Ported from is_write(): whether path is a valid RM/MV target
  // under the given check. Root (uid 0) can always write. A symlink,
  // an access(W_OK)-writable path, or a path owned by the current
  // user all count as writable for Plain/MustBeDirectory; a
  // nonexistent path is writable only under MissingIsWritable
  // (MV's "destination doesn't have to exist yet" case) and is
  // otherwise rejected. MustBeDirectory additionally requires the
  // (existing) path to actually be a directory.
  static bool isWritable(const std::string &path, WriteCheck check = WriteCheck::Plain);

  // Copies/moves sources (absolute paths) to destination using the
  // configured cp/mv command (Configuration::commandFor(Cp/Mv)).
  // Captures output (progress lines from -v) rather than attaching a
  // real terminal -- see the class comment on -i confirmation. This is
  // usilicrc's K-52/K-54 ("without SHELL window").
  ChildProcess::Result copyTo(const std::vector<std::string> &sources, const std::string &destination);
  ChildProcess::Result moveTo(const std::vector<std::string> &sources, const std::string &destination);

  // Deletes targets (absolute paths) using the configured rm command.
  // K-56 ("without SHELL window").
  ChildProcess::Result remove(const std::vector<std::string> &targets);

  // K-51/K-53/K-55 (^X^Kc/^X^Km/^X^Kr, "with SHELL window"): same
  // cp/mv/rm invocation as copyTo()/moveTo()/remove(), but via
  // ChildProcess::runInteractive() instead of the captured run() --
  // the real command's own output (e.g. -v progress lines) is visible
  // live on the real terminal as it runs, instead of only being
  // available afterward via Result::output. Returns the child's exit
  // status, or -1 if sources is empty or no command is configured for
  // the role (mirroring ExecMode::run()'s own -1 convention).
  int copyToInteractive(const std::vector<std::string> &sources, const std::string &destination);
  int moveToInteractive(const std::vector<std::string> &sources, const std::string &destination);
  int removeInteractive(const std::vector<std::string> &targets);

 private:
  std::vector<std::string> buildArgv(ConfiguredRole role, const std::vector<std::string> &sources,
                                      const std::string *destination) const;
  ChildProcess::Result runWithSources(ConfiguredRole role, const std::vector<std::string> &sources,
                                       const std::string *destination);
  int runInteractiveWithSources(ConfiguredRole role, const std::vector<std::string> &sources,
                                 const std::string *destination);

  const Configuration &configuration_;
  KeyInput &keyInput_;
};

}  // namespace fm
