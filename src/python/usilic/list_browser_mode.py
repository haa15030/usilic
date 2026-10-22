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

    def show(self, items, sorted_view, label, on_choose, prompt_kind=None, badge_for=None):
        self.source = list(items)
        self.label, self.on_choose, self.prompt_kind, self.badge_for = label, on_choose, prompt_kind, badge_for
        self.set_sorted(sorted_view)

    def set_sorted(self, sorted_view):
        """sorted_viewならソートして重複を除き先頭に、でなければ入力順で最後(最新)にカーソル。"""
        self.sorted = sorted_view
        self.items = sorted(set(self.source)) if sorted_view else list(self.source)
        lines = []
        for i, item in enumerate(self.items):
            badge = self.badge_for(item) if self.badge_for else ''
            lines.append(Line(item + ('  ' + badge if badge else ''), i + 1))
        self.view.set_lines(lines)
        self.view.cursor_index = 0 if sorted_view else max(0, len(self.items) - 1)
        self.view.top_index = 0
        self.view.clamp()
        self.view.ensure_cursor_visible()

    def toggle_sorted(self):
        current = self.selected_item()
        self.set_sorted(not self.sorted)
        if current in self.items:  # 同じ項目にカーソルを残す
            self.view.cursor_index = (self.items.index(current) if self.sorted
                                      else len(self.items) - 1 - self.items[::-1].index(current))
            self.view.ensure_cursor_visible()

    def selected_item(self):
        i = self.view.cursor_index
        return self.items[i] if 0 <= i < len(self.items) else None
