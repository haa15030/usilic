"""文字列の一覧(C++のlist_browser_mode.cppのListBrowserMode)。

履歴の一覧(^X^F・^X^D・^J^M・^J^A・^J^E、プロンプトの^R)に使う。行は「項目  (バッジ)」。^Rでソートして重複を
除いた一覧と入力順の一覧を切り換える(HELPのHISTORYの章)。
"""
from .content_view import Line


class ListBrowserMode:
    def __init__(self, view):
        self.view = view
        self.source = []        # 入力順(古い順)
        self.items = []         # 今の並び
        self.sorted = False
        self.badge_for = None   # 項目→バッジ(「(tar)」等)の関数
        self.label = ''         # モードラインの名前(FILE・DIR・MAN等)
        self.on_choose = None   # ^Mで選んだ時(項目を受け取る)
        self.prompt_kind = None  # 字を打った時に開くプロンプト(Noneなら打った字は無視)
        self.display_for = None  # 項目→表示の文字列(Noneなら項目のまま、ブックマーク等)
        self.pinned = []         # 並びに無ければ頭に差し込む項目(^J^Eの入口、C++のaddDefaultUsilicWebUrl())

    def show(self, items, sorted_view, label, on_choose, prompt_kind=None, badge_for=None, display_for=None,
             pinned=None):
        self.source = list(items)
        self.prompt_style = True  # ^X^F・^X^Dの一覧をプロンプトの形で(C++のプロンプトの履歴の間)、戻ると普通の一覧の形
        self.pinned = list(pinned or [])
        self.label, self.on_choose, self.prompt_kind, self.badge_for = label, on_choose, prompt_kind, badge_for
        self.display_for = display_for
        self.set_sorted(sorted_view)

    def snapshot(self):
        """入れ子の一覧(^[uの目次→各一覧)を開く前の状態。"""
        return (self.source, self.items, self.sorted, self.badge_for, self.label, self.on_choose, self.prompt_kind,
                self.display_for, list(self.view.lines), self.view.cursor_index, self.view.top_index, self.pinned)

    def restore(self, snap):
        (self.source, self.items, self.sorted, self.badge_for, self.label, self.on_choose, self.prompt_kind,
         self.display_for, lines, cursor, top, self.pinned) = snap
        self.view.set_lines(lines)
        self.view.cursor_index, self.view.top_index = cursor, top
        self.view.clamp()

    def set_sorted(self, sorted_view):
        """sorted_viewならソートして重複を除き先頭に、でなければ入力順で最後(最新)にカーソル。"""
        self.sorted = sorted_view
        self.items = sorted(set(self.source)) if sorted_view else list(self.source)
        # 並べた後で、無い入口を頭に(末尾の/の有無は問わない、並べ替えた順でも入力順でも)
        self.items = [p for p in self.pinned if p not in self.items and p[:-1] not in self.items] + self.items
        lines = []
        for i, item in enumerate(self.items):
            badge = self.badge_for(item) if self.badge_for else ''
            text = self.display_for(item) if self.display_for else item
            lines.append(Line(text + ('  ' + badge if badge else ''), i + 1, ellipsis=True))
        self.view.set_lines(lines)
        self.view.cursor_index = 0 if sorted_view else max(0, len(self.items) - 1)
        self.view.top_index = 0
        self.view.clamp()
        self.view.ensure_cursor_visible()

    def toggle_sorted(self):
        """^R: ソート済みと入力順を切り換える。カーソルは入力順なら最後(最新)、ソート済みなら先頭(C++の
        toggleListBrowserHistory()と同じ、前の項目には残さない)。"""
        self.set_sorted(not self.sorted)

    def selected_item(self):
        i = self.view.cursor_index
        return self.items[i] if 0 <= i < len(self.items) else None
