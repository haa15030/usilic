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
}


class Prompt:
    def __init__(self, kind, text='', on_submit=None, show_position=False):
        self.kind = kind
        self.label, self.message, self.history_category = KINDS[kind]
        self.editor = LineEditor()
        self.editor.set_text(text)
        self.on_submit = on_submit
        self.show_position = show_position   # CP・MV・RMは「CP 3/7 42% 転送先？」


def complete_path(text, base_directory, directories_only=False):
    """^Iの補完: textの最後の部分を、合うものの共通の頭まで延ばす(1つならディレクトリは/も)。"""
    expanded = os.path.expanduser(text)
    path = expanded if os.path.isabs(expanded) else os.path.join(base_directory, expanded)
    head, tail = os.path.split(path)
    try:
        names = sorted(n for n in os.listdir(head or '/') if n.startswith(tail))
    except OSError:
        return text
    if directories_only:
        names = [n for n in names if os.path.isdir(os.path.join(head, n))]
    if not names:
        return text
    common = os.path.commonprefix(names)
    added = common[len(tail):]
    if len(names) == 1 and os.path.isdir(os.path.join(head, common)):
        added += '/'
    return text + added
