#include "terminal_display.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <curses.h>
#ifdef __CYGWIN32__
#include <ncurses/term.h>
#else
#include <term.h>
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// <term.h>は`columns`と`lines`をマクロとして#defineしている
// (CUR Numbers[0]/[2]、terminfo自身の数値capabilityアクセサ)――
// このファイルでのそれらの識別子の、通常の変数/引数/メソッド名
// としてのそれ以降の使用を、テキスト的に壊してしまう(これが
// legacyのtc.cが、"rows"/"columns"というつづりの何かではなく
// tc_row()/tc_col()という名前で対応するアクセサを名付けた理由)。
// term.hのincludeの直後に両方をundefすることで、Display::
// columns()のオーバーライドを含む、このファイルの残りの部分が
// それらの名前を普通に使えるようにする。
#undef columns
#undef lines

namespace fm {

namespace {

// tputs()の1文字ごとの出力コールバックは(キャプチャ無しの)プレーンな
// 関数ポインタでなければならないので、「これはどのディスプレイの
// ためのものか、その出力は現在有効か」という状態はコールバック自体の
// 外側に置かねばならない。これはlegacyのtc.cを正確に反映している:
// それは同じ制約を持ち、outc()内でvt_mode('D')経由でチェックされる
// 単一のグローバル`vt`ポインタという同じ方法で解決していた。usilic
// は一度に単一の前面ディスプレイとしてしか動作したことがないので、
// これは新たな制約を持ち込まない。
TerminalDisplay *g_active = nullptr;

int outc(int c) {
  if (!g_active || !g_active->outputEnabled()) return 0;
  std::putchar(c);
  return c;
}

int windowSize(int *rowsp, int *colsp) {
  struct winsize ws;
  ws.ws_row = 0;
  int fd = open("/dev/tty", O_RDONLY);
  if (fd < 0 || ioctl(fd, TIOCGWINSZ, &ws) < 0) {
    // legacyのget_winsize()のフォールバックに(行/列の値が入れ替わって
    // 見えることも含めて)正確に一致している――これは調査済みで、実際
    // には無害であることが確認されている: 本物の端末は全て起動時に
    // TIOCSWINSZを設定するので、この経路は本質的に通られない。以前の
    // 調査についてはCLAUDE.mdのUTF-8 Step 3.5の履歴参照。
    *rowsp = 80;
    *colsp = 30;
  } else if (!ws.ws_row) {
    *rowsp = 80;
    *colsp = 30;
  } else {
    *rowsp = ws.ws_row;
    *colsp = ws.ws_col;
  }
  if (fd >= 0) ::close(fd);
  return 0;
}

std::string capOrEmpty(char *cap) { return cap ? std::string(cap) : std::string(); }

}  // namespace

TerminalDisplay::TerminalDisplay(TerminalCapabilities caps, int rows, int columns)
    : caps_(std::move(caps)), rows_(rows), columns_(columns) {
  g_active = this;
}

std::unique_ptr<TerminalDisplay> TerminalDisplay::fromEnvironment(bool exitOnFailure) {
  // exitOnFailureの完全な経緯についてはこの関数自身の宣言コメント
  // (terminal_display.hpp)参照。下の3つの失敗地点は全て同じ
  // フォールバックを共有する: 何も表示せず(呼び出し元が既にそれが
  // 実際の描画に使われないと分かっている場合、劣化したプレース
  // ホルダーはユーザー向けのエラーではない)、慣習的な80桁x24行の
  // 端末サイズの、空のcapabilitiesスタブを返す――このファイルの他所
  // にあるwindowSize()自身の80x30「実際のサイズが得られない」
  // フォールバックとは「異なる」(このコメントの以前のバージョンは
  // 誤ってそれらが一致すると主張していた): あちらはrows=80/
  // cols=30で、特に本物の端末は全て起動時にTIOCSWINSZを設定する
  // ため本質的に到達不能であることが確認された、legacyに合わせた
  // 奇妙さである――こちらの経路は到達不能では「ない」($TERM無しの
  // GUI起動に対する主要な経路である)、そしてこちら自身のrows/
  // columnsはGraphicalDisplay::create()の初期ウィンドウサイズへ
  // 直接流れ込むので、「気にしない、決して当たらない」プレース
  // ホルダーではなく、実際に正気な値が必要である。コンストラクタは
  // (caps, rows, columns)を取る――rowsが「先」――これを一度、
  // ここで逆にしてしまったことが、既に実際に報告された症状を
  // 生み出した: 全てのlsの行の描画が、権限/所有者/サイズの各列の
  // 直後で切り詰められた。ウィンドウが誤って、24行の縦幅、80桁の
  // 横幅ではなく、24桁「幅」しか無かった(rows=80, columns=24)ため。
  auto degraded = [] { return std::make_unique<TerminalDisplay>(TerminalCapabilities{}, 24, 80); };

  const char *term = std::getenv("TERM");
  if (!term) {
    if (!exitOnFailure) return degraded();
    std::fprintf(stderr, "usilic: TERM environment variable is not set.\n");
    std::exit(1);
  }
  // tc_load()のTERMオーバーライドからの移植。
  if (!std::strcmp(term, "screen")) setenv("TERM", "vt100", 1);
  else if (!std::strcmp(term, "cygwin")) setenv("TERM", "kterm", 1);

  int state = 0;
  setupterm(nullptr, 1, &state);
  if (state != 1) {
    if (!exitOnFailure) return degraded();
    std::fprintf(stderr, "usilic: Invalid terminal.\n");
    std::exit(1);
  }

  TerminalCapabilities caps;
  caps.clearScreen = capOrEmpty(clear_screen);
  caps.clearToEndOfLine = capOrEmpty(clr_eol);
  caps.cursorAddress = capOrEmpty(cursor_address);
  caps.cursorLeft = capOrEmpty(cursor_left);
  caps.standoutOn = capOrEmpty(enter_standout_mode);
  caps.standoutOff = capOrEmpty(exit_standout_mode);
  caps.underlineOn = capOrEmpty(enter_underline_mode);
  caps.underlineOff = capOrEmpty(exit_underline_mode);
  caps.boldOn = capOrEmpty(enter_bold_mode);
  caps.boldOff = capOrEmpty(exit_attribute_mode);
  caps.italicOn = capOrEmpty(enter_italics_mode);
  caps.italicOff = capOrEmpty(exit_italics_mode);
  caps.saveCursor = capOrEmpty(save_cursor);
  caps.restoreCursor = capOrEmpty(restore_cursor);
  caps.changeScrollRegion = capOrEmpty(change_scroll_region);
  caps.scrollForward = capOrEmpty(scroll_forward);
  caps.scrollReverse = capOrEmpty(scroll_reverse);
  caps.enterAltScreen = capOrEmpty(enter_ca_mode);
  caps.exitAltScreen = capOrEmpty(exit_ca_mode);
  caps.keyUp = capOrEmpty(key_up);
  caps.keyDown = capOrEmpty(key_down);
  caps.keyLeft = capOrEmpty(key_left);
  caps.keyRight = capOrEmpty(key_right);
  caps.keyPageUp = capOrEmpty(key_ppage);
  caps.keyPageDown = capOrEmpty(key_npage);

  if (caps.cursorAddress.empty()) {
    if (!exitOnFailure) return degraded();
    std::fprintf(stderr, "usilic: Invalid terminal.\n");
    std::exit(1);
  }

  int rows, columns;
  windowSize(&rows, &columns);
  if (!std::getenv("LINES") || !std::getenv("COLUMNS")) {
    setenv("LINES", std::to_string(rows).c_str(), 1);
    setenv("COLUMNS", std::to_string(columns).c_str(), 1);
  }

  return std::make_unique<TerminalDisplay>(std::move(caps), rows, columns);
}

void TerminalDisplay::write(const std::string &text) {
  if (!outputEnabled_) return;
  std::fputs(text.c_str(), stdout);
}

void TerminalDisplay::putChar(int c) {
  // Ctrl-G(BEL)は、出力が他の点では抑制されている間でも常に鳴る
  // ――tc_putchar()のK_g特別扱いからの移植。
  if (c == 7) {
    std::putchar(c);
    std::fflush(stdout);
    return;
  }
  if (!outputEnabled_) return;
  std::putchar(c);
}

void TerminalDisplay::flush() {
  if (!outputEnabled_) return;
  std::fflush(stdout);
}

void TerminalDisplay::moveCursor(int row, int col) {
  if (!outputEnabled_) return;
  tputs(tgoto(caps_.cursorAddress.c_str(), col, row), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::setVisibleCursor(int row, int col) { moveCursor(row, col); }

void TerminalDisplay::hideVisibleCursor() {}

void TerminalDisplay::cursorLeft() {
  if (!outputEnabled_) return;
  tputs(caps_.cursorLeft.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::saveCursor() {
  if (!outputEnabled_ || caps_.saveCursor.empty()) return;
  tputs(caps_.saveCursor.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::restoreCursor() {
  if (!outputEnabled_ || caps_.restoreCursor.empty()) return;
  tputs(caps_.restoreCursor.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::clearScreen() {
  if (!outputEnabled_) return;
  tputs(caps_.clearScreen.c_str(), rows_, outc);
  std::fflush(stdout);
}

void TerminalDisplay::clearToEndOfLine() {
  if (!outputEnabled_) return;
  tputs(caps_.clearToEndOfLine.c_str(), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::scrollForward() {
  // legacyのtc_sf()/tc_sr()には(他のほとんどのプリミティブとは
  // 異なり)明示的なoutputEnabledの早期returnが無い――それらは
  // outc()自身の1文字ごとのチェックだけに依存していた。このクラスは
  // 代わりに明示的にガードする(display.hppのoutputEnabled()に関する
  // 注記参照): outc()はいずれにせよ同じバイトを抑制していたはずな
  // ので、観測可能な挙動を変えることなくその不整合を締める。
  if (!outputEnabled_) return;
  tputs(caps_.scrollForward.c_str(), 1, outc);
}

void TerminalDisplay::scrollReverse() {
  if (!outputEnabled_) return;
  tputs(caps_.scrollReverse.c_str(), 1, outc);
}

void TerminalDisplay::setAttribute(Attribute attr) {
  if (!outputEnabled_) return;
  std::string esc = attributeEscape(attr);
  if (esc.empty()) return;
  tputs(esc.c_str(), 1, outc);

  switch (attr) {
    case Attribute::Bold: boldOn_ = true; break;
    case Attribute::BoldOff: boldOn_ = false; break;
    case Attribute::Underline: underlineOn_ = true; break;
    case Attribute::UnderlineOff: underlineOn_ = false; break;
    case Attribute::Italic: italicOn_ = true; break;
    case Attribute::ItalicOff: italicOn_ = false; break;
    case Attribute::Standout: standoutOn_ = true; break;
    case Attribute::StandoutOff: standoutOn_ = false; break;
  }

  // 2026-09-04追加、直接のユーザー報告("-cでは斜体にならない"=fk)を
  // 調査して発見した実際のバグの修正。attributeEscape()自身のBoldOff
  // ケース参照: caps_.boldOffは(ほとんどの本物の端末のterminfoが
  // 専用の「太字だけを解除する」capabilityを持たないため)常に
  // exit_attribute_mode――つまり本物のSGRリセット(端末側では実質
  // \x1b[0m相当)を指す。これは太字「だけ」ではなく、その時点で有効な
  // 下線/斜体/反転表示も含めて「全て」を黙って消してしまう――このクラス
  // 自身は元々こういった横断的な状態を一切追跡していなかったので、
  // 呼び出し元(ContentView::renderLine())側の独立したBold/Underline/
  // Italic/Standoutトラッキング(それぞれ自分の属性が変化した時にしか
  // setAttribute()を呼ばない)と噛み合わず、"<i><b>fgrep</b>(1)</i>"の
  // ような入れ子(HELP.txt自身に複数箇所実在する、例えば"<i><b>fgrep</b>
  // (1)</i>")で、"</b>"によるBoldOffが本物の端末上の斜体まで一緒に
  // 消してしまい、以後は呼び出し元が(自分の中ではItalicは「変化して
  // いない」と思っているので)ItalicのON符号を再送しない――実機再現の
  // 生バイト列キャプチャで確認済み: "\x1b[1m\x1b[3mfgrep\x1b(B\x1b[m
  // (1)\x1b[23m"となっており、"(1)"の直前で本来まだ有効なはずの
  // "\x1b[3m"が再送されていない。修復は、まさにこのリセットが起きた
  // 直後、上のswitchで更新した「今から見た事実」に基づき、それでも
  // なお有効であるべき他の属性のONコードを直ちに再送出するだけ――
  // 呼び出し元には一切変更を要求しない。
  if (attr == Attribute::BoldOff) {
    if (underlineOn_) tputs(attributeEscape(Attribute::Underline).c_str(), 1, outc);
    if (italicOn_) tputs(attributeEscape(Attribute::Italic).c_str(), 1, outc);
    if (standoutOn_) tputs(attributeEscape(Attribute::Standout).c_str(), 1, outc);
  }

  std::fflush(stdout);
}

std::string TerminalDisplay::attributeEscape(Attribute attr) const {
  switch (attr) {
    case Attribute::Underline:
      return !caps_.underlineOn.empty() ? caps_.underlineOn : caps_.standoutOn;
    case Attribute::UnderlineOff:
      return !caps_.underlineOff.empty() ? caps_.underlineOff : caps_.standoutOff;
    case Attribute::Bold:
      return caps_.boldOn;
    case Attribute::BoldOff:
      return caps_.boldOff;
    case Attribute::Standout:
      return caps_.standoutOn;
    case Attribute::StandoutOff:
      return caps_.standoutOff;
    case Attribute::Italic:
      return caps_.italicOn;
    case Attribute::ItalicOff:
      return caps_.italicOff;
  }
  return "";
}

int TerminalDisplay::rows() const { return rows_; }
int TerminalDisplay::columns() const { return columns_; }

void TerminalDisplay::setSize(int rows, int columns) {
  rows_ = rows;
  columns_ = columns;
  applyScrollRegion();
}

std::string TerminalDisplay::keySequence(Key key) const {
  switch (key) {
    case Key::Up: return caps_.keyUp;
    case Key::Down: return caps_.keyDown;
    case Key::Left: return caps_.keyLeft;
    case Key::Right: return caps_.keyRight;
    case Key::PageUp: return caps_.keyPageUp;
    case Key::PageDown: return caps_.keyPageDown;
  }
  return "";
}

bool TerminalDisplay::outputEnabled() const { return outputEnabled_; }

void TerminalDisplay::setOutputEnabled(bool enabled) { outputEnabled_ = enabled; }

bool TerminalDisplay::smoothScrollEnabled() const { return smoothScroll_; }

void TerminalDisplay::setSmoothScrollEnabled(bool enabled) {
  smoothScroll_ = enabled;
  applyScrollRegion();
}

std::pair<int, int> TerminalDisplay::scrollRegionArgs() const {
  return {rows_ - (smoothScroll_ ? 1 : 2), 0};
}

void TerminalDisplay::applyScrollRegion() {
  if (!outputEnabled_ || caps_.changeScrollRegion.empty()) return;
  auto [m, n] = scrollRegionArgs();
  tputs(tgoto(caps_.changeScrollRegion.c_str(), m, n), 1, outc);
  std::fflush(stdout);
}

void TerminalDisplay::open() {
  if (!outputEnabled_) return;
  tputs(caps_.enterAltScreen.c_str(), 1, outc);
  std::fflush(stdout);
  applyScrollRegion();
}

void TerminalDisplay::close() {
  // legacyのtc.cはexit_ca_mode(te)をTc.teへ取り込んでいたが一度も
  // それを呼んでいなかった――usilicは終了時に常に端末の代替画面を
  // 有効なままにしていた。これは単純な移植ではなく意図的な修正
  // である: 代替画面を開くDisplayは、それを離れるのもそのDisplay
  // であるべきだ。
  if (!outputEnabled_ || caps_.exitAltScreen.empty()) return;
  tputs(caps_.exitAltScreen.c_str(), 1, outc);
  std::fflush(stdout);
}

// 完全な経緯についてはDisplay::leaveForExternalProgram()自身の
// コメント参照(docs/HISTORY.md:9023、legacyのvt.cのvt_init(-2)
// ケースからの移植): applyScrollRegion()は、モードラインのために
// 最後の1行を確保するため、usilic自身の描画向けにスクロール領域を
// rows_-2(またはスムーズスクロール下ではrows_-1)に制限する――
// 外部プログラム(`less`、PROGRAM一覧のエントリ)は、その同じ本物の
// 端末を引き継ぐが、usilicがそれを狭めたことなど全く知らないので、
// その実行中は「画面全体」を取り戻す必要がある。
void TerminalDisplay::leaveForExternalProgram() {
  if (!outputEnabled_ || caps_.changeScrollRegion.empty()) return;
  tputs(tgoto(caps_.changeScrollRegion.c_str(), rows_ - 1, 0), 1, outc);
  std::fflush(stdout);
}

// 上のペアのもう半分: 制御が戻った時に、スクロール領域をusilic
// 自身の通常の(スムーズスクロールに依存する)範囲へ再度制限する
// ――単にapplyScrollRegion()自体が、既にsmoothScroll_/rows_から
// まさにそれを計算している。
void TerminalDisplay::reenterFromExternalProgram() { applyScrollRegion(); }

// ContentView::renderLine()自身の同一のバイト範囲ロジック
// (col < run.start || col >= run.end)参照――このクラス自身の慣習
// (バイトオフセット == 画面の列、ContentView自身のクラスコメント
// 参照)はここでも当てはまる。行全体に対して1回のwrite()呼び出しを
// 行うのではなく、バイトごとに書き込むことで、Bold/Underlineが
// 各runの境界ちょうどで切り替わるようにする――下のmodeLineSegments
// 自身のStandout切り替えが自身の(単一の、行全体の)runに対して既に
// 使っているのと同じ形。
void TerminalDisplay::writeAttributedLine(const std::string &text, const std::vector<Display::AttributeRun> &runs) {
  bool boldOn = false, underlineOn = false, italicOn = false;
  for (std::size_t col = 0; col < text.size(); col++) {
    bool wantBold = false, wantUnderline = false, wantItalic = false;
    for (const auto &run : runs) {
      if (static_cast<int>(col) < run.start || static_cast<int>(col) >= run.end) continue;
      if (run.attribute == Attribute::Bold) wantBold = true;
      else if (run.attribute == Attribute::Underline) wantUnderline = true;
      else if (run.attribute == Attribute::Italic) wantItalic = true;
    }
    if (wantBold != boldOn) {
      setAttribute(wantBold ? Attribute::Bold : Attribute::BoldOff);
      boldOn = wantBold;
    }
    if (wantUnderline != underlineOn) {
      setAttribute(wantUnderline ? Attribute::Underline : Attribute::UnderlineOff);
      underlineOn = wantUnderline;
    }
    if (wantItalic != italicOn) {
      setAttribute(wantItalic ? Attribute::Italic : Attribute::ItalicOff);
      italicOn = wantItalic;
    }
    putChar(static_cast<unsigned char>(text[col]));
  }
  if (boldOn) setAttribute(Attribute::BoldOff);
  if (underlineOn) setAttribute(Attribute::UnderlineOff);
  if (italicOn) setAttribute(Attribute::ItalicOff);
}

void TerminalDisplay::renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                                      const std::vector<ModeLineSegment> &modeLineSegments) {
  (void)pixelOffsetY;  // 本物の端末では行未満の細かいアドレス指定は無い――このメソッド自身のヘッダーコメント参照
  if (!outputEnabled_) return;
  int contentRows = std::max(rows_ - 1, 0);
  for (int row = 0; row < contentRows; row++) {
    int lineIndex = topIndex + row;
    moveCursor(row, 0);
    clearToEndOfLine();
    if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) {
      const ScrolledLine &line = lines[lineIndex];
      if (line.attributes.empty()) write(line.text);
      else writeAttributedLine(line.text, line.attributes);
    }
  }
  if (rows_ > 0) {
    // ContentView::renderModeLine()自身と同じ、セグメントごとの
    // Standout run――理由についてはDisplay::ModeLineSegment自身の
    // コメント参照(非standoutなセグメント、フィールド間のプレーンな
    // ギャップは、renderModeLine()自身のdrawSegment()呼び出しの間の
    // プレーンなputChar(' ')と同様、属性を全く付けずに書かれる)。
    moveCursor(contentRows, 0);
    clearToEndOfLine();
    for (const auto &segment : modeLineSegments) {
      if (segment.standout) setAttribute(Attribute::Standout);
      write(segment.text);
      if (segment.standout) setAttribute(Attribute::StandoutOff);
    }
  }
  flush();
}

}  // namespace fm
