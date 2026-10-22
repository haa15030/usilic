"""モードラインの位置の書式(C++のmode_line.cpp)。"""


def format(current_line, total_lines):  # noqa: A001  C++のModeLine::format()と同じ名前
    """「001/500 TOP」「250/500 50%」「500/500 END」。currentはtotalの桁数まで0で埋める。"""
    total = str(max(total_lines, 0))
    current = str(max(current_line, 0)).rjust(len(total), '0')
    # TOPより先にENDを見る(1行だけの時は「すべて見終えた」が優先)。
    if current_line >= total_lines:
        return current + '/' + total + ' END'
    if current_line <= 1:
        return current + '/' + total + ' TOP'
    percent = current_line * 100 // total_lines if total_lines > 0 else 0
    return current + '/' + total + ' ' + '%2d%%' % percent


def format_fixed_width(current_line, total_lines):
    """SHELLの「00002/00002 END」(5桁の0埋め、ModeLine::formatFixedWidth())。"""
    total = str(max(total_lines, 0)).rjust(5, '0')
    current = str(max(current_line, 0)).rjust(5, '0')
    if current_line >= total_lines:
        return current + '/' + total + ' END'
    if current_line <= 1:
        return current + '/' + total + ' TOP'
    percent = current_line * 100 // total_lines if total_lines > 0 else 0
    return current + '/' + total + ' ' + '%2d%%' % percent
