#include "directory_mode.hpp"

#include <optional>
#include <sstream>

#include "child_process.hpp"

namespace fm {

DirectoryMode::DirectoryMode(ContentView &view, ModeStack &modeStack, History &history,
                              const Configuration &configuration)
    : view_(view), modeStack_(modeStack), history_(history), configuration_(configuration) {
  // Marked rows render Standout even off the cursor -- see
  // ContentView::setMarkedPredicate()'s own comment for the real report
  // this fixes (marking previously had zero visual feedback at all).
  view_.setMarkedPredicate([this](int index) { return isMarked(index); });
  // Mode-line fields, ported from disp_window()'s own field layout for
  // the T_dir/file-selector case (name0[i]=='/' && *prev0==K_z keeps
  // the whole absolute path as-is, see its comment): the current
  // directory's full path on the left, and the listing command (e.g.
  // "(ls)") on the right -- NOT the selected entry's name, which was
  // this port's own invented design before this was corrected to
  // match the real CUI. The right-edge "usilic v1.5b" watermark
  // (rightWatermark, see its own comment on ModeLineFields) is the
  // OTHER thing legacy shows on this screen specifically -- confirmed
  // by a real CUI screenshot, and by prev0 genuinely being the "no
  // previous-mode text" sentinel for T_dir in the C source (unlike
  // File mode, which populates that slot with real content instead).
  // "1.5b" mirrors tcb/src/version.h's own version tag -- this is the
  // same project, same version epoch, just a different implementation
  // language, so there's no reason to invent a separate number here.
  // Registered once here rather than re-set on every enterDirectory()
  // call since it's a live callback into currentDirectory_/
  // configuration_, not a snapshot.
  // 2026-08-15 addition: `trailing` changed from the "(ls)" program
  // indicator to the listing's own total byte count, matching the
  // SAME convention every other mode's own trailing field already
  // uses (contentByteCount()/ContentView::totalByteCount(), see its
  // own comment) -- a direct user request ("全モードのモードラインに
  // 「画面TOPからENDまでのバイト数」を追加してほしい") specifically
  // named Directory as one of the modes still missing this. All THREE
  // of trailing/trailing2/rightWatermark were already spoken for here
  // (byte count makes a fourth concept with nowhere obvious to go), so
  // "(ls)" now lives in trailing2 instead, as a fallback shown only
  // when searchAnnotation_ isn't set -- a search annotation (which
  // mode a LOCATE/GREP/CUT search actually came from) is more
  // situationally useful than the program name, which is almost always
  // just "(ls)" anyway. A judgment call, not legacy-source-verified
  // (this port has no legacy binary to compare a byte-count-showing
  // Directory screen against); flagged for correction if this doesn't
  // match what was actually meant.
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    const auto &ls = configuration_.commandFor(ConfiguredRole::Ls);
    std::string program = ls.argv.empty() ? "" : ls.argv.front();
    std::string programBadge = program.empty() ? "" : "(" + program + ")";
    std::string trailing2 = searchAnnotation_.empty() ? programBadge : searchAnnotation_;
    // fillTrailingToEdge stays keyed on searchAnnotation_ specifically
    // (NOT trailing2's own emptiness, which is now ALWAYS non-empty
    // once "(ls)" is filling in as its fallback) -- a real regression
    // found "(ls)" itself getting padded all the way to the row's
    // right edge, which left renderModeLine()'s own drawnLen tracking
    // already AT the column count by the time it got to rightWatermark,
    // silently dropping "usilic v1.5b" via that function's own
    // `drawnLen + watermark.size() <= cols` check -- exactly the
    // "usilic v1.5b" half of the =bd/=be report this round was
    // supposed to fix, not preserve.
    ContentView::ModeLineFields fields{currentDirectory_, std::to_string(view_.totalByteCount()),
                                        /*fillTrailingToEdge=*/!searchAnnotation_.empty(), "usilic v1.5b", trailing2};
    // See reportEditError()'s/clearEditError()'s own comments -- NOT
    // cleared here on read. A real user report (2026-08-17, GUI mode
    // only: "-cはOKだが-gだといつもの一瞬出て消える症状") found that
    // clearing it here (an earlier version of this code) made it
    // vanish almost instantly under -g specifically: -g's run loop
    // calls redraw() far more often than a keypress alone warrants
    // (mouse-hover tracking, the ~60Hz idle loop, ...), so a
    // read-once-and-clear flag got consumed by one of THOSE incidental
    // reads before the message was ever actually visible on screen --
    // -c never has this problem since nothing there calls redraw()
    // except in direct response to a keypress. Cleared instead by
    // clearEditError(), called once at the top of Application::
    // handleCommand() for every dispatched command -- so this message
    // now persists through any number of incidental redraws exactly the
    // way legacy's own echo_mess() does, and only actually goes away
    // once the user does something else.
    //
    // Only `name`/`nameUnderlined` are overridden -- an earlier version
    // returned a fresh, mostly-default-constructed ModeLineFields here
    // instead, which silently dropped the byte count/"(ls)"/"usilic
    // v1.5b" fields (all still computed above) for the error message's
    // own duration -- a real report + screenshots (=cg/=ch) comparing
    // the mode line before/after showed those fields visibly missing.
    // Only the ONE field the user actually asked to be replaced
    // ("左端モード名...だけを丸々消してメッセージを表示させたら？")
    // changes now; everything else keeps rendering normally underneath.
    if (showEditError_) {
      // Padded (with trailing spaces, underlined along with the message
      // itself) to the width of the directory name it replaces, rather
      // than left at its own natural length -- a follow-up report found
      // that a shorter replacement string shifted every field after it
      // (the position/byte-count/trailing badges) left by the
      // difference, even though those fields' own *content* was by then
      // already correctly preserved (see the long comment block above).
      // Guarded against currentDirectory_ being shorter than the
      // message itself (e.g. "/"), in which case no padding is added
      // and the shift is unavoidable.
      std::string message = editErrorMessage_;
      if (message.size() < fields.name.size()) message.resize(fields.name.size(), ' ');
      fields.name = message;
      fields.nameUnderlined = true;
    } else if (!pendingOperationLabel_.empty()) {
      // "CP"/"MV"/"RM" in place of the directory path, plain Standout
      // (renderModeLine()'s own default for `name` -- NOT nameUnderlined,
      // which is reserved for the showEditError_ status-message
      // convention above; this is a MODE indicator, not an error) --
      // 2026-08-21 addition, a real user report ("^Xc,m,r等反応無し")
      // after the same day's ^X^M flow change (see Application::
      // startCopyPrompt()'s own comment): arming used to be completely
      // silent (no prompt shown until ^X^M), so a keypress that had, in
      // fact, registered looked indistinguishable from one that hadn't.
      // Set by Application::startCopyPrompt()/startMovePrompt()/
      // startRemovePrompt() via setPendingOperationLabel(), cleared by
      // clearPendingOperationLabel() once startDestinationPrompt() (^X^M)
      // consumes the arm (success OR failure/staleness) -- and here too,
      // by enterDirectory() itself, so a stale arm left over from
      // navigating away doesn't keep showing a label for an operation
      // that's no longer actually armed (see PendingCpMvRmOperation's
      // own comment for why the arm itself doesn't survive that either).
      //
      // Bare, UNPADDED "CP"/"MV"/"RM" -- 2026-08-22 correction, a direct
      // user comparison against a real reference screenshot (=ed):
      // legacy's own armed mode line concatenates fields at their
      // natural width (a short "CP" label simply leaves the fields
      // after it starting earlier than the unarmed layout would), unlike
      // showEditError_'s own padding above (which deliberately
      // preserves alignment with the SURROUNDING unarmed layout it
      // temporarily replaces -- a different, narrower goal that doesn't
      // apply here).
      fields.name = pendingOperationLabel_;
      // The FULL directory path, not truncated -- 2026-08-21 addition
      // (a direct user report, "=eiは（ls）からなので戻り値はディレク
      // トリ名では？"), corrected 2026-08-22 (another direct comparison
      // against =ed/=ee: legacy shows the path in full, right-padded
      // with spaces to the row's own right edge, not "...”-truncated to
      // a short tail the way drawPromptLabel()'s OWN destination-path
      // convention does elsewhere). fillTrailingToEdge below both pads
      // this to the edge AND (per its own established behavior, see
      // renderModeLine()'s own comment) squeezes rightWatermark out for
      // lack of room -- explicitly cleared here too, rather than left to
      // rely on that as an incidental side effect.
      fields.trailing2 = currentDirectory_;
      fields.fillTrailingToEdge = true;
      fields.rightWatermark = "";
    }
    return fields;
  });
}

bool DirectoryMode::enterDirectory(const std::string &path, bool consultHistory) {
  ProgramCommand ls = configuration_.commandFor(ConfiguredRole::Ls);
  if (ls.argv.empty()) return false;

  // Trailing-slash normalization, done HERE (before the `ls` argv is
  // built) rather than only at the end of this function where
  // currentDirectory_ itself gets it -- 2026-08-22 fix, a real report +
  // screenshot (=en): BSD `ls` (macOS) treats a symlink-to-directory
  // argument WITHOUT a trailing slash as "describe the symlink itself"
  // (matching `ls -d`), not "list its contents", unlike GNU/Linux `ls`
  // which always dereferences regardless -- `/tmp` itself is exactly
  // such a symlink (-> `/private/tmp`) on macOS. executeCopy()/
  // executeMove()'s own destination-navigation (enterDirectory(destination))
  // passes whatever the user actually typed in the prompt verbatim,
  // which had no reason to already end in "/" -- typing "/tmp" and
  // landing there afterward showed only `/tmp`'s own symlink line
  // ("1/1 END"), not its real contents.
  std::string normalizedPath = path;
  if (!normalizedPath.empty() && normalizedPath.back() != '/') normalizedPath += '/';

  std::vector<std::string> argv = ls.argv;
  argv.push_back(normalizedPath);

  ChildProcess::Options options;
  // Ported from fork_bin()'s LC_ALL=C forcing for exactly this
  // reason: parseLsLine()'s month-name anchor only recognizes English
  // abbreviations (see its own comment, ported from scroll.c's
  // get_col()). Without this, a non-English locale's `ls` falls back
  // to the last-space heuristic and mis-splits any filename
  // containing a space.
  options.forceCLocale = true;
  ChildProcess::Result result = ChildProcess::run(argv, options);
  if (!result.ok) return false;

  std::vector<DirectoryEntry> entries;
  std::vector<ContentView::Line> lines;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    if (line.rfind("total ", 0) == 0) continue;  // `ls -l`'s block-count summary line, not an entry
    DirectoryEntry entry = parseLsLine(line);
    ContentView::Line contentLine;
    contentLine.text = filenameOnlyMode_ ? entry.name : line;
    if (!filenameOnlyMode_) contentLine.highlightStartColumn = entry.nameStartColumn;
    lines.push_back(std::move(contentLine));
    entries.push_back(std::move(entry));
  }

  // Trailing-slash normalization only (NOT canonicalization -- no
  // symlink resolution, no relative-to-absolute conversion): matches
  // the CUI mode-line's own directory display convention (see
  // CLAUDE.md's real-screenshot comparison). Reuses normalizedPath
  // (computed above, before the `ls` argv was built) rather than
  // re-deriving the same normalization a second time.
  currentDirectory_ = normalizedPath;
  searchAnnotation_.clear();  // a fresh `ls` invalidates the old search -- see setSearchAnnotation()'s own comment
  entries_ = std::move(entries);
  markedIndices_.clear();  // marks are scoped to the listing they were made in; a fresh `ls` invalidates them
  // A CP/MV/RM arm is scoped to the directory it was made in too (see
  // Application::PendingCpMvRmOperation's own comment) -- clearing the
  // LABEL here as well (not just Application's own state) keeps the
  // mode line from continuing to show "CP"/etc. for an arm that a fresh
  // `ls` (e.g. navigating to a different directory) has already made
  // stale.
  clearPendingOperationLabel();
  // Same restore cancelPendingCpMvRm()/startDestinationPrompt() do on
  // their own un-arm paths -- a stale arm cleared here (navigating to a
  // different directory) must not leave the cursor row's own highlight
  // still suppressed.
  view_.setCursorRowAlsoHighlighted(true);
  view_.setLines(std::move(lines));

  // K-48 (^X^D, "DIR: Select directory name")'s own data source --
  // every successfully-listed directory counts as "accessed" (HELP.jp
  // 2.9's own broad wording), matching History::pushRecent()'s
  // existing first-occurrence-wins/capped-size behavior (see its own
  // comment) -- no extra bookkeeping needed here.
  history_.pushRecent(RingCategory::RecentDirectory, currentDirectory_);

  auto pos = consultHistory ? history_.positionFor(PositionCategory::Directory, currentDirectory_) : std::nullopt;
  if (pos) {
    int idx = pos->row;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(entries_.size())) idx = static_cast<int>(entries_.size()) - 1;
    view_.setCursorIndex(idx < 0 ? 0 : idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  return true;
}

void DirectoryMode::setSearchAnnotation(const std::string &text) { searchAnnotation_ = text; }

void DirectoryMode::reportEditError() { reportError("Invalid mode"); }

void DirectoryMode::reportError(const std::string &message) {
  editErrorMessage_ = message;
  showEditError_ = true;
}

void DirectoryMode::clearEditError() { showEditError_ = false; }

void DirectoryMode::setPendingOperationLabel(const std::string &label) { pendingOperationLabel_ = label; }

void DirectoryMode::clearPendingOperationLabel() { pendingOperationLabel_.clear(); }

const std::string &DirectoryMode::currentDirectory() const { return currentDirectory_; }
const std::vector<DirectoryEntry> &DirectoryMode::entries() const { return entries_; }

const DirectoryEntry *DirectoryMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

std::string DirectoryMode::parentDirectory(const std::string &dir) {
  if (dir.empty() || dir == "/") return "/";
  std::string trimmed = dir;
  if (trimmed.back() == '/') trimmed.pop_back();
  std::size_t slash = trimmed.find_last_of('/');
  if (slash == std::string::npos) return "/";
  return trimmed.substr(0, slash + 1);
}

bool DirectoryMode::descendTo(const std::string &target) {
  saveCurrentCursorPosition();
  ModeStackFrame frame;
  frame.directory = currentDirectory_;
  frame.name = currentDirectory_;
  modeStack_.push(std::move(frame));

  if (!enterDirectory(target)) {
    // Couldn't descend (permission denied, race with deletion, ...):
    // undo the push and stay put, matching legacy's own
    // access(p, R_OK)-fails-stay-in-T_dir behavior.
    modeStack_.pop();
    return false;
  }
  return true;
}

bool DirectoryMode::openSelected() {
  const DirectoryEntry *entry = selectedEntry();
  if (!entry || !entry->isDirectory) return false;
  // "./" (2026-08-23 fix, a direct user report): used to be a hard
  // no-op. Legacy's own equivalent (and this port's own ^J^K/
  // GoToFileSelector, already fixed the same way when already IN
  // Directory mode -- see Application::goToFileSelector()'s own
  // comment) doubles as a re-list even when "landing" somewhere
  // already showing -- re-running `ls` here is exactly how a stale
  // size/mtime column (e.g. right after overwriting a file in place
  // via CP) gets refreshed without navigating away and back. See
  // refresh()'s own comment for why this calls it (rather than
  // enterDirectory() directly) -- a same-day follow-up correction:
  // consulting saved history position snapped the cursor to a stale
  // row instead of keeping it where the user currently was.
  if (entry->name == "./") return refresh();

  std::string target;
  if (entry->name == "../") {
    target = parentDirectory(currentDirectory_);
  } else {
    target = currentDirectory_;
    if (!target.empty() && target.back() != '/') target += '/';
    target += entry->name;
  }
  return descendTo(target);
}

bool DirectoryMode::refresh() {
  const DirectoryEntry *before = selectedEntry();
  std::string selectedName = before ? before->name : std::string();
  if (!enterDirectory(currentDirectory_, /*consultHistory=*/false)) return false;
  if (!selectedName.empty()) {
    for (std::size_t i = 0; i < entries_.size(); i++) {
      if (entries_[i].name == selectedName) {
        view_.setCursorIndex(static_cast<int>(i));
        view_.redraw();
        break;
      }
    }
  }
  return true;
}

bool DirectoryMode::goToParent() {
  std::string target = parentDirectory(currentDirectory_);
  if (target == currentDirectory_) return false;  // already at root ("/"), matching legacy's own no-op there
  return descendTo(target);
}

bool DirectoryMode::goBack() {
  if (modeStack_.empty()) return false;
  saveCurrentCursorPosition();
  auto frame = modeStack_.pop();
  if (!frame) return false;
  if (modeStack_.isFloor(*frame)) {
    if (frame->previousDirectory.empty()) return false;
    std::string target = frame->previousDirectory;
    if (!enterDirectory(target)) return false;
    // Swap directory/previousDirectory and re-push -- see this
    // function's own declaration comment on why (a genuine toggle, not
    // a one-shot pop).
    modeStack_.pushFloor(target, frame->directory, nullptr, 0);
    return true;
  }
  return enterDirectory(frame->directory);
}

bool DirectoryMode::canGoBack() const { return !modeStack_.empty(); }

void DirectoryMode::resetNavigationToFloor(const std::string &previousDirectory) {
  modeStack_.pushFloor(currentDirectory_, previousDirectory, nullptr, 0);
}

void DirectoryMode::saveCurrentCursorPosition() {
  if (currentDirectory_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(entries_.size());
  pos.top = 0;  // ContentView recomputes a visible viewport from cursorIndex on restore; not load-bearing here
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::Directory, currentDirectory_, pos);
}

// Ported from scroll.c's get_col(): `ls -alF` output's date field
// ("Mon DD HH:MM " for recent files, "Mon DD  YYYY " for older ones)
// is always exactly 13 bytes from the month abbreviation to the start
// of the filename, and the month abbreviation is a reliable,
// locale-independent anchor (given forceCLocale above) -- unlike
// naively taking the last space in the line, which breaks the moment
// the filename itself contains a space (e.g. a Finder/QuickTime
// export like "2024-09-14 12.13.08.mp4"). Falls back to the last
// space if no recognizable date field is found (matching get_col()'s
// own fallback, e.g. for non-`ls -alF`-shaped lines).
DirectoryEntry DirectoryMode::parseLsLine(const std::string &line) const {
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  std::string searchable = line;
  std::size_t arrow = searchable.find(" -> ");  // symlink target -- not part of the name
  if (arrow != std::string::npos) searchable.erase(arrow);

  std::size_t nameStart = std::string::npos;
  for (const char *month : kMonths) {
    std::size_t pos = 0;
    while ((pos = searchable.find(month, pos)) != std::string::npos) {
      bool leftBoundaryOk = (pos == 0 || searchable[pos - 1] == ' ');
      bool rightBoundaryOk = (pos + 3 < searchable.size() && searchable[pos + 3] == ' ');
      if (leftBoundaryOk && rightBoundaryOk && searchable.size() > pos + 13) {
        std::size_t candidate = pos + 13;
        if (nameStart == std::string::npos || candidate < nameStart) nameStart = candidate;
      }
      pos += 1;
    }
  }
  if (nameStart == std::string::npos) {
    std::size_t lastSpace = searchable.find_last_of(' ');
    nameStart = (lastSpace == std::string::npos) ? 0 : lastSpace + 1;
  }

  DirectoryEntry entry;
  entry.lsLine = line;
  // searchable (arrow-stripped), not the raw `line` -- a symlink's " ->
  // target" portion belongs in lsLine (the raw display row) but NOT in
  // `name` (used to build real filesystem paths, e.g. openSelectedEntry()'s
  // own `path += entry->name`), which the original `line.substr()` here
  // never accounted for.
  if (nameStart < searchable.size()) entry.name = searchable.substr(nameStart);
  entry.isDirectory = !line.empty() && line[0] == 'd';
  // 2026-08-17 fix: `ls -alF`'s own trailing type-indicator character
  // ('*' executable, '@' symlink, '=' socket, '%' whiteout, '|' FIFO --
  // '/' for directories is handled separately below) used to stay
  // embedded in `name` verbatim, silently breaking every path built
  // from a non-directory entry with one of these types. Harmless for a
  // DIRECTORY's own trailing '/' (opendir()/stat() treat "dir" and
  // "dir/" identically, and openSelected() actually DEPENDS on it
  // staying present -- see its own `entry->name == "./"` checks -- so
  // directories are deliberately left untouched here). A real
  // reproduction (^J^L search -> open a LOCATE result -> ^J^K, landing
  // in a directory full of executables -- exactly how a real
  // .app/Contents bundle looks, which no synthetic test fixture in
  // this port ever chmod +x's) found Enter on ANY such entry silently
  // doing nothing: the constructed path (".../LicensingSettings.xml*")
  // doesn't exist on disk, so fileMode_.loadFile() failed and
  // openSelectedEntry() returned false with no visible change --
  // exactly the "^J^Kを使うと以後反応しなくなる" symptom reported,
  // previously misdiagnosed as broader state corruption.
  if (!entry.isDirectory && !entry.name.empty()) {
    char last = entry.name.back();
    if (last == '*' || last == '@' || last == '=' || last == '%' || last == '|') entry.name.pop_back();
  }
  entry.nameStartColumn = static_cast<int>(nameStart);
  return entry;
}

// "./"/"../" are never markable -- 2026-08-22 fix, a real user report
// ("CP:「./」「../」は選択から除外してほしい"): CP/MV/RM's own targets
// come straight from whatever's marked (or, with nothing marked, the
// cursor's own entry -- see targetEntries()'s own comment below), and
// "cp -R ." / "rm -r .." are exactly the kind of accident this guard
// exists to make structurally impossible, not just unlikely.
bool DirectoryMode::isSelfOrParentEntry(int index) const {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return false;
  return entries_[index].name == "./" || entries_[index].name == "../";
}

void DirectoryMode::toggleMark(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  if (isSelfOrParentEntry(index)) return;
  auto it = markedIndices_.find(index);
  if (it != markedIndices_.end())
    markedIndices_.erase(it);
  else
    markedIndices_.insert(index);
}

bool DirectoryMode::isMarked(int index) const { return markedIndices_.count(index) > 0; }

std::size_t DirectoryMode::markedCount() const { return markedIndices_.size(); }

void DirectoryMode::clearMarks() { markedIndices_.clear(); }

void DirectoryMode::markAll() {
  markedIndices_.clear();
  for (int i = 0; i < static_cast<int>(entries_.size()); i++) {
    if (isSelfOrParentEntry(i)) continue;
    markedIndices_.insert(i);
  }
}

std::vector<DirectoryEntry> DirectoryMode::targetEntries() const {
  std::vector<DirectoryEntry> result;
  if (!markedIndices_.empty()) {
    for (int idx : markedIndices_) result.push_back(entries_[idx]);  // std::set iterates in ascending order
    return result;
  }
  // Same "./"/"../" exclusion as toggleMark()/markAll() above, for the
  // cursor-fallback case (nothing explicitly marked) -- markedIndices_
  // itself can never contain either anymore, but the cursor can still
  // simply be resting on one.
  if (const DirectoryEntry *entry = selectedEntry()) {
    if (entry->name != "./" && entry->name != "../") result.push_back(*entry);
  }
  return result;
}

void DirectoryMode::setFilenameOnlyMode(bool enabled) {
  filenameOnlyMode_ = enabled;
  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  for (const auto &entry : entries_) {
    ContentView::Line line;
    line.text = enabled ? entry.name : entry.lsLine;
    if (!enabled) line.highlightStartColumn = entry.nameStartColumn;
    lines.push_back(std::move(line));
  }
  view_.setLines(std::move(lines));
  view_.redraw();
}

bool DirectoryMode::filenameOnlyMode() const { return filenameOnlyMode_; }

}  // namespace fm
