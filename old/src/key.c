#include "usilic.h"
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

static struct {
  struct {
    u_int winch	: 1;
    u_int term	: 1;
    u_int ctrl	: 1;
  } f;
} Key;

typedef int (*GETCH)(void);

static int is_esc(char *ks[], char esc[])
{
  int i;

  for (i = 0; ks[i] && (ks[i][0] != esc[0] || ks[i][2] != esc[2]); i++) ;
  return (ks[i]) ? i : 0;
}

static void set_name(STR *sp0, char name[])
{
  STR *sp;
  int i, c, len;

  if (!sp0) return;
  for (len = strlen(name), i = 0; ; len++) {
    if (!(c = sp0->name[len])) break;
    for (sp = sp0->next; sp && sp->name[len] == c; sp = sp->next) ;
    if (sp) break;
  }
  if ((len -= (i = strlen(name)))) {
    memcpy(&name[i], &sp0->name[i], len);
    name[i + len] = '\0';
  }
}

static int key_getch(int f_key, u_char cva[])
{
  static u_char *cvp, cvp0[N_line];
  int i;

  if (f_key == 2 || f_key == 3) {
    if ((i = strlen(cva)) >= N_line) {
      memcpy(cvp0, cva, N_line - 1);
      cvp0[N_line-1] = '\0';
      i = N_line - 1;
    } else strcpy(cvp0, cva);
    cvp = cvp0;
    if (f_key == 2) vt_mode(-'d');
    return 0;
  } else if (!(i = *cvp++)) return -1;
  else if (load_intr() == S_err0) while ((i = *cvp)) cvp++;
  if (!*cvp) vt_mode('d');
  return i;
}

/* key_getch()'s "playback" mode (its else-branch) doesn't touch f_key/cva,
   so it was called through `getch` -- typedef'd as a 0-argument GETCH --
   relying on whatever garbage was left in the argument registers to not
   coincidentally equal 2 or 3 (key_getch's "setup" mode, which dereferences
   cva). That's UB and was observed to crash: this wrapper matches GETCH's
   real signature and passes a value guaranteed not to be 2 or 3. */
static int key_getch_play(void)
{
  return key_getch(1, NULL);
}

static int set_km(int m, int **p_kmp, int f_M)
{ /* keyboard macro */
  static int f_me, n_km, a_km[N_macro];

  switch(f_M) {
  case -1:
    return *a_km;
  case M_ms:
    f_me = n_km = 0;
    break;
  case 0:
    a_km[n_km++] = m;
    a_km[n_km] = 0;
    if (n_km < N_macro-1) break;
  case M_me:
    f_me = 1;
    n_km = -1;
    break;
  case M_mm:
    if (!f_me) {
      a_km[n_km++] = f_M; /* endless loop */
      a_km[n_km] = 0;
      f_me = 1;
    }
    *p_kmp = a_km;
    break;
  }
  return n_km + 1;
}

static void key_ttou(int dummy)
{
  signal(SIGTTOU, key_ttou);
  if (s_S(S_pid)) print_pid(getpid());
  if (s_S(S_error)) kill(getpid(), SIGTSTP);
  else killpg(getpgrp(), SIGTSTP);
}

static void key_cont(int dummy)
{
  signal(SIGCONT, key_cont);
  if (s_S(S_pid)) print_pid(getpid());
  vt_init(-9); /* check SIGTTOU */
}

void key_winch(int spid0)/*usilic*/
{
  static int spid;

  if (spid0 < 0) {
    spid = -spid0;
    return;
  }   
  signal(SIGWINCH, key_winch);	
  vt_winsize();
  if (spid && !kill(spid, 0)) vt_mode('w');
  Key.f.winch = 1;
}

static void key_segv(int dummy)
{
  save_stderr("%s: %s.\n", s_S(S_usilic), s_S(S_err8));
  exit_usilic();
  print_stderr(NULL);
  exit(1);
}

void key_term(int dummy)/*usilic*/
{
  int pid;

  if ((pid = get_hup(0))) kill(pid, SIGKILL);
  unlink_file();
  Key.f.term = 1;
}

static void stand_str(RV *rvp0, int f_M)
{
  static RV *rvp;
  static PP p;
  int i, len, n, f_enter, n_attr, f_stand, f_grep;
  static ATTR pp_attr[N_attr]; /* display control-code (^?) */

  if (!vt_mode('D') || rvp0 == F11 || f_M == M_shell 
      || f_M == T_arc3 || f_M == T_input) return;
  if (is_comm(f_M, 0)) f_enter = f_M;
  else if ((i = is_comm(f_M, 1))) f_enter = i;
  else f_enter = 0;
  if (rvp0) {
    rvp = rvp0;
    vt_stand();
    f_stand = 1;
  } else if (!rvp) return;
  else {
    vt_endstand();
    f_stand = -1;
  }
  vt_move(rvp->row, rvp->col);
  p.attr = NULL;
  f_grep = f_M == M_egrep || f_M == M_cut 
    || f_M == T_hfgrep || f_M == T_hegrep;
  if (strlen(rvp->str)) p.cs = strdup(&rvp->pp->cs[(f_enter) ? 0 : rvp->col]);
  else return;
  n_attr = set_attr(pp_attr, p.cs, f_stand, f_grep);
  if (vt_charset(0) == C_ascii) {
    for (i = 0, n = rvp->col, len = strlen(p.cs); len; len--) {
      if ((p.cs[i] >= 0x80 && (n += 4) <= vt_col(0))
	  || (p.cs[i] < ' ' && (n += 2) <= vt_col(0))
	  || ++n <= vt_col(0)) i++;
      else break;
    }
    p.cs[i] = '\0';
    p.f.len = strlen(p.cs);
  } else {
    if (strlen(p.cs) + rvp->col > vt_col(0)) p.cs[vt_col(0) - rvp->col] = '\0';
    p.f.len = strlen(p.cs);
    if (count_k(p.cs, p.f.len)) p.f.len--;
  }
  if (f_enter) p.split = rvp->pp->split;
  else p.split = (f_M == T_grep && f_stand > 0) ? -f_M : 0;
  if (n_attr) {
    pp_attr[n_attr++].n = -1;
    p.attr = pp_attr;
  }
  put_str(&p, f_enter);
  if (f_enter) vt_move(rvp->row, 0);
  free(p.cs);
  vt_endstand();
}

static int kton(int k, int key_c[])
{
  int i, i_tab;

  for (i_tab = 0; key_c[i_tab]; i_tab++) ;
  for (i = 0; i < i_tab && key_c[i] != k; i++) ;
  return (i < i_tab) ? i : 0;
}

static void key_eeol(int row, int col0, int len, int f_exit)
{
  if (!f_exit) vt_endstand();
  vt_move(row, col0);
  while (len-- > 0) vt_putchar(' ');
  vt_move(row, col0);
}

/* UTF-8: on-screen column and byte offset into `buff' coincide for
   ASCII (1 byte = 1 column) and for EUC's "wide" pairs (always
   exactly 2 bytes = 2 columns), so this file has always tracked
   col/cole/shift (and *colp/*colep/*shiftp) as byte offsets and fed
   them straight to vt_move() as columns too, and compared them
   directly against the field width `len' (itself a column count) to
   decide when the field is too full and needs to scroll. A UTF-8
   character can be 1-4 bytes but only 0-2 columns, breaking both of
   those: byte length can exceed `len' well before the real on-screen
   width does (triggering the "please scroll" branch far too early),
   and the resulting `shift' byte offset -- computed from byte counts
   -- isn't guaranteed to land on a character boundary, splitting a
   multi-byte sequence and corrupting the redraw (a stray continuation
   byte, missing characters). These helpers bridge byte offsets to
   real screen columns (and back) so the rest of this file's byte-based
   indexing doesn't need to change. */
static int char_bytes_before(u_char *buff, int pos)
{
  int end = pos - 1, start;

  if (vt_charset(0) != C_utf8) return count_k(buff, pos) ? 2 : 1;
  if (end < 0) return 0;
  for (start = end; start > 0 && (buff[start] & 0xc0) == 0x80; start--) ;
  return end - start + 1;
}

static int b2col(u_char *buff, int shift, int bytepos)
{
  int pos, col, sl;

  if (vt_charset(0) != C_utf8) return bytepos - shift;
  for (pos = shift, col = 0; pos < bytepos && buff[pos]; pos += sl) {
    sl = utf8_seqlen(buff[pos]);
    col += utf8_width(&buff[pos], sl);
  }
  return col;
}

static int utf8_total_width(u_char *buff)
{
  int total = 0, pos, sl;

  for (pos = 0; buff[pos]; pos += sl) {
    sl = utf8_seqlen(buff[pos]);
    total += utf8_width(&buff[pos], sl);
  }
  return total;
}

/* byte offset that puts the last `len' on-screen columns of `buff'
   into view (i.e. where "scroll right-aligned to fit `len' columns"
   should start reading from), always landing on a character start. */
static int utf8_right_shift(u_char *buff, int len)
{
  int col = utf8_total_width(buff) - len, pos, sl;

  for (pos = 0; buff[pos] && col > 0; ) {
    sl = utf8_seqlen(buff[pos]);
    col -= utf8_width(&buff[pos], sl);
    pos += sl;
  }
  return pos;
}

/* returns 1 and sets *shiftp to the byte offset to scroll to (and,
   if colp is non-NULL, *colp to the cursor position that goes with
   it) if `buff' is wider than `len' columns; returns 0 (leaving
   *shiftp/*colp untouched) if it already fits. Replaces the various
   `(shift = cole - col0 - len) > 0' checks scattered through this
   file, all of which compared a byte count (cole - col0) against a
   column count (len) -- harmless for ASCII/EUC where the two always
   match, but wrong for UTF-8 (see the comment above char_bytes_before
   for the full story). Callers that set `col = col0 + len' right
   after the old check need *colp too: col/shift are byte offsets
   (again, see char_bytes_before), so the cursor at the end of a
   scrolled-into-view UTF-8 buffer sits at col0 + the *byte* length of
   the visible portion, not col0 + len (a column count) -- using the
   latter desyncs col from the buffer and corrupts the next insert. */
static int need_scroll(u_char *buff, int col0, int cole, int len, int *shiftp, int *colp)
{
  if (vt_charset(0) == C_utf8) {
    if (utf8_total_width(buff) <= len) return 0;
    *shiftp = utf8_right_shift(buff, len);
    if (colp) *colp = col0 + (int)strlen((char*)buff) - *shiftp;
    return 1;
  }
  if (cole - col0 - len <= 0) return 0;
  *shiftp = cole - col0 - len;
  if (colp) *colp = col0 + len;
  return 1;
}

static int input_key(int f_M, u_char mess[], u_char *cs, int col0, char *cname, LIST **listp, STR *cmd, u_char iname[], char dir[], char old[], u_char copy[])
{
  int i, j, k, m, len, row, col, cole, shift, f_list, ti;
  u_char *cp, buff[N_line];
  static int ilen;
  static u_char buffb[N_line];

  row = vt_row(0) - 1;
  k = vt_col(0) / 4;
  switch(f_M) {
  case M_man2:
  case M_cut2:
  case M_locate:
  case T_locate:
  case M_fgrep:
    k = vt_col(0) / 2;
  case M_prog2:
  case T_rm:
  case T_cp:
  case T_mv:
  case T_arc:
  case T_arc3:
  case M_lfile2:
  case M_ldir2:
    *buff = '\0';
    break;
  case M_exit:
    strcpy(buff, "y");
    k = 17;
    break;
  case M_man:
    k = vt_col(0) / 2;
  default:
    if (!cs || cs == F3) *buff = '\0';
    else strcpy(buff, cs);
    if (cname) sprintf(strchr(buff, '\0'), " %s", cname);
    break;
  }
  if ((i = *iname)) {
    if (i == K_a0 && !*buffb) {
      m = 0;
      *iname = '\0';
    } else {
      if (*buffb) strcpy(buff, buffb);
      else ilen = 0;
      if (i == K_a0) buff[ilen] = *buffb = '\0';
      else strcpy(&buff[ilen], iname);
      *iname = '\0';
      col = cole = strlen(buff) + col0;
      m = -K_e;
    }
  } else m = 0;
  col = cole = col0 + strlen(buff);
  len = vt_col(0) - col0 - k - 1;
  if (f_M == M_exit) len = 2;
  col = (col > col0 + len) ? col0 + len : col;
  shift = f_list = 0;
  key_eeol(row, col0, len, f_M == M_exit);
  vt_move(row, col0);
  if ((i = *buff)) {
    if (i == K_a0) *buff = '\0';
    else if (need_scroll(buff, col0, cole, len, &shift, NULL)) {
      print_buff(&buff[shift], count_k(buff, shift));
    } else print_buff(buff, 0);
  }
  vt_flush();
  for (m = 0; ; m = (m > 0) ? 0 : m
       , f_list = (f_list != 1) ? 0 : f_list) {
    m = edit_line(m, T_input, buff, len, row, col0, &col, &cole, &shift, copy);
    switch(m) {
    case 0:
    case N_char:
      break;
    case M_hist:
      switch(f_M) {
      case T_hist:
      case T_hfile:
      case T_hdir:
      case T_hman:
      case T_hlocate:
      case T_hfgrep:
      case T_hegrep:
	echo_mess(S_errv);
	continue;
      }
    case M_cmd:
      if (f_M == T_cmd) continue;
    case M_list:
      if (m == M_cmd || m == M_hist) { 
	if (f_M != M_fgrep && f_M != M_egrep) ilen = 0;
      } else if (f_M == T_list 
	  || f_M == M_locate || f_M == T_locate
	  || f_M == M_fgrep || f_M == M_egrep) continue;
      if (m == M_list) {
	*listp = free_list(*listp);
	if ((ti = col + shift - col0)) buff[ti] = '\0';
	if (strchr(buff, '\0')[-1] == '~')
	  strcpy(&strchr(buff, '\0')[-1], s_S(S_home));
	else if (strstr(buff, "~/")) {
	  char *p;
	  cp = &strstr(buff, "~/")[2];
	  p = alloca(strlen(cp) + 1);
	  strcpy(p, cp);
	  sprintf(&cp[-2], "%s%s", s_S(S_home), p);
	}
	{
	  char *p = alloca(strlen(buff) + 1);
	  if (*buff != '/') {
	    p = alloca(strlen(dir) + 1);
	    strcpy(p, dir);
	  } else {
	    p = alloca(strlen(buff) + 1);
	    strcpy(p, buff);
	    get_dir(p);
	  }
	  i = make_list(p, buff, &ilen, listp, f_M);
	}
	if (*buffb) *buffb = '\0';
	col = cole = col0 + strlen(buff);
	len = vt_col(0) - col0 - k - 1;
	if (!need_scroll(buff, col0, cole, len, &shift, &col)) shift = 0;
      } else i = -m;
      switch(i) {
      case 0:
	break;
      case 1:
	col = cole = strlen(buff) + col0;
	m = -K_e;
	continue;
      default:
	if (i < 0) ;
	else if (!f_list) {
	  f_list = 1;
	  col = cole = strlen(buff) + col0;
	  m = -K_e;
	  vt_putchar(K_g);
	  continue;
	} else f_list = 0;
	strcpy(mess, &buff[ilen]);
	if (i < 0) {
	  m = (i == -M_cmd) ? -T_cmd : -T_hist;
	  strcat(buffb, buff);
	} else {
	  m = -T_list;
	  strcpy(buffb, buff);
	}
	goto jump;
      }
      break;
    case M_susp:
      vt_init(-2);
      killpg(getpgrp(), SIGTSTP);
      vt_winsize();
      vt_init(2);
      m = -M_clear;
      goto jump;
      break;
    default:
      if (m < N_char || m >= N_num) goto jump;
      else
	switch((m - N_char)) {
	case '\r':
	  i = M_cr;
	  if (!*buff)
	    if (f_M == T_locate)
	      if (!cs || cs == F3) *buff = '\0';
	      else strcpy(buff, cs);
	    else {
	      vt_putchar(K_g);
	      i = 0;
	    }
	  else {
	    j = 0;
	    switch(f_M) {
	    case T_locate:
	      i = T_cr;
	      break;
	    case T_hfile:
	    case M_lfile:
	      if ((cp = strstr(buff, " (")) || (cp = strstr(buff, " \042"))) {
		j = cp - buff;
		*cp = '\0';
	      } else j = 0;
	    case T_hdir:
	    case M_ldir:
	    case M_lfile2:
	    case M_ldir2:
	      if ((cp = strstr(buff, "//"))) memmove(buff, &cp[1], strlen((char*)&cp[1]) + 1);
	      else if (strchr(buff, '\0')[-1] == '~')
		strcpy(buff, s_S(S_home));
	      else if ((cp = strstr(buff, "~/"))) {
		char *p = alloca(strlen(&cp[2]) + 1);
		strcpy(p, &cp[2]);
		sprintf(buff, "%s%s", s_S(S_home), p);
	      }
	      while (*(cp = &strchr(buff, '\0')[-1]) == ' ' || *cp == '\t') 
		*cp = '\0';
	      if (access(buff, F_OK) < 0) {
		*buff = '\0';
		vt_putchar(K_g);
		if ((i = f_M == M_lfile || f_M == M_ldir
		     || f_M == T_hfile || f_M == T_hdir)) {
		  m = -M_clear;
		  goto jump;
		}
	      } else if ((f_M == M_lfile || f_M == T_hfile) && j) 
		buff[j] = ' ';
	      break;
	    default:
	      break;
	    }
	  }
	  if (shift) shift = 0;
	  col = cole = col0;
	  strcpy(mess, buff);
	  switch(f_M) {
	  case M_cut:
	  case M_cut2:
	  case M_fgrep:
	  case M_egrep:
	  case T_hegrep:
	  case T_hfgrep:
	    break;
	  default:
	    for (j = strlen(mess) - 1; j >= 0 && mess[j] == ' '; j--) ;
	    if (mess[j+1]) mess[j+1] = '\0';
	    break;
	  }
	  *buff = '\0';
	  m = i;
	  goto jump;
	  break;
	case K_s:
	  m = M_fgrep;
	  goto jump;
	  break;
	case K_w:
	  if (f_M == T_cp || f_M == T_mv || f_M == T_arc || f_M == T_arc3) {
	    char *p = alloca(1 + 1 + strlen(old) + 1);
	    sprintf(p, "%c%c%s", K_u, K_k, old);
	    read_m(NULL, 3, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
	  } else vt_putchar(K_g);
	  break;
	default:
          m = -m;
	  break;
	}
      break;
    }
  }
 jump:
  return m;
}  

int read_m(RV *rvp, int f_M, int klen, u_char *cva, char *cname, LIST **listp, STR *cmd, u_char *iname, char *dir, char *old)/*usilic*/
{
  static int key_k[32], key_r[8], key_g[14], key_y[6];
  static int n_bm, n_km, *p_km;
  static u_char *p_bm;
  static int *key_m, *key_c, k_wait;
  static GETCH getch = vt_getch;
  u_char *cp;
  int i, c, k, f_enter, f_list, rowb, colb;
  
  if (f_M == T_key2) {
    key_m = (int*)cva;
    key_c = (int*)cname;
    k_wait = conf_wait(NULL, NULL);
    read_esc(-1, key_r, key_c);

    /* copy buffer key */
    key_y[0] = -1;
    key_y[1] = K_a;
    key_y[2] = K_e;
    key_y[3] = K_b;
    key_y[4] = K_f;
    key_y[5] = 0;
    
    /* invalid command-line key */
    for (i = 0; i < 29; i++)
      switch(i) {
      case 0:
	key_k[i] = -1; /* return value 0 from ktoc() */
	break;
      case K_j:  /* ^J */	
      case K_o:  /* ^O */
      case K_t:  /* ^T */
      case K_x:  /* ^X */
      case K_ec:  /* ^[ */
	key_k[i] = -1; /* can be used for `F-?' in usilicrc */
	break;
      case 28:
	key_k[i] = 0;
	break;
      default:
	key_k[i] = i;
	break;
      }

    /* cursor key */
    key_r[0] = -1;
    key_r[1] = key_c[M_kup];
    key_r[2] = key_c[M_kdn];
    key_r[3] = key_c[M_knext];
    key_r[4] = key_c[M_kprev];
    key_r[5] = key_c[M_kback];
    key_r[6] = key_c[M_kforw];
    key_r[7] = 0;

    /* invalid key in T_input */
    key_g[0] = -1;
    key_g[1] = M_up;
    key_g[2] = M_dn;
    key_g[3] = M_fgrep;
    key_g[4] = M_egrep;
    key_g[5] = M_kforw;
    key_g[6] = M_kback;
    key_g[7] = M_head;
    key_g[8] = M_tail;
    key_g[9] = M_prev;
    key_g[10] = M_next;
    key_g[11] = M_kprev;
    key_g[12] = M_knext;
    key_g[13] = 0;

    return 0;
  }
  if (cva) {
    key_getch(f_M, cva);
    getch = key_getch_play;
    return 0;
  }
  if (is_comm(f_M, 1) || f_M == M_exit) f_enter = 2;
  else if (is_comm(f_M, 0)) f_enter = 1;
  else f_enter = 0;
  f_list = 1;
  switch(f_M) {
  case M_copy:
    c = 0;
    break;
  case M_fgrep:
  case M_locate:
  case M_man2:
  case M_cut2:
  case M_prog2:
  case M_lfile2:
  case M_ldir2:
    f_list = 0;
  case M_egrep: case M_cut:
  case M_man:
  case M_lfile: case M_ldir:
  case M_prog:
  case M_hist: case T_hist:
  case T_hfile: case T_hdir: case T_hman: case T_hlocate:
  case T_hfgrep: case T_hegrep:
  case M_cmd: case T_cmd:
  case M_list: case T_list:
  case T_locate:
    c = 1;
    break;
  default:
    c = f_enter == 2;
    break;
  }
  if (f_enter == 2) {
    vt_endstand();
    vt_move(vt_row(0) - 1, klen);
    switch(f_M) {
    case T_rm:
      vt_printf("Delete ? %s ", s_S(S_erry));
      klen += 9 + strlen(s_S(S_erry)) + 1;
      break;
    case M_exit:
      klen -= 2;
      break;
    default:
      vt_printf("Destination ? ");
      klen += 14;
      break;
    }
  }
  if (c) {
    int f_stand = 1;
    u_char *p = (rvp->pp && rvp->pp->tell != (uintptr_t)F7) ? rvp->pp->cs : NULL;
    if (*iname || !p) /* filename with no attribute on the file-list after */
      f_stand = 0;    /* `Enter' (select file) */
    if (f_list && f_stand) stand_str(rvp, f_M);
    while (1) {
      c = input_key(f_M, rvp->str, p, klen, (*cname) ? cname : NULL
		    , listp, cmd, iname, dir, old, rvp->copy);
      if (!f_list && strint(key_g, c)) vt_putchar(K_g);
      else break;
    }
    if (f_list && f_stand && c != M_cr && c != T_cr) stand_str(NULL, f_M);
    return c;
  }
  stand_str(rvp, f_M);  
  k = c = 0;
  do {
    if (Key.f.winch) {
      Key.f.winch = 0;
      return T_winch;
    } else if (Key.f.term) {
      Key.f.term = 0;
      return T_exit;
    } else if (!c) {
      if (getch != vt_getch) {
	if ((c = getch()) < 0) {
	  getch = vt_getch;
	  c = 0;
	  continue;
	}
      } else if (p_bm) {
	c = p_bm[n_bm++];
	if (!p_bm[n_bm]) {
	  if (getch == vt_getch) vt_mode('d');
	  free(p_bm);
	  p_bm = NULL;
	  n_bm = 0;
	}
	usleep(k_wait);
      } else if (p_km) {
	if ((c = p_km[n_km++]) == M_mm) {
	  set_km(0, &p_km, c);
	  n_km = 0;
	  c = p_km[n_km++];
	}
	if (vt_getch() || !p_km[n_km]) {
	  if (getch == vt_getch) vt_mode('d');
	  p_km = NULL;
	  n_km = 0;
	  vt_init(3);
	} else usleep(k_wait);
	stand_str(NULL, f_M);
	return c;
      }	else c = getch();
      if (f_M == M_getch && c) goto SET_KM;
      if (((c > 0 && c < ' ') || c == K_at) && Key.f.ctrl) goto SET_KM;
      if (f_M == M_copy && strint(key_y, c)) {
	c += N_char;
	goto SET_KM;
      }
      switch(c) {
      case K_at:
	if (f_M == M_shell || f_M == T_input) {
	  vt_putchar(K_g);
	  c = 0;
	}
	break;
      default:
	if (c >= 'A' && c <= 'D' && (f_M != M_shell && f_M != T_input)) 
	  c += 'a'-'A';
	break;
      }
    }
    if (c == K_ec)
      if (k >> 8 == K_ec) {
	c = M_exit;
	continue;
      } else {
	i = -1;
	if (getch == vt_getch && !(i = read_esc(f_M, NULL, NULL))) {
	  c = 0;
	  continue;
	} else if (i < 0) {
	  k = K_ec << 8;
	  c = 0;
	  continue;
	} else {
	  c = i;
	  goto SET_KM;
	}
      }
    else if (c == K_v) {
      c = M_kforw;
      continue;
    } else if (c == 'w' && k >> 8 == K_ec) {
      c = M_kback;
      continue;
    } else if (c == K_z) {
      c = M_susp;
      continue;
    } else if (c >= '0' && c <= '9') {
      if (c >= '1' && k >> 8 == key_c[M_num]) {
	if (rvp != F11)
	  disp_window(rvp->ps, rvp->pp, f_M, NULL, NULL, NULL, &k);
	if (!(c = print_line(NULL, NULL, (f_M == M_shell) ? -c : c, F2))) {
	  c = M_clear;
	  goto SET_KM;
	} else {
	  c += N_num;
	  goto SET_KM;
	}
      } else if (k >> 8 == key_c[M_cd]) {
	c += T_0 - '0';
	goto SET_KM;
      }
    }
    if (c) {
      if (Key.f.winch || Key.f.term) {
	c = 0;
	continue;
      }
      do {
	if (k) {
	  k |= c;
	  if (strint(key_m, k)) {
	    k <<= 8;
	    c = 0;
	    continue;
	  } else if (!(c = kton(k, key_c))) {
	    k = c = 0;
	    continue;
	  } else if (f_M == M_shell || f_M == T_input) continue;
	} else if (strint(key_m, c)
		   && (((f_M == M_shell || f_M == T_input) 
			&& !strint(key_k, c) && c < ' ')
		       || (f_M != M_shell && f_M != T_input))) { 
	  k = c << 8;
	  c = 0;
	  continue;
	} else k = c;
	if (f_M == M_shell || f_M == T_input)
	  if ((!strint(key_k, k) && k < ' ')
	      || (f_M == M_shell 
		  && k == K_s) /* ^S */
	      || (f_M == T_input && k == K_c)) { /* ^C */
	    vt_putchar(K_g);
	    k = c = 0;
	  } else {
	    switch(k) {
	    case K_l:	/* ^L */
	      c = M_clear;
	      break;
	    case K_i:	/* ^I */
	      c = M_list;
	      break;
	    case K_s:	/* ^S */
	      c = M_fgrep;
	      break;
	    case K_r:	/* ^R */
	      c = M_hist;
	      break;
	    case K_n:	/* ^N */
	      c = M_dn;
	      break;
	    case K_p:	/* ^P */
	      c = M_up;
	      break;
	    case K_g:	/* ^G */
	      c = M_bl;
	      break;
	    case K_z:	/* ^Z */
	      c = M_susp;
	      break;
	    case K_w:	/* ^W */
	      if (f_M != T_input) {
		c = T_w;
		break;
	      }
	    default:
	      c = k + N_char;
	      break;
	    }
	    k = 0;
	  }
	else if (!(c = kton(k, key_c))
		 || strchr(print_key(NULL), c)
		 || c == M_list) k = c = 0;
	else 
	  switch(c) {
	  case M_cmd:
	    c = T_cmd3;
	    break;
	  case M_hist:
	    c = T_hist2;
	    break;
	  case M_ms:
	    echo_mess(S_errp);
	    n_km = set_km(0, NULL, c);
	    if (p_km) p_km = NULL;
	    c = k = 0;
	    continue;
	  case M_me:
	    if (n_km) {
	      k = S_errq;
	      set_km(0, NULL, c);
	    } else k = S_errr;
	    echo_mess(k);
	    n_km = c = k = 0;
	    continue;
	  case M_mm:
	    if (!set_km(0, NULL, -1)) {
	      echo_mess(S_errr);
	      c = k = 0;
	      continue;
	    }
	    set_km(0, &p_km, c);
	    n_km = 0;
	    c = p_km[n_km++];
	    vt_init(-3);
	    break;
	  default:
	    if (c >= M__0 && c <= M__19) {
	      if (!(cp = s_S(S_macro + c - M__0))) {
		char *p = alloca(strlen(s_S(S_usilic)) + 10);
		sprintf(p, "%src: M-%d", s_S(S_usilic), c - M__0);
		save_intr(S_errs);
		print_intr(p, NULL);
	      } else p_bm = strdup(cp);
	      c = k = n_bm = 0;
	    }
	    break;
	  }
      } while (0);
      if (getch == vt_getch) stand_str(rvp, f_M);
    } else if (f_M == M_shell && !k) /* check access() ==> shell.c(2) */
      return 0;
#if 1
    if (!access(s_S(S_enter), F_OK)) {
      c = T_du2;
      vt_move(vt_row(0) - 1, 0);
      vt_putchar(K_g);
      vt_printf(" (%s%s%s)    ", vt_geta(A_md), s_S(S_errh), vt_geta(A_me));
      vt_move(vt_row(0) - 1, 2 + strlen(s_S(S_errh)) + 2);
      vt_flush();
      while (vt_getch() != K_m) ;
      f_M = T_input;
    }
#endif
  } while (!c);
  if (getch == vt_getch) stand_str(NULL, f_M);
  if (f_M != M_shell)
    if (c == M_susp) {
      rowb = vt_row(0);
      colb = vt_col(0);
      vt_endstand();
      vt_move(vt_row(0) - 1, 0);
      if (is_fm()) {
	vt_init(-2);
	kill(getpid(), SIGTSTP);
	vt_init(2);
      } else {
	vt_init(-2);
	killpg(getpgrp(), SIGTSTP);
	killpg(getpgrp(), SIGCONT);
	vt_init(2);
      }
      vt_winsize();
      if (rowb != vt_row(0) || colb != vt_col(0)) key_winch(0);
      c = M_clear;
    }
  if (c == M_ascii) {
    /* was a straight 2-way ASCII<->EUC toggle (pre-UTF-8 code); cycle
       through all three now instead of leaving UTF-8 users stuck
       toggling between ASCII and EUC with no way back to UTF-8. */
    vt_charset((vt_charset(0) == C_ascii) ? C_euc
	       : (vt_charset(0) == C_euc) ? C_utf8
	       : C_ascii);
    c = M_sel;
  }
SET_KM:
  if (getch == vt_getch && n_km && !p_km) n_km = set_km(c, NULL, 0);
  return c;
}

void load_signal(void)/*usilic*/
{
  int i;

  for (i = 1; i < NSIG; i++) signal(i, SIG_DFL);
}

void save_signal(int f_usilic)/*usilic*/
{
  int i;

  for (i = 1; i < NSIG; i++)
    switch(i) {
    case SIGWINCH:
      signal(i, key_winch);
      break;
    case SIGTERM:
    case SIGHUP:
      signal(i, key_term);
      break;
    case SIGSEGV:
      signal(i, key_segv);
      break;
    case SIGTTIN:
      signal(i, SIG_DFL);
      break;
    case SIGTTOU:
      signal(i, key_ttou);
      break;
    case SIGCONT:
      signal(i, key_cont);
      break;
    case SIGTSTP:
    case SIGCHLD:
      break;
    default:
      signal(i, SIG_IGN);
      break;
    }
}

int find_name(int n_name, STR **spp, LIST **listp, char name[], char str[], char bak[], int f_find)
{
  DIR *dirp;
  struct dirent *dp;
  struct stat st;
  STR *sp;
  PN pn;
  int i;
  char tmp[N_line];

  sp = *spp;
  if (!(dirp = opendir(str))) return n_name;
  while ((dp = readdir(dirp)))
    if ((dp->d_ino || !strcmp(dp->d_name, ".."))
	&& (!*name || (*name && !strncmp(dp->d_name, name, strlen(name))))) {
      if (f_find) {
	strcpy(tmp, str);
	strcat(tmp, dp->d_name);
	stat(tmp, &st);
	if ((((i = S_ISDIR(st.st_mode)) || !(st.st_mode & S_IXUSR))
	     && f_find == 1)
	    || (!i && f_find == 2)) continue;
      } else {
	strcpy(tmp, str);
	strcat(tmp, dp->d_name);
	stat(tmp, &st);
	i = S_ISDIR(st.st_mode);
      }
      if (i && strchr(dp->d_name, '\0')[-1] != '/') strcat(dp->d_name, "/");
      strcpy(bak, dp->d_name);
      if (*name) sp = save_str(sp, dp->d_name, &pn);
      *listp = save_list(*listp, dp->d_name, &pn);
      n_name++;
    }
  *spp = sp;
  closedir(dirp);
  return n_name;
}
  
void make_cmd(STR *sp, LIST **listp)/*usilic*/
{
  static PN pn;

  for (; sp; sp = sp->next) 
    if (sp->name) *listp = save_list(*listp, sp->name, &pn);
}

/* backslash-escape shell glob/metacharacters in a completed filename
   before inserting it into the SHELL command line (M_shell only --
   other make_list() callers are plain path-input fields that never
   reach a shell, where escaping would be wrong). Matches how
   bash-completion/zsh/fish etc. complete filenames containing these:
   without it, a name like "...[二]@20260702....ts" gets inserted
   verbatim, and unescaped "[...]" is a glob bracket expression to the
   shell -- one that can fail to match depending on what follows it
   (see the "202607*" case that behaved differently from
   "\[二\]202607*" in this session's investigation), leaving the user
   to track down by hand why a filename that "looks right" on the
   command line doesn't match anything. */
static void esc_shell(u_char dst[], u_char src[])
{
  static char sp[] = " \t[]{}()<>|&;'\"`$*?!~#\\";
  int i, j;

  for (i = j = 0; src[i]; i++) {
    if (strchr(sp, src[i])) dst[j++] = '\\';
    dst[j++] = src[i];
  }
  dst[j] = '\0';
}

int make_list(u_char dir0[], u_char buff[], int *lenp, LIST **listp, int f_M)/*usilic*/
{
  int c, i, n_name, f_prog, f_dir;
  static char nc[] = "/<>()|; ";
  char *cp, *dir, name[N_line], bak[N_line];
  STR *sp;

  switch(f_M) {
  case M_prog: case M_prog2:
  case M_man: case M_man2:
  case M_shell:
    if (*buff != '.') {
      for (i = 0; (c = buff[i]) && !strchr(nc, c); i++) ;
      f_prog = !c;
    } else f_prog = 0;
    f_dir = 0;
    break;
  case M_ldir: case M_ldir2:
  case T_arc: case T_arc3:
    f_prog = 0;
    f_dir = 2;
    break;
  default:
    f_dir = f_prog = 0;
    break;
  }
  if (f_prog) {
    strcpy(name, buff);
    dir = malloc(strlen(s_S(S_path)) + 1);
  } else {
    if (f_M != M_shell && (i = ichrstr(buff, 0, "//"))) i--;
    else 
      for (i = strlen(buff); i; i--) 
	if ((c = buff[i]) && strchr(&nc[1], c)) break;
    if (i) i++;
    if (buff[i]) {
      if (buff[i] == '/') {
	dir = malloc(strlen(&buff[i]) + 1 + 1);
	strcpy(dir, &buff[i]);
      } else {
	u_char *p = alloca(strlen(&buff[i]) + 1);
	strcpy(p, &buff[i]);
	dir = malloc(strlen(dir0) + strlen(p) + 1 + 1);
	strcpy(dir, dir0);
	strcat(dir, p);
      }
      strcpy(name, dir);
      get_dir(dir);
      if (strchr(dir, '\0')[-1] != '/') strcat(dir, "/");
      get_name(name);
    } else {
      dir = malloc(strlen(dir0) + 1);
      strcpy(dir, dir0);
      *name = '\0';
    }
  }
  sp = NULL;
  if (f_prog) {
    char *path = malloc(strlen(s_S(S_path)) + 1);
    char *str = malloc(strlen(s_S(S_path)) + 1);
    n_name = 0;
    strcpy(path, s_S(S_path));
    cp = path;
    while (1) {
      strcpy(dir, cp);
      if ((cp = strchr(dir, ':'))) {
	*cp = '\0';
	strcpy(str, dir);
	strcat(str, "/");
        n_name = find_name(n_name, &sp, listp, name, str, bak, f_prog);
	*cp++ = ':';
      } else {
	strcat(dir, "/");
        n_name = find_name(n_name, &sp, listp, name, dir, bak, f_prog);
	break;
      }
    }
    free(path);
    free(str);
  } else n_name = find_name(0, &sp, listp, name, dir, bak, f_dir);
  if (f_prog) i = 0;
  else {
    c = 0;
    i = strlen(buff);
    do {
      if ((c = buff[i]) && strchr(nc, c)) break;
      if (!i) break;
    } while (i--);
    if ((!i && c == '/') || i) i++;
  }
  if (!*name) {
    if (!f_prog && n_name == 1) {
      if (f_M == M_shell) esc_shell(&buff[i], bak);
      else strcpy(&buff[i], bak);
    }
  } else if (n_name) {
    set_name(sp, name);
    if (f_M == M_shell) esc_shell(&buff[i], name);
    else strcpy(&buff[i], name);
  }
  if (n_name == 1) {
    if (f_M == M_shell)
      if (strchr(bak, '\0')[-1] == '/') {
	if (strchr(buff, '\0')[-1] != '/') strcat(buff, "/");
      } else strcat(buff, " ");
    else if (!f_prog && f_dir && strchr(buff, '\0')[-1] != '/') 
      strcat(buff, "/");
  }
  free_str(sp);
  *lenp = i;
  free(dir);
  return n_name;
}

static void add_buff(int m, int *shiftp, int row, int *colp, int col0, int len, int *colep, u_char back[], u_char buff[])
{
  int i;
  static int u8_len, u8_i;
  static u_char u8_buf[4];
  u_char seq[4];
  int slen;

  if (vt_charset(0) == C_utf8 && (u8_len || (m >= 0x80 && m < 0x100))) {
    /* Buffer a UTF-8 sequence across calls (this function is fed one
       byte per call, from edit_line()'s per-keystroke loop). Inserting
       and drawing it one byte at a time would put a vt_move() between
       its bytes -- edit_line() moves the cursor right before every
       call, and this function itself moves it again at the end -- which
       tears the sequence in two on the wire. Terminals see an
       incomplete multi-byte sequence interrupted by a cursor-position
       escape and render the pieces as replacement characters ("?"),
       even though every byte's *value* was correct all along (this is
       why read_line()'s own byte log, on the output/echo side, never
       showed any corruption: the break happens only here, on the
       keystroke-echo side). */
    if (!u8_len) u8_len = utf8_seqlen(m);
    u8_buf[(u8_i < 4) ? u8_i : 3] = m;
    if (++u8_i < u8_len) return;
    slen = (u8_i < 4) ? u8_i : 4;
    memcpy(seq, u8_buf, slen);
    u8_len = u8_i = 0;
  } else {
    seq[0] = m;
    slen = 1;
  }

  if (strlen(buff) > N_line - 2 - slen) {
    vt_putchar(K_g);
    return;
  }
  if (*colp < col0 + len && (*shiftp || (!*shiftp && *colp < *colep))) {
    i = *shiftp + *colp - col0;
    strcpy(back, &buff[i]);
    memmove(&buff[i + slen], back, strlen((char*)back) + 1);
    memcpy(&buff[i], seq, slen);
    strcpy(back, &buff[*shiftp]);
    back[len] = '\0';
    vt_move(row, col0);
    print_buff(back, count_k(buff, *shiftp));
    *colp += slen;
    *colep += slen;
    vt_move(row, col0 + b2col(buff, *shiftp, *shiftp + *colp - col0));
  } else if (vt_charset(0) == C_utf8) {
    /* Append (or extend the currently-scrolled view with) the new
       character. The byte-at-a-time "scroll by exactly 1 byte, save
       whatever fell off the left edge" dance in the EUC/ASCII branch
       below assumes 1 byte = 1 column; shifting a UTF-8 buffer by a
       fixed 1 byte can land mid-character, corrupting the view (a
       stray continuation byte, dropped characters). Simpler and
       always correct: append the bytes, then let need_scroll() (the
       same helper edit_line()'s K_e/K_u/K_y/paste use) recompute the
       right-aligned shift for the new, longer content from scratch. */
    int oldlen = strlen((char*)buff);

    memcpy(&buff[oldlen], seq, slen);
    buff[oldlen + slen] = '\0';
    *colep += slen;
    if (need_scroll(buff, col0, *colep, len, shiftp, colp)) {
      vt_move(row, col0);
      print_buff(&buff[*shiftp], count_k(buff, *shiftp));
    } else {
      key_eeol(row, col0, len, 0);
      print_buff(buff, 0);
      *colp = *colep;
    }
  } else {
    i = 0;
    if (*colp == col0 + len) {
      (*shiftp)++;
      vt_move(row, col0);
      if ((i = buff[*colp - col0 + *shiftp - 1])) {
	strcpy(back, &buff[*colp - col0 + *shiftp - 1]);
	buff[*colp-col0 + *shiftp - 1] = '\0';
      }
      print_buff(&buff[*shiftp], count_k(buff, *shiftp));
    }
    if ((m > 0 && m < ' ') || m == K_at) {
      vt_stand();
      vt_putchar(((m == K_at) ? 0 : m) + '@');
      vt_endstand();
    } else vt_putchar(m);
    vt_flush();
    memcpy(&buff[*colp-col0 + ((*shiftp) ? *shiftp - 1 : 0)], seq, slen);
    buff[*colp-col0 + ((*shiftp) ? *shiftp - 1 : 0) + slen] = '\0';
    *colep += slen;
    if (i) strcat(buff, back);
    if (!*shiftp) *colp = *colep;
    else strcpy(back, &buff[*shiftp]);
    vt_move(row, *colp);
  }
}

int edit_line(int m, int mb, u_char buff[], int len, int row, int col0, int *colp, int *colep, int *shiftp, u_char *copy)/*usilic*/
{
  int i, col, cole, shift;
  static u_char back[N_line+1], yank[N_line+1];

  col = *colp;
  cole = *colep;
  shift = *shiftp;
  for (; ; m = (m > 0) ? 0 : m) {
    if (!m) m = read_m(F11, mb, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (Key.f.ctrl) {
      Key.f.ctrl = 0;
      if (((m > 0 && m < ' ') || m == K_dl || m == K_at)) {
	vt_move(row, col0 + b2col(buff, shift, col - col0 + shift));
	add_buff(m, &shift, row, &col, col0, len, &cole, back, buff);
	vt_flush();
	continue;
      }
    }
    if (m == M_paste)
      if (copy) {
	vt_move(row, col0);
	if ((i = strlen(buff) + strlen(copy) + 1 - N_line) > 0) {
	  vt_putchar(K_g);
	  strchr(copy, '\0')[-i] = '\0';
	}
	if (col == cole) strcat(buff, copy);
	else {
	  i = col - col0 + shift;
	  memmove(&buff[i + strlen(copy)], &buff[i], strlen(&buff[i]) + 1);
	  memcpy(&buff[i], copy, strlen(copy));
	}
	cole = strlen(buff) + col0;
	if (need_scroll(buff, col0, cole, len, &shift, &col)) {
	  strcpy(back, &buff[shift]);
	  print_buff(back, count_k(buff, shift));
	} else {
	  key_eeol(row, col0, len, 0);
	  print_buff(buff, 0);
	  col = cole;
	}
	vt_flush();
      } else m = 0;
    else if ((m >= 0 && m <= N_char) || m >= N_num) break;
    else {
      switch(((m > 0) ? m - N_char : m)) {
      case 0:
	continue;
      case K_q:
	Key.f.ctrl = 1;
	break;
      case K_u: /* ^U */
	i = shift + col - col0;
	cole -= i;
	memcpy(yank, buff, i);
	yank[i] = '\0';
	memmove(buff, &buff[i], strlen((char*)&buff[i]) + 1);
	if (!need_scroll(buff, col0, cole, len, &shift, NULL)) shift = 0;
	strcpy(back, &buff[shift]);
	col = col0;
	key_eeol(row, col0, len, 0);
	print_buff(back, count_k(buff, shift));
	vt_move(row, col);
	vt_flush();
	break;
      case K_k: /* ^K */
	if (shift) {
	  cole = shift + col;
	  strcpy(yank, &buff[cole - col0]);
	  buff[cole - col0] = '\0';
	  if (!need_scroll(buff, col0, cole, len, &shift, &col)) {
	    shift = 0;
	    col = cole;
	  }
	  strcpy(back, &buff[shift]);
	  key_eeol(row, col0, len, 0);
	  print_buff(back, count_k(buff, shift));
	} else {
	  strcpy(yank, &buff[col - col0]);
	  buff[col - col0] = '\0';
	  key_eeol(row, col, len - (col - col0), 0);
	  cole = col;
	}
	break;
      case K_y: /* ^Y */
	vt_move(row, col0);
	if ((i = strlen(buff) + strlen(yank) + 1 - N_line) > 0) {
	  vt_putchar(K_g);
	  strchr(yank, '\0')[-i] = '\0';
	}
	if (col == cole) strcat(buff, yank);
	else {
	  i = col - col0 + shift;
	  memmove(&buff[i + strlen(yank)], &buff[i], strlen(&buff[i]) + 1);
	  memcpy(&buff[i], yank, strlen(yank));
	}
	cole = strlen(buff) + col0;
	if (need_scroll(buff, col0, cole, len, &shift, &col)) {
	  strcpy(back, &buff[shift]);
	  print_buff(back, count_k(buff, shift));
	} else {
	  key_eeol(row, col0, len, 0);
	  print_buff(buff, 0);
	  col = cole;
	}
	break;
      case K_dl:
      case K_h: /* ^H */
	if (col <= col0) {
	  vt_putchar(K_g);
	  continue;
	} else {
	  if (shift) {
	    if ((i = count_k(buff, col - col0 + shift - 1))) cole--;
	    memmove(&buff[col - col0 + shift - 1 - i]
		   , &buff[col - col0 + shift
			  + count_k(buff, col - col0 + shift)]
		   , strlen((char*)&buff[col - col0 + shift
			  + count_k(buff, col - col0 + shift)]) + 1);
	    if (i && shift) shift--;
	    if (shift) shift--;
	    else if (i) col--;
	    strcpy(back, &buff[shift]);
	    cole--;
	  } else if (vt_charset(0) == C_utf8) {
	    /* count_k()'s "?2:1" below assumes a fixed-width EUC pair;
	       a UTF-8 character deleted here can be 1-4 bytes, so find
	       its real length instead of guessing 2. */
	    i = char_bytes_before(buff, col - col0);
	    if (col == cole) buff[cole - col0 - i] = '\0';
	    else memmove(&buff[col - col0 - i], &buff[col - col0]
			 , strlen((char*)&buff[col - col0]) + 1);
	    col -= i;
	    cole -= i;
	    strcpy(back, buff);
	  } else {
	    if (col == cole) {
	      i = (count_k(buff, col - col0 - 1)) ? 2 : 1;
	      buff[cole - col0 - i--] = '\0';
	      col -= i;
	      cole -= i;
	    } else {
	      if ((i = count_k(buff, col - col0 - 1))) cole--;
	      memmove(&buff[col - col0 - 1 - i], &buff[col - col0]
		     , strlen((char*)&buff[col - col0]) + 1);
	      if (i) col--;
	    }
	    col--;
	    cole--;
	    strcpy(back, buff);
	  }
	  back[len] = '\0';
	  key_eeol(row, col0, len, 0);
	  print_buff(back, count_k(buff, shift));
	  vt_move(row, col0 + b2col(buff, shift, col - col0 + shift));
	}
	break;
      case K_d: /* ^D */
	if ((shift && col >= col0 + len)
	    || (!shift && (cole == col0 || cole == col))) {
	  vt_putchar(K_g);
	  continue;
	}
	if (shift) {
	  if ((i = count_k(buff, col - col0 + shift + 1))) cole--;
	  memmove(&buff[col - col0 + shift], &buff[col - col0 + shift + 1 + i]
		 , strlen((char*)&buff[col - col0 + shift + 1 + i]) + 1);
	  if (i) {
	    shift--;
	    col++;
	  }
	  if (shift) shift--;
	  else if (i) col++;
	  strcpy(back, &buff[shift]);
	  col++;
	  cole--;
	} else if (vt_charset(0) == C_utf8) {
	  /* the >=0x80 + count_k check below assumes a fixed-width EUC
	     pair; the character at the cursor here can be 1-4 bytes. */
	  i = utf8_seqlen(buff[col - col0]);
	  memmove(&buff[col - col0], &buff[col - col0 + i]
		 , strlen((char*)&buff[col - col0 + i]) + 1);
	  cole -= i;
	  strcpy(back, buff);
	} else {
	  i = buff[col - col0] >= 0x80;
	  memmove(&buff[col - col0], &buff[col - col0 + 1]
		 , strlen((char*)&buff[col - col0 + 1]) + 1);
	  if (i && count_k(buff, col - col0 + 1)) {
	    cole--;
	    memmove(&buff[col - col0], &buff[col - col0 + 1]
		   , strlen((char*)&buff[col - col0 + 1]) + 1);
	  }
	  cole--;
	  strcpy(back, buff);
	}
	back[len] = '\0';
	key_eeol(row, col0, len, 0);
	print_buff(back, count_k(buff, shift));
	vt_move(row, col0 + b2col(buff, shift, col - col0 + shift));
	break;
      case K_a: /* ^A */
	vt_move(row, (col = col0));
	if (shift) {
	  strcpy(back, buff);
	  back[len] = '\0';
	  print_buff(back, 0);
	  vt_move(row, col);
	  vt_flush();
	  shift = 0;
	}
	break;
      case -K_e:
	m -= m;
      case K_e: /* ^E */
	vt_move(row, col0);
	if (need_scroll(buff, col0, cole, len, &shift, &col)) {
	  strcpy(back, &buff[shift]);
	  print_buff(back, count_k(buff, shift));
	} else {
	  key_eeol(row, col0, len, 0);
	  print_buff(buff, 0);
	  col = cole;
	}
	break;
      case K_b: /* ^B */
	if (!*buff) {
	  vt_putchar(K_g);
	  continue;
	}
	if (col <= col0) {
	  if (shift) {
	    if (count_k(buff, shift - 1)) shift--;
	    strcpy(back, &buff[--shift]);
	    back[len] = '\0';
	    vt_move(row, col0);
	    print_buff(back, count_k(buff, shift));
	    vt_move(row, col0);
	    vt_flush();
	  } else {
	    vt_putchar(K_g);
	    continue;
	  }
	} else if (vt_charset(0) == C_utf8) {
	  i = char_bytes_before(buff, col - col0 + shift);
	  col -= i;
	  vt_move(row, col0 + b2col(buff, shift, col - col0 + shift));
	} else {
	  vt_move(row, --col);
	  if (count_k(buff, col - col0 + shift)) vt_move(row, --col);
	}
	break;
      case K_f: /* ^F */
	if (col >= cole) {
	  vt_putchar(K_g);
	  continue;
	}
	if (col < col0 + len - 1 && vt_charset(0) == C_utf8) {
	  i = utf8_seqlen(buff[col - col0 + shift]);
	  col += i;
	  vt_move(row, col0 + b2col(buff, shift, col - col0 + shift));
	} else if (col < col0 + len - 1) {
	  vt_move(row, ++col);
	  if (count_k(buff, col - col0 + shift)) vt_move(row, ++col);
	} else {
	  if (shift+len >= cole - col0) {
	    vt_putchar(K_g);
	    continue;
	  }
	  if (count_k(buff, shift + 1)) shift++; 
	  strcpy(back, &buff[++shift]);
	  back[len] = '\0';
	  vt_move(row, col0);
	  print_buff(back, count_k(buff, shift));
	  vt_flush();
	}
	break;
      case K_c: /* ^C */
      case '\r':
	goto jump;
      default:
	if (m > 0) goto jump;
	else m = -m - N_char;
	vt_move(row, col0 + b2col(buff, shift, col - col0 + shift));
	add_buff(m, &shift, row, &col, col0, len, &cole, back, buff);
	break;
      }
      vt_flush();
    }
  }
 jump:
  *colp = col;
  *colep = cole;
  *shiftp = shift;
  return m;
}

int read_esc(int f_M, int *key_r0, int *key_c0)/*usilic*/
{
  int i, c;
  static char *ks[7], esc[8] = {K_ec, };
  static int *key_r, *key_c;
  char *cp;

  if (f_M < 0) {
    vt_ks(ks);
    key_r = key_r0;
    key_c = key_c0;
    return 0;
  }
  i = strlen(ks[0]) - 1;
  for (cp = &esc[1]; i && (*cp = vt_getch()); i--, cp++) ;
  if (esc[1]) {
    i = is_esc(ks, esc);
    esc[1] = '\0';
    if (ks[i]) {
      if (i >= 4) vt_getch();
      stand_str(NULL, f_M);
      c = kton(key_r[i+1], key_c) + ((f_M == T_shell) ? N_char : 0);
    } else c = 0;
  } else c = -1;
  return c;
}
