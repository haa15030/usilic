#include "tcb.h"
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "menu.h"

static struct {
  int f_Mb;
  jmp_buf jb;
} Menu;

static void menu_clear(int dummy)
{
  signal(SIGSEGV, menu_clear);
  Menu.f_Mb = T_skip;
  longjmp(Menu.jb, 0);
}

static void clear_cut(CUT *cutp)
{
  if (*cutp->name) *cutp->name = '\0';
  if (cutp->buff) {
    free(cutp->buff);
    cutp->buff = NULL;
  }
}

static void m_copy(LINE *lp, NUM *np, HEAP *hp, char name[])
{
  if (is_name(name, M_copy)) return;
  hp->sp = save_stack(hp->sp, lp, np->M, name, hp->bp, 0);
  strcpy(lp->prev, &name[*name == ' ']);
  *lp->arg = K_z;
  strcpy(name, s_P(P_mode)[M_copy]);
  np->M = M_copy;
  np->Mb = 0;
}

static void m_num(NUM *np, int m, HEAP *hp, FILE **pinp, PN *pnp, char full[])
{
  int i, j, f_pipe;
  PP *pp, *pp1 = NULL;

  m -= N_num;
  if ((f_pipe = hp->bp == &hp->base[4] && *pinp && *pinp != F1)) 
    pnp->nrow = hp->bp->nrow;
  for (i = 0, pp = hp->bp->pp; pp; i++) {
    if (pp->f.line == m) break;
    while (!pp->next && f_pipe && *pinp && *pinp != F1)
      alloc_pipe(0, pinp, &pnp->nrow, NULL, 0);
    if (f_pipe && !*pinp) break;
    pp1 = pp; 
    pp = pp->next;
  }
  pnp->line = (pp1) ? pp1->f.line + 1 : 1;
  if (f_pipe)
    for (j = 0; j < vt_row(0) - 1 && *pinp; j++)
      alloc_pipe(0, pinp, &pnp->nrow, NULL, 0);
  if ((j = pnp->nrow - vt_row(0) + 2) < 0) j = 0;
  if (i > j) pnp->row = i - j;
  else {
    pnp->top = i;
    pnp->row = 0;
  }
  if (f_pipe) {
    hp->bp->nrow = pnp->nrow; /* ==> (2) */
    pnp->row = 0;
  }
  if (np->Mb == T_split || np->Mb == T_buff) {
    strcpy(full, s_S(S_tcb));
    np->ll = 0;
    np->M = T_split2;
  }
}

static int m_dir(int m, HEAP *hp, LINE *lp, char name[], RV *rvp, ARRAY *ap, int *f_Mp)
{
  static u_char dir[N_line];
  u_char *cp, *prev = NULL;
  int i, f_list;

  f_list = strlen(rvp->str) && rvp->pp->cs && *rvp->pp->cs == '/';
  strcpy(dir, (f_list) ? rvp->pp->cs : lp->dir);
  cp = rindex(dir, '/');
  switch(m) {
  case M_m:
    if (cp > dir) *--cp = '\0';
    rindex(dir, '/')[1] = '\0'; 
    break;
  case M_sel:
    prev = alloca(strlen(lp->prev) + 1);
    strcpy(prev, lp->prev);
    cp[1] = '\0'; 
    break;
  default:
    for (i = m - T_0 + 1, cp = dir; i && (cp = index(cp, '/')); i--, cp++) ;
    if (cp) *cp = '\0';
    break;
  }
  i = access(dir, F_OK);
  if (i < 0) {
    if (*name == '/') strcpy(dir, s_S(S_home));
    else {
      sprintf(ap->intr, ": %s", dir);
      return echo_mess(-ENOENT);
    }
  }
  clear_stack(hp, lp, name, &i, K_z);
  if (prev && is_prev(prev, M_du)) {
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, &prev[4]);
    strcpy(ap->name, lp->dir);
  }
  check_dir(hp, lp, dir, &hp->ll.p.dir);
  strcpy(name, dir);
  *f_Mp = T_dir;
  return 0;
}

static void m_row(int m, HEAP *hp, RV *rvp, PN *pnp, NUM *np, FILE **pinp)
{
  static FILE *pin0 = F1;
  int i;

  move_row(m, rvp, pnp, (hp->bp == &hp->base[4]) ? pinp : &pin0, np->Mb);
  if (hp->bp->nrow < pnp->nrow) hp->bp->nrow = pnp->nrow; /* ==> (2) */
  disp_window(rvp->ps, rvp->pp, np->M, NULL, NULL, NULL, &i);
  print_intr(NULL, NULL); /* Not enough buffer && T_pipe? && M_tail */ 
}

static int m_page(int m, HEAP *hp, RV *rvp, PN *pnp, NUM *np, FILE **pinp)
{
  int i, j;
  static FILE *pin0 = F1;

  i = move_page(m, rvp, pnp, (hp->bp == &hp->base[4]) ? pinp : &pin0, np->Mb);
  if (hp->bp->nrow < pnp->nrow) hp->bp->nrow = pnp->nrow; /* ==> (2) */
  if (((j = m == M_forw || m == M_kforw) 
       || (m == M_back || m == M_kback)) && i && i <= vt_row(0) - 2) {
    if (j)
      for (rvp->row = i = vt_row(0) - i - 1; i <= vt_row(0) - 3; i++)
	rvp->pp = rvp->pp->prev;
    else
      for (rvp->row = --i; i; i--) rvp->pp = rvp->pp->next;
    pnp->row = rvp->row;
    pnp->line = rvp->pp->f.line;
    if (strlen(rvp->str)) {
      strcpy(rvp->str, &rvp->pp->cs[rvp->col]);
      rvp->str[vt_col(0) - rvp->col] = '\0';
    }
  }
  disp_window(rvp->ps, rvp->pp, np->M, NULL, NULL, NULL, &i);
  print_intr(NULL, NULL); /* Not enough buffer && T_pipe? && M_tail */ 
  return ((i = pnp->nrow - (vt_row(0) - 1)) > 0) ? i : 0;
}

static u_char *m_w(int m, PP *pp, u_char name[], u_char dir[], char comm[])
{
  u_char *cp = NULL;
  FILE *tp;

  switch(m) {
  case M_w:
    tp = fopen(s_S(S_w), "w");
    fprintf(tp, "%s", name);
    fclose(tp);
    cp = malloc(11 + strlen(s_S(S_w)) + 1);
    sprintf(cp, "echo `cat %s`", s_S(S_w));
    break;
  case M_w2:
    send_pp(pp, "w");
    cp = malloc(4 + strlen(s_S(S_w)) + 1);
    sprintf(cp, "cat %s", s_S(S_w));
    break;
  case M_chdir:
    cp = strdup(comm);
    break;
  case M_chdir2:
    cp = malloc(4 + strlen(dir) + 2 + 1);
    sprintf(cp, "cd \047%s\047\r", dir);
    break;
  }
  return cp;
}

static int m_arc(int m, int f_Mb)
{
  if ((m == M_cp && !check_chp(S_cp))
      || (m == M_mv && !check_chp(S_mv))
      || (m == M_rm && !check_chp(S_rm))) return -echo_mess(S_errc);
  if (m == M_arc 
      && (f_Mb == T_tar || f_Mb == T_lha || f_Mb == T_unzip 
	  || f_Mb == T_rpm || f_Mb == T_deb)) 
    m = T_arc2;
  else if (f_Mb == T_dir || f_Mb == T_du
	   || (m == M_rm 
	       && (f_Mb == M_lfile
		   || f_Mb == M_ldir 
		   || f_Mb == M_man
		   || f_Mb == M_prog
		   || f_Mb == M_egrep
		   || f_Mb == M_cmd
		   || f_Mb == T_cmd))) ;
  else return -echo_mess(S_errv);
  return m;
}

static int m_exit(int m, HEAP *hp, LINE *lp, ARRAY *ap, int *f_Mp, int f_comm)
{
  int i, f_M;

  f_M = (m == T_arc2) ? M_arc : m;
  while (read_stack(hp->sp, s_P(P_mode)[f_M], 1) || is_name(ap->name, f_M))
    hp->sp = load_stack(hp->sp, lp, f_Mp, ap->name, &hp->bp, hp->base);
  if (((i = *ap->name == ' ') 
       && !is_name(ap->name, M_du) && !is_name(ap->name, T_pipe2)) 
      || m == M_log || m == M_error) ;
  else if (is_inter(NULL, -1, 0))
    return echo_mess(S_errf); /* for su etc. */
  if (f_comm == 2 || m == M_exit) f_comm = 1;
  hp->sp = save_stack(hp->sp, lp, *f_Mp, ap->name, hp->bp, !f_comm);
  strcpy(lp->prev, &ap->name[i]);
  *lp->arg = K_z;
  strcpy(ap->name, s_P(P_mode)[f_M]);
  *f_Mp = m;
  return 0;
}

static int m_locate(int m, ARRAY *ap, HEAP *hp, NUM *np, LINE *lp, RV *rvp)
{
  if (!check_chp(S_locate)) return m;
  if (is_name(ap->name, M_locate)) return echo_mess(S_errv);
  if (m == M_locate2) {
    if (!hp->base[5].pp) return echo_mess(S_errv);
    strcpy(rvp->str, ap->lstr);
    m = T_locate;
  }
  clear_stack(hp, lp, ap->name, &np->M, '/');
  hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
  strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  strcpy(ap->name, s_P(P_mode)[M_locate]);
  *lp->arg = K_z;
  if ((np->M = m) == T_locate) {
    hp->bp = &hp->base[5];
    np->Mb = T_skip;
  }
  return 0;
}

static int m_grep(int m, ARRAY *ap, LINE *lp, FLAG *fp, HEAP *hp, NUM *np)
{
  if (!check_chp((m == M_fgrep) ? S_fgrep : S_egrep)) return m;
  if ((is_name(ap->name, M_hist)
       && (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep)
	   || is_prev(lp->prev, M_cut)))
      || is_name(ap->name, M_cut) 
      || is_name(ap->name, M_fgrep) 
      || is_name(ap->name, M_egrep)) return echo_mess(S_errv);
  if (fp->grep)
    clear_stack(hp, lp, ap->name, &np->M, 'g');
  fp->grep = (m == M_fgrep) ? 2 : 1;
  hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
  strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  strcpy(ap->gname, ap->name);
  strcpy(ap->name, s_P(P_mode)[m]);
  *lp->arg = K_z;
  np->M = m;
  return 0;
}

static int m_cut(int m, ARRAY *ap, HEAP *hp, NUM *np, PN *pnp, FILE **pinp, LINE *lp)
{
  int i;

  if (is_name(ap->name, M_cut)) return echo_mess(S_errv);
  if (hp->bp == &hp->base[4] && ((i = !*pinp) || (*pinp && *pinp != F1))) { 
    if (!i) {
      *pinp = seek_intr(*pinp, &pnp->nrow);
      if (*pinp) return echo_mess(S_errv);
    }
    alloc_winch(hp->bp, 1); /* set bp->size for init_cut() */
  }
  hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0); /* ==> (8) */ 
  if ((i = m == T_fcut) || m == T_fcut2) m = (i) ? M_cut2 : M_cut;
  else clear_cut(&lp->cut); /* ==> (7) */
  if (hp->ll.p.split) hp->ll.p.split = free_list(hp->ll.p.split);
  strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  strcpy(ap->name, s_P(P_mode)[M_cut]);
  lp->cut.chp = set_cutchp(lp->arg);
  *lp->arg = K_z;
  np->M = m;
  return 0;
}

static int m_man(int m, HEAP *hp, LINE *lp, int *f_Mp, char name[])
{
  if (!check_chp(S_man)) return m;
  clear_stack(hp, lp, name, f_Mp, '/');
  hp->sp = save_stack(hp->sp, lp, *f_Mp, name, hp->bp, 0);
  strcpy(lp->prev, &name[*name == ' ']);
  *lp->arg = K_z;
  strcpy(name, s_P(P_mode)[M_man]);
  *f_Mp = m;
  return 0;
}

static void m_help(int m, ARRAY *ap, LINE *lp, HEAP *hp, NUM *np)
{
  if (m == M_key || m == T_key) {
    if (is_name(ap->name, M_key)) 
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 1);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    strcpy(ap->name, s_P(P_mode)[M_key]);
  } else {
    if ((!strcmp(ap->name, s_S(S_help)) && hp->bp == &hp->base[7])
	|| read_stack(hp->sp, &hp->base[7], 0))
      clear_stack(hp, lp, ap->name, &np->M, 'h');
    else if (is_name(ap->name, M_key) || is_name(ap->name, M_tcb)) 
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    strcpy(ap->name, s_S(S_help));
  }
  *lp->arg = K_z;
  np->M = m;
  np->Mb = 0;
}

static int m_prog(int m, LINE *lp, NUM *np, HEAP *hp, char name[])
{
  np->Mb = 0;
  switch(m) {
  case M_prog2:
    if (is_inter(NULL, 0, 0)) return echo_mess(S_errf);
    np->Mb = M_prog;
  case M_lfile2:
    if (!np->Mb) np->Mb = M_lfile;
  case M_ldir2:
    if (!np->Mb) np->Mb = M_ldir;
  case M_lfile: case M_ldir:
  case M_prog:
    if (!np->Mb) np->Mb = m;
    if (is_fm() && *name) clear_stack(hp, lp, name, &np->M, '/');
    hp->sp = save_stack(hp->sp, lp, np->M, name, hp->bp, 0);
    strcpy(lp->prev, &name[*name == ' ']);
    *lp->arg = K_z;
    strcpy(name, s_P(P_mode)[np->Mb]);
    np->M = m;
    np->Mb = 0;
  }
  return 0;
}

static int m_list(int m, HEAP *hp, NUM *np, ARRAY *ap, LINE *lp)
{
  int f_fgrep;

  if (m == T_hist) {
    if (is_name(ap->name, M_lfile)) m = T_hfile;
    else if (is_name(ap->name, M_ldir)) m = T_hdir;
    else if (is_name(ap->name, M_man)) m = T_hman;
    else if (is_name(ap->name, M_locate)) m = T_hlocate;
    else if (((f_fgrep = is_name(ap->name, M_fgrep)) 
	      || is_name(ap->name, M_egrep) 
	      || is_name(ap->name, M_cut))) {
      if (is_prev(lp->prev, M_hist)) return echo_mess(S_errv);
      m = (f_fgrep) ? T_hfgrep : T_hegrep;
    } else if (is_inter(NULL, 0, 0)) return echo_mess(S_errf);
  } else if (m == T_cmd && is_name(ap->name, M_hist)) 
    return echo_mess(S_errf);
  else if (m != T_list && is_inter(NULL, 0, 0)) echo_mess(S_errf);
  if (m == T_list || m == T_cmd || m == T_hist
      || m == T_hfile || m == T_hdir || m == T_hman 
      || m == T_hlocate || m == T_hfgrep || m == T_hegrep) {
    clear_stack(hp, lp, ap->name, &np->M, 'g');
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  }
  switch(m) {
  case T_hfile:
  case T_hdir:
  case T_hman:
  case T_hlocate:
  case T_hfgrep:
  case T_hegrep:
    np->Mb = M_hist;
    break;
  case T_list:
    np->Mb = M_list;
    break;
  case T_cmd:
    np->Mb = M_cmd;
    break;
  case T_hist:
    np->Mb = M_hist;
    break;
  default:
    np->Mb = m;
    break;
  }
  *lp->arg = K_z;
  strcpy(ap->name, s_P(P_mode)[np->Mb]);
  np->M = m;
  np->Mb = 0;
  return 0;
}

static int m_edit(int line, char name[], char arg[], HEAP *hp, NUM *np)
{
  int m;

  if ((m = do_edit(line, name, arg, np->M))) return m;
  np->Mb = 0;
  free_bp(&hp->base[1]); /* ==> (3) */
  if ((hp->bp && hp->bp < &hp->base[0]) || hp->bp > &hp->base[N_base-1]) {
    free_bp(hp->bp);
    free(hp->bp);
  }
  return m;
}

static int m_shell(int m, HEAP *hp, LINE *lp, NUM *np, ARRAY *ap, PN *pnp, FILE **pinp, FLAG *fp, struct stat st0)
{
  static char name[N_line];
  int i;
  struct stat st;
  FILE *pin;
  STACK *sp;

  if ((pin = *pinp) != F1) 
    if ((hp->bp == &hp->base[4] && is_name(ap->name, T_pipe2))
	|| ((sp = read_stack(hp->sp, &hp->base[4], 0))
	    && is_name(sp->name, T_pipe2))) {
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      clear_stack(hp, lp, ap->name, &np->M, 'p');
    }
  switch(np->Mb) {
  case M_list:
  case M_hist:
  case M_cmd:
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
  case T_cmd:
  case T_hist:
  case M_prog:
  case M_prog2:
    clear_stack(hp, lp, ap->name, &np->M, 'p');
    if (np->Mb != M_list) {
      save_ll(np->ll, &hp->ll, &hp->ss, ap->full, pnp);
      fp->comm = 3;
    }
    break;
  default:
    save_ll(np->ll, &hp->ll, &hp->ss, ap->full, pnp);
    if ((i = is_comm(np->Mb, 2)) || np->Mb == T_arc3) {
      if (!i)
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    } else {
      if (hp->bp != &hp->base[4]) 
	clear_stack(hp, lp, ap->name, &np->M, 'P'); /* clear only "(stdin)" */
      if (np->Mb != M_du) fp->comm = 0;
    }
    *ap->mess = '\0';
  }
  np->ll = -1;
  strcpy(name, &ap->name[*ap->name == ' ']);
  np->Mb = M_shell;
  if (load_intr() == S_errg) unlink(s_S(S_intr));
  m = do_shell(name, ap->mess, pinp, hp->w, &np->ppid, hp->ll.p.prog, 
	       &hp->ll.p.list, &hp->ss.p.cmd, &hp->ss.p.hist, fp->comm);
  vt_mode('d');
  if (hp->w) {
    free(hp->w);
    hp->w = NULL;
  }
  if (fp->comm) fp->comm = 0;
  do {
    switch(-m) {
    case M_cmd:
      make_cmd(hp->ss.p.cmd, &hp->ll.p.cmd);
    case M_hist:
    case M_list:
      hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
      strcpy(lp->prev, &s_P(P_mode)[M_shell][1]);
      continue;
      break;
    case T_dir:
      check_dir(hp, lp, ap->mess, &hp->ll.p.dir);
      strcpy(ap->name, ap->mess);
      stat(ap->name, &st);
      free_bp(hp->bp);
      break;
    case T_file:
      strcpy(ap->name, name);
      check_dir(hp, lp, ap->mess, &hp->ll.p.dir);
      hp->sp = save_stack(hp->sp, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, ap->mess);
      fp->cr = 1;
      np->Mb = 0;
      break;
    case T_pipe2:
      if (hp->bp == &hp->base[4]) /* ^O^M or MAN */
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
      sprintf(lp->prev, "%s", &ap->name[*ap->name == ' ']);
      strcpy(ap->name, s_P(P_mode)[T_pipe2]);
      *lp->arg = K_z;
      np->Mb = 0;
      break;
    case M_bl:
      if (vt_mode('W')) fp->comm = 4;
      m = -T_disp;
      if (np->M == T_dir) {
	chdir(ap->name);
	stat(ap->name, &st);
	if (st.st_ctime != st0.st_ctime || st.st_ino != st0.st_ino) 
	  free_bp(hp->bp);
      }
      continue;
      break;
    case T_winch:
    case M_hs:
      fp->comm = 4;
      return m;
    case M_du:
      clear_stack(hp, lp, ap->name, &np->M, K_z);
      if (strcmp(lp->dir, ap->mess)) {
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->mess);
	strcpy(ap->name, ap->mess);
      }
    default:
      if (-m >= M__0 && -m <= M__19) { 
	char *p = alloca(4 + 1);
	strcpy(p, mtoa(-m));
	read_m(NULL, 3, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
	m = -T_disp;
      }
      continue;
      break;
    }
    np->M = -m;
    m = 0;
  } while (0);
  return m;
}

static int m_du(int m, HEAP *hp, ARRAY *ap, NUM *np, LINE *lp, FILE **pinp, struct stat *st0p)
{
  if (m == M_du2 && !hp->base[2].pp) return echo_mess(S_errv);
  alloc_pipe(-1, pinp, NULL, NULL, 0);
  clear_stack(hp, lp, ap->name, &np->M, K_z);
  if (m == M_du2) {
    stat(lp->dir, st0p);
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, ap->du);
    strcpy(ap->name, ap->du);
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    chdir(ap->du);
    strcpy(lp->prev, ap->du);
    strcpy(ap->name, s_P(P_mode)[M_du]);
    np->Mb = T_skip;
    np->M = T_du;
    hp->bp = &hp->base[2];
  } else {
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, ap->du);
    chdir(ap->du);
    strcpy(ap->name, s_P(P_mode)[M_du]);
    np->M = M_du;
  }
  return 0;
}

static int m_yn(int m, PP *pp, int f_Mb, u_char prev[])
{
  int i, j;
  char *cp;

  if (!is_comm(f_Mb, 0)) return echo_mess(S_errv);
  for (; pp; pp = pp->next) 
    if (m == M_yes) {
      pp->split = -f_Mb;
      if (is_prev(prev, M_du)) j = ichrstr(pp->cs, 0, "./");
      else j = get_col(pp->cs) - 1;
      j = strlen((cp = &pp->cs[j]));
      if (f_Mb == M_rm && *prev != '/') i = 1;
      else i = (f_Mb == M_mv || f_Mb == M_rm) ? is_write(&cp[1], 0) : 1;
      if (i <= 0 || !strcmp(&cp[1], "./") || !strcmp(&cp[1], "../"))
	pp->split = 0;
    } else pp->split = 0;
  return -M_clear;
}

static int m_dest(int m, LINE *lp, RV *rvp, NUM *np, HEAP *hp, ARRAY *ap, FLAG *fp)
{
  FILE *tp;
  int i, j, fd;
  u_char *p, tmp[N_line];
  struct stat st;

  if ((i = is_comm(np->Mb, 0)) || is_comm(np->Mb, 1)) {
    if (i) {
      np->Mb = i;
      disp_prev(lp->prev, lp->dir, np->klen);
    } else {
      if (np->Mb != T_rm) {
	while ((p = strstr(ap->mess, "//"))) strcpy(ap->mess, &p[1]);
	if (*ap->mess == '~') {
	  strcpy(tmp, getenv("HOME"));
	  if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
	  if (strlen(ap->mess) > 1) 
	    strcat(tmp, &ap->mess[1 + (ap->mess[1] == '/')]);
	  strcpy(ap->mess, tmp);
	} else strcpy(tmp, ap->mess);
	j = 1;
	if (np->Mb == T_cp || np->Mb == T_mv) {
	  if (np->Mb == T_mv && access(lp->dir, W_OK) < 0) j = -EACCES;
	  else {
	    PP *pp;
	    int i_mark;
	    for (i_mark = 0, pp = hp->bp->pp; i_mark < 2 && pp; pp = pp->next) 
	      if (pp->split < 0) i_mark++;
	    if (strchr(tmp, '\0')[-1] != '/') {
	      if (!access(tmp, F_OK)) {
		stat(tmp, &st);
		if (!S_ISDIR(st.st_mode)) get_dir(tmp);
		else if (strcmp(tmp, "/")) strcat(tmp, "/");
	      } else if (!strchr(tmp, '/')) strcpy(tmp, ".");
	      else get_dir(tmp);
	    }
	    if (access(tmp, W_OK)) j = -EACCES;
	  }
	} else if (np->Mb == T_arc || np->Mb == T_arc3) {
	  if (!getuid()) {
	    if (stat(tmp, &st) >= 0 && !S_ISDIR(st.st_mode))/* Not directory */
	      j = 0;
	  } else j = is_write(tmp, 1);
	}
	if (j <= 0) {
	  save_intr(j);
	  np->Mb = T_skip;
	  return 0;
	}
	if (np->Mb == T_arc3) {
	  clear_stack(hp, lp, ap->name, &np->M, 'g');
	  hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	  strcpy(&strchr(ap->mess, '\0')[1], ap->name);
	}
      } else if (np->Mb == T_rm) {
	if (*ap->mess != 'y') {
	  disp_prev(lp->prev, lp->dir, 0);
	  np->Mb = M_rm;
	  return 0;
	}
      } else if (((i = nmark(hp->bp->pp, np->Mb)) > 1 
		  && (j = is_write(ap->mess, 1)) <= 0)
		 || (i == 1 && (j = is_write(ap->mess, -1)) < 0)) {
	save_intr((j < -128) ? - (j - 128) : ((j) ? j : S_err7));
	disp_prev(lp->prev, lp->dir, 0);
	np->Mb = is_comm(np->Mb, 1);
	return 0;
      }
      clear_stack(hp, lp, ap->name, &np->M, 'g');
      if (!(hp->w = command(np->Mb, ap->mess, hp->bp, lp, np->klen
			    , ap->du, &fd))) {
	tp = fdopen(fd, "r");
	/* (4) RM at EGREP */
	if ((m = do_rm(&hp->ll, &hp->ss, lp->prev, tp)) == -M_egrep
	    || m == -T_cmd2)
	  hp->sp = load_stack(hp->sp, lp, &np->M
			      , ap->name, &hp->bp, hp->base);
	hp->sp = load_stack(hp->sp, lp, &np->M
			    , ap->name, &hp->bp, hp->base);
	fclose(tp);
      } else m = -M_shell;
      np->clen = 0;
    }
  }
  return m;
}

static FILE *m_pipe(NUM *np, ARRAY *ap, LINE *lp)
{
  int f_chp;
  CHILD *chp;
  FILE *pin;

  np->M = 0;
  if (!is_prev(lp->prev, M_du)) sprintf(lp->prev, "%s", lp->dir);
  *lp->arg = K_z;
  strcpy(ap->mess, ap->name);
  if ((chp = check_ext(ap->name, NULL, 0)) && (f_chp = is_chp(chp, NULL))) {
    pin = fork_pipe(f_chp, chp, ap->name);
    np->M = T_pipe3;
  } else {
    if (chp) chp = NULL;
    pin = fopen(ap->name, "r");
  }
  if (!np->M) np->M = T_pipe3;
  np->Mb = 0;
  return pin;
}

static int m_cr(int m, NUM *np, HEAP *hp, LINE *lp, ARRAY *ap, RV *rvp, FLAG *fp, PN *pnp, FILE **pinp)
{
  int i, j, size;
  u_char *cp;

  if (!*pinp && m == M_fp) { /* ^O^M after ^O^M ^J^C */
    echo_mess(S_errv);
    return m;
  } else if (m == M_fcut || m == M_fcut2)
    switch(np->Mb) {
    case T_tar: case T_lha: case T_unzip: 
    case T_rpm: case T_deb:
    case T_dir: case T_du: case T_locate:
      break;
    default:
      echo_mess(S_errv);
      return m;
      break;
    }
  else if (!strlen(rvp->str) && !is_comm(np->Mb, 0)) return m;
  i = 0;
  switch(np->Mb) {
  case M_exit:
    if (*rvp->str == 'y') {
      np->Mb = 0;
      return -T_exit;
    } else {
      disp_prev(lp->prev, lp->dir, 0);
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      np->Mb = T_skip;
      return 0;
    }
    break;
  case T_cmd: case T_hist:
    i = np->Mb;
  case M_list: case M_cmd: case M_hist:
    clear_stack(hp, lp, ap->name, &np->M, 'g');
    if (i)
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    strcpy(ap->mess, rvp->str);
    return -M_shell;
  case T_list: case T_hfile: case T_hdir: case T_hman:
    clear_stack(hp, lp, ap->name, &np->M, 'g');
    strcpy(ap->iname, rvp->str);
    j = np->Mb;
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if ((i = is_comm(np->M, 0)) || (is_comm(np->M, 1))) {
      if (i) np->M = np->Mb = i;
      else np->Mb = np->M;
      strcat(ap->mess, rvp->str);
      *rvp->str = '\0';
      m = -T_disp;
    } else {
      m = -np->M;
      if (j != T_list)
	read_m(NULL, 3, 0, "\r", NULL, NULL, NULL, NULL, NULL, NULL);
      else if ((j = is_name(ap->name, M_hist)) || is_name(ap->name, M_cmd)) {
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	if (j) m = (is_prev(lp->prev, M_shell)) ? -M_hist : -T_hist;
	else m = (is_prev(lp->prev, M_shell)) ? -M_cmd : -T_cmd;
      }
    }
    return m;
  case M_key:
    if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))
      clear_stack(hp, lp, ap->name, &np->M, 'g');
    if (rvp->pp->tell) {
      u_char *p = alloca(4 + 4 + 1);
      strcpy(p, mtoa(M_bl));
      if (rvp->pp->tell != M_bl) strcat(p, mtoa(rvp->pp->tell));
      read_m(NULL, 3, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    return 0;
  case M_man: case M_man2:
    if (hp->bp == &hp->base[4] && *pinp != F1) 
      alloc_pipe(-1, pinp, NULL, NULL, 0);
    clear_stack(hp, lp, ap->name, &np->M, 'p');
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    strcpy(ap->name, rvp->str);
    *lp->arg = K_z;
    np->M = T_man;
    np->Mb = 0;
    return 0;
    break;
  case T_hlocate:
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
  case M_locate:
    if (np->Mb == M_locate) 
      hp->ss.p.hlocate = save_history(hp->ss.p.hlocate, rvp->str, 1);
    strcpy(ap->lstr, rvp->str);
    np->M = T_locate;
    return 0;
  case T_tar: case T_lha: case T_unzip: case T_rpm: case T_deb:
    if ((i = m != M_fcut && m != M_fcut2)) 
      fp->cr = (m == M_cr) ? 1 : ((m == M_fp) ? 2 : 0);
    else {
      if (lp->cut.buff) free(lp->cut.buff);
      if (np->Mb == T_rpm || (size = rvp->pp->split) < 0) 
	lp->cut.buff = read_arc(hp->arc, ap->name, rvp->str, &size);
      else {
	if (size > hp->bsize) size = hp->bsize;
	while (!(lp->cut.buff = malloc(size)) && (size /= 2)) ; 
	memcpy(lp->cut.buff, (u_char*)rvp->pp->tell, size);
      }
      sprintf(lp->cut.name, " %d", size);
      fp->cut = (m == M_fcut2) ? 2 : 1;
    }
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp, i);
    break;
  case M_egrep:
    if (!load_list(hp->ll.p.grep, rvp->str, NULL)) {
      hp->ll.p.grep = save_list(hp->ll.p.grep, rvp->str, pnp);
      if (is_fm())
	hp->ss.p.grep = save_str(hp->ss.p.grep, rvp->str, pnp);
    }
  case T_hfgrep:
  case T_hegrep:
  case M_fgrep:
    if ((i = np->Mb == T_hfgrep) || np->Mb == T_hegrep) {
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      np->Mb = (is_name(ap->name, M_cut)) ? M_cut : ((i) ? M_fgrep : M_egrep);
    }
    if (np->Mb != M_cut) {
      if (is_name(ap->name, M_fgrep))
	hp->ss.p.hfgrep = save_history(hp->ss.p.hfgrep, rvp->str, 1);
      else
	hp->ss.p.hegrep = save_history(hp->ss.p.hegrep, rvp->str, 1);
      strcpy(ap->gstr, rvp->str);
      if (np->Mb != M_cut) strcpy(ap->name, ap->gname);
      break;
    }
  case M_cut: case M_cut2:
    /* 2026-08-05: M_cut2 (^Jc, FIXED string) was unconditionally
       sharing M_cut's (^J^C, REGEX) own hegrep ring here, unlike the
       M_fgrep/T_hfgrep/T_hegrep block just above (line ~831) which
       DOES branch on fixed-vs-regex for exactly this same choice.
       A user comparing CUT's fixed/regex split against the already-
       consistent FGREP/EGREP split found this the confusing odd one
       out. m still holds its own switch-entry value here (M_cut or
       M_cut2), not yet overwritten by anything below. */
    if (m == M_cut2)
      hp->ss.p.hfgrep = save_history(hp->ss.p.hfgrep, rvp->str, 1);
    else
      hp->ss.p.hegrep = save_history(hp->ss.p.hegrep, rvp->str, 1);
    if (np->Mb == M_cut) {
      hp->ll.p.grep = save_list(hp->ll.p.grep, rvp->str, pnp);
      if (is_fm())
	hp->ss.p.grep = save_str(hp->ss.p.grep, rvp->str, pnp);
    }
    i = *lp->cut.name; /* (7) */
    hp->sp = load_stack(hp->sp, lp	/* load lp->cut */
			, &np->M, ap->name, &hp->bp, hp->base);
    j = *lp->cut.name; /* (8) */
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 1);
    strcpy(lp->cut.str, rvp->str);
    if (!i && j) *lp->cut.name = '\0'; /* !i : ^J^C, j : ^J^B */
    np->M = T_cut;
    return 0;
  case T_grep:
    np->tell = rvp->pp->tell;
    strcpy(rvp->str, ap->gstr);
    if (is_name(ap->name, M_fgrep))
      hp->ss.p.hfgrep = save_history(hp->ss.p.hfgrep, rvp->str, 1);
    else
      hp->ss.p.hegrep = save_history(hp->ss.p.hegrep, rvp->str, 1);
    break;
  case T_locate:
    if ((i = m == M_fcut) || m == M_fcut2) {
      fp->cut = (i) ? 1 : 2;
      break;
    }
  case M_ldir:
  case M_ldir2:
  case M_lfile:
  case M_lfile2:
    if (*(cp = &strchr(rvp->str, '\0')[-2]) == '/' && cp[1] == '~')
      memmove(rvp->str, &cp[1], strlen((char*)&cp[1]) + 1);
    else if ((cp = strchr(rvp->str, '~')) && cp != rvp->str
	     && cp[-1] == '/' && (!cp[1] || cp[1] == '/'))
      memmove(rvp->str, cp, strlen((char*)cp) + 1);
    if (*rvp->str != '/') {
      if (*rvp->str == '~') 
	sprintf(ap->name, "%s%s", s_S(S_home), &rvp->str[1]);
      else sprintf(ap->name, "%s%s", lp->dir, rvp->str);
      strcpy(rvp->str, ap->name);
    } else strcpy(ap->name, rvp->str);
    if ((cp = strstr(rvp->str, " ("))) *cp = '\0';
    lp->cut.chp = NULL;
    if (set_cut(hp, &lp->cut, rvp->str, ap->name, lp->dir, lp->old)) {
      *lp->prev = K_z;
      hp->grep = NULL;
      hp->sp = save_stack(NULL, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, lp->dir);
      np->M = T_cut;
      return 0;
    }
    if (np->Mb == M_lfile) fp->cr = (strstr(ap->name, " (")) ? 3 : 1;
    else fp->cr = (np->Mb == T_locate && m == M_fp) ? 2 : 1;
    break;
  case T_dir:
  case T_cut:
    if ((i = m == M_fcut) || m == M_fcut2) fp->cut = (i) ? 1 : 2;
    fp->cr = (m == M_cr) ? 1 : ((m == M_fp) ? 2 : 0);
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp
			, (np->Mb == T_cut));
    break;
  case T_du:
    strcpy(rvp->str, ap->du);
  case M_prog:
  case M_prog2:
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp
			, (np->Mb != M_prog2));
    break;
  default:
    if (is_comm(np->Mb, 0)) {
      if (is_prev(lp->prev, M_du)) i = ichrstr(rvp->pp->cs, 0, "./");
      else if (np->Mb == T_arc2 || *lp->prev == '/') 
	i = get_col(rvp->pp->cs) - 1;
      else i = 0;
      i = strlen((cp = &rvp->pp->cs[i]));
      if (rvp->pp->split == -np->Mb) rvp->pp->split = 0;
      else rvp->pp->split = -np->Mb;
      if (rvp->pp->split) {
	if (np->Mb == M_rm && *lp->prev != '/') np->clen = 0;
	else {
	  j = (np->Mb == M_mv || np->Mb == M_rm) ? is_write(&cp[1], 0) : 1;
	  if (j <= 0 || !strcmp(&cp[1], "./") || !strcmp(&cp[1], "../")) {
	    vt_putchar(K_g);
	    np->clen -= i;
	    rvp->pp->split = 0;
	  } else np->clen += i;
	}
      } else np->clen -= i;
      return -M_clear;
    } else if (m == M_cr && (is_comm(np->Mb, 1))) {
      strcpy(ap->mess, rvp->str);
      if (np->Mb == T_arc3) strcpy(&strchr(ap->mess, '\0')[1], ap->name);
      return -M_dest;
    }
    if (!strlen(rvp->str)) {
      if (m == M_code && np->Mb == T_file && strstr(lp->arg, "(file)")) {
	*lp->arg = K_z;
	fp->cr = 0;
	np->M = T_file;
	m = 0;
      }
      return m;
    }
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp, 1);
    break;
  }
  if (!(m = mode(hp, np, ap, lp, rvp)))
    switch(np->M) {
    case T_buff:
      if (fp->cut) {
	*lp->cut.name = ' ';
	m = -((fp->cut == 2) ? T_fcut : T_fcut2);
	fp->cut = 0;
	np->M = np->Mb;
	return m;
      }
      break;
    case T_file2:
    case T_file:
    case T_dir:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      if (!access(lp->dir, F_OK)) 
	check_dir(NULL, lp, NULL, &hp->ll.p.dir);
      if (fp->cut) {
	strcpy(lp->cut.name, ap->name);
	*lp->arg = K_z;
	m = -((fp->cut == 2) ? T_fcut : T_fcut2);
	fp->cut = 0;
	return m;
      }
      if (fp->cr == 2 && np->M != T_dir) *pinp = m_pipe(np, ap, lp);
      break;
    case T_grep:
      if (hp->bp == &hp->base[4] && *pinp && *pinp != F1) {
	pnp->nrow = hp->bp->nrow;
	*pinp = seek_intr(*pinp, &pnp->nrow);
	print_intr(NULL, NULL);
	if (hp->bp->nrow < pnp->nrow) hp->bp->nrow = pnp->nrow;
      }
      strcpy(hp->grep->str, ap->gstr);
      break;
    case M_shell:
      m = -M_shell;
      break;
    }
  else
    switch(-m) {
    case M_edit:
    case M_w:
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      break;
    case T_prog:
      if (np->M == T_pipe5) *pinp = m_pipe(np, ap, lp);
      else {
	u_char *p = alloca(4 + 4 + 1);
	strcpy(p, mtoa(M_sel));
	strcat(p, mtoa(M_bl2));
	read_m(NULL, 2, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
      }
      np->Mb = 0;
      m = 0;
      break;
    case T_skip:
      np->Mb = T_skip;
      m = 0;
    case M_clear:
      break;
    }
  return m;
}

static void m_clear(RV *rvp, PN *pnp, LINE *lp, NUM *np, ARRAY *ap, HEAP *hp, FLAG *fp)
{
  vt_mode('d');
  print_intr(F8, NULL);
  if (rvp->row > vt_row(0) - 2) pnp->row = rvp->row = vt_row(0) - 2;
  disp_window(rvp->ps, rvp->pp, np->M, ap->name, lp->prev, lp->arg, &np->klen);
  if (np->Mb != M_locate && np->Mb != T_locate && np->Mb != M_exit)
    print_line(rvp->ps, rvp->pp
	       , (hp->bp == &hp->base[4]) ? -np->nlen : np->nlen, NULL);
  print_intr(NULL, (*ap->intr) ? ap->intr : NULL);
  if (*ap->intr) *ap->intr = '\0';
}

static int m_bl(int m, NUM *np, ARRAY *ap, LINE *lp, HEAP *hp, FLAG *fp, FILE **pinp, PN *pnp, struct stat st0)
{
  int i;
  char tmp[N_line];
  struct stat st;

  do {
    switch(np->Mb) {
    case M_list: case M_cmd: case M_hist:
      if (is_prev(lp->prev, M_shell)) {
	*ap->mess = '\0';
	return -M_shell;
      }
      break;
    case T_list: case T_cmd: case T_hist:
    case T_hfile: case T_hdir:
    case T_hman: case T_hlocate:
    case T_hfgrep: case T_hegrep:
      break;
    default:
      if ((i = is_comm(np->Mb, 2))) {
	disp_prev(lp->prev, lp->dir, 0);
	np->Mb = i;
	return m;
      }
      continue;
      break;
    }
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if ((i = is_comm(np->M, 0)) || is_comm(np->M, 1)) {
      if (i) np->M = np->Mb = i;
      else {
	np->Mb = np->M;
	*ap->iname = K_a0;
      }
      m = -T_disp;
    } else m = -T_disp;
    return m;
  } while (0);
  if (is_pg() && !hp->sp) return m;
  if (hp->sp) {
    *tmp = '\0';
    switch(np->M) {
    case M_cut2:
    case T_cut:
    case M_cut:
#if 1
      if ((i = *lp->cut.name)) {
	if (hp->bp != &hp->base[7]
	    && !is_prev(lp->prev, M_egrep) 
	    && !is_prev(lp->prev, M_fgrep))
	  hp->sp = load_stack(hp->sp, lp, &np->M
			      , ap->name, &hp->bp, hp->base);
      }
#endif
      break;
    case T_grep:
      if ((hp->grep = load_grep(hp->grep))) strcpy(ap->gstr, hp->grep->str);
      pnp->row = pnp->top = 0;
      break;
    case T_man:
      if (!is_prev(lp->prev, M_egrep) && !is_prev(lp->prev, M_fgrep))
	alloc_pipe(-1, pinp, NULL, NULL, 0);
      if (!pnp->nrow) np->ll = -1;
      break;
    case T_pipe4:
    case T_pipe5:
      if (!is_prev(lp->prev, M_egrep) && !is_prev(lp->prev, M_fgrep))
	alloc_pipe(-1, pinp, NULL, NULL, 0);
      pnp->row = pnp->top = 0;
      break;
    case T_dir:
      strcpy(tmp, ap->name);
      break;
    }
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if ((is_prev(lp->prev, M_egrep) || is_prev(lp->prev, M_fgrep))
	&& !hp->bp->pp)
      clear_stack(hp, lp, ap->name, &np->M, 'g');
    if (is_name(ap->name, M_shell)) return -M_shell;
    if ((is_fm()) 
	&& !strcmp(ap->name, s_S(S_help))) np->Mb = T_skip;
    else if (np->M == T_du) {
      if (strcmp(lp->dir, ap->du)) {
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->du);
      }
      chdir(lp->dir);
    } else if (*lp->prev == K_z && hp->bp == &hp->base[1]) {
      clear_cut(&lp->cut);
      if (*tmp && !strcmp(tmp, ap->name)) {
	strcpy(ap->name, lp->old);
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->name);
      }
      stat(ap->name, &st);
      np->Mb = T_skip;
      do {
	if (fp->winch) {
	  fp->winch = 0;
	  continue;
	} else if (!hp->bp->pp) ; /* (3) */
	else if (st.st_ctime == st0.st_ctime && st.st_ino == st0.st_ino)
	  continue;
	np->Mb = 0;
      } while (0);
    } else np->Mb = (hp->bp && hp->bp != &hp->base[0]) ? T_skip : 0;
  } else if (!strcmp(lp->old, lp->dir) 
	     && !strcmp(lp->dir, ap->name)) {
    getcwd(tmp, N_line);
    if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
    if (strcmp(lp->dir, tmp)) chdir(lp->dir);
    np->Mb = T_skip;
  } else {
    strcpy(ap->name, lp->old);
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, ap->name);
  }
  return 0;
}

static int get_m(HEAP *hp, ARRAY *ap, FLAG *fp, NUM *np, LINE *lp, RV *rvp, FILE **pinp, PN *pnp, CHILD *chp)
{
  char tmp[N_line];
  int i, m;
  PN pn_bak;
  STR *sp;
  static struct stat st0;
  static PN pn_mark;

  if (np->M == T_dir) stat(ap->name, &st0);
  do {
    switch(np->Mb) {
    case T_exec:
      m = -M_bl;
      break;
    case M_copy:
      make_map(rvp, np->nlen);
    default:
      m = read_m(rvp, np->Mb, np->klen, NULL, ap->cname
		 , &hp->ll.p.list, hp->ss.p.hist, ap->iname, lp->dir, lp->old);
      break;
    }
    do {
      if (m < 0) m = -m;
      else if (m > N_num) {
	if (hp->bp->pp->f.code) echo_mess(S_errv);
	else {
	  m_num(np, m, hp, pinp, pnp, ap->full);
	  m = -T_disp;
	}
	continue;
      }
      switch(m) {
      case 0:
      case M_clear: /* ^L */
	m_clear(rvp, pnp, lp, np, ap, hp, fp);
	break;
      case T_0: case T_1: case T_2: case T_3: case T_4: /* ^J? */
      case T_5: case T_6: case T_7: case T_8: case T_9: 
      case M_m: /* ^O^K */
      case M_sel: /* ^J^K */
	m = m_dir(m, hp, lp, ap->name, rvp, ap, &np->M);
	break;
      case M_cp: /* ^X^Kc */
      case M_mv: /* ^X^Km */
      case M_rm: /* ^X^Kr */
      case M_arc: /* ^X^Ka */
	fp->comm = 1;
      case M_cp2: /* ^Xc */
      case M_mv2: /* ^Xm */
      case M_rm2: /* ^Xr */
      case M_arc2: /* ^Xa */
	if (is_inter(NULL, 0, 0)) {
	  echo_mess(S_errf);
	  continue;
	}
	if (!is_comm(m, 0)) {
	  fp->comm = 2;
	  m = is_comm(m, 3);
	}
	if ((i = m_arc(m, np->Mb)) < 0) break;
	m = i; 
      case M_tcb: /* ^T */
      case M_error: /* ^X^Ke */
      case M_log: /* ^X^Kl */
      case M_exit: /* ^X^C */
	if ((m = m_exit(m, hp, lp, ap, &np->M, fp->comm)) && fp->comm) 
	  fp->comm = 0;
	break;
      case T_exit:
	vt_move(vt_row(0) - 1, 0);
	return -1;
	break;
      case M_mark: /* ^Space */
	memcpy(&pn_mark, pnp, sizeof(PN));
	if (np->Mb != M_locate && np->Mb != T_locate) {
	  disp_window(rvp->ps, rvp->pp, np->M, NULL, NULL, NULL, &i);
	  print_line(F2, NULL, 0, NULL);
	}
	break;
      case M_jump: /* ^X^X */
	memcpy(&pn_bak, pnp, sizeof(PN));
	memcpy(pnp, &pn_mark, sizeof(PN));
	memcpy(&pn_mark, &pn_bak, sizeof(PN));
	m = -T_disp;
	continue;
      case M_prev: case M_next: /* k, j */
      case M_up: case M_dn: /* ^P, ^N */
      case M_kprev: case M_knext: /* left-arrow, right-arrow */
      case M_kup: case M_kdn: /* up-arrow, down-arrow */
	m_row(m, hp, rvp, pnp, np, pinp);
	break;
      case M_forw2: case M_back2:
	m = (m == M_forw2) ? M_forw : M_back;
      case M_forw: case M_back: case M_kforw: case M_kback:
      case M_head: case M_tail:
	/* ` ', b, ^V, ^[v, ^[<, ^[> */
	m_page(m, hp, rvp, pnp, np, pinp);
	break;
      case M_w4: /* ^X^W */
	i = send_pp(hp->bp->pp, "a");
	save_intr(S_erra);
	sprintf(tmp, " %s; %d line(s)", s_S(S_write), i);
	print_intr(NULL, tmp);
	continue;
	break;
      case M_w: /* ^Jw */
      case M_w2: /* ^J^W */
      case M_chdir: /* ^J, */
      case M_chdir2: /* ^J. */
	if (is_inter(NULL, 0, 0)) echo_mess(S_errf);
	else if ((hp->w = m_w(m, hp->bp->pp, ap->name, lp->dir, ap->comm))) {
	  *ap->mess = '\0';
	  m = -M_shell;
	}
	break;
      case M_locate2: /* ^Jl */
      case M_locate: /* ^J^L */	
	m = m_locate(m, ap, hp, np, lp, rvp);
	break;
      case M_fgrep: /* ^S */
      case M_egrep: /* ^J^S */
	m = m_grep(m, ap, lp, fp, hp, np);
	break;
      case T_fcut2:
      case T_fcut:
      case M_cut2: /* ^Jc */
      case M_cut: /* ^J^C */
	m = m_cut(m, ap, hp, np, pnp, pinp, lp);
	break;
      case M_man: /* ^J^M */
      case M_man2: /* ^Jm */
	m = m_man(m, hp, lp, &np->M, ap->name);
	break;
      case M_quit: /* q */
	vt_move(vt_row(0) - 1, 0);
	return -1;
	break;
      case M_ls: /* ^Ol */
	if (*lp->prev != K_z) {
	  echo_mess(S_errv);
	  continue;
	}
	fp->ls = !fp->ls;
	conf_ls(NULL, utoa(fp->ls));
	m = 0;
	break;
      case M_hs: /* ^Oh */
	if (!vt_mode('D')) vt_mode('d');
	if (vt_mode('H')) {
	  vt_mode(-'h');
	  vt_cs(vt_row(0) - 2, 0);
	} else {
	  vt_mode('h');
	  vt_cs(vt_row(0) - 1, 0);
	}
	m = -((fp->comm == 4) ? M_shell : M_clear);
	continue;
	break;
      case T_winch:
	fp->winch = 1; 
	if (np->M == T_dir || is_str(np->M, &i) || is_list(np->M, &i)) {
	  m = (fp->comm == 4) ? T_winch : -T_disp;
	  free_bp(hp->bp);
	} else {
	  i = 0;
	  if (np->M == T_tar || np->M == T_lha || np->M == T_unzip
	      || np->M == T_rpm || np->M == T_deb)
	    hp->bp->pp = alloc_head(NULL, -1, 0, NULL); /* (5) */
 	  else if (!init_col(np->M, NULL, 0)) { 
	    free_pp(hp->bp->pp);
	    hp->bp->pp = alloc_pp(hp->bp->buff, -1
				  , hp->bp->size, NULL, ap->name); /* (6) */
	  } else {
	    i = hp->bp == &hp->base[4];
	    pnp->nrow = alloc_winch(hp->bp, i);
	  }
	  if (i) {
	    if (*pinp && *pinp != F1)
	      for (m = vt_row(0) - pnp->nrow; m > 0 && *pinp; m--)
		alloc_pipe(0, pinp, &pnp->nrow, NULL, 0);
	    else i = 0;
	  }
	  set_rv(np, pnp, rvp, hp
		 , ((m = pnp->nrow - (vt_row(0) - !i)) > 0) ? m : 0);
	  if (rvp->col < 0) rvp->col = 0;
	  m = (fp->comm == 4) ? T_winch : -M_clear;
	}
	if (fp->comm == 4) {
	  fp->comm = 0;
	  read_m(NULL, 2, 0, mtoa(M_shell)
		 , NULL, NULL, NULL, NULL, NULL, NULL);
	}
	continue;
	break;
      case M_help: /* ^H */
	if (is_pg()) m = M_key;
      case M_key:
	m_help(m, ap, lp, hp, np);
	m = 0;
	break;
      case M_copy: /* ^Oc */
	m_copy(lp, np, hp, ap->name);
	m = 0;
	break;
      case M_prog2: /* ^Jp */
      case M_lfile2: /* ^Xf */
      case M_ldir2: /* ^Xd */
      case M_lfile: /* ^X^F */
      case M_ldir: /* ^X^D */
      case M_prog: /* ^J^P */
	m = m_prog(m, lp, np, hp, ap->name);
	break;
      case T_cmd2:
      case T_cmd3:
      case T_cmd:
	if (m != T_cmd2) make_cmd(hp->ss.p.cmd, &hp->ll.p.cmd);
	m = T_cmd;
      case M_hist: /* ^R */
      case T_hist2:
	if (m == T_hist2) m = T_hist;
      case T_hist:
      case M_cmd: /* ^J^R */
      case T_list:
      case M_list: /* ^I */
	m = m_list(m, hp, np, ap, lp);
	break;
      case M_edit: /* ^Je */
	if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))
	  clear_stack(hp, lp, ap->name, &np->M, 'g');
	m = m_edit(pnp->line, ap->name, lp->arg, hp, np);
	break;
      case M_hup: /* ^Xs */
	if (is_inter(NULL, 0, 0)) echo_mess(S_errf);
	else {
	  is_inter(F12, 0, 0);
	  m = -M_shell;
	}
	break;
      case M_shell: /* ^[s */
	strcpy(rvp->str, "");
	m = m_shell(m, hp, lp, np, ap, pnp, pinp, fp, st0);
	break;
      case M_du: /* ^Jd */
	free_bp(&hp->base[2]);
	if ((i = fork_du(lp->dir)) < 0) {
	  echo_mess(S_errj);
	  continue;
	}
	np->fd = i;
	strcpy(ap->du, lp->dir);
	break;
      case T_du2:
      case M_du2: /* ^J^D */
	if (m == T_du2) m = M_du;
	m = m_du(m, hp, ap, np, lp, pinp, &st0);
	break;
      case T_cr:
	strcpy(ap->lstr, rvp->str);
	np->M = T_locate;
	m = 0;
	break;
      case M_yes: /* ^X^Y */
      case M_no: /* ^X^N */
	m = m_yn(m, hp->bp->pp, np->Mb, lp->prev);
	break;
      case M_dest: /* ^X^M */
	m = m_dest(m, lp, rvp, np, hp, ap, fp);
	break;
      case M_bl2: /* ^J^J */
	if (!(sp = hp->ss.p.hfile)) {
	  echo_mess(S_err2);
	  continue;
	}
	if (sp->next) {
	  if (!(i = (*ap->name == '/' && strchr(ap->name, '\0')[-1] == '/')
		|| *ap->name == ' ')
	      && *ap->full == '/' && strchr(ap->full, '\0')[-1] != '/')
	    hp->ss.p.hfile = save_history(sp, ap->full, 1);
	  while (sp->next->next) sp = sp->next;
	  if (i) sp = sp->next;
	}
	np->Mb = M_lfile;
	strcpy(rvp->str, sp->name);
	m = M_cr;
      case M_cr: case M_code: case M_fp: case M_fcut: case M_fcut2: 
	/* ^M, ^[^M, ^O^M, ^J^B, ^Jb */
	if ((m = m_cr(m, np, hp, lp, ap, rvp, fp, pnp, pinp)) == -T_exit)
	  return -1;
	break;
      case T_disp:
	np->Mb = (hp->bp && hp->bp->pp) ? T_skip : 0;
	m = 0;
	break;
      case M_bl: /* ^G */
	m = m_bl(m, np, ap, lp, hp, fp, pinp, pnp, st0);
	break;
      }
    } while (m < 0);
  } while (m);
  switch(np->M) {
  case M_rm:
  case M_cp:
  case M_mv:
  case M_arc:
    break;
  }
  save_ll(np->ll, &hp->ll, &hp->ss, ap->full, pnp);
  if (load_intr() == S_errg) unlink(s_S(S_intr));
  return 0;
}

static int init_menu(HEAP *hp, ARRAY *ap, NUM *np, LINE *lp, FILE **pinp)
{
  char tmp[N_line];
  struct stat st;
  int i, j;
  CHILD *chp;
  static PN pn;

  hp->child = init_child();
  np->history = 200;
  sprintf(tmp, "%d", np->history);
  save_history(F6, tmp, 0);
  vt_clear();
  if (is_fm()) {
    get_child(hp->child, NULL, NULL, NULL, NULL);
    if (init_config(hp->child, &hp->ll.p.prog, &hp->ll.p.grep
		    , &hp->ss.p.prog, &hp->ss.p.grep, &hp->bsize
		    , &ap->key_m, &ap->key_c, ap->name, ap->comm) < 0)
      return -1;
    save_history(F6, tmp, 0);
    sscanf(tmp, "%d", &np->history);
    if ((j = *ap->name)) {
      if (j != '/') {
	getcwd(lp->dir, 128);
	if (strchr(lp->dir, '\0')[-1] != '/') strcat(lp->dir, "/");
	strcat(lp->dir, ap->name);
	strcpy(ap->name, lp->dir);
      }
      if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
      erase_dot(ap->name);
      strcpy(lp->dir, ap->name);
    } else if (vt_set(0) & 0x80) {
      read_list(&hp->ll, &hp->ss, lp, ap->name);
      save_history(hp->ss.p.hist, tmp, -1);
      hp->ss.p.hist = free_history(hp->ss.p.hist, tmp, np->history);
      save_history(hp->ss.p.hfile, tmp, -1);
      hp->ss.p.hfile = free_history(hp->ss.p.hfile, tmp, np->history);
      save_history(hp->ss.p.hdir, tmp, -1);
      hp->ss.p.hdir =  free_history(hp->ss.p.hdir, tmp, np->history);
      save_history(hp->ss.p.hman, tmp, -1);
      hp->ss.p.hman =  free_history(hp->ss.p.hman, tmp, np->history);
      save_history(hp->ss.p.hlocate, tmp, -1);
      hp->ss.p.hlocate =  free_history(hp->ss.p.hlocate, tmp, np->history);
      save_history(hp->ss.p.hfgrep, tmp, -1);
      hp->ss.p.hfgrep =  free_history(hp->ss.p.hfgrep, tmp, np->history);
      save_history(hp->ss.p.hegrep, tmp, -1);
      hp->ss.p.hegrep =  free_history(hp->ss.p.hegrep, tmp, np->history);
      print_pid(getpid());
    }
    if (!(i = *lp->dir) || j) {
      pn.nrow = 10000;
      if (!j) getcwd(lp->dir, 128);
      hp->ll.p.man = save_list(hp->ll.p.man, s_S(S_tcb), &pn);
      if (strchr(lp->dir, '\0')[-1] != '/') strcat(lp->dir, "/");
      strcpy(lp->old, lp->dir);
      sprintf(lp->arg, "%c%c", K_z, 0);
      strcpy(lp->prev, lp->arg);
      strcpy(ap->name, lp->dir);
    }
    if (j) np->M = T_dir;
    else {
      hp->sp = save_stack_dir(hp->sp, lp->dir, lp->old, &hp->base[1]);
      load_list(hp->ll.p.dir, lp->dir, &pn);/*load pn if lp->dir exist */
      hp->ss.p.dir = save_str(hp->ss.p.dir, lp->dir, &pn);
      hp->ss.p.hdir = save_history(hp->ss.p.hdir, lp->dir, 1);
      hp->ll.p.dir = save_list(hp->ll.p.dir, lp->dir, &pn);
      if (!i) {
	strcpy(lp->prev, lp->dir);
	strcpy(ap->name, s_S(S_help));
	np->M = M_help;
      } else if (strstr(lp->arg, s_S(S_man))) np->M = T_man;
      else {
	if ((i = stat(ap->name, &st)) < 0) strcpy(ap->name, lp->dir);
	chdir(lp->dir);
	if ((np->M = (i || S_ISDIR(st.st_mode)) ? T_dir : T_file) == T_dir) {
	  if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
	  hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	} else if (*lp->cut.str) {
	  if (lp->cut.chp) {
	    ins_q(lp->cut.str);
	    sprintf(tmp, "%s%s\r", mtoa(M_cut2), lp->cut.str);
	  } else {
	    strcpy(lp->cut.name, ap->name);
	    np->M = T_cut;
	  }
	}
      }
    }
    if (np->M != M_help && *lp->cut.str) 
      vt_printf("Reading %s \042%s\042 ... ", ap->name, lp->cut.str);
    else vt_printf("Reading %s ... ", ap->name);
    vt_flush();
    if (*lp->cut.str && np->M != T_cut) 
      read_m(NULL, 2, 0, tmp, NULL, NULL, NULL, NULL, NULL, NULL);
    alloc_sh(NULL, 0, hp->bsize);
  } else {
    sprintf(lp->arg, "%c%c", K_z, 0);
    sprintf(lp->prev, "%c%c", K_y, 0);
    hp->child = save_child(hp->child,s_P(P_child)[S_egrep - S_ls], 0, NULL);
    hp->child = save_child(hp->child,s_P(P_child)[S_fgrep - S_ls], 0, NULL);
    save_child(F10, NULL, 0, NULL);
    get_child(hp->child, NULL, NULL, NULL, NULL);
    if (init_config(hp->child, NULL, &hp->ll.p.grep, NULL, NULL, &hp->bsize
		    , &ap->key_m, &ap->key_c, ap->name, ap->comm) < 0)
      return -1;
    if (vt_set(0) & 0x04) {
      np->M = T_key;
      strcpy(ap->name, s_P(P_mode)[M_key]);
    } else if (vt_set(0) & 0x08) {
      np->M = T_cat;
      strcpy(ap->name, s_P(P_mode)[M_key]);
    } else if ((np->M = (*ap->name == ' ') ? T_pipe : T_pipe3) == T_pipe3) {
      if ((chp = check_ext(ap->name, NULL, 0)) && (i = is_chp(chp, NULL))) {
	sprintf(lp->arg, " (%s)", chp->argv[0]);
	*pinp = fork_pipe(i, chp, ap->name);
      } else *pinp = fopen(ap->name, "r");
    }
    strcpy(ap->mess, ap->name);
    vt_printf("Reading %s ... ", ap->name);
    vt_flush();
  }
  i = -1;
  alloc_buff(&i, 0, hp->bsize);
  alloc_pipe(-2, NULL, NULL, NULL, hp->bsize);
  set_pp(NULL, hp->bsize, NULL, 0, 0);
  return conf_ls(NULL, NULL);
}

static void init_bp(NUM *np, HEAP *hp, ARRAY *ap, CHILD **chpp, FILE **pinp, CUT *cutp, u_char prev[], char arg[], RV *rvp, FLAG *fp)
{
  int c, i;

  if (np->M != M_copy) echo_mess(2);
  if ((np->ll = is_str(np->M, &i)))
    hp->bp = init_str(np->M, &hp->base[i], (STR*)hp->ss.a[np->ll], ap->name);
  else if ((np->ll = is_list(np->M, &i))) {
    if (np->M == M_prog) sprintf(ap->cname, "\047%s\047", prev);
    hp->bp = init_list(np->M, &hp->base[i], (LIST*)hp->ll.a[np->ll], ap->name);
  } else {
    switch(np->M) {
    case M_cut2:
    case M_man2:
    case M_fgrep:
    case M_locate:
    case M_prog2:
    case M_lfile2:
    case M_ldir2:
    case M_copy:
      *chpp = NULL;
      break;
    case M_key:
    case T_key:
      free_bp(hp->base);
      hp->bp = hp->base;
      init_bin(hp->bp, NULL, chpp, np->M, ap->name, 0);
      break;
    case M_tcb:
    case M_error:
    case M_log:
      hp->bp = calloc(sizeof(BASE), 1);
      init_bin(hp->bp, NULL, chpp, np->M, ap->name, 0);
      break;
    case M_help:
    case T_help:
      hp->bp = &hp->base[7];
      init_cut2(hp->bp, NULL, set_help(NULL), "^  [1-6][1-6]+\\.");
      np->M = T_cut;
      break;
    case T_buff:
      if (fp->cr <= 1) {
	hp->bp = calloc(sizeof(BASE), 1);
	if ((*chpp = init_arc(hp->bp, hp->arc, ap->name
			      , hp->bsize, prev, arg, rvp->pp, fp->cr)) 
	    == F9) {
	  np->M = T_exec2;
	  *chpp = NULL;
	}
      } else {
	hp->bp = &hp->base[4];
	alloc_pipe(-1, pinp, NULL, NULL, 0);
	if ((*pinp = init_arc2(hp->bp, hp->arc, ap->name, rvp->pp
			       , prev, arg)) != F1) {
	  np->nlen = init_pipe(hp->bp, pinp, T_pipe3, ap->name, prev, arg
			       , &np->klen, chpp, np->ppid);
	  np->M = T_pipe3;
	}
      }
      break;
    case T_split:
      c = 0;
      if ((i = hp->bp == &hp->base[7]) && !fp->help) fp->help = 1;
      if (fp->cr <= 1) {
	hp->bp = calloc(sizeof(BASE), 1);
	if (i || *cutp->name)
	  init_split2(hp->bp, cutp->buff, hp->bsize
		      , (i) ? (u_char*)set_help(NULL) : cutp->name
		      , rvp->pp, fp->cr);
	else 
	  init_split(hp->bp, ap->name, rvp->pp, cutp->bp, fp->cr);
      } else {
	hp->bp = &hp->base[4];
	alloc_pipe(-1, pinp, NULL, NULL, 0);
	if (i || *cutp->name) {
	  if ((*pinp = init_split4(hp->bp, cutp->buff
				   , (i) ? (u_char*)set_help(NULL) : cutp->name
				   , rvp->pp)) != F1) {
	    np->nlen = init_pipe(hp->bp, pinp, T_pipe3, ap->name, prev, arg
				 , &np->klen, chpp, 0);
	    np->M = T_pipe3;
	  }
	} else 
	  if ((*pinp = init_split3(hp->bp, ap->name
				   , rvp->pp, cutp->bp->pp)) != F1) {
	    np->nlen = init_pipe(hp->bp, pinp, T_pipe3, ap->name
				 , prev, arg, &np->klen, chpp, 0);
	    np->M = T_pipe3;
	  }
      }
      break;
    case T_du:
      break;
    case M_cp:
    case M_mv:
    case M_rm:
    case M_arc:
      if (*prev == '/') chdir(prev);
    case T_arc2:
    case M_exit:
      break;
    case T_dir:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      hp->bp = &hp->base[1];
      init_ls(hp->bp, ap->name, np->M, fp->ls);
      break;
    case M_du:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      hp->bp = &hp->base[2];
      init_bin(hp->bp, NULL, chpp, np->M, ap->name, np->fd);
      np->fd = 0;
      break;
    case T_locate:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      hp->bp = &hp->base[5];
      init_ls(hp->bp, rvp->str, np->M, 0);
      break;
    case T_man:
      hp->bp = &hp->base[4];
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      init_man(hp->bp, pinp, ap->name, prev, arg, &np->klen);
      break;
    case T_grep:
      rvp->pp = hp->bp->pp;
      hp->bp = calloc(sizeof(BASE), 1);
      init_grep(hp->bp, ap->name, ap->gstr, rvp->pp, (fp->grep == 2));
      *chpp = get_child(NULL, NULL
			, (fp->grep == 2) ? s_S(S_fgrep) : s_S(S_egrep)
			, NULL, NULL);
      break;
    case T_pipe3:
    case T_pipe:
    case T_pipe2:
      hp->bp = &hp->base[4];
      np->nlen = init_pipe(hp->bp, pinp, np->M, ap->mess, prev, arg
			   , &np->klen, chpp, np->ppid);
      break;
    case T_cut:
      if ((c = *cutp->name)) {
	hp->bp = calloc(sizeof(BASE), 1);
	if (c != ' ')
	  if (access(cutp->name, F_OK) < 0) 
	    make_bp(hp->bp, s_S(S_err3), cutp->name, NULL);
	  else init_cut2(hp->bp, NULL, cutp->name, cutp->str);
	else init_cut2(hp->bp, cutp->buff, cutp->name, cutp->str);
      } else {
	cutp->bp = hp->bp;
	hp->bp = calloc(sizeof(BASE), 1);
	init_cut(hp->bp, ap->name, cutp->str, cutp->bp);
      }
      *chpp = NULL;
      break;
    default:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      if ((is_fm()) 
	   && (*chpp = (fp->cr) ? check_ext(ap->name, NULL, 0) : NULL))
	np->M = is_child(ap->name, *chpp);
      hp->bp = calloc(sizeof(BASE), 1);
      switch(np->M) {
      case T_exec:
	break;
      case T_tar:
      case T_lha:
      case T_unzip:
      case T_rpm:
      case T_deb:
	hp->arc = *chpp;
      default:
	np->M = init_bin(hp->bp, &hp->base[9], chpp, np->M, ap->name, fp->cr);
	break;
      }
    }
  }
}

void menu(char pathname[], FILE *pin)/*tcb*/
{
  /*
   *  base[?]
   *	0: M_key, M_lfile, M_ldir, M_log, M_prog, M_man
   *	1: T_dir
   *	2: M_du
   *	3: M_egrep
   *	4: T_pipe, T_pipe2, T_pipe3
   *	5: M_locate
   *	6: M_cmd, T_cmd, M_hist, T_hfile, T_hdir, T_hman, T_hlocate
   *	7: M_help
   *	8: M_list, T_list
   *	9: Header of T_tar, T_lha, T_unzip, T_rpm, T_deb
   *   10: M_cut	
   *   11: T_hfgrep, T_hegrep
   *
   *  T_pipe?
   *	 : " "
   *	2: " (stdin)"
   *	3: pipe or regular-file
   */
  int i;
  RV rv;
  CHILD *chp;
  static FLAG f;
  static PN pn;
  static LINE l;
  static HEAP h;
  static ARRAY a;
  static NUM n;
  
  strcpy(a.name, pathname);
  free_h(&h);
  if ((i = init_menu(&h, &a, &n, &l, &pin)) < 0) return; 
  f.ls = i;
  f.cr = 1;
  read_m(NULL, T_key2, 0, (u_char *)a.key_m, (char*)a.key_c
	 , NULL, NULL, NULL, NULL, NULL);
  signal(SIGSEGV, menu_clear);
  setjmp(Menu.jb);
  if ((n.Mb = Menu.f_Mb) == T_skip) {
    if (is_pg() || (n.M == T_dir && *l.prev == K_z)) {
      vt_move(vt_row(0) - 1, 0);
      save_stderr("%s: %s.\n", s_S(S_tcb), s_S(S_err8));
      exit_tcb();
      print_stderr(NULL);
      exit(exit_ALLOC(NULL));
    }
    sprintf(a.intr, ": %s", a.name);
    save_intr(S_err8);
    clear_stack(&h, &l, a.name, &n.M, K_z);
    n.Mb = 0;
  }
  n.clen = 0;
  n.error = load_intr(); /* for erre */
  do {
    chp = load_chp(l.arg);
    n.nlen = 1;
    *a.cname = '\0';
    if (n.Mb != T_skip || !h.bp->pp) {
      init_bp(&n, &h, &a, &chp, &pin, &l.cut, l.prev, l.arg, &rv, &f);
      if (n.M == T_exec2) {
	n.Mb = n.M = T_exec;
	continue;
      } else if ((n.Mb = n.M) == T_exec) do_exec(a.name, chp);
    } else n.Mb = n.M;
    if (n.M != T_exec) {
      pn.nrow = h.bp->nrow; /* (2) */
      if (!h.bp->pp) {
	make_bp(h.bp, s_S(S_err1), a.name, NULL);
	pn.nrow = h.bp->nrow = 0;
      }
    }
    if (h.bp->size < 0 || n.M == T_type || (h.bp->pp && h.bp->pp->f.code)) {
      n.ll = -1;
      pn.top = pn.row = 0;
    } else {
      n.ll = load_ll(chp, &a, l.prev, &l.cut, &h, &pn, n.M, &pin);
      if (h.bp->nrow < pn.nrow) h.bp->nrow = pn.nrow; /* ==> (2) */
    }
    if (n.Mb != T_exec) {
      int j = h.bp == &h.base[4] && pin && pin != F1;
      set_rv(&n, &pn, &rv, &h 
	     , ((i = pn.nrow - (vt_row(0) - !j)) > 0) ? i : 0);
      print_arg(l.arg, (h.bp == &h.base[4] || (rv.pp && rv.col >= 0)) 
		? -1 : h.bp->size, h.bp->buff, chp, n.M);
      if (n.M != T_pipe && n.M != T_pipe2 && n.M != T_pipe3)
	n.nlen = disp_window(rv.ps, rv.pp, n.Mb, a.name, 
			     l.prev, l.arg, &n.klen);
      else n.Mb = n.M = (n.M == T_pipe3) ? T_pipe5 : T_pipe4;
      if (rv.col == -1) rv.col = 0;
      if (is_comm(n.Mb, 1) || n.Mb == M_exit) 
	disp_prev(l.prev, l.dir, n.klen * ((n.Mb == M_exit) ? -1 : 1));
    }
    if (n.error) {
      save_intr(n.error);
      n.error = 0;
    }
    if (!f.help) {
      if (h.bp == &h.base[7]) {
	vt_move(0, 21);
	vt_printf("[ Press %sEnter%s key ]", vt_geta(A_md), vt_geta(A_me));
      }
    }
    print_intr(NULL, (*a.intr) ? a.intr : NULL);
    if (*a.intr) *a.intr = '\0';
  } while (!get_m(&h, &a, &f, &n, &l, &rv, &pin, &pn, chp));
  if (vt_set(0) & 0x80) {
    alloc_pipe(-1, &pin, NULL, NULL, 0);
    save_ll(n.ll, &h.ll, &h.ss, a.full, &pn);
    do {
      clear_stack(&h, &l, a.name, &n.M, 'g');
      if (h.bp == &h.base[7]) {
	h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	continue;
      }
      if (*a.name != ' ') {
	if (*a.name != '/') {
	  if (strstr(l.arg, s_S(S_man))) { /* T_man */
	    strcpy(l.prev, l.dir);
	    break;
	  } else ; /* file in T_tar, T_lha, T_unzip, T_rpm, T_deb */
	} else if (is_prev(l.prev, M_du)) {
	  if (n.M == T_dir) *l.prev = K_z;
	  else strcpy(l.prev, l.dir);
	  break;
	} else if (n.M != T_split && n.M != T_buff 
		   && (*l.prev == '/' || n.M == T_dir) && h.bp->nrow) 
	  if (!h.bp->pp || !h.bp->pp->f.code) break;
      }
      h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
    } while (h.sp);
    if (*l.cut.str)
      do {
	if (n.M != T_cut) ;
	else if (strchr(l.prev, '\0')[-1] != '/') {
	  h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	  if (n.M == T_buff)
	    h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	  if (n.M == T_tar || n.M == T_lha || n.M == T_unzip 
	      || n.M == T_rpm || n.M == T_deb)
	    h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	} else continue;
	*l.cut.str = '\0';
      } while (0);
    if (*a.name == '/' && *l.arg == K_z) /* != T_man */
      l.arg[1] = '\0';
    if (*l.prev == K_z) l.prev[1] = '\0';
    write_list(h.ll, h.ss, &l, a.name);
  }
  disp_window(NULL, NULL, -2, NULL, NULL, NULL, NULL);
  clear_cut(&l.cut);
  set_help(F13);
}

