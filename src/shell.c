#include "tcb.h"
#if 0
#define CHECK_SFD
#endif
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

static struct {
  struct {
    u_int nobl	: 1;
    u_int init	: 2;
    u_int alen	: 4;
    u_int blen	: 4;
    u_int clen	: 5;
  } f;
} Shell;

typedef struct {
  u_int bash	: 1;
  u_int bs	: 2;
  u_int i_errp	: 2;
  u_int i_err	: 5;
  u_int n_err	: 8;
} FLAG;

typedef union {
  struct { /* for write_shell() (parent process) */
    int *crp;
    int *colp, *rowp;
    int *col0p, *colep;
    int *colbp;
    int *lenp;
    u_char *dir, *version;
  } w;
  struct { /* for read_shell() (child process) */
    int *row0p, *lenp, *ip;
    RV *rvp;
    PN *pnp;	
    PP **p0p;
    u_char *buff, *prev;
    u_char *name, *prompt, *arg;
  } r;
} SHELL;

static void write_shell(void *p)
{
  static int row, col, col0, colb, cole, len, f_cr, *pv;
  static char dir[N_line], version[80];
  SHELL *sp;
  u_char *cp, *cp2;
  int f_disp;

  switch(Shell.f.init) {
  case 3:
    pv = NULL;
    return;
  case 2:
    sp = p;
    sp->w.crp = &f_cr;
    sp->w.colp = &col;
    sp->w.rowp = &row;
    sp->w.col0p = &col0;
    sp->w.colbp = &colb;
    sp->w.colep = &cole;
    sp->w.lenp = &len;
    sp->w.dir = dir;
    sp->w.version = version;
    strcpy(version, vt_geta(A_md));
    strcat(version, getenv("TCB"));
    strcat(version, vt_geta(A_me));
    break;
  case 1:
    pv = p;
    break;
  case 0:
    if (!pv) return;
    read(pv[4], &len, sizeof(int));
    if (!len) /* <== (4) */
      f_cr = 1;
    else {
      read(pv[4], dir, len);
      if ((cp = strstr(dir, version))) {
	if ((cp2 = strstr(dir, ":~"))) {
	  memmove(&dir[strlen(s_S(S_home))], &cp2[3], strlen(&cp2[3]) + 1);
	  memcpy(dir, s_S(S_home), strlen(s_S(S_home)));
	} else { /* cp points inside dir[]; &cp[...] and dir overlap when
		    cp != dir, so strcpy() here is undefined behavior (and
		    on macOS, _FORTIFY_SOURCE's __chk_fail_overlap() catches
		    it and hangs write_shell(), freezing tcb after a CP/MV/RM
		    completes and it tries to redraw the SHELL prompt) */
	  u_char *sp = &cp[strlen(version) + 1];
	  memmove(dir, sp, strlen(sp) + 1);
	}
	if (!strlen(dir)) ; /* garbage */
	else {
	  strcpy(&strchr(dir, '\0')[-2], "/");
	  chdir(dir); /* go to the directory specified by SHELL-prompt */
	} 
      }
      read(pv[4], &row, sizeof(int));
      read(pv[4], &col0, sizeof(int));
      read(pv[4], &f_disp, sizeof(int));
      col = cole = colb = col0; /* (7) */
      len = vt_col(0) - col0 - 1;
      if (!f_disp && row > 1 && load_intr() != S_errw) {
	if (!Shell.f.nobl)
	  echo_mess(S_errg); /* "Exit" */
	else echo_mess(-S_errg); /* without BL (0x07) */
      }
    }
    write(pv[7], &len, sizeof(int));
    break;
  default:
    break;
  }
}

static int input_shell(int spid, void *w, u_char prev[], char str[], FILE *ofp, FILE **pinp, LIST *prog, LIST **listp, STR **cmdp, STR **histp, int *ppidp, int f_shell)
{
  u_char *cp;
  int c, i, m, n, colb, f_list, ti, f_esc, st_size = 0;
  struct stat st;
  static PN pn;
  static int *pv, pos, ilen, shift, col_b;
  static u_char buff[N_pipe+1], pp_str[N_pipe+1];
  static u_char *mess, *wb, comm[N_pipe+1], *w2;
  static struct {
    u_int cmd	: 1;
    u_int skip	: 2;
    u_int w2	: 2;
  } ff;
  static SHELL s;

  if (!s.w.dir && f_shell >= 0) {
    Shell.f.init = 2;
    write_shell(&s);
    Shell.f.init = 0;
  }
  i = m = 0;
  switch(f_shell) {
  case -1:
    if (wb) free(wb);
    if (w2) {
      free(w2);
      w2 = NULL;
    }
    return 0;
  case 0:
    pv = (int*)w;
    mess = prev;
    pos = M_tail;
    Shell.f.init = 1;
    write_shell(pv);
    Shell.f.init = 0;
    return 0;
  case 1:
  case 2:
  case 3:
    Shell.f.init = 0;
    if (f_shell == 2) {
      m = T_tag2;
      vt_mode(-'d');
    } else m = T_tag;
    i = vt_mode('W'); /* 'W' == 1 : resize window */
    sprintf(comm, "%d\n%d\n%s\n%d\n%d\n%d\n"
	    , vt_row(0), vt_col(0), prev, vt_charset(0), vt_mode('H'), i);
    write_comm(pv[1], m, comm, spid);
    i = wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
    if (vt_mode('W')) {
      vt_mode(-'w');
      if (col_b != vt_col(0)) {
	if (col_b > vt_col(0)) col_b = vt_col(0); 
	if (i >= col_b / 4 * 3 + Shell.f.alen)
	  read_m(NULL, 2, 0, "\r"
		 , NULL, NULL, NULL, NULL, NULL, NULL); /* (5) */
      }
    }
    print_intr(NULL, NULL); /* S_errm etc. */
  case 4:
    if (*str) {
      do {
	if (ff.cmd || f_shell >= 3) 
	  ff.cmd = ilen = 0;
	strcpy(&buff[ilen], str);
	*s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	m = -K_e;
      } while (0);
    } else {
      m = 0;
      if (w) {
	if (shift) shift = 0;
	*s.w.colp = *s.w.colep = *s.w.col0p;
	ilen = 0;
	*buff = '\0';
      }
      if (ff.cmd) ff.cmd = 0;
    }
    chdir(s.w.dir);
    if (i || f_shell == 4) {
      vt_move(*s.w.rowp, *s.w.colp);
      if (f_shell == 4) vt_eeol();
    }
    if (!access(mess, F_OK)) unlink(mess);
    break;
  default:
    m = 0;
    break;
  }
  for (f_list = f_esc = 0; ; m = (m > 0) ? 0 : m, f_list = f_list == 1) {
    if (w) {
      if (pos != M_tail) {
	write_comm(pv[1], M_tail, NULL, spid);
	wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      }
      if ((!strncmp(w, "cat", 3) || !strncmp(w, "echo", 4))
	  && strstr(w, s_S(S_w))) { /* ^Jw / ^J^W */
	ff.skip = 1;
	strcpy(&buff[ilen], w);
	*s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	m = -K_e;
	if (wb) free(wb);
	wb = strdup(w);
      } else {
	if (!strncmp(w, s_S(S_w), strlen(s_S(S_w)))) {
	  /* CP / MV / RM / ARC */
	  cp = &strchr(w, '\r')[1];
	  Shell.f.nobl = 1;
	  w2 = strdup(cp);
	  *cp = '\0';
	  ff.w2 = 0;
	  st_size = 0;
	  while (1) { /* (11) */
	    /* prompt for `rm -i' etc. doesn't work until (12) done. */
	    write_comm(pv[1], T_in, NULL, spid);
	    if (!wait_comm(pv[2], NULL, NULL, 0, buff, pp_str)) break;
	    usleep(10000);
	  }
	  write_comm(pv[1], T_out, NULL, spid);
	  wait_comm(pv[2], NULL, NULL, 0, NULL, NULL);
	}
	fwrite(w, 1, strlen(w), ofp);
	fflush(ofp);
	if (w2) 
	  while (!access(s_S(S_w), F_OK)) /* (9) */
	    usleep(10000); 
	if (!strncmp(w, "cd", 2)) *histp = save_history(*histp, w, 1);
      }
      w = NULL;
    } else if (!access(mess, F_OK)) {
      vt_mode(-'d');
      write_comm(pv[1], T_off, NULL, spid);
      wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
      if ((m = -from_main(prev, str, mess, pinp, ppidp))) break;
    }
    if (kill(spid, 0)) {
      pv = NULL;
      m = -M_bl;
      break;
    }
    colb = *s.w.col0p;
    n = 0;
    if (!m) {
      write_comm(pv[1], T_in, NULL, spid);
      if (wait_comm(pv[2], NULL, NULL, 0, buff, pp_str)) {
	/* Interaction */
	do {
	  if (vt_mode('W')) {
	    sprintf(comm, "%d\n%d\n%s\n%d\n%d\n%d\n"
		    , vt_row(0), vt_col(0), prev, vt_charset(0)
		    , vt_mode('H'), 1);
	    write_comm(pv[1], T_tag, comm, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	    vt_mode(-'w');
	    c = 0;
	  } else if ((c = vt_getch()) == K_ec)
	    f_esc = (c = read_esc(T_shell, NULL, NULL)) < 0;
	  if (c > N_char)
	    switch(c - N_char) {
	    case M_kup:
	    case M_kdn:
	    case M_knext:
	    case M_kprev:
	    case M_kback:
	    case M_kforw:
	      n = c - N_char;
	      continue;
	    default:
	      break;
	    }
	  if (!c) {
	    if (w2 && f_shell == 2) {
	      do {
		stat(s_S(S_log), &st);
		if (st.st_size == st_size) {
		  if (++ff.w2 == 3) {
		    FILE *err = fopen(s_S(S_log), "r");
		    fseek(err, -1, 2);
		    for (i = 0; i < 80; i++) {
		      if ((c = fgetc(err)) == '\r' || c == '?') break;
		      fseek(err, -2, 1);
		    }
		    fclose(err);
		    if (c == '?') {
		      vt_mode('d');
		      sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
		      write_comm(pv[1], T_disp, comm, spid);
		      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
				, &buff[shift], pp_str);
		      f_shell = 1;
		      continue;
		    }
		  } else continue;
		}
		ff.w2 = 0;
		st_size = st.st_size; 
	      } while (0);
	    }
	    continue;
	  }
	  if (w2 && f_shell != 5) {
	    if (f_shell == 2) {
	      vt_mode('d');
	      sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
	      write_comm(pv[1], T_disp, comm, spid);
	      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
			, &buff[shift], pp_str);
	      f_shell = 1;
	      continue;
	    }
	    f_shell = 5;
	    fputc(K_u, ofp); /* (10) */
	    fflush(ofp);
	  }
	  switch(c) {
	  case 0:
	    continue;
	  case K_v: /* ^V */
	    n = M_kforw;
	    continue;
	  case 'v':
	    if (f_esc) {
	      f_esc = 0;
	      n = M_kback;
	      continue;
	    }
	    break;
	  case K_h: /* ^H */
	    c = K_dl;
	  case K_dl: /* Back space */
	  case K_c: /* ^C */
	  case K_z: /* ^Z */
	  case K_d: /* ^D */
	  case K_m: /* ^M */
	    break;
	  case K_g: /* ^G */
	    write_comm(pv[1], T_off, NULL, spid);
	    wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
	    m = -M_bl;
	    goto jump;
	  case K_l:  /* ^L */
	    sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
	    write_comm(pv[1], T_disp, comm, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	    continue;
	    break;
	  default:
	    if (c < ' ') continue;
	    break;
	  }
	  fputc(c, ofp);
	  fflush(ofp);
	} while (0);
	if (*s.w.crp) {
	  *s.w.crp = 0;
	  fputc(K_c, ofp);
	  fflush(ofp);
	}
	if (!n) continue;
      } else if (f_esc) f_esc = 0;
    }
    if (w2) {
      fprintf(ofp, "%s\r", w2);
      fflush(ofp);
      free(w2);
      w2 = NULL;
      ff.w2 = 0;
    }
    if (!n) {
      col_b = *s.w.col0p;
      m = edit_line(m, M_shell, buff, *s.w.lenp, *s.w.rowp, *s.w.col0p
		    , s.w.colp, s.w.colep, &shift, NULL);
      if (col_b != *s.w.col0p) {
	/* set `*s.w.col0p' at (5),(6),(7) */
	col_b = *s.w.col0p - col_b;
	*s.w.colep += col_b;
	*s.w.colp += col_b;
      }
    } else m = n;
    switch(m) {
    case 0:
      if (*s.w.crp) {
	*s.w.crp = 0;
	fputc(K_c, ofp);
	fflush(ofp);
      }
      break;
    case M_clear:
      sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
      write_comm(pv[1], T_disp, comm, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
    case N_char:
    case M_egrep:
      break;
    case M_hist:
    case M_cmd:
      ff.cmd = 1;
    case M_list:
      if (*listp) *listp = free_list(*listp);
      if (m == M_list) {
	if ((ti = *s.w.colp + shift - *s.w.col0p)) buff[ti] = '\0';
	do {
	  if (!(i = ichrstr(buff, 0, "/~")))
	    if (!(i = ichrstr(buff, 0, " ~")))
	      if (*buff != '~') continue;
	  strcpy(comm, getenv("HOME"));
	  strcat(comm, &buff[i+1]);
	  strcpy(&buff[i], comm);
	} while (0);
	i = make_list(s.w.dir, buff, &ilen, listp, M_shell);
      } else i = -m;
      switch(i) {
      case 0:
	break;
      case 1:
	*s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	m = -K_e;
	continue;
      default:
	if (i < 0) vt_mode(-'d');
	else if (!f_list) {
	  f_list = 1;
	  *s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	  vt_putchar(K_g);
	  m = -K_e;
	  continue;
	} else {
	  f_list = 0;
	  vt_mode(-'d');
	}
	write_comm(pv[1], T_off, NULL, spid);
	wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
	strcpy(str, &buff[ilen]);
	m = (i < 0) ? i : -M_list;
	goto jump;
      }
      break;
    case M_susp:
      fputc(K_z, ofp);
      fflush(ofp);
      break;
    case M_kanji:
    case M_mark:
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
      break;
    case M_kup:
    case M_up:
    case M_prev: 
    case M_kprev: 
    case M_kback: case M_head:
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      break;
    case M_kdn:
    case M_dn:
    case M_next: 
    case M_knext: 
    case M_kforw: case M_tail:
      if (pos == M_tail) vt_putchar(K_g);
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      break;
    case M_jump:
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      break;
    case T_w:
      if (!wb || !*wb) {
	vt_putchar(K_g);
	continue;
      }
      ff.skip = 1;
      if (pos != M_tail) {
	write_comm(pv[1], M_tail, NULL, spid);
	wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      }
      i = strlen(wb);
      vt_move(*s.w.rowp, *s.w.colp);
      fputc(K_u, ofp); /* (3) */
      fflush(ofp);
      strcpy(&buff[ilen], wb);
      *s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
      m = -K_e;
      break;
    default:
      if (m < N_char)
	switch(m) {
	case M_du:
	  fprintf(ofp, "%s -u\n", s_S(S_tcb));
	  fflush(ofp);
	  continue;
	case M_chdir2:
	  i = strlen(s.w.dir);
	default:
	  vt_mode(-'d');
	  write_comm(pv[1], T_off, NULL, spid);
	  wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
	  m = -m;
	  goto jump;
	  break;
	}
      else if (m >= N_num) {
	sprintf(comm, "%d\n", m - N_num - 1);
	write_comm(pv[1], N_num, comm, spid);
	wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      } else
	switch(m - N_char) {
	case K_q:
	case K_s:
	  fputc(m - N_char, ofp);
	  fflush(ofp);
	  break;
	case K_r:
	  if (pos != M_tail) {
	    write_comm(pv[1], M_tail, NULL, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	  }
	  m = -K_r;
	  break;
	case K_c:
	  vt_move(*s.w.rowp, *s.w.col0p);
	  vt_eeol();
	  fputc(K_c, ofp);
	  fflush(ofp);
	  shift = f_list = 0;
	  *s.w.colp = *s.w.colep = *s.w.col0p;
	  *buff = '\0';
	  if (ff.skip) ff.skip = 0;
	  break;
	case '\r':
	  write_comm(pv[1], T_out, NULL, spid);
	  wait_comm(pv[2], NULL, NULL, 0, NULL, NULL);
	  if (pos != M_tail) {
	    if (strlen(pp_str) > (i = strlen(s.w.version)) 
		&& (!strncmp(pp_str, s.w.version, i))) { /* past prompt */
	      if (pp_str[i+1] == '~') i -= strlen(s_S(S_home)) - 2;
	      strcpy(buff, &(strchr(&pp_str[i+strlen(s.w.dir)], ' '))[1]);
	      write_comm(pv[1], M_tail, NULL, spid);
	      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, NULL, pp_str);
	      if ((cp = strchr(buff, '\r'))) *cp = '\0';
	      *s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	    } else {
	      write_comm(pv[1], M_tail, NULL, spid);
	      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
			, &buff[shift], pp_str);
	    }
	  }
	  if (*buff) {
	    if (Shell.f.nobl) Shell.f.nobl = 0;
	    vt_move(*s.w.rowp, 0);
	    vt_eeol();
	    for (cp = buff; *cp == ' ' || *cp == '\t'; cp++) ;
	    if (*cp) {
	      strcpy(comm, cp);
	      while (comm[(i = strlen(comm) - 1)] == ' ') comm[i] = '\0';
	      if ((cp = strchr(comm, '|'))) {
		char *p;
		while (1) {
		  p = &cp[1];
		  strcpy(buff, &cp[1]);
		  for (cp = buff; *cp == ' ' || *cp == '\t'; cp++) ;
		  strcpy(buff, cp);
		  if ((cp = strchr(buff, ' '))) *cp = '\0';
		  if (!strcmp(buff, s_S(S_tcb))) i = -1;
		  else i = find_prog(prog, buff);
		  if (i) break;
		  if (!(cp = strchr(p, '|'))) break;
		}
	      } else if (strchr(comm, '<')) {
		strcpy(buff, comm);
		*strchr(buff, '<') = '\0';
		if ((cp = strchr(buff, ' '))) *cp = '\0';
		if (!strcmp(buff, s_S(S_tcb))) i = -1;
		else i = find_prog(prog, buff);
	      } else {
		strcpy(buff, comm);
		if ((cp = strchr(buff, ' '))) *cp = '\0';
		if (!strcmp(buff, s_S(S_tcb))) i = -1;
		else i = (find_prog(prog, buff)) ? 2 : 0;
	      }
	      if (i > 0) {
		do_prog(comm, cmdp, histp);
		strcpy(buff, comm);
		sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
		write_comm(pv[1], T_disp, comm, spid);
		wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
			  , &buff[shift], pp_str);
 		fputc(K_c, ofp);
	      } else {
		fwrite(comm, 1, strlen(comm), ofp);
		fputc('\r', ofp);
		if (!ff.skip) *cmdp = save_str(*cmdp, comm, &pn);
		if (ff.skip != 1) *histp = save_history(*histp, comm, 1);
	      }
	    } else fputc('\r', ofp);
	  } else fputc('\r', ofp);
	  fflush(ofp);
	  if (m >= 0) {
	    if (shift) shift = 0;
	    *s.w.colp = *s.w.colep = *s.w.col0p;
	    *buff = '\0';
	    f_list = 0;
	  }
	  if (ff.skip) ff.skip = 0;
	  break;
	default:
	  if (ff.skip == 1) ff.skip = 2;
	  if (pos != M_tail) {
	    write_comm(pv[1], M_tail, NULL, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	  }
	  m = -m;
	  break;
	}
      break;
    }
  }
jump:  
  vt_mode(-'d');
  col_b = vt_col(0);
  return m;
}

void write_comm(int pfd, int m, char *comm, int spid)/*tcb*/
{
  int len;

  kill(spid, SIGALRM);
  if (comm) {
    write(pfd, &m, sizeof(int));
    len = strlen(comm) + 1;
    write(pfd, &len, sizeof(int));
    write(pfd, comm, len);
  } else {
    m = -m;
    write(pfd, &m, sizeof(int));
  }
}

static int read_comm(int pfd, char comm[])
{
  int m, len;

  read(pfd, &m, sizeof(int));
  if (m > 0) {
    read(pfd, &len, sizeof(int));
    read(pfd, comm, len);
  } else {
    *comm = '\0';
    m = -m;
  }
  return m;
}

static PP *hide_ps(PP *ps)
{ /* save virtual line (prompt line) */
  while (ps->next) ps = ps->next;
  if (ps->prev) ps->prev->next = NULL;
  else return NULL;
  return ps;
}

static void disp_shell(RV *rvp, char name[], u_char prev[], char arg[], int row0, u_char prompt[], int len, u_char buff[])
{
  PP *ps = NULL;
  int i;

  if (!vt_mode('D')) vt_mode('d');
  print_line(rvp->ps, rvp->pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
  if ((i = strlen(prompt))) ps = hide_ps(rvp->ps);
  else {
    for (ps = rvp->ps; ps->next; ps = ps->next) ;
    ps->cs = buff;
    ps->f.len = strlen(ps->cs);
  }
  disp_window(rvp->ps, rvp->pp, T_shell, name, prev, arg, &len);
  if (i) {
    /* Command-line */
    if (ps) ps->prev->next = ps;
    if (!rvp->pe->next) { /* bottom page */
      vt_sc();
      vt_move(row0, 0); /* row0: cursor position (prompt line) */
      if (*rvp->str) {
	i = prompt[Shell.f.clen];
	prompt[Shell.f.clen] = '\0';
	vt_printf("%s...%s", prompt, rvp->str);
	prompt[Shell.f.clen] = i;
      } else vt_printf("%s", prompt);
      vt_eeol();
      vt_flush();
      vt_rc();
    }
    vt_move(rvp->row, rvp->col);
  } else {
    /* Interaction */
    for (ps = rvp->ps; ps->next; ps = ps->next) ;
    ps->cs = F3;
  }
}

static u_char *read_string(u_char *cp, u_char str[])
{
  int i = 0;

  for (i = 0; (str[i] = *cp++) != '\n'; i++) ;
  str[i] = '\0';
  return cp;
}

static void shell_row(RV *rvp, PN *pnp, int f_M, u_char buff[])
{
  PP *ps;

  ps = (!rvp->pp->next) ? hide_ps(rvp->ps) : NULL; /* save virtual line */
  move_row(f_M, rvp, pnp, NULL, M_shell);
  disp_window(rvp->ps, rvp->pp, M_shell, NULL, NULL, NULL, NULL);
  if (ps) ps->prev->next = ps; /* load virtual line */
}

static int shell_page(RV *rvp, PN *pnp, int f_M, u_char prompt[], u_char buff[])
{
  PP *ps;
  int i = 0;

  if (f_M == M_kback || f_M == M_head) ps = NULL;
  else if ((i = strlen(prompt))) 
    ps = hide_ps(rvp->ps); /* save SHELL-prompt line */
  else {
    for (ps = rvp->ps; ps->next; ps = ps->next) ;
    ps->cs = buff;
    ps->f.len = strlen(ps->cs);
  }
  move_page(f_M, rvp, pnp, NULL, M_shell);
  if (i) {
    ps->prev->next = ps; /* load SHELL-prompt line */
    if (rvp->pp->next) { /* not end line */
      if (rvp->pe->next && rvp->pe->next->cs != F3) 
	; /* not end page */
      else /* stopped at the end line after saving */
	shell_row(rvp, pnp, M_dn, buff);
    }
  } else if (ps) {
    ps->cs = F3;
    i = -1;
  }
  disp_window(rvp->ps, rvp->pp, M_shell, NULL, NULL, NULL, NULL);
  return i;
}

static int next_line(RV *rvp, PN *pnp, int c, int sf)
{
  PP *pp;
  if (!(pp = alloc_sh(F3, 1, c))) return rvp->row;
  if (rvp->row < vt_row(0) - 2) {
    rvp->pp = rvp->pp->next;
    rvp->pe = rvp->pe->next;
    pnp->nrow++;
    pnp->row++;
  } else {
    rvp->pp = rvp->pp->next;
    rvp->pe = rvp->pe->next;
    rvp->ps = rvp->ps->next;
    rvp->row--;
    pnp->nrow++;
    pnp->top++;
    if (sf && vt_mode('D')) {
      if (vt_mode('H')) vt_move(vt_row(0) - 1, 0);
      else vt_move(vt_row(0) - 2, 0);
      vt_sf();
      if (vt_mode('H')) vt_move(vt_row(0) - 2, 0);
      vt_eeol();
    }
  }
  rvp->row++;
  if (vt_mode('D'))
    disp_window(rvp->ps, rvp->pp, M_shell, NULL, NULL, NULL, NULL);
  if (rvp->row > vt_row(0) - 2) rvp->row = vt_row(0) - 2;
  if (pp == F4) echo_mess(S_err4);
  vt_move(rvp->row, 0);
  return rvp->row;
}

static int read_line(int c, u_char buff[], char prompt[], RV *rvp, int *row0p, PN *pnp, FLAG *fp)
{
  static int plen;
  static int n_buff, n_col;
  static struct {
    u_int nl	: 1;
    u_int bs	: 1;
    u_int esc	: 1;
    u_int md	: 2;
    u_int us	: 2;
  } ff;
  int i;

  if (rvp == F7) {
    ff.nl = ff.bs = ff.esc = ff.md = ff.us = 0;
    plen = n_buff = n_col = 0;
    return 0;
  }
  plen = strlen(prompt);
  if (!plen && n_col == vt_col(0) && (c == '\r' || c == '\n')) {
    *buff = '\0';
    return '\r'; /* new line was already added at (8), so skip next_line() */
  }
  if (n_buff >= N_pipe || c == '\n' || (n_col == vt_col(0) && c == '\r')) { 
    int j;
    u_char *p, *p0, *cp;
    ff.nl = 0;
    p0 = p = NULL;
    if (plen && strlen(buff) >= vt_col(0) + Shell.f.alen 
	&& strlen(buff) > vt_col(0) + Shell.f.alen) {
      u_char *p2;
      p0 = p = calloc(strlen(buff), 2);
      strcpy(p, buff);
      p2 = alloca(strlen(p) + 1);
      j = Shell.f.alen;
      do {
	strcpy(p2, p);
	cp = &p[vt_col(0) + j];
	*cp = '\0';
	/* set string of prompt to virtual line */
	rvp->pp->cs = strdup(p);
	rvp->pp->f.str = 1;
	rvp->pp->f.len = strlen(rvp->pp->cs);
	next_line(rvp, pnp, '\\', 0);
	/* '\\' : line continuation ==> alloc.c (2) */
	if (!fp->bash && cp[-1] == '\r') ff.nl = 1;
	strcpy(p, p2);
	strcpy(p, &p[vt_col(0) + j]);
        if (!*p) break;
	if (*p == '\r') ff.nl = 1;
	j = 0;
      } while (strlen(p) >= vt_col(0));
    }
    if (c != '\r') c = '\n';
    if (plen) {
      if ((cp = strchr(buff, '\r'))) *cp = '\0';
      vt_move(*row0p, 0);
      cp = buff;
      j = Shell.f.alen;
      do {
	for (i = 0; *cp && i < vt_col(0) + j; i++, cp++) {
	  if (is_euckana_1_byte(*cp)) i--;
	  vt_putchar(*cp);
	}
	vt_flush();
	if (i == vt_col(0) + j) {
	  if ((i = *row0p < vt_row(0) - 2)) (*row0p)++;
	  else {
	    if (vt_mode('H')) vt_move(vt_row(0) - 1, 0);
	    vt_sf();
	  }
	  vt_move(*row0p, 0);
	  j = 0;
	}
	vt_eeol();
      } while (*cp);
      if (p) strcpy(buff, p);
    }
    if (*buff) {
      if ((cp = strchr(buff, '\r'))) *cp = '\0';
      if (*buff) {
	rvp->pp->cs = strdup(buff); 
	rvp->pp->f.str = 1;
	rvp->pp->f.len = strlen(rvp->pp->cs);
	rvp->pp->f.line = rvp->pp->prev->f.line + rvp->pp->prev->f.nl;
	rvp->pp->f.nl = 1;
      } else c = '\n';
    } else if (n_col == vt_col(0))
      c = '\r'; /* skip alloc_sh() at next_line() */
    if (plen) {
      if ((p = strchr(prompt, ' ')))
	p = strchr(&p[1], ' ');
    } else p = NULL;
    if (p) {
      /* SHELL-prompt */
      p[1] = '\0';
      if ((j = *row0p) < vt_row(0) - 2) {
	j = ++(*row0p);
	if (plen > vt_col(0) + Shell.f.alen) {
	  j++;
	  if (plen > vt_col(0) + Shell.f.alen + vt_col(0)) j++;
	}
      }
      vt_move(j, 0);
      vt_flush();
      *prompt = '\0';
    } else if (*row0p < vt_row(0) - 2) (*row0p)++;
    *rvp->str = *buff = '\0';
    ff.us = ff.md = ff.esc = n_buff = n_col = 0;
    if (ff.nl) vt_putchar('\n');
    if (p0) free(p0);
  } else {
    if (!plen && n_col >= vt_col(0)) 
      ff.us = ff.md = ff.esc = n_buff = 0;
    if (!n_buff) n_col = 0;
    buff[n_buff++] = c;
    buff[n_buff] = '\0';
    do {
      if (n_col < vt_col(0) && !ff.us && c == '_') {
	if (++n_col == vt_col(0)) continue;
	else {
	  ff.us = 1;
	  vt_putchar('_');
	  vt_flush();
	}
      } else if (ff.us) {
	if (ff.us == 1)
	  if (c == K_h) {
	    vt_putchar(c);
	    vt_flush();
	    ff.us = 0;
	    if ((n_buff -= 2) < 0) n_buff = 0;
	    n_col--;
	    buff[n_buff] = '\0';
	  } else {
	    ff.us = 0;
	    n_col++;
	    continue;
	  }
	else if (ff.us == 2)
	  if (c == K_h) ff.us = 3;
	  else {
	    ff.us = 0;
	    n_col += 3;
	    vt_putchar('_');
	    vt_putchar('_');
	    vt_putchar(c);
	  }
	else if (ff.us == 3) {
	  if (c == K_h) {
	    ff.us = 0;
	    if ((n_buff -= 4) < 0) n_buff = 0;
	    buff[n_buff] = '\0';
	  } else {
	    ff.us = 0;
	    n_col += 4;
	    vt_putchar('_');
	    vt_putchar('_');
	    vt_putchar(K_h);
	    vt_putchar(c);
	  }
	}	  
      } else if (c == K_h)
	if (ff.bs) { /* ^H ^H */
	  ff.bs = 0;
	  vt_left();
	  vt_putchar(' ');
	  vt_flush();
	  vt_left();
	  if ((n_buff -= 3) < 0) n_buff = 0;
	  buff[n_buff] = '\0';
	  ff.md = 0;
	} else ff.md++; /* bold */
      else if (ff.md) {
	if (ff.md == 1)
	  if (c == ' ') ff.bs = 1;
	  else {
	    ff.md = 0;
	    if ((n_buff -= 2) < 0) n_buff = 0;
	    buff[n_buff] = '\0';
	  }
	else if (ff.md == 2) ff.md = 3;
	else if (ff.md == 3) {
	  ff.md = 0;
	  if ((n_buff -= 4) < 0) n_buff = 0;
	  buff[n_buff] = '\0';
	}
      } else if (c == K_ec && !*prompt) {
	ff.esc = 1; /* escape sequence */
	if (!plen) buff[--n_buff] = '\0';
      } else if (ff.esc) {
	ff.esc = (c < 'A' || c > 'Z') && (c < 'a' || c > 'z');
	if (!plen) buff[--n_buff] = '\0';
      } else {
	if (!is_euckana_1_byte(c)) n_col++;
	continue;
      }
      if (!plen) return c;
    } while(0);
    if (c == '\t' && (i = n_col % 8)) n_col += 8 - i;
    if (!plen && n_col >= vt_col(0)) {
      if (*row0p < vt_row(0) - 2) (*row0p)++;
      vt_putchar(c);
      rvp->pp->cs = strdup(buff);
      rvp->pp->f.str = 1;
      rvp->pp->f.len = strlen(rvp->pp->cs);
      rvp->pp->f.line = rvp->pp->prev->f.line + rvp->pp->prev->f.nl;
      rvp->pp->f.nl = 0;
      n_buff = ff.us = ff.md = 0;
      /* (8) */
      c = '\n'; /* add new line at next_line() */
    } else
      do {
	if (pnp->line == 1) return c;
	if (plen) {
	  if (plen >= vt_col(0) / 4 * 3 + Shell.f.alen) {
	    vt_move(*row0p, Shell.f.blen);
	    i = Shell.f.blen + plen - vt_col(0) + vt_col(0) / 4 + 4;
	    if (count_k(prompt, i)) sprintf(rvp->str, " %s", &prompt[i + 1]);
	    else strcpy(rvp->str, &prompt[i]);
	    vt_printf("...%s", rvp->str);
	    vt_flush();
	  } else {
	    if (*rvp->str) *rvp->str = '\0';
	    vt_move(*row0p, 0);
	    vt_printf("%s", prompt);
	  }
	  vt_flush();
	} else vt_putchar(c);
      } while (0);
  }
  return c;
}

void print_prompt(RV *rvp, int row0, char prompt[], char buff[])
{
  int i;

  if (vt_mode('D')) {
    print_line(rvp->ps, rvp->pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
    vt_move(rvp->row, rvp->col);
    if (!strlen(prompt)) {
      if (!rvp->pe->next && rvp->row == row0) {
	vt_move(row0, 0);
	vt_printf("%s", buff);
	vt_flush();
      }
      return;
    }
    if (rvp->pe && !rvp->pe->next) {
      vt_sc();
      vt_move(row0, 0);
      if (*rvp->str) {
	i = prompt[Shell.f.clen];
	prompt[Shell.f.clen] = '\0';
	vt_printf("%s...%s", prompt, rvp->str);
	prompt[Shell.f.clen] = i;
      } else vt_printf("%s", prompt);
      vt_eeol();
      vt_flush();
      vt_rc();
    } 
    vt_flush();
  }
}
  
static int set_pos(PP *pp, int *posp)
{
  int m;

  if (!pp->prev)
    if (*posp != M_head) m = *posp = M_head;
    else m = M_cmd;
  else if (*posp == M_head) m = *posp = (!pp->next) ? M_tail: T_not;
  else if (!pp->next)
    if (*posp != M_tail) m = *posp = M_tail;
    else m = M_cmd;
  else if (*posp == M_tail) m = *posp = (!pp->prev) ? M_head : T_not;
  else m = M_cmd;
  return m;
}

static void output_shell(FILE **, FILE **, char[], int[], int, int);
static void shell_hup(int dummy)
{
  signal(SIGALRM, SIG_IGN);
  output_shell(NULL, NULL, NULL, NULL, 0, -1);
}

static void shell_pipe(int dummy)
{
  signal(SIGPIPE, shell_pipe);
}

static BASE *print_shell(BASE *bp, RV *rvp, PN *pnp, PP *p0, int *rowp, int pid)
{
  char str[256];
  int i;
  PP *pp;
  BASE *bp0;

  bp->pp = p0;
  pnp->nrow = alloc_winch(bp, 1);
  bp0 = calloc(sizeof(BASE), 1);
  for (i = 0, pp = bp->pp; pp->next; i++, pp = pp->next) {
    if (pp->cs == F3) break;
    memcpy(str, pp->cs, pp->f.len);
    str[pp->f.len] = '\0';
    bp0->pp = alloc_sh(str, (!i) ? -pid : 1, (pp->f.nl) ? '\n' : 0);
  }
  free_bp(bp);
  free(bp);
  bp = bp0;
  alloc_sh(F3, 1, '\n');
  for (pp = bp->pp; pp->prev; pp = pp->prev) ;
  pnp->top = pnp->nrow - (vt_row(0) - 1);
  rvp->pp = bp->pp;
  init_row(&rvp->pp, pnp, 0, ((i = pnp->nrow - (vt_row(0) - 1)) > 0) ? i : 0);
  rvp->ps = rvp->pp;
  for (rvp->row = 0, pp = rvp->pp; pp->cs != F3; pp = pp->next, rvp->row++) ;
  rvp->pe = rvp->pp = pp;
  *rowp = pnp->row = rvp->row; 
  return bp;
}

static void read_shell(void *p)
{
  static char comm[N_pipe+1], str[N_pipe+1];
  static char *name, *prompt, *arg;
  static u_char *buff, *prev;
  static int *pv, *row0p, *lenp, *ip;
  static RV *rvp;
  static PN *pnp;
  static int pos;
  static PP **p0p;
  static int row_mark, tell;
  static int i, m;
  static char *cp;
  static SHELL *sp;
  static PP *pp;
  
  switch(Shell.f.init) {
  case 2:
    sp = p;
    name = sp->r.name;
    prompt = sp->r.prompt;
    arg = sp->r.arg;
    buff = sp->r.buff;
    prev = sp->r.prev;
    rvp = sp->r.rvp;
    pnp = sp->r.pnp;
    p0p = sp->r.p0p;
    row0p = sp->r.row0p;
    lenp = sp->r.lenp;
    ip = sp->r.ip;
    break;
  case 1:
    pv = p;
    pos = M_tail;
    break;
  case 0:
    m = read_comm(pv[0], comm);
    switch(m) {
    case T_in:
      write(pv[3], &m, sizeof(int));
      write(pv[3], ip, sizeof(int));
      return;
    case T_out:
      write(pv[3], &m, sizeof(int));
      *ip = 1;
      return;
    case T_off:
      vt_mode(-'d');
      m = M_cmd;
      break;
    case T_disp:
      cp = read_string(comm, str);
      i = atoi(str);
      read_string(cp, str);
      vt_setwin(i, atoi(str));
      disp_shell(rvp, name, prev, arg, *row0p, prompt, *lenp, buff);
      m = M_cmd;
      break;
    case M_mark:
      row_mark = rvp->pp->f.line;
      print_line(F2, NULL, 0, NULL);
      m = M_cmd;
      break;
    case M_jump:
      tell = row_mark - 1;
      row_mark = rvp->pp->f.line;
    case N_num:
      if (m == N_num) {
	read_string(comm, str);
	tell = atoi(str);
      }
      if (tell > 1) {
	for (pp = *p0p, i = 1; pp->next->next; pp = pp->next, i++)
	  if (pp->f.line == tell) break;
	tell = i;
      }
      if (pnp->nrow > vt_row(0)) {
	tell++;
	rvp->pp = *p0p;
	rvp->row = init_row(&rvp->pp, pnp, tell
			    , ((i = pnp->nrow - (vt_row(0) - 1)) > 0) ? i : 0);
	if ((rvp->pe = rvp->ps = rvp->pp))
	  for (i = 0; i < vt_row(0) - 2 && rvp->pe->next; i++)
	    rvp->pe = rvp->pe->next;
	for (i = 0; i < pnp->row && rvp->pp->next; i++) 
	  rvp->pp = rvp->pp->next;
	while (pnp->row > i++) rvp->row--;
      } else {
	shell_page(rvp, pnp, M_head, prompt, buff);
	for (i = tell; i; i--)
	  shell_row(rvp, pnp, M_dn, buff);
      }
      disp_shell(rvp, name, prev, arg, *row0p, prompt, *lenp, buff);
      m = set_pos(rvp->pp, &pos);
      break;
    case T_tag:
    case T_tag2:
      cp = read_string(comm, str);
      i = atoi(str);
      cp = read_string(cp, str);
      vt_setwin(i, atoi(str));
      cp = read_string(cp, prev);
      cp = read_string(cp, str);
      vt_charset(atoi(str));
      cp = read_string(cp, str);
      if (atoi(str)) {
	vt_mode('h');
	vt_cs(vt_row(0) - 1, 0);
      } else {
	vt_mode(-'h');
	vt_cs(vt_row(0) - 2, 0);
      }
      cp = read_string(cp, str);
      if (atoi(str)) output_shell(NULL, NULL, NULL, NULL, 0, 0);
    case M_clear:
      if (m != T_tag2) 
	disp_shell(rvp, name, prev, arg, *row0p, prompt, *lenp, buff);
      if (m == T_tag && rvp->pp->next) m = M_tail;
      else {
	m = M_cmd;
	break;
      }
    default:
      i = 0;
      do {
	switch(m) {
	case M_dn: case M_up: 
	case M_kdn: case M_kup: 
	case M_prev: case M_next: 
	case M_kprev: case M_knext: 
	  shell_row(rvp, pnp, m, buff);
	  break;
	case M_kback: case M_head:
	case M_kforw: case M_tail:
	  shell_page(rvp, pnp, m, prompt, buff);
	  break;
	default:
	  m = M_cmd;
	  continue;
	}
	m = set_pos(rvp->pp, &pos);
	print_prompt(rvp, *row0p, prompt, buff);
	break;
      } while (0);
      break;
    }
    i = 0;
    if (rvp->pp->cs != F3)
      for (pp = rvp->pp, *comm = '\0'; ; pp = pp->next) {
	if (pp->cs == F3) break;
	i += strlen(pp->cs) + 1;
	strcat(comm, pp->cs);
	if (pp->split) 	/* (1) <== alloc.c (2) */
	  ;
	else break;
      }
    write(pv[3], &m, sizeof(int));
    write(pv[3], &i, sizeof(int));
    if (i) write(pv[3], comm, i);
    m = strlen(prompt);
    write(pv[3], &m, sizeof(int));
    write(pv[3], row0p, sizeof(int));
    m = (m != T_off && rvp->pe && !rvp->pe->next) ? T_prompt : 0;
    write(pv[3], &m, sizeof(int));
    break;
  }
}
  
static int add_line(int row, int c, u_char buff[], char prompt[], RV *rvp, int *row0p, PN *pnp, int pv[], FLAG *fp)
{
  int i;

  if ((i = read_line(c, buff, prompt, rvp, row0p, pnp, fp)) == '\n' || i < 0) {
    row = next_line(rvp, pnp, (i < 0) ? 0 : '\n', 1);
    if (i < 0) vt_putchar (-i);
    fp->i_err = fp->i_errp = fp->n_err = 0;
    *prompt = pnp->line = 0;
  }
  return row;
}

static void output_shell(FILE **ifpp0, FILE **ofpp0, char prev0[], int pv0[], int pid0, int f_shell)
{
  static char prompt[N_pipe+1], name[N_line], arg[10], tcb[10];
  static PP *p0;
  static FILE *ifp, *ofp, **ifpp, **ofpp;
  static BASE *bp;
  static RV rv;
  static PN pn;
  static int sfd, pid, len, row0, *pv, inter;
  static struct sigaction alrm = {
    .sa_handler = (void(*)(int))read_shell,
    .sa_flags = SA_RESTART,
  };
  static char *errp[3], nn[4] = {0,};
  int c, i, j, c1, row, col, f_cr, f_bin;
  u_char prev[N_line], buff[N_pipe+1], str[N_pipe+1];
  char *p, *p2, err[30];
  SHELL s;
  CHILD *chp;
  FLAG f;

  switch(f_shell) {
  case 0:
    bp = print_shell(bp, &rv, &pn, p0, &row0, pid);
    p0 = bp->pp; /* s_S(S_lb2) */
    bp->pp = bp->pp->next;
    return;
  case 1:
  case 2:
    if ((j = (f_shell == 1) ? M_shell : T_shell2) == T_shell2) vt_mode(-'d');
    init_ALLOC();
    Shell.f.init = 1;
    read_shell(pv0);
    if (!pv) {
      Shell.f.init = 2;
      s.r.name = name;
      s.r.prompt = prompt;
      s.r.arg = arg;
      s.r.buff = buff;
      s.r.prev = prev;
      s.r.rvp = &rv;
      s.r.pnp = &pn;
      s.r.p0p = &p0;
      s.r.row0p = &row0;
      s.r.lenp = &len;
      s.r.ip = &inter;
      read_shell(&s);
    }
    Shell.f.init = 0;
    inter = 1; /* ==> (11) */
    strcpy(tcb, getenv("TCB"));
    errp[0] = s_S(S_mark);
    errp[1] = tcb;
    errp[2] = ":";
    ifpp = ifpp0;
    ofpp = ofpp0;
    ifp = *ifpp;
    ofp = *ofpp;
    chp = get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL);
    f.bash = !strcmp(chp->argv[0], "bash");
    signal(SIGCHLD, SIG_DFL);
    while (fgetc(ifp) == EOF) ; /* (12) */
    if (!fork()) {
      if (f.bash) {
	/* unset PROMPT_COMMAND: overriding PS1 alone isn't enough to get a
	   prompt tcb's line-by-line SHELL-mode parser can recognize. Many
	   desktop terminal emulators (e.g. GNOME Terminal/VTE, via
	   /etc/profile.d/vte.sh) set PROMPT_COMMAND to inject an OSC
	   escape sequence (reporting cwd as a file:// URI, percent-encoded)
	   before every prompt. tcb's parser doesn't know this escape
	   sequence and reads it as literal text, desyncing the line buffer
	   into a repeating garbage loop -- most visible when the cwd has
	   non-ASCII characters (e.g. a localized "Pictures" folder name),
	   which makes the injected escape sequence long enough to spill
	   across what tcb expects to be a single prompt line. */
	/* eos: sentinel marking the end of this setup line, matched below.
	   pid0 makes it unique to this run -- s_S(S_mark) alone isn't
	   enough since PS1 (built with it, just below) already contains it
	   twice, so a scan for S_mark stops at that earlier occurrence and
	   leaks everything after it (echoed PS1 tail, the real first
	   prompt, ...) into what tcb parses as the first SHELL-prompt line. */
	char eos[32];
	sprintf(eos, "#eos%d#", pid0);
	fprintf(ofp, "unset PROMPT_COMMAND; export PS1=\047%s%s:\\w\\$ %s\047; set -o ignoreeof; export COLUMNS=%d; unset HISTFILE; export PAGER=\047%s -p %d\047\n%s\n"
		, s_S(S_mark), tcb, s_S(S_mark), vt_col(0), s_S(S_tcb), pid0, eos);
      } else
	fprintf(ofp, "set prompt = \042%s%s:%%~%%# %s\042; set ignoreeof; setenv PAGER \047%s -p %d\047\n"
		, s_S(S_mark), tcb, s_S(S_mark), s_S(S_tcb), pid0);
      fflush(ofp);
      exit(0);
    }
#if 0
    while ((c = fgetc(ifp)) != '\r') ;
#else
    if (f.bash) {
      /* Discard everything up through our own "#eosPID#" sentinel (see
	 above) instead of a bare '#': between the fork() above and here,
	 bash may already have auto-drawn its very first prompt using the
	 ORIGINAL (pre-tcb) PROMPT_COMMAND -- e.g. GNOME Terminal/VTE's
	 /etc/profile.d/vte.sh, which reports cwd via an OSC escape sequence
	 -- before it ever reads the setup line sent above, and a bare '#'
	 can turn up by coincidence inside that leftover output and end
	 this loop too early. */
      char eos[32];
      int mi = 0, ml = sprintf(eos, "#eos%d#", pid0);
      while (mi < ml && (c = fgetc(ifp)) != EOF)
	mi = (c == eos[mi]) ? mi + 1 : 0;
    } else
      while ((c = fgetc(ifp)) != '#') ;
#endif
    wait(0);
    strcpy(prev, prev0);
    sfd = fileno(fopen(s_S(S_log), "a"));
    pv = pv0;
    pid = pid0;
    pn.row = pn.top =0;
    pn.nrow = 2;
    sprintf(str, "%s", s_S(S_lb2));
    bp = calloc(sizeof(BASE), 1);
    /* virtual line contains the string "SHELL  version ..." */
    p0 = bp->pp = alloc_sh(str, -pid, '\n'); 
    /* first SHELL-prompt */
    alloc_sh(F3, 1, '\n');
    *rv.str = '\0';
    rv.row = rv.col = 0;
    strcpy(name, s_P(P_mode)[M_shell]);
    sprintf(arg, "%c(%s)", K_z, chp->argv[0]);
    len = disp_window(bp->pp, bp->pp, j, name, prev, arg, &i);
    rv.ps = bp->pp;
    /* set current line to first SHELL-prompt */
    bp->pp = bp->pp->next;
    rv.pp = rv.pe = bp->pp;
    print_line(rv.ps, rv.pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
    vt_move(++rv.row, rv.col);
    row = row0 = rv.row;
    for (i = 1; i < NSIG; i++) 
      switch(i) {
      case SIGALRM:
	sigaction(i, &alrm, NULL);
	break;
      case SIGHUP:
      case SIGSEGV:
	signal(i, shell_hup);
	break;
      case SIGPIPE:
	signal(i, shell_pipe);
	break;
      default:
	signal(i, SIG_IGN);
	break;
      }
    *buff = *prompt = '\0';
    f_cr = f_bin = 0;
    read_line(0, NULL, NULL, F7, NULL, NULL, NULL);
    pn.line = -1; 
    for (c1 = f.i_err = f.i_errp = f.n_err = 0; ; ) {
      if (c1) {
	c = c1;
	c1 = 0;
      } else if ((c = fgetc(ifp)) == EOF) break;
      else if (c == K_g) {
#ifdef	CHECK_SFD
	write(sfd, &c, 1);
#endif
	c1 = 0;
	continue; /* <== (3) */
      }
      if (c == ' ' && *prompt && ((c1 = fgetc(ifp)) == '\r' || c1 == K_h)) { 
	/* " ^M" or " ^H" is the mark for length of winsize.ws_col. (tcsh) */
#ifdef	CHECK_SFD
	write(sfd, &c1, 1);
#endif
	c1 = 0;
	continue;
      }
      if (!*prompt) {
	if (!c && !f_bin) f_bin = 1;
        if (f_bin && (c >= 0x80 || (c < ' ' && c != '\n'))) c = ' ';
	if (!f_bin) {
	  switch(f.bs) {
	  case 0:
	    if (c == '\047') f.bs++;
	    break;
	  case 1:
	    if (c == '?') f.bs++;
	    else f.bs = 0;
	    break;
	  case 2:
	    if (c == ' ') f.bs++;
	    else f.bs = 0;
	    break;
	  case 3:
	    if (c == ' ' || c == K_h) continue; /* <== (10) */
	    else f.bs = 0;
	    break;
	  }
	}
      }
      if (vt_mode('D') && rv.pp->next) {
	/* go to the line of SHELL-prompt from current line */
	print_line(rv.ps, rv.pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
	if (shell_page(&rv, &pn, M_tail, prompt, buff) >= 0) {
	  vt_move(row0, 0);
	  if (*rv.str) {
	    i = prompt[Shell.f.clen];
	    prompt[Shell.f.clen] = '\0';
	    vt_printf("%s...%s", prompt, rv.str);
	    prompt[Shell.f.clen] = i;
	  } else vt_printf("%s", prompt);
	  vt_eeol();
	} else vt_move(rv.row, strlen(buff));
      }
      if (c != '\r') {
	if (!*prompt) {
	  /* detect the string PS1 environment variable */
	  if (c == errp[f.i_errp][f.i_err]) {
	    err[f.n_err + f.i_err++] = c;
	    err[f.n_err + f.i_err] = '\0';
	    if (c != ':') continue;
	    /* matched to PS1 */
	    sprintf(prompt, "%s%s%s%s"
		    , vt_geta(A_md), errp[1], vt_geta(A_me), errp[2]);
	    c = errp[0][0];
	    p = &prompt[strlen(prompt)];
	    for (c1 = 0; (j = fgetc(ifp)) != c; ) {
#ifdef	CHECK_SFD
	      write(sfd, &j, 1);
#endif
	      if (j == '\r') { 
		if (c1 == ' ') {
		  p--;
		  c1 = 0;
		  continue;
		} else *p++ = c1;
	      }
	      if (j == '\\') { /* code above 0x80 (tcsh) */
		fread(nn, 1, 3, ifp);
		j = (u_char)strtol(nn, NULL, 8);
	      }
	      c1 = j;
	      if (!j) j = ' ';
	      *p++ = j;
	    }
	    *p = '\0';
	    c1 = 0;
	    for (i = 1; (j = errp[0][i]) && (c = fgetc(ifp)) == j; i++) {
#ifdef	CHECK_SFD
	      write(sfd, &c, 1);
#endif
	      /* read current directory name */
	      ;
	    }
	    if (*buff) {
	      /* 
	       * force to start a new line when there is any string before 
	       * SHELL-prompt (`ftp Enter ^D' etc.), with the result that 
	       * `set Enter' whose output contains PS1 will make 
	       * a extra new line.
	       */
	      kill(getppid(), SIGALRM);
	      i = 0;
	      write(pv[5], &i, sizeof(int)); /* (4) */
	      read(pv[6], &i, sizeof(int));
	      f_cr = 1;
	    }
	    p = malloc(strlen(prompt) + 1);
	    p2 = malloc(strlen(prompt) * 2);
	    strcpy(p, prompt);
	    strcpy(p2, prompt);
#ifndef	CHECK_SFD
 	    write(sfd, p2, strlen(p2));
#endif
	    free(p2);
	    memset(prompt, 0, strlen(p));
	    row0 = rv.row;
	    for (i = 0; (c = p[i]); i++) {
	      prompt[i] = c;
	      read_line(c, buff, prompt, &rv, &row0, &pn, &f);
	      vt_eeol();
	    }
	    strcpy(prompt, p);
	    free(p);
	    pn.line = 1;
	    vt_flush();
	    if (f_bin) f_bin = 0;
	    if (f_cr) f_cr = 0;
	    else {
	      /*
	       * inform parent process of SHELL-prompt contains 
	       * current directory name.
	       */
	      kill(getppid(), SIGALRM);
	      i = strlen(prompt) + 1;
	      write(pv[5], &i, sizeof(int));
	      write(pv[5], prompt, i);
	      write(pv[5], &row0, sizeof(int));
	      i = strlen(rv.str);
	      col = (i) ? Shell.f.blen + 3 + i : strlen(prompt) - Shell.f.alen;
	      write(pv[5], &col, sizeof(int)); /* (6) */
	      i = vt_mode('D');
	      write(pv[5], &i, sizeof(int));
	      read(pv[6], &i, sizeof(int));
	    }
	    f.i_errp = f.n_err = 0;
	    f.i_err = strlen(buff);
	    inter = 0;
	    continue;
	  } else if (f.i_err) {
	    if (!errp[f.i_errp][f.i_err]) {
	      /* next checking */
	      f.n_err += f.i_err;
	      f.i_err = 0;
	      f.i_errp++;
	      c1 = c;
	      continue;
	    } else {
	      /* not SHELL-prompt */
	      c1 = c; 
	      for (i = 0; (c = err[i]); i++)
		row = add_line(row, c, buff, prompt, &rv, &row0, &pn, pv, &f);
#ifndef	CHECK_SFD
 	      write(sfd, err, strlen(err));
#endif
	      f.i_err = f.i_errp = f.n_err = *prompt = 0;
	      continue;
	    }
	  }
	}
      }
      if (pn.line < 0) continue; /* garbage */
#ifndef CHECK_SFD
      if (c != '\r') write(sfd, &c, 1);
#endif
      row = add_line(row, c, buff, prompt, &rv, &row0, &pn, pv, &f);
    }
  case -1:
    bp->pp = p0;
    free_bp(bp);
    free(bp);
    close(sfd);
    fclose(ofp);
    fclose(ifp);
    *ifpp = *ofpp = NULL;
    if (!kill(pid, 0)) kill(pid, SIGHUP);
    exit_ALLOC(&s_P(P_mode)[M_shell][1]);
    exit(0);
    break;
  default:
    break;
  }
}

int from_main(char name[], char str[], char *mess, FILE **pinp, int *ppidp)/*tcb*/
{
  FILE *fp;
  int len, sfd;
  u_char m;
  struct stat st; 
  struct sockaddr_un cli_addr;
  static char sname[N_line];
  
  if (!mess) {
    if (*sname && !access(sname, F_OK)) unlink(sname);
    return 0;
  }
  stat(mess, &st);
  if (S_ISSOCK(st.st_mode)) {
    if ((sfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return 0;
    cli_addr.sun_family = AF_UNIX;
    strcpy(cli_addr.sun_path, mess);
    if (connect(sfd, (struct sockaddr *)&cli_addr, sizeof(cli_addr)) < 0)
      return 0;
    fp = fdopen(sfd, "r");
  } else fp = fopen(mess, "r");
  fread(&m, 1, 1, fp);
  switch(m) {
  case T_pipe2:
    fread(ppidp, 1, 4, fp);
    if (*pinp && *pinp != F1) /* ^O^M or MAN */
      alloc_pipe(-1, pinp, NULL, NULL, 0);
    *pinp = fp;
    strcpy(sname, mess);
    break;
  default:
    fread(&len, 1, 4, fp);
    fread(str, 1, len, fp);
    if (m != M_du) {
      fread(&len, 1, 4, fp);
      fread(name, 1, len, fp);
    }
    fclose(fp);
    unlink(mess);
    break;
  }
  return m;
}

int wait_comm(int pfd, int *posp, int *rowp, int col0p, u_char *buff, u_char pp_str[])/*tcb*/
{
  int m, row, len, f_prompt;

  read(pfd, &m, sizeof(int));
  switch(m) {
  case T_in:
    if (!vt_mode('D')) vt_mode('d');
    read(pfd, &f_prompt, sizeof(int));
    return f_prompt;
    break;
  case T_out:
    return 0;
    break;
  default:
    if (m != M_cmd && m != T_off) *posp = m;
    break;
  }
  read(pfd, &len, sizeof(int));
  if (len) read(pfd, pp_str, len);
  read(pfd, &len, sizeof(int)); /* strlen(prompt) */
  read(pfd, &row, sizeof(int));
  read(pfd, &m, sizeof(int));
  if (rowp) *rowp = row;
  if (len && m == T_prompt && buff) {
    if (*posp != M_tail) vt_sc();
    vt_move(*rowp, col0p);
    print_buff(buff, 0);
    vt_flush();
    if (*posp != M_tail) vt_rc();
  }
  return len;
}

static int make_shell(FILE **ifpp, FILE **ofpp, int pv[], u_char prev[], int pid, char mess[], int f_comm)
{
  int i, spid;
  static struct sigaction alrm = {
    .sa_handler = (void(*)(int))write_shell,
    .sa_flags = SA_RESTART,
  };
  sigaction(SIGALRM, &alrm, NULL);
  for (i = 0; i < 8; i++) if (pv[i]) close(pv[i]);
  pipe(pv);
  pipe(&pv[2]);
  pipe(&pv[4]);
  pipe(&pv[6]);
  input_shell(0, pv, mess, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
  if (!(spid = fork())) {
    usleep(100000); /* following is preceded by pause() of parent process */
    output_shell(ifpp, ofpp, prev, pv, pid, (f_comm) ? 2 : 1);
  } else pause();
  start_shell(0, spid, NULL, NULL, NULL, NULL);
  return spid;
}

int start_shell(int shell_pid, int disp_pid, FILE **ofpp, FILE **ifpp, char *mess, char pty[])/*tcb*/
{
  static FILE *ifp, *ofp;
  static int pid, spid;
  static char *messp;
  int i, tfd, fd;
  
  if (shell_pid < 0) return pid;
  if (disp_pid) {
    spid = disp_pid;
    return 0;
  }
  if (mess) {
    messp = mess;
    if (!ofpp && !ifpp) tfd = 0;
    else if ((tfd = open_pty(pty, 1)) < 0) return 0;
    unlink(s_S(S_mess));
    if (!(pid = fork())) {
      char str[256];
      CHILD *chp = get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL);
      /* pty[] already holds the slave device path from open_pty() */
      fd = open(pty, O_RDWR);
      setsid();
#ifdef TIOCSCTTY
      ioctl(fd, TIOCSCTTY, 0);
#endif
      dup2(fd, 0);
      dup2(fd, 1);
      dup2(fd, 2);
      close(fd);
      strcpy(str, s_S(S_mess));
      strcat(str, ".tmp");
      ofp = fopen(str, "w");
      fprintf(ofp, "%d", (int)getpid());
      fclose(ofp);
      rename(str, s_S(S_mess));
      for (i = 0; i <= 31; i++) signal(i, SIG_DFL);
      if (!strcmp(chp->argv[0], "bash")) 
	/*
	 * bash inserts string "^H^[^A..." in order to redisplay 
	 * prompt whose length is equal to COLUMNS environment variable.
	 * Following setting avoids inserting above string.
	 * (the value will be reset in output_shell()) 
	 */
	setenv("COLUMNS", "1024", 1);
      execvp(*chp->argv, chp->argv);
      exit(0);
    }
    while (access(s_S(S_mess), F_OK) < 0) usleep(10000);
    ifp = fopen(s_S(S_mess), "r");
    fscanf(ifp, "%d", &pid);
    fclose(ifp);
    unlink(s_S(S_mess));
    strcpy(mess, s_S(S_mess));
    sprintf(&strstr(mess, "/s.")[3], "%d.%d", (int)getuid(), pid);
    if (!ofpp && !ifpp) return pid;
    *ofpp = ofp = fdopen(tfd, "w");
    *ifpp = ifp = fdopen(tfd, "r");
  } else {
    if (spid && !kill(spid, 0)) {
      kill(spid, SIGHUP);
      while (!kill(spid, 0)) usleep(10000);
    }
    if (ofp) {
      fclose(ofp);
      ofp = NULL;
    }
    if (ifp) {
      fclose(ifp);
      ifp = NULL;
    }
    if (!access(messp, F_OK)) unlink(messp);
    input_shell(0, NULL, NULL, NULL, NULL, NULL, 
		NULL, NULL, NULL, NULL, NULL, -1);
    from_main(NULL, NULL, NULL, NULL, NULL); /* unlink socket */
  }
  return pid;
}

int do_shell(u_char prev[], char str[], FILE **pinp, u_char *w, int *ppidp, LIST *prog, LIST **listp, STR **cmdp, STR **histp, int f_comm)/*tcb*/
{
  int m, f_shell, f_init;
  char dir[N_line];
  static int pid, spid, pv[8];
  static FILE *ifp, *ofp;
  static char mess[N_line], pty[256];

  if (!Shell.f.alen) {
    Shell.f.alen = strlen(vt_geta(A_md)) + strlen(vt_geta(A_me));
    Shell.f.blen = strlen(getenv("TCB")) + 1; 
    Shell.f.clen = Shell.f.alen + Shell.f.blen;
  }
  f_init = !(pid && !kill(pid, 0));
  if (!ofp || (spid && kill(spid, 0))) {
    if ((f_shell = (f_comm && f_comm < 3) ? f_comm : 4) != 4) vt_mode(-'d');
    else f_comm = 0;
    if (ofp) {
      fclose(ofp);
      fclose(ifp);
      ofp = NULL;
      ifp = NULL;
    }
    if (!(pid = start_shell(0, 0, &ofp, &ifp, mess, pty))) {
      save_error("%s: %s (%s).\n", s_S(S_tcb), s_S(S_err5), pty_strerror());
      save_intr(S_err5);
      return -M_bl;
    }
    if ((spid = make_shell(&ifp, &ofp, pv, prev, pid, mess, f_comm)) < 0) {
      kill(pid, SIGHUP);
      kill(getpid(), SIGHUP);
      return 0;
    }
    vt_cursor(spid);
    is_inter(pv, pid, spid);
  } else f_shell = (f_comm) ? f_comm : 1;
  getcwd(dir, N_line);
  print_intr(F8, NULL); /* erase "Exit" */
  m = input_shell(spid, w, prev, str, ofp, pinp, prog, 
		  listp, cmdp, histp, ppidp, f_shell);
  chdir(dir);
  return m;
}

int is_inter(int *pv0, int pid0, int spid0)/*tcb*/
{
  int f_inter = 0;
  static int pid, spid, i_inter, *pv;

  if (pv0 == F12) {
    if (spid && !kill(spid, 0)) {
      kill(pid, SIGHUP);
      kill(spid, SIGHUP);
      while (!kill(spid, 0) || !kill(pid, 0)) usleep(10000);
    }
  } else if (!pv0) {
    if (spid && !kill(spid, 0)) {
      write_comm(pv[1], T_in, NULL, spid);
      f_inter = wait_comm(pv[2], NULL, NULL, 0, NULL, NULL);
      if (pid0 < 0 && f_inter) return ++i_inter == 1;
      else if (i_inter) i_inter = 0;
    }
  } else {
    pv = pv0;
    pid = pid0;
    spid = spid0;
  }
  return f_inter;
}

