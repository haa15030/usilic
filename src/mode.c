#include "tcb.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "menu.h"
int echo_mess(int f_mess)/*tcb*/
{
  switch(f_mess) {
  case 0:
    break;
  case 1:
    fprintf(stdout, "\n%s%s: %s%s "
	    , s_S(S_tcb), vt_geta(A_md), s_S(S_err9), vt_geta(A_me));
    fflush(stdout);
    vt_init(2);
    read_m(F11, M_getch, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    break;
  case 2:
    vt_move(vt_row(0) - 1, 0);
    vt_printf("%s%s%s", vt_geta(A_md), s_S(S_errk), vt_geta(A_me));
    vt_flush();
    break;
  default:
    save_intr(f_mess);
    print_intr(NULL, NULL);
    return 1;
    break;
  }
  return 0;
}

void free_h(void *hp0)/*tcb*/
{
  int i;
  static HEAP *hp;

  if (hp0) {
    hp = hp0;
    return;
  }
  clear_stack(hp, NULL, NULL, NULL, 0);
  if (hp->w) free(hp->w);
  for (i = 0; i < N_base; i++) free_bp(&hp->base[i]);
  for (i = 0; i < N_ll; i++) 
    hp->ll.a[i] = (u_long)free_list((LIST*)hp->ll.a[i]);
  for (i = 0; i < N_ss; i++) {
    free_str((STR*)hp->ss.a[i]);
    hp->ss.a[i] = 0;
  }
  free_child(hp->child);
}

int skip_spc(u_char cs[], int f_spc)/*tcb*/
{
  static char mark[] = "*=|@";
  static int i_dir;
  int c, i, j;
  u_int ino;
  u_char *cp;
  struct stat st;

  if (f_spc < 0) {
    if ((cp = strstr(cs, "../"))) i_dir = cp - cs;
    return i_dir;
  } else if (f_spc) {
    if ((cp = strstr(cs, " -> "))) *cp = '\0';
    switch(f_spc) {
    case 1:
      while (1) {
	if ((cp = strchr(cs, ' ')) && cp[-1] != '\\') {
	  if (cp[1]) {
	    strcpy(cs, &cp[1]);
	    continue;
	  } else *cp = '\0';
	}
	break;
      }
      break;
    case 2:
      for (cp = &cs[i_dir]; *cp == ' '; cp++) ;
      strcpy(cs, cp);
      break;
    }
  }
  for (cp = cs; ; cp++) 
    if ((cp = strchr(cp, '\\')) && cp[1]) {
      for (i = 1, j = 0; i <= 3 && (c = cp[i]) >= '0' && c <= '9'; i++)
	j = (j << 3) + c - '0';
      if (j) {
	*cp++ = j;
	i--;
      }
      strcpy(cp, &cp[i]);
    } else break;
#ifdef SKIP_SPC
 debug("1:cs=%s\n",cs);
#endif
  for (i = 0; (c = mark[i]); i++)
    if ((cp = strchr(cs, c))) {
      if (stat(cs, &st) < 0) {
	*cp = '\0';
	break;
      }
      *cp = '\0';
      ino = st.st_ino;
      if (!stat(cs, &st) && ino == st.st_ino) break;
      *cp = c;
    }
#ifdef SKIP_SPC
 debug("2:cs=%s\n",cs);
#endif
  return 0;
}

static void init_rv(NUM *np, PN *pnp, RV *rvp, HEAP *hp, int etop)
{
  int i;
  PP *pp;

  if (np->Mb == M_du) np->Mb = np->M = T_du;
  if (pnp->top > etop) pnp->top = etop;
  else if (pnp->top < 0) pnp->top = 0;
  pp = hp->bp->pp;
  if ((rvp->row = init_row(&pp, pnp, np->tell, etop)) > vt_row(0) - 2) 
    pnp->row = rvp->row = vt_row(0) - 2;
  if ((rvp->ps  /* beginning row of window */
       = rvp->pe = rvp->pp = pp)) {
    for (i = 0; i < vt_row(0) - 2 && rvp->pe->next; i++)
      rvp->pe = rvp->pe->next; /* end row of window */
    for (i = 0; i < pnp->row && rvp->pp->next; i++) 
      rvp->pp = rvp->pp->next; /* current row */
  }
  if (np->M == T_dir && hp->bp->size >= 0) 
    for (pp = hp->bp->pp; pp && !strstr(pp->cs, "../"); pp = pp->next) ;
  else pp = hp->bp->pp;
  rvp->col = init_col(np->M, pp->cs, hp->bp->size);
  if (hp->bp->pp->tell != (u_int)F7 /* F7: "No size" etc. */
      && rvp->pp && rvp->col >= 0) { /* reverse attr. */
    strcpy(rvp->str, &rvp->pp->cs[rvp->col]);
  } else strcpy(rvp->str, "");
}

void set_rv(NUM *np, PN *pnp, RV *rvp, HEAP *hp, int etop)/*mode*/
{
  PP *pp;

  init_rv(np, pnp, rvp, hp, etop);
  if (np->tell) np->tell = 0;
  else if (pnp->line == -2) ;
  else if (pnp->line > 0) {
    if (pnp->line > rvp->pp->f.line)
      for (pp = rvp->pp; pnp->line > pp->f.line && pp->next; pp = pp->next)
	if (pnp->row < vt_row(0) - 2) pnp->row++;
	else pnp->top++;
    else if (pnp->line < rvp->pp->f.line) {
      for (pp = rvp->pp; pnp->line <= pp->f.line && pp->prev; pp = pp->prev)
	if (pnp->row) pnp->row--;
	else pnp->top--;
      if (!pnp->row) pnp->top++;
      else pnp->row++;
    }
    if (pnp->line != rvp->pp->f.line) init_rv(np, pnp, rvp, hp, etop);
  }
  pnp->line = rvp->pp->f.line;
}

int load_ll(CHILD *chp, ARRAY *ap, u_char prev[], CUT *cutp, HEAP *hp, PN *pnp, int f_M, FILE **pinp)/*mode*/
{
  int i, skip;
  PN pn;
  LIST **lp;
  char **mp;

  mp = s_P(P_mode);
  lp = NULL;
  skip = 0;
  if (*ap->name != ' ') {
    strcpy(ap->full, ap->name);
    cat_child(chp, ap->full);
  }
  switch(f_M) {
  case M_du:
    skip = 1; /* display from the beginning of buffer */
  case T_du:
    /* set `lp' and `full' for saving PN of N_num save_ll() */
    lp = &hp->ll.p.mode;
    strcpy(ap->full, ap->du);
    break;
  case T_pipe2:
  case T_pipe:
    skip = 1;
    strcpy(ap->full, mp[T_pipe2]);
  case T_pipe4:
    if (*ap->name == ' ') {
      strcpy(ap->full, mp[T_pipe2]);
      lp = &hp->ll.p.mode;
    } else lp = &hp->ll.p.man;
    break;
  case T_pipe3:
    skip = 1;
  case T_pipe5:
    strcat(ap->full, mp[T_pipe2]);
    lp = &hp->ll.p.mode;
    break;
  case M_cmd:
  case T_cmd:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_cmd]);
    break;
  case M_man:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_man]);
    break;
  case T_man:
    lp = &hp->ll.p.man;
    break;
  case M_cut:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_cut]);
    break;
  case T_cut:
    if (hp->bp == &hp->base[7]) lp = &hp->ll.p.mode;
    else {
      if ((chp = check_ext(prev, NULL, 0))
	  && (is_arg(chp->argv[0], S_tar)
	      || is_arg(chp->argv[0], S_lha)
	      || is_arg(chp->argv[0], S_unzip)
	      || is_arg(chp->argv[0], S_rpm)
	      || is_arg(chp->argv[0], S_deb))) lp = &hp->ll.p.mode;
      else {
	if (*ap->name != '/') {
	  sprintf(ap->full, "%s %s", s_P(P_mode)[M_cut]
		  , &ap->name[*ap->name == ' ']);
	  lp = &hp->ll.p.mode;
	} else {
	  if ((cutp->chp = check_ext(ap->name, NULL, 0)))
	    lp = &hp->ll.p.mode;
	  else lp = &hp->ll.p.file;
	  if (*cutp->str) cutp->chp = NULL;
	}
	if (!*cutp->name && lp != &hp->ll.p.mode) lp = &hp->ll.p.mode;
	if (lp == &hp->ll.p.mode) strcat(ap->full, prev);
	sprintf(strchr(ap->full, '\0'), " \042%s\042", cutp->str);
      }
    }
    chp = NULL;
    break;
  case T_locate:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_locate]);
    break;
  case M_egrep:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_egrep]);
    break;
  case T_grep:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_egrep]);
    strcat(ap->full, prev);
    strcat(ap->full, ap->gstr);
    break;
  case M_rm:
  case T_rm:
    lp = &hp->ll.p.mode;
    if (is_prev(prev, M_du)) strcpy(ap->full, ap->du);
    else if (is_prev(prev, M_lfile)) strcpy(ap->full, mp[M_lfile]);
    else if (is_prev(prev, M_ldir)) strcpy(ap->full, mp[M_ldir]);
    else if (is_prev(prev, M_man)) strcpy(ap->full, mp[M_man]);
    else if (is_prev(prev, M_prog)) strcpy(ap->full, mp[M_prog]);
    else if (is_prev(prev, M_egrep)) strcpy(ap->full, mp[M_egrep]);
    else if (is_prev(prev, M_cmd)) strcpy(ap->full, mp[M_cmd]);
    else {
      lp = &hp->ll.p.dir;
      strcpy(ap->full, prev);
    }
    break;
  case T_arc2:
  case T_arc3:
    lp = &hp->ll.p.file;
    break;
  case M_cp:
  case M_mv:
  case M_arc:
  case T_cp:
  case T_mv:
  case T_arc:
    if (is_prev(prev, M_du)) {
      strcpy(ap->full, ap->du);
      lp = &hp->ll.p.mode;
    } else {
      strcpy(ap->full, prev);
      lp = &hp->ll.p.dir;
    }
    break;
  case T_dir:
    lp = &hp->ll.p.dir;
    break;
  case T_file:
  case T_file2:
  case T_exec:
  case T_bin:
  case T_gz:
  case T_roff:
  case T_rpm:
  case T_deb:
  case T_unzip:
  case T_lha:
  case T_tar:
  case T_type:
    lp = &hp->ll.p.file;
    break;
  case T_split:
    sprintf(ap->full, "%s.%d", prev, pnp->nrow);
  case T_buff:
    lp = &hp->ll.p.split;
    break;
  case T_split2:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, s_S(S_tcb));
    break;
  case M_prog:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_prog]);
    break;
  case M_key:
  case T_key:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_key]);
    break;
  case M_error:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_error]);
    break;
  case M_log:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_log]);
    break;
  case M_lfile:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_lfile]);
    break;
  case M_ldir:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_ldir]);
    break;
  default:
    lp = NULL;
    *ap->full = 0;
    break;
  }  
  if (lp) {
    memcpy(&pn, pnp, sizeof(PN));
    if (skip || !load_list(*lp, ap->full, pnp))
      pnp->row = pnp->top = pnp->line = 0;
    else if (pnp->nrow < 0) { /* (1) */
      pnp->row = pnp->top = pnp->line = 0;
      pnp->nrow = pn.nrow;
    } else {
      if (*pinp && *pinp != F1 && hp->bp == &hp->base[4]
	  && (i = pnp->top) > 0)
	while (i-- && *pinp) 
	  alloc_pipe(0, pinp, &pn.nrow, NULL, 0);
      if (pnp->nrow != pn.nrow) pnp->nrow = pn.nrow;
    }
    if (pnp->row && pnp->row > pnp->nrow - 1) /* cursor line > total line */
      if ((pnp->row = pnp->nrow - 1) < 0) /* total line == 0 */
	pnp->row = 0;
    i = 0;
  } else {
    i = -1;
    switch(f_M) {
    case M_hist:
    case T_hist:
    case T_hfile: 
    case T_hdir: 
    case T_hman: 
    case T_hlocate:
    case T_hfgrep:
    case T_hegrep:
      pnp->line = -2;
      if ((pnp->row = hp->bp->nrow - 1) < 0) pnp->row = 0;
      if ((pnp->top = hp->bp->nrow - vt_row(0) + 1) < 0) pnp->top = 0;
      break;
    case M_fgrep:
    case M_locate:
    case M_man2:
    case M_cut2:
    case M_prog2:
    case M_lfile2:
    case M_ldir2:
    case M_exit:
    case M_copy:
      i = -2;
      break;
    default:
      pnp->row = pnp->top = pnp->line = 0;
      break;
    }
  }
  return (lp) ? (((u_int)lp - (u_int)&hp->ll.a[0]) / sizeof(u_int)) : i;
}

int mode(HEAP *hp, NUM *np, ARRAY *ap, LINE *lp, RV *rvp)/*mode*/
{
  struct stat st, l_st;
  int i, f_M, m;
  char *cp;
  u_char *p;
  
  m = 0;
  switch(f_M = np->Mb) {
  case M_egrep:
  case M_fgrep:
    hp->grep = save_grep(hp->grep);
    hp->sp = load_stack(hp->sp, &(hp->grep->l), &(hp->grep->f_M)
		       , hp->grep->name, &(hp->grep->bp), hp->base);
    hp->bp = hp->grep->bp;
    hp->sp = save_stack(hp->sp, &hp->grep->l, hp->grep->f_M
		       , hp->grep->name, hp->grep->bp, 1);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    *lp->arg = K_z;
    strcpy(ap->name, s_P(P_mode)[f_M]);
    np->M = T_grep;
    break;
  case T_grep:
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    p = alloca(strlen(ap->name) + 1);
    strcpy(p, ap->name);
    memcpy(lp, &(hp->grep->l), sizeof(LINE));
    np->M = hp->grep->f_M;
    hp->bp = hp->grep->bp;
    strcpy(ap->name, hp->grep->name);
    print_ctrl('^', rvp->str);
    sprintf(lp->prev, "%s: %s", &p[1], rvp->str);
    np->Mb = T_skip;
    break;
  case T_dir:
    p = alloca(strlen(ap->name) + strlen(rvp->pp->cs));
    strcpy(p, ap->name);
    if (!is_prev(lp->prev, M_du) 
	&& !is_prev(lp->prev, M_egrep) && !is_prev(lp->prev, M_fgrep))
      clear_stack(hp, lp, NULL, &f_M, 0);
    else 
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if (strcmp(p, ap->name)) strcpy(ap->name, p);
    strcpy(p, rvp->pp->cs);
#ifdef SKIP_SPC
 debug("T_dir:%s\n", p);
#endif
    skip_spc(p, 2);
    errno = 0;
    if (strcmp(lp->dir, ap->name)) {
      strcpy(lp->old, lp->dir);
      strcpy(lp->dir, ap->name);
    }
    chdir(ap->name);
    lstat(p, &l_st);
    if (access(p, R_OK) < 0) {
      save_error("%s: %s%s: %s.\n", s_S(S_tcb), lp->dir, p, strerror(errno));
      save_intr(-errno);
      f_M = T_dir;
      if (S_ISLNK(l_st.st_mode)) m = -M_clear;
    } else {
      if ((i = !strcmp(p, "../")) || !strcmp(p, "./")) {
	if (i && strcmp(ap->name, "/")) {
	  for (cp = &strchr(ap->name, '\0')[-2]; *cp != '/'; cp--) ;
	  cp[1] = '\0';
	}
	f_M = T_dir;
      } else {
	stat(p, &st);
	if ((f_M = (S_ISDIR(st.st_mode)) ? T_dir : T_file) == T_dir) {
	  strcat(ap->name, p);
	  if (S_ISLNK(l_st.st_mode) && strchr(ap->name, '\0')[-1] != '/') 
	    strcat(ap->name, "/"); /* symlnk */
	}
      }
      if (f_M == T_dir && *lp->prev != K_z) 
	clear_stack(hp, lp, NULL, NULL, 0);
    }
    if (f_M == T_file) {
      hp->sp = save_stack(hp->sp, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, lp->dir);
      strcpy(ap->name, lp->dir);
      strcat(ap->name, p);
    } else {
      if (strcmp(ap->name, lp->dir)) {
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->name);
      }
      chdir(ap->name);
    }
    np->M = f_M;
    break;
  case T_cut:
    strcpy(lp->prev, rvp->pp->cs);
    *lp->arg = K_z;
    np->M = T_split;
    break;
  case T_lha:
  case T_tar:
  case T_unzip:
  case T_rpm:
  case T_deb:
    strcpy(lp->prev, ap->name);
    p = alloca(strlen(rvp->pp->cs) + 1);
    strcpy(p, rvp->pp->cs);
#ifdef SKIP_SPC
 debug("T_lha..:%s\n", p);
#endif
    skip_spc(p, 1);
    strcpy(ap->name, p);
    np->M = T_buff;
    break;
  case T_du:
    strcpy(ap->name, rvp->str);
    if ((cp = strstr(rvp->pp->cs, "./"))) {
      p = alloca(strlen(&cp[2]) + 1);
      strcpy(p, &cp[2]);
      strcat(ap->name, p);
    } else chdir(ap->name);
    stat(ap->name, &st);
    np->M = (S_ISDIR(st.st_mode)) ? T_dir : T_file;
    strcpy(lp->old, lp->dir);
    if (np->M == T_dir) {
      if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
      strcpy(lp->dir, ap->name);
    } else {
      strcpy(lp->dir, ap->name);
      get_dir(lp->dir);
    }
    sprintf(lp->prev, "%s: %s", &s_P(P_mode)[M_du][1], lp->dir);
    break;
  case M_prog2:
  case M_prog:
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if (m == M_prog && *ap->name == ' ')
      save_error("%s: %s: %s.\n", s_S(S_tcb), ap->name, s_S(S_err3));
    else {
      for (cp = rvp->str; *cp == ' ' || *cp == '\t'; cp++) ;
      p = alloca(strlen(cp) + 1);
      strcpy(p, cp);
      if ((cp = strchr(p, ' '))) *cp = '\0';
      if (!find_prog(hp->ll.p.prog, p)) {
	strcpy(ap->mess, rvp->str);
	m = -M_shell;
      } else {
	if ((i = stat(lp->prev, &st)) < 0) i = 0;
	else i = st.st_ctime;
	do_prog(rvp->str, &hp->ss.p.cmd, &hp->ss.p.hist);
	if (i) stat(lp->prev, &st);
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	i = (!i || (i && i == st.st_ctime));
	if (!i) {
	  free_bp(&hp->base[1]);
	  m = -T_prog;
	  if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))
	    clear_stack(hp, lp, ap->name, &np->M, 'g');
	  if ((hp->bp && hp->bp < &hp->base[0]) 
	      || hp->bp > &hp->base[N_base-1]) {
	    free_bp(hp->bp);
	    free(hp->bp);
	  }
	} else m = -T_skip;
      }
    }
    break;
  case M_ldir:
  case M_lfile:
  case M_ldir2:
  case M_lfile2:
  case T_locate:
    strcpy(ap->name, lp->dir);
    clear_stack(hp, lp, ap->name, &f_M, 0);
    strcpy(lp->dir, ap->name);
    strcpy(ap->name, ((cp = strstr(rvp->str, "//"))) 
	   ? &cp[1] : (char*)rvp->str);
    if ((*(cp = ap->name) == '~' && !cp[1]) || (cp = strstr(ap->name, "~/"))) {
      p = alloca(strlen(s_S(S_home)) + strlen(&cp[1]) + 1);
      sprintf(p, "%s%s", s_S(S_home), &cp[1]);
      strcpy(cp, p);
    }
    while (*(cp = &strchr(ap->name, '\0')[-1]) == ' ' || *cp == '\t') 
      *cp = '\0';
    if ((i = (!stat(ap->name, &st) && S_ISDIR(st.st_mode))))
      if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
    erase_dot(ap->name);
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, ap->name);
    get_dir(lp->dir);
    if (i) {
      strcpy(lp->dir, ap->name);
      chdir(ap->name);
      *lp->prev = K_z;
      np->M = T_dir;
    } else {
      strcpy(lp->dir, ap->name);
      get_dir(lp->dir);
      chdir(lp->dir);
      *lp->prev = K_z;
      hp->sp = save_stack(hp->sp, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, lp->dir);
      np->M = (strstr(rvp->str, " (")) ? T_file2 : T_file;
    }
    break;
  }
  return m;
}  

void clear_stack(HEAP *hp, LINE *lp, char *name, int *f_Mp, int f_clear)/*mode*/
{
  int f_M;
  LINE l;
  STACK *sp;
  GREP *grep;
  BASE *bp;

  if (!lp) lp = &l;
  f_M = (f_Mp) ? *f_Mp : T_dir;
  grep = hp->grep;
  sp = hp->sp;
  bp = hp->bp;
  while (1) {
    switch(f_clear) {
    case 0:
      if (!sp) goto jump;
      break;
    case K_z:
      if (*lp->prev == K_z) goto jump;
      break;
    case '/':
      if ((*name != ' ' || !strcmp(name, " ") || is_name(name, T_pipe2)))
	goto jump;
      else if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep)) {
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	grep = load_grep(grep);
      } 
      break;
    case 'p':
    case 'P':
      if (bp == &hp->base[4]
	&& (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))) {
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	grep = load_grep(grep);
      }
      if (bp == &hp->base[7] || !strcmp(name, s_S(S_help))) ;
      else if (!is_name(name, M_help))
	if (!read_stack(sp, &hp->base[4], 0)
	    && ((f_clear == 'p' && bp != &hp->base[4] && *name != ' ')
		|| (f_clear == 'P' && *name != ' ')))
	  goto jump;
      break;
    case 'h':
      if (!(!strcmp(name, s_S(S_help)) && bp == &hp->base[7])
	  && !read_stack(hp->sp, &hp->base[7], 0)) goto jump;
    case 'g':
      if (strchr(lp->prev, ':') /* skip RM at EGREP (menu.c(4)) */
	  && (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))) {
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	grep = load_grep(grep);
      }
      if (f_clear == 'g') goto jump;
      break;
    }
    sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
  }
 jump:
  hp->sp = sp;
  hp->bp = bp;
  if (!f_clear || f_clear == K_z) while ((grep = load_grep(grep))) ;
  hp->grep = grep;
  if (f_Mp) *f_Mp = f_M;
}
  
char *set_cut(HEAP *hp, CUT *cutp, u_char mess[], char name[], char dir[], char old[])/*mode*/
{
  int c;
  char *cp, *tmp;

  if ((cp = strchr(mess, '\042'))) {
    c = cp[-1];
    cp[-1] = '\0';
    *strchr(&cp[1], '\042') = '\0';
    strcpy(cutp->str, &cp[1]);
    cutp->chp = NULL;
    strcpy(cutp->name, mess);
    clear_stack(hp, NULL, NULL, NULL, 0);
    strcpy(name, mess);
    cp[-1] = c;
    tmp = alloca(strlen(name) + 2);
    strcpy(tmp, name);
    get_dir(tmp);
    if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
    if (strcmp(dir, tmp)) {
      strcpy(old, dir);
      strcpy(dir, tmp);
      chdir(dir);
      free_bp(&hp->base[1]);
    }
  }
  return cp;
}

void check_dir(HEAP *hp, LINE *lp, char *dir, LIST **dirp)/*mode*/
{
  PN pn;

  if (hp) clear_stack(hp, lp, NULL, NULL, 0);
  if (dir && strcmp(lp->dir, dir)) { /* exchange current directory name */
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, dir);
    chdir(lp->dir);
  }
  if (!load_list(*dirp, lp->dir, &pn)) {
    /* save current directory name */
    pn.nrow = -1; /* ==> (1) */
    *dirp = save_list(*dirp, lp->dir, &pn);
  }
}

int fmark(int f_M)/*tcb*/
{
  int f_mark;

  if (is_comm(f_M, 0)) f_mark = f_M;
  else if (!(f_mark = is_comm(f_M, 1))) f_mark = f_M == M_exit;
  return f_mark;
}

int nmark(PP *pp, int f_M)/*tcb*/
{
  int n_mark, f_mark;

  for (n_mark = 0, f_mark = fmark(f_M); pp; pp = pp->next)
    if (pp->split == -f_mark) n_mark++;
  return n_mark;
}

int send_pp(PP *pp, char fmode[])/*tcb*/
{
  FILE *fp;
  int i, row;
  u_char *cs;

  fp = fopen((!strcmp(fmode, "a")) ? s_S(S_write) : s_S(S_w), fmode);
  for (row = 0; pp; pp = pp->next) {
    for (i = pp->f.len, cs = pp->cs; i; i--, cs++) fputc(*cs, fp);
    if (!pp->f.code && pp->f.nl) {
      fputc('\n', fp);
      row++;
    }
  }
  fclose(fp);
  return row;
}

int do_rm(LL *llp, SS *ssp, u_char prev[], FILE *fp)/*tcb*/
{
  int m;

  if (is_prev(prev, M_lfile)) {
    llp->p.file = delete_name(llp->p.file, ssp->p.file, NULL, fp);
    m = -M_lfile;
  } else if (is_prev(prev, M_ldir)) {
    llp->p.dir = delete_name(llp->p.dir, ssp->p.dir, NULL, fp);
    m = -M_ldir;
  } else if (is_prev(prev, M_man)) {
    llp->p.man = delete_name(llp->p.man, ssp->p.man, NULL, fp);
    m = -M_man;
  } else if (is_prev(prev, M_prog)) {
    llp->p.prog = delete_name(llp->p.prog, ssp->p.prog, ssp->p.rprog, fp);
    m = -M_prog;
  } else if (is_prev(prev, M_egrep)) {
    llp->p.grep = delete_name(llp->p.grep, ssp->p.grep, ssp->p.rgrep, fp);
    m = -M_egrep;
  } else if (is_prev(prev, M_cmd)) {
    llp->p.cmd = delete_name(llp->p.cmd, ssp->p.cmd, NULL, fp);
    m = -T_cmd2;
  } else m = 0;
  return m;
}

int get_hup(int pid0)/*tcb*/
{ /* kill child process after exit of tcb */
  static int pid;

  if (pid0 > 0) pid = pid0;
  else if (pid0 < 0) pid = 0;
  return pid;
}

static void mode_cont(int dummy)
{
  signal(SIGCONT, mode_cont);
  if (s_S(S_pid)) print_pid(getpid());
}

void do_prog(char *str, STR **cmdp, STR **histp)/*tcb*/
{
  int pid;
  void (*cont)(int);
  static PN pn;

  *cmdp = save_str(*cmdp, str, &pn);
  *histp = save_history(*histp, str, 1);
  cont = signal(SIGCONT, mode_cont);
  vt_clear();
  vt_init(-2);
#if 0
  signal(SIGCHLD, SIG_DFL);
#endif
  if (!(pid = fork())) {
    load_signal();
    unsetenv("TCB"); /* enable to launch `tcb' from PROGRAM-mode */
    execlp("sh", "sh", "-c", str, NULL);
  }
  fprintf(stdout, "\n%s: %s\n", s_S(S_tcb), str);
  get_hup(pid);
#if 0
  wait(NULL);
#else
  while (!kill(pid, 0)) usleep(100000);
#endif
  get_hup(-1);
#if 0
  signal(SIGCHLD, sig_child);
#endif
  signal(SIGCONT, cont);
  echo_mess(1);
}

void save_ll(int n_ll, LL *llp, SS *ssp, char full[], PN *pnp)/*tcb*/
{
  int f_cut = 0;

  do {
    switch(n_ll) {
    case 0:
      do {
	if (*full == ' ') {
	  if (is_name(full, M_fgrep)
	      || is_name(full, T_pipe2)
	      || is_name(full, M_du)
	      || is_name(full, M_error)
	      || is_name(full, M_log)) continue;
	  if (is_name(full, M_egrep) 
	      && strlen(full) != strlen(s_P(P_mode)[M_egrep])) continue;
	  if (is_name(full, M_cut) && strchr(full, '\042')) continue;
	} else if (!is_name(full, M_help)) continue;
	ssp->p.mode = save_str(ssp->p.mode, full, pnp);
      } while (0);
      break;
    case 1:
      if (*full != '/' || strchr(full, '\0')[-1] == '/') continue; 
      ssp->p.file = save_str(ssp->p.file, full, pnp);
      ssp->p.hfile = save_history(ssp->p.hfile, full, 1);
      break;
    case 2:
      if (*full != '/' || strchr(full, '\0')[-1] != '/') continue;
      ssp->p.dir = save_str(ssp->p.dir, full, pnp);
      ssp->p.hdir = save_history(ssp->p.hdir, full, 1);
      break;
    case 3:
      if (*full == '/' || *full == ' ') continue;
      ssp->p.prog = save_str(ssp->p.prog, full, pnp);
      break;
    case 4:
      if (*full == '/' || *full == ' ') continue;
      ssp->p.man = save_str(ssp->p.man, full, pnp);
      ssp->p.hman = save_history(ssp->p.hman, full, 1);
      break;
    default:
      if (n_ll < 0) continue;
      break;
    }
    f_cut = 1;
    llp->a[n_ll] = (u_long)save_list((LIST*)llp->a[n_ll], full, pnp);
  } while (0);
  if (is_name(full, M_cut) && !f_cut) 
    llp->a[0] = (u_long)save_list((LIST*)llp->a[0], full, pnp);
}

int do_edit(int row, char name[], char arg[], int f_M)/*mode*/
{
  int pid;
  void (*cont)(int);
  CHILD *chp;
  struct stat st, st_file;

  switch(f_M) {
  case T_tar:
  case T_lha:
  case T_unzip:
  case T_rpm:
  case T_deb:
  case T_buff:
  case T_cut:
  case T_split:
    return echo_mess(S_errv);
    break;
  default:
    if (strchr(arg, '(') 
	|| *name == ' '
	|| ((is_fm()) && *name != '/'))
      return echo_mess(S_errv);
    else if (!(chp = get_child(NULL, s_P(P_mode)[M_edit], NULL, name, NULL)))
      return echo_mess(-ENOENT);
    break;
  }
  vt_init(-2);
  sprintf(chp->argv[1], "+%d", row);
  stat(name, &st_file);
  cont = signal(SIGCONT, mode_cont);
#if 0
  signal(SIGCHLD, SIG_DFL);
#endif
  if (!(pid = fork())) {
    load_signal();
    execvp(*chp->argv, chp->argv);
  }
  get_hup(pid);
#if 0
  wait(NULL);
#else
  while (!kill(pid, 0)) usleep(100000);
#endif
  get_hup(-1);
#if 0
  signal(SIGCHLD, sig_child);
#endif
  signal(SIGCONT, cont);
  vt_init(2);
  stat(name, &st);
  return (st.st_ctime == st_file.st_ctime) ? -M_clear : 0;
}

void do_exec(char name[], CHILD *chp)/*tcb*/
{
  int i, j, pid;
  void (*cont)(int);
  char **argv, *arg;

  i = j = 0;
  j = strlen(chp->argv[i++]) + 1;
  while (i < chp->f.argc) j += strlen(chp->argv[i++]) + 1;
  j += strlen(name) + 1 + 2;
  arg = alloca(j);
  i = 0;
  sprintf(arg, "%s ", chp->argv[i++]);
  while (i < chp->f.argc) sprintf(strchr(arg, '\0'), "%s ", chp->argv[i++]);
  sprintf(strchr(arg, '\0'), "\047%s\047", name);
  vt_clear();
  vt_init(-2);
  cont = signal(SIGCONT, mode_cont);
  if (!(argv = alloc_arg(arg, NULL)))
    save_error("%s: %s: %s.\n", s_S(S_tcb), arg, strerror(ENOENT));
  else {
#if 0
    signal(SIGCHLD, SIG_DFL);
#endif
    if (!(pid = fork())) {
      load_signal();
      execvp(*argv, argv);
    }
    fprintf(stdout, "\n%s: %s\n", s_S(S_tcb), arg);
    get_hup(pid);
#if 0
    wait(NULL);
#else
    while (!kill(pid, 0)) usleep(100000);
#endif
    get_hup(0);
#if 0
    signal(SIGCHLD, sig_child);
#endif
  }
  signal(SIGCONT, cont);
  echo_mess(1);
  free_arg(argv);
}

void clear_base(BASE *base)/*tcb*/
{
  if (base->buff) {
    free(base->buff);
    base->buff = NULL;
  }
  base->size = 0;
}

PP *free_pp(PP *pp)/*tcb*/
{
  PP *p0;

  do {
    if (pp->f.str) free(pp->cs);
    if (pp->attr) free(pp->attr);
    p0 = pp;
    pp = pp->next;
    free(p0);
  } while (pp);
  return pp;
}

static int print_arc(char *cp, char dir[], FILE *fp)
{
  CHILD *chp, *chp2;
  char *p;
  int i;

#ifdef SKIP_SPC
 debug("print_arc:%s\n", cp);
#endif
  skip_spc(cp, 1);
  if (!(chp = check_ext(cp, NULL, 0))) return 0;
  do {
    switch(is_chp(chp, NULL)) {
    case 1:
    case 2:
      return 0;
      continue;
    case 3:
      fprintf(fp, "%s %s ", chp->argv[0], s_S(S_arc3));
      break;
    case 4:
      fprintf(fp, "%s %s ", chp->argv[0], s_S(S_arc2));
      break;
    case 5:
      fprintf(fp, "%s %s ", chp->argv[0], s_S(S_arc1));
      break;
    case 7:
    case 6:
      chp2 = (CHILD*)chp->argv[chp->f.argc + 1];
      p = alloca(1 + strlen(dir) + strlen(cp) + 1 + 1);
      sprintf(p, "\047%s%s\047", dir, cp);
      *cp = '\0';
      chp2->argv[chp2->f.argc] = p;
      for (i = 0; chp2->argv[i]; i++) fprintf(fp, "%s ", chp2->argv[i]);
      fprintf(fp, "| %s %s %s ", chp->argv[0], s_S(S_arc1), chp->argv[2]);
      return 2;
    case 8:
      fprintf(fp, "%s \047%s%s\047 | cpio -%s "
	      , s_S(S_rpm2cpio), dir, cp, s_S(S_arc4));
      return 3;
      break;
    case 9:
      chp = get_child(NULL, NULL, s_S(S_gzip), "-f", NULL);
      chp2 = get_child(NULL, ".tar", s_S(S_tar), "-", NULL);
      fprintf(fp, "ar %s \047%s%s\047 data.tar.gz | %s %s %s | %s %s %s "
	      , s_S(S_arc5), dir, cp
	      , chp->argv[0], chp->argv[1], chp->argv[2]
	      , chp2->argv[0], s_S(S_arc1), chp2->argv[2]);
      return 3;
      break;
    }
    fprintf(fp, "\047%s", dir);
  } while (0);
  return 1;
}

char *command(int f_Mb, u_char str[], BASE *bp, LINE *lp, int klen, u_char du[], int *fdp)/*tcb*/
{
  PP *pp;
  char *rm;
  u_char *p, *cp, *dir;
  u_char tmp[N_char], tmp2[N_char];
  int f_M, i, pv[2], pid, f_sh, f_du;
  struct stat st;
  FILE *fp;

  if (*str != '/') {
    p = alloca(strlen(lp->dir) + strlen(str) + 1);
    strcpy(p, lp->dir);
    strcat(p, str);
    strcpy(str, p);
  }
  f_sh = (f_du = is_prev(lp->prev, M_du)) || *lp->prev == '/';
  if (f_Mb == T_arc || f_Mb == T_arc3) {
    f_du = 0;
    cp = str;
  } else cp = (f_du) ? du : lp->dir;
  dir = alloca(strlen(cp) + 1);
  strcpy(dir, cp);
  if (f_sh) {
    rm = malloc(strlen(s_S(S_path)) + 2);
    strcpy(rm, is_inst(s_S(S_path), s_S(S_RM)));
  } else rm = NULL;
  pipe(pv);
  if (!(pid = fork())) {
    load_signal();
    close(pv[0]);
    fp = fdopen(pv[1], "w");
    if (f_sh) {
      fprintf(fp, "cd \047%s\047\n", dir);
      fprintf(fp, "if ! ");
      p = alloca(strlen(s_S(S_path)) + 2);
      do {
	switch(f_Mb) {
	case T_cp:
	  strcpy(p, is_inst(s_S(S_path), s_S(S_CP)));
	  break;
	case T_mv:
	  strcpy(p, is_inst(s_S(S_path), s_S(S_MV)));
	  break;
	case T_rm:
	  strcpy(p, rm);
	  break;
	default:
	  continue;
	}
	fprintf(fp, "%s ", p);
      } while (0);
    }
    if (f_Mb == T_arc3) {
      cp = &strchr(str, '\0')[1];
      if (*cp != '/') {
	memmove(&cp[strlen(lp->dir)], cp, strlen(cp) + 1);
	memcpy(cp, lp->dir, strlen(lp->dir));
      }
      if ((i = print_arc(cp, "", fp)) != 3 && i != 2) 
	fprintf(fp, "%s\047 ", cp);
    }
    f_M = is_comm(f_Mb, 1);
    *tmp2 = '\0';
    for (pp = bp->pp; pp; pp = pp->next) {
      strcpy(tmp, pp->cs);
      if (pp->split == -f_M) {
	if (f_sh) {
	  if (f_du) {
	    if (!(cp = strchr(tmp, '/'))) continue;
	    cp++;
	  } else if (f_Mb == T_arc3 
		     && strchr(tmp, '\0')[-1] == '/'
		     && !strstr(tmp, " -> ")) {
	    if (*tmp2) {
	      cp = &tmp[get_col(tmp)];
	      while (cp[-1] != ' ') cp--;
	      if (strncmp(cp, tmp2, strlen(tmp2))) fprintf(fp, "%s ", tmp2);
	    } else cp = NULL;
	    if (!cp) {
	      cp = &tmp[get_col(tmp)];
	      while (cp[-1] != ' ') cp--;
	    }
	    strcpy(tmp2, cp);
	    continue;
	  } else {
	    if (*tmp2) *tmp2 = '\0';
	    if ((cp = strchr(tmp, '\t'))) cp++; /* for ARC on DU */
	    else cp = tmp;
#ifdef SKIP_SPC
 debug("!T_arc3=%s\n", cp);
#endif
	    skip_spc(cp, (f_Mb == T_arc3) ? 1 : 2);
	  }
	} else cp = tmp;
	i = 0;
	if (f_sh && f_Mb == T_arc)
	  if (!(i = print_arc(cp, lp->dir, fp)) || i == 3) {
	    if (!i) fprintf(fp, "# %s.", s_S(S_errc));
	    continue;
	  }
	if (f_sh) {
	  if (*cp == '/') cp++;
	  if (i != 1 && (i = *cp)) fputc('\047', fp);
	  while (*cp) fputc(*cp++, fp);
	  if (i) fputc('\047', fp);
	  fputc(' ', fp);
	} else {
	  for (; *cp; cp++) fputc(*cp, fp);
	  fputc('\n', fp);
	}
      }
    }
    if (*tmp2) fprintf(fp, "%s ", tmp2);
    if (f_sh) {
      if (f_Mb != T_rm && f_Mb != T_arc && f_Mb != T_arc3) {
	fputc('\047', fp);
#ifdef SKIP_SPC
 debug("!T_rm..:%s\n", str);
#endif
	skip_spc(str, 0);
	fprintf(fp, "%s", str);
	fputc('\047', fp);
      }
      fprintf(fp, "; then\necho %d > %s\nfi\n", S_errw, s_S(S_intr));
      fputc('\n', fp);
    }
    fclose(fp);
    exit(0);
  } else for (pp = bp->pp; pp; pp = pp->next) if (pp->split) pp->split = 0;
  close(pv[1]);
  if (f_sh) {
    FILE *ifp, *ofp;
    u_char *p;
    CHILD *chp;
    ofp = fopen(s_S(S_w), "w");
    ifp = fdopen(pv[0], "r");
    chp = get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL);
    fprintf(ofp, "cat %s\n", s_S(S_w));
    if ((cp = strchr(rm, ' '))) *cp = '\0';
    fprintf(ofp, "%s %s\n", rm, s_S(S_w)); /* ==> shell.c (9) */
    while ((i = fgetc(ifp)) != EOF) fputc(i, ofp);
    fclose(ifp);
    fclose(ofp);
    chmod(s_S(S_w), S_IXUSR|S_IWUSR|S_IRUSR);
    if (f_Mb != T_rm && f_Mb != T_arc && f_Mb != T_arc3)
      if (!stat(str, &st))
	if (S_ISDIR(st.st_mode))
	  if (*str == '/') {
	    p = alloca(strlen(str) + 1);
	    strcpy(p, str);
	  } else {
	    p = alloca(strlen(dir) + strlen(str) + 1);
	    strcpy(p, dir);
	    strcat(p, str);
	  }
	else {
	  get_dir(str);
	  p = alloca(strlen(str) + 1);
	  strcpy(p, str);
	}
      else {
	cp = &strchr(str, '\0')[-1];
	if (*cp == '/') *cp = '\0';
	get_dir(str);
	p = alloca(strlen(str) + 1);
	strcpy(p, str);
      }
    else {
      p = alloca(strlen(dir) + 1);
      strcpy(p, dir);
    }
    cp = alloca(strlen(s_S(S_w)) + strlen(s_S(S_tcb)) + strlen(p) + 3);
    sprintf(cp, "%s\r%s %s", s_S(S_w), s_S(S_tcb), p);
    free(rm);
    return strdup(cp);
  } else {
    *fdp = pv[0];
    return NULL;
  }
}

int is_write(u_char name[], int f_dir)/*mode*/
{
  u_char *cp;
  struct stat st;
  int i, f_symlink;

  if (!getuid()) return 1;
  cp = alloca(strlen(name) + 1);
  strcpy(cp, name);
#ifdef SKIP_SPC
debug("is_write:%s\n", cp);
#endif
  skip_spc(cp, 1);
  lstat(cp, &st);
  f_symlink = S_ISLNK(st.st_mode);
  i = stat(cp, &st);
  switch(f_dir) {
  case 1:
    if (i < 0) {
      save_error("%s: %s: %s.\n", s_S(S_tcb), cp, strerror(ENOENT));
      return -ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) {
      save_error("%s: %s: %s.\n", s_S(S_tcb), cp, s_S(S_err7));
      return 0;
    }
    break;
  case -1:
    if (i < 0) return 1;
    break;
  }
  if (f_symlink || !access(cp, W_OK) || (!i && st.st_uid == getuid())) 
    return 1;
  return -EACCES;
}

CHILD *set_cutchp(char arg[])/*tcb*/
{
  char *cp;

  if (*arg != K_z && (cp = strchr(arg, '('))) {
    char *p = alloca(strlen(&cp[1]) + 1);
    strcpy(p, &cp[1]);
    *strchr(p, ')') = '\0';
    return get_child(NULL, NULL, p, NULL, NULL);
  }
  return NULL;
}

void print_intr(char *save, char *mess)/*tcb*/
{
  static int f_errg;
  char *p, *cp;
  int n_error, i, j, nlen;

  if (save == F8) {
    if (load_intr() == S_errg) unlink(s_S(S_intr));
    f_errg = 0;
    return;
  }
  if (mess == F8 && !f_errg) return;
  if (f_errg) {
    n_error = S_errg;
    nlen = 0;
    vt_sc();
    vt_endstand();
    cp = p = NULL;
    vt_move(vt_row(0) - 1, nlen);
    p = s_S(n_error);
    cp = malloc(strlen(vt_geta(A_md)) + strlen(p) 
		+ strlen(vt_geta(A_me)) + 5);
    sprintf(cp, " (%s%s%s) ", vt_geta(A_md), p, vt_geta(A_me));
    i = strlen(vt_geta(A_md)) + strlen(vt_geta(A_me));
    if ((j = strlen(cp) - i - vt_col(0)) > 0) {
      j = strlen(cp) - i - j;
      cp[j + strlen(vt_geta(A_md))] = '\0';
    }
    vt_printf("%s", cp);
    vt_flush();
    vt_rc();
    free(cp);
  }
  if (!access(s_S(S_intr), F_OK)) {
    if ((n_error = load_intr()) == S_errg || n_error == -S_errg) {
      if ((f_errg = 1 + (n_error < 0)) == 2) {
	save_intr(S_errg);
	n_error = -n_error;
      }
      nlen = 0;
    } else nlen = disp_window(NULL, NULL, -1, NULL, NULL, NULL, NULL);
    vt_sc();
    vt_endstand();
    cp = p = NULL;
    if (!mess || mess == F8) mess = "";
    vt_move(vt_row(0) - 1, nlen);
    p = (n_error < 0) ? strerror(-n_error) : s_S(n_error);
    cp = alloca(strlen(vt_geta(A_md)) + strlen(p) + strlen(mess)
		+ strlen(vt_geta(A_me)) + 5);
    sprintf(cp, " (%s%s%s%s) ", vt_geta(A_md), p, mess, vt_geta(A_me));
    if (n_error == S_errp || n_error == S_errq) p = NULL;
    else if (f_errg != 2) vt_putchar(K_g);
    i = strlen(vt_geta(A_md)) + strlen(vt_geta(A_me));
    if ((j = strlen(cp) - i + nlen - vt_col(0)) > 0) {
      j = strlen(cp) - i - j;
      cp[j + strlen(vt_geta(A_md))] = '\0';
    }
    vt_printf("%s", cp);
    vt_flush();
    vt_rc();
    if (!vt_mode('D')) 
      ; /* S_errm in SHELL etc. */
    else unlink(s_S(S_intr));
    if (save && p) save_error("%s: %s.\n", save, p);
  }
}
  
void ins_q(u_char str[])/*mode*/
{
  u_char *cp;

  for (cp = str; *cp; cp++) 
    if (*cp < ' ') {
      memmove(&cp[1], cp, strlen(cp) + 1);
      *cp++ = K_q;
    }
}

