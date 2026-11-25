"""モードの切り替えとキーのディスパッチ(C++のapplication.cppのApplication)。

キーはC++版と同じK-番号(key_bindings.py、usilicrcのKEY節で置き換えられる)で受ける。打ったバイトを
pending_keysにため、割り当てと完全に一致すればその機能(handle_key_number())、どれかの頭なら次を待つ。
^Gの戻り先はreturn_stack(モードを積む、(ls)でディレクトリへ移ると空にする、C++と同じ)。
"""
import os
import re
import select
import shlex
import signal
import subprocess
import sys
import tempfile
import time

from . import mode_line, utf8
from . import history as hist
from . import help_text
from . import os_name
from . import children
from . import bookmarks as bm
from . import macro as mc
from . import messages_ja
from . import prompt as pr
from . import rescue
import types
from .archive_mode import ArchiveMode, remove_paired_apple_double_lines
from .configuration import Configuration, TAR
from .content_view import ContentView, Line, wrap_text, expand_tabs
from .copy_mode import CopyMode
from .directory_mode import DirectoryMode
from .du_mode import DuMode
from . import epub_vertical as ev
from .epub_mode import hide_emphasis_marks, strip_emphasis_marks, EpubBook, is_epub_file
from .markdown import is_markdown_file, render_markdown, pager_byte_count as markdown_pager_byte_count
from .file_mode import FileMode
from .grep_mode import GrepMode, FIXED, REGEX, match_spans
from .key_bindings import CTRL_SPACE, DIGIT, prefix_label, ALL as ALL_CONTEXT
from .line_editor import LineEditor
from .list_browser_mode import ListBrowserMode
from .locate_mode import LocateMode
from .man_mode import ManMode
from .part_mode import PartMode, occur_lines
from .messages_ja import japanese, localized
from .mouse import MouseEvent, Paste, Speeds, dropped_paths, level_for_release_distance, step_level_by_key
from .shell import Shell, PROMPT_DISPLAY_NAME

PROGRAM_LABEL = 'usilic-1.5b'
USILIC_TOC_TITLES = ['MAN', 'DIR', 'FILE', 'TOOL', 'WORK', 'USILIC', 'ARC', 'EPUB', 'PART']  # ^[uの目次(usilicTocTitles())
DIRECTORY, FILE, SHELL = 'Directory', 'File', 'Shell'
ARCHIVE, LIST, MAN, DU, LOCATE, KEY, HELP, TOC = 'Archive', 'List', 'Man', 'Du', 'Locate', 'Key', 'Help', 'Toc'
PART_TOC, PART, OCCUR, INFO = 'PartToc', 'Part', 'Occur', 'Info'
USILIC_SCRIPT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'usilic.py')
# ^J^Eの一覧の頭に、履歴に無ければ足す入口(C++のkUsilicWebDefaultUrl・kEpubListSiteUrls)。
USILIC_WEB_DEFAULT_URL = 'http://localhost/usilic-web/'
EPUB_LIST_SITE_URLS = ('https://yomou.syosetu.com/', 'https://ncode.syosetu.com/', 'https://noc.syosetu.com/',
                       'https://kakuyomu.jp/', 'https://www.aozora.gr.jp/', 'https://archiveofourown.org/')
# 小説サイトの入口(C++のisEpubNovelSiteUrl()): 作品の番号を探しに開く所なので、^Owの今の対処によらずOpenで開く。
NOVEL_SITE_HOSTS = ('syosetu.com', 'yomou.syosetu.com', 'noc.syosetu.com', 'kakuyomu.jp', 'www.aozora.gr.jp',
                    'ncode.syosetu.com', 'archiveofourown.org')


def is_usilic_web_url(text):
    return re.fullmatch(r'https?://\S*usilic[-_/]web\S*', text, re.I) is not None


def is_epub_list_site_url(text):
    return text in EPUB_LIST_SITE_URLS or text + '/' in EPUB_LIST_SITE_URLS


def is_novel_site_url(url):
    m = re.fullmatch(r'https?://([^/]+)/*', url)
    return bool(m) and m.group(1) in NOVEL_SITE_HOSTS


REPEATABLE = set(range(1, 13)) | {15, 16, 97}  # ^Uの回数が効くK-番号(移動・n/p・^Xe)
CTRL_G, CTRL_J, CTRL_O, CTRL_X, ESC, CR = 0x07, 0x0A, 0x0F, 0x18, 0x1B, 0x0D
TAB_COMPLETION_KINDS = (pr.FILE, pr.DIRECTORY, pr.EPUB, pr.COPY, pr.MOVE, pr.ARCHIVE, pr.TOOL, pr.MAN)
CPMVRM_MARKER = '#usilic-cpmvrm#'  # SHELLで動かしたCP/MV/RM・展開の終わりの印(C++のkCpMvRmExitMarker)

# ページャーとして起動した時に使えるK-番号(HELPの行頭がタブの行のキー、C++のpagerAllowedKeys())
PAGER_KEYS = set(range(1, 24)) | {28, 47, 48, 49, 51, 52, 99, 100}


def scroll_thumb_row_range(top_index, height, total):
    """C++のscrollThumbRowRange(): つまみの(頭の行, 行の数)。全部見えていれば(0, 0)。"""
    if height <= 0 or total <= height:
        return 0, 0
    def lround(x):
        return int(x + 0.5) if x >= 0 else -int(-x + 0.5)
    thumb_height = min(max(lround(height * (height / total)), 1), height)
    max_top = height - thumb_height
    thumb_top = min(max(lround(max_top * (top_index / (total - height))), 0), max_top)
    return thumb_top, thumb_height


class Application:
    def __init__(self, display, key_input, configuration=None):
        self.display = display
        self.key_input = key_input
        self.configuration = configuration or Configuration()
        self.bindings = self.configuration.key_bindings
        self.directory_view = ContentView()
        self.file_view = ContentView()
        self.file_view.cursor_highlight = False  # FileModeのコンストラクタと同じ(ページャーは反転しない)
        self.directory_mode = DirectoryMode(self.directory_view)
        self.directory_mode.column_threshold = self.configuration.column_threshold
        self.file_mode = FileMode(self.file_view)
        self.current_mode = DIRECTORY
        self.return_stack = []      # ^Gの戻り先(モード)
        self.pager_launch = False   # 引数のfile・標準入力で起動した(Application::pagerLaunch_)
        self.first_start_pager = False  # 最初の起動のHELP(C++のfirstStartPager_)
        self.parent_pid = 0          # 外部起動の子なら親のusilicのPID(C++のparentPid_)
        self.channel = children.Channel()  # 親子の受け渡しのFIFO(C++のchannelPath_)
        self.key_colon_list_launch = False  # -KのKEYの子(C++のkeyColonListLaunch_)
        self.external_markers = []   # 新しいウィンドウの子が終わると消えるファイル(C++のexternalPagerMarkers_)
        self.help_jump_on_start = None  # 親が開き直したHELPの子が起動したら飛ぶ行(USILIC_HELP_JUMP)
        self.children_pending = (False, False)  # -H・-Kで開くHELP・KEYの子(実行ループの最初で)
        self.pending_shell_offer = None  # 起動時のインストールの案内(実行ループの最初で、C++のpendingShellOffer_)
        self.shell_offer = None      # 置いたコマンドの続き(C++のshellOffer_)
        self.help_in_parent_pending = False  # 最初の起動: 実行ループの最初でHELPを開く
        self.limited_leader_j = False  # 機能限定SHELLの^Jの後
        self.limited_escapes = 0     # 機能限定SHELLの^[^[^M(すぐ終わる)
        self.pager_numbers_cache = None  # pager_key_numbers()
        self.pager_help_return = None  # ページャーのfileからHELPを開いた時の戻り先(remember_pager_file_for_help())
        # EPUBの余白(^[+矢印、C++の項目1144・1146): 縦書きは上下の行、横書きは左右の桁。
        self.epub_margin_top = self.epub_margin_bottom = self.epub_margin_left = self.epub_margin_right = 0
        self.epub_margin_columns = 0  # 今の本文の行の頭に足した空白(幅が狭ければ0、C++のepubMarginColumns_)
        self.epub_margin_side = None  # ^[+矢印の後、↑/↓を待っている間の矢印(C++のepubMarginSide_)
        self.pager_files = []       # n/p/x/fで切り替えるfile
        self.pager_index = 0
        self.start_directory = os.getcwd()
        self.wants_exit = False
        self.rescue_exit = False  # ^J^Rで終えた(usilic.pyが終えた後に「. U」へ、U.shの-vからならU.shへ戻る)
        self.notice = None          # showTransientNotice()の文(次のキーで消える)
        self.pending_keys = []      # 照合中のキー(C++のleaderXPending_等をまとめたもの)
        self.goto_digits = None     # ^[Nの数字(gotoLineDigits_)
        self.prompt = None          # モードラインの入力欄(prompt.Prompt)
        self.mark_before_jump = None  # ^X^Xの戻り先(markBeforePageScroll()等)
        self.prefix_since = None
        self.prefix_shown = False
        self.utf8_pending = bytearray()
        # SHELL(Application::shell_)
        self.shell = Shell()
        self.shell_view = ContentView()
        self.shell_view.cursor_highlight = False
        self.shell_view.bottom_anchored = True
        self.shell_browsing = False  # ^P・^Nで確定した行を動いている間(Shell::historyBrowsing())
        self.shell_started = False   # このセッションでbashを動かした(C++のshellStarted_、ログを書く)
        self.shell_log_block = ''    # saveShellLog()がこのセッションで最後に書いた分(shellLogBlock_)  # C++のShellのscrollback_.setBottomAnchored(true)
        self.command_line = LineEditor()
        self.pending_shell_run = None   # SHELLで動かしているCP/MV/RM・展開(終わったら行き先の(ls)へ)
        self.history = None  # ディレクトリで起動した時だけ(usilic.py)
        # FGREP/EGREP(grepInPlaceActive_、PromptKind::Fgrep/Egrep)
        self.grep = GrepMode()
        self.grep_active = False
        self.grep_prompt = None   # (syntax, LineEditor)を打っている間
        self.keep_directory_for = None  # このfileから^Gで(ls)へ戻る時は、fileのディレクトリへ移らない(USILIC_OPEN、U.shの-v)
        self.prompt_field = (0, 0)  # 入力欄の始まりの桁・出している字の始まり(prompt_mode_line())
        self.jump_highlight = None  # KEYのhで飛んだ行の強調(view, 行, 字, 始まり)(grepJumpHighlightView_)
        # 一覧・各モード
        self.list_view = ContentView()
        self.list_mode = ListBrowserMode(self.list_view)
        self.list_prompt = None    # ^Rで一覧を開いた元のプロンプト(^Mでそこへ戻す)
        self.tab_completion = None  # ^I^Iの候補の一覧の(元のプロンプト(SHELLはNone), 補完した語, その前の部分)
        self.prompt_swallow_byte = False  # プロンプトの中の^Xrの次の字(捨てる)
        self.tab_armed = False      # 候補が幾つもあった^Iの直後(続けて^Iで一覧、C++のpromptTabArmed_)
        self.archives = []         # ARCの入れ子(一番後ろが今のアーカイブ)
        self.man_view = ContentView()
        self.man_view.raw_line_numbers = False  # C++のMANの行は元の行の番号を持たない
        self.man_mode = ManMode(self.man_view, self.configuration)
        self.du_view = ContentView()
        self.du_mode = DuMode(self.du_view, self.configuration)
        self.locate_view = ContentView()
        self.locate_mode = LocateMode(self.locate_view, self.configuration)
        self.key_view = ContentView()
        self.key_view.raw_line_numbers = False  # KEYの一覧(CUTの見出し)も
        self.help_view = ContentView()
        self.help_view.cursor_highlight = False
        self.help_byte_count = 0
        self.key_byte_count = 0
        self.toc_view = ContentView()
        self.epub = None           # 開いている本(EpubBook)
        self.epub_name = ''        # モードラインの名前(HELPの本は「HELP」)
        self.epub_toc_lines = []   # 目次の各項目の本文の行
        self.previous_file = None  # ^J^Jの行き先
        self.dir_list_origin = ''  # ^X^Dの一覧から(ls)へ移る前のディレクトリ(C++のdirListOriginDirectory_)
        # 第4段: PART・OCCUR・INFO・COPY・マーク・^U・マクロ・レジスタ等
        self.part_toc_view = ContentView()
        self.part_view = ContentView()
        self.part_view.cursor_highlight = False
        self.part_view.raw_line_numbers = False  # 部分の中は折り返した行で数える(C++のcutContentView_)
        self.part = PartMode(self.part_toc_view, self.part_view)
        self.part_position_key = None  # FILEを分けたPARTの位置を覚える名前(「パス "パターン"」、C++のcutPositionKey_)
        self.occur_view = ContentView()
        self.occur_items = []      # (元の画面の行, Line)
        self.occur_origin = None   # 一覧を作った元のモード
        self.occur_syntax = FIXED
        self.occur_global = False  # occur_itemsの行が本全体の行(EPUBの記録の一覧)
        self.info_view = ContentView()
        self.info_targets = []     # INFOの行ごとの開く物(Noneは開けない)
        self.copy = None           # COPY(CopyMode)
        self.copy_saved_position = None  # COPYに入る前の(ビュー, カーソル, 頭)
        self.copy_prefix = None    # COPYの中の'ESC'・'X'・'goto'
        self.copy_digits = ''
        self.clipboard_text = ''
        self.mark_ring = []        # (view, 行)、最大16(^␣・^X^X・^U^␣)
        self.count = None          # ^Uの回数
        self.count_digits = None
        self.last_command = None   # ^Xzで繰り返す(K-番号, 数字, 回数)
        self.repeating_z = False
        self.register_state = None  # 'r'(^Xrの後)・'set'・'jump'
        self.registers = {}
        self.macro_recording = None  # ^X(〜^X)のバイト列
        self.macro_repeat_armed = False  # ^Xeの直後(eでもう一度、C++のmacroRepeatArmed_)
        self.macro_list = None  # MACROの一覧の(行, 記録を書いた直後か)
        self.macro_list_last_number = 0  # 最後に書いたM-番号(次の^Xsはその行から)
        self.macro_bytes = b''
        self.macro_just_played = False
        self.playing = False
        self.mode_line_hidden = False  # ^Of
        self.url_mode = 0              # ^Owで巡回(usilicrcのURLの何番目か)
        self.epub_link_stack = []      # 本の中のリンクで飛ぶ前の行(^Gで戻る)
        self.epub_chapter_mode = False  # ^Os(章ごと)
        self.epub_lines = []           # 本全体の行(章ごとの時はその一部をfile_viewへ)
        self.epub_chapter = 0
        # 縦書き(本のOPFがpage-progression-direction="rtl"の時、epub_vertical.py)
        self.vertical = None           # 組んだ列の並びと位置(VerticalBook)
        self.vertical_view = ContentView()
        self.vertical_view.cursor_highlight = False
        self.vertical_key = None       # 組んだ時の(本の行, 行数, 桁数, 章ごと)、変われば組み直す
        self.vertical_synced = None    # 縦書きに合わせた本全体の行(横書きの側で動いたら縦書きを合わせる)
        self.vertical_pieces = {}      # 元の行→[(本全体の行, 頭の空白の数, 論理行の中での始まり)]
        self.vertical_last_number = None
        self.vertical_source = None    # 組んだ時の本全体の行(epub_lines、別の物になれば組み直す)
        # 縦書きの本の中の横書きの範囲(奥付等、EpubHints.horizontal)を見ている間、その範囲(元の行の両端)
        self.epub_section = None
        self.epub_section_line = None  # 範囲を見ている時の本全体の行(キー以外で動いたら範囲を離れたとみなす)
        self.epub_from_below = False   # 先へ送って範囲へ入り、最初の行がまだ画面の上まで上がっていない
        self.epub_note = None          # 注釈の区画を見ている間、元の所(show_epub_note())
        self.last_key_number = None    # 最後のK-番号(横書きの範囲から出たかの決め方)
        # マウス(C++のapp/main.cppのrunTerminalPhase()・runTerminalMouseDrag()・runCoastLoop())
        self.drag = None               # 左ボタンを押している間(押した所・動いた量・離す直前の動き)
        self.coast = None              # 慣性スクロール(段・向き・縦書きの列か・次に進む時刻)
        self.click_targets_cursor = False  # 慣性を止めた後の次のクリックはハイライト行へ
        self.wheel_reversed = False    # ^Od(ホイールの向き)
        self.speeds = Speeds(self.configuration.speed_values, self.configuration.arrow_values)
        # 縦書きの本の章の一覧・検索結果の一覧(OCCUR)の縦書き(C++のrefreshEpubTocVertical())
        self.vlist = None              # 組んだ一覧(VerticalBook)
        self.vlist_key = None
        self.vlist_first = {TOC: 0, OCCUR: 0}  # 右端の列(一覧ごと)
        self.vlist_view = ContentView()
        self.vlist_view.cursor_highlight = False
        self.translate_pending = [[], None]  # KEY節の読み替えで溜めている途中のバイト(KeyBindings.feed())
        self.utf8_skip = 0
        self.vertical_prefix = None    # 縦書きのCOPYの'ESC'
        # 作業ファイルの置き場(C++のworkRootDirectory_と同じ/tmp/.usilic.<PID>、終わる時に消す)
        self.work_root = os.path.join('/tmp' if os.path.isdir('/tmp') else tempfile.gettempdir(),
                                      '.usilic.%d' % os.getpid())
        self.work_files = []
        self.last_shell_insert = ''    # ^Wで出し直す(^J^W・^Jw)
        self.tool_directory = ''
        self.bookmark_place = None     # ^Xrmで置く所
        self.list_stack = []           # 一覧の中から開いた一覧(^Gで前の一覧へ)
        self.prompt_ctrl_o = False     # プロンプトの^O(^O^Y)
        self.occur_byte_count = self.info_byte_count = 0

    # --- 画面 ---
    def views(self):
        return [self.directory_view, self.file_view, self.shell_view, self.list_view, self.man_view, self.du_view,
                self.locate_view, self.key_view, self.help_view, self.toc_view, self.part_toc_view, self.part_view,
                self.occur_view, self.info_view, self.vertical_view, self.vlist_view] + \
            [a.view for a in self.archives]

    def active_view(self):
        return {
            PART_TOC: self.part_toc_view, PART: self.part_view, OCCUR: self.occur_view, INFO: self.info_view,
            SHELL: self.shell_view, FILE: self.file_view, LIST: self.list_view, MAN: self.man_view,
            DU: self.du_view, LOCATE: self.locate_view, KEY: self.key_view, HELP: self.help_view,
            TOC: self.toc_view, ARCHIVE: self.archives[-1].view if self.archives else self.directory_view,
        }.get(self.current_mode, self.directory_view)

    def refresh_shell_view(self, follow=True):
        """SHELLの行を作り直す。followなら最後(プロンプト)へ(出力が来た時、^Pで戻っていてもやめる)。"""
        at_end = self.shell_view.cursor_index >= len(self.shell_view.lines) - 1
        self.shell_view.set_lines(self.shell.view_lines(self.wrap_width(), self.shell_live_text()[0]))
        if follow:
            self.shell_browsing = False  # C++の新しい出力・送ったコマンドでのexitHistoryBrowsing()
        if follow or at_end:
            self.shell_view.go_to_bottom()

    def shell_live_text(self):
        """打っている行に見せるコマンドとカーソルの桁。プロンプトの後ろの欄に入らなければ、C++と同じく折り返さずに
        横にずらす(Shell::updateCommandLineField()の欄)。プロンプトが来る前は折り返す。"""
        if not self.shell.prompt_ready:
            return self.command_line.text, None
        width = max(1, self.display.columns - utf8.text_width(expand_tabs(self.shell.prompt)))
        return self.command_line.visible_slice(width)

    def live_row_count(self):
        """SHELLの最後の「プロンプト+編集中のコマンド」が折り返して何行か。"""
        head = self.shell.prompt if self.shell.prompt_ready else self.shell.pending
        return len(wrap_text(expand_tabs(head + self.shell_live_text()[0]), self.wrap_width()))

    def exit_shell_browsing(self):
        """Shell::exitHistoryBrowsing(): ^Pで戻っていたのをやめて、打っている行へ。"""
        self.shell_browsing = False
        self.shell_view.go_to_bottom()

    def layout(self):
        rows, columns = self.display.refresh_size()
        for view in self.views():
            view.set_height(rows - (0 if self.mode_line_hidden else 1))
        self.vertical_view.set_height(self.epub_vertical_rows())
        self.vlist_view.set_height(self.epub_vertical_rows())
        self.file_mode.rewrap(self.wrap_width())
        self.directory_mode.columns = columns  # 幅が変わったら名前だけの表示かを決め直す(C++と同じ)
        self.directory_mode.apply_column_auto_display()
        if self.shell.alive():
            self.shell.on_resize(rows - 1, columns)
            self.refresh_shell_view()

    def wrap_width(self):
        """折り返しの幅(桁いっぱい、C++版と同じ)。"""
        return max(1, self.display.columns)

    def position_numbers(self, view):
        """モードラインの位置(今の行, 行の数)。行が元の行の番号を持っていれば(折り返したFILE等)その番号で数える
        (C++のContentView::applyRawLineNumbers())。EPUBの章ごとの時も本全体の行の数(C++は本全体の一部を見せる)。"""
        lines = view.lines
        current, total = (view.cursor_index + 1 if lines else 0), len(lines)
        if lines and lines[0].raw_line_number is not None and view.raw_line_numbers and not (
                view is self.file_view and self.archive_member_shown()):
            index = max(0, min(view.cursor_index, len(lines) - 1))
            current = lines[index].raw_line_number if lines[index].raw_line_number is not None else current
            whole = self.epub_lines if view is self.file_view and self.epub is not None and self.epub_chapter_mode \
                and self.epub_lines else lines
            total = whole[-1].raw_line_number if whole[-1].raw_line_number is not None else total
        return current, total

    def position_text(self, view, fixed=False):
        if self.notice:
            return self.notice
        return (mode_line.format_fixed_width if fixed else mode_line.format)(*self.position_numbers(view))

    def mode_line_segments(self):
        """モードライン: [(字, 反転するか)]。欄で組むものはC++と同じく欄だけ反転し、欄の間は反転しない空白1つ
        (ContentView::scrollModeLineSegments())。マクロの記録中はモード名をMACROに(modeNameOverride())。"""
        body = self.mode_line_body()
        recording = self.macro_recording is not None and self.prompt is None
        if isinstance(body, tuple):
            fields, position = body
            if recording:
                fields.name = 'MACRO'
            if self.goto_digits is not None:  # ^[gg: 位置の欄の幅のまま、反転しない入力欄に(gotoLineFieldWidth_)
                padded = self.goto_digits.ljust(len(position))
                segments = mode_line.compose(fields, padded, self.display.columns, position_plain=True)
                column = 0
                for text, attr in segments:
                    if attr == mode_line.PLAIN and text == padded:
                        self.prompt_field = (column, 0)
                        break
                    column += utf8.text_width(text)
                return segments
            # 知らせ(show_transient_notice())は反転しない(C++のsetPositionTextOverride(text, plain=true))
            return mode_line.compose(fields, position, self.display.columns,
                                     position_plain=bool(self.notice) and position == self.notice)
        if isinstance(body, list):
            return body
        return ('MACRO   ' + body) if recording else body   # 字のまま(プロンプト等、行の端まで反転)

    def archive_member_shown(self):
        """ARCの中のfileを見ている(C++のarchiveContentView_: 位置も大きさも画面の行で数える)。"""
        return self.current_mode == FILE and (self.file_mode.tail or '').startswith('ARC: ')

    def content_byte_count(self):
        """本文の行のバイト数(C++のcontentByteCount(fileView_)): EPUBの章の一覧・縦書きの大きさ。"""
        lines = self.epub_lines if self.epub is not None and self.epub_lines else self.file_view.lines  # 章ごとでも本全体
        return sum(len(line.text.encode('utf-8')) + 1 for line in lines)

    def mode_line_text(self):
        segments = self.mode_line_segments()
        return segments if isinstance(segments, str) else mode_line.segments_text(segments)

    def mode_line_body(self):
        """モードライン(ContentView::ModeLineFields)。欄の間は空白3つ。"""
        view = self.active_view()
        if self.vertical_selecting():  # C++のepubVerticalView_(epubSelectActive()): 名前だけCOPYに
            v = self.vertical
            position = 'Record?(y/n)' if v.asking else self.notice or v.position_text()
            return mode_line.Fields('COPY', str(self.content_byte_count()), True, '', 'EPUB 縦書き'), position
        if self.copy is not None:  # C++のcopyView_: 「COPY 状態 COPYに入る前のモードラインの名前」
            state = 'Rectangle-Mark' if self.copy.anchor is not None and self.copy.rectangle else '---'  # normalPositionText()
            saved, self.copy = self.copy, None
            try:
                under = self.mode_line_body()
            finally:
                self.copy = saved
            name = under[0].name if isinstance(under, tuple) else ''
            return mode_line.Fields('COPY', name, True), self.notice or state
        if self.prompt is not None:
            p = self.prompt
            position = mode_line.format(view.cursor_index + 1, len(view.lines)) if p.show_position else ''
            position = self.notice if p.kind in (pr.OCCUR_FIXED, pr.OCCUR_REGEX) and self.notice else position
            listed = p.candidate_list or p.kind in (pr.BOOKMARK_SET, pr.BOOKMARK_JUMP)  # 一覧の下の戻り先
            under = (self.return_stack[-1] if self.return_stack else DIRECTORY) if listed else None
            editor = p.editor
            if p.kind == pr.RESCUE_PASSWORD:  # 打った字は*で(字の数だけ)
                editor = types.SimpleNamespace(text='*' * len(editor.text), cursor=editor.cursor)
            return self.prompt_mode_line(p.label, position, localized(p.message), editor, p.kind == pr.EXIT, under)
        if self.grep_prompt is not None:
            syntax, editor = self.grep_prompt
            if syntax == FIXED:  # インクリメンタル: 「FGREP 1/11 打った字」(一致が無ければ位置は空、C++と同じ)
                position = self.grep.position_text() if self.grep.occurrences else ''
                return self.prompt_mode_line('FGREP', position, '', editor)
            return self.prompt_mode_line('EGREP', self.notice or '', '', editor)
        if self.grep_active and self.current_mode != KEY:  # KEYは一覧のモードラインのまま、位置を一致の番号に
            name, size = self.mode_line_target()
            fields = mode_line.Fields(self.grep.label(), str(size), True, '', '(' + self.grep.label().lower() + ')')
            fields.trailing3 = name
            return fields, self.notice or self.grep.position_text()
        position = self.position_text(view)
        directory = self.directory_mode.current_directory
        mode = self.current_mode
        F = mode_line.Fields
        if mode == SHELL:  # C++のshell_.scrollback()の欄
            pos = self.notice or mode_line.format_fixed_width(view.cursor_index + 1, len(view.lines))
            fields = F('SHELL', str(self.shell.shown_byte_count(self.wrap_width())), True, '', '(bash)')
            fields.trailing3 = directory  # 戻り先(C++のshellReturnName_)
            return fields, pos
        if mode == FILE:
            fm = self.file_mode
            if self.epub is not None and fm.current_file == self.epub.path:
                if self.vertical_active() and self.vertical is not None:
                    position = self.notice or self.vertical.position_text()
                    return F(self.epub_name, str(self.content_byte_count()), True, '', 'EPUB 縦書き'), position
                name = self.epub_name if self.epub_name == 'HELP' else self.epub.path  # C++はHELP以外パスのまま
                fields = F(name, str(fm.byte_count), True, '', 'EPUB', trailing2_kind=mode_line.PATH_MIDDLE)
                fields.split_layout_when_crowded = True
                return fields, position
            if fm.listing_badge and not self.pager_launch:  # (ls)の^O^Mの選べない一覧: 戻り値は(ls)(C++のpagerListingDirectory_)
                fields = F(fm.current_file, str(fm.byte_count), True, '', fm.listing_badge)
                fields.trailing3 = mode_line.truncated_trailing_path(os.path.dirname(fm.current_file).rstrip('/') + '/')
                return fields, self.notice or mode_line.format_fixed_width(*self.position_numbers(view))
            if self.pager_launch:  # ページャー(C++のpagerMode_)。組んだ物(.md)の位置はFILEと同じ形、他は5桁
                pos = self.notice or (mode_line.format if fm.transformed or fm.whole_loaded
                                      else mode_line.format_fixed_width)(
                    *self.position_numbers(view))
                return F(fm.current_file, str(fm.byte_count), False, PROGRAM_LABEL, fm.label or ''), pos
            if fm.origin is not None:  # USILICの一覧から開いたfile: 名前はパスのまま、後ろに印
                fields = F(fm.origin[0], str(fm.byte_count), True, '', fm.origin[1])
                return fields, position
            if os.path.isabs(fm.current_file):  # C++はfileのディレクトリ(FileModeの欄)
                directory = os.path.dirname(fm.current_file).rstrip('/') + '/'
            size = self.content_byte_count() if self.archive_member_shown() else fm.byte_count
            fields = F(os.path.basename(fm.current_file), str(size), True, '', fm.tail or directory,
                       trailing2_kind=mode_line.PATH_MIDDLE if fm.tail else mode_line.PATH)
            fields.split_layout_when_crowded = True
            if fm.label and not fm.tail:  # 組んだプログラム(「(groff)」等)の後ろにディレクトリ
                fields.trailing2, fields.trailing3 = fm.label, directory
            return fields, position
        if mode == ARCHIVE:
            a = self.archives[-1]
            fields = F(a.display_name, str(a.byte_count), True, '', a.badge)
            fields.trailing3 = mode_line.truncated_trailing_path(directory)
            return fields, position
        if mode == LIST and self.list_mode.label == 'USILIC' and self.list_prompt is None:
            # USILICの一覧(目次・種類・時系列のどれも、C++のunderlineView_の欄): 大きさは一覧の行、最後に(ls)のディレクトリ
            if not view.lines:
                return F(''), position
            fields = F('USILIC', str(sum(len(l.text.encode('utf-8')) + 1 for l in view.lines)), True, '')
            fields.trailing3 = directory
            return fields, position
        if mode == LIST:
            lm = self.list_mode
            item = lm.selected_item() or ''
            back = self.return_stack[-1] if self.return_stack else DIRECTORY
            if self.tab_completion is not None:  # ^I^Iの候補の一覧: 語の前の部分に続けて、右端に戻り先
                return self.prompt_line_with_text(lm.label, position, self.tab_completion[2] + item, back)
            if self.list_prompt is not None and self.list_prompt.kind in (pr.FGREP, pr.EGREP) and not lm.items:
                # 空のHISTORY(C++のlistBrowserView_の欄): 右端に戻り先
                destination = mode_line.truncated_trailing_path(self.previous_mode_name(back))
                fields = F('HISTORY', '', True, '', destination)
                fields.trailing_plain = fields.trailing2_right_justified = True
                return fields, position
            if not lm.sorted and (lm.prompt_kind is not None or self.list_prompt is not None):
                # 入力順(listBrowserChronological_): 名前はHISTORY、右端にプロンプトの名前
                label = self.list_prompt.label if self.list_prompt is not None else lm.label
                if lm.prompt_kind in (pr.FILE, pr.DIRECTORY) and self.list_prompt is None:
                    # ^X^F・^X^Dの一覧: 入力欄にカーソルの行、そのすぐ後ろに「FILE」(drawPromptLabel()のshowsBadge)
                    segments, _, _ = mode_line.prompt_segments('HISTORY', position, '', item, len(item), label,
                                                               self.display.columns, badge=True)
                    return segments
                return F('HISTORY', '', False, label), position
            if lm.prompt_kind in (pr.FILE, pr.DIRECTORY) and self.list_prompt is None and not lm.prompt_style:
                # 開いた物から戻った・^[<・^[>の後: 普通の一覧の形(項目を反転、途中を「...」、C++のRecentFileの欄)
                return F(lm.label, item, True, '', trailing_kind=mode_line.PATH_MIDDLE), position
            if lm.prompt_kind in (pr.FILE, pr.DIRECTORY) and self.list_prompt is None:
                # ^X^F・^X^D: C++はプロンプトのまま一覧を出し、入力欄にカーソルの行(drawPromptLabel())
                return self.prompt_line_with_text(lm.label, position, item, back)
            if lm.prompt_kind is not None and lm.sorted and self.list_prompt is None:
                # ^J^T等の直接の一覧(C++のpromptHistoryContext_->isDirectEntry): 項目は反転しない、右端に戻り先
                destination = self.previous_mode_name(back) if lm.prompt_kind in (pr.TOOL, pr.FILE) else ''
                trailing2 = mode_line.truncated_trailing_path(destination) if destination else ''
                fields = F(lm.label, item, bool(trailing2), '', trailing2)
                fields.trailing_plain = fields.trailing2_right_justified = True
                return fields, position
            watermark = self.list_prompt.label if self.list_prompt is not None else ''
            return F(lm.label, item, False, watermark, trailing_kind=mode_line.PATH_MIDDLE), position
        if mode == MAN:
            fields = F(self.man_mode.current_page, str(self.man_mode.byte_count), True, '', '(man)')
            fields.trailing3 = 'MAN'
            return fields, position
        if mode == INFO:  # C++はINFOをファイルとして開く(FILEの欄、ディレクトリはtrailing2)
            fields = F('INFO', str(self.info_byte_count), True, '', directory, trailing2_kind=mode_line.PATH)
            fields.split_layout_when_crowded = True
            return fields, position
        if mode in (DU, LOCATE):
            name, size, tail = {DU: ('DU', self.du_mode.byte_count, self.du_mode.current_path),
                                LOCATE: ('LOCATE', self.locate_mode.byte_count, directory)}[mode]
            fields = F(name, str(size), True)
            fields.trailing3 = tail
            return fields, position
        if mode == KEY:  # CUTの一覧(cutView_)の欄
            if self.grep_active:
                position = self.grep.position_text()
            return F('KEY', str(self.key_byte_count), True, '', 'USILIC' if self.key_colon_list_launch
                     else self.key_return_name(directory)), position
        if mode == HELP:  # 平文のHELP(FILEの欄)
            fields = F('HELP', str(self.help_byte_count), True, '', directory, trailing2_kind=mode_line.PATH)
            fields.split_layout_when_crowded = True
            return fields, position
        if mode == TOC:  # EPUBの章の一覧(CUT)
            name = 'HELP' if self.epub_name == 'HELP' else 'EPUB'
            if self.vertical_list_active():  # 縦書きの章の一覧: 位置は「選んでいる章/章の数」
                position = self.notice or '%02d/%02d' % (view.cursor_index + 1, max(1, len(view.lines)))
                if self.pager_launch and self.pager_help_return is None:  # ^Gの戻り先が無い: 透かし(C++の項目1191)
                    return F('EPUB', str(self.content_byte_count()), False, PROGRAM_LABEL, '縦書き'), position
                fields = F('EPUB', str(self.content_byte_count()), True, '', '縦書き')
            elif self.pager_launch and self.pager_help_return is None:
                # ページャーの本の章の一覧は^Gでどこへも戻らない: 戻り値は無く透かし(C++の項目1115)
                return F(name, str(self.content_byte_count()), False, PROGRAM_LABEL), position
            else:  # 大きさは本文の行のバイト数(C++のcontentByteCount(fileView_))
                fields = F(name, str(self.content_byte_count()), True)
            help_return = self.pager_help_return and (self.pager_launch or self.help_returns_to_file())
            fields.trailing3 = self.pager_help_return['path'] if help_return else directory
            return fields, position
        if mode == PART_TOC:
            part = self.part
            if part.origin == 'LOG':  # SHELLのログ(^[l): 名前の後ろにログのファイル(C++のcutView_の欄)
                return F('LOG', part.origin_path, False, ''), position
            badge = '(fgrep)' if part.syntax == FIXED else '(egrep)'
            fields = F('PART' if part.origin in ('PART', 'FILE') else part.origin, str(part.byte_count), True, '', badge)
            # FILEを分けたものはそのfileのパス、MAN・LOCATE・DUはページ等、他はディレクトリ
            fields.trailing3 = part.origin_path or part.source_name or directory
            return fields, position
        if mode == PART:  # 部分の中: 分けた元の名前(FILEはfileの名前)、大きさは部分の行(C++のcutContentView_)
            part = self.part
            name = os.path.basename(part.source_name) if part.origin in ('FILE', 'LOG') else part.origin
            size = sum(len(l.text.encode('utf-8')) + 1 + l.hidden_bytes for l in view.lines)
            return F(name or 'PART', str(size), True, '', 'PART: ' + part.pattern), position
        if mode == OCCUR:
            if self.occur_global:
                fields = F('EPUB', str(self.occur_byte_count), True, '', '(marks)')
                fields.trailing3 = self.epub_name
            else:
                label = 'FGREP' if self.occur_syntax == FIXED else 'EGREP'
                fields = F(label, str(self.occur_byte_count), True, '', '(' + label.lower() + ')')
                fields.trailing3 = self.previous_mode_name(self.occur_origin)  # 戻り先(ファイルならそのパス)
            return fields, position
        d = self.directory_mode
        if d.annotation:  # USILICの一覧から入った所: 「(ls)」の代わりに印、右端まで(C++のprimaryAnnotations_)
            return F(directory, str(d.byte_count), True, '', d.annotation), position
        fields = F(directory, str(d.byte_count), False, PROGRAM_LABEL, '(ls)')  # 右端は常に(C++のDirectoryModeの欄)
        fields.split_layout_when_crowded = True
        if d.sort_label_shown:
            fields.trailing2, fields.trailing2_plain = d.sort_key, True
        return fields, position

    def key_return_name(self, directory):
        """KEYの戻り値(C++の項目1189): ^Gの戻り先。HELPはファイルとしては見せないので「HELP」、fileならフルパス。"""
        target = self.return_stack[-1] if self.return_stack else DIRECTORY
        if target == TOC:
            return 'HELP' if self.epub_name == 'HELP' else 'EPUB'
        if target == HELP:
            return 'HELP'
        return self.previous_mode_name(FILE) if target == FILE else directory

    def prompt_mode_line(self, label, position, message, editor, exit_confirm=False, mode=None):
        """drawPromptLabel()の形のモードライン。右端は戻り先(previousModeName())。"""
        mode = self.current_mode if mode is None else mode
        destination = self.previous_mode_name(mode) if mode != SHELL or label != 'SHELL' else self.shell_directory()
        kind = 'prefixed' if mode in (MAN, LOCATE, DU) else ''
        tail = mode_line.prompt_tail(destination, kind) if destination else ''
        segments, start, shift = mode_line.prompt_segments(
            label, position, message, editor.text, editor.cursor, tail, self.display.columns,
            exit_destination=destination if exit_confirm else None)
        self.prompt_field = (start, shift)
        return segments

    def prompt_line_with_text(self, label, position, text, destination_mode):
        destination = self.previous_mode_name(destination_mode) if destination_mode != SHELL or label != 'SHELL' \
            else self.shell_directory()  # SHELLの^I^Iの一覧はSHELLのディレクトリ
        kind = 'prefixed' if destination_mode in (MAN, LOCATE, DU) else ''
        tail = mode_line.prompt_tail(destination, kind) if destination else ''
        segments, _, _ = mode_line.prompt_segments(label, position, '', text, len(text), tail, self.display.columns)
        return segments

    def shell_directory(self):
        directory = self.shell.current_directory
        return directory if directory.endswith('/') else directory + '/'

    def previous_mode_name(self, mode):
        """Application::previousModeName(): 戻り先の名前。"""
        if mode == DIRECTORY:
            return self.directory_mode.current_directory
        if mode == FILE:
            if self.epub is not None and self.epub_name == 'HELP' and self.file_mode.current_file == self.epub.path:
                return 'HELP'  # 作ったHELPの本(C++の項目1130)
            return self.file_mode.current_file
        if mode == INFO:
            return 'INFO'
        if mode == MAN:
            return 'MAN: ' + self.man_mode.current_page
        if mode == ARCHIVE:
            return self.archives[-1].current_archive if self.archives else ''
        if mode == DU:
            return 'DU: ' + self.directory_mode.current_directory
        if mode == LOCATE:
            return 'LOCATE: ' + self.locate_mode.query
        if mode == OCCUR:
            return ('FGREP' if self.occur_syntax == FIXED else 'EGREP') + ': ' + self.grep.pattern
        if mode == SHELL:
            return 'SHELL'
        if mode in (KEY, TOC, PART_TOC, PART):
            return 'PART'
        if mode == LIST and self.list_mode.label == 'USILIC':
            return 'USILIC'
        return ''

    def mode_line_target(self):
        """FGREP/EGREPのモードラインの最後の欄(探している物の名前)と大きさ。"""
        if self.current_mode == FILE:
            return self.file_mode.current_file, self.file_mode.byte_count
        if self.current_mode == SHELL:
            return 'SHELL', self.shell.shown_byte_count(self.wrap_width())
        if self.current_mode == MAN:
            return self.man_mode.current_page, self.man_mode.byte_count
        if self.current_mode == ARCHIVE:
            return self.archives[-1].display_name, self.archives[-1].byte_count
        return self.directory_mode.current_directory, self.directory_mode.byte_count

    @staticmethod
    def elide_middle(text, width):
        """表示幅widthに収まるよう、真ん中を「...」にする(頭と終わりを残す)。"""
        room = width - 3
        head_width, head = 0, []
        for ch in text:
            w = utf8.display_width_of_codepoint(ord(ch))
            if head_width + w > room // 2:
                break
            head.append(ch)
            head_width += w
        tail_width, tail = 0, []
        for ch in reversed(text):
            w = utf8.display_width_of_codepoint(ord(ch))
            if tail_width + w > room - head_width:
                break
            tail.append(ch)
            tail_width += w
        return ''.join(head) + '...' + ''.join(reversed(tail))

    def marked_rows(self):
        """下線を付ける行(選択)。"""
        if self.current_mode == DIRECTORY:
            return self.directory_mode.marks
        if self.current_mode == ARCHIVE and self.archives:
            return self.archives[-1].marks
        return ()

    def redraw(self):
        view = self.active_view()
        if self.current_mode == FILE and self.in_epub_body() and self.epub.right_to_left:
            self.update_epub_section()
        if self.current_mode == FILE and self.vertical_active():
            self.render_vertical()
            view = self.vertical_view
        elif self.copy is not None and self.vertical_list_active():
            view = self.copy.view
        elif self.vertical_list_active():
            self.render_vertical_list()
            view = self.vlist_view
        section = self.epub_section if view is self.file_view and self.in_epub_body() and \
            self.epub.right_to_left and not self.epub_note_active() else None
        offset = view.anchor_offset()
        visible = [None] * offset + view.visible_lines()  # SHELLは下に寄せる(上の空きはNone)
        marks = self.marked_rows()
        grep_on = self.grep_active or self.grep_prompt is not None
        # 縦書きの上下の余白(C++のlayoutViews()・ContentView::setMarginRows())
        margin_top = self.epub_vertical_margin_top() if view is self.vertical_view or view is self.vlist_view or (
            self.copy is not None and self.vertical_list_active()) else 0
        for row in range(margin_top):
            self.display.draw_row(row, '')
        for row in range(view.height):
            if row < len(visible) and section is not None and \
                    not section[0] <= visible[row].raw_line_number <= section[1]:
                self.display.draw_row(row + margin_top, '')  # 横書きの範囲の外の行は空に(C++のsetShownIndexRange())
            elif row < len(visible) and visible[row] is not None:
                index = view.top_index + row - offset
                line = visible[row]
                reverse = view.cursor_highlight and index == view.cursor_index and not (
                    self.prompt is not None and self.prompt.show_position and not self.prompt.candidate_list
                    and self.prompt.kind not in (pr.BOOKMARK_SET, pr.BOOKMARK_JUMP)) \
                    and self.copy is None
                runs = list(line.attributes)
                if self.copy is not None:
                    runs += self.copy.runs_for_row(index)
                text = line.text
                budget = self.display.columns - 1 if view.cursor_highlight else self.display.columns
                if line.ellipsis and len(text.encode('utf-8')) > budget:  # 途中を「...」に(ellipsisEligible)
                    text = mode_line.middle_ellipsis_by_width(text, budget)
                    line = Line(text, line.raw_line_number, highlight_start=line.highlight_start)
                if line.never_highlighted:
                    reverse = False
                if (grep_on or self.grep.persistent) and index in self.grep.matches and view is self.grep.view:
                    runs += [(s, e, 'match') for s, e in match_spans(line.text, self.grep.pattern, self.grep.syntax)]
                jump = self.jump_highlight
                if jump and jump[0] is view and jump[1] == index and jump[2] == line.text:
                    runs.append((jump[3], len(line.text), 'match'))
                underline = index in marks
                reverse = reverse and not underline  # マークした行はカーソルがあっても下線だけ(markedRowUsesUnderline_)
                if line.highlight_start is not None or line.highlight_end is not None:
                    # 反転・下線は範囲の字だけ(C++のrenderLine()、(ls)は名前から、KEYは「：」まで)
                    start = line.highlight_start or 0
                    if line.highlight_start is not None and not line.ellipsis and \
                            len(text.encode('utf-8')) > self.display.columns - 1 and start < len(text):
                        # 長い行は名前の前をそのまま、名前の途中を「...」に(truncateHighlightedSuffix())
                        text = text[:start] + mode_line.middle_ellipsis_by_width(
                            text[start:], max(0, self.display.columns - 1 - utf8.text_width(text[:start])))
                    end = len(text) if line.highlight_end is None else line.highlight_end
                    if reverse and end > start:
                        runs.append((start, end, 'reverse'))
                    if underline and end > start:
                        runs.append((start, end, 'underline'))
                    reverse = underline = False
                text, runs = hide_emphasis_marks(text, runs)
                self.display.draw_row_styled(row + margin_top, text, runs, reverse=reverse, underline=underline)
            elif row == offset and not view.lines and (view.empty_placeholder is not None or view.cursor_highlight):
                # 行が無い時の1行目: 決めた物(「pat: 見つかりません」等)か、カーソルを反転する画面では反転した「--」
                # (C++のContentViewのemptyPlaceholderLine_の既定)
                p = view.empty_placeholder
                if p is None:
                    self.display.draw_row_styled(row + margin_top, '--', [], reverse=True)
                else:
                    self.display.draw_row_styled(row + margin_top, p.text, list(p.attributes))
            else:
                self.display.draw_row(row + margin_top, '')
        if margin_top or view is self.vertical_view or view is self.vlist_view:
            for row in range(margin_top + view.height, self.file_view.height):
                self.display.draw_row(row, '')
        if self.mode_line_hidden and hasattr(self.display, 'draw_scroll_thumb'):
            thumb = scroll_thumb_row_range(view.top_index, view.height, len(view.lines))
            if thumb[1] > 0:
                self.display.draw_scroll_thumb(*thumb)
        if not self.mode_line_hidden or self.prompt is not None or self.notice:
            self.display.draw_mode_line(self.mode_line_segments())
        if (self.current_mode == SHELL and self.prompt is None and self.goto_digits is None
                and self.grep_prompt is None and not self.grep_active):
            self.display.show_cursor_at(*self.shell_cursor_position())
        elif view is self.vertical_view and self.prompt is None and self.grep_prompt is None:
            row, column = self.vertical.cursor_screen_position()  # 列カーソルの頭(C++の-cと同じ)
            self.display.show_cursor_at(row + margin_top, column)
        elif view is self.vlist_view and self.prompt is None and self.grep_prompt is None:
            self.display.show_cursor_at(margin_top, max(0, ev.screen_column(self.vlist_cursor_column() - self.vlist.first,
                                                                            self.display.columns)))
        elif self.prompt is not None or self.grep_prompt is not None or self.goto_digits is not None:
            self.display.show_cursor_at(self.display.rows - 1, self.prompt_cursor_column())
        elif self.copy is not None:
            c = self.copy
            self.display.show_cursor_at(max(0, c.row - c.view.top_index),
                                        utf8.text_width(c.text_at(c.row)[:c.col]))
        elif 0 <= view.cursor_index - view.top_index < view.height:
            # ブロックカーソルはカーソル行の頭に(C++のsyncVisibleCursor()、反転しない画面ではこれが今の行の印)
            self.display.show_cursor_at(view.cursor_index - view.top_index, 0)
        else:
            self.display.hide_cursor()
        self.display.flush()

    def prompt_cursor_column(self):
        """入力中のモードラインの、入力欄のカーソルの桁。"""
        segments = self.mode_line_segments()   # 入力欄の始まり(prompt_field)もここで決まる
        editor = self.prompt.editor if self.prompt is not None else \
            self.grep_prompt[1] if self.grep_prompt is not None else None
        if editor is not None and isinstance(segments, list):
            start, shift = self.prompt_field
            return min(self.display.columns - 1, start + utf8.text_width(editor.text[shift:editor.cursor]))
        if self.goto_digits is not None and isinstance(segments, list):  # ^[gg: 位置の欄の中
            return min(self.display.columns - 1, self.prompt_field[0] + len(self.goto_digits))
        text = self.mode_line_text()
        return max(0, min(self.display.columns - 1, utf8.text_width(text)))

    def shell_cursor_position(self):
        """コマンド行のカーソルの画面の位置(行, 桁)。^Pで戻っている間はその行の頭(moveDisplayCursorToShellHistoryLine())。"""
        view = self.shell_view
        if self.shell_browsing:
            return max(0, view.cursor_index - view.top_index + view.anchor_offset()), 0
        head = (self.shell.prompt if self.shell.prompt_ready else self.shell.pending)
        live, column = self.shell_live_text()
        if column is not None:  # 1行の欄(横にずらす)
            row = len(view.lines) - 1 - view.top_index + view.anchor_offset()
            return max(0, min(row, view.height - 1)), utf8.text_width(expand_tabs(head)) + column
        before = head + self.command_line.text[:self.command_line.cursor]
        width = self.wrap_width()
        pieces = wrap_text(expand_tabs(before), width)
        live_start = len(view.lines) - len(wrap_text(expand_tabs(head + self.command_line.text), width))
        row = live_start + len(pieces) - 1 - view.top_index + view.anchor_offset()
        return max(0, min(row, view.height - 1)), utf8.text_width(pieces[-1])

    def save_positions(self):
        """終了の時: 今の画面の位置を覚える(保存はusilic.pyが)。"""
        if self.current_mode == FILE:
            if self.epub_note_active():
                self.leave_epub_note()
            self.file_mode.save_cursor_position()
        if self.current_mode in (PART_TOC, PART):
            self.save_part_position()  # 中身を見たまま終わっても一覧の位置が残るように
        self.save_list_positions()
        self.directory_mode.save_current_cursor_position()

    def save_part_position(self):
        """C++のsaveCutFilePosition(): 一覧の位置と、中を見ていればその中の行(+1、一覧なら-1)。"""
        if self.history is None or not self.part_position_key:
            return
        line = self.part_view.cursor_index + 1 if self.current_mode == PART else -1
        self.history.set_position(hist.SCREEN, self.part_position_key, hist.CursorPosition(
            self.part_toc_view.cursor_index, len(self.part.segments), 0, line))

    def restore_part_position(self):
        """C++のrestoreCutFilePosition()・reopenLastCutContent(): 覚えた一覧の位置へ、中を見ていたら中のその行へ。"""
        pos = self.history.position_for(hist.SCREEN, self.part_position_key) \
            if self.history is not None and self.part_position_key else None
        if pos is None:
            return
        last = len(self.part.segments) - 1
        if last < 0:
            return
        self.part_toc_view.cursor_index = max(0, min(pos.row, last))
        self.center_on(self.part_toc_view, self.part_toc_view.cursor_index)
        if pos.line_number > 0 and self.part.open_selected():
            self.enter_mode(PART)
            self.center_on(self.part_view, max(0, min(pos.line_number - 1, len(self.part_view.lines) - 1)))

    def save_list_positions(self):
        """ARC・DU・MANの位置(SCREEN・MAN、C++のsaveCursorPosition())。"""
        if self.history is None:
            return
        if self.archives:
            a = self.archives[-1]
            self.history.set_position(hist.SCREEN, a.current_archive,
                                      hist.CursorPosition(a.view.cursor_index, len(a.entries), 0, -1))
        if self.du_mode.current_path:
            self.history.set_position(hist.SCREEN, self.du_mode.current_path,
                                      hist.CursorPosition(self.du_view.cursor_index, len(self.du_mode.entries), 0, -1))
        if self.man_mode.current_page:
            self.history.set_position(hist.MAN_PAGE, self.man_mode.current_page,
                                      hist.CursorPosition(self.man_view.cursor_index, len(self.man_view.lines), 0, -1))

    def restore_list_position(self, category, name, view):
        if self.history is None:
            return
        pos = self.history.position_for(category, name)
        if pos is not None:  # C++のsetLines()(頭は0)+setCursorIndex(): その行が画面の一番下に来る
            view.top_index = 0
            view.cursor_index = max(0, min(pos.row, len(view.lines) - 1))
            view.ensure_cursor_visible()

    def show_transient_notice(self, text):
        self.notice = localized(text)

    # --- モードの行き来 ---
    def enter_mode(self, mode):
        """新しいモードへ(今のモードを^Gの戻り先に積む)。同じモードなら積まない(C++と同じ)。"""
        if self.current_mode != mode:
            self.save_mode_position()
            self.return_stack.append(self.current_mode)
        self.current_mode = mode

    def save_mode_position(self):
        if self.current_mode in (PART_TOC, PART):
            self.save_part_position()
        if self.current_mode == FILE:
            self.file_mode.save_cursor_position()
        elif self.current_mode in (ARCHIVE, DU, MAN):
            self.save_list_positions()

    def go_back(self):
        """^G: 積んだ戻り先へ(無ければ(ls))。FILEから(ls)へは、そのfileのディレクトリへ移る(C++のreturnToDirectory())。"""
        self.save_mode_position()
        leaving = self.current_mode
        self.current_mode = self.return_stack.pop() if self.return_stack else DIRECTORY
        if self.current_mode == LIST and leaving in (FILE, DIRECTORY):
            self.list_mode.prompt_style = False  # 開いた物から戻った一覧は普通の一覧の形(C++と同じ)
        if leaving == FILE and self.current_mode == DIRECTORY and self.file_mode.current_file != self.keep_directory_for:
            self.move_to_file_directory(self.file_mode.current_file)
        if self.current_mode == ARCHIVE and not self.archives:
            self.current_mode = DIRECTORY
        return True

    def selected_source_path(self):
        """^J^C・^J^B・^JNの元(C++の各関数のsourcePath)。(パス, ディレクトリか)、無ければ(None, False)。"""
        mode = self.current_mode
        if mode == LIST:
            item = self.list_mode.selected_item()
            if item is None:
                return None, False
            return self.strip_tags(item), self.list_mode.label == 'DIR'
        if mode == LOCATE:
            path = self.locate_mode.selected_result()
            return path, bool(path) and os.path.isdir(path)
        if mode == DU:
            entry = self.du_mode.selected_entry()
            return (entry.path, entry.is_directory) if entry else (None, False)
        if mode in (FILE, PART_TOC, PART):
            return self.file_mode.current_file, False
        if mode == ARCHIVE and self.archives:
            return self.archives[-1].current_archive, False
        return None, False

    def enter_directory_with_floor(self, path):
        """pathの(ls)へ。前の所とは^Gで行き来(C++のenterDirectory()+resetNavigationToFloor())。"""
        previous = self.directory_mode.current_directory
        if not self.directory_mode.enter_directory(path):
            return False
        if previous:
            self.directory_mode.reset_navigation_to_floor(previous)
        self.return_stack.clear()
        self.current_mode = DIRECTORY
        return True

    def go_to_file_selector(self):
        """^J^C(Application::goToFileSelector()): (ls)なら読み直す。他は見ている物のディレクトリの(ls)へ。"""
        mode = self.current_mode
        if mode == DIRECTORY or (mode in (PART_TOC, PART) and self.part.origin != 'FILE'):
            if not self.directory_mode.enter_directory(self.directory_mode.current_directory):
                return False
            self.return_stack.clear()
            self.current_mode = DIRECTORY
            return True
        if mode == SHELL:
            return bool(self.shell.current_directory) and self.enter_directory_with_floor(self.shell.current_directory)
        if mode == LIST and self.list_mode.label != 'FILE':
            return False
        source, _ = self.selected_source_path()
        if not source or mode in (DU,):
            return False
        parent = os.path.dirname(source.rstrip('/') if source != '/' else source)
        return bool(parent) and self.enter_directory_with_floor(parent)

    def go_to_parent_from_file_or_archive(self):
        """^J^B(Application::goToParentFromFileOrArchive()): (ls)は親へ。他は見ている物の入っているディレクトリの親へ。"""
        mode = self.current_mode
        if mode == DIRECTORY or (mode in (PART_TOC, PART) and self.part.origin != 'FILE'):
            if not self.directory_mode.go_to_parent():
                return False
            self.return_stack.clear()
            self.current_mode = DIRECTORY
            return True
        source, is_directory = self.selected_source_path()
        if not source:
            return False
        containing = source.rstrip('/') if is_directory and len(source) > 1 else os.path.dirname(source)
        parent = os.path.dirname(containing)
        return bool(parent) and self.enter_directory_with_floor(parent)

    def jump_to_path_prefix(self, digit):
        """^JN(Application::jumpToPathPrefix()): 見ている物のパスのN番目の「/」までのディレクトリへ。"""
        mode = self.current_mode
        if mode == DIRECTORY or (mode in (PART_TOC, PART) and self.part.origin != 'FILE'):
            source = self.directory_mode.current_directory
        else:
            source, _ = self.selected_source_path()
        if not source:
            return False
        return self.enter_directory_with_floor(self.truncate_to_path_prefix(source, digit))

    def go_to_recent_file(self):
        """^J^J(Application::goToRecentFile()): FILEの履歴の、今見ている物以外で一番新しい項目を開く(外から起動した
        物・usilic-webのURLは除く)。"""
        current = ''
        if self.current_mode == ARCHIVE and self.archives:
            current = self.archives[-1].current_archive
            if is_epub_file(current):
                current += '/'  # ARCで開いた.epubの履歴の形
        elif self.current_mode == FILE:
            current = self.file_mode.current_file
        elif self.current_mode in (PART_TOC, PART) and self.part.origin == 'FILE' and self.part_position_key:
            current = self.part_position_key
        for item in reversed(self.ring(hist.RECENT_FILE)):
            if current and item == current:
                continue
            if item.endswith(hist.USILIC_TAG) or is_usilic_web_url(item):
                continue
            if self.open_recent_file_entry(item):
                return True
        return False

    def go_to_recent_directory(self):
        """Application::goToRecentDirectory(): 最近のディレクトリの、今の所以外で一番新しい所へ(外から起動した物は除く)。"""
        for name in reversed(self.ring(hist.RECENT_DIRECTORY)):
            if name == self.directory_mode.current_directory or name.endswith(hist.USILIC_TAG):
                continue
            if self.directory_mode.enter_directory(name):
                return True
        return False

    def move_to_file_directory(self, path):
        """(ls)を、見ていたfileのディレクトリにする(前の所とは^Gで行き来、C++のreturnToDirectory())。"""
        if not path or not os.path.isabs(path) or not os.path.isfile(path):
            return
        parent = os.path.dirname(path).rstrip('/') + '/'
        previous = self.directory_mode.current_directory
        if parent != previous and self.directory_mode.enter_directory(parent) and previous:
            self.directory_mode.reset_navigation_to_floor(previous)

    def enter_directory_listing(self, path, keep_stack=False):
        """(ls)へ(ディレクトリを選んだ時は^Gの戻り先を空にする、keep_stackならLOCATE等からの1回目)。"""
        if self.current_mode != DIRECTORY:
            self.save_mode_position()
            if keep_stack:
                self.return_stack.append(self.current_mode)
            else:
                self.return_stack.clear()
        elif not keep_stack:
            self.return_stack.clear()
        previous = self.directory_mode.current_directory
        if not self.directory_mode.enter_directory(path):
            self.show_transient_notice('Cannot list: ' + path)
            return False
        if previous and previous != self.directory_mode.current_directory:
            self.directory_mode.reset_navigation_to_floor(previous)  # C++の一覧・プロンプト等からの移動と同じ
        self.current_mode = DIRECTORY
        return True

    # --- 起動 ---
    def start_directory_listing(self, path):
        if not self.directory_mode.enter_directory(path):
            return False
        self.current_mode = DIRECTORY
        return True

    def current_resumable_name(self):
        """終わる時に見ていた物(次に引数無しで起動した時に開き直す、C++のcurrentResumableName()): FILEのファイル、
        fileを分けたPART(「パス "文字列"」)、一番外のアーカイブ(ARCで見ていた.epubは「パス/」)、他は(ls)のディレクトリ。"""
        mode = self.current_mode
        name = self.file_mode.current_file
        if mode == FILE and name and os.path.isabs(name) and os.path.isfile(name):
            return name
        if mode in (PART_TOC, PART) and self.part.source_name and os.path.isfile(self.part.source_name):
            return '%s "%s"' % (self.part.source_name, self.part.pattern)
        if mode == ARCHIVE and len(self.archives) == 1:
            path = self.archives[0].current_archive
            return path + '/' if is_epub_file(path) else path
        return self.directory_mode.current_directory

    def start_resuming_session(self, last, fallback):
        """引数無しの起動: 前回終わった時に見ていた物から始める(C++のstartResumingSession())。開けなければその所の
        (ls)。名前が無ければDIRの履歴の最新の(外から起動した物は除く)ディレクトリ。"""
        spec = self.part_spec(last) if last else None
        if spec and os.path.isfile(spec[0]):
            if self.start_directory_listing(os.path.dirname(spec[0]) or fallback):
                self.reopen_part(last)
                return True
        if last.endswith('/') and is_epub_file(last[:-1]) and os.path.isfile(last[:-1]):
            if self.start_directory_listing(os.path.dirname(last[:-1])):
                self.open_archive(last[:-1])
                return True
        if last and os.path.isfile(last):
            if self.start_directory_listing(os.path.dirname(last) or fallback):
                if is_epub_file(last):
                    self.open_epub(last)
                elif not (self.configuration.archive_for(last) and self.open_archive(last)):
                    self.open_file(last)
                return True
        if not last:
            recent = [d for d in self.ring(hist.RECENT_DIRECTORY) if not d.endswith(hist.USILIC_TAG)]
            if recent and os.path.exists(recent[-1]):
                fallback = recent[-1]
        directory = last or fallback
        while directory and not os.path.isdir(directory):  # 無くなっていれば、ある所まで上へ
            parent = os.path.dirname(directory.rstrip('/'))
            directory = parent if parent != directory else ''
        return self.start_directory_listing(directory or fallback)

    def start_pager(self, files, stdin_stream=None):
        """引数のfile(複数ならn/p/x/f)または標準入力(Application::startPagerFileList())。"""
        self.pager_launch = True
        self.current_mode = FILE
        if stdin_stream is not None:
            self.pager_files = ['(stdin)']
            return self.file_mode.load_stream(stdin_stream, '(stdin)', self.wrap_width(), self.file_view.height)
        self.pager_files = files
        self.pager_index = 0
        return self.open_pager_file(0)

    def open_pager_file(self, index):
        self.pager_index = index
        path = os.path.abspath(self.pager_files[index])
        if is_epub_file(path) and self.open_epub(path, push=False):
            return True
        if is_markdown_file(path) and self.open_markdown(path, push=False):
            return True
        if len(self.pager_files) == 1 and self.load_archive_listing(path):  # C++のstartPagerFileList()と同じく引数が1つの時
            return True
        if not self.file_mode.load_file(path, self.wrap_width(), self.file_view.height):
            return False
        self.file_mode.load_whole_if_small_file(path)
        return True

    def load_archive_listing(self, path):
        """ARCHIVEのsuffixのfileの、選べない一覧(tar -tvf等の出力そのまま): 引数で開いた時と(ls)の^O^M(C++の
        startArchivePagerLoadAsync()・finishArchiveLoadAsync())。"""
        found = self.configuration.archive_for(path)
        if not found:
            return False
        _, argv, piped, badge = found
        environment = dict(os.environ, LC_TIME='C', LC_CTYPE='en_US.UTF-8')  # C++のforceUtf8CTypeForListing
        environment.pop('LC_ALL', None)
        try:
            if piped:
                first = subprocess.Popen(piped + [path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                output = subprocess.run(argv, stdin=first.stdout, capture_output=True, env=environment).stdout
                first.stdout.close()
                first.wait()
            else:
                output = subprocess.run(argv + [path], capture_output=True, env=environment).stdout
        except OSError as error:
            self.show_transient_notice('Cannot run: ' + str(error.filename))
            return False
        if found[0] == TAR:  # 本体と組になる「._」は出さない(ARCと同じ、C++のfinishArchiveLoadAsync())
            output = remove_paired_apple_double_lines(output.decode('utf-8', 'surrogateescape')).encode(
                'utf-8', 'surrogateescape')
        self.file_mode.load_bytes(output[:self.configuration.buffer_limit_bytes()], path, self.wrap_width())
        self.file_mode.listing_badge = self.file_mode.label = badge
        self.file_mode.record_history = False
        return True

    # --- ファイルを開く ---
    def resolve_path(self, text, base=None):
        """入力のパス: ~は家、相対は(ls)のディレクトリ基準(C++のPathResolver::resolve())。"""
        text = text.strip()
        # 2026-11-23、直接のユーザー報告(^X^Dの一覧で/mediaと打つと「/home/usilic//media/が無い」): 一覧から打ち始めると、
        # 選んでいた項目(/home/usilic/)に続けて打つ。C++と同じく、途中の「//」・「~/」から後を打ち直したパスとみなす。
        # 末尾の「/~」は「~」(legacyのM_ldir・M_lfile)。
        if text.endswith('/~'):
            text = '~'
        home = os.path.expanduser('~')
        if text == '~':
            text = home
        elif text.startswith('~/'):
            text = home + text[1:]
        elif '~/' in text:
            text = home + text[text.find('~/') + 1:]
        double = text.rfind('//')
        if double >= 0:
            text = text[double + 1:]
        if not os.path.isabs(text):
            text = os.path.join(base or self.directory_mode.current_directory or os.getcwd(), text)
        return os.path.normpath(text) + ('/' if text.endswith('/') and len(text) > 1 else '')

    def open_path(self, path, keep_stack=True):
        """FILE(^Xf)・一覧から: ディレクトリ・アーカイブ・EPUB・fileのどれかとして開く。"""
        if os.path.isdir(path):
            return self.enter_directory_listing(path, keep_stack=keep_stack)
        if not os.path.exists(path):
            self.show_transient_notice("Can't open " + path)
            return False
        return self.open_file_entry(path)

    def open_file_entry(self, path, tail=None):
        """(ls)の^Mでfileを開く(EPUB・ARC・LAUNCH・FILTER・バイナリ、Application::openSelectedEntry())。"""
        if not os.access(path, os.R_OK):
            self.show_transient_notice('Permission denied')
            return False
        if is_epub_file(path):
            return self.open_epub(path)
        launch = self.configuration.launch_for(path)
        if launch:
            return self.run_external(launch + [path])
        if self.quick_look_binary(path):
            return True
        if self.configuration.archive_for(path):
            if self.open_archive(path):
                return True
        return self.open_file(path, tail=tail)

    def open_file(self, path, tail=None, streaming=False, raw=False):
        """fileをFILEで(FILTERの出力、バイナリはfile(1)の結果)。streamingは^O^M(画面の分だけ読む)。"""
        width = self.wrap_width()
        program = None if raw else self.configuration.filter_for(path)
        self.remember_previous_file()
        if program:
            try:
                result = subprocess.run(program + [path], capture_output=True)
            except OSError:
                self.show_transient_notice('Cannot run: ' + program[0])
                return False
            self.file_mode.load_bytes(result.stdout, path, width)
            self.file_mode.label = '(' + program[0] + ')'
        else:
            try:
                size = os.path.getsize(path)
                with open(path, 'rb') as f:
                    head = f.read(0x10000)
            except OSError:
                self.show_transient_notice('Cannot read: ' + path)
                return False
            offending = None if raw else self.configuration.looks_binary(head)
            if offending is not None:
                return self.show_binary_info(path, offending, tail)
            if not raw and is_markdown_file(path):
                return self.open_markdown(path, tail)
            if not self.file_mode.load_file(path, width, self.file_view.height):
                self.show_transient_notice('Cannot read: ' + path)
                return False
            if not streaming and size <= self.configuration.buffer_limit_bytes():
                self.file_mode.load_all(self.reading_tick())
            elif not streaming:
                self.show_transient_notice('Not enough buffer')
        self.file_mode.tail = tail
        self.file_mode.record_history = not (streaming or raw)
        self.file_mode.restore_from_history()
        self.enter_mode(FILE)
        return True

    def reading_tick(self):
        """読み始めて1秒経っても終わっていなければ、一度だけモードラインに「FILE 読み込み中...」(C++の
        readingAbortCheck()、読み終えて描き直すと消える)。長い読み込みの間に時々呼ぶ関数を返す。"""
        start, shown = time.monotonic(), [False]

        def tick():
            if not shown[0] and time.monotonic() - start >= 1.0:
                shown[0] = True
                message = localized('Reading...')
                used = utf8.text_width('FILE') + 1 + utf8.text_width(message)
                # drawPromptLabel()のstaticMessage: 名前は反転、文は下線、残りは先に埋めた反転のまま
                self.display.draw_mode_line([('FILE', mode_line.STANDOUT), (' ', mode_line.PLAIN),
                                             (message, mode_line.UNDERLINE),
                                             (' ' * max(0, self.display.columns - 1 - used), mode_line.STANDOUT)])
                self.display.flush()
        return tick

    def open_markdown(self, path, tail=None, push=True):
        """.md・.markdown: 文字装飾を付けて(C++のmarkdownToHelpMarkup()+expandHelpMarkup()、^[^Mは生のまま)。"""
        try:
            with open(path, 'rb') as f:
                source = f.read(self.configuration.buffer_limit_bytes()).decode('utf-8', 'replace')
        except OSError:
            self.show_transient_notice('Cannot read: ' + path)
            return False
        # ページャーではC++と同じく組んだ中身のバイト数(loadTransformedPagerFile())、他はfileの大きさ
        size = markdown_pager_byte_count(source) if self.pager_launch else os.path.getsize(path)
        self.file_mode.set_rendered(path, render_markdown(source, self.wrap_width(), self.reading_tick()), size,
                                    self.wrap_width(), renderer=lambda w: render_markdown(source, w))
        self.file_mode.transformed = self.pager_launch
        self.file_mode.tail = tail
        self.file_mode.record_history = not self.pager_launch
        self.file_mode.restore_from_history()
        if push:
            self.enter_mode(FILE)
        else:
            self.current_mode = FILE
        return True

    def show_binary_info(self, path, offending, tail=None, data=None):
        """FileMode::binaryTypeText(): 「(^@) Mach-O 64-bit executable」を太字で。"""
        try:
            if data is None:
                result = subprocess.run(['file', '-b', path], capture_output=True)
            else:
                result = subprocess.run(['file', '-b', '-'], input=data, capture_output=True)
            kind = result.stdout.decode('utf-8', 'replace').split('\n')[0]
        except OSError:
            kind = "cannot execute `file'"
        text = '(^%s) %s' % (chr(0x40 + offending), kind)
        self.file_mode.set_rendered(path, [Line(text, 1, attributes=[(0, len(text), 'bold')])], len(text) + 1,
                                    self.wrap_width())
        self.file_mode.tail = tail
        self.file_mode.record_history = False
        self.enter_mode(FILE)
        return True

    def remember_previous_file(self):
        """^J^Jの行き先(今のfile)。"""
        if self.current_mode == FILE and self.file_mode.current_file and self.epub is None:
            self.previous_file = self.file_mode.current_file

    def open_selected_entry(self):
        """(ls)の^M(Application::openSelectedEntry())。"""
        entry = self.directory_mode.selected_entry()
        if entry is None:
            return False
        if entry.name in ('./', '.'):
            return self.directory_mode.enter_directory(self.directory_mode.current_directory)
        target = os.path.normpath(os.path.join(self.directory_mode.current_directory, entry.name))
        if entry.is_directory or os.path.isdir(target):
            self.return_stack.clear()
            if not self.directory_mode.descend_to(target):  # ../も同じ(C++のdescendTo())
                self.show_transient_notice('Cannot list: ' + target)
                return False
            return True
        return self.open_file_entry(target)

    def quick_look_binary(self, path):
        """macOS: バイナリのfileで、画像・PDFか、クイックルックの扱える種類(`qlmanage -m plugins`の表にmdlsの
        kMDItemContentTypeTreeのどれかがある)なら、qlmanage -pで開く(C++のopenExternallyIfApplicable())。"""
        if not os.access('/usr/bin/qlmanage', os.X_OK) or is_epub_file(path):
            return False
        try:
            with open(path, 'rb') as f:
                head = f.read(0x10000)
        except OSError:
            return False
        if self.configuration.looks_binary(head) is None:
            return False
        try:
            mime = subprocess.run(['file', '-b', '--mime-type', path], capture_output=True, text=True).stdout
        except OSError:
            mime = ''
        mime = mime.split('\n')[0]
        if not (mime.startswith('image/') or mime == 'application/pdf'):
            supported = self.quick_look_supported_types()
            if not supported:
                return False
            try:
                tree = subprocess.run(['mdls', '-raw', '-name', 'kMDItemContentTypeTree', path],
                                      capture_output=True, text=True)
            except OSError:
                return False
            if tree.returncode != 0 or not supported & set(re.findall(r'"([^"]*)"', tree.stdout)):
                return False
        if self.history is not None:  # C++のrunQuickLook()と同じく、TOOLの履歴に「qlmanage -p 名前」
            self.history.push_recent(hist.SHELL_COMMAND, 'qlmanage -p ' + os.path.basename(path.rstrip('/')))
        return self.run_external(['qlmanage', '-p', path])

    _quick_look_types = None

    @classmethod
    def quick_look_supported_types(cls):
        """`qlmanage -m plugins`の各行(「  <UTI> -> <ジェネレーター> (...)」)のUTI(起動後に1回だけ調べる)。"""
        if cls._quick_look_types is None:
            types = set()
            try:
                out = subprocess.run(['qlmanage', '-m', 'plugins'], capture_output=True, text=True).stdout
            except OSError:
                out = ''
            for line in out.splitlines():
                if ' -> ' in line:
                    types.add(line.split(' -> ', 1)[0].strip())
            cls._quick_look_types = types
        return cls._quick_look_types

    def run_external(self, argv, cwd=None, env=None):
        """LAUNCH・TOOL・EDIT: 画面を抜けてプログラムを動かし、終わったら戻る。"""
        self.display.suspend()
        try:
            subprocess.call(argv, cwd=cwd, env=env)
        except OSError:
            pass
        finally:
            self.display.resume()
            self.layout()
        return True

    # --- ARC ---
    def open_archive(self, path, display_name=None, nested=False):
        archive = ArchiveMode(ContentView(), self.configuration)
        archive.view.set_height(self.directory_view.height)
        self.show_transient_notice('Loading archive...')
        self.redraw()
        self.notice = None
        if not archive.load_archive(path, display_name):
            return False
        if not nested:
            self.archives = []
            if self.history is not None:
                self.history.push_recent(hist.RECENT_FILE, path)
        self.archives.append(archive)
        self.restore_list_position(hist.SCREEN, path, archive.view)
        self.enter_mode(ARCHIVE)
        return True

    def open_archive_member(self):
        """ARCの^M: 中のfileを表示(中のアーカイブはさらに一覧)。"""
        archive = self.archives[-1]
        entry = archive.selected_entry()
        if entry is None or entry.is_directory:
            return False
        data = archive.extract(entry)
        if data is None:
            self.show_transient_notice("Can't open " + entry.name)
            return False
        display = archive.display_name + '/' + entry.name
        if self.configuration.archive_for(entry.name):
            suffix = os.path.basename(entry.name)
            fd, temp = tempfile.mkstemp(prefix='usilic-', suffix='-' + suffix)
            with os.fdopen(fd, 'wb') as f:
                f.write(data)
            if self.open_archive(temp, display, nested=True):
                return True
        tail = 'ARC: ' + archive.display_name
        offending = self.configuration.looks_binary(data[:0x10000])
        if offending is not None:
            return self.show_binary_info(entry.name, offending, tail, data=data)
        self.remember_previous_file()
        self.file_mode.load_bytes(data[:self.configuration.buffer_limit_bytes()], entry.name, self.wrap_width())
        self.file_mode.tail = tail
        self.file_mode.record_history = False
        self.enter_mode(FILE)
        return True

    def archive_back(self):
        """ARCの^G: 入れ子なら外のアーカイブへ、でなければ戻り先へ。"""
        if len(self.archives) > 1:
            inner = self.archives.pop()
            if os.path.basename(inner.current_archive).startswith('usilic-'):
                try:
                    os.remove(inner.current_archive)
                except OSError:
                    pass
            return True
        self.save_list_positions()
        return self.go_back()

    def start_archive_extract(self):
        """^Oa: 展開先を尋ねる(ARCでは選択か全体、(ls)ではカーソルのアーカイブ全体)。"""
        if self.current_mode == ARCHIVE:
            archive = self.archives[-1]
            path, kind = archive.current_archive, archive.kind
            members = [e.name for e in archive.target_entries()]
            names = members or [e.name for e in archive.entries]
        elif self.current_mode == DIRECTORY:
            entry = self.directory_mode.selected_entry()
            if entry is None or len(self.directory_mode.marks) > 1:
                return False
            path = os.path.join(self.directory_mode.current_directory, entry.name)
            found = self.configuration.archive_for(path)
            if not found:
                return False
            kind, members = found[0], []
            probe = ArchiveMode(ContentView(), self.configuration)
            names = [e.name for e in probe.entries] if probe.load_archive(path) else []
        else:
            return False

        def submit(text):
            destination = self.resolve_path(text) if text else self.data_directory()
            if not os.path.isdir(destination):
                self.show_transient_notice(destination + ': No such directory')
                return False
            argv = ArchiveMode.extract_argv(path, kind, members, destination)
            if argv is None:
                self.show_transient_notice('Extract not supported for this archive type')
                return False
            return self.start_shell_run(argv, destination, ArchiveMode.top_level_names(names))
        self.prompt = pr.Prompt(pr.ARCHIVE, on_submit=submit)
        return True

    # --- EPUB ---
    def open_epub(self, path, name=None, push=True):
        """.epubを本として開く(前回の位置があれば本文、無ければ最初の章、C++のopenEpubQuietly())。"""
        book = EpubBook()
        self.show_transient_notice('Reading...')
        self.redraw()
        self.notice = None
        if not book.load(path):
            self.show_transient_notice('Not a book: ' + path)
            return False
        self.remember_previous_file()
        self.epub = book
        if name is None and help_text.is_generated_help_epub(path):  # 作ったHELP(C++の項目1117)
            name = 'HELP'
        self.epub_name = name or os.path.basename(path)
        width = self.wrap_width()
        lines, self.epub_toc_lines = self.render_epub_with_margins(book, width)

        self.epub_lines, self.epub_link_stack = lines, []

        def renderer(w):
            self.epub_lines, self.epub_toc_lines = self.render_epub_with_margins(book, w)
            if not self.epub_chapter_mode:
                return self.epub_lines
            start, end = self.chapter_bounds(self.epub_chapter)
            return self.epub_lines[start:end]
        self.file_mode.set_rendered(path, lines, os.path.getsize(path), width, renderer=renderer)
        self.file_mode.record_history = name is None and not self.pager_launch
        self.toc_view.set_lines([Line(strip_emphasis_marks(title), i + 1) for i, title in enumerate(book.chapter_titles)])
        self.toc_view.cursor_index = self.toc_view.top_index = 0
        first_time = self.history is None or self.history.position_for(hist.FILE, path) is None
        self.file_mode.restore_from_history()
        if push:
            self.enter_mode(FILE)
        else:
            self.current_mode = FILE
        if self.epub_chapter_mode:
            line = self.file_view.cursor_index
            self.epub_chapter = self.chapter_of_line(line)
            start = self.apply_epub_chapter()
            self.center_on(self.file_view, line - start)
        if first_time and len(self.epub_toc_lines) > 1:
            self.go_to_chapter(0)
        self.sync_toc_cursor()
        return True

    def render_epub_with_margins(self, book, width):
        """横書きの左右の余白: 幅を狭く組み、行の頭に空白を足す(C++のFileMode::readAndWrap())。幅が20桁未満になるなら
        空けない。縦書きに組み直す時はepub_logical_lines()がこの空白を取り除く。"""
        left, right = self.epub_margin_left, self.epub_margin_right
        self.epub_margin_columns = 0
        lead_rows = self.display.rows * 2  # 行の高さが変わっても足りるように(C++のsetEpubHorizontalLeadRows())
        if width - left - right < 20:
            return book.render(width, lead_rows)
        self.epub_margin_columns = left
        lines, toc = book.render(width - left - right, lead_rows)
        if left > 0:
            for line in lines:
                if line.text:
                    line.text = ' ' * left + line.text
                    line.attributes = [(a + left, b + left, t) for a, b, t in line.attributes]
                    line.links = [(a + left, b + left, h) for a, b, h in line.links]
        return lines, toc

    def set_epub_margins(self, margins):
        """起動時: 保存した余白[上, 下, 左, 右]。"""
        self.epub_margin_top, self.epub_margin_bottom, self.epub_margin_left, self.epub_margin_right = margins

    def epub_vertical_margin_top(self):
        """縦書きの上に空ける行。列に4行残らなければ、下から先に縮める(C++のepubVerticalMarginTop())。"""
        return max(0, min(self.epub_margin_top, self.file_view.height - 4))

    def epub_vertical_margin_bottom(self):
        return max(0, min(self.epub_margin_bottom, self.file_view.height - 4 - self.epub_vertical_margin_top()))

    def epub_vertical_rows(self):
        """縦書きの列の高さ(C++のepubVerticalRows())。"""
        return max(1, self.file_view.height - self.epub_vertical_margin_top() - self.epub_vertical_margin_bottom())

    def handle_epub_margin_key(self, side, change):
        """^[+矢印(side)の後の↑(増やす)/↓(減らす)(C++のhandleEpubMarginKey())。横書きのEPUBの画面では←が左・→が右の
        桁、縦書き(本文と章の一覧)では↑が上・↓が下の行。今の向きでない側は何もしない。"""
        if change not in ('Up', 'Down'):
            return False
        delta = 1 if change == 'Up' else -1
        vertical = (self.current_mode == FILE and self.vertical_active()) or self.vertical_list_active()
        horizontal = self.current_mode == FILE and not vertical and self.in_epub_body()
        if vertical and side in ('Up', 'Down'):
            top = side == 'Up'
            shown = self.epub_vertical_margin_top() if top else self.epub_vertical_margin_bottom()
            current = self.epub_margin_top if top else self.epub_margin_bottom
            following = max(0, min(current, shown) + delta)
            if delta > 0 and self.file_view.height - self.epub_vertical_margin_top() - \
                    self.epub_vertical_margin_bottom() - 1 < 4:
                return False
            if following == current:
                return False
            if top:
                self.epub_margin_top = following
            else:
                self.epub_margin_bottom = following
        elif horizontal and side in ('Left', 'Right'):
            current = self.epub_margin_left if side == 'Left' else self.epub_margin_right
            if current + delta < 0 or (delta > 0 and self.display.columns - self.epub_margin_left -
                                       self.epub_margin_right - 1 < 20):
                return False
            if side == 'Left':
                self.epub_margin_left = current + delta
            else:
                self.epub_margin_right = current + delta
            self.file_mode.width = 0  # 幅が同じでも組み直す(C++のepubMarginReflow_)
        else:
            return False
        if self.history is not None:
            self.history.set_epub_margins([self.epub_margin_top, self.epub_margin_bottom,
                                           self.epub_margin_left, self.epub_margin_right])
        self.layout()
        return True

    def go_to_chapter(self, index):
        if not self.epub_toc_lines:
            self.show_transient_notice('No chapter')
            return False
        index = max(0, min(index, len(self.epub_toc_lines) - 1))
        self.epub_chapter = index
        offset = self.apply_epub_chapter()
        self.file_view.cursor_index = self.file_view.top_index = self.epub_toc_lines[index] - offset
        self.file_view.clamp()
        self.toc_view.cursor_index = index
        self.toc_view.ensure_cursor_visible()
        return True

    def current_chapter(self):
        """本文のカーソルがある章(目次の番号)。"""
        return self.chapter_of_line(self.global_epub_line())

    def sync_toc_cursor(self):
        if self.epub_toc_lines:
            self.toc_view.cursor_index = self.current_chapter()
            self.toc_view.ensure_cursor_visible()

    def in_epub_body(self):
        return self.current_mode == FILE and self.epub is not None and self.file_mode.current_file == self.epub.path

    # --- 一覧(履歴) ---
    def show_list(self, items, label, on_choose, sorted_view, prompt_kind=None, badge_for=None, pinned=None,
                  allow_empty=False):
        if not items and not pinned and not allow_empty:  # 頭に差し込む入口があれば、履歴が空でも開く
            self.show_transient_notice('No string')
            return False
        if self.current_mode == LIST:
            self.list_stack.append(self.list_mode.snapshot())
        else:
            self.list_stack = []
        if prompt_kind in (pr.FILE, pr.DIRECTORY) and sorted_view:
            on_choose = self.direct_list_chooser(prompt_kind, on_choose)
        self.list_mode.show(items, sorted_view, label, on_choose, prompt_kind, badge_for, pinned=pinned)
        self.enter_mode(LIST)
        return True

    def direct_list_chooser(self, prompt_kind, on_choose):
        """^X^F・^X^D等の直接の一覧から開いた物に印(C++のexecutePromptSubmission()のfromDirectEntry): fileは名前をパスの
        まま後ろに「FILE」、(ls)は「(ls)」の代わりに「DIR」(^Gで元のディレクトリへ戻ってから一覧へ)。"""
        def chosen(item):
            origin = self.directory_mode.current_directory
            if not on_choose(item):
                return False
            if prompt_kind == pr.FILE and self.current_mode == FILE and self.epub is None:
                self.file_mode.origin = (self.file_mode.current_file, 'FILE')
            elif prompt_kind == pr.DIRECTORY and self.current_mode == DIRECTORY:
                self.directory_mode.annotation = 'DIR'
                self.dir_list_origin = origin
            return True
        return chosen

    def ring(self, category):
        return self.history.recent_entries(category) if self.history is not None else []

    def file_badge(self, item):
        """fileHistoryBadges(): .epubは(epub)、アーカイブは(tar)等。"""
        if is_epub_file(item):
            return '(epub)'
        found = self.configuration.archive_for(item)
        return found[3] if found else ''

    @staticmethod
    def strip_tags(item):
        """「パス (usilic)」「パス "パターン"」の印を外したパス。"""
        if item.endswith(hist.USILIC_TAG):
            item = item[:-len(hist.USILIC_TAG)]
        if len(item) >= 4 and item.endswith('"'):
            at = item.rfind(' "', 0, len(item) - 1)
            if at >= 0:
                item = item[:at]
        return item

    def open_recent_file_entry(self, item):
        """FILEの履歴の項目を開く(C++のopenRecentFileEntry())。「パス "パターン"」はそのfileを開いてパターンで
        分けたPARTに(.epubをタブで分けた項目はEPUBとして)、他はfile・ディレクトリ等として。"""
        spec = None if item.endswith(hist.USILIC_TAG) else self.part_spec(item)
        if spec:
            if is_epub_file(spec[0]) and spec[1] == '\t':
                return self.open_epub(spec[0])
            return self.reopen_part(item)
        return self.open_path(self.strip_tags(item))

    def start_file_list(self, filter_fn=None, label='FILE', on_choose=None):
        items = [i for i in self.ring(hist.RECENT_FILE)
                 if filter_fn is None or (not i.endswith(hist.USILIC_TAG) and filter_fn(self.strip_tags(i)))]
        return self.show_list(items, label, on_choose or self.open_recent_file_entry, True,
                              pr.FILE, self.file_badge)

    def start_epub_list(self):
        """^J^E: 履歴の.epubと、usilic-web・小説サイトの入口(無ければ頭に足す、C++のaddDefaultUsilicWebUrl())。"""
        # 末尾が.epubの項目だけ(CUTの印等の付いた項目・ARCで開いた「パス/」は入れない、C++のisEpubFile(item))
        items = [i for i in self.ring(hist.RECENT_FILE)
                 if is_epub_file(i) or is_usilic_web_url(i) or is_epub_list_site_url(i)]
        return self.show_list(items, 'FILE', self.open_epub_list_item, True, pr.FILE, self.epub_list_badge,
                              pinned=(USILIC_WEB_DEFAULT_URL,) + EPUB_LIST_SITE_URLS)

    def open_epub_list_item(self, item):
        if is_epub_list_site_url(item) or is_usilic_web_url(item):
            if is_usilic_web_url(item) and self.history is not None:
                self.history.push_recent(hist.RECENT_FILE, item)
            return self.open_url(item)  # 小説サイトの入口はopen_url()がOpenで開く
        return self.open_path(self.strip_tags(item))

    def epub_list_badge(self, item):
        """小説サイトの入口は作品の番号を探しに開く所なので「(作品ID)」(C++のfileHistoryBadges())。"""
        return '(作品ID)' if is_novel_site_url(item) else self.file_badge(item)

    def start_prompt(self, kind, text='', on_submit=None, show_position=False):
        self.prompt = pr.Prompt(kind, text, on_submit, show_position)
        return True

    def prompt_handlers(self, kind):
        """プロンプトの種類ごとの^Mの処理(Application::executePromptSubmission())。"""
        return {
            pr.FILE: lambda t: self.execute_path_input(t, False),
            pr.DIRECTORY: lambda t: self.execute_path_input(t, True),
            pr.MAN: self.execute_man,
            pr.LOCATE: self.execute_locate,
            pr.EPUB: self.execute_epub_input,
        }.get(kind)

    def execute_path_input(self, text, directory):
        """^Xf・^Xdの^M(C++のexecutePathInput()): 無い物は知らせてそのまま、ディレクトリは(ls)、他は開く。"""
        path = self.resolve_path(text) if text else ''
        if not path:
            return False
        if not os.path.exists(path):
            self.show_transient_notice('No such file or directory: ' + text)
            return False
        if directory and os.path.isdir(path):
            return self.enter_directory_listing(path.rstrip('/') + '/', keep_stack=True)
        return self.open_path(path)

    def start_history_prompt(self, kind):
        return self.start_prompt(kind, '', self.prompt_handlers(kind))

    def start_list_from_prompt(self):
        """プロンプトの^R: 入力順の一覧(EGREPはusilicrcのGREPの例を先頭に)。"""
        p = self.prompt
        if p.history_category is None or p.kind in (pr.MARK_REGEX, pr.MARK_EXTENSION) or \
                p.kind in (pr.FILE, pr.DIRECTORY, pr.EPUB) and not p.candidate_list:
            return True  # ^Xf・^Xd・^Jeには履歴の一覧が無い(^X^F等の一覧から打ち始めた物にはある、C++と同じ)
        items = list(self.ring(p.history_category))
        if p.kind in (pr.EGREP, pr.PART, pr.OCCUR_REGEX, pr.MARK_REGEX):
            items = list(self.configuration.grep_examples) + items
        if not items and p.kind not in (pr.FGREP, pr.EGREP):
            self.show_transient_notice('No string')
            return True
        self.prompt = None
        self.list_prompt = p

        def choose(item):
            back = self.list_prompt
            self.list_prompt = None
            self.go_back()
            back.editor.set_text(self.strip_tags(item))
            self.restore_prompt(back)
            return True
        result = self.show_list(items, p.label, choose, False, prompt_kind=p.kind,
                                allow_empty=p.kind in (pr.FGREP, pr.EGREP))  # 空でもHISTORYの画面(C++と同じ)
        self.list_view.empty_placeholder = Line('No String') if not items else None
        return result

    def restore_prompt(self, p):
        if p.kind in (pr.FGREP, pr.EGREP):
            self.grep_prompt = (FIXED if p.kind == pr.FGREP else REGEX, p.editor)
        else:
            self.prompt = p

    # --- MAN・LOCATE・DU ---
    def execute_man(self, name):
        name = name.strip()
        if not name:
            return False
        self.show_transient_notice('Reading...')
        self.redraw()
        self.notice = None
        if not self.man_mode.load_page(name, self.wrap_width()):
            self.show_transient_notice('No manual entry for ' + name)
            return False
        if self.history is not None:
            self.history.push_recent(hist.RECENT_MAN_PAGE, name)
        self.restore_list_position(hist.MAN_PAGE, name, self.man_view)
        self.enter_mode(MAN)
        return True

    def execute_locate(self, query):
        if not query:
            return False
        if self.history is not None:
            self.history.push_recent(hist.RECENT_LOCATE_QUERY, query)
        if query != self.locate_mode.query or not self.locate_mode.results:
            self.show_transient_notice('Reading...')
            self.redraw()
            self.notice = None
            if not self.locate_mode.search(query):
                return False
        if self.current_mode == LOCATE:  # LOCATEの中で^J^L: 途中の戻り先は捨てる(C++と同じ)
            return True
        self.enter_mode(LOCATE)
        return True

    def start_locate_prompt(self):
        last = self.ring(hist.RECENT_LOCATE_QUERY)
        text = last[-1] if last else ''  # LOCATEの履歴の最後(C++のstartLocatePrompt())
        result = self.start_prompt(pr.LOCATE, text, self.execute_locate)
        if self.prompt is not None:
            self.prompt.editor.select_all()  # 前の検索語は打つと置き換わる(C++のselectAllOnSeed)
        return result

    def start_du(self, path):
        """^Jd: duを動かす(^Gで中断、それまでの結果を出す)。"""
        argv = self.du_mode.command_argv_for(path)
        output, _ = self.run_abortable(argv, 'Disk usage...')
        if output is None:
            self.show_transient_notice('Cannot run: ' + argv[0])
            return False
        self.save_list_positions()
        self.du_mode.load_usage_from_output(path, output.decode('utf-8', 'surrogateescape'))
        self.restore_list_position(hist.SCREEN, path, self.du_view)
        self.enter_mode(DU)
        return True

    def run_abortable(self, argv, label):
        """argvを動かし、出力を集める。その間に^Gを押すとやめ、そこまでの出力を返す。"""
        self.show_transient_notice(label)
        self.redraw()
        self.notice = None
        try:
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except OSError:
            return None, False
        chunks, aborted, limit = [], False, self.configuration.buffer_limit_bytes()
        while True:
            readable, _, _ = select.select([proc.stdout, self.key_input.fd], [], [], 0.2)
            if self.key_input.fd in readable:
                key = self.key_input.read_key(timeout=0)
                if key and key[0] == CTRL_G:
                    proc.kill()
                    aborted = True
            if proc.stdout in readable:
                data = os.read(proc.stdout.fileno(), 65536)
                if not data:
                    break
                chunks.append(data)
                if sum(len(c) for c in chunks) > limit:
                    proc.kill()
                    self.show_transient_notice('Not enough buffer')
            elif aborted:
                break
        proc.wait()
        return b''.join(chunks), aborted

    def du_or_locate_choose(self, path):
        """DU・LOCATEの^M: ディレクトリは(ls)(^Gで一覧へ戻れる)、fileは開く。"""
        if not path:
            return False
        if os.path.isdir(path):
            return self.enter_directory_listing(path, keep_stack=True)
        if not os.path.exists(path):
            self.show_transient_notice("Can't open " + path)
            return False
        return self.open_file_entry(path, tail=os.path.dirname(path) + '/')

    def execute_epub_input(self, text):
        """^Je: カレントディレクトリの.epubだけ(C++と同じ)。作品番号・作品のURLはnarou2epubで、他のURLは^Owで。"""
        text = text.strip()
        # Archive of Our Ownの作品・章のURLは作品のURLにそろえてnarou2epubへ(サイトのEPUBを落とす、C++と同じ)
        ao3 = re.fullmatch(r'https?://(www\.)?archiveofourown\.org/(collections/[^/\s]+/)?works/([0-9]+)(/\S*)?', text)
        if ao3:
            if self.history is not None:
                self.history.push_recent(hist.RECENT_FILE, text)
            return self.run_narou2epub('https://archiveofourown.org/works/' + ao3.group(3))
        if re.fullmatch(r'[nN][0-9]{4,}[a-zA-Z]+', text) or re.fullmatch(r'[0-9]{10,}', text) or re.match(
                r'https?://(ncode|novel18|yomou|noc)\.syosetu\.com/[nN][0-9]{4,}[a-zA-Z]+|https?://kakuyomu\.jp/works/'
                r'[0-9]+|https?://www\.aozora\.gr\.jp/cards/', text):
            if self.history is not None:
                self.history.push_recent(hist.RECENT_FILE, text)
            return self.run_narou2epub(text)
        if re.match(r'https?://', text):
            return self.open_url(text)
        path = self.resolve_path(text)
        if not is_epub_file(path) or not os.path.isfile(path):
            self.show_transient_notice('No such EPUB: ' + text)
            return False
        return self.open_epub(path)

    # --- 選択とCP/MV/RM ---
    def toggle_mark(self):
        view = self.active_view()
        marks = self.marked_rows()
        if not isinstance(marks, set):
            return False
        index = view.cursor_index
        if self.current_mode == DIRECTORY:
            entry = self.directory_mode.selected_entry()
            if entry is None or entry.name in ('./', '../', '.', '..'):
                return False
            self.directory_mode.temporary_marks = False
        if index in marks:
            marks.discard(index)
        else:
            marks.add(index)
        return True

    def mark_all(self, on):
        if self.current_mode == DIRECTORY:
            self.directory_mode.marks = set(i for i, e in enumerate(self.directory_mode.entries)
                                            if e.name not in ('./', '../')) if on else set()
            self.directory_mode.temporary_marks = False
        elif self.current_mode == ARCHIVE:
            self.archives[-1].marks = set(range(len(self.archives[-1].entries))) if on else set()
        return True

    def target_paths(self):
        d = self.directory_mode
        return [os.path.join(d.current_directory, d.entries[i].name.rstrip('/'))
                for i in sorted(d.marks) if i < len(d.entries)]

    def data_directory(self):
        """~/.usilic/data/(行き先を入れずに^Mした時、C++のdataDirectory())。"""
        path = os.path.join(os.path.expanduser('~'), '.usilic', 'data')
        os.makedirs(path, exist_ok=True)
        return path + '/'

    def previous_directory(self):
        """^W: 以前のディレクトリ(DIRの履歴で今のディレクトリより前の物)。"""
        here = self.directory_mode.current_directory
        for item in reversed(self.ring(hist.RECENT_DIRECTORY)):
            if item != here and not item.endswith(hist.USILIC_TAG):
                return item
        return ''

    def start_cp_mv_rm(self, kind):
        """^Oc・^Om・^Or(何も選択していない時は動かない、C++のstartCopyPrompt()等)。"""
        if self.current_mode != DIRECTORY or not self.directory_mode.marks:
            return False
        targets = self.target_paths()

        def submit(text):
            if kind == pr.REMOVE:
                if text != 'y':
                    return False
                if not all(self.is_writable(t) for t in targets):
                    self.show_transient_notice('Permission denied')
                    return False
                return self.start_shell_run(self.configuration.command_for('Rm') + targets, None, [])
            destination = self.resolve_path(text) if text else self.data_directory()
            if kind == pr.MOVE and not all(self.is_writable(t) for t in targets):
                self.show_transient_notice('Permission denied')
                return False
            role = 'Cp' if kind == pr.COPY else 'Mv'
            names = [os.path.basename(t) for t in targets]
            return self.start_shell_run(self.configuration.command_for(role) + targets + [destination],
                                        destination, names)
        return self.start_prompt(kind, '', submit, show_position=True)

    @staticmethod
    def is_writable(path):
        """CpMvRmMode::isWritable(): シンボリックリンク、書ける、自分の物。"""
        if os.getuid() == 0 or os.path.islink(path) or os.access(path, os.W_OK):
            return True
        try:
            return os.stat(path).st_uid == os.getuid()
        except OSError:
            return False

    def start_shell_run(self, argv, destination, names, on_done=None):
        """SHELLでargvを動かし、終わりの印で結果を見る(Application::startCpMvRmShellRun())。"""
        if not self.ensure_shell():
            return False
        self.pending_shell_run = {'destination': destination, 'names': names, 'on_done': on_done,
                                  'origin': self.directory_mode.current_directory, 'start': len(self.shell.lines)}
        self.directory_mode.marks = set()
        self.enter_mode(SHELL)
        command = ' '.join(shlex.quote(a) for a in argv) + '; echo ' + shlex.quote(CPMVRM_MARKER) + '$?'
        self.shell.submit_line(command)
        self.refresh_shell_view()
        return True

    def check_shell_run(self):
        """SHELLの出力に終わりの印が来たら、行き先の(ls)へ(成功した時だけ)。"""
        run = self.pending_shell_run
        if run is None:
            return
        for i in range(len(self.shell.lines) - 1, -1, -1):
            line = self.shell.lines[i]
            if line.startswith(CPMVRM_MARKER):
                del self.shell.lines[i]
                break
        else:
            return
        self.pending_shell_run = None
        status = line[len(CPMVRM_MARKER):].strip()
        if status != '0':
            return  # 失敗: SHELLに残る(経過とエラーが見える)
        if run['on_done'] is not None:  # narou2epub・url2pdf: 出力の最後の行(作ったファイル)
            output = [l for l in self.shell.lines[run['start'] + 1:i] if l.strip()]
            run['on_done'](output)
            return
        destination = run['destination']
        if destination is None:  # RM: 元の(ls)を読み直す
            self.return_stack.clear()
            self.directory_mode.enter_directory(run['origin'])
            self.current_mode = DIRECTORY
            return
        names = run['names']
        if not os.path.isdir(destination):  # 名前を変えた(1つのMV・CP)
            names = [os.path.basename(destination.rstrip('/'))]
            destination = os.path.dirname(destination.rstrip('/'))
        self.return_stack.clear()
        if self.directory_mode.enter_directory(destination):
            self.directory_mode.marks = set(i for i, e in enumerate(self.directory_mode.entries)
                                            if e.name.rstrip('/') in names)
            self.directory_mode.temporary_marks = True
        self.current_mode = DIRECTORY

    # --- RESCUE(^J^R) ---
    def start_rescue(self):
        """^J^R: usilicを終えて、端末の「. U」へ(端末で「. U」と打った時と同じ画面: オプションの一覧と「Option: -」)。
        2026-11-26、直接のユーザー依頼: シェル(U.sh)とファイル管理(usilic)の間には境界があり、行き来の道はU.shからは-v、
        usilicからは^J^R。^J^Rで戻った先はターミナルで起動した時と同じ画面でないと、シェルの側へ来たことにならない。
        U.shの-vから起動したusilic(USILIC_FROM_U)なら終わるだけ(U.shが一覧を出し直す)、他はusilic.pyが終えた後に
        「. U」へ切り替わる(rescue_exit)。前はFILE等と同じ一覧で選び(rescue.py)、その次はusilicのSHELLで「. U」を打っていた。"""
        if not rescue.available():
            self.show_transient_notice('RESCUE: no ' + rescue.U_SH)
            return False
        self.rescue_exit = True
        self.wants_exit = True
        return True

    # --- HELP・KEY ---
    def start_help(self):
        """^H: HELP.epubをEPUBで(無ければ平文のHELP)。"""
        path = help_text.help_epub_path(japanese())
        if self.in_epub_body() and self.epub is not None and self.epub.path == path:
            self.show_transient_notice('Already in HELP')
            return True
        if path:
            self.remember_pager_file_for_help()
        if path and self.open_epub(path, name='HELP'):
            return True
        return self.open_plain_help(None)

    def remember_pager_file_for_help(self):
        """C++のrememberPagerFileForHelp()(項目1189): ページャーのfile・本からHELPを開く前に、^Gの戻り先と位置を覚える。
        標準入力は読み直せないので覚えない。"""
        path = self.file_mode.current_file
        if not self.pager_launch:  # 通常のファイルの^H^Gはそのファイルへ(C++の項目1206)。^Hする度に決め直す
            self.pager_help_return = None
            if self.current_mode == FILE and path and os.path.isfile(path) and not is_epub_file(path) and \
                    self.file_mode.record_history and not self.file_mode.has_active_stream():
                self.pager_help_return = {'path': path, 'normal_file': True, 'stack': list(self.return_stack),
                                          'tail': self.file_mode.tail,
                                          'cursor': self.file_view.cursor_index, 'top': self.file_view.top_index}
            return
        if self.pager_help_return is not None or not path or \
                not self.pager_files or self.pager_files[0] == '(stdin)' or help_text.is_generated_help_epub(path):
            return
        book = self.epub is not None and path == self.epub.path
        self.pager_help_return = {'path': path, 'index': self.pager_index, 'book': book,
                                  'line': self.global_epub_line() if book else 0,
                                  'cursor': self.file_view.cursor_index, 'top': self.file_view.top_index}

    def help_returns_to_file(self):
        target = self.pager_help_return
        return not self.pager_launch and target is not None and target.get('normal_file') and \
            self.epub is not None and help_text.is_generated_help_epub(self.epub.path)

    def return_to_file_from_help(self):
        target, self.pager_help_return = self.pager_help_return, None
        self.file_mode.save_cursor_position()
        self.epub = None
        self.return_stack = target['stack']
        self.current_mode = FILE
        if not self.open_file(target['path'], target['tail']):
            return False
        view = self.file_view
        view.cursor_index, view.top_index = target['cursor'], target['top']
        view.clamp()
        return True

    def return_to_pager_file_from_help(self):
        """C++のreturnToPagerFileFromHelp(): HELPの章の一覧の^Gで、元のfileを開き直して元の位置へ。"""
        target, self.pager_help_return = self.pager_help_return, None
        self.file_mode.save_cursor_position()
        self.epub = None
        self.return_stack = []
        self.current_mode = FILE
        if not self.open_pager_file(target['index']):
            return False
        if target['book']:
            self.set_epub_cursor(target['line'])
            return True
        view = self.file_view
        self.file_mode.ensure_loaded_through(target['cursor'] + view.height)
        view.cursor_index, view.top_index = target['cursor'], target['top']
        view.clamp()
        return True

    def plain_help(self):
        return help_text.plain_help_lines(self.wrap_width(), japanese(), self.configuration)

    def open_plain_help(self, raw_line):
        found = self.plain_help()
        if not found:
            self.show_transient_notice('HELP not found: %s (make install)' % 'HELP.1')
            return False
        lines, self.help_byte_count = found[0], found[1]
        self.help_view.set_lines(lines)
        self.help_view.cursor_index = self.help_view.top_index = 0
        if raw_line is not None:
            self.help_view.go_to_line(raw_line)  # 画面の中ほどに(C++と同じ)
        self.enter_mode(HELP)
        return True

    def start_key(self):
        """^K: HELPの「：」の行の一覧(Application::startKey())。"""
        if self.current_mode == KEY:
            self.show_transient_notice('Already in KEY')
            return True
        found = self.plain_help()
        if not found:
            self.show_transient_notice('HELP not found: %s (make install)' % 'HELP.1')
            return False
        lines = help_text.key_source_lines(found[0])
        if self.pager_launch:  # ページャーのKEYは行頭がタブのキーだけ(C++のkeySourceLines()、-Pの子も)
            raws = found[3]
            lines = [l for l in lines if l.raw_line_number and l.raw_line_number <= len(raws) and
                     raws[l.raw_line_number - 1].startswith('\t')]
        self.key_byte_count = help_text.key_list_byte_count(lines, found[3], self.wrap_width())
        cursor = self.key_view.cursor_index
        # C++のKEY(CUTの見出し): 反転は「：」の手前まで、キーでない見出しの行は反転しない、字の装飾は無し
        # (setLabelHighlightEndMarker())。
        shown, line_start = [], True
        for line in lines:
            at = line.text.find(help_text.FULL_COLON)
            end = at if line_start and at >= 0 and help_text.is_key_assignment_label(line.text) else 0
            shown.append(Line(line.text, line.raw_line_number, line.ends_with_newline, highlight_end=end))
            line_start = line.ends_with_newline
        lines = shown
        self.key_view.set_lines(lines)
        self.key_view.cursor_index = cursor  # 前回KEYを離れた行(keyCursorIndex_)
        self.key_view.clamp()
        self.key_view.ensure_cursor_visible()
        self.enter_mode(KEY)
        return True

    def key_line_label(self):
        """カーソルのある元の行の頭(折り返しの続きにいても、その行の最初の断片)。"""
        view = self.key_view
        i = view.cursor_index
        while i > 0 and not view.lines[i - 1].ends_with_newline:
            i -= 1
        return view.lines[i] if view.lines else None

    def execute_key_line(self):
        """KEYの^M: その行のキーを、戻り先のモードで打ったことにする(executeSelectedKeyLine())。"""
        line = self.key_line_label()
        if line is None or not help_text.is_key_assignment_label(line.text):
            return False
        keys = help_text.key_bytes_from_label(line.text)
        if self.key_colon_list_launch and self.parent_pid > 0:  # -KのKEYの子: 親で動かす(C++のexecuteSelectedKeyLine())
            if not children.send(self.parent_pid, 'keys', 'K - ' + bytes(b & 0xFF for b in keys).hex() + '\n'):
                self.show_transient_notice('Parent not reachable')
            return True
        self.go_back()
        for b in keys:  # 表記はKEY節で変えたキー(rewrite_help_text())なので、利用者のキーとして読み替えを通す
            self.feed_key(b, None)
        return True

    def jump_from_key_to_help(self):
        """KEYのh: HELP.epubの同じ行へ(jumpFromKeyToHelp()・jumpHelpToKeyLine())。HELP.epubが無ければ平文のHELPの
        同じ行へ(openPlainHelp())。"""
        line = self.key_line_label()
        if line is None:
            return False
        found = self.plain_help()
        request = help_text.key_line_request(found[3], line.raw_line_number) if found else None
        if self.key_colon_list_launch and self.parent_pid > 0:  # -KのKEYの子: 親(HELPの子)へ
            if request and not children.send(self.parent_pid, 'keys', 'H ' + self.encode_help_request(*request) + '\n'):
                self.show_transient_notice('Parent not reachable')
            return True
        path = help_text.help_epub_path(japanese())
        if request and path:
            self.remember_pager_file_for_help()
        if request and path and self.open_epub(path, name='HELP'):
            return self.jump_help_to_key_line(*request)
        return self.open_plain_help(line.raw_line_number)

    def jump_help_to_key_line(self, key, nth):
        """jumpHelpToKeyLine(): 開いたHELP.epubの本文で、KEYの行と同じ行へ。行を(字下げの後ろから)強調する。"""
        index = help_text.find_key_line(self.epub_lines, key, nth)
        if index is None:
            self.show_transient_notice('Not found in HELP')
            return False
        self.set_epub_cursor(index)
        view = self.file_view
        text = view.lines[view.cursor_index].text if view.lines else ''
        start = len(text) - len(text.lstrip(' \t\u00a0'))
        self.jump_highlight = (view, view.cursor_index, text, start)
        return True

    # --- 最初の起動(C++のsetFirstStartPager()・limitedShellAllows()・offerShellCommand()) ---
    def limited_shell_key(self, byte, special):
        """最初の起動の機能限定SHELL: インストールの間は字・^H・^Mだけ、その後はコマンド行の編集・^C・^D・^G・
        ^P/^N・^M・^Js・^J.・^J,だけ。使えないキーは「ページャーでは使えません」。^[^[^Mはすぐ終わる。"""
        def refuse():
            self.show_transient_notice('Not available in pager')
            return False
        if special is not None:
            return refuse()
        if byte == ESC:
            self.limited_escapes += 1
            return refuse() if self.limited_escapes > 2 else True
        escapes, self.limited_escapes = self.limited_escapes, 0
        if escapes >= 2 and byte == CR:
            self.wants_exit = True
            return True
        if escapes:
            return refuse()
        printable = byte >= 0x20
        if self.shell_offer is not None:  # インストールの案内が続いている間
            self.limited_leader_j = False
            if printable or byte in (0x08, CR):
                return self.feed_shell_byte(byte)
            return refuse()
        if self.limited_leader_j:
            self.limited_leader_j = False
            number = {ord('s'): 69, ord('.'): 71, ord(','): 72}.get(byte)
            return self.handle_key_number(number, None) if number else refuse()
        if byte == CTRL_J:
            self.limited_leader_j = True
            return True
        if printable or byte in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, CTRL_G, 0x08, 0x0B, CR, 0x0E, 0x10, 0x15, 0x17,
                                 0x19, 0x1F):
            return self.feed_shell_byte(byte)
        return refuse()

    def offer_shell_command(self, command, follow_up=None, retries=2):
        """SHELLへ切り替え、`command`を入力欄に置く(プロンプトが出たら実行する)。続きがあれば、プロンプトが戻るたびに
        follow_up()の行を実行する(最大retries回、C++のoffer_shell_command()・pollShellOfferFollowUp())。"""
        if not self.ensure_shell():
            return False
        if self.current_mode != SHELL:
            self.enter_mode(SHELL)
        self.command_line.set_text(command)
        self.shell_offer = {'follow_up': follow_up, 'offered': command, 'generation': self.shell.prompt_generation,
                            'retries': retries} if follow_up else None
        if self.shell.prompt_ready:  # もうプロンプトが出ていれば(起動を待ち合わせ済み)、そのまま実行する
            self.feed_shell_byte(CR)
        self.refresh_shell_view()
        return True

    def poll_shell_offer(self):
        offer = self.shell_offer
        if offer is None:
            return False
        if not self.shell.alive():
            self.shell_offer = None
            return False
        if self.shell.prompt_generation == offer['generation']:
            return False
        offer['generation'] = self.shell.prompt_generation
        if self.command_line.text == offer['offered']:  # 置いた行は^Mを待たずに、プロンプトが出たら実行する
            self.feed_shell_byte(CR)
            return True
        following = offer['follow_up']()
        if not following or offer['retries'] <= 0:
            self.shell_offer = None
            if self.first_start_pager and not following and self.current_mode == SHELL:
                self.go_back()  # 最初の起動: 全部入れ終えたらHELPへ戻る(C++のfinishFirstStartInstall())
            return True
        offer['retries'] -= 1
        offer['offered'] = following
        self.command_line.set_text(following)
        self.feed_shell_byte(CR)
        return True

    # --- 子のウィンドウ(^T^H・^T^K・-H・-K、C++のlaunchHelpChild()・launchKeyChild()・pollChildChannel()) ---
    @staticmethod
    def encode_help_request(key, nth):
        return '%d:%s' % (nth, key.encode('utf-8').hex())

    @staticmethod
    def decode_help_request(text):
        nth, _, key = text.partition(':')
        try:
            return bytes.fromhex(key).decode('utf-8', 'replace'), int(nth)
        except ValueError:
            return None

    def setup_children(self, parent_pid, window_slot):
        """外部起動の子なら親のディレクトリに自分のFIFO(HELPの子は"help")、親なら"keys"を開く。"""
        self.parent_pid = parent_pid
        if parent_pid > 1:
            self.channel.open(parent_pid, 'help' if window_slot == 1 else 'w.%d' % os.getpid())
            self.channel.register_as_child(parent_pid)
        else:
            self.channel.open(os.getpid(), 'keys')

    def launch_external_usilic(self, args, directory=None, slot=0, cascade=0, extra_env=None):
        """子のusilic(C++のlaunchExternalUsilic()): 端末の新しいウィンドウで、開けなければ画面を明け渡して前面で。"""
        owner = self.parent_pid if self.parent_pid > 0 else os.getpid()
        env = {'USILIC_TRANSIENT': str(owner)}
        if slot:
            env['USILIC_WINDOW_SLOT'] = str(slot)
        env.update(extra_env or {})
        command = ' '.join('%s=%s' % (k, shlex.quote(v)) for k, v in env.items()) + ' ' + \
            ' '.join(shlex.quote(a) for a in [sys.executable, USILIC_SCRIPT] + list(args))
        marker = children.open_in_new_terminal_window(directory, 'unset USILIC USILIC_HANDOFF; ' + command, slot,
                                                      cascade)
        if marker:
            self.external_markers.append(marker)
            self.show_transient_notice('Opened in a new window')
            return True
        return self.run_external([sys.executable, USILIC_SCRIPT] + list(args), cwd=directory or None,
                                 env=dict(os.environ, **env))

    def help_child_target(self):
        path = help_text.help_epub_path(japanese())
        if path:
            return path
        found = self.plain_help()
        return found[2] if found else None

    def launch_help_child(self, jump=None):
        """^T^H・-H: HELP(作ったHELP.epub、無ければ平文のHELP)を子のウィンドウで。"""
        target = self.help_child_target()
        if not target:
            self.show_transient_notice('HELP not found: %s (make install)' % 'HELP.1')
            return False
        return self.launch_external_usilic([target], slot=1,
                                           extra_env={'USILIC_HELP_JUMP': jump} if jump else None)

    def launch_key_child(self):
        """^T^K・-K: KEYを子のウィンドウで(ページャーの親なら-P、ページャーで使えるキーだけ)。"""
        return self.launch_external_usilic(['-K', '-P'] if self.pager_launch else ['-K'], slot=2)

    def forward_help_request(self, request):
        """KEYの子のh: HELPの子へ回す。いなければHELPの子を開き直し、起動したらその行へ(C++のforwardHelpRequest())。"""
        if children.send(os.getpid(), 'help', 'H ' + request + '\n'):
            return True
        return self.launch_help_child(jump=request)

    def jump_help_request(self, request):
        """HELPの子: 送られた行へ(開いているHELPの本の中で)。"""
        decoded = self.decode_help_request(request)
        if decoded is None:
            return False
        path = help_text.help_epub_path(japanese())
        if path and not self.in_epub_body() and not self.open_epub(path, name='HELP'):
            return False
        return self.jump_help_to_key_line(*decoded)

    def execute_remote_keys(self, keys):
        """KEYの子の^M: そのキーを今の画面で(利用者のキーとして、C++のexecuteRemoteKeys())。"""
        for b in keys:
            self.feed_key(b, None)
        return True

    def poll_children(self):
        """親がいなくなれば終わる。受け渡しのFIFOの行を処理する。新しいウィンドウの子が終われば知らせを消す。"""
        if self.parent_pid > 1 and children.parent_gone(self.parent_pid):
            self.wants_exit = True
            return True
        if self.help_in_parent_pending:  # 最初の起動: 親自身がHELPをページャーとして開く
            self.help_in_parent_pending = False
            self.start_help()
            self.redraw()
        if self.pending_shell_offer is not None:
            command, follow_up, retries, on_offered = self.pending_shell_offer
            self.pending_shell_offer = None
            if self.offer_shell_command(command, follow_up, retries) and on_offered:
                on_offered()
            return True
        if any(self.children_pending):
            help_child, key_child = self.children_pending
            self.children_pending = (False, False)
            if help_child:
                self.launch_help_child()
            if key_child:
                self.launch_key_child()
            return True
        if self.help_jump_on_start is not None:
            request, self.help_jump_on_start = self.help_jump_on_start, None
            self.jump_help_request(request)
            return True
        handled = False
        for line in self.channel.read_lines():
            if line.startswith('K ') and len(line) > 4:
                try:
                    keys = list(bytes.fromhex(line[4:]))
                except ValueError:
                    keys = []
                if keys:
                    self.execute_remote_keys(keys)
                handled = True
            elif line.startswith('H '):
                if self.channel.is_help():
                    self.jump_help_request(line[2:])
                else:
                    self.forward_help_request(line[2:])
                handled = True
        if self.external_markers:
            left = [m for m in self.external_markers if os.path.exists(m)]
            if len(left) != len(self.external_markers) and self.notice == localized('Opened in a new window'):
                self.notice = None
                handled = True
            self.external_markers = left
        return handled

    # --- キー ---
    def pending_prefix_label(self):
        """Application::pendingPrefixLabel()(1秒経ったらモードラインへ)。"""
        if not self.pending_keys:
            return ''
        return prefix_label(self.pending_keys)

    def feed_key(self, byte, special):
        """1キー(Application::feedByte()→dispatchByte())。マクロの記録中は打ったバイトを残す。"""
        if isinstance(special, MouseEvent):
            return self.handle_mouse(special)
        if isinstance(special, Paste):
            return self.handle_paste(special.text)
        # ^[や辺の矢印の後の矢印はEPUBの余白: 慣性スクロールに取られない(C++の項目1145)
        awaiting_margin = self.epub_margin_side is not None or self.pending_keys == [ESC] or \
            self.translate_pending[0] == [ESC]
        if special in ('Up', 'Down', 'Left', 'Right') and awaiting_margin:
            self.coast = self.drag = None
            flushed, self.translate_pending[0] = self.translate_pending[0], []
            for b in flushed:
                self.feed_canonical(b, None)
            return self.feed_canonical(byte, special)
        if self.coast is not None and special in ('Up', 'Down', 'Left', 'Right'):
            return self.adjust_coast_by_arrow(special)  # 慣性中の矢印: 速さ・向き
        # EPUBの慣性スクロール中の;/:は、章を移ってから同じ段階・向きの慣性を続ける(C++の項目1083)
        keep_coast = self.coast if (self.coast is not None and special is None and byte in (ord(';'), ord(':'))
                                    and self.current_mode == FILE and self.epub is not None) else None
        self.coast = self.drag = None  # キーで慣性・ドラッグは終わる
        if keep_coast is not None:
            result = self.feed_key(byte, special)
            if self.current_mode == FILE and self.epub is not None:
                keep_coast['due'] = time.monotonic()
                keep_coast['columns'] = self.vertical_scroll_active()
                self.coast = keep_coast
            return result
        self.click_targets_cursor = False
        if self.macro_repeat_armed:  # ^Xeの直後のe(K-131)は、記録したマクロをもう一度(MACRO節のeより先に)
            self.macro_repeat_armed = False
            if special is None and byte == self.bindings.macro_repeat_key:
                self.notice = None
                return self.play_recorded_macro()
        if special in ('Up', 'Down', 'Left', 'Right') and self.start_coast_by_arrow(special):
            return True
        # KEY節で変えたキーを既定のキー列へ読み替えてから本体へ(C++のfeedByteUngated())。UTF-8の字の続きは読み替えない。
        if special is not None:
            flushed, self.translate_pending[0] = self.translate_pending[0], []
            for b in flushed:
                self.feed_canonical(b, None)
            return self.feed_canonical(byte, special)
        if self.utf8_skip > 0 or byte >= 0xC0:
            self.utf8_skip = self.utf8_skip - 1 if self.utf8_skip > 0 else (1 if byte < 0xE0 else 2 if byte < 0xF0 else 3)
            return self.feed_canonical(byte, None)
        out = self.bindings.feed(CTRL_SPACE if byte == 0 else byte, self.key_context(), self.translate_pending)
        result = True
        for b in out:
            result = self.feed_canonical(0 if b == CTRL_SPACE else b, None)
        return result

    def key_context(self):
        """読み替えの場面(C++のkeyBindingContext())。"""
        from . import key_bindings as kb
        if self.pending_keys or self.goto_digits is not None or self.register_state is not None:
            return kb.CONTEXT_NONE
        if self.copy is not None or self.vertical_selecting():
            return kb.CONTEXT_COPY
        if self.prompt is not None or self.grep_prompt is not None or self.current_mode == SHELL:
            return kb.CONTEXT_PROMPT
        if self.current_mode == LIST and self.list_mode.prompt_kind:
            return kb.LEADER_ONLY
        return kb.ALL

    def feed_canonical(self, byte, special):
        """既定のキー列の1キー(読み替えた後、マクロの再生・KEYの^Mから)。"""
        # ^]は、どの画面・プロンプトでも^J^C(カレントディレクトリへ)と同じ(C++の項目1113)。^X等の続きの1打は除く。
        if special is None and byte == 0x1D and not self.pending_keys and self.goto_digits is None \
                and self.register_state is None and not self.limited_leader_j:
            self.feed_canonical(CTRL_J, None)
            return self.feed_canonical(0x03, None)
        if self.macro_recording is not None and not self.playing and special is None:
            self.macro_recording.append(byte)
        self.notice = None
        if self.vertical_selecting():
            return self.feed_vertical_copy_key(byte, special)
        if self.copy is not None:
            return self.feed_copy_key(byte, special)
        if self.prompt is not None:
            return self.feed_prompt_byte(byte, special)
        if self.grep_prompt is not None:
            return self.feed_grep_prompt_byte(byte)
        if self.grep_active and special is None:
            # ^N/^P: 次/前の一致。^Sの検索はEmacsと同じく^S/^Rも(C++の項目1194)
            if byte in (0x0E, 0x10) or (byte in (0x13, 0x12) and self.grep.syntax == FIXED):
                self.grep.move(1 if byte in (0x0E, 0x13) else -1)
                self.show_current_match()
                return True
            if byte == 0x12:  # ^J^Sの確定の後の^R: 履歴を開かない(^U^Rで履歴、C++のegrepReverseKey())
                self.show_transient_notice('^R: only in ^S (FGREP)')
                return True
            if byte == CTRL_G:  # そこに留まる(^Gも今いる一致のまま、C++版と同じ)
                self.grep_active = False
                return True
            self.grep_active = False  # 他のキー(^Mも)は、一致の間を移るのをやめてから(KEYの^Mはその行のキー)
            self.redraw()  # やめたことをすぐ描く(C++のdeactivateGrepInPlace()、後の処理が長いfileの読み込みでも)
        if self.goto_digits is not None:
            if special is None and ord('0') <= byte <= ord('9'):
                self.goto_digits += chr(byte)
                return True
            digits, self.goto_digits = self.goto_digits, None
            if byte == CR and digits:  # ^X^Xの戻り先は変えない(C++版と同じ)
                if self.current_mode == FILE:
                    self.file_mode.ensure_loaded_through(int(digits) + self.file_view.height)
                self.active_view().go_to_line(int(digits))
            return True
        if self.first_start_pager and self.current_mode == SHELL and not self.pending_keys:
            return self.limited_shell_key(byte, special)
        if self.epub_margin_side is not None:  # ^[+矢印の後の↑/↓、他のキーは取り消し(C++のdispatchByte())
            side, self.epub_margin_side = self.epub_margin_side, None
            return special is not None and self.handle_epub_margin_key(side, special)
        if special is not None:
            arrow = str(special).replace('Shift', '')
            if arrow in ('Up', 'Down', 'Left', 'Right') and (self.pending_keys == [ESC] or
                                                             (str(special).startswith('Shift') and not self.pending_keys)):
                self.pending_keys = []
                self.epub_margin_side = arrow
                return True
            self.pending_keys = []
            return self.handle_special(special)
        if self.current_mode == SHELL and (self.utf8_pending or byte >= 0x80):
            return self.feed_shell_byte(byte)  # UTF-8の字(続きのバイトの0x80は^SPCではない)
        if byte >= 0x80:
            return False
        if byte == 0:
            byte = CTRL_SPACE  # ^SPCはNUL(C++のKeyInputと同じく0x80に)
        if self.register_state is not None:
            return self.feed_register_byte(byte)
        if not self.pending_keys:
            if self.current_mode == SHELL and not self.starts_binding_in_text_mode(byte):
                return self.feed_shell_byte(byte)
            if self.current_mode == LIST and self.list_mode.prompt_kind and byte >= 0x20 and byte != 0x7F \
                    and byte != CTRL_SPACE and not self.starts_binding_in_text_mode(byte):
                return self.type_into_list_prompt(byte)
            if self.feed_count_byte(byte):
                return True
            if self.repeating_z and byte == ord('z'):  # ^Xzの後のz
                return self.repeat_last_command()
            self.repeating_z = False
            if self.current_mode == DIRECTORY and byte in (ord('%'), ord('*')):
                return self.start_mark_pattern(byte == ord('*'))
            macro = self.macro_for_byte(byte)
            if macro is not None:
                return self.run_macro(macro)
        elif self.current_mode == SHELL and self.word_edit_escape(self.command_line, byte):
            self.refresh_shell_view()
            return True
        return self.feed_key_sequence(byte)

    def word_edit_escape(self, editor, byte):
        """ESC f/b/d/DEL(M-f/M-b/M-d/M-DEL、C++のapplyWordEditEscape())。割り当ての照合より先に。"""
        if self.pending_keys != [ESC] or byte not in (ord('f'), ord('b'), ord('d'), 0x7F, 0x08):
            return False
        self.pending_keys = []
        {ord('f'): editor.move_word_right, ord('b'): editor.move_word_left,
         ord('d'): editor.kill_word_forward}.get(byte, editor.kill_word_backward)()
        return True

    def feed_prompt_leader_byte(self, editor, byte):
        """プロンプトの中の前置キー(^X・^J・ESCで始まる割り当て、C++のfeedPromptByteInner()がdispatchLeaderByte()へ)。
        続きを待つ間はプロンプトのまま、割り当てに当たればプロンプトをやめてその機能。Noneは前置キーではない。"""
        if self.word_edit_escape(editor, byte):
            return True
        if self.prompt_swallow_byte:
            self.prompt_swallow_byte = False
            return True
        if not self.pending_keys and ([byte] == self.bindings.prefixes[2][:1] or not self.starts_binding_in_text_mode(byte)):
            return None  # ^O(^O^Y)はfeed_prompt_byte()の中で
        prompt, grep_prompt = self.prompt, self.grep_prompt
        self.prompt = self.grep_prompt = None
        result = self.feed_key_sequence(byte)
        if self.register_state is not None:  # ^Xr…(レジスタ・ブックマーク)はプロンプトの中では使わない(C++と同じ、次の字も捨てる)
            self.register_state = None
            self.prompt_swallow_byte = True
            self.sequence_state = 'none'
        if self.sequence_state != 'run':  # 続きを待つ・割り当て無し: プロンプトのまま
            self.prompt, self.grep_prompt = prompt, grep_prompt
            return True
        return result

    def feed_key_sequence(self, byte):
        """pending_keysへ足して割り当てと照合する(sequence_stateに'more'・'none'・'run')。"""
        self.sequence_state = 'run'
        # 縦書きの本では;(前の章)と:(次の章)を入れ替える(C++のfeedByteUngated()、^[</^[>と同じく逆)
        if not self.pending_keys and byte in (ord(';'), ord(':')) and self.prompt is None and \
                self.current_mode == FILE and self.in_epub_body() and self.epub.right_to_left:
            byte = ord(':') if byte == ord(';') else ord(';')
        self.pending_keys.append(byte)
        keys = self.pending_keys
        x_prefix, o_prefix = self.bindings.prefixes[0], self.bindings.prefixes[2]
        if x_prefix and keys == x_prefix + [ord('r')]:  # ^Xr…(レジスタ・ブックマーク)
            self.pending_keys = []
            self.register_state = 'r'
            return True
        if x_prefix and keys == x_prefix + [ord('z')]:  # ^Xz
            self.pending_keys = []
            return self.repeat_last_command()
        if o_prefix and keys in (o_prefix + [0x13], o_prefix + [CTRL_J, 0x13]):  # ^O^S・^O^J^S(occur)
            self.pending_keys = []
            return self.start_occur_prompt(FIXED if len(keys) == len(o_prefix) + 1 else REGEX)
        if o_prefix and keys == o_prefix + [CTRL_J]:
            self.sequence_state = 'more'
            return True
        if keys == [ESC, ord('g')]:  # ^[gg(EmacsのM-g g): 行番号を打つ(^[Nと同じ)
            self.sequence_state = 'more'
            return True
        if keys == [ESC, ord('g'), ord('g')]:
            self.pending_keys = []
            self.goto_digits = ''
            return True
        state, number = self.bindings.match(self.pending_keys)
        if state in ('more', 'none'):
            self.sequence_state = state
        if state == 'more':
            return True
        keys, self.pending_keys = self.pending_keys, []
        if state == 'none':
            self.count = None
            return False
        digit = keys[-1] if self.bindings.sequence_for(number)[-1] == DIGIT else None
        # KEYのh(K-105、C++の項目1189)はページャーでも使える
        if self.pager_launch and number not in self.pager_key_numbers() and \
                not (number == 105 and self.current_mode == KEY):
            self.show_transient_notice('Not available in pager')
            return False
        return self.run_command(number, digit)

    def pager_key_numbers(self):
        """ページャーで使えるK-番号(C++のpagerAllowedKeys()): HELPの行頭がタブの「キー：説明」の行のキー(「（^Xrl も
        同じ）」のキーも)と、^M・^K・^Og・^Oj・^[[ ^[] ^[{ ^[}・^J^F・^Ot・^T^H・^T^K・^H。最初の起動(first_start_pager)
        では機能限定SHELLへ行く^[s・^Js・^J.・^J,と^I(INFO)も。"""
        key = (japanese(), self.first_start_pager)
        if self.pager_numbers_cache is not None and self.pager_numbers_cache[0] == key:
            return self.pager_numbers_cache[1]
        numbers = set(PAGER_KEYS) | {28, 93, 92, 109, 110, 112, 113, 129, 130}
        numbers |= {n for n, seq in self.bindings.sequences.items()
                    if seq in ([0x0F, ord('g')], [0x0F, ord('t')], [0x0A, 0x06], [0x08])}  # ^HもC++の項目1185
        found = self.plain_help()
        for raw in (found[3] if found else []):
            if not raw.startswith('\t') or help_text.FULL_COLON not in raw:
                continue
            labels = [raw]
            same = raw.find('も同じ')
            opening = raw.rfind('（', 0, same) if same >= 0 else -1
            if opening >= 0:
                labels.append(raw[opening + 1:same])
            for label in labels:
                keys = help_text.key_bytes_from_label(label)
                pending, translated = [[], None], []
                for b in keys:
                    translated += self.bindings.feed(b, ALL_CONTEXT, pending)
                state, number = self.bindings.match(translated or keys)
                if state == 'done':
                    numbers.add(number)
        if self.first_start_pager:
            numbers |= {68, 69, 71, 72, 82}
        self.pager_numbers_cache = (key, numbers)
        return numbers

    def run_command(self, number, digit):
        """K-番号の機能を^Uの回数だけ(移動等)。^Xzのために覚える。"""
        count = self.count or 1
        self.count = self.count_digits = None
        if number not in (97, 98):
            self.macro_just_played = False
        if number == 36 and count > 1 and self.current_mode not in (DIRECTORY, ARCHIVE):
            return self.pop_mark()  # ^U^␣: マークを遡る
        self.last_command = (number, digit, count)
        result = False
        for _ in range(count if number in REPEATABLE else 1):
            result = self.handle_key_number(number, digit)
        return result

    def repeat_last_command(self):
        """^Xz: 直前のコマンドを、回数も含めてもう一度(続けてzで繰り返し)。"""
        if self.last_command is None:
            self.show_transient_notice('No command to repeat')
            return False
        number, digit, count = self.last_command
        self.repeating_z = True
        self.count = count
        result = self.run_command(number, digit)
        self.repeating_z = True
        return result

    def feed_count_byte(self, byte):
        """^U: 次のコマンドを4回(^U^Uで16回、^U数字でその回数)。"""
        if byte == 0x15:
            self.count = 4 if self.count is None else self.count * 4
            self.count_digits = ''
            self.notice = 'C-u %d' % self.count
            return True
        if self.count_digits is not None and ord('0') <= byte <= ord('9'):
            self.count_digits += chr(byte)
            self.count = int(self.count_digits)
            self.notice = 'C-u %d' % self.count
            return True
        return False

    def starts_binding_in_text_mode(self, byte):
        """SHELL・字を打つ一覧で、割り当ての方へ回すキー(F-0/F-1/F-2/F-4かESCで始まる物、HELPのKEYの章)。"""
        heads = [p[0] for i, p in enumerate(self.bindings.prefixes) if p and i != 3]
        return byte == ESC or byte in heads

    def type_into_list_prompt(self, byte):
        """字を打つ一覧: 選んでいる項目に続けて打つプロンプト(C++のfeedRegexListByte()・feedTabCompletionListByte())。
        一覧は画面に残り、^N・^Pで候補を移ると入力欄がその項目に(candidate_list)。^Mで元のモードへ戻ってから実行。"""
        kind = self.list_mode.prompt_kind
        back, self.list_prompt = self.list_prompt, None
        tab, self.tab_completion = self.tab_completion, None
        selected = self.list_mode.selected_item()
        if tab is not None:  # ^I^Iの候補の一覧: 語の前の部分に続けて
            p, partial, prefix = tab
            seed = prefix + (selected if selected is not None else partial)
            if p is None:
                p = pr.Prompt(pr.SHELL_COMMAND, '', self.submit_shell_command)
        elif kind == pr.TOOL and back is None:
            return True  # ^J^Tの一覧では打った字は使わない(C++と同じ)
        else:
            p = back if back is not None else pr.Prompt(kind, '', self.prompt_handlers(kind))
            seed = self.strip_tags(selected or '')
        p.editor.set_text(seed)
        p.editor.clear_undo()
        if kind in (pr.FGREP, pr.EGREP):  # ^J^S等: 元のモードのプロンプトに
            self.go_back()
            self.restore_prompt(p)
            return self.feed_grep_prompt_byte(byte)
        p.candidate_list = p.show_position = True
        self.prompt = p
        return self.feed_prompt_byte(byte, None)

    def submit_shell_command(self, text):
        """SHELLの^I^Iの一覧から打ち始めたプロンプトの^M: その行をSHELLで動かす(C++のPromptKind::ShellCommand)。"""
        self.command_line.set_text(text)
        return self.feed_shell_byte(CR)

    def handle_special(self, special):
        """矢印・PageUp/PageDown(Application::resolveCommand()の特別なキー)。"""
        if self.current_mode == SHELL:
            if special in ('Left', 'Right'):
                (self.command_line.move_left if special == 'Left' else self.command_line.move_right)()
                self.refresh_shell_view()
                return True
        number = {'Up': 4, 'Left': 4, 'Down': 3, 'Right': 3, 'PageUp': 8, 'PageDown': 5}[special]
        if ((self.current_mode == FILE and self.vertical_active()) or self.vertical_list_active()) and \
                special in ('Left', 'Right'):
            number = 3 if special == 'Left' else 4  # 縦書きは←で先(左)へ
        return self.handle_key_number(number, None)

    def handle_key_number(self, number, digit):
        """K-番号の機能(Application::handleCommand()の、この版で動く分)。"""
        mode = self.current_mode
        view = self.active_view()
        self.last_key_number = number
        if self.vertical_list_active():
            result = self.vertical_list_number(number)
            if result is not None:
                return result
        if mode == FILE and self.vertical_active():
            result = self.vertical_number(number)
            if result is not None:
                return result
        extra = self.handle_stage4_number(number, digit, mode, view)
        if extra is not None:
            return extra
        if number in (3, 4) and mode == OCCUR and view.lines and \
                view.cursor_index == (len(view.lines) - 1 if number == 3 else 0):
            view.cursor_index = 0 if number == 3 else len(view.lines) - 1  # 一致の一覧の^N/^Pは巡回(;・:も)
            view.ensure_cursor_visible()
            return True
        if number in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12):
            return self.move(number, view)
        if number == 13:  # ^[N
            self.goto_digits = chr(digit)
            return True
        if number == 14:
            return self.exchange_point(view)
        if number in (15, 16, 17, 18):
            return self.pager_switch(number)
        if number == 19:
            if self.pager_launch:
                self.wants_exit = True
            return True
        if number == 20:
            return self.back()
        if number == 21:  # ^L: 再表示(一時的な選択は消す)
            if mode == DIRECTORY and self.directory_mode.temporary_marks:
                self.directory_mode.marks = set()
            self.display.screen.clear()
            return True
        if number == 22:
            return self.suspend()
        if number in (24, 25):  # ^J^C・^]
            if self.pager_launch:
                self.show_transient_notice('Not available in pager')
                return False
            return self.go_to_file_selector()
        if number == 26:  # ^J^B
            if self.pager_launch:
                self.show_transient_notice('Not available in pager')
                return False
            return self.go_to_parent_from_file_or_archive()
        if number == 27:  # ^JN: N番目の「/」までのディレクトリ(truncateToPathPrefix())
            return self.jump_to_path_prefix(digit - ord('0'))
        if number == 28:
            return self.enter_key()
        if number in (29, 30):  # ^O^M(1行ずつ)・^[^M(そのまま)
            if mode == DIRECTORY:
                entry = self.directory_mode.selected_entry()
                if entry and not entry.is_directory:
                    path = os.path.join(self.directory_mode.current_directory, entry.name)
                    if number == 29 and self.load_archive_listing(path):  # ^O^M: アーカイブは選べない一覧(C++と同じ)
                        self.enter_mode(FILE)
                        return True
                    return self.open_file(path, streaming=number == 29, raw=True)
            return False
        if number == 31:  # ^J^J
            return self.go_to_recent_file()
        if number in (35, 36):
            return self.toggle_mark()
        if number in (37, 38):
            return self.mark_all(number == 37)
        if number == 40:
            return self.start_cp_mv_rm(pr.COPY)
        if number == 41:
            return self.start_cp_mv_rm(pr.MOVE)
        if number == 42:
            return self.start_cp_mv_rm(pr.REMOVE)
        if number == 46:
            return self.start_archive_extract()
        if number == 47:  # ^Je
            return self.start_prompt(pr.EPUB, '', self.execute_epub_input)
        if number == 48:  # ^J^E
            return self.start_epub_list()
        if number == 49:  # ^Oe: .epubをARCとして
            if mode == DIRECTORY:
                entry = self.directory_mode.selected_entry()
                if entry and is_epub_file(entry.name):
                    return self.open_archive(os.path.join(self.directory_mode.current_directory, entry.name))
            return False
        if number == 50:  # ^R
            return self.toggle_history_view()
        if number == 51:
            return self.start_grep_prompt(FIXED)
        if number == 52:
            return self.start_grep_prompt(REGEX)
        if number == 55:  # ^Jd
            path = self.directory_mode.current_directory
            if mode == DU:
                entry = self.du_mode.selected_entry()
                if entry and entry.is_directory:
                    path = entry.path
            return self.start_du(path)
        if number == 56:  # ^J^D
            if not self.du_mode.current_path:
                return self.start_du(self.directory_mode.current_directory)
            self.enter_mode(DU)
            return True
        if number == 57:  # ^Jl
            if self.locate_mode.start_update_db():
                self.show_transient_notice('updatedb')
            return True
        if number == 156:  # ^J^R: RESCUE(rescue.py)
            return self.start_rescue()
        if number == 58:  # ^J^L
            return self.start_locate_prompt()
        if number == 59:  # ^Jm
            return self.start_history_prompt(pr.MAN)
        if number == 60:  # ^J^M: 名前順で重ねの無い一覧、usilicが無ければ頭に(C++のstartManList()、直接の一覧)
            items = self.ring(hist.RECENT_MAN_PAGE)
            return self.show_list(items, 'MAN', self.execute_man, True, pr.MAN,
                                  pinned=None if 'usilic' in items else ['usilic'])
        if number == 61:  # ^Xf
            return self.start_history_prompt(pr.FILE)
        if number == 62:  # ^X^F
            return self.start_file_list()
        if number == 63:  # ^Xd
            return self.start_history_prompt(pr.DIRECTORY)
        if number == 64:  # ^X^D
            items = self.ring(hist.RECENT_DIRECTORY)
            return self.show_list(items, 'DIR', lambda i: self.enter_directory_listing(self.strip_tags(i),
                                                                                         keep_stack=True),
                                  True, pr.DIRECTORY)
        if number == 68:
            return self.start_shell()
        if number == 69:  # ^Js: SHELLを再起動
            self.end_shell_session()  # C++のshellRestart(): 前のセッションをログへ
            self.shell.terminate()
            self.shell = Shell()
            if not self.ensure_shell():  # SHELLの中からでも新しいbashで(start_shell()はSHELLなら何もしない)
                return False
            self.enter_mode(SHELL)
            self.refresh_shell_view()
            return True
        if number in (90, 91):
            return self.start_help()
        if number in (93, 94):
            return self.start_key()
        if number in (99, 100):
            return self.start_prompt(pr.EXIT, 'y', self.execute_exit)  # C++版と同じく「y」を入れて尋ねる
        if number == 103:  # ^J^A
            return self.start_file_list(lambda p: bool(self.configuration.archive_for(p)) and not is_epub_file(p),
                                        'FILE', lambda i: self.open_archive(self.strip_tags(i)) or
                                        self.open_path(self.strip_tags(i)))
        if number == 105:  # h(KEYからHELPへ)
            return self.jump_from_key_to_help() if mode == KEY else False
        if number == 129:  # ^T^H: HELPを子のウィンドウで(-Hと同じ)
            return self.launch_help_child()
        if number == 130:  # ^T^K: KEYを子のウィンドウで(-Kと同じ)
            return self.launch_key_child()
        return False

    def move(self, number, view):
        """移動(K-1〜K-12)。ページャーは先に要る所まで読む(ensurePagerStreamLoaded())。"""
        if self.current_mode == LIST and number in (11, 12):
            self.list_mode.prompt_style = False  # ^[<・^[>はプロンプトの形の一覧を普通の一覧の形に(C++と同じ)
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            if number == 12:
                self.file_mode.load_all()
            else:
                self.file_mode.ensure_loaded_through(max(view.top_index, view.cursor_index) + 2 * view.height)
        if number in (5, 6, 7, 8, 9, 10, 11, 12):
            self.push_mark(view)  # ページ送り・先頭・末尾の前の行にマーク(HELPの^X^X)
        {
            1: view.scroll_line_down, 2: view.scroll_line_up, 3: view.move_down, 4: view.move_up,
            5: view.page_down, 6: view.page_down, 7: view.page_down, 8: view.page_up, 9: view.page_up,
            10: view.page_up, 11: view.go_to_top, 12: view.go_to_bottom,
        }[number]()
        return True

    def push_mark(self, view, index=None):
        """マーク(最大16個、同じ画面の物だけを使う)。"""
        index = view.cursor_index if index is None else index
        if self.mark_ring and self.mark_ring[-1] == (view, index):
            return
        self.mark_ring.append((view, index))
        del self.mark_ring[:-16]

    def marks_for(self, view):
        return [i for v, i in self.mark_ring if v is view]

    def center_on(self, view, index):
        view.cursor_index = index
        view.clamp()
        view.top_index = view.cursor_index - view.height // 2  # ^[Nと同じく中ほどへ
        view.clamp()

    def exchange_point(self, view):
        """^X^X: 最新のマークと今の行を入れ替える。"""
        for i in range(len(self.mark_ring) - 1, -1, -1):
            v, index = self.mark_ring[i]
            if v is view:
                self.mark_ring[i] = (view, view.cursor_index)
                self.center_on(view, index)
                return True
        self.show_transient_notice('No mark set')
        return False

    def pop_mark(self):
        """^U^␣: 新しいマークから順に遡って移る(遡った物は一番古い所へ回す)。"""
        if self.current_mode == FILE and self.vertical_active():  # 縦書きの記録は1つ(C++と同じ)
            self.sync_vertical()
            if not self.vertical.pop_mark():
                self.show_transient_notice('No mark set')
                return False
            self.vertical_to_file()
            return True
        view = self.active_view()
        for i in range(len(self.mark_ring) - 1, -1, -1):
            v, index = self.mark_ring[i]
            if v is view:
                del self.mark_ring[i]
                self.mark_ring.insert(0, (view, index))
                self.center_on(view, index)
                return True
        self.show_transient_notice('No mark set')
        return False

    def pager_switch(self, number):
        if not self.pager_launch:
            return False
        last = len(self.pager_files) - 1
        index = {15: min(self.pager_index + 1, last), 16: max(self.pager_index - 1, 0), 17: 0, 18: last}[number]
        if index != self.pager_index and self.pager_files[0] != '(stdin)':
            self.pager_help_return = None  # HELPから別のfileへ移ったら、戻り先はもう無い
            self.open_pager_file(index)
        return True

    @staticmethod
    def truncate_to_path_prefix(path, digit):
        """truncateToPathPrefix(): 先頭の「/」を0番目として、digit番目の「/」まで。"""
        remaining, pos = digit + 1, 0
        while remaining > 0:
            slash = path.find('/', pos)
            if slash < 0:
                return path
            pos = slash + 1
            remaining -= 1
        return path[:pos]

    def back(self):
        """^G(モードごと)。"""
        mode = self.current_mode
        if mode == KEY and self.key_colon_list_launch:  # -KのKEYの子は戻り先を持たない(C++と同じ)
            return False
        if mode == DIRECTORY:
            if self.directory_mode.annotation == 'DIR' and self.return_stack and self.return_stack[-1] == LIST:
                self.directory_mode.annotation = ''  # ^X^Dの一覧から入った所: 元のディレクトリへ戻ってから一覧へ
                if self.dir_list_origin:
                    self.directory_mode.enter_directory(self.dir_list_origin)
            if self.return_stack:
                return self.go_back()
            if not self.directory_mode.go_back():
                self.go_to_recent_directory()  # 戻る所が無ければ最近のディレクトリへ(C++のgoToRecentDirectory())
            return True
        if mode == FILE:
            if self.pager_launch and not self.in_epub_body():
                return True
            if self.in_epub_body():  # 本文の^Gは目次へ
                self.file_mode.save_cursor_position()
                self.sync_toc_cursor()
                self.current_mode = TOC
                return True
            return self.go_back()
        if mode == TOC:
            if self.first_start_pager and self.shell_started and self.shell.alive():
                self.enter_mode(SHELL)  # 最初の起動: インストールの後は機能限定SHELLへ(C++のreturnFromCut())
                self.refresh_shell_view()
                return True
            if self.help_returns_to_file():
                return self.return_to_file_from_help()
            if self.pager_launch and self.pager_help_return is not None:
                return self.return_to_pager_file_from_help()
            if self.pager_launch:  # ページャーの章の一覧の^Gはどこへも戻らない(C++のreturnFromCut())
                return False
            return self.go_back()
        if mode == ARCHIVE:
            return self.archive_back()
        if mode == OCCUR and not self.occur_global:  # 一致の一覧から元の画面へ: 飛んだ時の「FGREP: 語」の印と一致の色は外す
            self.grep.persistent = False
            label = self.previous_mode_name(OCCUR)
            if self.file_mode.origin is not None and self.file_mode.origin[1] == label:
                self.file_mode.origin = None
            if self.directory_mode.annotation == label:
                self.directory_mode.annotation = ''
        if mode == LIST and self.list_stack:  # 一覧の中から開いた一覧: 前の一覧へ
            self.list_mode.restore(self.list_stack.pop())
            return True
        if mode == LIST and self.tab_completion is not None:  # ^I^Iの候補の一覧: 元のままプロンプトへ
            return self.return_from_tab_completion(None)
        if mode == LIST and self.list_prompt is not None:  # プロンプトの^Rの一覧: プロンプトへ戻る
            back, self.list_prompt = self.list_prompt, None
            self.go_back()
            self.restore_prompt(back)
            return True
        return self.go_back()

    def enter_key(self):
        """^M(モードごと)。"""
        mode = self.current_mode
        if mode == DIRECTORY:
            return self.open_selected_entry()
        if mode == ARCHIVE:
            return self.open_archive_member()
        if mode == LIST:
            item = self.list_mode.selected_item()
            if item is None:
                return False
            return self.list_mode.on_choose(item)
        if mode == TOC:
            index = self.toc_view.cursor_index
            self.current_mode = FILE
            return self.go_to_chapter(index)
        if mode == DU:
            entry = self.du_mode.selected_entry()
            return self.du_or_locate_choose(entry.path if entry else None)
        if mode == LOCATE:
            return self.du_or_locate_choose(self.locate_mode.selected_result())
        if mode == KEY:
            return self.execute_key_line()
        if mode == PART_TOC:
            if not self.part.open_selected():
                return False
            self.enter_mode(PART)
            return True
        if mode == OCCUR:
            return self.occur_jump()
        if mode == INFO:
            return self.info_open()
        return False

    def toggle_history_view(self):
        """^R(ToggleHistoryView): (ls)は^X^D、FILE・ARCは^X^Fの一覧、一覧では並びを切り換え。"""
        mode = self.current_mode
        if mode == LIST:
            self.list_mode.toggle_sorted()
            return True
        # C++のstartRegexPromptList(): (ls)は^X^D、FILE・ARCは^X^Fと同じ一覧(並べ替えた順、^Rで入力順に)
        if mode == DIRECTORY:
            return self.handle_key_number(64, None)
        if mode in (FILE, ARCHIVE):
            return self.start_file_list()
        return False

    def suspend(self):
        """^Z: サスペンド(端末を戻してから止まる)。"""
        self.display.suspend(clear=False)
        os.kill(os.getpid(), signal.SIGSTOP)
        self.display.resume()
        self.layout()
        return True

    # --- プロンプト ---
    def feed_prompt_byte(self, byte, special):
        """モードラインの入力欄(LineEditor)。^Mで実行、^Gでやめる、^Rで履歴、^Iで補完、^Wで以前のディレクトリ。"""
        p = self.prompt
        editor = p.editor
        if byte != 0x09:
            self.tab_armed = False
        if special is not None:
            if special in ('Left', 'Right'):
                (editor.move_left if special == 'Left' else editor.move_right)()
            return True
        if self.prompt_ctrl_o:  # ^O^Y: クリップボードの内容を入れる
            self.prompt_ctrl_o = False
            if byte == 0x19:
                editor.insert(self.clipboard_contents().replace('\n', ' '))
            return True
        if byte == CTRL_O:
            self.prompt_ctrl_o = True
            return True
        if p.kind == pr.EXIT and byte == ESC and not self.pending_keys:  # 終了の確認は^[でもやめる(C++と同じ)
            self.prompt = None
            return True
        leader = self.feed_prompt_leader_byte(editor, byte)
        if leader is not None:
            return leader
        if p.kind == pr.MACRO_EDIT and byte in (0x0E, 0x10, CTRL_G, CR):
            if byte == CTRL_G:
                return self.close_macro_list()
            if byte == CR:
                self.prompt = None
                return self.execute_macro_edit(editor.text)
            if self.macro_list[1]:  # 記録を書いた直後は「--」の行だけ
                return True
            (self.list_view.move_down if byte == 0x0E else self.list_view.move_up)()
            editor.set_text(self.list_mode.selected_item() or '')
            editor.move_home()
            return True
        if (p.candidate_list or p.kind in (pr.BOOKMARK_SET, pr.BOOKMARK_JUMP)) and byte in (0x0E, 0x10):  # 一覧を選ぶ
            (self.list_view.move_down if byte == 0x0E else self.list_view.move_up)()
            selected = self.list_mode.selected_item() or ''
            editor.set_text(selected if not p.candidate_list else self.strip_tags(selected))
            return True
        if byte == CR:
            self.prompt = None
            text = editor.text
            if p.candidate_list:
                self.go_back()
            return p.on_submit(text) if p.on_submit else True
        if byte == CTRL_G:
            self.prompt = None
            if p.candidate_list:
                self.go_back()
            if p.kind in (pr.BOOKMARK_SET, pr.BOOKMARK_JUMP):
                self.bookmark_place = None
                self.go_back()
            return True
        if byte == 0x12 and editor.text and p.kind in (pr.OCCUR_FIXED, pr.OCCUR_REGEX):  # ^Uで消してから(C++のfeedPromptByteInner())
            self.show_transient_notice('^R: only in ^S (FGREP)')
            return True
        if byte == 0x12:  # ^R
            return self.start_list_from_prompt()
        if byte == 0x09 and p.kind in TAB_COMPLETION_KINDS:
            return self.complete_by_tab(p)
        if byte == 0x17 and p.kind in (pr.COPY, pr.MOVE, pr.ARCHIVE):  # ^W
            editor.insert(self.previous_directory())
            return True
        return self.edit_line(editor, byte)

    def complete_by_tab(self, p):
        """^I(K-65): 候補の共通の頭まで補完(C++のfeedPromptByteInner()・Shell::onTab())。候補が幾つもあればBELで、続けて
        ^Iで候補の一覧。1つに決まったfileはFILE・DIR・ARC等ではその場で確定する(ディレクトリはその下を補完できるように
        確定しない)。TOOL・MAN・SHELLは最後の語だけを、まだコマンド名を打っている間は$PATHから。pはNoneでSHELL。"""
        kind = p.kind if p is not None else None
        editor = p.editor if p is not None else self.command_line
        text, cursor = editor.text, editor.cursor
        word_start = 0
        partial = text
        if kind in (pr.TOOL, pr.MAN, None):
            word_start = max(text.rfind(' ', 0, cursor), text.rfind('\t', 0, cursor)) + 1
            partial = text[word_start:cursor]
        if kind in (pr.TOOL, pr.MAN, None) and pr.should_search_path(text):
            candidates = pr.path_program_candidates(partial)
        else:
            base = (self.shell.current_directory if kind is None else self.directory_mode.current_directory) \
                or os.getcwd()
            candidates = pr.complete_filesystem_path(partial, base, kind in (pr.DIRECTORY, pr.ARCHIVE))
            if kind in (pr.FILE, pr.EPUB):  # fileを開く所ではディレクトリを候補にしない
                candidates = [c for c in candidates if not c.endswith('/')]
            if kind == pr.EPUB:  # ^Jeはカレントディレクトリの.epubだけ
                candidates = [c for c in candidates if '/' not in c and is_epub_file(c)]
        if not candidates:
            self.tab_armed = False
            return True
        common = os.path.commonprefix(candidates)
        ambiguous = len(candidates) > 1
        if ambiguous and self.tab_armed:
            self.tab_armed = False
            return self.show_tab_completion_list(p, candidates, partial, text[:word_start])
        if len(common) > len(partial):
            editor.insert(common[len(partial):])
        self.tab_armed = ambiguous
        if not ambiguous and not common.endswith('/') and kind in (pr.FILE, pr.DIRECTORY, pr.EPUB, pr.ARCHIVE):
            return self.feed_prompt_byte(CR, None)
        return True

    def show_tab_completion_list(self, p, candidates, partial, prefix):
        """^I^Iの候補の一覧(C++のshowTabCompletionList())。^Mで選んだ物を入れる(FILE・DIR・ARCはそのまま確定)、^Gで元の
        まま戻る、字を打つと選んでいる物に続けて打つ。"""
        self.prompt = None
        self.tab_completion = (p, partial, prefix)
        label = p.label if p is not None else 'SHELL'
        return self.show_list(candidates, label, lambda item: self.return_from_tab_completion(item), True,
                              prompt_kind=p.kind if p is not None else pr.TOOL)

    def return_from_tab_completion(self, item, byte=None):
        """一覧から戻る(itemがNoneなら^G、byteは一覧で打った字、C++のreturnFromTabCompletionList()・
        feedTabCompletionListByte())。"""
        p, partial, prefix = self.tab_completion
        self.tab_completion = None
        self.go_back()
        full = prefix + (item if item is not None else partial)
        if p is None:
            self.command_line.set_text(full)
            if byte is not None:
                return self.feed_shell_byte(byte)
            self.refresh_shell_view()
            return True
        if item is not None and byte is None and p.kind in (pr.FILE, pr.DIRECTORY, pr.EPUB, pr.ARCHIVE):
            return p.on_submit(full) if p.on_submit else True
        p.editor.set_text(full)
        self.restore_prompt(p)
        return self.feed_prompt_byte(byte, None) if byte is not None else True

    def edit_line(self, editor, byte):
        """コマンド行の編集(^A ^E ^B ^F ^H ^D ^K ^U ^Y ^_、UTF-8の字)。"""
        if byte == 0x01:
            editor.move_home()
        elif byte == 0x05:
            editor.move_end()
        elif byte == 0x02:
            editor.move_left()
        elif byte == 0x06:
            editor.move_right()
        elif byte in (0x08, 0x7F):
            editor.backspace()
        elif byte == 0x04:
            editor.delete_forward()
        elif byte == 0x0B:
            editor.kill_to_end()
        elif byte == 0x15:
            editor.kill_to_start()
        elif byte == 0x19:
            editor.yank()
        elif byte == 0x1F:  # ^_: 取り消し
            editor.undo()
        elif byte >= 0xC0 or (self.utf8_pending and byte >= 0x80):
            if byte >= 0xC0:
                self.utf8_pending.clear()
            self.utf8_pending.append(byte)
            if len(self.utf8_pending) >= utf8.sequence_length(self.utf8_pending[0]):
                editor.insert(bytes(self.utf8_pending).decode('utf-8', 'replace'), self_insert=True)
                self.utf8_pending.clear()
        elif 0x20 <= byte < 0x7F:
            editor.insert(chr(byte), self_insert=True)
        else:
            return False
        return True

    def execute_exit(self, answer):
        if answer.strip().lower() == 'y':
            self.wants_exit = True
        return True

    # --- FGREP/EGREP ---
    def start_grep_prompt(self, syntax):
        """^S(FGREP、打つたびに探す)・^J^S(EGREP)のプロンプト(Application::startGrepPrompt())。"""
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.load_all()  # ページャーは残りを全部読んでから(docs/CHANGES.md項目1009)
        self.grep_active = False
        self.grep.matches, self.grep.occurrences, self.grep.current, self.grep.pattern = [], [], -1, ''
        self.grep.persistent = False
        self.jump_highlight = None  # KEYのhの強調も(clearGrepJumpHighlight())
        self.grep.view = self.active_view()
        self.grep.origin = self.active_view().cursor_index
        self.push_mark(self.active_view())  # ^Sの開始にもマーク(HELPの^X^X)
        self.grep_prompt = (syntax, LineEditor(pr.PROMPT_KILL))
        return True

    def run_grep(self, syntax, pattern):
        view = self.active_view()
        ok = self.grep.search([line.text for line in view.lines], pattern, syntax, self.grep.origin)
        if ok and self.grep.matches:
            self.show_current_match()
        return ok

    def show_current_match(self):
        """今の一致の行へ、画面の中ほどに(C++版と同じ)。"""
        line = self.grep.current_line()
        if line is None:
            return
        view = self.active_view()
        view.cursor_index = line
        view.top_index = line - view.height // 2
        view.clamp()

    def feed_grep_prompt_byte(self, byte):
        syntax, editor = self.grep_prompt
        leader = self.feed_prompt_leader_byte(editor, byte)
        if leader is not None:
            if syntax == FIXED and self.grep_prompt is not None:
                self.run_grep(FIXED, editor.text)
            return leader
        if byte == CTRL_G:  # やめる。カーソルは今の一致の行に残る(C++と同じ、元の位置へは戻らない)
            self.grep_prompt = None
            return True
        if byte == CR:
            self.grep_prompt = None
            pattern = editor.text
            if not pattern:
                return True
            if self.history is not None:
                self.history.push_recent(hist.RECENT_FGREP_PATTERN if syntax == FIXED else hist.RECENT_EGREP_PATTERN,
                                         pattern)
            # ^Sは打っている間に探し終えていて、^N・^S等で動いた今の一致のまま確定する(C++と同じ)
            already = syntax == FIXED and self.grep.pattern == pattern and self.grep.matches
            if not already and (not self.run_grep(syntax, pattern) or not self.grep.matches):
                self.show_transient_notice('No string')
                return True
            self.grep_active = True
            return True
        if byte in (0x13, 0x12) and syntax == FIXED and (byte == 0x13 or editor.text):
            # 打っている間の^S: 次の一致、^R: 前(何か入っていれば履歴を開かない、^Uで消してから^Rで履歴、C++の項目1194)
            if self.grep.matches:
                self.grep.move(1 if byte == 0x13 else -1)
                self.show_current_match()
            return True
        if byte == 0x12 and editor.text:  # 文字がある間は履歴を開かない(^Uで消してから^R、C++のfeedPromptByte())
            self.show_transient_notice('^R: only in ^S (FGREP)')
            return True
        if byte == 0x12:  # ^R: 入力順の一覧(EGREPはusilicrcのGREPの例を先頭に)
            self.grep_prompt = None
            self.prompt = pr.Prompt(pr.FGREP if syntax == FIXED else pr.EGREP)
            self.prompt.editor = editor
            result = self.start_list_from_prompt()
            if self.prompt is not None:  # 一覧が空だった
                self.prompt = None
                self.grep_prompt = (syntax, editor)
            return result
        if byte == 0x13:  # ^J^Sの打っている間の^Sは何もしない
            return True
        if byte in (0x0E, 0x10) and syntax == FIXED:  # 打っている間の^N: 次の一致へ、^P: 前へ(巡回)
            self.grep.move(-1 if byte == 0x10 else 1)
            self.show_current_match()
            return True
        if not self.edit_line(editor, byte):
            return True
        if syntax == FIXED:
            self.run_grep(FIXED, editor.text)
        return True

    # --- SHELL ---
    def ensure_shell(self):
        if self.shell.alive():
            return True
        directory = self.directory_mode.current_directory or os.getcwd()
        if not self.shell.start(directory, self.display.rows - 1, self.display.columns):
            self.shell.terminate()
            self.show_transient_notice('Cannot start SHELL')
            return False
        self.shell_started = True
        return True

    def start_shell(self):
        """^[s: SHELLへ(初めてならbashを起動、Command::StartShell)。"""
        if self.current_mode == SHELL:
            return True
        if not self.ensure_shell():
            return False
        self.enter_mode(SHELL)
        self.refresh_shell_view()
        return True

    def feed_shell_byte(self, byte):
        """Application::feedShellByte(): コマンド行の編集、^Mで送る、^Cは中断、^Gで戻る。
        ^P・^Nは確定した行を1行ずつ動く(C++のhistoryBrowsing)。プロンプトの行で^Mならそのコマンドをもう一度。"""
        editor = self.command_line
        if byte != 0x09:
            self.tab_armed = False
        view = self.shell_view
        last = len(view.lines) - self.live_row_count() - 1  # 確定した最後の行
        if byte == CTRL_G:
            if self.shell_browsing:
                self.exit_shell_browsing()
                return True
            return self.go_back()
        if byte == 0x10:  # ^P
            if not self.shell_browsing:
                if last < 0:
                    return True
                self.shell_browsing = True
                view.cursor_index = last
            else:
                view.cursor_index = max(0, view.cursor_index - 1)
            view.ensure_cursor_visible()
            return True
        if byte == 0x0E:  # ^N
            if self.shell_browsing:
                if view.cursor_index >= last:
                    self.exit_shell_browsing()
                else:
                    view.cursor_index += 1
                    view.ensure_cursor_visible()
            return True
        if self.shell_browsing and byte == CR:
            command = view.lines[view.cursor_index].shell_command
            if command is None:
                self.exit_shell_browsing()
                return True
            editor.set_text(command)
            self.shell_browsing = False
        elif self.shell_browsing and byte != 0x03:
            self.exit_shell_browsing()
        if byte == CR:
            command = editor.text
            editor.set_text('')
            self.shell.submit_line(command)
            if command.strip() and self.history is not None:
                self.history.push_recent(hist.SHELL_COMMAND, command)
        elif byte == 0x03:   # ^C
            editor.set_text('')
            self.shell.send_control_byte(0x03)
        elif byte == 0x04 and not editor.text:   # 空の行の^DはEOF
            self.shell.send_control_byte(0x04)
        elif byte == 0x12:   # ^R: SHELLのコマンドの履歴(入力順)
            items = self.ring(hist.SHELL_COMMAND)
            if items:
                def choose(item):
                    self.go_back()
                    self.command_line.set_text(item)
                    self.refresh_shell_view()
                    return True
                return self.show_list(items, 'SHELL', choose, False)
        elif byte == 0x0C:
            pass  # 再表示
        elif byte == 0x09:   # ^I: 最後の語を補完
            result = self.complete_by_tab(None)
            if self.current_mode != SHELL:
                return result
        else:
            self.edit_line(editor, byte)
        self.refresh_shell_view()
        return True

    # --- 第4段: K-番号の追加分 ---
    def handle_stage4_number(self, number, digit, mode, view):
        """第4段で足した機能。該当しなければNone(handle_key_number()の続きへ)。"""
        if number == 5 and mode == DIRECTORY and sys.platform == 'darwin' and self.quick_look():
            return True  # (ls)の␣はクイックルック。対象が無ければ(./ ../)ページ送り(C++と同じ)
        if number == 36 and mode not in (DIRECTORY, ARCHIVE):  # ^␣: マークを置く
            self.push_mark(view)
            self.show_transient_notice('Mark set')
            return True
        if number == 23:  # ^Of
            self.mode_line_hidden = not self.mode_line_hidden
            if hasattr(self.display, 'strip'):  # 隠している間は右端の2桁をつまみに(C++のmodeLineVisible_)
                self.display.strip = 2 if self.mode_line_hidden else 0
            self.layout()
            return True
        if number == 32 and mode == DIRECTORY:
            return self.directory_mode.toggle_filename_only()
        if number == 33 and mode == DIRECTORY:
            return self.directory_mode.cycle_sort_key()
        if number == 34:
            if mode == DIRECTORY:
                return self.directory_mode.toggle_reverse_sort()
            self.display.screen.clear()
            return True
        if number == 43:
            return self.start_tool_prompt()
        if number == 44:
            items = self.ring(hist.SHELL_COMMAND)
            return self.show_list(items, 'TOOL', lambda i: self.start_tool_prompt(i), True, pr.TOOL)
        if number == 45:
            return self.open_external_usilic()
        if number == 53:
            return self.start_part_prompt()
        if number == 106:  # ^J^P
            items = [i for i in self.ring(hist.RECENT_FILE) if self.part_spec(i)]
            return self.show_list(items, 'FILE', self.reopen_part, True, pr.FILE)
        if number == 65:
            return self.start_copy()
        if number == 66:
            return False  # ^O^Yはプロンプトの中だけ(feed_prompt_byte())
        if number == 67:
            return self.start_edit()
        if number == 70:  # ^[l
            return self.start_shell_log()
        if number == 86:  # ^J^Os
            return self.show_shell_log()
        if number in (71, 72, 73, 74, 75, 76, 77):
            return self.shell_helper(number)
        if number == 78:
            return self.start_usilic_toc()
        if number == 79:
            return self.show_list(self.ring(hist.USILIC), 'USILIC', self.open_usilic_item, True)
        if number == 80:
            return self.show_list(self.ring(hist.USILIC), 'USILIC', self.open_usilic_item, False)
        if number == 81:
            return self.save_buffer_to_work()
        if number in (82, 83):
            return self.start_info()
        if number == 84:
            return self.open_file(self.configuration.path) if self.configuration.path else False
        if number == 85:
            path = os.path.join(os.path.expanduser('~'), '.usilic', 'error')
            return self.open_file(path) if os.path.exists(path) else False
        if number == 88:
            return self.enter_directory_listing(self.data_directory(), keep_stack=True)
        if number == 89:
            return self.show_work_list()
        if number == 92:
            return self.toggle_language()
        if number == 95:
            if self.macro_recording is not None:
                self.show_transient_notice('Already defining kbd macro')
                return False
            self.macro_recording = []
            self.show_transient_notice('Defining kbd macro...')
            return True
        if number == 96:
            return self.stop_macro_recording(96)
        if number == 97:
            return self.play_recorded_macro()
        if number == 98:
            return self.save_or_reload_macros()
        if number == 101 and self.in_epub_body():
            return self.toggle_epub_chapter_mode()
        if number == 102:
            return self.cycle_url_mode()
        if number == 107:  # ^Od: ホイールの向き(既定はmacOSのナチュラルスクロールの向き=reversed)
            self.wheel_reversed = not self.wheel_reversed
            self.show_transient_notice('Wheel: normal' if self.wheel_reversed else 'Wheel: reversed')
            return True
        if number == 108:
            return self.show_epub_marks()
        if number == 28 and self.in_epub_body():
            return self.follow_epub_link()
        if number == 20 and self.in_epub_body() and self.epub_note_active():  # 注釈の区画から元の所へ
            return self.leave_epub_note()
        if number == 20 and self.in_epub_body() and self.epub_link_stack:  # 注釈から戻る
            line = self.epub_link_stack.pop()
            self.set_epub_cursor(line)
            return True
        return None

    # --- PART・OCCUR ---
    @staticmethod
    def part_spec(item):
        """FILEの履歴の「パス "文字列"」を(パス, 文字列)に。違えばNone。"""
        if len(item) >= 4 and item.endswith('"'):
            at = item.rfind(' "', 0, len(item) - 1)
            if at > 0:
                return item[:at], item[at + 2:-1]
        return None

    def start_part_prompt(self):
        """^Jp: 今の画面を拡張正規表現で分ける。"""
        return self.start_prompt(pr.PART, '', self.execute_part)

    def execute_part(self, pattern, syntax=REGEX):
        if not pattern:
            return False
        view = self.active_view()
        # 分けた元(C++のexecuteCut()のcutOrigin_)と、一覧のモードラインの最後の欄
        origin, origin_path = {DIRECTORY: ('PART', ''), MAN: ('MAN', self.man_mode.current_page),
                               LOCATE: ('LOCATE', self.locate_mode.query),
                               DU: ('DU', self.du_mode.current_path)}.get(self.current_mode, ('FILE', ''))
        name = self.file_mode.current_file if self.current_mode == FILE else ''
        size = {FILE: self.file_mode.byte_count, MAN: self.man_mode.byte_count,
                SHELL: self.shell.shown_byte_count(self.wrap_width())}.get(self.current_mode, self.directory_mode.byte_count)
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.load_all()
        if self.current_mode == FILE:  # 大きさはFILEの画面の行で(C++のcontentByteCount(fileView_))
            size = sum(len(l.text.encode('utf-8')) + 1 + l.hidden_bytes for l in view.lines)
        if self.current_mode not in (PART_TOC, PART):
            self.part_position_key = None
        if not self.part.split(view.lines, pattern, size, name, syntax, origin, origin_path):
            self.show_transient_notice('No string')
            return False
        if self.history is not None:
            self.history.push_recent(hist.RECENT_EGREP_PATTERN, pattern)
            self.history.remember_name(hist.CUT_PATTERN, pattern)
            if name and self.current_mode == FILE and os.path.isfile(name):
                self.history.push_recent(hist.RECENT_FILE, '%s "%s"' % (name, pattern))
        if name and self.current_mode == FILE and os.path.isfile(name):
            self.part_position_key = '%s "%s"' % (name, pattern)
        self.enter_mode(PART_TOC)
        self.restore_part_position()
        return True

    def reopen_part(self, item):
        """^J^Pの項目(「パス "文字列"」): fileを開き直して同じ分割をやり直す。"""
        spec = self.part_spec(self.strip_tags(item) if item.endswith(hist.USILIC_TAG) else item)
        if not spec or not os.path.isfile(spec[0]):
            return False
        if not self.open_file(spec[0]):
            return False
        return self.execute_part(spec[1])

    def start_occur_prompt(self, syntax):
        """^O^S・^O^J^S: 今の画面で一致した行を一覧に。"""
        kind = pr.OCCUR_FIXED if syntax == FIXED else pr.OCCUR_REGEX
        return self.start_prompt(kind, '', lambda t: self.execute_occur(t, syntax))

    def execute_occur(self, pattern, syntax):
        if not pattern:
            return False
        view = self.active_view()
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.load_all()
        found = occur_lines(view.lines, pattern, syntax)
        if self.history is not None:
            self.history.push_recent(hist.RECENT_FGREP_PATTERN if syntax == FIXED else hist.RECENT_EGREP_PATTERN,
                                     pattern)
        self.occur_items, self.occur_syntax, self.occur_origin = found, syntax, self.current_mode
        self.occur_global = False
        self.grep.pattern, self.grep.syntax, self.grep.view = pattern, syntax, view
        self.grep.set_matches([l.text for l in view.lines], [i for i, _ in found])
        self.occur_byte_count = sum(len(l.text.encode('utf-8')) + 1 for _, l in found)
        self.occur_view.set_lines([Line(l.text, i + 1, attributes=l.attributes) for i, (_, l) in enumerate(found)])
        self.occur_view.cursor_index = self.occur_view.top_index = 0
        # 一致が無ければ空の一覧に「pattern: 見つかりません」(GrepMode::showMatches()、「見つかりません」に下線)
        suffix = localized('No string')
        text = pattern + ': ' + suffix
        self.occur_view.empty_placeholder = None if found else \
            Line(text, None, attributes=[(len(text) - len(suffix), len(text), 'underline')])
        self.enter_mode(OCCUR)
        return True

    def occur_jump(self):
        """一覧の^M: 元の画面のその行へ(一致の強調は残す、; :で前/次)。"""
        i = self.occur_view.cursor_index
        if not 0 <= i < len(self.occur_items):
            return False
        line = self.occur_items[i][0]
        if self.occur_global:  # EPUBの記録の一覧(^O^E): 本全体の行
            self.enter_mode(self.occur_origin)
            self.set_epub_cursor(line)
            return True
        self.grep.select_line(line)
        self.grep.persistent = True
        label = self.previous_mode_name(OCCUR)  # 「FGREP: 語」
        self.enter_mode(self.occur_origin)
        view = self.active_view()
        self.push_mark(view)
        view.cursor_index = line  # C++のopenSelectedGrepMatch(): setCursorIndex()(画面に入れるだけ)
        view.ensure_cursor_visible()
        # 右端(fileは後ろ、(ls)は「(ls)」の代わり)に「FGREP: 語」(C++のFileAnnotationKind::Search・DirAnnotationKind::Grep)
        if self.occur_origin == FILE and self.epub is None:
            self.file_mode.origin = (os.path.basename(self.file_mode.current_file), label)
        elif self.occur_origin == DIRECTORY:
            self.directory_mode.annotation = label
        return True

    # --- TOOL・外のページャー ---
    def start_tool_prompt(self, text=None):
        """^Jt: 選択(無ければカーソル行)のfile名を入れて、その前にプログラム名を打つ。"""
        self.tool_directory = ''
        seed = ''
        if text is None:
            if self.current_mode == DIRECTORY:
                d = self.directory_mode
                indices = sorted(d.marks) or [d.view.cursor_index]
                names = [d.entries[i].name for i in indices if i < len(d.entries) and not d.entries[i].is_directory]
                names = [n.rstrip('*@=%|') for n in names]
                if names:
                    self.tool_directory = d.current_directory
                    seed = ' '.join(shlex.quote(n) for n in names)
            elif self.current_mode == FILE and os.path.isfile(self.file_mode.current_file):
                self.tool_directory = os.path.dirname(self.file_mode.current_file)
                seed = shlex.quote(os.path.basename(self.file_mode.current_file))
            text = seed
        p = pr.Prompt(pr.TOOL, text, self.execute_tool)
        if seed:
            p.editor.cursor = 0
        self.prompt = p
        return True

    def execute_tool(self, command):
        """Application::executeProgram(): PROGRAMの物は画面を抜けて、usilicは外のページャー、他はSHELLで。"""
        words = command.split()
        if not words:
            return False
        if self.history is not None:
            self.history.push_recent(hist.SHELL_COMMAND, command)
        directory, self.tool_directory = self.tool_directory, ''
        programs = [p[0] for p in self.configuration.programs if p]
        if words[0] == 'usilic':
            return self.launch_external_usilic(shlex.split(command)[1:], directory or None)
        if words[0] in programs:
            return self.run_external(['sh', '-c', command], cwd=directory or None)
        if not self.ensure_shell():
            return False
        line = '(cd %s && %s)' % (shlex.quote(directory), command) if directory else command
        self.enter_mode(SHELL)
        self.shell.submit_line(line)
        self.refresh_shell_view()
        return True

    def open_external_usilic(self):
        """^Ou: 選択(無ければカーソル行)を外のusilic(ページャー・その場限りの(ls))で、1つずつ。"""
        if self.current_mode != DIRECTORY:
            return False
        d = self.directory_mode
        indices = sorted(d.marks) or [d.view.cursor_index]
        paths = [os.path.join(d.current_directory, d.entries[i].name.rstrip('*@=%|'))
                 for i in indices if i < len(d.entries) and d.entries[i].name not in ('./', '../')]
        for k, path in enumerate(paths):  # 1つずつ別々の子で(2つ目から右下へずらす、C++のexternalCascade_)
            self.launch_external_usilic([path], cascade=k, extra_env={'USILIC_NO_HISTORY': '1'})
            if self.history is not None:
                category = hist.RECENT_DIRECTORY if os.path.isdir(path) else hist.RECENT_FILE
                self.history.push_recent(category, path.rstrip('/') + ('/' if os.path.isdir(path) else '') +
                                         hist.USILIC_TAG)
        return True

    def quick_look(self):
        d = self.directory_mode
        indices = sorted(d.marks) or [d.view.cursor_index]
        paths = [os.path.join(d.current_directory, d.entries[i].name.rstrip('*@=%|'))
                 for i in indices if i < len(d.entries) and d.entries[i].name not in ('./', '../')]
        if not paths:
            return False
        try:
            subprocess.Popen(['qlmanage', '-p'] + paths, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            return False
        return True

    # --- USILIC・WORK ---
    def start_usilic_toc(self):
        """^[u: MAN・DIR・FILE・TOOL・WORK・USILIC・ARC・EPUB・PARTの目次。"""
        result = self.show_list(USILIC_TOC_TITLES, 'USILIC', self.open_usilic_group, False)
        self.list_view.cursor_index = self.list_view.top_index = 0
        return result

    def open_usilic_group(self, title):
        """USILICの目次の項目(Application::openUsilicGroup()): 同じUSILICの一覧で、その種類の物だけ。MAN・DIR・FILEは
        USILICの履歴を種類で分けた物(「名前 (man)」・/で終わる物・他の/で始まる物)、TOOLはコマンドの履歴、ARC・EPUB・
        PARTはFILEの履歴から、USILICは外のusilicで開いた物。どれも名前順で重ねの無い並び。"""
        titles = USILIC_TOC_TITLES
        kind = titles.index(title) if title in titles else -1
        if kind < 0:
            return False

        def usilic_kind(entry):
            entry = entry[:-len(hist.USILIC_TAG)] if entry.endswith(hist.USILIC_TAG) else entry
            if not entry.startswith('/'):
                return 0
            return 1 if entry.endswith('/') else 2
        on_choose = self.open_usilic_item
        if title == 'TOOL':
            entries = sorted(set(self.ring(hist.SHELL_COMMAND)))
            on_choose = self.start_tool_prompt  # TOOLのプロンプトにその字を入れる
        elif title == 'WORK':
            entries = list(self.work_files)
        elif title in ('ARC', 'EPUB', 'PART'):
            keep = {'ARC': lambda e: self.is_archive_history_entry(e), 'EPUB': is_epub_file,
                    'PART': lambda e: self.part_spec(e) is not None}[title]
            entries = [e for e in sorted(set(self.ring(hist.RECENT_FILE))) if keep(e)]
            if title == 'ARC':
                on_choose = self.open_recent_file_entry
        elif title == 'USILIC':
            entries = sorted(set(e for e in self.ring(hist.USILIC) if e.endswith(hist.USILIC_TAG)))
        else:
            entries = sorted(set(e for e in self.ring(hist.USILIC) if usilic_kind(e) == kind))
        result = self.show_list(entries, 'USILIC', on_choose, False, allow_empty=True)
        self.list_view.cursor_index = self.list_view.top_index = 0  # 頭から(C++のunderlineMode_.show())
        return result

    def is_archive_history_entry(self, item):
        """Application::isArchiveHistoryEntry(): アーカイブの項目(.epubは除く、ARCで開いた「…/」も)。"""
        path = item[:-1] if len(item) > 1 and item.endswith('/') else item
        return not is_epub_file(path) and bool(self.configuration.archive_for(path))

    def open_usilic_item(self, item):
        """USILICの項目: 「名前 (man)」はMAN、「(usilic)」付きは外のusilic、/で終わればディレクトリ。"""
        if item.endswith(' (man)'):
            return self.execute_man(item[:-len(' (man)')])
        if item.endswith(hist.USILIC_TAG):
            path = item[:-len(hist.USILIC_TAG)]
            return self.launch_external_usilic([path], extra_env={'USILIC_NO_HISTORY': '1'})
        spec = self.part_spec(item)
        if spec or not os.path.exists(item):  # 無い物(「パス "文字列"」等)はFILEの履歴の開き方で
            return self.open_recent_file_entry(item)
        if not self.open_path(item):
            return False
        if self.current_mode == DIRECTORY and os.path.isdir(item):
            self.directory_mode.annotation = 'USILIC'  # C++のDirAnnotationKind::Usilic
        elif self.current_mode == FILE and self.file_mode.current_file == item and self.epub is None:
            self.file_mode.origin = (item, 'USILIC')
        return True

    def allocate_work_file(self):
        os.makedirs(self.work_root, exist_ok=True)
        path = os.path.join(self.work_root, 'w.%d' % (len(self.work_files) + 1))
        self.work_files.append(path)
        return path

    def save_buffer_to_work(self):
        """^X^W: 今の画面の行を作業ファイル(WORK)へ足す。"""
        path = self.work_files[-1] if self.work_files else self.allocate_work_file()
        try:
            with open(path, 'a', encoding='utf-8', errors='surrogateescape') as f:
                for line in self.active_view().lines:
                    f.write(line.text + '\n')
        except OSError:
            self.show_transient_notice('Cannot write: ' + path)
            return False
        return True  # 知らせは出さない(C++のsaveBufferToFile()と同じ)

    def show_work_list(self):
        """^J^Ow(Application::executeOpenWorkList()): USILICの目次のWORKを開いたのと同じ(^Gは目次のWORKへ)。"""
        self.start_usilic_toc()
        self.list_view.cursor_index = USILIC_TOC_TITLES.index('WORK')
        return self.open_usilic_group('WORK')

    # --- マクロ ---
    def macro_for_byte(self, byte):
        if self.current_mode in (KEY,) and byte == ord('h'):
            return None
        for macro in self.configuration.macro_list():
            seq = mc.macro_key_bytes(macro, self.bindings.prefixes)
            if seq == [byte]:
                return macro
        return None

    def run_macro(self, macro):
        """MACRO節のキー: K-nは機能、"文字列"は打ったことにする。"""
        self.playing = True
        try:
            for kind, value in macro.items:
                if kind == 'k':
                    if self.pager_launch and value not in PAGER_KEYS:
                        continue
                    self.run_command(value, None)
                else:
                    for b in value.encode('utf-8'):
                        self.feed_canonical(b, None)
        finally:
            self.playing = False
        return True

    def stop_macro_recording(self, number):
        """^X)(K-96)・記録中の^Xe(K-97): 記録を終える(終えたキー自身は記録に入れない)。"""
        if self.macro_recording is None:
            self.show_transient_notice('Not defining kbd macro')
            return False
        seq = self.bindings.sequence_for(number) or []
        recorded = self.macro_recording
        self.macro_recording = None
        if seq and recorded[-len(seq):] == seq:
            recorded = recorded[:-len(seq)]
        self.macro_bytes = bytes(recorded)
        self.show_transient_notice('Keyboard macro defined')
        return True

    def play_recorded_macro(self):
        """^Xe: 記録したキーを打ち直す(間隔はWAIT、C++のplayRecordedMacro())。記録が無ければ何もしない。記録中なら
        記録を終えてから(Emacsのkmacro-end-and-call-macro)。打ち直した後は、続けてe(K-131)でもう一度。"""
        if self.macro_recording is not None:
            self.stop_macro_recording(97)
        if not self.macro_bytes:
            return False
        self.playing = True
        wait = self.configuration.wait_centiseconds / 100.0
        try:
            for b in self.macro_bytes:
                self.feed_canonical(b, None)
                if wait:
                    self.redraw()
                    time.sleep(wait)
        finally:
            self.playing = False
        self.macro_just_played = True
        self.macro_repeat_armed = True
        self.show_transient_notice('Type e to repeat macro')
        return True

    def save_or_reload_macros(self):
        """^Xs(C++のsaveMacroToUsilicrc()): usilicrcを読み直してMACROの一覧。^Xeの直後なら、記録をMACRO節の「--」の
        行へ書いてから、その行でキーを尋ねる。"""
        just_played, self.macro_just_played = self.macro_just_played, False
        path = self.configuration.path
        if not path:
            return False
        self.reload_configuration()
        recorded = False
        if just_played and self.macro_bytes:
            items = mc.items_from_bytes(list(self.macro_bytes))
            if not items:
                return False
            mc.set_macro_marker(path, ['\t--\t' + items])
            recorded = True
        return self.start_macro_list(recorded)

    def start_macro_list(self, recorded):
        """MACROの一覧(C++のstartMacroList()): 「キー  キーの表記」の行と「--」の行。カーソルの行を「キー キーの表記」
        として入力欄で直し、^Mでusilicrcへ(execute_macro_edit())。記録を書いた直後は「--」の行だけ(^N・^Pでは動かない)。"""
        if self.prompt is not None and self.prompt.kind != pr.MACRO_EDIT:
            return False
        lines = mc.macro_lines(mc.read_text(self.configuration.path))
        if not any(m.number == 0 for m in lines):
            lines.append(mc.MacroLine())
        if not recorded:
            lines = [m for m in lines if m.number] or lines
        items, displays, selected = [], {}, 0
        for i, m in enumerate(lines):
            shown_notation = mc.notation(m.items)
            item = (m.key + ' ' + shown_notation) if m.number else ''
            shown = m.key if m.number else '--'
            shown += ' ' * (4 - len(shown) if len(shown) < 4 else 1)
            items.append(item)
            displays[item] = shown + shown_notation
            if (m.number == 0) if recorded else (self.macro_list_last_number and m.number == self.macro_list_last_number):
                selected = i
        self.macro_list = (lines, recorded)
        if self.current_mode != LIST or self.prompt is None:
            self.list_mode.show(items, False, 'MACRO', lambda i: True, None, None, lambda n: displays.get(n, n))
            self.enter_mode(LIST)
        else:
            self.list_mode.show(items, False, 'MACRO', lambda i: True, None, None, lambda n: displays.get(n, n))
        self.list_view.cursor_index = selected
        self.list_view.ensure_cursor_visible()
        p = pr.Prompt(pr.MACRO_EDIT, items[selected], self.execute_macro_edit, show_position=True)
        p.candidate_list = True
        p.editor.move_home()
        self.prompt = p
        return True

    def close_macro_list(self):
        self.prompt = None
        self.macro_list = None
        return self.go_back()

    def execute_macro_edit(self, text):
        """MACROの一覧の^M(C++のexecuteMacroEdit()): 「キー キーの表記」でその行(「--」の行なら新しい番号)を書き換える。
        表記が今のままなら要素も今のまま。キーが空・他の行で使っている・表記が読めない時はBELでそのまま。"""
        lines, _ = self.macro_list
        index = self.list_view.cursor_index
        if not 0 <= index < len(lines):
            return self.close_macro_list()

        def stay():
            p = pr.Prompt(pr.MACRO_EDIT, text, self.execute_macro_edit, show_position=True)
            p.candidate_list = True
            self.prompt = p
            return False
        number = lines[index].number
        key, _, shown_notation = text.lstrip(' \t').replace('\t', ' ').partition(' ')
        shown_notation = shown_notation.strip(' \t')
        if not key or '#' in key or '"' in key:
            return stay()
        path = self.configuration.path
        rc_text = mc.read_text(path)
        description = ''
        for m in mc.macro_lines(rc_text):
            if m.number and m.number != number and m.key == key:
                return stay()
            if m.number == number and number:  # 「# キーの並び  説明」の説明を残す
                at = next((i for i, c in enumerate(m.comment) if c in ' \t'), -1)
                if at >= 0:
                    description = m.comment[at:].lstrip(' \t')
        if not shown_notation or shown_notation == mc.notation(lines[index].items):
            items = lines[index].items
        else:
            items = mc.items_from_notation(shown_notation)
        if not items:
            return stay()
        written = number or mc.next_macro_number(rc_text)
        line = '\tM-%d  %s\t%s\t\t# %s' % (written, key, items, mc.notation(items))
        if description:
            line += '\t' + description
        ok = mc.set_macro_line(path, number, line) if number else mc.set_macro_marker(path, [line, '\t--'])
        if not ok:
            return stay()
        self.reload_configuration()
        self.macro_list_last_number = written
        self.close_macro_list()
        self.show_transient_notice('M-%d %s' % (written, key))
        return True

    def reload_configuration(self):
        """usilicrcを読み直す(EDITで直した時・^Xs)。"""
        config = Configuration()
        if config.load_default_file():
            self.configuration = config
            self.bindings = config.key_bindings
            for mode in (self.man_mode, self.du_mode, self.locate_mode):
                mode.configuration = config
            self.directory_mode.column_threshold = config.column_threshold
            self.directory_mode.apply_column_auto_display()
        return True

    # --- COPY ---
    def start_copy(self):
        if self.prompt is not None:
            return False
        if self.current_mode == FILE and self.vertical_active():  # 縦書きのまま字を範囲指定する
            self.sync_vertical()
            self.vertical.start_select()
            self.vertical_prefix = None
            return True
        if self.vertical_list_active():  # 縦書きの一覧は描いた画面のまま(C++のCopyModeのスナップショット)
            self.render_vertical_list()
            snapshot = ContentView()
            snapshot.set_height(self.vlist_view.height)
            snapshot.set_lines(list(self.vlist_view.lines))
            self.copy = CopyMode(snapshot)
        else:
            view = self.active_view()
            self.copy = CopyMode(view)
            self.copy_saved_position = (view, view.cursor_index, view.top_index)
        self.copy_prefix = None
        return True

    def feed_copy_key(self, byte, special):
        """COPYのキー(^N ^P ^F ^B ^A ^E・矢印、^@、^X␣、^[w、^[t、^[N、^G)。"""
        c = self.copy
        if special is not None:
            {'Up': lambda: c.move_row(-1), 'Down': lambda: c.move_row(1), 'Left': lambda: c.move_col(-1),
             'Right': lambda: c.move_col(1), 'PageUp': lambda: c.move_row(-c.view.height),
             'PageDown': lambda: c.move_row(c.view.height)}[special]()
            return True
        prefix, self.copy_prefix = self.copy_prefix, None
        if prefix == 'goto':
            if ord('0') <= byte <= ord('9'):
                self.copy_digits += chr(byte)
                self.copy_prefix = 'goto'
                return True
            if byte == CR and self.copy_digits:
                c.go_to_row(int(self.copy_digits) - 1)
            return True
        if prefix == 'ESC':
            if byte == ord('w'):
                return self.copy_to_clipboard(False)
            if byte == ord('t'):
                return self.copy_to_clipboard(True)
            if ord('0') <= byte <= ord('9'):
                self.copy_prefix, self.copy_digits = 'goto', chr(byte)
                return True
            if byte == ord('<'):
                c.go_to_row(0)
            elif byte == ord('>'):
                c.go_to_row(len(c.view.lines) - 1)
            elif byte == ord('v'):
                c.move_row(-c.view.height)
            elif chr(byte).isalpha() and chr(byte) not in 'wtvfbd':
                # ^[sのようなモードキー: ^[wと同じく確定してからCOPYを抜け、そのキーを通常どおり(C++の項目1120)
                self.copy_then_leave()
                self.feed_canonical(ESC, None)
                return self.feed_canonical(byte, None)
            return True
        if prefix == 'X':
            if byte == ord(' '):
                c.toggle(True)
            elif byte != CTRL_G:  # ^X^F等も確定してから抜けて通常どおり
                self.copy_then_leave()
                self.feed_canonical(CTRL_X, None)
                return self.feed_canonical(byte, None)
            return True
        if byte == CTRL_J:  # ^Jの続き(^Js等)も確定してから抜ける
            self.copy_then_leave()
            return self.feed_canonical(CTRL_J, None)
        if byte == ESC:
            self.copy_prefix = 'ESC'
        elif byte == CTRL_X:
            self.copy_prefix = 'X'
        elif byte in (0, CTRL_SPACE):
            c.toggle(False)
        elif byte == CTRL_G:
            self.end_copy()
        elif byte in (0x0E, 0x10):
            c.move_row(1 if byte == 0x0E else -1)
        elif byte in (0x06, 0x02):
            c.move_col(1 if byte == 0x06 else -1)
        elif byte == 0x01:
            c.line_start()
        elif byte == 0x05:
            c.line_end()
        elif byte == 0x16:
            c.move_row(c.view.height)
        return True

    def copy_then_leave(self):
        """^[wと同じく範囲(離れ小島も)を確定してヤンクバッファ・クリップボードへ入れ、COPYを抜ける(C++のcopyThenLeave())。"""
        text = self.copy.confirmed_text() if self.copy is not None else ''
        self.end_copy()
        if text:
            pr.PROMPT_KILL['text'] = text
            self.deliver_copied_text(text, False, notice=False)

    def end_copy(self):
        """COPYを抜ける。下のモードの位置はCOPYに入る前のまま(C++はCOPYを写し(copyView_)の上でする)。"""
        self.copy = None
        saved, self.copy_saved_position = self.copy_saved_position, None
        if saved is not None:
            view, cursor, top = saved
            view.cursor_index, view.top_index = cursor, top
            view.clamp()

    def copy_to_clipboard(self, to_tool):
        """^[w: 範囲をクリップボードへ(macOSはpbcopy、他はxclip)。^[tはさらにTOOLのプロンプトへ(改行もそのまま)。
        ^[wはC++と同じくCOPYのまま(矩形の範囲の時だけ抜ける)、知らせは出さない。"""
        c = self.copy
        rectangle = c.anchor is not None and c.rectangle
        text = c.confirmed_text()
        if rectangle or to_tool:
            self.end_copy()
        if not text:  # 範囲が無い: ^[tは前にコピーした字でTOOLのプロンプトへ(C++のclipboardText())
            return self.start_tool_prompt(self.clipboard_text or '') if to_tool else True
        pr.PROMPT_KILL['text'] = text  # プロンプトの^Yで入る(C++のpromptEditor_.setKillBuffer())
        return self.deliver_copied_text(text, to_tool, notice=False)

    def deliver_copied_text(self, text, to_tool, notice=True):
        """COPYで集めた字をクリップボードへ(to_toolならTOOLのプロンプトへも)。"""
        if not text:
            return False
        self.clipboard_text = text
        argv = ['pbcopy'] if sys.platform == 'darwin' else ['xclip', '-selection', 'clipboard']
        try:
            subprocess.run(argv, input=text.encode('utf-8'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass
        if to_tool:
            return self.start_tool_prompt(text)
        if notice:
            self.show_transient_notice('Copied: ' + text.split('\n')[0][:40])
        return True

    def clipboard_contents(self):
        if sys.platform == 'darwin':
            try:
                return subprocess.run(['pbpaste'], capture_output=True).stdout.decode('utf-8', 'replace')
            except OSError:
                pass
        return self.clipboard_text

    # --- EDIT ---
    def start_edit(self):
        """^[e: エディタ(+N付き)で開き、終わったら読み直す(usilicrcなら設定も)。"""
        editing_file = self.current_mode == FILE and not self.in_epub_body() and \
            os.path.isfile(self.file_mode.current_file)
        if editing_file:
            path = self.file_mode.current_file
            lines = self.file_view.lines
            line = (lines[self.file_view.cursor_index].raw_line_number if lines else 1) or 1
        elif self.current_mode == DIRECTORY and self.directory_mode.selected_entry() and \
                not self.directory_mode.selected_entry().is_directory:
            path = os.path.join(self.directory_mode.current_directory,
                                self.directory_mode.selected_entry().name.rstrip('*@=%|'))
            line = 1
        else:
            return False
        self.run_external(self.configuration.editor_argv() + ['+%d' % line, path])
        if self.configuration.path and os.path.realpath(path) == os.path.realpath(self.configuration.path):
            self.reload_configuration()
        if editing_file:
            cursor, tail, label = self.file_view.cursor_index, self.file_mode.tail, self.file_mode.label
            if self.file_mode.load_file(path, self.wrap_width(), self.file_view.height):
                self.file_mode.load_all()
                self.file_mode.tail, self.file_mode.label = tail, label
                self.file_view.cursor_index = cursor
                self.file_view.clamp()
                self.file_view.ensure_cursor_visible()
        else:
            self.directory_mode.refresh()
        return True

    # --- INFO・SHELLの補助 ---
    def start_info(self):
        """^I: OS名(起動のコマンドライン)・Startup file・ERROR・SHELL・DATA・WORK(^Mでそれぞれを開く)。"""
        home = os.path.join(os.path.expanduser('~'), '.usilic')
        rows = [(os_name.os_display_name() or 'Boot options', os_name.command_line(), None),
                ('Startup file', self.configuration.path or '', 'file'),
                ('ERROR', os.path.join(home, 'error'), 'file'),
                ('SHELL', os.path.join(home, 'shell'), 'shell'),  # 値はSHELLの出力のファイル(C++のlogFilePath_)
                ('DATA', self.data_directory(), 'dir')]
        works = self.work_files or ['']
        for i, path in enumerate(works):
            rows.append(('WORK' if i == 0 else '', path, 'file' if path else None))
        lines, self.info_targets = [], []
        width = max(12, len(rows[0][0]))  # 「:」の桁はOS名と「Startup file」の長い方に揃える(C++と同じ)
        for number, (label, value, kind) in enumerate(rows, 1):
            text = (label.rjust(width) + ' : ' if label else ' ' * (width + 3)) + value
            # 反転は値から、開けない行(1行目・空のWORK)は反転しない(C++のhighlightStartColumn・neverHighlighted)
            lines.append(Line(text, number, highlight_start=width + 3, never_highlighted=kind is None))
            self.info_targets.append((kind, value) if kind else None)
        self.info_byte_count = sum(len(l.text.encode('utf-8')) + 1 for l in lines)
        self.info_view.set_lines(lines)
        self.info_view.cursor_index = self.info_view.top_index = 0
        self.enter_mode(INFO)
        return True

    def info_open(self):
        i = self.info_view.cursor_index
        target = self.info_targets[i] if 0 <= i < len(self.info_targets) else None
        if target is None:
            return False
        kind, value = target
        if kind == 'dir':
            return self.enter_directory_listing(value, keep_stack=True)
        if kind == 'shell':
            self.save_shell_log()  # 今のSHELLの分を書いてから(C++のrefreshShellLogFile())
        if not os.path.exists(value):
            open(value, 'a').close() if os.path.isdir(os.path.dirname(value)) else None
        if not os.path.exists(value) or not self.open_file(value):
            return False
        # C++のopenSelectedBannerEntry(): 空なら下線の「No String」の1行、名前はfileの名前で後ろに「INFO」、^GはINFOへ
        if not self.file_view.lines:
            self.file_view.set_lines([Line('No String', None, attributes=[(0, 9, 'underline')])])
        self.file_mode.origin = (os.path.basename(value), 'INFO')
        return True

    def shell_log_path(self):
        """SHELLのログのfile(C++のlogFilePath_、~/.usilic/shell)。"""
        return os.path.join(os.path.expanduser('~'), '.usilic', 'shell')

    def save_shell_log(self):
        """Application::saveShellLog(): 今のSHELLの行を、前のセッションの分を残したままログへ書く。このセッションで
        前に書いた分(shell_log_block)があればそれを置き換え、無ければ足す。"""
        if not self.shell_started:
            return
        block = ''.join(row + '\n' for row in self.shell.committed_rows(self.wrap_width()))
        path = self.shell_log_path()
        try:
            with open(path, encoding='utf-8', errors='surrogateescape') as f:
                text = f.read()
        except OSError:
            text = ''
        at = text.rfind(self.shell_log_block) if self.shell_log_block else -1
        text = text[:at] + block + text[at + len(self.shell_log_block):] if at >= 0 else text + block
        temporary = '%s.tmp.%d' % (path, os.getpid())
        try:
            with open(temporary, 'w', encoding='utf-8', errors='surrogateescape') as f:
                f.write(text)
            os.replace(temporary, path)
        except OSError:
            return
        self.shell_log_block = block

    def end_shell_session(self):
        """SHELLを終える前(bashが終わった・^Js): ログを書き、次のセッションは新しい分として足す。"""
        self.save_shell_log()
        self.shell_log_block = ''
        self.shell_started = False

    def show_shell_log(self):
        """^J^Os(Application::executeShowShellLog()): ログを書いてからfileとして開く。^Gは(ls)へ。"""
        self.save_shell_log()
        path = self.shell_log_path()
        if not os.path.exists(path) or not self.open_file(path):
            return False
        self.enter_mode(FILE)
        self.return_stack[:] = [DIRECTORY]  # C++のfileReturnMode_ = Directory
        return True

    def start_shell_log(self):
        """^[l(Application::executeStartLog()): ログを「usilic-1.5b:」(プロンプトの行)で分けた一覧(LOG)。"""
        self.save_shell_log()
        path = self.shell_log_path()
        if not os.path.exists(path) or not self.open_file(path):
            return False
        self.current_mode = FILE
        view = self.file_view
        size = sum(len(l.text.encode('utf-8')) + 1 + l.hidden_bytes for l in view.lines)
        if not self.part.split(view.lines, PROMPT_DISPLAY_NAME + ':', size, path, FIXED, 'LOG', path):
            self.show_transient_notice('No string')
            return False
        self.part_position_key = None
        self.current_mode = PART_TOC
        self.return_stack[:] = [DIRECTORY]  # ^Gは(ls)へ(C++のcutReturnMode_の既定)
        return True

    def shell_helper(self, number):
        """^J. ^J, ^Jw ^J^W ^O/ ^O^F ^O^B(SHELLの章)。"""
        if number == 71:  # ^J.: SHELLのカレントディレクトリを(ls)の所へ
            directory = self.directory_mode.current_directory
            if self.current_mode == FILE and os.path.isfile(self.file_mode.current_file):
                directory = os.path.dirname(self.file_mode.current_file) + '/'
            if not self.ensure_shell():
                return False
            self.enter_mode(SHELL)
            self.shell.submit_line("cd '" + directory + "'")  # C++と同じく、そのまま''で囲む
            self.refresh_shell_view()
            return True
        if number == 72:
            if not self.ensure_shell():
                return False
            self.enter_mode(SHELL)
            self.shell.submit_line('ls -l')
            self.refresh_shell_view()
            return True
        if number in (73, 74):
            origin = self.return_stack[-1] if self.current_mode == SHELL and self.return_stack else self.current_mode
            if number == 73:  # ^Jw: バッファ名
                name = self.file_mode.current_file if origin == FILE else self.directory_mode.current_directory
                self.last_shell_insert = shlex.quote(name)
            else:  # ^J^W: バッファの内容を~/.usilic/data/へ保存して「cat ファイル名」(C++のshellInsertBufferContent())
                if self.shell.alive() and not self.shell.prompt_ready:  # 前のコマンドがまだ終わっていない
                    self.show_transient_notice('SHELL busy (the previous command has not finished)')
                    return False
                view = self.active_view() if self.current_mode != SHELL else {
                    FILE: self.file_view, DIRECTORY: self.directory_view}.get(origin, self.directory_view)
                # 名前: (ls)はディレクトリ名、FILEはファイル名(.epubは.txtに、C++の項目1134)、ARCは選んだ中身の名前。
                # 他(一致した行の一覧等、一覧そのものが中身)は作業ファイル。
                name = ''
                if origin == DIRECTORY:
                    name = os.path.basename(self.directory_mode.current_directory.rstrip('/'))
                elif origin == FILE:
                    name = os.path.basename(self.file_mode.current_file)
                    if is_epub_file(name):
                        name = os.path.splitext(name)[0] + '.txt'
                elif origin == ARCHIVE and self.archives:
                    entry = self.archives[-1].selected_entry()
                    name = os.path.basename(getattr(entry, 'name', '') or '') if entry else ''
                if name:
                    path = os.path.join(self.data_directory(), name)
                    if self.history is not None:
                        self.history.push_recent(hist.RECENT_DIRECTORY, self.data_directory().rstrip('/') + '/')
                else:
                    path = self.allocate_work_file()
                with open(path, 'w', encoding='utf-8', errors='surrogateescape') as f:
                    for line in view.lines:
                        f.write(line.text + '\n')
                self.last_shell_insert = 'cat ' + shlex.quote(path)
            if not self.ensure_shell():
                return False
            if self.current_mode != SHELL:
                self.enter_mode(SHELL)
            self.command_line.set_text(self.last_shell_insert)
            self.refresh_shell_view()
            return True
        if self.current_mode != SHELL:
            return False
        if number == 75:
            return self.start_grep_prompt(FIXED)
        self.shell_view.page_down() if number == 76 else self.shell_view.page_up()
        return True

    def toggle_language(self):
        """^Oj: 日本語/英語(HELPを見ていればその言語のHELPへ)。"""
        messages_ja.set_japanese(not japanese())
        if self.history is not None:  # 次の起動も(C++のHistory::helpLanguage()、ページャーは書き戻さない)
            self.history.set_help_language(1 if japanese() else 2)
        if self.current_mode == KEY:  # KEYはその場で切り換えた言語の一覧に(C++のreopenHelpInOtherLanguage())
            cursor = self.key_view.cursor_index
            self.current_mode = self.return_stack.pop() if self.return_stack else DIRECTORY
            self.key_view.cursor_index = cursor
            return self.start_key()
        if self.current_mode == HELP:  # 平文のHELPは全体の中の割合の位置へ
            fraction = self.help_view.cursor_index / max(1, len(self.help_view.lines))
            back = list(self.return_stack)
            self.current_mode = self.return_stack.pop() if self.return_stack else DIRECTORY
            if self.open_plain_help(None):
                self.return_stack = back
                self.help_view.cursor_index = int(fraction * len(self.help_view.lines))
                self.help_view.clamp()
                self.center_on(self.help_view, self.help_view.cursor_index)
            return True
        if self.in_epub_body() and self.epub_name == 'HELP':
            fraction = self.file_view.cursor_index / max(1, len(self.file_view.lines))
            back = list(self.return_stack)
            if self.start_help() and self.in_epub_body():
                self.return_stack = back
                self.set_epub_cursor(int(fraction * len(self.epub_lines)))
        return True

    # --- (ls)の%・* ---
    def start_mark_pattern(self, extension):
        if extension:
            entry = self.directory_mode.selected_entry()
            name = entry.name.rstrip('/*@=%|') if entry else ''
            dot = name.rfind('.')
            seed = name[dot:] if dot > 0 else ''
            return self.start_prompt(pr.MARK_EXTENSION, seed, lambda t: self.execute_mark_pattern(t, True))
        return self.start_prompt(pr.MARK_REGEX, '', lambda t: self.execute_mark_pattern(t, False))

    def execute_mark_pattern(self, text, extension):
        """Application::executeMarkPattern(): 名前が正規表現・拡張子に合う行を選ぶ。"""
        if not text:
            return False
        if extension and not text.startswith('.'):
            text = '.' + text
        try:
            pattern = None if extension else re.compile(text)
        except re.error:
            self.show_transient_notice('No string')
            return False
        count = 0
        for i, entry in enumerate(self.directory_mode.entries):
            name = entry.name.rstrip('/*@=|')
            if name in ('.', '..'):
                continue
            if (pattern.search(name) if pattern else len(name) > len(text) and name.endswith(text)):
                self.directory_mode.marks.add(i)
                count += 1
        self.show_transient_notice('%d %s' % (count, 'file marked' if count == 1 else 'files marked'))
        return count > 0

    # --- レジスタ・ブックマーク ---
    def current_place(self):
        """(パス, 元の行の番号)。ディレクトリは行0。"""
        if self.current_mode == FILE and self.file_mode.current_file and os.path.exists(self.file_mode.current_file):
            lines = self.file_view.lines
            raw = lines[self.file_view.cursor_index].raw_line_number if lines else 1
            return os.path.abspath(self.file_mode.current_file), raw or 1
        if self.current_mode == DIRECTORY:
            return self.directory_mode.current_directory, 0
        return None

    def jump_to_place(self, path, raw_line):
        if raw_line == 0:
            return os.path.isdir(path) and self.enter_directory_listing(path)
        if not os.path.exists(path):
            return False
        if not (self.current_mode == FILE and os.path.abspath(self.file_mode.current_file) == path):
            if not self.open_path(path):
                return False
        view = self.file_view
        self.push_mark(view)
        view.go_to_line(raw_line)
        return True

    def feed_register_byte(self, byte):
        """^Xrの後: ␣c(レジスタへ)・jc(レジスタの所へ)・m(ブックマーク)・b/l(ブックマークの一覧)。"""
        state, self.register_state = self.register_state, None
        if byte == CTRL_G:
            return False
        if state == 'r':
            if byte == ord(' '):
                self.register_state = 'set'
                return True
            if byte == ord('j'):
                self.register_state = 'jump'
                return True
            if byte == ord('m'):
                return self.start_bookmark_list(True)
            if byte in (ord('b'), ord('l')):
                return self.start_bookmark_list(False)
            return False
        if state == 'set':
            place = self.current_place()
            if place is None:
                self.show_transient_notice("Can't set register here")
                return False
            self.registers[byte] = place
            self.show_transient_notice('Register %s set' % chr(byte))
            return True
        place = self.registers.get(byte)
        if place is None:
            self.show_transient_notice('Register %s is empty' % chr(byte))
            return False
        if not self.jump_to_place(*place):
            self.show_transient_notice("Can't open " + place[0])
            return False
        return True

    def start_bookmark_list(self, setting):
        """^Xrm(名前を尋ねて置く、初期値はファイル名)・^Xrb(一覧から^N/^Pで選んで^M、-名前で消す)。"""
        if setting:
            self.bookmark_place = self.current_place()
            if self.bookmark_place is None:
                self.show_transient_notice("Can't set bookmark here")
                return False
        entries = bm.read_bookmarks()
        if not setting and not entries:
            self.show_transient_notice('No bookmarks')
            return False
        names = [e[0] for e in entries]
        displays = dict(zip(names, bm.display_lines(entries)))
        if setting:
            path = self.bookmark_place[0].rstrip('/')
            initial = os.path.basename(path) or path
        else:
            initial = names[0]
        if not names:
            names, displays = [initial], {initial: '-- (new)'}
        self.list_mode.show(names, False, 'BOOKMARK', lambda i: True, None, None, lambda n: displays.get(n, n))
        self.list_view.cursor_index = 0
        self.enter_mode(LIST)
        kind = pr.BOOKMARK_SET if setting else pr.BOOKMARK_JUMP
        self.prompt = pr.Prompt(kind, initial, self.execute_bookmark, show_position=True)
        return True

    def execute_bookmark(self, text):
        """ブックマークの一覧の^M(C++のexecuteBookmark())。名前が空・消す名前や飛ぶ名前が無い時はBELで一覧のまま。"""
        name = text.strip(' \t')
        setting = self.bookmark_place is not None

        def stay():
            kind = pr.BOOKMARK_SET if setting else pr.BOOKMARK_JUMP
            self.prompt = pr.Prompt(kind, text, self.execute_bookmark, show_position=True)
            return False
        if not name:
            return stay()
        if setting:
            path, raw = self.bookmark_place
            self.bookmark_place = None
            self.go_back()
            bm.set_bookmark(name, path, raw)
            self.show_transient_notice('Bookmark "%s" set' % name)
            return True
        if name.startswith('-') and len(name) > 1:
            if not bm.delete_bookmark(name[1:]):
                return stay()
            self.go_back()
            self.show_transient_notice('Bookmark "%s" deleted' % name[1:])
            return True
        entry = bm.take_bookmark(name)
        if entry is None:
            return stay()
        self.go_back()
        if not self.jump_to_place(entry[1], entry[2]):
            self.show_transient_notice("Can't open " + entry[1])
            return False
        return True

    # --- EPUBの残り ---
    def set_epub_cursor(self, line):
        """本全体の行lineへ(章ごとの時はその章を出す)。"""
        if self.epub_chapter_mode:
            self.epub_chapter = self.chapter_of_line(line)
            start = self.apply_epub_chapter()
            line -= start
        self.center_on(self.file_view, line)

    def chapter_of_line(self, line):
        current = 0
        for i, start in enumerate(self.epub_toc_lines):
            if start <= line:
                current = i
        return current

    def chapter_bounds(self, index):
        starts = sorted(set(self.epub_toc_lines)) or [0]
        start = self.epub_toc_lines[index] if self.epub_toc_lines else 0
        later = [s for s in starts if s > start]
        return start, (later[0] if later else len(self.epub_lines))

    def apply_epub_chapter(self):
        """章ごとの時: file_viewにその章の行だけを出す。始まりの行を返す。"""
        if not self.epub_chapter_mode:
            self.file_view.set_lines(self.epub_lines)
            return 0
        start, end = self.chapter_bounds(self.epub_chapter)
        self.file_view.set_lines(self.epub_lines[start:end])
        return start

    def global_epub_line(self):
        offset = self.chapter_bounds(self.epub_chapter)[0] if self.epub_chapter_mode else 0
        return self.file_view.cursor_index + offset

    def toggle_epub_chapter_mode(self):
        """^Os: 章ごと/連続のスクロール。"""
        line = self.global_epub_line()
        self.epub_chapter_mode = not self.epub_chapter_mode
        self.epub_chapter = self.chapter_of_line(line)
        start = self.apply_epub_chapter()
        self.center_on(self.file_view, line - start)
        self.show_transient_notice('Scroll: chapter' if self.epub_chapter_mode else 'Scroll: continuous')
        return True

    def follow_epub_link(self, href=None):
        """本文の^M: カーソル行の最初のリンク(本の中は飛んで^Gで戻る、Webは^Owのモードで)。縦書きは選んでいる
        リンク(href)。"""
        if href is None:
            lines = self.file_view.lines
            if not lines:
                return False
            line = lines[self.file_view.cursor_index]
            if not line.links:
                return False
            href = line.links[0][2]
        if re.match(r'[a-z]+:', href):
            return self.open_url(href)
        target = self.epub.line_for(href)
        if target is None:
            return False
        if self.show_epub_note(self.epub_lines[target].raw_line_number):  # 注釈の区画へ(^Gで元の所へ)
            return True
        if self.epub_note_active():  # 注釈の中のリンク(↩等)から本文へ: 本文の行に戻してから
            self.leave_epub_note()
            self.set_epub_cursor(target)
            return True
        self.epub_link_stack.append(self.global_epub_line())
        self.set_epub_cursor(target)
        self.file_view.top_index = self.file_view.cursor_index
        self.file_view.clamp()
        self.show_transient_notice('^G to return from note')
        return True

    def cycle_url_mode(self):
        """^Ow: Webのリンクの開き方を巡回(名前をモードラインへ)。"""
        handlers = self.configuration.url_handler_list()
        self.url_mode = (self.url_mode + 1) % len(handlers)
        self.show_transient_notice(handlers[self.url_mode][0])
        return True

    def open_url(self, url):
        """Webのリンク(usilicrcのURL、Application::openUrl())。処理が終わると既定(最初)に戻る。"""
        handlers = self.configuration.url_handler_list()
        name, argv = handlers[self.url_mode % len(handlers)]
        self.url_mode = 0
        if is_novel_site_url(url):  # 小説サイトの入口は^Owの今の対処によらずOpenで(C++のopenEpubWebLink())
            opener = dict(handlers).get('Open')
            if opener is None:
                self.show_transient_notice('No Open in URL (usilicrc)')
                return False
            name, argv = 'Open', opener
        try:
            if name == 'Open':
                subprocess.Popen(argv + [url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            elif name == 'Copy':
                subprocess.run(argv, input=url.encode('utf-8'))
                self.show_transient_notice('Copied: ' + url)
            elif name == 'Look':
                subprocess.Popen(['qlmanage', '-p', url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            elif name == 'Term':
                return self.run_external(argv + [url])
            elif name == 'Dump':
                result = subprocess.run(argv + [url], capture_output=True)
                self.remember_previous_file()
                self.file_mode.load_bytes(result.stdout, url, self.wrap_width())
                self.file_mode.record_history = False
                self.file_mode.tail = 'Dump'
                self.enter_mode(FILE)
            elif name == 'Pdf':
                return self.start_shell_run(argv + [url], None, [], on_done=self.after_pdf)
            elif name == 'Epub':
                return self.run_narou2epub(url)
        except OSError:
            self.show_transient_notice('Cannot run: ' + (argv[0] if argv else name))
            return False
        return True

    def after_pdf(self, output):
        path = output[-1].strip() if output else ''
        if path and os.path.exists(path):
            opener = ['qlmanage', '-p'] if sys.platform == 'darwin' else ['xdg-open']
            subprocess.Popen(opener + [path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True

    def run_narou2epub(self, target):
        """作品番号・URL→narou2epubで~/.local/usilic-web/texts/にEPUBを作って開く(C++と同じ、2026-11-09)。"""
        handler = dict(self.configuration.url_handler_list()).get('Epub')
        if not handler:
            self.show_transient_notice('No Epub in URL (usilicrc)')
            return False
        directory = os.path.join(os.path.expanduser('~'), '.local', 'usilic-web', 'texts')
        os.makedirs(directory, exist_ok=True)
        return self.start_shell_run(handler + [target, '-o', directory], None, [],
                                    on_done=lambda out: self.after_narou2epub(out))

    def after_narou2epub(self, output):
        path = output[-1].strip() if output else ''
        if path.endswith('.epub') and os.path.exists(path):
            return self.open_epub(path)
        return False

    def epub_marks_path(self):
        return os.path.join(os.path.expanduser('~'), '.usilic', 'epub-marks.txt')

    def read_epub_marks(self):
        try:
            with open(self.epub_marks_path(), encoding='utf-8', errors='replace') as f:
                return [l.rstrip('\n') for l in f if '.epub:' in l]
        except OSError:
            return []

    @staticmethod
    def parse_epub_mark(entry):
        """「本.epub:章のパス:位置: 文字列」を(本, 章のパス, 位置, 文字列)に。違えばNone。"""
        book, sep, rest = entry.partition('.epub:')
        path, _, rest = rest.partition(':')
        digits, sep2, text = rest.partition(': ')
        if not sep or not sep2 or not digits.isdigit():
            return None
        return book + '.epub', path, int(digits), text

    def epub_mark_place(self, chapter, offset, text, logical=None):
        """開いている本の記録の位置を(本全体の行, その行の中の字の範囲)に。無ければNone。"""
        chapters = {href: start for start, href in getattr(self.epub, 'chapter_files', [])}
        if chapter not in chapters:
            return None
        sources, pieces = logical or self.epub_logical_lines()
        place = ev.place_in_chapter([x.raw for x in sources], {x.raw: x.text for x in sources}, chapters[chapter],
                                    offset, text)
        if place is None:
            return None
        raw, pos = place
        g, pad, start = next((p for p in reversed(pieces.get(raw, [])) if p[1] is not None and p[2] <= pos),
                             (None, 0, 0))
        if g is None:
            return None
        shown = self.epub_lines[g].text
        begin = min(pos - start + pad, len(shown))
        return g, (begin, min(begin + len(text), len(shown)))

    def show_epub_marks(self):
        """^O^E: 記録(~/.usilic/epub-marks.txt、「本.epub:章のパス:位置: 文字列」)の一覧。本文からは今の本の記録を、
        記録した字を強調した本文の行の一覧に(C++のstartEpubMarkList()、^Mでその行へ)。他の画面からは全部の本の
        記録(^Mでその本を開いてその所へ)。"""
        entries = [e for e in (self.parse_epub_mark(x) for x in self.read_epub_marks()) if e]
        if self.in_epub_body():
            book = os.path.basename(self.epub.path)
            logical = self.epub_logical_lines()
            by_line = {}
            for name, chapter, offset, text in entries:
                if name != book:
                    continue
                found = self.epub_mark_place(chapter, offset, text, logical)
                if found is not None:
                    by_line.setdefault(found[0], []).append(found[1])
            if not by_line:
                self.show_transient_notice('No marks')
                return False
            lines = sorted(by_line)
            self.occur_items = [(g, self.epub_lines[g]) for g in lines]
            self.occur_global, self.occur_origin, self.occur_syntax = True, FILE, FIXED
            self.occur_byte_count = sum(len(self.epub_lines[g].text.encode('utf-8')) + 1 for g in lines)
            self.occur_view.set_lines([Line(self.epub_lines[g].text, i + 1,
                                            attributes=[(a, b, 'match') for a, b in by_line[g] if b > a])
                                       for i, g in enumerate(lines)])
            self.occur_view.cursor_index = self.occur_view.top_index = 0
            self.enter_mode(OCCUR)
            return True
        items = [x for x in self.read_epub_marks() if self.parse_epub_mark(x)]
        if not items:
            self.show_transient_notice('No marks')
            return False

        def choose(item):
            name, chapter, offset, text = self.parse_epub_mark(item)
            path = next((self.strip_tags(f) for f in self.ring(hist.RECENT_FILE)
                         if os.path.basename(self.strip_tags(f)) == name), name)  # FILEの履歴から本の場所を
            if not self.open_epub(path):
                return False
            found = self.epub_mark_place(chapter, offset, text)
            line = found[0] if found else self.epub.line_for(chapter)
            if line is not None:
                self.set_epub_cursor(line)
            return True
        return self.show_list(items, 'EPUB', choose, False)

    # --- EPUBの縦書き(epub_vertical.py) ---
    def vertical_active(self):
        """縦書きの本の本文を見ている(縦横は本が決める、OPFのpage-progression-direction="rtl"、C++と同じ)。"""
        return self.in_epub_body() and self.epub.right_to_left and self.epub_section is None

    def vertical_selecting(self):
        return (self.vertical is not None and self.vertical.selecting and self.current_mode == FILE
                and self.vertical_active() and self.prompt is None)

    def epub_logical_lines(self):
        """本全体の行(epub_lines)を元の行ごとの論理行につなぎ直す(C++のepubLogicalLines())。中央寄せ・字下げの行は
        頭の空白を除き、縦書きでは列の頭の空きにする。(SourceLineの並び, 元の行→[(本全体の行, 頭の空白の数,
        論理行の中での始まり)])。"""
        sources, pieces = [], {}
        hints = self.epub.hints
        for g, line in enumerate(self.epub_lines):
            raw = line.raw_line_number
            head = not sources or sources[-1].raw != raw
            margin = self.epub_margin_columns  # 横書きの左の余白(C++のepubLogicalLines())
            pad = margin if margin > 0 and line.text[:margin] == ' ' * margin else 0
            if raw in hints.center or raw in hints.indent:
                pad = len(line.text) - len(line.text.lstrip(' '))
            text = line.text[pad:]
            runs = [(max(0, a - pad), max(0, b - pad), t) for a, b, t in line.attributes if t != 'ruby_text']
            links = [(max(0, a - pad), max(0, b - pad), h) for a, b, h in line.links]
            if head:
                first, rest = hints.indent.get(raw, (0, 0))
                sources.append(ev.SourceLine(text, raw, runs, links, first, rest, raw in hints.align_end))
                pieces[raw] = [(g, pad, 0)]
                continue
            src = sources[-1]
            offset = len(src.text)
            src.text += text
            src.runs += [(a + offset, b + offset, t) for a, b, t in runs]
            for a, b, h in links:  # 折り返しで切れたリンクは1つに
                if src.links and src.links[-1][1] == a + offset and src.links[-1][2] == h:
                    src.links[-1] = (src.links[-1][0], b + offset, h)
                else:
                    src.links.append((a + offset, b + offset, h))
            pieces[raw].append((g, pad, offset))
        return sources, pieces

    def sync_vertical(self):
        """縦書きの列を(本・幅・行数・章ごとが変わっていれば)組み直し、横書きの側(FGREP・目次・リンク等)で
        動いていれば、その行を縦書きの右端に置く。"""
        key = (self.epub_vertical_rows(), self.display.columns, self.epub_chapter_mode)
        line = self.global_epub_line()
        if self.vertical is None or key != self.vertical_key or self.vertical_source is not self.epub_lines:
            sources, self.vertical_pieces = self.epub_logical_lines()
            hints = self.epub.hints
            sources = [x for x in sources if self.epub_horizontal_range(x.raw) is None]  # 横書きの範囲は列にしない
            chapter_raws = {self.epub_lines[i].raw_line_number for i in self.epub_toc_lines
                            if 0 <= i < len(self.epub_lines)}
            self.vertical = ev.VerticalBook(sources, chapter_raws, self.epub_vertical_rows(), self.display.columns,
                                            self.epub_chapter_mode, hints.centered, hints.horizontal)
            self.vertical.body_total = self.epub_lines[-1].raw_line_number or 0 if self.epub_lines else 0
            self.vertical_key, self.vertical_source, self.vertical_synced = key, self.epub_lines, None
        if line != self.vertical_synced:
            self.show_vertical_line(line)

    def show_vertical_line(self, line):
        if not 0 <= line < len(self.epub_lines):
            return
        raw = self.epub_lines[line].raw_line_number
        pos = next((start for g, _, start in self.vertical_pieces.get(raw, []) if g == line), 0)
        self.vertical.show_raw(raw, pos)
        self.vertical_synced = line

    def vertical_to_file(self):
        """縦書きの列カーソルの所へ、横書きの側(file_view、FILEの位置の記録・目次・^G)を合わせる。"""
        v = self.vertical
        if not v.cols:
            return
        column = v.cols[v.cursor]
        pos = next((cell.start for cell in column.cells if cell.start >= 0), 0)
        line = None
        for g, _, start in self.vertical_pieces.get(column.raw, []):
            if start <= pos:
                line = g
        if line is None:
            return
        self.set_epub_cursor(line)
        self.vertical_synced = line

    def vertical_highlights(self):
        """FGREP/EGREPの一致の字(元の行→論理行の中の範囲)。"""
        grep_on = self.grep_active or self.grep_prompt is not None or self.grep.persistent
        if not grep_on or self.grep.view is not self.file_view or not self.grep.pattern:
            return None
        offset = self.chapter_bounds(self.epub_chapter)[0] if self.epub_chapter_mode else 0
        out = {}
        for index in self.grep.matches:
            g = index + offset
            if not 0 <= g < len(self.epub_lines):
                continue
            line = self.epub_lines[g]
            for piece, pad, start in self.vertical_pieces.get(line.raw_line_number, []):
                if piece == g:
                    out.setdefault(line.raw_line_number, []).extend(
                        (a - pad + start, b - pad + start) for a, b in match_spans(line.text, self.grep.pattern,
                                                                                    self.grep.syntax))
        return out

    def render_vertical(self):
        self.sync_vertical()
        rows = self.vertical.render(self.vertical_highlights())
        self.vertical_view.set_lines([Line(text, None, attributes=runs) for text, runs in rows])
        self.vertical_view.cursor_index = self.vertical_view.top_index = 0

    def vertical_number(self, number):
        """縦書きの本文のK-番号(移動・^M・^␣)。該当しなければNone(ふつうの処理へ、横書きの側で動けば
        sync_vertical()が縦書きを合わせる)。"""
        self.sync_vertical()
        v = self.vertical
        last, self.vertical_last_number = self.vertical_last_number, number
        if number == 28:
            href = v.selected_href()
            if href is None:
                return False
            v.mark = v.cursor
            return self.follow_epub_link(href)
        if number == 36:
            v.mark = v.cursor
            self.show_transient_notice('Mark set')
            return True
        if number in (1, 2):  # j/k: 本の途中・端の横書きの範囲へも入る(C++のscrollEpubVertical())
            delta = 1 if number == 1 else -1
            edge = v.per_screen - 1 if not v.chapter_breaks and delta > 0 else 0
            from_column = v.first + edge
            if not v.step_first(delta):
                if not self.enter_epub_section_at_edge(delta):
                    return False
                return True
            if not self.enter_epub_section_between(from_column, v.first + edge, delta):
                self.vertical_to_file()
            return True
        if number == 11 and self.enter_epub_section_at_book_end():  # ^[<: 本の末尾の奥付等の最下行へ
            v.mark = v.cursor
            return True
        from_raw = v.current_raw()
        pages = (5, 6, 7, 8, 9, 10)
        result = v.command(number, repeated_page=last in pages and number in pages)
        if result is None:
            return None
        if result is False and number == 14:
            self.show_transient_notice('No mark set')
        if number in (3, 4, 5, 6, 7, 8, 9, 10) and self.maybe_enter_epub_section(from_raw, number in (3, 5, 6, 7)):
            return result
        self.vertical_to_file()
        return result

    # --- 縦書きの本の中の横書きの範囲(奥付等)・注釈の区画 ---
    def epub_horizontal_range(self, raw):
        return next((r for r in self.epub.hints.horizontal if r[0] <= raw <= r[1]), None)

    def epub_line_of_raw(self, raw):
        """元の行rawの(無ければその後の)最初の本全体の行。"""
        return next((g for g, line in enumerate(self.epub_lines) if line.raw_line_number >= raw),
                    len(self.epub_lines) - 1)

    def enter_epub_section(self, section, raw, from_below=False, from_above=False):
        """横書きの範囲へ入る(C++のenterEpubHorizontal()): カーソルをrawの行に置き、範囲の最初の行を画面の一番上に
        (範囲の途中/最後へ入る時はその行が見える所まで)。from_below: 縦書きから先へ送って入る時、範囲の最初の行を
        画面の一番下に置く。from_above: 前へ送って入る時、範囲の最後の行を画面の一番上に置く。"""
        self.epub_section = section
        raw = max(section[0], min(raw, section[1]))
        line = self.epub_line_of_raw(raw)
        self.set_epub_cursor(line)
        view, offset = self.file_view, line - self.file_view.cursor_index
        start = self.epub_line_of_raw(section[0]) - offset
        index = view.cursor_index
        view.top_index = max(start, index - max(1, view.height) + 1)
        self.epub_from_below = from_below and raw == section[0]
        if self.epub_from_below:
            view.top_index = max(0, start - max(1, view.height) + 1)
        if from_above and raw == section[1]:
            view.top_index = index
        self.epub_section_line = self.global_epub_line()

    def maybe_enter_epub_section(self, from_raw, forward):
        """縦書きで先へ/前へ動いた後: 動く前の列と今の列の間に横書きの範囲があれば(本の端でそれ以上動けない時は
        その先にあれば)、そこへ入る(C++のmaybeEnterEpubHorizontal())。"""
        v = self.vertical
        now = v.current_raw()

        def no_text_beyond(d):
            c = v.cursor + d
            while 0 <= c < len(v.cols):
                if not v.cols[c].padding:
                    return False
                c += d
            return True
        ranges = self.epub.hints.horizontal
        if forward:
            for r in ranges:
                if r[0] > from_raw and (now >= r[0] or no_text_beyond(1)):
                    self.enter_epub_section(r, r[0], from_below=True)
                    return True
        else:
            for r in reversed(ranges):
                if r[1] < from_raw and (now <= r[1] or no_text_beyond(-1)):
                    self.enter_epub_section(r, r[1])
                    return True
        return False

    def enter_epub_section_between(self, from_column, to_column, delta):
        """j/kで列を送った時、送る前と後の列の行の間に横書きの範囲(本の途中の付録等)があれば入る。"""
        v = self.vertical
        total = len(v.cols)
        from_column, to_column = min(from_column, total - 1), min(to_column, total - 1)
        if from_column < 0 or to_column < 0:
            return False
        a, b = v.cols[from_column].raw, v.cols[to_column].raw
        for r in self.epub.hints.horizontal:
            if (delta > 0 and r[0] > a and r[1] < b) or (delta < 0 and r[1] < a and r[0] > b):
                self.enter_epub_section(r, r[0] if delta > 0 else r[1], from_below=delta > 0, from_above=delta < 0)
                return True
        return False

    def enter_epub_section_at_edge(self, delta):
        """縦書きの列の端で、その先(前)に横書きの範囲があればそこへ入る(右端の列の行と比べる)。"""
        v = self.vertical
        if not 0 <= v.first < len(v.cols):
            return False
        edge = v.cols[v.first].raw
        ranges = self.epub.hints.horizontal
        target = next((r for r in ranges if r[0] > edge), None) if delta > 0 else \
            next((r for r in reversed(ranges) if r[1] < edge), None)
        if target is None:
            return False
        self.enter_epub_section(target, target[0] if delta > 0 else target[1], from_below=delta > 0)
        return True

    def enter_epub_section_at_book_end(self):
        """^[<: 縦書きの本文の最後より後ろに横書きの範囲(奥付等)があれば、その最下行(画面の一番下)へ。"""
        ranges = self.epub.hints.horizontal
        last_raw = max((c.raw for c in self.vertical.cols if not c.padding and c.cells), default=0)
        if not ranges or ranges[-1][0] <= last_raw:
            return False
        r = ranges[-1]
        self.enter_epub_section(r, r[1])
        view = self.file_view
        last = max((i for i, l in enumerate(view.lines) if r[0] <= l.raw_line_number <= r[1]), default=None)
        start = min((i for i, l in enumerate(view.lines) if r[0] <= l.raw_line_number <= r[1]), default=0)
        if last is not None:
            view.cursor_index = last
            view.top_index = max(start, last - max(1, view.height) + 1)
        self.epub_section_line = self.global_epub_line()
        return True

    def update_epub_section(self):
        """描く前に: 横書きの側で横書きの範囲の行へ動いていれば(目次・リンク・検索・開いた位置)範囲に入り、範囲を
        見ている間は範囲の外へ出たら縦書きへ戻る(C++のshowEpubVerticalRaw()・leaveEpubHorizontalIfOutside())。"""
        if self.epub_note_active() or not self.epub_lines:
            return
        line = self.global_epub_line()
        if self.epub_section is not None and line != self.epub_section_line and \
                self.last_key_number not in range(1, 13):
            self.epub_section = None  # 目次・リンク等で飛んだ(移動のキーではない)
            self.vertical_synced = None
        if self.epub_section is None:
            if line != self.vertical_synced:
                section = self.epub_horizontal_range(self.epub_lines[line].raw_line_number)
                if section is not None:
                    self.enter_epub_section(section, self.epub_lines[line].raw_line_number)
            return
        self.leave_epub_section_if_outside()
        if self.epub_section is not None:
            self.epub_section_line = self.global_epub_line()

    def leave_epub_section_if_outside(self):
        """横書きの範囲で、範囲の外へ出たら縦書きへ戻る(前へ出たら範囲の直前の行の最後の列、先へ出たらその後の行)。
        j/k等の画面を送るキーは範囲が画面に見えているかで、↑↓・ページ送りはカーソルの行で決める。"""
        section, view = self.epub_section, self.file_view
        lines, number = view.lines, self.last_key_number
        scrolling = number in (1, 2)
        by_visibility = scrolling or number not in (3, 4, 5, 6, 7, 8, 9, 10)
        if not lines:
            return
        raw = lines[view.cursor_index].raw_line_number
        top = view.top_index
        top_raw = lines[top].raw_line_number if 0 <= top < len(lines) else raw
        height = max(1, view.height)
        first_index = next((i for i, l in enumerate(lines) if l.raw_line_number >= section[0]), len(lines))
        if top >= first_index:
            self.epub_from_below = False
        last_index = next((i for i in range(len(lines) - 1, -1, -1) if lines[i].raw_line_number <= section[1]), -1)
        if by_visibility:
            before, after = first_index >= top + height, last_index < top
        else:
            before = raw < section[0] or (not self.epub_from_below and top_raw < section[0])
            after = raw > section[1]
        if not before and not after:
            return
        if before:
            raw = section[0] - 1
        elif scrolling or (by_visibility and raw <= section[1] + height):
            raw = section[1] + 1
        self.epub_section = None
        other = self.epub_horizontal_range(raw)
        if other is not None:
            self.enter_epub_section(other, raw)
            return
        self.vertical_synced = None  # 組み直しは幅等が変わった時だけ(sync_vertical())
        self.set_epub_cursor(self.epub_line_of_raw(max(1, raw)))
        self.sync_vertical()
        v = self.vertical
        if before:
            col = max((c for c, column in enumerate(v.cols) if not column.padding and column.raw <= raw), default=0)
            after_columns = any(not column.padding and column.raw > section[1] for column in v.cols)
            if v.chapter_breaks or not after_columns:
                v.set_view(col, col)
            else:
                v.set_view(col - v.per_screen + 1, col)
        elif raw == section[1] + 1 and not v.chapter_breaks:
            col = next((c for c, column in enumerate(v.cols) if not column.padding and column.raw >= raw), None)
            if col is not None:
                v.set_view(col, col)
        self.vertical_to_file()

    def epub_note_active(self):
        if self.epub_note is None:
            return False
        if self.file_view.lines is self.epub_note['shown'] and self.in_epub_body():
            return True
        self.epub_note = None
        return False

    def show_epub_note(self, raw):
        """元の行rawが注釈の区画(入れ子なら一番狭い物)の中なら、file_viewをその注釈の行だけにする(縦書きの本では
        横書きの範囲として横書きで見せる)。^Gで元の所へ(C++のshowEpubNoteAt())。"""
        notes = [r for r in self.epub.hints.notes if r[0] <= raw <= r[1]]
        if not notes:
            return False
        first, until = min(notes, key=lambda r: r[1] - r[0])
        note = [l for l in self.epub_lines if first <= l.raw_line_number <= until]
        while note and not note[-1].text.strip(' '):
            note.pop()  # 注釈の後ろの空行
        if not note:
            return False
        if self.epub_note_active():
            origin = self.epub_note
            self.leave_epub_note()
        else:
            v = self.vertical if self.vertical_active() else None
            origin = {'lines': self.file_view.lines, 'top': self.file_view.top_index,
                      'cursor': self.file_view.cursor_index, 'section': self.epub_section,
                      'vertical': (v.first, v.cursor) if v is not None else None, 'chapter': self.epub_chapter}
        vertical = self.epub.right_to_left and self.epub_horizontal_range(first) is None
        if self.epub.right_to_left and not vertical:
            self.epub_section = (first, until)  # 注釈が横書きの範囲の中
        self.file_view.set_lines(note)
        self.file_view.cursor_index = self.file_view.top_index = 0
        origin['shown'] = self.file_view.lines
        self.epub_note = origin
        if vertical:
            # 縦書きの本: 注釈の行だけで縦書きの列を組み、縦書きのまま見せる(C++と同じ、以前は横書き)。
            # 本文の列は覚えておき、leave_epub_note()で戻す。
            self.epub_section = None
            origin['vertical_book'] = (self.vertical, self.vertical_key, self.vertical_source, self.vertical_synced)
            self.sync_vertical()
            sources = [x for x in self.epub_logical_lines()[0]
                       if first <= x.raw <= until and self.epub_horizontal_range(x.raw) is None]
            hints = self.epub.hints
            self.vertical = ev.VerticalBook(sources, set(), self.epub_vertical_rows(), self.display.columns,
                                            self.epub_chapter_mode, hints.centered, hints.horizontal)
            self.vertical.body_total = self.epub_lines[-1].raw_line_number or 0 if self.epub_lines else 0
            self.vertical_synced = self.global_epub_line()
        return True

    def leave_epub_note(self):
        """注釈から本文の行と、注釈へ飛ぶ前の位置へ。"""
        note, self.epub_note = self.epub_note, None
        if 'vertical_book' in note:  # 注釈を縦書きで見せていた: 本文の列に戻す
            self.vertical, self.vertical_key, self.vertical_source, self.vertical_synced = note['vertical_book']
        self.file_view.set_lines(note['lines'])
        self.epub_chapter = note['chapter']
        self.file_view.cursor_index, self.file_view.top_index = note['cursor'], note['top']
        self.epub_section = note['section']
        self.epub_section_line = self.global_epub_line()
        if note['vertical'] is not None and self.vertical_active():
            self.sync_vertical()
            self.vertical.set_view(*note['vertical'])
            self.vertical_synced = self.global_epub_line()
        return True

    def feed_vertical_copy_key(self, byte, special):
        """縦書きのCOPYのキー。^N/←で次の列(左)、^P/→で前の列、^F/↓で下のマス、^B/↑で上のマス、^A/^Eで
        列の頭/末、^V/^[vで1画面先/前。^@で範囲を始め、もう一度で閉じてRecord?(y/n)(yで記録)。^[wで
        クリップボードへ、^[tでTOOLへ、^Gでやめる。"""
        v = self.vertical
        if v.asking:  # Record?(y/n): yで記録、^[w・^[tは範囲をコピー(C++と同じ)、他はやめる
            if special is None and byte == ESC and self.vertical_prefix is None:
                self.vertical_prefix = 'ESC'
                return True
            esc, self.vertical_prefix = self.vertical_prefix == 'ESC', None
            v.asking = False
            if special is None and byte in (ord('y'), ord('Y')):
                return self.record_vertical_selection()
            if not (esc and special is None and byte in (ord('w'), ord('t'))):
                return True
            self.vertical_prefix = 'ESC'
        if special is not None:
            columns, cells = {'Up': (0, -1), 'Down': (0, 1), 'Left': (1, 0), 'Right': (-1, 0),
                              'PageUp': (-v.per_screen, 0), 'PageDown': (v.per_screen, 0)}[special]
            v.move_point(columns, cells)
            return True
        prefix, self.vertical_prefix = self.vertical_prefix, None
        if prefix == 'ESC':
            if byte in (ord('w'), ord('t')):
                text = v.selection_text(' ' if byte == ord('t') else '\n')
                v.end_select()
                if not text:
                    self.show_transient_notice('No string')
                    return False
                return self.deliver_copied_text(text, byte == ord('t'))
            if byte == ord('v'):
                v.move_point(-v.per_screen, 0)
            return True
        moves = {0x0E: (1, 0), 0x10: (-1, 0), 0x06: (0, 1), 0x02: (0, -1), 0x16: (v.per_screen, 0)}
        if byte == ESC:
            self.vertical_prefix = 'ESC'
        elif byte in (0, CTRL_SPACE):
            v.toggle_anchor()
        elif byte == CTRL_G:
            v.end_select()
        elif byte in moves:
            v.move_point(*moves[byte])
        elif byte in (0x01, 0x05):
            v.point_to_column_edge(byte == 0x05)
        return True

    def record_vertical_selection(self):
        """範囲の字を~/.usilic/epub-marks.txtへ「本.epub:章のパス:位置: 文字列」で記録する(位置は章の頭からの
        字数、C++のrecordEpubSelection()と同じ書式、^O^Eの一覧で見る)。"""
        v = self.vertical
        ranges = v.selection_ranges()
        text = v.selection_text()
        v.end_select()
        if not text or not ranges:
            self.show_transient_notice('No string')
            return True
        raw, pos = ranges[0][0], ranges[0][1]
        chapter = None
        for start, href in self.epub.chapter_files:
            if start <= raw:
                chapter = (start, href)
        if chapter is None:
            self.show_transient_notice('No chapter')
            return True
        path = self.epub_marks_path()
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'a', encoding='utf-8') as f:
                f.write('%s:%s:%d: %s\n' % (os.path.basename(self.epub.path), chapter[1],
                                            v.offset_in_chapter(chapter[0], raw, pos), text))
        except OSError:
            self.show_transient_notice('Cannot write ' + path)
            return True
        self.show_transient_notice('Recorded: ' + text[:20])
        return True

    # --- マウス(C++のapp/main.cppのrunTerminalPhase()・runTerminalMouseDrag()・runCoastLoop()) ---
    def sync_mouse_tracking(self):
        """SHELL以外ではマウスを追う(SHELLでは端末の選択・貼り付けをそのまま使えるよう追わない)。"""
        want = self.current_mode != SHELL
        if hasattr(self.display, 'set_mouse_tracking') and getattr(self.display, 'mouse_on', None) != want:
            self.display.set_mouse_tracking(want)

    def mouse_view(self):
        """マウスが相手にする画面(縦書きは描いた画面ではなく列の並びなので別に扱う)。"""
        return self.active_view()

    def vertical_scroll_active(self):
        """縦書きの本文・一覧(ホイール・ドラッグ・慣性は列ごと)。"""
        return self.prompt is None and ((self.current_mode == FILE and self.vertical_active())
                                        or self.vertical_list_active())

    def copy_mouse_active(self):
        return self.copy is not None or self.vertical_selecting()

    def handle_mouse(self, event):
        """マウスの報告1つ。慣性中・ドラッグ中はその続き。"""
        self.notice = None
        if self.coast is not None:
            return self.mouse_during_coast(event)
        if self.drag is not None:
            return self.mouse_during_drag(event)
        if event.wheel:
            return self.scroll_by_wheel(event.wheel)
        if event.pressed and not event.motion and event.button == 2:  # 右クリック: ^G
            return self.feed_key(CTRL_G, None)
        if event.pressed and not event.motion and event.button == 0:
            return self.start_mouse_drag(event)
        if event.motion and event.button == 3 and self.prompt is None and self.grep_prompt is None:
            return self.hover(event.row)
        return False

    def hover(self, row):
        """ボタンを押していない動き: ポインタの行にカーソルを合わせる(C++のapplyHoverRowAndRedraw())。"""
        self.click_targets_cursor = False
        if self.vertical_scroll_active() or self.copy_mouse_active() or self.current_mode == SHELL:
            return False
        view = self.mouse_view()
        index = view.line_index_at_row(row)
        if index < 0 or index == view.cursor_index:
            return False
        view.cursor_index = index
        return True

    def scroll_by_wheel(self, direction, lines=1):
        """ホイールの1刻み: j/kと同じく1行(縦書きは1列、縦書きの一覧は1列)。端では何もしない。"""
        if self.wheel_reversed:
            direction = -direction
        if self.vertical_scroll_active():
            for _ in range(lines):
                if not self.scroll_epub_vertical(direction):
                    break
            return True
        view = self.mouse_view()
        for _ in range(lines):
            if direction < 0:
                if view.top_index <= 0:
                    break
                self.handle_key_number(2, None)
            else:
                if self.current_mode == FILE and self.file_mode.has_active_stream():
                    self.file_mode.ensure_loaded_through(view.top_index + 2 * view.height)
                if view.top_index >= view.max_top():
                    break
                self.handle_key_number(1, None)
        return True

    def scroll_epub_vertical(self, delta):
        """縦書きを1列ずらす(ホイール・ドラッグ・慣性、C++のscrollEpubVertical())。動いたらTrue。"""
        if self.vertical_list_active():
            v = self.vlist
            total = len(v.cols)
            first = max(0, min(v.first + delta, max(0, total - v.per_screen)))
            if first == v.first or total == 0:
                return False
            self.vlist_first[self.current_mode] = first
            col = min(first + v.per_screen, total) - 1 if delta > 0 else first
            source = self.vlist_source()
            source.cursor_index = max(0, min(v.cols[col].raw - 1, len(source.lines) - 1))
            self.render_vertical_list(keep_first=True)
            return True
        if not (self.current_mode == FILE and self.vertical_active()):
            return False
        before = (self.vertical.first, self.epub_section)
        self.last_key_number = 1 if delta > 0 else 2  # 画面を送る操作(j/k)として
        self.vertical_number(1 if delta > 0 else 2)
        return (self.vertical.first, self.epub_section) != before

    # --- 左ボタン(ドラッグ・クリック) ---
    def start_mouse_drag(self, event):
        if self.copy_mouse_active():
            self.copy_mouse_down(event.row, event.column)
            self.drag = {'copy': True}
            return True
        self.drag = {'copy': False, 'row': event.row, 'column': event.column, 'moved': False, 'samples': [],
                     'columns': self.vertical_scroll_active(), 'accum': 0,
                     'targets_cursor': self.click_targets_cursor}
        self.click_targets_cursor = False
        return True

    def mouse_during_drag(self, event):
        d = self.drag
        if event.wheel or event.button < 0:
            return False  # ドラッグ中のホイールは無視
        if d['copy']:
            if event.motion:
                self.copy_mouse_drag(event.row, event.column)
            elif not event.pressed:
                self.drag = None
                self.copy_mouse_up()
            return True
        now = time.monotonic()
        if event.motion:
            if d['columns']:
                d['accum'] += event.column - d['column']
                d['column'] = event.column
                while abs(d['accum']) >= ev.PITCH:
                    direction = 1 if d['accum'] > 0 else -1
                    d['accum'] -= direction * ev.PITCH
                    d['moved'] = True
                    d['samples'].append((now, direction))
                    if not self.scroll_epub_vertical(direction):
                        d['accum'] = 0
                    if not self.vertical_scroll_active():  # 横書きの範囲へ入った: 上下のドラッグとして続ける
                        d.update(columns=False, row=event.row, samples=[])
                        break
                return True
            delta = event.row - d['row']
            d['row'] = event.row
            if delta:
                d['moved'] = True
                self.drag_rows(delta)
                d['samples'].append((now, delta))
                if self.vertical_scroll_active():  # 横書きの範囲の外へ出た: 縦書きの左右のドラッグとして続ける
                    d.update(columns=True, column=event.column, accum=0, samples=[])
            return True
        if event.pressed:
            return False  # 押したままもう一度押した
        self.drag = None
        if not d['moved']:
            view = self.mouse_view()
            if d['targets_cursor'] and not d['columns']:
                row = view.cursor_index - view.top_index
                if 0 <= row < view.height:
                    return self.handle_mouse_click(row, 0)
            return self.handle_mouse_click(event.row, event.column)
        recent = sum(v for t, v in d['samples'] if t >= now - 0.15)  # 離す直前(150ms)の動き
        level = level_for_release_distance(recent)
        if level == 0 or (d['columns'] and not self.vertical_scroll_active()):
            return True
        # 上下は自然なスクロール(下へ引くと前が見える)、左右は引いた向きへ列を送る
        direction = (1 if recent > 0 else -1) if d['columns'] else (-1 if recent > 0 else 1)
        return self.start_coast(level, direction, from_drag=True)

    def drag_rows(self, delta):
        """上下のドラッグ: 画面を指の向きに送り(下へ引くと前が見える)、カーソルは送る向きの端の行。"""
        view = self.mouse_view()
        before = view.top_index
        if self.current_mode == FILE and self.file_mode.has_active_stream():
            self.file_mode.ensure_loaded_through(before - delta + 2 * view.height)
        view.top_index = max(0, min(before - delta, view.max_top()))
        view.cursor_index = min(view.top_index + view.height - 1, len(view.lines) - 1) if delta < 0 else \
            view.top_index
        self.last_key_number = 1 if delta < 0 else 2  # 横書きの範囲の出入りは画面を送る操作(j/k)として
        self.update_scrolled_section()

    def update_scrolled_section(self):
        """画面を送った後に、縦書きの本の横書きの範囲から出たか(描く前に見る、update_epub_section())。"""
        if self.current_mode == FILE and self.in_epub_body() and self.epub.right_to_left:
            self.epub_section_line = self.global_epub_line() if self.epub_section is not None else None
            self.update_epub_section()

    def handle_mouse_click(self, row, column):
        """クリック(C++のhandleMouseClickImpl()): 一覧はその行を選び、選んでいる行ならそれを開く。縦書きは列を選び、
        選んでいる列のリンク・リンクの字ならリンクへ。横書きのEPUBはリンクの字ならリンクへ。"""
        if self.vertical_selecting() or self.prompt is not None or self.grep_prompt is not None:
            return True
        mode = self.current_mode
        if mode == FILE and self.vertical_active():
            return self.click_epub_vertical(row, column)
        if self.vertical_list_active():
            return self.click_vertical_list(column)
        if mode == FILE and self.in_epub_body():
            index = self.file_view.line_index_at_row(row)
            return index >= 0 and self.click_epub_horizontal_link(index, column)
        if mode in (DIRECTORY, DU, ARCHIVE, LOCATE, LIST, TOC, PART_TOC, OCCUR, INFO):
            view = self.mouse_view()
            index = view.line_index_at_row(row)
            if index < 0:
                return False
            if index == view.cursor_index:
                return self.enter_key()
            view.cursor_index = index
            return True
        return False

    def click_epub_vertical(self, row, column):
        """縦書きの本文: リンクの字ならそのリンクへ。選んでいる列なら(リンク・画像の列は)開き、他の列は列カーソルを
        動かすだけ(C++のclickEpubVertical())。"""
        self.sync_vertical()
        v = self.vertical
        row -= self.epub_vertical_margin_top()  # 上の余白
        k = ev.column_at(column, self.display.columns)
        c = v.first + k
        if k < 0 or c >= len(v.cols):
            return False
        cells = v.cols[c].cells
        if 0 <= row < len(cells) and cells[row].link >= 0:
            v.set_view(v.first, c, cells[row].link)
            self.vertical_to_file()
            return self.vertical_number(28)
        if c == v.cursor:
            return self.vertical_number(28) if v.selected_href() is not None else True
        v.set_view(v.first, c)
        self.vertical_to_file()
        return True

    def click_epub_horizontal_link(self, index, column):
        """横書きのEPUBの本文: 下線(リンク)の字のクリックはそのリンクへ。"""
        line = self.file_view.lines[index]
        from .copy_mode import index_at_column
        pos = index_at_column(line.text, column)
        if pos >= len(line.text):
            return False
        for start, end, href in line.links:
            if start <= pos < end:
                self.file_view.cursor_index = index
                return self.follow_epub_link(href)
        return False

    # --- COPYのドラッグ(C++のcopyMouseDown()・copyMouseDrag()・copyMouseUp()) ---
    def copy_mouse_down(self, row, column):
        if self.vertical_selecting():  # 縦書きのCOPY: 押した所から範囲を始め直す
            v = self.vertical
            v.asking = False
            v.anchor = v.point = v.cell_at(max(0, row - self.epub_vertical_margin_top()), column)
            return
        view = self.copy.view
        index = view.line_index_at_row(row)
        if index < 0:  # 最後の行より下の余白は最後の行へ
            index = len(view.lines) - 1
        if index >= 0:
            self.copy.begin_or_extend_from_mouse(index, column)

    def copy_mouse_drag(self, row, column):
        if self.vertical_selecting():
            v = self.vertical
            if v.anchor is not None:
                v.point = v.cell_at(max(0, row - self.epub_vertical_margin_top()), column)
            return
        view = self.copy.view
        index = view.line_index_at_row(row)
        if index < 0:
            index = len(view.lines) - 1
        if index >= 0:
            self.copy.place_cursor_at(index, column)

    def copy_mouse_up(self):
        """縦書きのCOPY: 範囲があれば(押した所から動かしていれば)Record?(y/n)と問う。横書きは範囲を残すだけ。"""
        if self.vertical_selecting():
            v = self.vertical
            if v.anchor is None or v.anchor == v.point:
                v.anchor = None
            else:
                v.asking = True

    # --- 慣性 ---
    def start_coast(self, level, direction, from_drag=False):
        level = min(level + (self.speeds.drag_offset() if from_drag else 0), self.speeds.max_level())
        self.coast = {'level': max(1, level), 'direction': direction, 'columns': self.vertical_scroll_active(),
                      'due': time.monotonic() + self.speeds.seconds_per_row(max(1, level))}
        return True

    def start_coast_by_arrow(self, special):
        """←→(縦書きの本文は↓↑)で慣性を始める(段1、→・↓で先へ)。縦書きの一覧・SHELL・COPY・入力中は今までどおり。"""
        if self.current_mode == SHELL or self.copy_mouse_active() or self.prompt is not None or \
                self.grep_prompt is not None or self.grep_active or self.pending_keys or \
                self.goto_digits is not None or self.register_state is not None:
            return False
        vertical_body = self.current_mode == FILE and self.vertical_active()
        trigger = ('Down', 'Up') if vertical_body else () if self.vertical_list_active() else ('Right', 'Left')
        if special not in trigger:
            return False
        horizontal_epub = self.current_mode == FILE and self.in_epub_body() and not vertical_body
        forward = special == 'Down' or special == ('Left' if horizontal_epub else 'Right')
        return self.start_coast(1, 1 if forward else -1)

    def adjust_coast_by_arrow(self, special):
        c = self.coast
        if c['columns']:
            forward = special in ('Down', 'Left')
        elif self.current_mode == FILE and self.in_epub_body():
            forward = special in ('Left', 'Up')
        else:
            forward = special in ('Right', 'Up')
        c['level'], c['direction'] = step_level_by_key(c['level'], c['direction'], 1 if forward else -1,
                                                       self.speeds.max_level())
        return True

    def mouse_during_coast(self, event):
        """慣性中のマウス: ホイールは止めて1刻み、右クリックは止めて^G、クリックは止めるだけ(行の字の上なら、
        止めてハイライト行をクリックしたことに。次のクリックはハイライト行へ)。動き・離すは何もしない。"""
        if event.wheel:
            self.coast = None
            return self.scroll_by_wheel(event.wheel)
        if event.pressed and not event.motion and event.button == 2:
            self.coast = None
            return self.feed_key(CTRL_G, None)
        if event.pressed and not event.motion:
            columns = self.coast['columns']
            self.coast = None
            if columns:
                return True
            view = self.mouse_view()
            index = view.line_index_at_row(event.row)
            on_text = index >= 0 and utf8.text_width(view.lines[index].text) > event.column
            if on_text:
                row = view.cursor_index - view.top_index
                if 0 <= row < view.height:
                    return self.handle_mouse_click(row, 0)
            else:
                self.click_targets_cursor = True
            return True
        return False

    def coast_step(self):
        """慣性で1行(縦書きは1列)進める。端で止まる。"""
        c = self.coast
        if c['columns']:
            if not self.scroll_epub_vertical(c['direction']):
                self.coast = None
                return
            if not self.vertical_scroll_active():  # 横書きの範囲へ入った: 上下の慣性として続ける
                c['columns'] = False
        else:
            view = self.mouse_view()
            before = view.top_index
            if self.current_mode == FILE and self.file_mode.has_active_stream():
                self.file_mode.ensure_loaded_through(before + 2 * view.height)
            view.top_index = max(0, min(before + c['direction'], view.max_top()))
            if view.top_index == before:
                self.coast = None
                self.redraw()
                return
            view.cursor_index = min(view.top_index + view.height - 1, len(view.lines) - 1) \
                if c['direction'] > 0 else view.top_index
            self.last_key_number = 1 if c['direction'] > 0 else 2
            self.update_scrolled_section()
            if self.vertical_scroll_active():  # 横書きの範囲から出た: 縦書きの左右の慣性として続ける
                c['columns'] = True
        c['due'] = time.monotonic() + self.speeds.seconds_per_row(c['level'])
        self.redraw()

    # --- ファイルのドロップ(括弧付き貼り付け) ---
    def handle_paste(self, text):
        """貼り付けた字がファイルのパスなら開く(1つなら^Xfと同じ、いくつもならUSILICの一覧)。違えば、またはプロンプト・
        SHELLの中なら、打った字として渡す。"""
        paths = dropped_paths(text)
        if paths and self.current_mode != SHELL and self.prompt is None and self.grep_prompt is None:
            if len(paths) == 1:
                return self.open_path(paths[0])
            if not self.show_list(paths, 'USILIC', self.open_usilic_item, False):
                return False
            self.list_view.cursor_index = self.list_view.top_index = 0  # 最初のファイルを開く(^Gで一覧へ)
            return self.enter_key()
        for b in text.encode('utf-8'):
            self.feed_key(b, None)
        return True

    # --- 縦書きの章の一覧・検索結果の一覧(C++のrefreshEpubTocVertical()・handleEpubTocVerticalCommand()) ---
    def vertical_list_active(self):
        """縦書きの本の章の一覧(TOC)か、縦書きの本文から作った一致の一覧・記録の一覧(OCCUR)を見ている。"""
        if self.epub is None or not self.epub.right_to_left or self.file_mode.current_file != self.epub.path:
            return False
        if self.current_mode == TOC:
            return True
        return self.current_mode == OCCUR and self.occur_origin == FILE

    def vlist_source(self):
        return self.toc_view if self.current_mode == TOC else self.occur_view

    def vlist_cursor_column(self):
        source = self.vlist_source()
        return next((c for c, col in enumerate(self.vlist.cols) if col.raw == source.cursor_index + 1), 0)

    def render_vertical_list(self, keep_first=False):
        """一覧の1行を1つの論理行として本文と同じ組版で列に並べ、選んでいる行の最初の列を反転表示する。選んでいる
        行が画面の外なら右端の列を動かす(keep_first: 動かさない、ホイール等の1列送り)。"""
        source = self.vlist_source()
        key = (id(source.lines), len(source.lines), self.epub_vertical_rows(), self.display.columns)
        if self.vlist is None or self.vlist_key != key:
            sources = []
            for i, line in enumerate(source.lines):
                runs = list(line.attributes)
                if self.current_mode == OCCUR and not self.occur_global:  # 一致した字の色
                    runs = [(a, b, 'match') for a, b in match_spans(line.text, self.grep.pattern,
                                                                    self.occur_syntax)] + runs
                pad = len(line.text) - len(line.text.lstrip(' '))
                sources.append(ev.SourceLine(line.text.strip(' '), i + 1,
                                             [(max(0, a - pad), max(0, b - pad), t) for a, b, t in runs]))
            self.vlist = ev.VerticalBook(sources, set(), self.epub_vertical_rows(), self.display.columns, False)
            self.vlist_key = key
        v = self.vlist
        cursor = self.vlist_cursor_column()
        first = self.vlist_first.get(self.current_mode, 0)
        if not keep_first and cursor < first:
            first = cursor
        if not keep_first and cursor >= first + v.per_screen:
            first = cursor - v.per_screen + 1
        v.first = max(0, min(first, max(0, len(v.cols) - v.per_screen)))
        self.vlist_first[self.current_mode] = v.first
        rows = v.render(highlight_column=cursor)
        self.vlist_view.set_lines([Line(text, None, attributes=runs) for text, runs in rows])
        self.vlist_view.cursor_index = self.vlist_view.top_index = 0

    def select_vertical_list(self, index):
        source = self.vlist_source()
        source.cursor_index = max(0, min(index, len(source.lines) - 1))
        source.ensure_cursor_visible()
        return True

    def vertical_list_number(self, number):
        """縦書きの一覧のK-番号: ^N・j・←で次、^P・k・→で前、ページ送り、^[>で先頭・^[<で末尾(本文と同じく逆)。"""
        source = self.vlist_source()
        if self.vlist is None:
            self.render_vertical_list()
        v, index, count = self.vlist, source.cursor_index, len(source.lines)

        def at_column(c):
            if not v.cols:
                return 0
            return v.cols[max(0, min(c, len(v.cols) - 1))].raw - 1
        cursor = self.vlist_cursor_column()
        wrap = self.current_mode == OCCUR  # 縦書きの一致の一覧の^N/^Pは巡回(章の一覧は端で止まる、C++と同じ)
        if number == 3 and wrap and index >= count - 1:
            return self.select_vertical_list(0)
        if number == 4 and wrap and index <= 0:
            return self.select_vertical_list(count - 1)
        if number in (1, 3):
            return self.select_vertical_list(index + 1)
        if number in (2, 4):
            return self.select_vertical_list(index - 1)
        if number in (5, 6, 7):
            return self.select_vertical_list(max(index + 1, at_column(cursor + v.per_screen)))
        if number in (8, 9, 10):
            return self.select_vertical_list(min(index - 1, at_column(cursor - v.per_screen)))
        if number == 12:
            return self.select_vertical_list(0)
        if number == 11:
            return self.select_vertical_list(count - 1)
        return None

    def click_vertical_list(self, column):
        """縦書きの一覧のクリック: その列の行を選び、選んでいる行なら開く。"""
        if self.vlist is None:
            self.render_vertical_list()
        k = ev.column_at(column, self.display.columns)
        c = self.vlist.first + k
        if k < 0 or c >= len(self.vlist.cols):
            return False
        index = self.vlist.cols[c].raw - 1
        if index == self.vlist_source().cursor_index:
            return self.enter_key()
        return self.select_vertical_list(index)

    # --- 実行ループ(main.cppのrunTerminalPhase()) ---
    def run(self, resized):
        self.layout()
        self.redraw()
        while not self.wants_exit:
            self.sync_mouse_tracking()
            fds = [self.key_input.fd] + ([self.shell.fd] if self.shell.alive() else [])
            wait = 0.1
            if self.coast is not None:  # 慣性: 次の1行(1列)の時刻まで
                wait = max(0.0, min(wait, self.coast['due'] - time.monotonic()))
            try:
                readable, _, _ = select.select(fds, [], [], 0 if self.key_input.pending else wait)
            except InterruptedError:
                readable = []
            if self.coast is not None and time.monotonic() >= self.coast['due'] and not self.key_input.pending \
                    and self.key_input.fd not in readable:
                self.coast_step()
                continue
            if resized():
                self.layout()
                self.redraw()
            if self.shell.alive() and self.shell.fd in readable:
                if not self.shell.on_pty_readable():
                    self.end_shell_session()  # bashが終わった(C++のhandleShellChildExited())
                    if self.current_mode == SHELL:
                        self.go_back()
                self.check_shell_run()
                self.poll_shell_offer()
                if self.current_mode == SHELL:
                    self.refresh_shell_view()
                self.redraw()
            key = self.key_input.read_key(timeout=0) if self.key_input.fd in readable or self.key_input.pending else None
            if key is None:
                self.poll_prefix_echo()
                if self.poll_children():
                    self.redraw()
                continue
            self.prefix_shown = False
            self.feed_key(*key)
            self.redraw()
        if getattr(self.display, 'mouse_on', False):
            self.display.set_mouse_tracking(False)
        if self.parent_pid <= 0:
            self.channel.terminate_children()  # 子のHELP・KEY等も終わらせる(C++のterminateChildren())
        self.channel.close()
        self.shell.terminate()
        while len(self.archives) > 1:  # 入れ子のアーカイブの一時ファイル
            self.archive_back()
        if os.path.isdir(self.work_root):  # このセッションの作業ファイル(WORK)
            import shutil
            shutil.rmtree(self.work_root, ignore_errors=True)

    def poll_prefix_echo(self):
        """Application::pollPrefixEcho(): PREFIXのまま1秒経ったらモードラインへ。"""
        label = self.pending_prefix_label()
        if not label:
            self.prefix_since = None
            return
        now = time.monotonic()
        if self.prefix_since is None:
            self.prefix_since = now
        elif not self.prefix_shown and now - self.prefix_since >= 1.0:
            self.prefix_shown = True
            self.notice = label
            self.redraw()
