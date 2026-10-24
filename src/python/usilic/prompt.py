"""モードラインの入力欄(C++のapplication.cppのPromptKind・activatePrompt()・drawPromptLabel())。

種類ごとに、モードラインの名前(FILE・DIR等)・尋ねる文(「転送先？」等)・^Rの履歴の種類を持つ。
"""
import os

from . import history as hist
from .line_editor import LineEditor

FILE, DIRECTORY, MAN, LOCATE, EPUB, COPY, MOVE, REMOVE, ARCHIVE, FGREP, EGREP, EXIT = (
    'File', 'Directory', 'Man', 'Locate', 'Epub', 'Copy', 'Move', 'Remove', 'Archive', 'Fgrep', 'Egrep', 'Exit')
TOOL, PART, MACRO_KEY, BOOKMARK_SET, BOOKMARK_JUMP, MARK_REGEX, MARK_EXTENSION, OCCUR_FIXED, OCCUR_REGEX = (
    'Tool', 'Part', 'MacroKey', 'BookmarkSet', 'BookmarkJump', 'MarkRegex', 'MarkExtension', 'OccurFixed',
    'OccurRegex')
SHELL_COMMAND, MACRO_EDIT = 'ShellCommand', 'MacroEdit'

# 種類 -> (モードラインの名前, 尋ねる文, ^Rの履歴)
KINDS = {
    FILE: ('FILE', '', hist.RECENT_FILE),
    DIRECTORY: ('DIR', '', hist.RECENT_DIRECTORY),
    MAN: ('MAN', '', hist.RECENT_MAN_PAGE),
    LOCATE: ('LOCATE', '', hist.RECENT_LOCATE_QUERY),
    EPUB: ('EPUB', '', hist.RECENT_FILE),
    COPY: ('CP', 'Destination? ', hist.RECENT_DIRECTORY),
    MOVE: ('MV', 'Destination? ', hist.RECENT_DIRECTORY),
    REMOVE: ('RM', 'Delete? (y/n) ', None),
    ARCHIVE: ('ARC', '', hist.RECENT_DIRECTORY),
    FGREP: ('FGREP', '', hist.RECENT_FGREP_PATTERN),
    EGREP: ('EGREP', '', hist.RECENT_EGREP_PATTERN),
    EXIT: ('EXIT', 'Exit? (y/n) ', None),
    TOOL: ('TOOL', '', hist.SHELL_COMMAND),
    PART: ('PART', '', hist.RECENT_EGREP_PATTERN),
    MACRO_KEY: ('MACRO', 'Key: ', None),
    BOOKMARK_SET: ('BOOKMARK', 'Set: ', None),
    BOOKMARK_JUMP: ('BOOKMARK', 'Jump: ', None),
    MARK_REGEX: ('(ls)', 'Regexp: ', hist.RECENT_EGREP_PATTERN),
    MARK_EXTENSION: ('(ls)', 'Extension: ', None),
    OCCUR_FIXED: ('FGREP', '', hist.RECENT_FGREP_PATTERN),
    OCCUR_REGEX: ('EGREP', '', hist.RECENT_EGREP_PATTERN),
    SHELL_COMMAND: ('SHELL', '', hist.SHELL_COMMAND),
    MACRO_EDIT: ('MACRO', 'Key: ', None),
}


class Prompt:
    def __init__(self, kind, text='', on_submit=None, show_position=False):
        self.kind = kind
        self.label, self.message, self.history_category = KINDS[kind]
        self.editor = LineEditor()
        self.editor.set_text(text)
        self.on_submit = on_submit
        self.show_position = show_position   # CP・MV・RMは「CP 3/7 42% 転送先？」
        self.candidate_list = False  # 一覧から打ち始めた: 一覧が残り、^N・^Pで候補を移る(C++のpromptCandidateListActive_)


def complete_filesystem_path(partial, base_directory, directories_only=False):
    """^Iの補完の候補(C++のcompleteFilesystemPath()): partialの最後の/までをそのまま頭に、ディレクトリは/付き。名前順。"""
    slash = partial.rfind('/')
    dir_part, prefix = (partial[:slash + 1], partial[slash + 1:]) if slash >= 0 else ('', partial)
    if dir_part.startswith('/') or dir_part.startswith('~'):
        search = os.path.expanduser(dir_part)
    else:
        search = os.path.join(base_directory, dir_part)
    try:
        names = os.listdir(search)
    except OSError:
        return []
    out = []
    for name in names:
        if not name.startswith(prefix):
            continue
        is_dir = os.path.isdir(os.path.join(search, name))
        if directories_only and not is_dir:
            continue
        out.append(dir_part + name + ('/' if is_dir else ''))
    return sorted(out)


def path_program_candidates(partial):
    """$PATHの実行できるfileでpartialで始まる物(C++のpathProgramCandidates())。"""
    out = set()
    for directory in os.environ.get('PATH', '').split(':'):
        if not directory:
            continue
        try:
            entries = list(os.scandir(directory))
        except OSError:
            continue
        for entry in entries:
            if not entry.name.startswith(partial):
                continue
            try:
                if entry.is_dir() or not entry.stat().st_mode & 0o100:
                    continue
            except OSError:
                continue
            out.add(entry.name)
    return sorted(out)


def should_search_path(text):
    """まだコマンド名を打っている間(区切りの字が無い、.で始まらない)は$PATHから(C++のshouldSearchPath())。"""
    if text.startswith('.'):
        return False
    return not any(c in text for c in '/<>()|; ')
