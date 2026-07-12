#include "tcb.h"
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>

static struct {
  u_char *S[S_nn];
} Str;

void r_S(int n_S)/*tcb*/
{
  if (n_S < S_nn && Str.S[n_S]) {
    free((char*)Str.S[n_S]);
    Str.S[n_S] = 0;
  }
}

char *s_S(int n_S)/*tcb*/
{
  return (n_S < S_nn) ? (char*)Str.S[n_S] : NULL;
}

char **s_P(uintptr_t n_P)/*tcb*/
{
  static char *child[] = { /* built-in program */
    /* file manager */
    NULL,		/* S_ls */
    NULL,		/* S_du */
    NULL,		/* S_cp */
    NULL,		/* S_mv */
    NULL,		/* S_rm */
    NULL,		/* S_groff */
    NULL,		/* S_man */
    "locate",		/* S_locate */	

    "file",		/* S_file */
    "egrep -n",		/* S_egreap */
    "fgrep -ne",	/* S_fgrep */
    "gzip -dc",		/* S_gzip */
    "bzip2 -d",		/* S_bzip2 */
    "tar",		/* S_tar */
    "lha p",		/* S_lha */
    "unzip -p",		/* S_unzip */
    "rpm",		/* S_rpm */
    "rpm2cpio",		/* S_rpm2cpio */
    "ar p",		/* S_deb */
    NULL,
  };
  static char *tcbrc[] = { /* setting name defined in tcbrc */
    "ARCHIVE", 
    "FILE", 
    "EXEC", 
    "EDITOR",
    "SHELL",
    "GREP",
    "PROGRAM",
    "BUFFER",
    "CODE", 
    "HISTORY", 
    "WAIT",
    "SCROLL",
    "LS",
    "KEY",
    "CP",
    "MV",
    "RM",
    NULL,
  };
  static char *code[] = {				
    "ASCII",						/* C_ascii */
    "BINARY",						/* C_binary */
    "Shift-JIS",					/* C_sjis */
    "EUC",						/* C_euc */
    "JIS",						/* C_jis */
    "\075?ISO-2022-JP?B?",				/* C_base64 */
    "\075?ISO-2022-JP?Q?",				/* C_quote */
    "\075?SHIFT_JIS?B?",				/* C_shift_jis */
    "\075?US-ASCII?Q?",					/* C_us_ascii */
    "\075?EUC-JP?B?",					/* C_euc_jp */
    NULL,
  };
  static char **P[] = {
    child,     						/* P_child */
    NULL,  						/* P_mode */
    tcbrc,     						/* P_tcbrc */
    code,						/* P_code */
    NULL,						/* P_arc */
  };
  static struct {
    char *s;
    int m;
  } mp [] = {
    {" TCB",	M_tcb},
    {" HELP",	M_help},
    {" KEY",	M_key},
    {" FGREP",	M_fgrep},
    {" EGREP",	M_egrep},
    {" CUT",	M_cut},
    {" DU",	M_du},
    {" LOCATE",	M_locate},
    {" PROGRAM",	M_prog},
    {" MAN",	M_man},
    {" FILE",	M_lfile},
    {" DIR",	M_ldir},
    {" HISTORY",	M_hist},
    {" COMMAND",	M_cmd},
    {" CP",	M_cp},
    {" MV",	M_mv},
    {" RM",	M_rm},
    {" ARC",	M_arc},
    {" SHELL",	M_shell},
    {" LIST",	M_list},
    {" EDIT",	M_edit},
    {" ERROR",	M_error},
    {" LOG",	M_log},
    {" COPY",	M_copy},
    {" EXIT",	M_exit},
    {" ",	T_pipe},
    {" (stdin)",	T_pipe2},

    /* for ~/.tcb/history */
    {" FILE_HISTORY",	T_hfile},
    {" DIR_HISTORY",	T_hdir},
    {" MAN_HISTORY",	T_hman},
    {" LOCATE_HISTORY",	T_hlocate},
    {" FGREP_HISTORY",	T_hfgrep},
    {" EGREP_HISTORY",	T_hegrep},
    {" MODE", T_mode},
    {NULL,	T_null},
  };
  static char *mode[T_null];
  int i, m;
  
  if (n_P > P_nn && !P[P_arc]) P[P_arc] = (char**)n_P;
  if (!*child) for (i = 0; i < 7; i++) child[i] = Str.S[S_option + i];
  if (!P[P_mode]) {
    for (i = 0; (m = mp[i].m) != T_null; i++) mode[m] = mp[i].s;
    P[P_mode] = mode;
  }
  return (n_P < P_nn) ? P[n_P] : NULL;
}

static void get_prog(char str[])
{ 
  int i;
  char *p;

  p = alloca(strlen(str) + 1);
  for (i = 0; str[i] && str[i] != ' '; i++) p[i] = str[i];
  p[i] = '\0';
  strcpy(str, p);
}

static void print_tcb(char *str, int f_tcb)
{
  char *cp, *p = str, tmp[256];
  
  strcpy(p, s_S(S_version));
  *p = 'V';
  sprintf(strchr(p, '\0'), "--\nStartup file : %s\n", toul(s_S(S_tcbrc), 0));
  if (f_tcb & 0x80)
    sprintf(strchr(p, '\0'), "History file : %s\n", toul(s_S(S_hist), 0));
  sprintf(strchr(p, '\0'), "Startup mode : %s\n"
	  , (f_tcb & (0x80 | 0x40)) 
	  ? tobo("file manager", 0) : tobo("pager", 0));
  sprintf(strchr(p, '\0'), "Command line : %s\n", s_S(S_option+7));
  strcat(p, "User name    : ");
  do {
    if ((cp = getenv("user"))) sprintf(strchr(p, '\0'), "%s", cp);
    else if ((cp = getenv("USER"))) sprintf(strchr(p, '\0'), "%s", cp);
    else {
      sprintf(strchr(p, '\0'), "---\n");
      continue;
    }
    if ((!(cp = getenv("hostname")) 
	&& !(cp = getenv("HOSTNAME"))
	&& !(cp = getenv("HOST")))
	|| !strlen(cp)) {
      int i, pv[2];
      pipe(pv);
      if (!fork()) {
	close(1);
	dup2(pv[1], 1);
	close(pv[1]);
	close(pv[0]);
	execlp("hostname", "hostname", NULL);
      }
      close(pv[1]);
      for (i = 0; read(pv[0], &tmp[i], 1) == 1 && i < 256; i++) ;
      if (i == 256) i--;
      tmp[i] = '\0';
      close(pv[0]);
      if ((cp = strchr(tmp, '\n'))) *cp = '\0';
      cp = (i) ? tmp : NULL;
    }
    if (cp) sprintf(strchr(p, '\0'), "@%s\n", cp);
    else strcat(p, "\n");
  } while (0);
  if (f_tcb & (0x80 | 0x40)) {
    sprintf(strchr(p, '\0')
	    , "%s log    : %s\n"
	    , tobo(&s_P(P_mode)[M_error][1], 0)
	    , toul(s_S(S_error), 0));
    sprintf(strchr(p, '\0')
	    , "%s log    : %s\n"
	    , tobo(&s_P(P_mode)[M_shell][1], 0)
	    , toul(s_S(S_log), 0));
  }
}

static char *get_cp(char str[])
{
  char *cp;

  if ((cp = strchr(str, '#'))
      || (cp = strchr(str, '\n'))) *cp = '\0';
  for (cp = str; *cp && (*cp == ' ' || *cp == '\t'); cp++) ;
  return (*cp) ? cp : NULL;
}

static char *get_val(FILE *fp, char str[], int *ip)
{
  int i = 0;
  char *p, *cp;

  do {
    if (!fgets(str, N_line, fp) || (i = strchr(s_S(S_nop), *str) == NULL)) {
      *ip = (i) ? 1 : -1;
      return NULL;
    }
  } while (!(cp = get_cp(str)) || *cp < '0' || *cp > '9');
  if ((p = strchr(cp, ' ')) || (p = strchr(cp, '\t'))) *p = '\0';
  *ip = 0;
  return cp;
}

static int conf_arc(FILE *fp, char *str, CHILD *chp)
{
  char *cp, *p, *p2, **cpp;

  if (fp) {
    while (fgets(str, N_line, fp)) {
      if (!strchr(s_S(S_nop), *str)) return 2;
      if ((cp = get_cp(str))) {
	if (!(p = strchr(cp, ' '))) p = strchr(cp, '\t');
	if (!p) return 1;
	*p = '\0';
	p2 = cp;
	cp = &p[1];
	for (cpp = s_P(P_arc); *cpp; cpp++) {
	  p = *cpp;
	  if (!strncmp(p, cp, strlen(cp))) {
	    p = strchr(p, ' ');
	    p = ((cp = strchr(p, ' '))) ? &cp[1] : &p[1];
	    cp = malloc(strlen(p2) + 1 + strlen(p) + 1);
	    sprintf(cp, "%s %s", p2, p);
	    save_child(chp, cp, 1, NULL);
	    free(cp);
	  }
	}
      }
    }
  } else
    for (cpp = s_P(P_arc); *cpp; cpp++) {
      strcpy(str, *cpp);
      if ((cp = get_cp(str))) {
	if (!(p = strchr(cp, ' '))) p = strchr(cp, '\t');
	if (!p) p = cp;
	if ((p = strchr(p, '|')) && strchr(&p[1], '|')) {
	  strcpy(str, cp);
	  save_error("conf_arc(): \042%s\042: %s.\n", str, s_S(S_errm));
	  save_stderr("conf_arc(): \042%s\042: %s.\n", str, s_S(S_errm));
	} else save_child(chp, cp, 1, NULL);
      }
    }
  return -1;
}

static int conf_bin(FILE *fp, char str[], int f_child, CHILD *chp, char sh[])
{
  CHILD *chp2, *chp3;
  char *cp, *p;
  u_char **spp;
  int f_set = 1;

  while (fgets(str, N_line, fp)) {
    if (!strchr(s_S(S_nop), *str)) return 1 + f_set;
    if ((cp = get_cp(str))) {
      if (!(p = strchr(cp, ' '))) p = strchr(cp, '\t');
      if (!p) p = cp;
      if ((p = strchr(p, '|')))
	if (strchr(&p[1], '|')) {
	  strcpy(str, cp);
	  return 1;
	}
      if (f_child < 14) save_child(chp, cp, f_child + 1, NULL);
      else if (is_fm()){
	spp = &Str.S[S_option + f_child - 12];
	if ((p = strchr(*spp, ' '))) *p = '\0';
	chp2 = get_child(NULL, NULL, *spp, NULL, NULL);
	chp3 = save_child(NULL, cp, 0, NULL);
	chp3->next = chp2->next;
	(get_child(NULL, NULL, NULL, NULL, chp2))->next = chp3;
	free_chp(chp2);
	free(*spp);
	*spp = strdup(cp);
      }
      if (f_child == 4) strcpy(sh, cp);
      f_set = 2;
    }
  }
  return -1;
}

static int conf_grep(FILE *fp, char str[], LIST **lgpp, STR **sgpp)
{
  int size, code;
  char *cp;
  PN pn;
  JIS jis;

  jis.c = 0;
  while (fgets(str, N_line, fp)) {
    char *p;
    if (!strchr(s_S(S_nop), *str)) return 1;
    if ((cp = get_cp(str))) {
      p = cp;
      if (*cp == '\042' || *cp == '\047') {
	p++;
	cp++;
	while (*cp != '\042' && *cp != '\047') cp++;
      } else while (*cp != ' ' && *cp != '\t') cp++;
      *cp = '\0';
      memmove(str, p, strlen(p) + 1); /* p may overlap str; strcpy() is UB here and
					  aborts under macOS's fortified libc */
      size = strlen(str);
      if ((code = init_code(&size, str, 0)) == C_sjis) toeuc(str);
      else if (code == C_jis) {
	size = jistoeuc(size, str, &jis);
	str[size] = '\0';
      }
      *lgpp = save_list(*lgpp, str, &pn);
      if (sgpp) *sgpp = save_str(*sgpp, str, &pn);
    }
  }
  return -1;
}

static int conf_prog(FILE *fp, char str[], LIST **lcpp, STR **scpp)
{
  int i;
  char *cp;
  static PN pn;

  for (i = 1; fgets(str, N_line, fp) != 0; ) {
    if (!strchr(s_S(S_nop), *str)) return 1;
    if ((cp = get_cp(str))) {
      *lcpp = save_list(*lcpp, cp, &pn);
      *scpp = save_str(*scpp, cp, &pn);
      i++;
    }
  }
  return -1;
}

static int conf_bsize(FILE *fp, char str[], int *bsizep)
{
  char *cp;
  int i;

  if ((cp = get_val(fp, str, &i)) == NULL) return i;
  if ((i = atoi(cp)) > 100 || !i) {
    save_error("%src: BUFFER: %s.\n", s_S(S_tcb), s_S(S_errm));
    i = 1;
  }
  *bsizep = i * 1024 * 1024;
  return 0;
}

static int conf_code(FILE *fp, char str[])
{
  char *cp, *sp, val[3];
  int code, i = 0;

  do {
    if (!fgets(str, N_line, fp) || (i = strchr(s_S(S_nop), *str) == NULL))
      return (i) ? 1 : -1;
    if ((cp = strchr(str, '\n'))) *cp = '\0';
    for (cp = str; *cp && *cp != '#' && (*cp < '0' || *cp > '9'); cp++) ;
  } while (*cp < '0' || *cp > '9');
  for (cp = str, sp = s_S(S_code); ; ) {
    while (*cp && (*cp < '0' || *cp > '9')) cp++;
    if (!*cp) break;
    for (i = 0; i < 2 && *cp >= '0' && *cp <= '9'; i++, cp++) val[i] = *cp;
    val[i] = '\0';
    if ((code = atoi(val)) < 32) { 
      if (!code && sp == s_S(S_code)) break;
      else *sp++ = code;
    }
  }
  *sp = '\0';
  return 0;
}

static int conf_history(FILE *fp, char str[])
{
  char *cp;
  int i;

  if ((cp = get_val(fp, str, &i)) == NULL) return i;
  save_history(F6, cp, 0);
  return 0;
}


int conf_wait(FILE *fp, char str[])/*tcb*/
{
  static int k_wait = 100000;
  char *cp;
  int i;

  if (fp) {
    if ((cp = get_val(fp, str, &i)) == NULL) return i;
    k_wait = atoi(cp) * 10000;
    return 0;
  } else return k_wait;
}

static int conf_scroll(FILE *fp, char *str)
{
  static int f_scroll;
  char *cp;
  int i = 0;

  if (fp) {
    if ((cp = get_val(fp, str, &i)) == NULL) return i;
    f_scroll = atoi(cp);
  }
  if (f_scroll) {
    vt_mode('h');
    vt_cs(vt_row(0) - 1, 0);
  } else {
    vt_mode(-'h');
    vt_cs(vt_row(0) - 2, 0);
  }
  return i;
}

int conf_ls(FILE *fp, char *str)/*tcb*/
{
  static int f_ls;
  char *cp;
  int i = 0;

  if (fp) {
    if ((cp = get_val(fp, str, &i)) == NULL) return i;
    f_ls = atoi(cp);
    return i;
  } else if (str) f_ls = atoi(str);
  return f_ls;
}

static int get_str(FILE *fp, u_char str[])
{
  u_char *p, *cp;

  if (!fgets(str, N_char, fp)) return -1;
  if (!strncmp(str, "P-?", 3))
    if (is_pg()) return 3;
    else *str = '#';
  else if (!strncmp(&str[1], "-?", 2)) return 2;
  if (!strchr(s_S(S_nop), *str)) return 1;
  if (strchr(str, '\042') >= strchr(str, '#')) cp = str;
  else
    for (cp = str; (p = strchr(cp, '\042')); cp++) {
      if (!(cp = strchr(&p[1], '\042'))) return -1;
      if ((p = strchr(cp, '#'))) {
	*p = '\0';
	break;
      }
    }
  if ((cp = strchr(cp, '#'))) *cp = '\0';
  if ((cp = strchr(str, '\n'))) *cp = '\0';
  for (cp = str; *cp && (*cp == ' ' || *cp == '\t'); cp++) ;
  if (cp > str) strcpy(str, cp);
  return 0;
}

static void key_err(FILE *fp, char mark[], int f_err)
{
  fclose(fp);
  save_stderr("%src: %s: %s.\n", s_S(S_tcb), mark, s_S(f_err));
  exit_tcb();
  print_stderr(NULL);
  exit(1);
}

static char *save_ov(int k)
{
  int c, d;
  u_int u;
  char *cp;
  static char tmp[40];

  u = 0xffffffff;
  cp = tmp;
  *cp = '\0';
  do {
    u >>= 8;
    if (k > u) {
      d = k;
      k &= u;
      for (c = u; c; c >>= 8, d >>= 8) ;
      if (d < ' ') {
	sprintf(cp, "^%c", 'A' + d - 1);
	cp += 2;
      } else if (d == ' ') {
	sprintf(cp, "Space");
	cp += 5;
      } else if (d == K_at) {
	sprintf(cp, "^Space");
	cp += 6;
      } else if (!d) *cp = '\0';
      else {
	sprintf(cp, "%c", d);
	cp++;
      }
    }
  } while (u);
  return tmp;
}

static char *set_m(char *cp, int *mp)
{
  int i;
  char p[3];

  for (i = 0; *cp >= '0' && *cp <= '9'; i++) p[i] = *cp++;
  p[i] = '\0';
  *mp = atoi(p);
  while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
  return cp;
}

static char *set_s(u_char *cp, u_char s[])
{
  int i, len;
  u_char c, *p;

  len = (u_int)strchr(&cp[1], '\042') - (u_int)&cp[1];
  for (i = 0, p = s; i < len; i++)
    if (!strncmp(&cp[1+i], "\\c", 2) && (c = cp[1+i+2]) >= 'A' && c <= 'Z') {
      *p++ = c - 'A' + 1;
      i += 2;
    } else *p++ = cp[1+i];
  *p = '\0';
  cp += len + 2;
  while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
  return cp;
}

int conf(char *temp, char *args, char *version, int f_tcb)/*tcb*/
{
#include "config.h"
  static char LS[] = "ls -alFN";
  static char TAR[] = "xvf";
  static char LHA[] = "e";
  static char UNZIP[] = "";
  static char CPIO[] = "idv";
  static char AR[] = "p";
  static char *ARCHIVE[] = {
    ".tar tar tvf",
    ".tgz gzip -dc | tar tvf -",
    ".tar.bz2 bzip2 -dc | tar tvf -",
    ".lzh lha",
    ".zip unzip -l",
    ".rpm rpm -qplv",
    ".deb ar tv",
    NULL,
  };
  static char *err[] = {
    "Interrupted", 					/* S_err0 */
    "No size", 						/* S_err1 */
    "No string",					/* S_err2 */
    "No such file",					/* S_err3 */
    "Not enough buffer",				/* S_err4 */
    "Failed to open /dev/pty..",			/* S_err5 */
    "No manual entry",	 				/* S_err6 */
    "Not a directory", 					/* S_err7 */
    "Segmentation fault",				/* S_err8 */
    "Hit any key",					/* S_err9 */
    "Wrote",						/* S_erra */
    "Macro interrupted",				/* S_errb */
    "Not installed",					/* S_errc */
    "Edit `SHELL' of tcbrc",				/* S_errd */
    "~/.tcb/tcbrc ignored",				/* S_erre */
    "SHELL busy",					/* S_errf */
    "Exit",						/* S_errg */
    "Hit Enter",					/* S_errh */
    "SHELL: hangup",					/* S_erri */
    "DU busy",						/* S_errj */
    "Reading ...",					/* S_errk */
    "Failed to allocate memory",			/* S_errl */
    "Invalid format", 					/* S_errm */
    "Can't be set here",				/* S_errn */
    "Out of range",					/* S_erro */
    "Defining keyboard macro...",			/* S_errp */
    "Keyboard macro defined",				/* S_errq */
    "Not defining keyboard macro",			/* S_errr */
    "No builtin macro",					/* S_errs */
    "Invalid login shell",				/* S_errt */
    "Invalid terminal",					/* S_erru */
    "Invalid mode",					/* S_errv */
    "Exited abnormally",				/* S_errw */
    "Clear previous mode",				/* S_errx */
    "(y/n)",						/* S_erry */
    NULL,
  };
  static char *shell[] = {
    "bash",
    "tcsh",
  };
  static char tcb[] = "tcb";
  static char jman[] = "man"; 
  static char *groff[] = {
    "groff",
    "-mandoc",
    "-Tutf8",
  };
  char *com[] = {LS, DU, CP, MV, RM};
  int i, j, pv[2], erre = 0;
  int i_mess, i_home;
  char *cp;
  struct stat st;
  char home[256], tmp[25600], mess[N_line];

  if (temp) {
    strcpy(temp, TMP);
    /*
     * following files will be used at "TMP" (defined in config.h) directory.
     *   d... notice of termination of `du'.
     *   e... log of ERROR after startup with directory argument.
     *   i... information about error message No..
     *   l... log of SHELL after startup with directory argument.
     *   s... output from `tcb' launched at SHELL-prompt.
     *   w... content of ^Jw and ^J^W.
     *   t... work file used instead of memory failed to be allocated.
     */
    return 0;
  }
  if (!(cp = ttyname(1))) {
    fprintf(stderr, "%s: %s.\n", tcb, strerror(errno));
    return -1;
  }
  Str.S[S_tty] = strdup(cp);
  Str.S[S_tcb] = strdup(tcb);	/* S_tcb */
  strcpy(home, getenv("HOME"));
  if (strchr(home, '\0')[-1] != '/') strcat(home, "/");
  Str.S[S_home] = strdup(home);
  strcpy(tmp, LIB);
  if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
  Str.S[S_lib] = strdup(tmp);
  if (!getenv("LANG")) setenv("LANG", "C", 1);
  Str.S[S_LANG] = strdup(getenv("LANG"));
  sprintf(strchr(home, '\0'), ".%s", s_S(S_tcb));
  if (!stat(home, &st)) {
    if (!S_ISDIR(st.st_mode)) {
      fprintf(stderr, "%s: %s: %s.\n", s_S(S_tcb), home, strerror(EEXIST));
      return -1;
    }
    if (access(home, W_OK) < 0) {
      fprintf(stderr, "%s: %s: %s.\n", s_S(S_tcb), home, strerror(errno));
      return -1;
    }
  } else if (mkdir(home, S_IRWXU|S_IRWXG|S_IRWXO) < 0) {
    fprintf(stderr, "%s: %s: %s.\n", tcb, home, strerror(errno));
    return -1;
  }
  sprintf(mess, "%s/s.", TMP);
  strcat(home, "/");
  i_home = strlen(home);
  i_mess = strlen(mess) - 2;
  strcat(mess, "XXXXXX");
  if (mkstemp(mess) < 0) {
    fprintf(stderr, "%s: mktemp: %s.\n", tcb, strerror(errno));
    return -1;
  }
  unlink(mess);
  Str.S[S_mess] = strdup(mess);	/* S_mess */
  fprintf(stderr, "S_mess=%s\n", Str.S[S_mess]);
  mess[i_mess] = 'd';
  Str.S[S_enter] = strdup(mess);	/* S_enter */
  mess[i_mess] = 'i';
  Str.S[S_intr] = strdup(mess);	/* S_intr */
  sprintf(tmp, "%s.%s/w.%s", s_S(S_home), s_S(S_tcb), s_S(S_tcb));
  Str.S[S_write] = strdup(tmp);
  sprintf(tmp, "%s.%s/stderr", s_S(S_home), s_S(S_tcb));
  Str.S[S_stderr] = strdup(tmp);
  cp = set_tcbrc(&erre);
  if (erre < 0) {
    fprintf(stderr, "%src: %s.\n", tcb, strerror(ENOENT));
    free(cp);
    return -1;
  }
  Str.S[S_tcbrc] = cp;
  Str.S[S_code] = calloc(sizeof(char), 32);	/* S_code */
  Str.S[S_version] = strdup(version);
  Str.S[S_path] = strdup(getenv("PATH"));	/* S_path */
  Str.S[S_option+7] = malloc(strlen(s_S(S_tcb)) 
			     + strlen(args) + 1);
  sprintf(Str.S[S_option+7], "%s%s", s_S(S_tcb), args);
  Str.S[S_nop] = strdup("# \t\n");
  if (!getenv("TCBCHARSET")) setenv("TCBCHARSET", CHARSET, 1);
  if (f_tcb & (0x80|0x40)) {
    if (f_tcb & 0x80) {
      char *p;
      home[i_home] = '\0';
      strcat(home, "history");
      Str.S[S_hist] = strdup(home);
      strcpy(&home[i_home], "pid");
      Str.S[S_pid] = strdup(home);
      strcpy(&home[i_home], "error.old");
      p = alloca(strlen(home) + 1);
      strcpy(p, home);
      strcpy(&home[i_home], "error");
      rename(home, p);
      Str.S[S_error] = strdup(home);
      strcpy(&home[i_home], "shell.old");
      strcpy(p, home);
      strcpy(&home[i_home], "shell");
      Str.S[S_log] = strdup(home);
      rename(home, p);
    } else {
      mess[i_mess] = 'e';
      Str.S[S_error] = strdup(mess);
      unlink(mess);
      mess[i_mess] = 'l';
      Str.S[S_log] = strdup(mess);
      unlink(mess);
    }
    pipe(pv);
    if (!fork()) {
      int pv2[2];
      if (!fork()) {
	close(1);
	dup2(pv2[1], 1);
	close(pv2[1]);
	close(pv2[0]);
	write(1, ".\\\n", 3);
	exit(0);
      }
      close(0);
      dup2(pv2[0], 0);
      close(pv2[0]);
      close(pv2[1]);
      close(2);
      dup2(pv[1], 2);
      close(pv[1]);
      close(pv[0]);
      execlp(groff[0], groff[0], groff[2], NULL);
      exit(0);
    }
    close(pv[1]);
    cp = malloc(strlen(groff[0]) + 1 
		+ strlen(groff[1]) + 1
		+ strlen(groff[2]) + 1);
    sprintf(cp, "%s %s ", groff[0], groff[1]);
    for (j = 0; read(pv[0], &tmp[j], 1) == 1 && j < 256; j++) ;
    if (!j) strcat(cp, groff[2]);
    else {
      if (j == 256) j--;
      tmp[j] = '\0';
      save_error("%s", tmp);
      strcat(cp, "-R");
    }
    close(pv[0]);
    pipe(pv);
    if (!fork()) {
      dup_error();
      close(0);
      close(1);
      dup2(pv[1], 1);
      close(pv[0]);
      close(pv[1]);
      execlp("ls", "ls", "-N", NULL);
      exit(0);
    }
    close(pv[1]);
    for (j = 0; read(pv[0], &tmp[j], 1) == 1 && j < 256; j++) ;
    if (!j) *strchr(LS, 'N') = ' ';
    close(pv[0]);
    for (i = 0; i < 5; i++) Str.S[S_option+i] = strdup(com[i]);
    Str.S[S_mark] = malloc(6);
    sprintf(Str.S[S_mark], "%c%s", '^', "[[?m");
    Str.S[S_option+5] = cp;
    Str.S[S_option+6] = strdup(jman);
    sprintf(tmp, "%s%s", s_S(S_lib), &s_P(P_mode)[M_help][1]);
    Str.S[S_help] = strdup(tmp);
    mess[i_mess] = 'w';
    Str.S[S_w] = strdup(mess);		/* S_w */
    mess[i_mess] = 't';
    Str.S[S_tmp] = strdup(mess);	/* S_tmp */
    for (i = j = 0; i < 2; i++) 
      if (is_inst(s_S(S_path), shell[i]) && !j) j = 1;
    if (!j) {
      fprintf(stderr, "Install %s or %s.\n", shell[0], shell[1]);
      return -1;
    }
    strcpy(tmp, getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
    for (i = 0, cp = NULL; i < 2; i++) 
      if (strstr(tmp, shell[i])) {
	cp = shell[i];
	break;
      }
    if (!cp) {
      if (!strcmp(tmp, "/bin/sh")) {
	pipe(pv);
	if (!fork()) {
	  close(1);
	  dup2(pv[1], 1);
	  close(pv[1]);
	  close(pv[0]);
	  execlp("file", "file", "/bin/sh", NULL);
	}
	close(pv[1]);
	read(pv[0], tmp, 256);
	close(pv[0]);
	tmp[255] = '\0';
      }
      if (strstr(tmp, shell[0])) cp = shell[0];
    }
    if (cp) {
      char *p = alloca(strlen(cp) + 1);
      strcpy(p, cp);
      get_prog(p);
      Str.S[S_sh] = strdup(cp);
      if (!is_inst(s_S(S_path), p)) {
	fprintf(stderr, "%s: %s: %s.\n", tcb, p, err[S_errc - S_err0]);
	return -1; /* s_S(S_sh) isn't installed */
      }
    }
    sprintf(tmp, "%s  %s", &s_P(P_mode)[M_shell][1], version);
    *strchr(tmp, '\n') = '\0';
    Str.S[S_lb2] = strdup(tmp);	/* S_lb2 */
    for (i = j = 0; i < S_tty - S_ls; i++, j++) {
      cp = s_P(P_child)[j];
      /* set Str.S[S_ls]  ... */ 
      if (!cp || !strlen(cp)) continue;
      else {
	char *p = alloca(strlen(cp) + 1);
	strcpy(p, cp); 
	get_prog(p);
	Str.S[i + S_ls] = strdup(p);
      }
    }
    Str.S[S_arc1] = strdup(TAR);
    Str.S[S_arc2] = strdup(LHA);
    Str.S[S_arc3] = strdup(UNZIP);
    Str.S[S_arc4] = strdup(CPIO);
    Str.S[S_arc5] = strdup(AR);
  } else
    for (i = 0, j = S_file - S_ls; i < S_tty - S_file; i++, j++) {
      /* set Str.S[S_file]  ... */ 
      cp = s_P(P_child)[j];
      if (!cp || !strlen(cp)) continue;
      else { 
	char *p = alloca(strlen(cp) + 1);
	strcpy(p, cp);
	get_prog(p);
	Str.S[i + S_file] = strdup(p);
      }
    }
  for (i = 0; err[i]; i++) 
    Str.S[i + S_err0] = strdup(err[i]);
  if (erre) save_intr(S_erre);
  print_tcb(mess, f_tcb);
  Str.S[S_TCB] = strdup(mess);
  s_P((uintptr_t)ARCHIVE);
  return 0;
}

void free_S(void)/*tcb*/
{
  int i;

  for (i = 0; i < S_nn; i++) r_S(i);
}

u_char *print_key(int *key_c)/*tcb*/
{
  static struct {
    int key;
    int mode;
  } command[] = {
    {K_a, 0},			/* ^A */
    {K_b, 0},			/* ^B */
    {K_c, 0},			/* ^C */
    {K_d, 0},			/* ^D */
    {K_e, 0},			/* ^E */
    {K_f, 0},			/* ^F */
    {K_g, M_bl},		/* ^G */
    {K_h, 0},			/* ^H */
    {K_i, M_list},		/* ^I */
    				/* ^J */
    {K_k, 0},			/* ^K */
    {K_l, M_clear}, 		/* ^L */
    {K_m, M_cr},		/* ^M */
    {K_n, M_dn},		/* ^N */
				/* ^O */
    {K_p, M_up},		/* ^P */
    {K_q, 0},			/* ^Q */
    {K_r, M_hist},		/* ^R */
    {K_s, M_fgrep},		/* ^S */
				/* ^T */
    {K_u, 0},			/* ^U */
				/* ^V */
    {K_w, 0},			/* ^W */
				/* ^X */
    {K_z, 0},			/* ^Z */
    {0, 0},
  };
  static u_char inval_0[] = {/* pager */
    M_help, M_sel, M_fp, M_code, M_cd, M_m, M_bl2,
    M_fcut2, M_fcut, M_du, M_du2, M_locate, M_locate2, M_prog2, M_prog, 
    M_man2, M_man, M_lfile2, 
    M_lfile, M_ldir2, M_ldir, M_cmd, M_cp, M_cp2, M_mv, M_mv2, M_rm, 
    M_rm2, M_arc, M_arc2, M_dest, M_yes, M_no, M_shell, M_hup, 
    M_chdir2, M_chdir, M_w, M_w2, M_list, M_error, M_log, 
    M_ascii, M_kanji, M_exit, M_ls,
    0,
  };
  static u_char inval_2[] = { /* filemanager */
    M_quit, 0,
  };
  u_char *p, *cp, **label;
  int i, j, k, t;

  p = (s_S(S_error)) ? inval_2 : inval_0;
  if (!key_c) return p;
  label = &Str.S[S_label];
  for (i = 0; i < M_tcb; i++) {
    if (i >= 80 && !s_S(S_macro + i - 80)) continue;
    if (!label[i]) continue;
    t = (i) ? i : M_tcb;
    if (strchr(p, t)) continue;
    printf("%d:", t);
    cp = save_ov(key_c[t]);
    k = (t == M_cd || t == M_num) ? 1 : 0;
    if (strlen(cp) < 8-k) for (j = 8-k-strlen(cp); j; j--) putchar(' ');
    printf("%s", tobo(cp, 0));
    if (k == 1) printf("N");
    printf(" : %s", label[i]);
    do {
      if (i >= 80) printf(" (M)");
      switch(key_c[t]) {
      case K_v:		/* ^V */
      case (K_ec<<8) + 'v':	/* ^[v */
      case K_z:		/* ^Z */
	printf(" (ignored)");
	continue;
	break;
      default:
	for (j = 0; key_c[j]; j++)
	  if (j != t && key_c[j] == key_c[t]) break;
	if (key_c[j]) {
	  printf(" (overlapped)");
	  continue;
	}
	if (key_c[t] >= ' ' && key_c[t] <= 0x7e) ;
	else {
	  for (j = 0; command[j].key; j++)
	    if (command[j].key == key_c[t] 
		&& command[j].mode != t) break;
	  if (!command[j].key) continue;
	}
	break;
      }
    } while (0);
    printf("\n");
  }
  return NULL;
}

void set_key(int **key_mp, int **key_cp, int key_m0[], int key_m[], int key_c[])
{
  int i;

  *key_mp = key_m;
  *key_cp = key_c;
  *key_m = *key_m0 = -1;
  for (i = 0; i < M_nn; i++) key_c[i] = -1;
  /* built-in keys */
  key_c[M_key] = K_h;
  key_c[M_klt] = K_lt;
  key_c[M_krt] = K_rt;
  key_c[M_kup] = K_up;
  key_c[M_kdn] = K_dn;
  key_c[M_knext] = K_next;
  key_c[M_kprev] = K_prev;
  key_c[M_kforw] = K_v;
  key_c[M_kback] = (K_ec<<8) + 'v';
  key_c[M_dl] = K_dl;
  key_c[M_susp] = K_z;
  key_c[M_hs] = (K_o<<8) + 'h';
  key_c[M_ascii] = (K_o<<8) + 'a';
  key_c[M_copy] = (K_o<<8) + 'c';
  key_c[M_paste] = (K_o<<8) + 'p';
  key_c[M_ms] = (K_x<<8) + '(';
  key_c[M_me] = (K_x<<8) + ')';
  key_c[M_mm] = (K_x<<8) + 'e';
}

void read_mark(FILE *fp, char mark[], u_char str[])
{
  if (!strncmp(&str[1], &mark[1], strlen(mark) - 1)) {
    if (*str == *mark) return;
    else key_err(fp, mark, S_errm);
  }
  do {
    if (!fgets(str, N_char, fp)) key_err(fp, mark, S_errm);
  } while (strncmp(str, mark, strlen(mark)));
}

static void conf_m(FILE *fp, u_char str[])
{
  static char mark[] = "M-?";
  int i, j, f_m;
  u_char *cp, line[N_line];
  
  read_mark(fp, mark, str);
  f_m = !is_pg();
  for (i = 0; ; i++) {
    if ((j = get_str(fp, str)) < 0) key_err(fp, mark, S_errm);
    else if (is_pg() && j == 3) {
      i--;
      f_m = 1;
      continue;
    } else if (j) return;
    if (!*str) {
      i--;
      continue;
    }
    *line = '\0';
    if (!f_m) continue;
    for (cp = str; *cp; ) 
      if (!strncmp(cp, "K-", 2)) {
	cp = set_m(&cp[2], &j);
	strcat(line, mtoa(j)); 
      } else if (*cp == '\042') cp = set_s(cp, strchr(line, '\0'));
      else if (*cp == '\\') {
	do {
	  if (get_str(fp, str)) key_err(fp, mark, S_errm);
	} while (!*str);
	cp = str;
      }
    Str.S[S_macro + i] = strdup(line);	/* S_m0 ... S_m19 */
    f_m = !is_pg();
  }
}

static void conf_f(FILE *fp, u_char str[], int *i_mp, int *n_mp, int key_m0[], int key_m[])
{
  int i, j, k;
  u_char *cp;
  static char mark[] = "F-?";
  static char errn[] = "^[ (Esc)";

  read_mark(fp, mark, str);
  for (i = *n_mp = *i_mp = 0; ; i++) {
    if ((j = get_str(fp, str)) == 2) return;
    else if (j) key_err(fp, mark, S_errm);
    if (!*str) {
      i--;
      continue;
    }
    for (j = k = 0, cp = str; *cp; j += 8) {
      if (!strncmp(cp, "C-", 2) 
	       || !strncmp(cp, "C_", 2)
	       || !strncmp(cp, "\\c", 2)) {
	cp += 2;
	if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	  k = (k << j) + K_at;
	  cp += 2;
	} else if (*cp >= 'a' && *cp <= 'z') k = (k << j) + *cp - 'a' + 1;
	else if (*cp == '[') key_err(fp, errn, S_errn);
	else if ((*cp >= 'A' && *cp <= 'Z')
		 || (*cp > '[' && *cp <= '^'))
	  key_err(fp, &cp[-2], S_errm);
	cp++;
	switch(*cp) {
	case 0:
	case ' ':
	case '\t':
	  break;
	default:
	  key_err(fp, &cp[-3], S_errm);
	  break;
	}
      } else if (!strncmp(cp, "ESC", 3)) key_err(fp, errn, S_errn);
      else if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	k = (k << j) + 0x20;
	cp += 3;
      } else k = (k << j) + *cp++;
      while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
    }
    *n_mp = i;
    j = 0;
    if (k < 0xff) ;
    else if (k < 0xffff) key_m[(*i_mp)++ + 1] = k >> 8;
    else if (k < 0xffffff) {
      key_m[(*i_mp)++ + 1] = k >> 16;
      key_m[(*i_mp)++ + 1] = (k >> 8) & 0xff;
    }
    key_m0[i+1] = k;
  }
}

static int conf_c(FILE *fp, u_char str[], int i0, int *i_mp, int n_m, int key_m0[], int key_c[], int key_m[], char comm[])
{
  static char mark[] = "K-?";
  int i, j, k;
  u_char *cp, **label;

  label = &Str.S[S_label];
  read_mark(fp, mark, str);
  for (i = i0; ; i++) {
    j = get_str(fp, str);
    if (!i0 && j == 2 && i < M_end) {
    }
    if (!i0) {
      if (j == 2) {
	if (i < M_end) key_err(fp, mark, S_errm);
	else return j;
      } else if (j) key_err(fp, mark, S_errm);
    } else if (j) return j;
    if (!*str) {
      i--;
      continue;
    }
    if ((cp = strchr(str, '\042'))) {
      if ((j = (u_int)rindex(&cp[1], '\042') - (u_int)&cp[1]) > 0) {
	label[i] = calloc(1, j+1);
	memcpy(label[i], &cp[1], j);
      }
      *cp = '\0';
      if (i == M_chdir) {
	char *p;
	int len;
	if ((p = strchr(label[i], '`')) 
	    && (len = (u_int)strchr(&p[1], '\047') - (u_int)&p[1]) - 1 > 0) {
	  memcpy(comm, &p[1], len);
	  while ((p = strstr(comm, "\\c")))
	    if (p[2] >= 'A' && p[2] <= 'Z') {
	      *p = p[2] - 'A' + 1;
	      strcpy(&p[1], &p[3]);
	    }
	  while ((p = strstr(label[i], "\\c")))
	    if (p[2] >= 'A' && p[2] <= 'Z') {
	      sprintf(p, "^%c", p[2]);
	      strcpy(&p[2], &p[3]);
	    }
	}
      }
    } else {
      i--;
      continue;
    }
    for (j = k = 0, cp = str; *cp; j += 8) {
      if (!strncmp(cp, "F-", 2) || !strncmp(cp, "F_", 2)) {
	cp += 2;
	if (*cp > n_m + '0') {
	  char *p = alloca(4);
	  sprintf(p, "F-%c", *cp);
	  key_err(fp, p, S_erro);
	} else if (j)
	  if (key_m0[*cp - '0'+1] > 256) k = (k << 16) + key_m0[*cp - '0' + 1];
	  else k = (k << 8) + key_m0[*cp - '0' + 1];
	else k = key_m0[*cp - '0' + 1];
	cp++; 
      } else if (!strncmp(cp, "C-", 2) 
		 || !strncmp(cp, "C_", 2)
		 || !strncmp(cp, "\\c", 2)) {
	cp += 2;
	if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	  k = (k << j) + K_at;
	  cp += 2;
	} else if (*cp >= 'a' && *cp <= 'z') k = (k << j) + *cp - 'a' + 1;
	else if (*cp == '[') k = (k << j) + *cp - 0x40;
	else if ((*cp >= 'A' && *cp <= 'Z')
		 || (*cp > '[' && *cp <= '^'))
	  key_err(fp, &cp[-2], S_errm);
	cp++;
	switch(*cp) {
	case 0:
	case ' ':
	case '\t':
	  break;
	default:
	  key_err(fp, &cp[-3], S_errm);
	}
      } else {
	if (!strncmp(cp, "ESC", 3)) {
	  k = (k << j) + 0x1b;
	  cp += 3;
	} else if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	  k = (k << j) + 0x20;
	  cp += 3;
	} else k = (k << j) + *cp++;
      }
      while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
    }
    if ((key_c[(i) ? i : M_tcb] = k) > 0xff && k >> 8 != K_ec) {
      k >>= 8;
      do {
	for (j = 1; j < *i_mp && key_m[j] != k; j++) ;
	if (j == *i_mp) key_m[(*i_mp)++] = k;
	k >>= 8;
      } while (k);
    }
  }
  return 0;
}

int conf_key(FILE *fp, char str[], int **key_mp, int **key_cp, char *comm)/*tcb*/
{
  static int key_m[N_f*5];
  static int key_m0[N_f+1];
  static int key_c[M_nn+1];
  int n_m, i_m, f_skip = 0;
 
  if (key_cp == F5) print_key(key_c);
  else if (!fp) set_key(key_mp, key_cp, key_m0, key_m, key_c);
  else if (fp == F5) *((int*)key_cp) = key_c[*((int*)key_mp)];
  else {
    conf_f(fp, str, &i_m, &n_m, key_m0, key_m);
    conf_c(fp, str, 0, &i_m, n_m, key_m0, key_c, key_m, comm);
    conf_m(fp, str);
    f_skip = conf_c(fp, str, N_c, &i_m, n_m, key_m0, key_c, key_m, comm);
  }
  return f_skip;
}

int init_config(CHILD *chp, LIST **lcpp, LIST **lgpp, STR **scpp, STR **sgpp, u_int *bsizep, int **key_mp, int **key_cp, char name[], u_char comm[])/*tcb*/
{
  FILE *fp;
  int f_shell, f_tcbrc, n_tcbrc, f_skip;
  u_char *p, *cp, str[N_line];
  char **tcbrc, sh[5];
  
  conf_key(NULL, str, key_mp, key_cp, NULL);
  tcbrc = s_P(P_tcbrc);
  for (n_tcbrc = 0; tcbrc[n_tcbrc]; n_tcbrc++) ;
  p = alloca(n_tcbrc + 1);
  memset(p, ' ', n_tcbrc);
  p[n_tcbrc] = '\0';
  fp = fopen(s_S(S_tcbrc), "r");
  conf_arc(NULL, str, chp);
  for (f_shell = f_skip = 0; f_skip >= 0; ) {
    if (!f_skip)
      do {
	if (!fgets(str, N_line, fp)) f_skip = -1;
      } while (f_skip >= 0 && (strchr(s_S(S_nop), *str)));
    if (f_skip < 0) continue;
    f_tcbrc = 0;
    do {
      if (!(cp = strchr(str, ' ')))
	if (!(cp = strchr(str, '\t')))
	  if (!(cp = strchr(str, '\n'))) continue;
      *cp = '\0';
    } while (0);
    for (f_tcbrc = 0; tcbrc[f_tcbrc]; f_tcbrc++) 
      if (!strcmp(tcbrc[f_tcbrc], str)) break;
    if (tcbrc[f_tcbrc] && !strchr(p, f_tcbrc + '0')) f_skip = 0;
    else if (!strncmp(&str[1], "-?", 2)) {
      f_skip = 0;
      continue;
    } else {
      save_stderr("%src: %s: %s.\n", s_S(S_tcb), str, s_S(S_errm));
      return -1;
    }
    p[f_tcbrc] = f_tcbrc + '0';
    switch(f_tcbrc) {
    case 0:
      if ((f_skip = conf_arc(fp, str, chp)) == 1)
	save_error("%src: %s: %s.\n", s_S(S_tcb), str, s_S(S_errm));
      break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 14:
    case 15:
    case 16:
      if ((f_skip = conf_bin(fp, str, f_tcbrc, chp, sh)) == 1) {
	save_error("%src: %s: %s.\n", s_S(S_tcb), str, s_S(S_errm));
	return -1;
      }
      if (f_tcbrc == 4 && f_skip == 3) f_shell = 1;
      break;
    case 5:
      f_skip = conf_grep(fp, str, lgpp, sgpp);
      break;
    case 6:
      f_skip = (lcpp) ? conf_prog(fp, str, lcpp, scpp) : 0;
      break;
    case 7:
      f_skip = conf_bsize(fp, str, bsizep);
      break;
    case 8:
      f_skip = conf_code(fp, str);
      break;
    case 9:
      f_skip = conf_history(fp, str);
      break;
    case 10:
      f_skip = conf_wait(fp, str);
      break;
    case 11:
      f_skip = conf_scroll(fp, str);
      break;
    case 12:
      f_skip = conf_ls(fp, str);
      break;
    case 13:
      f_skip = conf_key(fp, str, key_mp, key_cp, comm);
      break;
    default:
      f_skip = 0;
      break;
    }
  }
  f_tcbrc = 0;
  if (lcpp)  
    do {
      if (!f_shell)
	if (s_S(S_sh)) continue;
        else save_stderr("%s: %s.\n", s_S(S_tcb), s_S(S_errd), NULL);
      else if (f_shell 
	       && !get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL)) {
	save_stderr("%s: %s: %s.  %s.\n"
		    , s_S(S_tcb), sh, s_S(S_errc), s_S(S_errd));
      } else continue;
      f_tcbrc = -1;
    } while (0);
  save_child(F10, NULL, 0, NULL);
  fclose(fp);
  return f_tcbrc;
}

char *set_tcbrc(int *errep)/*tcb*/
{
  char *home, *lib;
  struct stat st_lib, st_home;

  home = malloc(strlen(s_S(S_home)) + 1 
		    + strlen(s_S(S_tcb)) + 1 + strlen(s_S(S_tcb)) + 2 + 1);
  sprintf(home, "%s.%s/%src", s_S(S_home), s_S(S_tcb), s_S(S_tcb));
  lib = malloc(strlen(s_S(S_lib))
		   + strlen(s_S(S_tcb)) + 2 + 1);
  sprintf(lib, "%s%src", s_S(S_lib), s_S(S_tcb));
  if (stat(home, &st_home) >= 0) {
    if (access(lib, F_OK) >= 0 && st_lib.st_ctime > st_home.st_ctime) {
      if (errep) *errep = 1;
    } else {
      free(lib);
      return home;
    }
  }
  if (access(lib, F_OK) < 0 && errep) *errep = -1;
  free(home);
  return lib;
}
