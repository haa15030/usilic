#include "usilic.h"
#include <signal.h>
#include <termios.h>
static void rv_str(RV *rvp, int f_M)
{
  if (f_M == M_shell /* rvp->pp->cs == F3 : virtual line (SHELL prompt) */
      || !strlen(rvp->str)) return; /* T_file: rvp->str == "" */
  strcpy(rvp->str, &rvp->pp->cs[rvp->col]);
  if (!strlen(rvp->str)) strcpy(rvp->str, " ");
  rvp->str[vt_col(0) - rvp->col - (is_ls(f_M) != 0)] = '\0';
}

static int set_top(PP *pp)
{
  int top;

  for (top = 0; pp->prev; top++, pp = pp->prev) ;
  return top;
}

int move_page(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M)/*usilic*/
{
  FILE *pin;
  int i = 0, j, f_mark, top;

  pin = (f_M == M_egrep || f_M == M_fgrep || f_M == M_shell) ? NULL : *pinp;
  f_mark = fmark(f_M);
  switch(m) {
  case M_back:
  case M_kback:
  case M_head:
    if (!rvp->ps->prev) {
      while (rvp->pp->prev) {
	rvp->pp = rvp->pp->prev;
	rvp->row--;
	pnp->row--;
      }
      rv_str(rvp, f_M);
      if (pinp) *pinp = pin;
      pnp->line = rvp->pp->f.line;
      return 0;
    }
    while (rvp->row) {
      rvp->pp = rvp->pp->prev;
      rvp->row--;
      pnp->row--;
    }
    if (pnp->nrow > vt_row(0) - 1) {
      if (m == M_head)
	while (rvp->ps->prev) {
	  rvp->ps = rvp->ps->prev;
	  rvp->pe = rvp->pe->prev;
	  rvp->pp = rvp->pp->prev;
	}
      else  
	for (i = 0; i < vt_row(0) - 1 && rvp->ps->prev; i++) {
	  rvp->ps = (vt_mode('H')) 
	    ? sr_row(rvp->ps->prev, f_mark) : rvp->ps->prev;
	  rvp->pe = rvp->pe->prev;
	  rvp->pp = rvp->pp->prev;
	}
    }
    if (!vt_mode('H') || m == M_head) disp_pp(rvp->ps, f_mark);
    rv_str(rvp, f_M);
    break;
  case M_tail:
    if (pin && pin != F1) pin = seek_intr(pin, &pnp->nrow);
  case M_forw:
  case M_kforw:
    while (1) {
      if (!rvp->pe->next) {
	if (pin && pin != F1) {
	  for (i = pnp->nrow; pin && pnp->nrow - i < vt_row(0) - 2; ) 
	    alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	  if (pin) continue;
	}
	while (rvp->pp->next) {
	  rvp->pp = rvp->pp->next;
	  rvp->row++;
	  pnp->row++;
	}
	rv_str(rvp, f_M);
	if (pinp) *pinp = pin;
	pnp->line = rvp->pp->f.line;
	return 0;
      }
      break;
    }
    while (rvp->row < vt_row(0) - 2) {
      rvp->pp = rvp->pp->next;
      rvp->row++;
      pnp->row++;
    }
    if (pnp->nrow > vt_row(0) - 1) {
      if (m == M_tail)
	if (pin && pin != F1)
	  while (rvp->pe->next->next) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = rvp->pe->next;
	    rvp->pp = rvp->pp->next;
	  }
        else
	  while (rvp->pe->next) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = rvp->pe->next;
	    rvp->pp = rvp->pp->next;
	  }
      else
	for (i = 0; ; ) {
	  for (; i < vt_row(0) - 1 && rvp->pe->next; i++) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = (vt_mode('H')) 
	      ? sf_row(rvp->pe->next, f_mark) : rvp->pe->next;
	    if (!rvp->pp->next) break;
	    rvp->pp = rvp->pp->next;
	  }
	  if (!rvp->pe->next && pin && pin != F1) {
	    for (j = pnp->nrow; pin && pnp->nrow - j < vt_row(0) - i; )
	      alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	    continue;
	  }
	  break;
	}
    }
    if (!vt_mode('H') || m == M_tail) disp_pp(rvp->ps, f_mark);
    rv_str(rvp, f_M);
    break;
  }
  if (pinp) *pinp = pin;
  top = pnp->top;
  pnp->top = set_top(rvp->ps);
  pnp->line = rvp->pp->f.line;
  tcflush(vt_tfd(0), TCIFLUSH);
  return i;
}

void move_row(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M)/*usilic*/
{
  FILE *pin;
  int f_mark, top;

  pin = (f_M == M_egrep || f_M == M_fgrep || f_M == M_shell) ? NULL : *pinp;
  f_mark = is_comm(f_M, 0) || is_comm(f_M, 1);
  switch(m) {
  case M_prev:
  case M_kprev:
    if (!rvp->ps->prev) {
      while (rvp->pp->prev) {
	rvp->pp = rvp->pp->prev;
	rvp->row--;
	pnp->row--;
      }
      rv_str(rvp, f_M);
    }
    if (pnp->nrow <= vt_row(0) - 1) return;
    while (rvp->row) {
      rvp->pp = rvp->pp->prev;
      rvp->row--;
      pnp->row--;
    }
  case M_kup:
  case M_up:
    do {
      if (rvp->row) {
	rvp->row--;
	pnp->row--;
      } else if (rvp->ps->prev) {
	rvp->ps = sr_row(rvp->ps->prev, f_mark);
	rvp->pe = rvp->pe->prev;
      } else continue;
      rvp->pp = rvp->pp->prev;
    } while (0);
    rv_str(rvp, f_M);
    break;
  case M_next:
  case M_knext:
    if ((!pin || pin == F1) && !rvp->pe->next) {
      while (rvp->pp->next) {
	rvp->pp = rvp->pp->next;
	rvp->row++;
	pnp->row++;
      }
      rv_str(rvp, f_M);
    }
    if (pnp->nrow <= vt_row(0) - 1) return;
    while (rvp->row < vt_row(0) - 2) {
      rvp->pp = rvp->pp->next;
      rvp->row++;
      pnp->row++;
    }
  case M_kdn:
  case M_dn:
    do {
      if ((!pin || pin == F1) && pnp->row > pnp->nrow - 2 - (f_M == M_shell))
	continue;
      if (rvp->row < vt_row(0) - 2) {
	rvp->row++;
	pnp->row++;
      } else if (pin && pin != F1 && rvp->pe->next && !rvp->pe->next->next) {
	alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	rvp->pe = sf_row(rvp->pe->next, f_mark);
	rvp->ps = rvp->ps->next;
      } else if (rvp->pe->next) {
	rvp->pe = sf_row(rvp->pe->next, f_mark);
	rvp->ps = rvp->ps->next;
      } else continue;
      rvp->pp = rvp->pp->next;
    } while (0);
    rv_str(rvp, f_M);
    break;
  }
  if (pinp) *pinp = pin;
  top = pnp->top;
  pnp->top = set_top(rvp->ps);
  pnp->line = rvp->pp->f.line;
  tcflush(vt_tfd(0), TCIFLUSH);
}

PP *sr_row(PP *pp, int f_mark)/*usilic*/
{
  if (!vt_mode('D')) return pp;
  vt_move(0, 0);
  vt_sr();
  put_str(pp, f_mark);
  return pp;
}  

PP *sf_row(PP *pp, int f_mark)/*usilic*/
{
  if (!vt_mode('D')) return pp;
  if (vt_mode('H')) vt_move(vt_row(0) - 1, 0);
  else vt_move(vt_row(0) - 2, 0);
  if (vt_mode('H')) {
    vt_eeol();
    put_str(pp, f_mark);
    vt_sf();
  } else {
    vt_sf();
    vt_eeol();
    put_str(pp, f_mark);
  }
  return pp;
}  

int init_col(int f_M, char str[], int size)/*usilic*/
{
  int col, row;

  if (size < 0) return size;
  row = vt_row(0);
  col = 0;
  switch(f_M) { 
  case M_lfile: case M_ldir: 
  case M_cut: case T_cut:
  case M_prog: 
  case M_man:
  case M_egrep: case T_grep:
  case T_locate:
  case M_hist: case T_hist:
  case T_hfile: case T_hdir: case T_hman: case T_hlocate:
  case T_hfgrep: case T_hegrep:
  case M_cmd: case T_cmd:
  case M_list: case T_list:
  case M_du: case T_du:
  case M_key:
    col = 0;
    break;
  case T_dir:
  case T_tar:
  case T_lha:
  case T_unzip:
  case T_rpm:
  case T_deb:
    col = get_col(str);
    break;
  case M_cp:
  case M_mv:
  case M_rm:
  case M_arc:
  case T_arc2:
  case T_key:
  default:
    col = -1;
    break;
  }  
  return col;
}

int init_row(PP **ppp, PN *pnp, int tell, int etop)/*usilic*/
{
  int i, j, row;
  PP *pp;

  pp = *ppp;
  row = pnp->row;
  if (pnp->nrow > vt_row(0) - 1) {
    if (tell) {
      row = 0;
      if ((j = (pnp->top = tell - 1) > etop)) pnp->top = etop;
    } else j = 0;
    for (i = 0; i < pnp->top && pp->next; i++) pp = pp->next;
    if (j) for (i = tell - etop - 1; i > 0; i--) row++;
  } else if (tell) for (row = 0, i = tell - 1; i > 0; i--) row++;
  pnp->row = row;
  *ppp = pp;
  return row;
}

int get_col(char str[])/*usilic*/
{
  int col;
  char *cp, *p = alloca(strlen(str) + 1);

  strcpy(p, str);
  if ((cp = strstr(p, " -> "))) *cp = '\0';
  if ((cp = strchr(p, '\\'))) {
    while (*cp != ' ' && cp > p) cp--;
    col = cp - p + 2;
  } else col = (cp = strrchr(p, ' ')) ? cp - p + 2 : 1;
  return col - 1;
}

