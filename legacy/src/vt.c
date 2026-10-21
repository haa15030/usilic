#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static VT *vt;

void vt_printf(char *fmt, ...)/*usilic*/
{
  va_list ap;

  if (!vt_mode('D')) return;
  va_start(ap, fmt);
  vt->printf(fmt, ap);
  va_end(ap);
}

int vt_putchar(int c)/*usilic*/
{
  return vt->put_char(c);
}

void vt_flush(void)/*usilic*/
{
  vt->flush();
}

void vt_endstand(void)/*usilic*/
{
  vt->endstand();
}

void vt_stand(void)/*usilic*/
{
  vt->stand();
}

void vt_move(int row, int col)/*usilic*/
{
  vt->move(row, col);
}

int vt_row(int row)/*usilic*/
{
  return vt->row(row);
}

int vt_col(int col)/*usilic*/
{
  return vt->col(col);
}

void vt_clear(void)/*usilic*/
{
  vt->clear();
}

void vt_eeol(void)/*usilic*/
{
  vt->eeol();
}

void vt_left(void)/*usilic*/
{
  vt->left();
}

void vt_sf(void)/*usilic*/
{
  vt->sf();
}

void vt_sr(void)/*usilic*/
{
  vt->sr();
}

void vt_rc(void)/*usilic*/
{
  vt->rc();
}

void vt_sc(void)/*usilic*/
{
  vt->sc();
}

void vt_cs(int m, int n)/*usilic*/
{
  vt->cs(m, n);
}

void vt_seta(int attr)/*usilic*/
{
  vt->seta(attr);
}

char *vt_geta(int attr)/*usilic*/
{
  return vt->geta(attr);
}

void vt_ks(char *ks[])/*usilic*/
{
  vt->ks(ks);
}

int vt_mode(int mode)/*usilic*/
{
  return vt->mode(mode);
}

int vt_cursor(int f_cursor)/*usilic*/
{
  return vt->cursor(f_cursor);
}

int vt_set(int f_usilic0)/*usilic*/
{
  static int f_usilic;

  if (f_usilic0) {
    f_usilic = f_usilic0;
    vt = tc_load();
  }
  return f_usilic;
}

int get_winsize(int *rowp, int *colp)/*usilic*/
{
  struct winsize winsize;
  int fd;

  winsize.ws_row = 0;
  fd = open("/dev/tty", O_RDONLY);
  if (ioctl(fd, TIOCGWINSZ, &winsize) < 0)
    fprintf(stderr, "%s: get_winsize(): %s\n", s_S(S_usilic), strerror(errno));
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

int set_winsize(int row, int col)/*usilic*/
{
  struct winsize winsize;

  winsize.ws_row = row;
  winsize.ws_col = col;
  if (ioctl(1, TIOCSWINSZ, &winsize) < 0)
    save_error("%s: set_winsize(): %s.\n", s_S(S_usilic), strerror(errno));
  return 0;
}

int vt_getch(void)/*usilic*/
{
  int i;
  u_char c;

  if (!(i = read(vt_tfd(0), &c, 1)) || i < 0) return 0;
  return (c) ? c : K_at; /* Ctrl+Space */
}

int vt_charset(int i)/*usilic*/
{
  static int charset;

  if (i) charset = i;
  return charset;
}

void vt_winsize(void)/*usilic*/
{
  int row, col;

  get_winsize(&row, &col);
  vt_row(row);
  vt_col(col);
  setenv("LINES", utoa(row), 1);
  setenv("COLUMNS", utoa(col), 1);
  if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
}

void vt_setwin(int row, int col)/*usilic*/
{
  vt_row(row);
  vt_col(col);
  if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
}

int vt_tfd(int i)/*usilic*/
{
  static int tfd;

  if (i < 0) close(tfd);
  else if (i > 0) tfd = i;
  return tfd;
}

int vt_init(int init)/*usilic*/
{
  static struct termios tty_termio, vt_termio;
  static int tfd; 
  int row, col;
  char *usiliccharset;

  switch(init) {
  case 1:
    get_winsize(&row, &col);
    vt_row(row);
    vt_col(col);
    if ((tfd = open(s_S(S_tty), O_RDONLY)) < 0) tfd = 2;
    fprintf(stderr, "s_S(S_tty)=%s,tfd=%d\n", s_S(S_tty),tfd);
    if (!isatty(tfd)) {
      fprintf(stderr, "%s: %s\n", s_S(S_usilic), s_S(S_erru));
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
    if ((usiliccharset = getenv("USILIC_OLDCHARSET")))
      vt_charset((!strcasecmp(usiliccharset, "ascii")) ? C_ascii : C_euc);
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

