#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <sys/stat.h>

static struct {
  int abort;
  jmp_buf jb;
} Sub;

static void sub_clear(int dummy)
{
  Sub.abort = 1;
  longjmp(Sub.jb, 0);
}

static void sspc(u_char *cp, u_char *np)
{ /* convert space character for sscanf() */
  if (np)
    do {
      if (*cp && *cp < ' ') {
	*np++ = K_o;
	*np++ = *cp++ + '@';
      } else if (*cp == ' ') {
	*np++ = K_a;
	cp++;
      } else *np++ = *cp++;
    } while (np[-1]);
  else for (; (cp = strchr(cp, ' ')); *cp = K_a) ;
}

void rspc(u_char *cp, u_char *np)/*usilic*/
{ /* convert space character for sscanf() */
  if (np)
    do {
      if (*cp == K_o) {
	*np++ = *++cp - '@';
	cp++;
      } else if (*cp == K_a) {
	*np++ = ' ';
	cp++;
      } else *np++ = *cp++;
    } while (np[-1]);
  else for (; (cp = strchr(cp, K_a)); *cp = ' ') ;
}

static void print_list(LIST *lp, FILE *fp)
{
  if (lp) {
    print_list(lp->left, fp);
    fprintf(fp, "%s\n", lp->name);
    print_list(lp->right, fp);
  }
}

static int read_str(FILE *fp, u_char str[], STR **spp, LIST **lpp, int f, int m)
{
  char tmp[N_line], name[N_line], *cp;
  PN pn;

  if (*str != K_l || !is_name(&str[1], m)) return -1;
  if (!fgets(str, N_line, fp)) return -1;
  if (*str == K_l) return 0;
  do {
    if (*str == K_z) return 1;
    if (f) {
      pn.row = pn.nrow = pn.top = 0;
      while (1) {
	if ((cp = strchr(str, '\n'))) *cp = '\0';
	if (strlen(str)) break;
        if (!fgets(str, N_line, fp)) return -1;
      }
      memcpy(tmp, str, N_line-1);
      tmp[N_line-1] = '\0';
    } else {
      sscanf(str, "%s %d %d %d %d", tmp, &pn.row, &pn.nrow, &pn.top, &pn.line);
      if (pn.row < 0) pn.row = 0;
      if (pn.top < 0) pn.top = 0;
      if (pn.nrow < 0) pn.nrow = 0;
      if (!pn.line) pn.line = -1;
    }
    rspc(tmp, name);
    if (lpp) {
      if (spp) *spp = save_str(*spp, name, &pn);
      *lpp = save_list(*lpp, name, &pn);
    } else *spp = save_history(*spp, name, 0);
  } while ((cp = fgets(str, N_line, fp)) && *str != K_l);
  return (cp) ? 0 : -1;
}

static void write_str(FILE *fp, STR *sp, LIST *lp, int f, int m)
{ /* save strings to ~/.usilic/history in STR-list order */
  char name[N_line];

  fprintf(fp, "%c%s\n", K_l, s_P(P_mode)[m]);
  if (sp) {
    for (; sp; sp = sp->next)
      if (sp->name) {
	if (lp) load_list(lp, sp->name, &sp->pn);
	sspc(sp->name, name);
	if (f) fprintf(fp, "%s\n", name);
	else fprintf(fp,"%s %d %d %d %d\n"
		     , name, sp->pn.row, sp->pn.nrow, sp->pn.top, sp->pn.line);
      }
  } else print_list(lp, fp);
}

static void delete_str(STR *sp, char name[])
{ 
  while (sp) {
    if (sp->name && !strcmp(sp->name, name)) {
      free(sp->name);
      sp->name = NULL;
      break;
    }
    sp = sp->next;
  }
}

static void get_par(char *dir)
{ /* set parent directory name to dir[] */
  if (!strcmp(dir, "/")) return;
  for (dir = &strchr(dir, '\0')[-2]; *dir != '/'; dir--) ;
  dir[1] = '\0';
}

static void get_time(char str[])
{
  struct tm tt;
  time_t ll;

  time(&ll);
  memcpy(&tt, localtime(&ll), sizeof(struct tm));
  strcpy(str, asctime(&tt));
  *strchr(str, '\n') = '\0';
}

int find_prog(LIST *prog, u_char buff[])/*usilic*/
{ /* search for buff[] in the program-list of PROGRAM */
  if (prog) {
    int i;
    char *cp;

    if (find_prog(prog->left, buff)) return 1;
    if ((cp = strchr(prog->name, ' '))) *cp = '\0';
    i = strncmp(buff, prog->name, strlen(prog->name));
    if (cp) *cp = ' ';
    if (!i) return 1;
    if (find_prog(prog->right, buff)) return 1;
  }
  return 0;
}

GREP *load_grep(GREP *grep)/*usilic*/
{
  GREP *gp;

  if (!grep) return grep;
  gp = grep->prev;
  free(grep);
  return gp;
}

GREP *save_grep(GREP *grep)/*usilic*/
{
  GREP *gp;

  gp = calloc(sizeof(GREP), 1);
  gp->prev = grep;
  return gp;
}


int find_grep(GREP *grep, BASE *bp)/*usilic*/
{
  GREP *gp;

  if (!grep) return 0;
  do {
    gp = grep->prev;
    if (grep->bp == bp) return 1;
  } while ((grep = gp));
  return 0;
}

LIST *delete_name(LIST *lp, STR *sp, STR *rsp, FILE *fp)/*usilic*/
{
  char *cp, str[N_line];
  int i;

  for (i = 0; fgets(str, N_line, fp); i++) {
    if ((cp = strchr(str, '\n'))) *cp = '\0';
    delete_str(sp, str);
  }
  if (i) {
    lp = free_list(lp);
    while (rsp) { /* build-in */
      if (rsp->name) lp = save_list(lp, rsp->name, &rsp->pn);
      rsp = rsp->next;
    }
    while (sp) {
      if (sp->name) lp = save_list(lp, sp->name, &sp->pn);
      sp = sp->next;
    }
  }
  return lp;
}

static STR *save_hist(STR *hist, char name[], PN pn)
{
  if (!hist) {
    hist = calloc(sizeof(STR), 1);
    hist->name = strdup(name);
    memcpy(&hist->pn, &pn, sizeof(PN));
  } else hist->next = save_hist(hist->next, name, pn);
  return hist;
}

static int find_hist(STR *hist, char *name, int *countp)
{
  int count;

  if (!hist) count = 0;
  else for (count = 1; hist->next; hist = hist->next) count++;
  *countp = count;
  if (!hist || name == F6 || strcmp(name, hist->name)) return 0;
  return 1;
}

STR *save_history(STR *hist, char name[], int f_find)/*usilic*/
{
  static int n_history;
  static PN pn;
  int count;
  STR *sp;

  if (hist == F6) {
    sscanf(name, "%d", &n_history);
    return NULL;
  } else if (hist == F6) {
    sprintf(name, "%d", n_history);
    return NULL;
  } else if (f_find < 0) {
    find_hist(hist, F6, &count);
    sprintf(name, "%d", count);
    return NULL;
  }
  count = strlen(name) - 1;
  if (name[count] == '\r') /* '\r' SHELL-prompt */
    name[count] = '\0';
  if (f_find && find_hist(hist, name, &count)) return hist;
  if (hist && count == n_history) {
    sp = save_hist(hist->next, name, pn);
    free(hist->name);
    free(hist);
  } else sp = save_hist(hist, name, pn);
  return sp;
}

STR *free_history(STR *hist, char tmp[], int n_hist)/*usilic*/
{
  int count;
  STR *sp;

  sscanf(tmp, "%d", &count);
  if ((count -= n_hist) <= 0) return hist;
  for (; count; count--) {
    sp = hist;
    hist = hist->next;
    free(sp->name);
    free(sp);
  }
  find_hist(hist, F6, &count);
  return hist;
}

void find_list(LIST *lp, char name[], int *ip)/*usilic*/
{
  if (lp && !*ip) {
    find_list(lp->left, name, ip);
    if (!strcmp(lp->name, name)) *ip = 1;
    find_list(lp->right, name, ip);
  }
}

STR *save_str(STR *sp, char name[], PN *pnp)/*usilic*/
{
  if (!sp) {
    sp = calloc(sizeof(STR), 1);
    sp->name = strdup(name);
    memcpy(&sp->pn, pnp, sizeof(PN));
  } else if (!sp->name || strcmp(sp->name, name))
    sp->next = save_str(sp->next, name, pnp);
  return sp;
}

STACK *read_stack(STACK *sp, void *p, int f_read)/*usilic*/
{
  while (sp) {
    switch(f_read) {
    case 0:
      if (p == sp->bp) goto jump;
      break;
    case 1:
      if (!strcmp(sp->name, p)) goto jump;
      break;
    default:
      break;
    }
    sp = sp->next;
  }
jump:
  return sp;
}

STACK *load_stack(STACK *sp, LINE *lp, int *f_Mp, char *name, BASE **bpp, BASE base[])/*usilic*/
{
  STACK *s0, *sb = NULL;

  if (!sp) return sp;
  s0 = sp;
  while (sp->next) {
    sb = sp;
    sp = sp->next;
  }
  if (sp->f.bp) {
    if ((*bpp && *bpp < &base[0]) || *bpp > &base[N_base-1]) {
      free_bp(*bpp);
      free(*bpp);
    }
  }
  *bpp = sp->bp;
  if (name) strcpy(name, sp->name);
  strcpy(lp->prev, sp->prev);
  strcpy(lp->dir, sp->dir);
  strcpy(lp->old, sp->old);
  strcpy(lp->arg, sp->arg);
  strcpy(lp->cut.name, sp->cut.name);
  lp->cut.buff = sp->cut.buff;
  strcpy(lp->cut.str, sp->cut.str);
  lp->cut.chp = sp->cut.chp;
  lp->cut.bp = sp->cut.bp;
  free(sp->prev);
  free(sp->name);
  free(sp->dir);
  free(sp->old);
  free(sp->arg);
  *f_Mp = sp->f.M;
  free(sp);
  if (sb) {
    sb->next = NULL;
    return s0;
  } else return NULL;
}

STACK *save_stack(STACK *sp, LINE *lp, int f_M, char name[], BASE *bp, int f_bp)/*usilic*/
{
  if (!sp) {
    sp = calloc(sizeof(STACK), 1);
    sp->bp = bp;
    sp->prev = strdup(lp->prev);
    sp->name = strdup(name);
    sp->dir = strdup(lp->dir);
    sp->old = strdup(lp->old);
    sp->arg = strdup(lp->arg);
    strcpy(sp->cut.name, lp->cut.name);
    sp->cut.buff = lp->cut.buff;
    strcpy(sp->cut.str, lp->cut.str);
    sp->cut.chp = lp->cut.chp;
    sp->cut.bp = lp->cut.bp;
    sp->f.M = f_M;
    sp->f.bp = f_bp;
  } else sp->next = save_stack(sp->next, lp, f_M, name, bp, f_bp);
  return sp;
}

STACK *save_stack_dir(STACK *sp, char dir[], char old[], BASE *bp)/*usilic*/
{ /* save current directory (bottom of the stack) */
  char str[3];

  sprintf(str, "%c%c", K_z, 0);
  sp = calloc(sizeof(STACK), 1);
  sp->bp = bp;
  sp->prev = strdup(str);
  sp->name = strdup(dir);
  sp->dir = strdup(dir);
  sp->old = strdup(old);
  sp->arg = strdup(str);
  sp->f.M = T_dir;
  sp->f.bp = 1;
  return sp;
}

void get_name(char *pathname)/*usilic*/
{ /* set regular filename to pathname */
  char *cp, *cp0 = pathname;

  while ((cp = strchr(pathname, '/'))) pathname = &cp[1];
  if (cp0 != pathname) strcpy(cp0, pathname);
}

int get_dir(char *pathname)/*usilic*/
{ /* set current directory name to pathname */
  char *cp;

  if (!strchr(pathname, '/') || !strcmp(pathname, "/")) return 0;
  while ((cp = strchr(pathname, '/'))) pathname = &cp[1];
  *pathname = 0;
  return 1;
}

void erase_dot(char name[])/*usilic*/
{ /* rid name[] of current ("./") or parent ("../") directory name */
  int i, j;
  char *cp, tmp[N_line];

  for (cp = name; *cp && (cp = strstr(cp, "./")); )
    if (cp > name && cp[-1] == '.') { /* skip "../" */
      cp++;
      continue;
    } else if (cp[2]) memmove(cp, &cp[2], strlen(&cp[2]) + 1); /* erase "./" */
    /* usilic.old: 元は重なった範囲への strcpy()(未定義動作) */
    else *cp = '\0';
  if ((cp = strstr(name, "../"))) {
    strcpy(tmp, cp);
    *cp = '\0';
    while (strlen(tmp) && (cp = strstr(tmp, "../")))
      if (cp == tmp) {
	get_par(name);
	memmove(tmp, &cp[3], strlen(&cp[3]) + 1); /* usilic.old: 元は重なった範囲への strcpy()(未定義動作) */
      } else {
	i = strlen(name);
	j = cp - tmp;
	memcpy(&name[i], tmp, j);
	name[i + j] = '\0';
	memmove(tmp, cp, strlen(cp) + 1); /* usilic.old: 元は重なった範囲への strcpy()(未定義動作) */
      }
  } else *tmp = '\0';
  if (strlen(tmp)) strcat(name, tmp);
}

void read_list(LL *llp, SS *ssp, LINE *lp, char name[])/*usilic*/
{
  char *cp, str[N_line], str1[N_line];
  int f;
  static FILE *fp;
  static char argv0[256], s_hist[256];
  
  if (!s_S(S_hist)) return;
  signal(SIGSEGV, sub_clear);
  setjmp(Sub.jb);
  if (Sub.abort) {
    fclose(fp);
    unlink(s_S(S_hist)); /* rm ~/.usilic/history */
    *lp->dir = '\0';
    return;
  }
  strcpy(s_hist, s_S(S_hist));
  strcat(s_hist, ".old");
  if (!(fp = fopen(s_S(S_hist), "r"))) { /* ~/.usilic/history */
    if (!(fp = fopen(s_hist, "r"))) /* ~/.usilic/history.old */
      return;
  } else {
    memset(str, 0, 9);
    fseek(fp, -8, 2);
    fread(str, 1, 8, fp);
    if (!strchr(str, K_z)) { /* ~/.usilic/history doesn't contain `EOF (^Z)' */
      fclose(fp);
      rename(s_hist, s_S(S_hist));
      if (!(fp = fopen(s_S(S_hist), "r"))) return;
    } else fseek(fp, 0, 0);
  }
  if (!fgets(str, N_line, fp)) return;
  do {
    cp = fgets(str, N_line, fp);
  } while (*str == '\n' || *str == K_l || *str == '#');
  *str1 = '\0';
  sscanf(str, "%s %s %s %s %s %d %s %s"
	 , lp->old, lp->dir, lp->arg, lp->prev, name, &f, str1, argv0);
  rspc(name, NULL);
  if (*name == '/' && access(name, F_OK) < 0) {
    save_error("%s: %s: %s.\n", s_S(S_hist), name, strerror(ENOENT));
    *lp->old = '.';
    if (*str1) *str1 = '\0';
  }
  if (*lp->old == '.') { /* mode-line: ./ (ls) */
    getcwd(str, N_line);
    if (strchr(str, '\0')[-1] != '/') strcat(str, "/");
    strcpy(lp->old, str);
    strcpy(lp->dir, str);
    strcpy(name, str);
    *lp->prev = K_z;
  } else if (*argv0) lp->cut.chp = get_child(NULL, NULL, argv0, NULL, NULL);
  rspc(lp->arg, NULL);
  rspc(lp->old, NULL);
  rspc(lp->dir, NULL);
  rspc(lp->prev, NULL);
  rspc(str1, lp->cut.str);
  fgets(str, N_line, fp);
  f = 0;
  do {
    if (read_str(fp, str, &ssp->p.file, &llp->p.file, 0, M_lfile) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.dir, &llp->p.dir, 0, M_ldir) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.man, &llp->p.man, 0, M_man) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.grep, &llp->p.grep, 1, M_cut) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.cmd, &llp->p.cmd, 1, M_cmd) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.hist, NULL, 0, M_hist) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.hfile, NULL, 0, T_hfile) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.hdir, NULL, 0, T_hdir) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.hman, NULL, 0, T_hman) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.hlocate, NULL, 0, T_hlocate) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.hfgrep, NULL, 0, T_hfgrep) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.hegrep, NULL, 0, T_hegrep) < 0) 
      continue;
    if (read_str(fp, str, &ssp->p.mode, &llp->p.mode, 0, T_mode) < 0) 
      continue;
    if (*str != K_l || !is_name(&str[1], M_usilic)) continue;
    if (fgets(str, N_line, fp)) {
      int hs, ls = 1, charset = 0;
      sscanf(str, "%d %d %d", &hs, &ls, &charset);
      conf_ls(NULL, utoa(ls));
      vt_mode((hs) ? 'h' : -'h');
      if (vt_mode('H')) vt_cs(vt_row(0) - 1, 0);
      else vt_cs(vt_row(0) - 2, 0);
      vt_charset((charset) ? charset : C_euc);
    }
    f = 1;
  } while (0);
  if (!f) { /* ~/.usilic/history is broken or old format */
    save_error("%s: %s: %s.\n", s_S(S_usilic), s_S(S_hist), s_S(S_errm));
    kill(getpid(), SIGSEGV);
  }
  fclose(fp);
}

void write_list(LL ll, SS ss, LINE *lp, char name[])/*usilic*/
{
  FILE *fp;
  char str[N_line], str1[N_line];
  
  if (!s_S(S_hist)) return;
  strcpy(str, s_S(S_hist));
  strcat(str, ".old");
  rename(s_S(S_hist), str); /* backup */
  if (!(fp = fopen(s_S(S_hist), "w"))) return;
  get_time(str);
  fprintf(fp, "# ~/.usilic.old/history: %s\n", getenv("USILIC_OLD")); /* usilic.old: 元は USILIC(今は C++ 版の usilic が使う) */
  fprintf(fp, "# E\010EX\010XI\010IT\010T: %s\n", str);
  fprintf(fp, "# Note: The lines begin with the character \014 or \032 mustn't be deleted.\n\n");
  fprintf(fp, "%c Current buffer\n", K_l);
  sspc(lp->arg, NULL);
  sspc(lp->old, NULL);
  sspc(lp->dir, NULL);
  sspc(lp->prev, NULL);
  sspc(name, NULL);
  sspc(lp->cut.str, str1);
  lp->cut.chp = (*lp->cut.str) ? check_ext(name, NULL, 0) : NULL;
  fprintf(fp,"%s %s %s %s %s %d %s %s\n"
	  , lp->old, lp->dir, lp->arg, lp->prev, name, 0
	  , str1, (lp->cut.chp) ? lp->cut.chp->argv[0] : "");
  write_str(fp, ss.p.file, ll.p.file, 0, M_lfile);
  write_str(fp, ss.p.dir, ll.p.dir, 0, M_ldir);
  write_str(fp, ss.p.man, ll.p.man, 0, M_man);
  write_str(fp, ss.p.grep, NULL, 1, M_cut);
  write_str(fp, ss.p.cmd, NULL, 1, M_cmd);
  write_str(fp, ss.p.hist, NULL, 1, M_hist);
  write_str(fp, ss.p.hfile, NULL, 1, T_hfile);
  write_str(fp, ss.p.hdir, NULL, 1, T_hdir);
  write_str(fp, ss.p.hman, NULL, 1, T_hman);
  write_str(fp, ss.p.hlocate, NULL, 1, T_hlocate);
  write_str(fp, ss.p.hfgrep, NULL, 1, T_hfgrep);
  write_str(fp, ss.p.hegrep, NULL, 1, T_hegrep);
  write_str(fp, ss.p.mode, ll.p.mode, 0, T_mode);
  fprintf(fp, "%c%s\n", K_l, s_P(P_mode)[M_usilic]);
  fprintf(fp, "%d %d %d\n", vt_mode('H'), conf_ls(NULL, NULL), vt_charset(0));
  fprintf(fp, "%c\n", K_z);
  fclose(fp);
}

LIST *free_list(LIST *list)/*usilic*/
{
  if (list) {
    free_list(list->left);
    if (list->name) free(list->name);
    free_list(list->right);
    free(list);
  }  
  return NULL;
}

void free_str(STR *sp)/*usilic*/
{
  STR *sb;

  while (sp) {
    if (sp->name) free(sp->name);
    sb = sp->next;
    free(sp);
    sp = sb;
  }
}

BASE *init_str(int f_M, BASE *bp, STR s0[], char name[])/*usilic*/
{
  int f_grep = 0;

  switch(f_M) {
  case T_hfgrep:
  case T_hegrep:
    f_grep = 1;
  case T_hfile: case T_hdir:
  case M_hist: case T_hist:
  case T_hman: case T_hlocate:
  case M_key:
    free_bp(bp);
  default:
    bp->pp = alloc_str(s0, &bp->nrow, f_grep);
    if (!bp->nrow) make_bp(bp, s_S(S_err2), name, NULL);
    break;
  }
  return bp;
}

BASE *init_list(int f_M, BASE *bp, LIST *lp, char name[])/*usilic*/
{
  int f_grep = 0, f_ls = 0;

  switch(f_M) {
  case M_egrep: case M_cut:
    f_grep = 1;
  case M_lfile: case M_ldir: 
  case M_list: case T_list:
    if (!f_grep) f_ls = 1;
  case M_man: case M_prog: 
  case M_cmd: case T_cmd:
    free_bp(bp);
  default:
    if (!bp->pp) bp->pp = alloc_list(lp, &bp->nrow, f_grep, f_ls);
    if (!bp->nrow) make_bp(bp, s_S(S_err2), name, NULL);
  }
  return bp;
}

int load_list(LIST *list, char name[], PN *pn)/*usilic*/
{
  if (list) {
    if (load_list(list->left, name, pn)) return 1;
    if (!strcmp(list->name, name)) { 
      if (pn) memcpy(pn, &list->pn, sizeof(PN));
      return 1;
    }  
    if (load_list(list->right, name, pn)) return 1;
  }
  return 0;
}

LIST *save_list(LIST *list, char name[], PN *pnp)/*usilic*/
{
  int i;

  if (!list) {
    list = calloc(sizeof(LIST), 1);
    list->name = strdup(name);
    memcpy(&list->pn, pnp, sizeof(PN));
  } else if ((i = strcmp(list->name, name)) > 0)
    list->left = save_list(list->left, name, pnp);
  else if (i < 0) list->right = save_list(list->right, name, pnp);
  else memcpy(&list->pn, pnp, sizeof(PN));
  return list;
}

char *utoa(u_int u)/*usilic*/
{ /* convert unsigned-int to string */ 
  char tmp[32];
  static char str[32];
  int i;
  
  i = 0;
  do {
    tmp[i++] = u % 10 + '0';
    u /= 10;
  } while (u);
  tmp[i] = '\0';
  for (u = 0, i = strlen(tmp); i; u++, i--) str[u] = tmp[i-1];
  str[u] = '\0';
  return (char*)str;
}

char *tobo(char str[], int f_nl)/*usilic*/
{ /* convert str[] to bold attribute */
  static char tmp[N_line];
  int i, j;
  
  for (i = j = 0; str[i] && j < N_line - 4; i++, j += 3) {
    tmp[j] = str[i];
    tmp[j+1] = K_h;
    tmp[j+2] = str[i];
  }
  if (f_nl && str[i-1] != '\n') tmp[j++] = '\n';
  tmp[j] = 0;
  return tmp;
}

char *toul(char str[], int f_nl)/*usilic*/
{ /* convert str[] to underline attribute */
  static char tmp[N_line];
  int i, j;

  for (i = j = 0; str[i] && j < N_line - 4; i++, j += 3) {
    tmp[j] = '_';
    tmp[j+1] = K_h;
    tmp[j+2] = str[i];
  }
  if (f_nl && str[i-1] != '\n') tmp[j++] = '\n';
  tmp[j] = 0;
  return tmp;
}

char *tome(char str[], int f_nl)/*usilic*/
{
  static char tmp[N_line];
  int i, j;

  for (i = j = 0; str[i] && str[i] != '\n' && j < N_line - 2; i++, j++) {
    if (str[i] == K_h) i += 2;
    tmp[j] = str[i];
  }
  if (f_nl && str[i-1] != '\n') tmp[j++] = '\n';
  tmp[j] = 0;
  return tmp;
}

int ichrstr(char str0[], char c, char *str)/*usilic*/
{
  char *cp;

  if ((str && (cp = strstr(str0, str))) || (!str && (cp = strchr(str0, c))))
    return (int)(cp - str0) + 1; /* usilic.old: 元は (int)cp - (int)str0。64bit では切り詰められていた */
  else return 0;
}

void print_arg(char arg[], int size, u_char *buff, CHILD *chp, int f_M)/*usilic*/
{
  *arg = K_z; /* doesn't display mode-line[4] */
  if (chp)
    if (size >= 0 && s_S(S_sh) && f_M != T_pipe5)
      sprintf(arg, " %d (%s)", size, *chp->argv);
    else sprintf(arg, " (%s)", *chp->argv);
  else 
    switch(f_M) {
    case T_dir:
      sprintf(&arg[1], "(%s)", s_S(S_ls));
      break;
    case T_du:
      sprintf(&arg[1], "(%s)", s_S(S_du));
      break;
    case T_man:
      sprintf(&arg[1], "(%s)", s_S(S_man));
      break;
    case T_pipe4:
    case T_pipe5:
      break;
    default:
      if (f_M != M_copy && f_M != M_help && buff && size >= 0) 
	sprintf(&arg[1], "%d", size);
      else arg[1] = '\0';
      break;
    }
}  

void dup_error()/*usilic*/
{
  int fd;
  FILE *fp; /* usilic.old: 足した。下を参照 */
  
  if (!s_S(S_error)) return;
  fp = fopen(s_S(S_error), "a");
  if (!fp) return; /* usilic.old: fileno(NULL) は未定義動作。fopen() は
		       (親のディレクトリが無い等で)失敗することがあるのに、確かめていなかった */
  fd = fileno(fp);
  dup2(fd, 2);
  close(fd);
}

void save_error(char *fmt, ...)/*usilic*/
{
  FILE *fp;
  va_list ap;

  if (!s_S(S_error)) return;
  fp = fopen(s_S(S_error), "a");
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  fclose(fp);
}

void save_stderr(char *fmt, ...)/*usilic*/
{ /* print to `stderr' after reset terminal */
  FILE *fp;
  va_list ap;

  if (!s_S(S_stderr)) return;
  fp = fopen(s_S(S_stderr), "w");
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  fclose(fp);
}

void save_intr(int n_error)/*usilic*/
{
  FILE *fp;

  if (!n_error) return;
  fp = fopen(s_S(S_intr), "w");
  fprintf(fp, "%d\n", n_error);
  fclose(fp);
}

int load_intr(void)/*usilic*/
{
  FILE *fp;
  int n_error;

  if (access(s_S(S_intr), F_OK) < 0) return 0;
  fp = fopen(s_S(S_intr), "r");
  fscanf(fp, "%d", &n_error);
  fclose(fp);
  if (n_error == S_erre) unlink(s_S(S_intr));
  return n_error;
}

char *is_inst(char *path, char *prog)/*usilic*/
{ /* search `prog' on `path' */
  int i;
  char *cp;
  static char str[N_line];
  
  while (1) {
    if ((cp = strchr(path, ':'))) {
      if (!prog) return cp;
      *cp = '\0';
      strcpy(str, path);
      *cp++ = ':';
      path = cp;
    } else strcpy(str, path);
    strcat(str, "/");
    strcat(str, prog);
    if ((i = ichrstr(str, ' ', NULL))) str[i-1] = '\0';
    if (!access(str, X_OK|F_OK)) {
      if (i) str[i-1] = ' ';
      return str;
    }
    if (!cp) break;
  }  
  return NULL;
}

int is_access(char name[], BASE *bp)/*usilic*/
{
  struct stat st;

  if (access(name, R_OK|F_OK) < 0) { /* No such file or directory */
    make_bp(bp, strerror(errno), name, NULL);
    return -1;
  } else {
    stat(name, &st);
    if (!st.st_size) { /* No size */
      make_bp(bp, s_S(S_err1), name, NULL);
      return -1;
    }
  }
  return 0;
}  

int is_str(int f_M, int *n_bp)/*usilic*/
{
  int n_ll;

  do {
    switch(f_M) {
    case M_hist:
    case T_hist:
      n_ll = 6;
      break;
    case T_hfile:
      n_ll = 9;
      break;
    case T_hdir:
      n_ll = 10;
      break;
    case T_hman:
      n_ll = 11;
      break;
    case T_hlocate:
      n_ll = 12;
      break;
    case T_hfgrep:
      /* ^X^F ==> ^R (T_hfile:6) ==> ^S ==> ^R (T_hfgrep:10) */
      /* ^I (M_list:8) ==> ^S ==> ^R (T_hfgrep:10) */
      *n_bp = 11;
      n_ll = 13;
      continue;
    case T_hegrep:
      *n_bp = 11;
      n_ll = 14;
      continue;
    default:
      *n_bp = n_ll = 0;
      continue;
    }
    *n_bp = 6;
  } while (0);
  return n_ll;
}  
  
int is_list(int f_M, int *n_bp)/*usilic*/
{
  int n_ll;

  *n_bp = 0;
  switch(f_M) {
  case M_egrep:
    n_ll = 5;
    *n_bp = 3;
    break;
  case M_lfile:
    n_ll = 1;
    break;
  case M_ldir:
    n_ll = 2;
    break;
  case M_prog:
    n_ll = 3;
    break;
  case M_man:
    n_ll = 4;
    break;
  case M_cut:
    n_ll = 5;
    *n_bp = 10;
    break;
  case M_cmd:
  case T_cmd:
    *n_bp = 6;
    n_ll = 6;
    break;
  case M_list:
  case T_list:
    n_ll = 7;
    *n_bp = 8;
    /* ^R (T_hist:6) ==> ^I (T_list:8) */
    break;
  default:
    n_ll = 0;
    break;
  }
  return n_ll;
}  
  
int open_pty(char str[], int f_open)/*usilic*/
{
  char *cp, *np;
  int tfd, i;

  for (cp = "pqrs", np = NULL; *cp; cp++)
    for (np = "0123456789abcdef"; *np; np++) {
      sprintf(str, "/dev/pty%c%c", *cp, *np);
      /* usilic.old: O_NONBLOCK: macOS には /dev/ptyXX がまだあるが
	 使えず、ふつうに open() すると戻ってこないので、SHELL のキーで usilic が
	 固まっていた。待たずに試し(使えない組はすぐ失敗し、SHELL は起動できない
	 と知らせる)、使える組が見つかったら親の側を待つ読み書きに戻す。 */
      if ((tfd = open(str, O_RDWR | O_NONBLOCK)) >= 0) {
	str[5] = 't';
	if ((i = open(str, O_RDWR | O_NONBLOCK)) >= 0) { /* usilic.old: 上を参照 */
	  str[5] = 'p';
	  close(i);
	  fcntl(tfd, F_SETFL, fcntl(tfd, F_GETFL) & ~O_NONBLOCK); /* usilic.old: 上を参照 */
	  if (f_open) return tfd;
	  else close(tfd);
	  return 1;
	}
	str[5] = 'p';
	close(tfd);
      }
    }
  return -1;
}

u_char *mtoa(int f_M)/*usilic*/
{ /* exchange order of m.i */
  union {
    u_char c[4];
    int i;
  } m;
  int i, j;
  static u_char cstr[5];

  conf_key(F5, NULL, (int**)&f_M, (int**)&m.i, NULL);
  for (j = 0, i = 3; i >= 0; i--) if (m.c[i]) cstr[j++] = m.c[i];
  cstr[j] = '\0';
  return cstr;
}

void print_buff(u_char buff[], int kanji_s)/*usilic*/
{ /* display control character with reverse attribute */
  int c, i, len, kanji_e;

  len = strlen(buff);
  if ((kanji_e = count_k(&buff[kanji_s], len - kanji_s))) len--;
  if (kanji_s) {
    i = 1;
    vt_putchar(' ');
  } else i = 0;
  for (; i < len; i++)
    if ((c = buff[i]) && (c < ' ' || c == K_dl || c == K_at)) {
      if (c == K_dl) c -= 0x80;
      else if (c == K_at) c = 0;
      vt_stand();
      vt_putchar(c + '@');
      vt_endstand();
    } else vt_putchar(buff[i]);
  if (kanji_e) vt_putchar(' ');
}

void print_ctrl(int cc, u_char str[])/*usilic*/
{
  int c, i, j;
  u_char *p;

  p = alloca(strlen(str) * 2 + 1);
  for (i = j = 0; (c = str[i]); i++, j++)
    if (c < ' ' || c == K_dl) sprintf(&p[j++], "%c%c", cc, c + '@');
    else if (c == K_at) sprintf(&p[j++], "%c@", cc);
    else p[j] = c;
  p[j] = '\0';
  strcpy(str, p);
}

void unlink_file()/*usilic*/
{
  unlink(s_S(S_w));
  unlink(s_S(S_mess));
  unlink(s_S(S_enter));
  unlink(s_S(S_intr));
  if (s_S(S_pid)) unlink(s_S(S_pid));
  else if (s_S(S_error)) {
    unlink(s_S(S_error));
    unlink(s_S(S_log));
  }
}

void print_pid(int pid)/*usilic*/
{
  FILE *fp = fopen(s_S(S_pid), "w");
  fprintf(fp, "%d\n", pid);
  fclose(fp);
}

int strint(int tab[], int u)/*usilic*/
{
  int i;

  for (i = 0; tab[i]; i++) if (tab[i] == u) break;
  return tab[i];
}

int is_comm(int f_M, int f_comm)/*usilic*/
{
  switch(f_comm) {
  case 0:
    switch(f_M) {
    case M_cp:
      f_M = T_cp;
      break;
    case M_rm:
      f_M = T_rm;
      break;
    case M_mv:
      f_M = T_mv;
      break;
    case M_arc:
      f_M = T_arc;
      break;
    case T_arc2:
      f_M = T_arc3;
      break;
    default:
      f_M = 0;
      break;
    }
    break;
  case 1:
  case 2:
    switch(f_M) {
    case T_cp:
      f_M = M_cp;
      break;
    case T_rm:
      f_M = M_rm;
      break;
    case T_mv:
      f_M = M_mv;
      break;
    case T_arc:
      f_M = M_arc;
      break;
    case T_arc3:
      if (f_comm == 1) {
	f_M = T_arc2;
	break;
      }
    default:
      f_M = 0;
      break;
    }
    break;
  case 3:
    switch(f_M) {
    case M_cp2:
      f_M = M_cp;
      break;
    case M_rm2:
      f_M = M_rm;
      break;
    case M_mv2:
      f_M = M_mv;
      break;
    case M_arc2:
      f_M = M_arc;
      break;
    default:
      f_M = 0;
      break;
    }
    break;
  }
  return f_M;
}

CHILD *check_chp(int s_i)/*usilic*/
{
  CHILD *chp;

  if (!(chp = get_child(NULL, NULL, s_S(s_i), NULL, NULL))) {
    char *p = alloca(strlen(s_S(s_i)) + 5);
    sprintf(p, " `%s\047.", s_S(s_i));
    save_intr(S_errc);
    print_intr(NULL, p);
    return NULL;
  }
  return chp;
}

void print_stderr(char *err0)/*usilic*/
{
  static char err[N_line];
  int c;
  FILE *fp;

  if (err0) {
    strcpy(err, err0);
    return;
  } else if (!*err) return;
  if (access(err, F_OK) < 0) return;
  fp = fopen(err, "r");
  while ((c = fgetc(fp)) != EOF) fputc(c, stderr);
  fclose(fp);
  unlink(err);
}
 
int is_ls(int f_M)/*usilic*/
{
  switch(f_M) {
  case T_dir: case T_du: case M_du:
  case M_lfile: case M_ldir:
  case T_hfile: case T_hdir:
  case M_hist:
    return 1;
  default:
    break;
  }
  return 0;
}

char *set_help(char *cp)/*usilic*/
{
  static char *help;

  if (cp == F13) {
    if (help) free(help);
  } else if (vt_charset(0) == C_ascii) {
    if (!help || strcmp(help, s_S(S_help))) {
      if (help) free(help);
      help = strdup(s_S(S_help));
    }
  } else {
    if (!help || !strcmp(help, s_S(S_help))) {
      if (help) free(help);
      help = malloc(strlen(s_S(S_help)) + 3 + 1);
      sprintf(help, "%s.jp", s_S(S_help));
    }
  }
  return help;
}

