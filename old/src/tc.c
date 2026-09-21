#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <curses.h>
#ifdef __CYGWIN32__
#include <ncurses/term.h>
#else
#include <term.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "tc.P"
#define N_LB (1+2+2+2+2+2+1+2+2+2+2+2+2)
static struct TC { /* termcap関数 */
  char *cs;
  char *ho, *cm;
  char *cl, *ce;
  char *so, *se;
  char *us, *ue;
  char *md, *me;
  char *le;
  char *sc, *rc;
  char *kl, *kr;
  char *ku, *kd;
  char *kP, *kN;
  char *sf, *sr;
  char *ti, *te;
} Tc;

/*
 * 【機能】tputsに渡す1文字出力の関数。ウィンドウ表示が有効(vt_mode 'D')のときだけputcharする。
 * 【C++】TerminalDisplay::putChar (src/cpp/src/terminal_display.cpp)
 */
static int outc(int c)
{
  if (!vt_mode('D')) return 0;
  putchar(c);
  return c;
}

/*
 * 【機能】1文字を端末へ出す。BEL(^G)だけは、表示が無効でも鳴らす。
 * 【C++】TerminalDisplay::putChar (src/cpp/src/terminal_display.cpp)
 */
static int tc_putchar(int c)
{
  if (c == K_g) {
    putchar(K_g);
    fflush(stdout);
    return K_g;
  }
  if (!vt_mode('D')) return 0;
  putchar(c);
  return c;
}

/*
 * 【機能】標準出力をflushする(表示が有効のとき)。
 * 【C++】TerminalDisplay::flush (src/cpp/src/terminal_display.cpp)
 */
static void tc_flush(void)
{
  if (!vt_mode('D')) return;
  fflush(stdout);
}

/*
 * 【機能】反転表示(standout)を終える(terminfoのse)。
 * 【C++】TerminalDisplay::setAttribute (src/cpp/src/terminal_display.cpp)
 * 【備考】C++はAttribute::StandoutOffとして、setAttributeでまとめて扱う。
 */
static void tc_endstand(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.se, 1, outc);
  fflush(stdout);
}

/*
 * 【機能】反転表示(standout)を始める(terminfoのso)。
 * 【C++】TerminalDisplay::setAttribute (src/cpp/src/terminal_display.cpp)
 * 【備考】C++はAttribute::Standoutとして、setAttributeでまとめて扱う。
 */
static void tc_stand(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.so, 1, outc);
  fflush(stdout);
}

/*
 * 【機能】カーソルを(row, col)へ移動する(terminfoのcm)。
 * 【C++】TerminalDisplay::moveCursor (src/cpp/src/terminal_display.cpp)
 */
static void tc_move(int row, int col)
{
  if (!vt_mode('D')) return;
  tputs(tgoto(Tc.cm, col, row), 1, outc);
  fflush(stdout);
}

/*
 * 【機能】端末の行数を記憶する/返す(iが0でなければ設定)。
 * 【C++】TerminalDisplay::rows (src/cpp/src/terminal_display.cpp)
 */
static int tc_row(int i)/*tc*/
{
  static int row;
  
#ifdef __CYGWIN32__
  if (i) 
#else
  if (i) 
#endif
    row = i;
  return row;
}

/*
 * 【機能】端末の桁数を記憶する/返す(iが0でなければ設定)。
 * 【C++】TerminalDisplay::columns (src/cpp/src/terminal_display.cpp)
 */
static int tc_col(int i)
{
  static int col;

  if (i) col = i;
  return col;
}

/*
 * 【機能】画面を消去する(terminfoのcl)。
 * 【C++】TerminalDisplay::clearScreen (src/cpp/src/terminal_display.cpp)
 */
static void tc_clear(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.cl, vt_row(0), outc);
  fflush(stdout);
}

/*
 * 【機能】カーソルから行末までを消去する(terminfoのce)。
 * 【C++】TerminalDisplay::clearToEndOfLine (src/cpp/src/terminal_display.cpp)
 */
static void tc_eeol(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.ce, 1, outc);
  fflush(stdout);
}

/*
 * 【機能】カーソルを1桁左へ動かす(terminfoのle)。
 * 【C++】TerminalDisplay::cursorLeft (src/cpp/src/terminal_display.cpp)
 */
static void tc_left(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.le, 1, outc);
  fflush(stdout);
}

/*
 * 【機能】端末のスクロール領域を1行、上へずらす(terminfoのsf)。
 * 【C++】TerminalDisplay::scrollForward (src/cpp/src/terminal_display.cpp)
 */
static void tc_sf(void)
{
  tputs(Tc.sf, 1, outc);
}

/*
 * 【機能】端末のスクロール領域を1行、下へずらす(terminfoのsr)。
 * 【C++】TerminalDisplay::scrollReverse (src/cpp/src/terminal_display.cpp)
 */
static void tc_sr(void)
{
  tputs(Tc.sr, 1, outc);
}

/*
 * 【機能】保存しておいたカーソル位置へ戻す(terminfoのrc)。
 * 【C++】TerminalDisplay::restoreCursor (src/cpp/src/terminal_display.cpp)
 */
static void tc_rc(void)/*tc*/
{
  if (!vt_mode('D') || !Tc.rc) return;
  tputs(Tc.rc, 1, outc);
  fflush(stdout);
}

/*
 * 【機能】カーソル位置を保存する(terminfoのsc)。
 * 【C++】TerminalDisplay::saveCursor (src/cpp/src/terminal_display.cpp)
 */
static void tc_sc(void)/*tc*/
{
  if (!vt_mode('D') || !Tc.sc) return;
  tputs(Tc.sc, 1, outc);
  fflush(stdout);
}

/*
 * 【機能】スクロール領域(m行〜n行)を設定する(terminfoのcs)。
 * 【C++】TerminalDisplay::applyScrollRegion (src/cpp/src/terminal_display.cpp)
 */
static void tc_cs(int m, int n)/*tc*/
{
  if (Tc.cs) {
    tputs(tgoto(Tc.cs, m, n), 1, outc);
    fflush(stdout);
  }
}

/*
 * 【機能】文字の属性(太字・下線・反転など)をterminfoの対応するコマンドで設定する。下線が無い端末では反転で
 *       代用する。
 * 【C++】TerminalDisplay::setAttribute (src/cpp/src/terminal_display.cpp)
 * 【備考】C++も、下線が無い端末では反転で代用する(attributeEscape)。
 */
static void tc_seta(int attr)
{
  if (!vt_mode('D')) return;
  switch(attr) {
  case A_us:
  case A_us2:
    if (Tc.us) tputs(Tc.us, 1, outc);
    else if (Tc.so) tputs(Tc.so, 1, outc);
    break;
  case A_ue:
    if (Tc.ue) tputs(Tc.ue, 1, outc);
    else if (Tc.se) tputs(Tc.se, 1, outc);
    break;
  case A_md:
  case A_md2:
  case A_md3:
    if (Tc.md) tputs(Tc.md, 1, outc);
    break;
  case A_me:
    if (Tc.me) tputs(Tc.me, 1, outc);
    break;
  case A_cs:
    if (Tc.so) tputs(Tc.so, 1, outc);
    break;
  case A_ce:
    if (Tc.se) tputs(Tc.se, 1, outc);
    break;
  }
}

/*
 * 【機能】属性に対応する端末の制御文字列(エスケープ)を返す(下線が無ければ反転のもの)。
 * 【C++】TerminalDisplay::attributeEscape (src/cpp/src/terminal_display.cpp)
 */
static char *tc_geta(int attr)
{
  switch(attr) {
  case A_us:
  case A_us2:
    return (Tc.us) ? Tc.us : Tc.so;
    break;
  case A_ue:
    return (Tc.ue) ? Tc.ue : Tc.se;
    break;
  case A_md:
  case A_md2:
    return Tc.md;
    break;
  case A_me:
    return Tc.me;
    break;
  case A_cs:
    return Tc.so;
    break;
  case A_ce:
    return Tc.se;
    break;
  default:
    return NULL;
  }
  return NULL;
}

/*
 * 【機能】カーソルキー(上下左右、PageUp/Down)が端末から送る文字列の表を返す。
 * 【C++】TerminalDisplay::keySequence (src/cpp/src/terminal_display.cpp)
 */
static void tc_ks(char *ks[])
{
  ks[0] = Tc.ku;
  ks[1] = Tc.kd;
  ks[2] = Tc.kr;
  ks[3] = Tc.kl;
  ks[4] = Tc.kP;
  ks[5] = Tc.kN;
}

/*
 * 【機能】端末の状態フラグの取得・設定。'D'=ウィンドウを表示する、'W'=SHELLでのSIGWINCH、
 *       'H'=ステータス行(hs)を持つ。小文字で設定、負の値(-'d'等)で解除。0のときは初期化(ti)を出す。
 * 【C++】TerminalDisplay::setOutputEnabled; TerminalDisplay::setModeLineVisible (src/cpp/src/terminal_display.cpp)
 */
static int tc_mode(int mode)
{
  static struct {
    u_int disp	: 1; /* ウィンドウを表示する */
    u_int win	: 1; /* SHELLでのSIGWINCH */
    u_int hs	: 1;
  } f;

  switch(mode) {
  case 0:
    tputs(Tc.ti, 1, outc);
    if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
    break;
  case 'D':
    return f.disp;
  case 'd':
    f.disp = 1;
    break;
  case -'d':
    f.disp = 0;
    break;
  case 'W':
    return f.win;
  case 'w':
    f.win = 1;
    break;
  case -'w':
    f.win = 0;
    break;
  case 'H':
    return f.hs;
  case 'h':
    f.hs = 1;
    break;
  case -'h':
    f.hs = 0;
    break;
  default:
    break;
  }
  return 0;
}

/*
 * 【機能】カーソルの表示・非表示に合わせて、ウィンドウサイズ変更のキー処理(key_winch)を切り替える。
 * 【C++】TerminalDisplay::hideVisibleCursor; TerminalDisplay::setVisibleCursor (src/cpp/src/terminal_display.cpp)
 */
static int tc_cursor(int f_cursor)
{
  key_winch(-f_cursor);
  return 0;
}

/*
 * 【機能】terminfoを読み込み(setupterm)、端末の各機能(cm、cl、ce、so、se、sf、sr、cs、キー等)を取り出して、
 *       端末操作の関数表(VT)を組み立てて返す。TERMの上書きにも対応する。
 * 【C++】TerminalDisplay::fromEnvironment (src/cpp/src/terminal_display.cpp)
 */
VT *tc_load(void)/*usilic*/
{
  int i, j;
  char *term;
  static VT vt = {
    tc_flush,
    vprintf,
    tc_putchar,
    tc_endstand,
    tc_stand,
    tc_move,
    tc_row,
    tc_col,
    tc_clear,
    tc_eeol,
    tc_left,
    tc_sf,
    tc_sr,
    tc_rc,
    tc_sc,
    tc_cs,
    tc_seta,
    tc_geta,
    tc_ks,
    tc_mode,
    tc_cursor,
  };
  int state;

  if ((term = getenv("TERM")) == NULL) {
    fprintf(stderr, "%s: TERM environment variable is not set.\n", 
	    s_S(S_usilic));
    free_S();
    exit(1);
  }
  if (!strcmp(term, "screen")) setenv("TERM", "vt100", 1);
  else if (!strcmp(term, "cygwin")) setenv("TERM", "kterm", 1);
  setupterm(term, 1, &state);
  if (state != 1) {
    fprintf(stderr, "%s: %s.\n", s_S(S_usilic), s_S(S_erru));
    free_S();
    exit(1);
  }
  Tc.cs = change_scroll_region;
  Tc.ho = cursor_home; 
  Tc.cm = cursor_address;
  Tc.cl = clear_screen; 
  Tc.ce = clr_eol;
  Tc.so = enter_standout_mode; 
  Tc.se = exit_standout_mode;
  Tc.us = enter_underline_mode; 
  Tc.ue = exit_underline_mode;
  Tc.md = enter_bold_mode; 
  Tc.me = exit_attribute_mode;
  Tc.le = cursor_left;
  Tc.sc = save_cursor; 
  Tc.rc = restore_cursor;
  Tc.kl = key_left; 
  Tc.kr = key_right;
  Tc.ku = key_up; 
  Tc.kd = key_down;
  Tc.kP = key_ppage; 
  Tc.kN = key_npage;
  Tc.sf = scroll_forward; 
  Tc.sr = scroll_reverse;
  Tc.ti = enter_ca_mode; 
  Tc.te = exit_ca_mode;
  if (!Tc.cm) {
    fprintf(stderr, "%s: %s.\n", s_S(S_usilic), s_S(S_erru));
    free_S();
    exit(1);
  }
  get_winsize(&i, &j);
  tc_row(i);
  tc_col(j);
  if (!getenv("LINES") || !getenv("COLUMNS")) {
    setenv("LINES", utoa(i), 1);
    setenv("COLUMNS", utoa(j), 1);
  }
  return &vt;
}

