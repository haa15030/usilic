#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static VT *vt;

/*
 * 【機能】書式付きで端末へ出力する(ウィンドウ表示が有効(vt_mode 'D')のときだけ)。VTの関数表のprintfを呼ぶ。
 * 【C++】Display::write (src/cpp/src/display.hpp)
 * 【備考】C++は書式化した文字列をwriteへ渡す。
 */
void vt_printf(char *fmt, ...)/*usilic*/
{
  va_list ap;

  if (!vt_mode('D')) return;
  va_start(ap, fmt);
  vt->printf(fmt, ap);
  va_end(ap);
}

/*
 * 【機能】1文字を端末へ出す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::putChar (src/cpp/src/display.hpp)
 */
int vt_putchar(int c)/*usilic*/
{
  return vt->put_char(c);
}

/*
 * 【機能】出力をflushする。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::flush (src/cpp/src/display.hpp)
 */
void vt_flush(void)/*usilic*/
{
  vt->flush();
}

/*
 * 【機能】反転表示を終える。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::setAttribute (src/cpp/src/display.hpp)
 * 【備考】C++はAttribute::StandoutOff。
 */
void vt_endstand(void)/*usilic*/
{
  vt->endstand();
}

/*
 * 【機能】反転表示を始める。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::setAttribute (src/cpp/src/display.hpp)
 * 【備考】C++はAttribute::Standout。
 */
void vt_stand(void)/*usilic*/
{
  vt->stand();
}

/*
 * 【機能】カーソルを(row, col)へ動かす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::moveCursor (src/cpp/src/display.hpp)
 */
void vt_move(int row, int col)/*usilic*/
{
  vt->move(row, col);
}

/*
 * 【機能】端末の行数を設定する/返す(引数が0でなければ設定)。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::rows (src/cpp/src/display.hpp)
 */
int vt_row(int row)/*usilic*/
{
  return vt->row(row);
}

/*
 * 【機能】端末の桁数を設定する/返す(引数が0でなければ設定)。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::columns (src/cpp/src/display.hpp)
 */
int vt_col(int col)/*usilic*/
{
  return vt->col(col);
}

/*
 * 【機能】画面を消去する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::clearScreen (src/cpp/src/display.hpp)
 */
void vt_clear(void)/*usilic*/
{
  vt->clear();
}

/*
 * 【機能】カーソルから行末までを消去する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::clearToEndOfLine (src/cpp/src/display.hpp)
 */
void vt_eeol(void)/*usilic*/
{
  vt->eeol();
}

/*
 * 【機能】カーソルを1桁左へ動かす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::cursorLeft (src/cpp/src/display.hpp)
 */
void vt_left(void)/*usilic*/
{
  vt->left();
}

/*
 * 【機能】スクロール領域を1行上へずらす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::scrollForward (src/cpp/src/display.hpp)
 */
void vt_sf(void)/*usilic*/
{
  vt->sf();
}

/*
 * 【機能】スクロール領域を1行下へずらす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::scrollReverse (src/cpp/src/display.hpp)
 */
void vt_sr(void)/*usilic*/
{
  vt->sr();
}

/*
 * 【機能】保存したカーソル位置へ戻す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::restoreCursor (src/cpp/src/display.hpp)
 */
void vt_rc(void)/*usilic*/
{
  vt->rc();
}

/*
 * 【機能】カーソル位置を保存する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::saveCursor (src/cpp/src/display.hpp)
 */
void vt_sc(void)/*usilic*/
{
  vt->sc();
}

/*
 * 【機能】スクロール領域を設定する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】TerminalDisplay::applyScrollRegion (src/cpp/src/terminal_display.cpp)
 */
void vt_cs(int m, int n)/*usilic*/
{
  vt->cs(m, n);
}

/*
 * 【機能】文字の属性(太字・下線など)を設定する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::setAttribute (src/cpp/src/display.hpp)
 */
void vt_seta(int attr)/*usilic*/
{
  vt->seta(attr);
}

/*
 * 【機能】属性に対応する端末の制御文字列を返す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::attributeEscape (src/cpp/src/display.hpp)
 */
char *vt_geta(int attr)/*usilic*/
{
  return vt->geta(attr);
}

/*
 * 【機能】カーソルキー等が送る文字列の表を返す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::keySequence (src/cpp/src/display.hpp)
 */
void vt_ks(char *ks[])/*usilic*/
{
  vt->ks(ks);
}

/*
 * 【機能】端末の状態フラグ('D'=表示有効、'W'=SHELLのSIGWINCH、'H'=ステータス行有り)を取得・設定する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::outputEnabled; Display::setOutputEnabled; Display::modeLineVisible (src/cpp/src/display.hpp)
 */
int vt_mode(int mode)/*usilic*/
{
  return vt->mode(mode);
}

/*
 * 【機能】カーソルの表示・非表示を切り替える。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。
 * 【C++】Display::setVisibleCursor; Display::hideVisibleCursor (src/cpp/src/display.hpp)
 */
int vt_cursor(int f_cursor)/*usilic*/
{
  return vt->cursor(f_cursor);
}

/*
 * 【機能】起動形態(f_usilic)を覚え、端末の実装(tc_load)を読み込む。引数0なら、覚えた起動形態だけを返す。
 * 【C++】TerminalDisplay::fromEnvironment (src/cpp/src/terminal_display.cpp)
 * 【備考】旧は起動形態のビット(f.usilic)を持つ。C++は起動形態を持たず、実装の選択(端末/GUI)だけをmainが行う。
 */
int vt_set(int f_usilic0)/*usilic*/
{
  static int f_usilic;

  if (f_usilic0) {
    f_usilic = f_usilic0;
    vt = tc_load();
  }
  return f_usilic;
}

/*
 * 【機能】端末のウィンドウの行数・桁数を、/dev/ttyへのioctl(TIOCGWINSZ)で取得する。取得できなければ
 *       環境変数(LINES/COLUMNS)などで代用する。
 * 【C++】TerminalDisplay::fromEnvironment (src/cpp/src/terminal_display.cpp)
 */
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

/*
 * 【機能】端末のウィンドウの大きさを、ioctl(TIOCSWINSZ)で設定する。
 * 【C++】Shell::start (src/cpp/src/shell.cpp)
 * 【備考】旧ソース内では、現在どこからも呼ばれていない(定義のみ)。C++は、SHELLの疑似端末の大きさを
 *       Shell::startで設定する。
 */
int set_winsize(int row, int col)/*usilic*/
{
  struct winsize winsize;

  winsize.ws_row = row;
  winsize.ws_col = col;
  if (ioctl(1, TIOCSWINSZ, &winsize) < 0)
    save_error("%s: set_winsize(): %s.\n", s_S(S_usilic), strerror(errno));
  return 0;
}

/*
 * 【機能】端末(/dev/tty)から1バイト読み込んで返す。0バイト(Ctrl+Space)は`K_at`として返す。
 * 【C++】KeyInput::readByte (src/cpp/src/key_input.cpp)
 */
int vt_getch(void)/*usilic*/
{
  int i;
  u_char c;

  if (!(i = read(vt_tfd(0), &c, 1)) || i < 0) return 0;
  return (c) ? c : K_at; /* Ctrl+Space */
}

/*
 * 【機能】端末の文字コード(ASCII/EUC/UTF-8)を設定する/返す(引数が0でなければ設定)。
 * 【C++】Charset::Encoding (src/cpp/src/charset.hpp)
 * 【備考】C++は、文字コードを値として引き回し、Charset::detectへterminal_encodingとして渡す。
 */
int vt_charset(int i)/*usilic*/
{
  static int charset;

  if (i) charset = i;
  return charset;
}

/*
 * 【機能】端末の大きさを取得して、行数・桁数を記憶し、環境変数LINES/COLUMNSへ反映する。ステータス行が
 *       無い端末では、スクロール領域も設定し直す。
 * 【C++】TerminalDisplay::setSize (src/cpp/src/terminal_display.cpp)
 */
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

/*
 * 【機能】行数・桁数を指定して設定し(SIGWINCH後など)、必要ならスクロール領域も設定し直す。
 * 【C++】TerminalDisplay::setSize (src/cpp/src/terminal_display.cpp)
 */
void vt_setwin(int row, int col)/*usilic*/
{
  vt_row(row);
  vt_col(col);
  if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
}

/*
 * 【機能】端末の入力に使うファイル記述子を、設定する/返す/閉じる。
 * 【C++】KeyInput::fd (src/cpp/src/key_input.cpp)
 */
int vt_tfd(int i)/*usilic*/
{
  static int tfd;

  if (i < 0) close(tfd);
  else if (i > 0) tfd = i;
  return tfd;
}

/*
 * 【機能】端末を、キー入力用のrawモード(ICANON/ECHO/ISIG等を無効)へ切り替える/元へ戻す。initの値で
 *       動作が違う: 1=初期化(/dev/ttyを開き、termiosの元の状態を保存して、rawモードにする)、
 *       2/-9=rawモードへ再設定(2はスクロール領域も設定し直す)、-1/-2=元のモードへ戻す(-1は
 *       さらに端末を閉じる)、3/-3=キー待ちの時間(VTIME)を短く/なしにする。
 * 【C++】KeyInput::open; KeyInput::close; KeyInput::leaveRawMode; KeyInput::reenterRawMode (src/cpp/src/key_input.cpp)
 */
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
    if ((usiliccharset = getenv(ENV_CHARSET)))
      /* config.hのCHARSETの既定値は"utf-8"なので、USILICCHARSETが
	 未設定の場合はinit_config()(str.c)のsetenv()経由でその値に
	 なる――しかし`export USILICCHARSET=`(未設定ではなく空文字列)
	 とすると、ここでのgetenv()は"ascii"にも"utf8"/"utf-8"にも
	 一致しない非NULLの空文字列を返し、ハードコードされたC_euc
	 へフォールスルーしてしまい、USILICCHARSETの値も~/.usilic/
	 historyも原因ではないのに、EUCの文字化けを静かに再発させて
	 いた。認識できない値(空文字列を含む)は、今は文書化された
	 既定値と一致するC_utf8へフォールスルーするようにした。 */
      vt_charset((!strcasecmp(usiliccharset, "ascii")) ? C_ascii
		 : (!strcasecmp(usiliccharset, "euc")) ? C_euc
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

