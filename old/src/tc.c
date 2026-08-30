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

static int outc(int c)
{
  if (!vt_mode('D')) return 0;
  putchar(c);
  return c;
}

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

static void tc_flush(void)
{
  if (!vt_mode('D')) return;
  fflush(stdout);
}

static void tc_endstand(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.se, 1, outc);
  fflush(stdout);
}

static void tc_stand(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.so, 1, outc);
  fflush(stdout);
}

static void tc_move(int row, int col)
{
  if (!vt_mode('D')) return;
  tputs(tgoto(Tc.cm, col, row), 1, outc);
  fflush(stdout);
}

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

static int tc_col(int i)
{
  static int col;

  if (i) col = i;
  return col;
}

static void tc_clear(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.cl, vt_row(0), outc);
  fflush(stdout);
}

static void tc_eeol(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.ce, 1, outc);
  fflush(stdout);
}

static void tc_left(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.le, 1, outc);
  fflush(stdout);
}

static void tc_sf(void)
{
  tputs(Tc.sf, 1, outc);
}

static void tc_sr(void)
{
  tputs(Tc.sr, 1, outc);
}

static void tc_rc(void)/*tc*/
{
  if (!vt_mode('D') || !Tc.rc) return;
  tputs(Tc.rc, 1, outc);
  fflush(stdout);
}

static void tc_sc(void)/*tc*/
{
  if (!vt_mode('D') || !Tc.sc) return;
  tputs(Tc.sc, 1, outc);
  fflush(stdout);
}

static void tc_cs(int m, int n)/*tc*/
{
  if (Tc.cs) {
    tputs(tgoto(Tc.cs, m, n), 1, outc);
    fflush(stdout);
  }
}

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

static void tc_ks(char *ks[])
{
  ks[0] = Tc.ku;
  ks[1] = Tc.kd;
  ks[2] = Tc.kr;
  ks[3] = Tc.kl;
  ks[4] = Tc.kP;
  ks[5] = Tc.kN;
}

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

static int tc_cursor(int f_cursor)
{
  key_winch(-f_cursor);
  return 0;
}

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

