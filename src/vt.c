#include "tcb.h"
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static VT *vt;

void vt_printf(char *fmt, ...)/*tcb*/
{
  va_list ap;

  if (!vt_mode('D')) return;
  va_start(ap, fmt);
  vt->printf(fmt, ap);
  va_end(ap);
}

int vt_putchar(int c)/*tcb*/
{
  return vt->put_char(c);
}

void vt_flush(void)/*tcb*/
{
  vt->flush();
}

void vt_endstand(void)/*tcb*/
{
  vt->endstand();
}

void vt_stand(void)/*tcb*/
{
  vt->stand();
}

void vt_move(int row, int col)/*tcb*/
{
  vt->move(row, col);
}

int vt_row(int row)/*tcb*/
{
  return vt->row(row);
}

int vt_col(int col)/*tcb*/
{
  return vt->col(col);
}

void vt_clear(void)/*tcb*/
{
  vt->clear();
}

void vt_eeol(void)/*tcb*/
{
  vt->eeol();
}

void vt_left(void)/*tcb*/
{
  vt->left();
}

void vt_sf(void)/*tcb*/
{
  vt->sf();
}

void vt_sr(void)/*tcb*/
{
  vt->sr();
}

void vt_rc(void)/*tcb*/
{
  vt->rc();
}

void vt_sc(void)/*tcb*/
{
  vt->sc();
}

void vt_cs(int m, int n)/*tcb*/
{
  vt->cs(m, n);
}

void vt_seta(int attr)/*tcb*/
{
  vt->seta(attr);
}

char *vt_geta(int attr)/*tcb*/
{
  return vt->geta(attr);
}

void vt_ks(char *ks[])/*tcb*/
{
  vt->ks(ks);
}

int vt_mode(int mode)/*tcb*/
{
  return vt->mode(mode);
}

int vt_cursor(int f_cursor)/*tcb*/
{
  return vt->cursor(f_cursor);
}

int vt_set(int f_tcb0)/*tcb*/
{
  static int f_tcb;

  if (f_tcb0) {
    f_tcb = f_tcb0;
    vt = tc_load();
  }
  return f_tcb;
}

int get_winsize(int *rowp, int *colp)/*tcb*/
{
  struct winsize winsize;
  int fd;

  winsize.ws_row = 0;
  fd = open("/dev/tty", O_RDONLY);
  if (ioctl(fd, TIOCGWINSZ, &winsize) < 0)
    fprintf(stderr, "%s: get_winsize(): %s\n", s_S(S_tcb), strerror(errno));
  else {
    if (!winsize.ws_row) {
      *rowp = 80;
      *colp = 30;
    } else {
      *rowp = winsize.ws_row;
      *colp = winsize.ws_col;
    }
  }
  close(fd);
  return 0;
}

int set_winsize(int row, int col)/*tcb*/
{
  struct winsize winsize;

  winsize.ws_row = row;
  winsize.ws_col = col;
  if (ioctl(1, TIOCSWINSZ, &winsize) < 0)
    save_error("%s: set_winsize(): %s.\n", s_S(S_tcb), strerror(errno));
  return 0;
}

int vt_getch(void)/*tcb*/
{
  int i;
  u_char c;

  if (!(i = read(vt_tfd(0), &c, 1)) || i < 0) return 0;
  return (c) ? c : K_at; /* Ctrl+Space */
}

int vt_charset(int i)/*tcb*/
{
  static int charset;

  if (i) charset = i;
  return charset;
}

void vt_winsize(void)/*tcb*/
{
  int row, col;

  get_winsize(&row, &col);
  vt_row(row);
  vt_col(col);
  setenv("LINES", utoa(row), 1);
  setenv("COLUMNS", utoa(col), 1);
  if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
}

void vt_setwin(int row, int col)/*tcb*/
{
  vt_row(row);
  vt_col(col);
  if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
}

int vt_tfd(int i)/*tcb*/
{
  static int tfd;

  if (i < 0) close(tfd);
  else if (i > 0) tfd = i;
  return tfd;
}

int vt_init(int init)/*tcb*/
{
  static struct termios tty_termio, vt_termio;
  static int tfd; 
  int row, col;
  char *tcbcharset;

  switch(init) {
  case 1:
    get_winsize(&row, &col);
    vt_row(row);
    vt_col(col);
    if ((tfd = open(s_S(S_tty), O_RDONLY)) < 0) tfd = 2;
    if (!isatty(tfd)) {
      fprintf(stderr, "%s: %s\n", s_S(S_tcb), s_S(S_erru));
      if (tfd != 2) close(tfd);
      return -1;
    }
    vt_tfd(tfd);
    tcgetattr(tfd, &tty_termio);
    vt_termio = tty_termio;
#if 0
    cfmakeraw(&vt_termio);
    vt_termio.c_iflag &= ~(INLCR|ICRNL|ISTRIP|IGNBRK|IGNCR);
    vt_termio.c_oflag &= OPOST;
    vt_termio.c_oflag &= ~ONLCR;
    vt_termio.c_lflag &= ~(ICANON|ECHO|NOFLSH);
#else
    vt_termio.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
			    |INLCR|IGNCR|ICRNL|IXON);
    vt_termio.c_oflag &= ~OPOST;
    vt_termio.c_oflag &= ~ONLCR;
    vt_termio.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN|NOFLSH);
    vt_termio.c_cflag &= ~(CSIZE|PARENB);
    vt_termio.c_cflag |= CS8;
#endif
    vt_termio.c_cc[VMIN] = 0;
    vt_termio.c_cc[VTIME] = 1;
    tcsetattr(tfd, TCSANOW, &vt_termio);
    vt_mode('d');
    vt_mode(0);
    if ((tcbcharset = getenv(ENV_CHARSET)))
      /* config.h's CHARSET default is "utf-8", so an unset TCBCHARSET
	 gets that value via setenv() in init_config() (str.c) -- but
	 `export TCBCHARSET=` (empty, not unset) makes getenv() return
	 a non-NULL empty string here, which matched neither "ascii"
	 nor "utf8"/"utf-8" and fell through to a hardcoded C_euc,
	 silently reintroducing EUC mojibake even with no TCBCHARSET
	 value and no ~/.tcb/history to blame. Any unrecognized value
	 (including "") now falls through to C_utf8 instead, matching
	 the documented default. */
      vt_charset((!strcasecmp(tcbcharset, "ascii")) ? C_ascii
		 : (!strcasecmp(tcbcharset, "euc")) ? C_euc
		 : C_utf8);
    break;
  case 2:
    if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
  case -9:
    tcsetattr(tfd, TCSANOW, &vt_termio);	
    break;
  case -1:
    vt_seta(A_me);
  case -2:
    if (!vt_mode('H')) vt_cs(vt_row(0) - 1, 0);
    vt_move(vt_row(0) - 1, 0);
    vt_eeol();
    tcsetattr(tfd, TCSANOW, &tty_termio);
    if (init == -1) {
      close(tfd);
      tfd = 0;
    }
    break;
  case 3:
    vt_termio.c_cc[VMIN] = 0;
    vt_termio.c_cc[VTIME] = 1;
    tcsetattr(tfd, TCSANOW, &vt_termio);
    break;
  case -3:
    vt_termio.c_cc[VMIN] = 0;
    vt_termio.c_cc[VTIME] = 0;
    tcsetattr(tfd, TCSANOW, &vt_termio);
    break;
  default:
    break;
  } 
  return 0;
}

