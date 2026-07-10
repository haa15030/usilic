#include "tcb.h"
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include "alloc.h"

static struct {
  PP *pp0, *pp1;
  int n_attr, f_intr;
  ATTR attr[N_attr];
  u_char line[N_buff];
} Alloc;  

static u_char *save_attr(u_char *cp, int attr, int *n_attrp, int *np)
{
  int n = *np, n_attr = *n_attrp;

  if ((n_attr = *n_attrp) > N_attr - 3) return cp;
  switch(attr) {
  case A_md3:
    cp += 3;
    Alloc.attr[n_attr].n = n+1;
    Alloc.attr[n_attr++].attr = A_md3;
    Alloc.attr[n_attr].n = n+3;
    Alloc.attr[n_attr++].attr = -A_me;
    n += 3;
    break;
  case A_md2:
    cp += 4;
    Alloc.attr[n_attr].n = n+2;
    Alloc.attr[n_attr++].attr = A_md2;
    Alloc.attr[n_attr].n = n+4;
    Alloc.attr[n_attr++].attr = -A_me;
    n += 4;
    break;
  case A_md:
    cp += 2;
    Alloc.attr[n_attr].n = n+1;
    Alloc.attr[n_attr++].attr = A_md;
    Alloc.attr[n_attr].n = n+2;
    Alloc.attr[n_attr++].attr = -A_me;
    n += 2;
    break;
  case A_us2:
    cp += 4;
    Alloc.attr[n_attr].n = n+4;
    Alloc.attr[n_attr++].attr = A_us2;
    Alloc.attr[n_attr].n = n+6;
    Alloc.attr[n_attr++].attr = -A_ue;
    n += 4;
    break;
  case A_us:
    cp += 2;
    Alloc.attr[n_attr].n = n+2;
    Alloc.attr[n_attr++].attr = A_us;
    Alloc.attr[n_attr].n = n+3;
    Alloc.attr[n_attr++].attr = -A_ue;
    n += 2;
    break;
  case '<':
  case -'<':
    Alloc.attr[n_attr].n = n;
    Alloc.attr[n_attr++].attr = attr;
    Alloc.attr[n_attr++].attr = *cp;
    break;
  case '^':
  case -'^':
    Alloc.attr[n_attr].n = n;
    Alloc.attr[n_attr++].attr = attr;
    Alloc.attr[n_attr++].attr = *cp + '@';
    break;
  }
  *np = n;
  *n_attrp = n_attr;
  return cp;
}

int set_attr(ATTR attr[], u_char str[], int f_stand, int f_grep)/*tcb*/
{
  int i, n, n_attr, f_attr;
  u_char c, *cp, *end;

  end = strchr(str, '\0');
  for (cp = str, i = n = n_attr = f_attr = 0; cp < end && i < vt_col(0) - 1
       ; i++, n++, cp++)
    if ((c = *cp) == K_dl || (c == K_at && f_grep)
	|| (c < ' ' 
	    && (f_grep 
		|| (!f_grep && c != '\n' && c != K_i && c != K_h)))) {
      if (c == K_dl) c -= 0x80;
      else if (c == K_at) c = 0;
      attr[n_attr].n = n;
      attr[n_attr++].attr = (f_stand > 0) ? -'^' : '^';
      attr[n_attr++].attr = c + '@';
      f_attr = 1;
    } else if (c >= 0x80 && vt_charset(0) == C_ascii) {
      attr[n_attr].n = n;
      attr[n_attr++].attr = (f_stand > 0) ? -'<' : '<';
      attr[n_attr++].attr = c;
      f_attr = 1;
    } else if (f_attr) {
      f_attr = 0;
      attr[n_attr].n = n;
      attr[n_attr++].attr = (f_stand > 0) ? A_cs : A_ce;
      i++;
    }
  return n_attr;
}

static void esc_pipe(FILE **pinp, PP *pp)
{
  if (pp) free(pp);
  if (*pinp != stdin) {
    if (*pinp) fclose(*pinp);
    killpg(getpgrp(), SIGPIPE);
  }
  *pinp = NULL;
  print_line(NULL, F2, 0, NULL);
  save_intr(S_errl);
  save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
}

static int linein(FILE *fp, u_char buff[], int *lenp, int *f_binp)
{
  int i, c, len, code, f_nl;
  JIS jis;

  len = *lenp;
  for (c = 0; len < N_buff/2 && c != EOF && c != '\n'; len++) {
    /* N_buff/2: 1 byte kana ==> 2 byte euc kana */
    if (!(i = fread(&c, 1, 1, fp))) {
      c = EOF;
      len--;
    } else {
      if (!*f_binp && c < ' ' && !strchr(s_S(S_code), c)) *f_binp = 1;
      buff[len] = c;
    }
  }
  buff[len] = '\0';
  if (vt_charset(0) != C_ascii) {
    code = init_code(&len, buff, 0);
    if (*f_binp > 0 && code == C_sjis) ;
    else {
      f_nl = strchr(buff, '\n') != NULL;
      switch(code) {
      case C_sjis:
	len = toeuc(buff);
	break;
      case C_jis: case C_base64: case C_quote:
	jis.c = 0;
	if (code == C_base64) jis.f.e = 1;
	else if (code == C_quote) jis.f.q = 1;
	len = jistoeuc(len, buff, &jis);
	if (jis.f.q) buff[len++] = '\n';
	break;
      default:
	if (code < 0 && !*f_binp) *f_binp = 1;
	break;
      }
      if (f_nl && !strchr(buff, '\n')) {
	/* '\n' was erased when init_code() converted */
	/* `1 byte kana' into `EUC 2 bytes kana' */
	strcat(buff, "\n");
	len++;
      }
    }
  }
  *lenp = len;
  return c;
}

PP *alloc_pipe(int init, FILE **pinp, int *nrowp, BASE *bp0, int nlen0)/*tcb*/
{
  static PP *pp, *pp0, *pp1;
  static int nlen, bsize, i_b, f_bin, len, s_len, p_len;
  static BASE *bp;
  u_char c, *end, *ce, *cs;
  int c1, c2, h, i, j, n, n_attr, f_eof, f_kanji;
  
  switch(init) {
  case -2:
    bsize = nlen0;
    set_ppp(&pp1, &pp0, 1);
    return NULL;
    break;
  case -1:
    if (*pinp == F1) return NULL;
    free_bp(bp);
    if (*pinp != stdin) {
      if (*pinp) fclose(*pinp);
      killpg(getpgrp(), SIGPIPE);
    }
    *pinp = F1;
    print_line(NULL, F2, 0, NULL);
    return NULL;
    break;
  case 0:
    if (!*pinp || *pinp == F1) return NULL;
    break;
  default:
    pp0 = pp1 = NULL;
    bp = bp0;
    s_len = p_len = *nrowp = 0;
    nlen = (init == T_man) ? -1 : nlen0;
    vt_move(0, 0);
    print_line(pinp, F2, 0, NULL);
    f_bin = f_kanji = len = i_b = 0;
    break;
  }
  do {
    if (i_b) {
      memcpy(Alloc.line, &Alloc.line[len - i_b], i_b);
      len = i_b;
      i_b = 0;
    } else len = 0;
    f_eof = linein(*pinp, Alloc.line, &len, &f_bin);
    end = Alloc.line + len;
    c1 = c2 = 0;
    for (ce = cs = Alloc.line, i = n = j = f_kanji = n_attr = 0; ce < end; ) {
      c = *ce;
      if (f_bin <= 0 && c == '\t') 
	for (h = 7 - (i & 7); h && i < vt_col(0); h--, i++) ;
      else if (i <= vt_col(0) - 1 && c != '\n' 
	       && (c == K_dl
		   || (c < ' ' && f_bin <= 0 && c != K_h)
		   || (c < ' ' && f_bin > 0)
		   || (vt_charset(0) == C_ascii && c == K_h 
		       && (ce[-1] >= 0x80 
			   || (ce[-1] == K_h && ce[-2] >= 0x80))))) {
	if (++i >= vt_col(0)) continue;
	if (c == K_dl) c -= 0x80;
	save_attr(&c, '^', &n_attr, &n);
      } else if (ce < end-1) {
	if (c >= 0x80 && vt_charset(0) == C_ascii) {
	  save_attr(&c, '<', &n_attr, &n);
	  i += 3;
	}
	if (f_bin > 0) ;
	else if (c == '_') {
	  if (ce[1] == K_h) {
	    c1 = 1;
	    if (ce[2] != '_') {
	      ce = save_attr(ce, A_us, &n_attr, &n);
	      continue;
	    }
	  } else if (ce < end-3 && 
		     ce[1] == '_' && ce[2] == K_h && ce[3] == K_h) {
	    ce = save_attr(ce, A_us2, &n_attr, &n);
	    continue;
	  }
	} else if (c == K_h) {
	  c2 = 1;
	  if (ce[1] != K_h) {
	    if (ce[1] >= 0x80) ce = save_attr(ce, A_md3, &n_attr, &n);
	    else ce = save_attr(ce, A_md, &n_attr, &n);
	  } else ce = save_attr(ce, A_md2, &n_attr, &n);
	  continue;
	} else if (c1) {
	  if (c1 < 2) c1++;
	  else c1 = 0;
	} else if (c2) c2++;
	if (f_bin <= 0 && vt_charset(0) != C_ascii && i < vt_col(0)) {
	  if (is_euckana_1_byte(ce[0]) && is_euckana_2_byte(ce[1])) {
	    ce += 2;
	    n += 2;
	    i++;
	    continue;
	  }
	  if (is_euc_1_byte(*ce)) f_kanji++;
	}
      }
      if ((j = *ce == '\n') || i >= vt_col(0)) {
	if (!(pp = calloc(sizeof(PP), 1))) {
	  esc_pipe(pinp, pp);
	  return pp0;
	}
	if (n_attr) {
	  Alloc.attr[n_attr++].n = -1;
	  if (!(pp->attr = malloc(sizeof(ATTR) * n_attr))) {
	    free(pp);
	    esc_pipe(pinp, pp);
	    return pp0;
	  }
	  memcpy(pp->attr, Alloc.attr, sizeof(ATTR) * n_attr);
	}
	if (j) {
	  if ((h = ce - cs) < 0) h = 0;
	  if (!(pp->cs = malloc(h + 1))) {
	    if (pp->attr) free(pp->attr);
	    esc_pipe(pinp, pp);
	    return pp0;
	  }
	  pp->f.str = 1;
	  memcpy(pp->cs, cs, h);
	  pp->f.len = h;
	} else {
	  /* length more than vt_col(0) */
	  if ((h = ce - cs - c1) < 0) h = 0;
	  if (!(pp->cs = malloc(h + 1))) {
	    if (pp->attr) free(pp->attr);
	    esc_pipe(pinp, pp);
	    return pp0;
	  }
	  pp->f.str = 1;
	  memcpy(pp->cs, cs, h);
	  if (f_kanji % 2) pp->cs[h-1] = ' ';
	  ce = cs + h - (f_kanji % 2) - 1;
	  pp->f.len = h - f_kanji % 2;
	}
	pp->cs[h] = '\0';
	pp->prev = pp1;
	if (!pp0) pp0 = pp;
	else pp1->next = pp;
	pp1 = pp;
	if (pp->prev) pp->f.line = pp->prev->f.line + pp->prev->f.nl;
	else pp->f.line = 1;
	pp->f.nl = j;
	s_len += h;
	p_len += sizeof(PP) + ((n_attr) ? sizeof(ATTR) * n_attr : 0);
	if (s_len > bsize || p_len > bsize) { /* not enouth buffer */
	  if (*pinp != stdin) {
	    if (*pinp) fclose(*pinp);
	    killpg(getpgrp(), SIGPIPE);
	  }
	  *pinp = NULL;
	  print_line(NULL, F2, 0, NULL);
	  save_intr(S_err4);
	  return pp0;
	}
	cs = ce + 1;
	i = n = -1;
	n_attr = 0;
	f_kanji = 0;
	if (++*nrowp <= vt_row(0) - 1 && nlen >= 0) {
	  /* display up to the environment variable ``LINES''  */
	  vt_move(*nrowp-1, 0);
	  vt_eeol();
	  put_str(pp, 0);
	  print_line(pp0, pp0, -nlen, NULL);
	} else if (*nrowp == vt_row(0)) {
	  if (f_bin > 0) pp0->f.bin = 1;
	  else f_bin = -1;
	}
	if (j) break;
      }
      i++;
      ce++;
      n++;
    }
    if ((i = f_eof == EOF) || !pp0) {
      if (!i || (!j && ce == end && len)) { /* end of line is not '\n' */
	if ((pp = calloc(sizeof(PP), 1))) {
	  if (n_attr) {
	    Alloc.attr[n_attr++].n = -1;
	    if ((pp->attr = malloc(sizeof(ATTR) * n_attr)))
	      memcpy(pp->attr, Alloc.attr, sizeof(ATTR) * n_attr);
	    else {
	      esc_pipe(pinp, pp);
	      return pp0;
	    }
	  }
	  if ((h = ce - cs) < 0) h = 0;
	  if ((pp->cs = malloc(h + 1))) {
	    pp->f.str = 1;
	    memcpy(pp->cs, cs, h);
	    pp->f.len = h;
	    pp->prev = pp1;
	    if (!pp0) pp0 = pp;
	    else pp1->next = pp;
	    pp1 = pp;
	    if (pp->prev) pp->f.line = pp->prev->f.line + pp->prev->f.nl;
	    else pp->f.line = 1;
	    pp->f.nl = 0;
	    if (++*nrowp <= vt_row(0) - 1 && nlen >= 0) {
	      vt_move(*nrowp-1, 0);
	      vt_eeol();
	      put_str(pp, 0);
	      print_line(pp0, pp0, -nlen, NULL);
	    }
	  } else {
	    if (pp->attr) free(pp->attr);
	    esc_pipe(pinp, pp);
	    return pp0;
	  }
	}
      }
      if (*pinp != stdin) {
	if (*pinp) fclose(*pinp);
	killpg(getpgrp(), SIGPIPE);
      }
      *pinp = NULL;
      print_line(NULL, F2, 0, NULL);
    } else if (!j && ce == end) i_b = n;
  } while (0);
  return pp0;
}

static void alloc_l(LIST *list, int *rowp, int f_grep, int f_ls)
{
  if (list) {
    alloc_l(list->left, rowp, f_grep, f_ls);
    {
      PP *pp;
      if ((pp = calloc(sizeof(PP), 1))) {
	if ((pp->cs = strdup(list->name))) { 
	  int i, n, len;
	  u_char *p = alloca(strlen(pp->cs) + 1);
	  if ((Alloc.n_attr = set_attr(Alloc.attr, list->name, 0, f_grep))) {
	    Alloc.attr[Alloc.n_attr++].n = -1;
	    if ((pp->attr = malloc(sizeof(ATTR)*Alloc.n_attr)))
	      memcpy(pp->attr, Alloc.attr, sizeof(ATTR)*Alloc.n_attr);
	    else {
	      save_intr(S_errl);
	      save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
	      free(pp->cs);
	      free(pp);
	      return;
	    }
	  }
	  pp->prev = Alloc.pp1;
	  if (!Alloc.pp0) Alloc.pp0 = pp;
	  else if (Alloc.pp1) Alloc.pp1->next = pp;
	  Alloc.pp1 = pp;
	  pp->f.str = 1;
	  strcpy(p, pp->cs);
	  if (vt_charset(0) == C_ascii) {
	    for (i = n = 0, len = strlen(p); len; len--) {
	      if ((p[i] >= 0x80 && (n += 4) <= vt_col(0))
		  || (p[i] < ' ' && (n += 2) <= vt_col(0)) 
		  || ++n <= vt_col(0)) i++;
	      else break;
	    }
	    p[i] = '\0';
	    pp->f.len = strlen(p);
	  } else {
	    if (strlen(p) > vt_col(0)) p[vt_col(0)] = '\0';
	    pp->f.len = strlen(p);
	    if (count_k(p, pp->f.len)) pp->f.len--;
	  }
	  pp->f.line = (pp->prev) ? pp->prev->f.line + 1 : 1;
	  pp->f.nl = 1;
	  (*rowp)++;
	} else {
	  free(pp);
	  save_intr(S_errl);
	  save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
	  return;
	}
      }
    }
    alloc_l(list->right, rowp, f_grep, f_ls);
  }  
}

PP *alloc_list(LIST *list, int *rowp, int f_grep, int f_ls)/*tcb*/
{
  int n_row = 0;

  Alloc.pp0 = Alloc.pp1 = NULL;
  alloc_l(list, &n_row, f_grep, f_ls);
  *rowp = n_row;
  return Alloc.pp0;
}  

PP *alloc_str(STR s0[], int *rowp, int f_grep)/*tcb*/
{
  int i, n, len, n_attr;
  STR *sp;
  PP *pp, *pp1;
  static PP *pp0;

  pp = pp0 = pp1 = NULL;
  *rowp = 0;
  for (sp = s0; sp; sp = sp->next) {
    if ((pp = calloc(sizeof(PP), 1))) {
      if ((pp->cs = strdup(sp->name))) {
	u_char *p = alloca(strlen(pp->cs) + 1);
	n_attr = set_attr(Alloc.attr, sp->name, 0, f_grep);
	Alloc.attr[n_attr++].n = -1;
	if ((pp->attr = malloc(sizeof(ATTR)*n_attr)))
	  memcpy(pp->attr, Alloc.attr, sizeof(ATTR)*n_attr);
	else {
	  free(pp->cs);
	  free(pp); 
	  save_intr(S_errl);
	  break;
	}
	pp->prev = pp1;
	if (!pp0) pp0 = pp;
	else pp1->next = pp;
	pp1 = pp;
	pp->f.str = 1;
	strcpy(p, pp->cs);
	if (vt_charset(0) == C_ascii) {
	  for (i = n = 0, len = strlen(p); len; len--) {
	    if ((p[i] >= 0x80 && (n += 4) <= vt_col(0))
		|| (p[i] < ' ' && (n += 2) <= vt_col(0))
		|| ++n <= vt_col(0)) i++;
	    else break;
	  }
	  p[i] = '\0';
	  pp->f.len = strlen(p);
	} else {
	  i = count_k(p, vt_col(0)); /* FGREP_HISTORY etc. */
	  if (strlen(p) > vt_col(0) - i) p[vt_col(0) - i] = '\0';
	  pp->f.len = strlen(p);
	}
	pp->f.line = (pp->prev) ? pp->prev->f.line + 1 : 1;
	pp->f.nl = 1;
	(*rowp)++;
      } else {
	free(pp);
	save_intr(S_errl);
	break;
      }
    } else {
      save_intr(S_errl);
      break;
    }
  }
  if (load_intr() == S_errl)
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  return pp0;
}  

PP *alloc_pp(u_char buff[], int f_M0, int size, int *rowp, char name[])/*tcb*/
{  /* edit output from `f(e)grep -b(n)` or `ls -l` */
  int i, h, len, n_row, n_attr, n, f_M, f_kanji, f_ascii, f_ls, i_dir;
  u_int tell, st_size;
  u_char c, *cs, *cp, *ce, *end;
  PP *pp, *pp0, *pp1;
  static struct {
    int f_M, split;
    char *name;
  } tab[6];

  if (!tab[0].name) {
    tab[0].name = s_P(P_mode)[M_key];
    if (is_fm()) {
      tab[1].name = s_P(P_mode)[M_du];
      tab[2].name = s_P(P_mode)[M_locate];
      tab[3].name = set_help(NULL);
    }
  }
  if (f_M0 < 0) { /* <== menu.c (6) */
    for (i = 0; i < 4; i++) if (!strcmp(tab[i].name, name)) break;
    if (i == 4 && *name == '/') i++;
    f_M = tab[i].f_M;
    st_size = tab[i].split;
  } else {
    f_M = f_M0;
    for (i = 0, buff; i < size; i++) if (!buff[i]) buff[i] = ' ';
    /* 
     * When `egrep -n (-b)' outputted following:
     *   123:...
     *   456:...
     *   789:... 
     * CUT will split buffer (file) at the line-number (bytes):
     *   1 - 122 
     *   123 - 455
     *   456 - 788
     *   789 - st_size
     */
    st_size = (f_M == M_cut) ? *rowp : 0; /* *rowp: total-lines (bytes) */
  }
  end = buff + size;
  pp = pp0 = pp1 = NULL;
  tell = 0;
  len = n_row = 0;
  i_dir = 0;
  f_ls = 1;
  f_ascii = vt_charset(0) == C_ascii;
  cs = buff;
  if (f_M == T_dir) {
    if (size) {
      cs = (u_char*)&strchr(buff, '\n')[1]; /* delete the line "total ..." */
      if (conf_ls(NULL, NULL)) {
	cp = cs;
	while (1) {
	  cp = &strchr(cp, '\n')[1];
	  if ((ce = strstr(cp, "../")) 
	      && (char*)ce < strchr(cp, '\n')) {
	    i_dir = (int)ce - (int)cp;
	    break;
	  }
	}
      }
      cs += i_dir;
    }
  } else if (f_M == M_cut && strncmp(cs, "0:", 2) && strncmp(cs, "1:", 2)) {
    if ((pp = calloc(sizeof(PP), 1))) {
      if ((pp->cs = strdup("--"))) {
	pp->f.str = 1;
	pp->f.len = strlen(pp->cs);
	pp->prev = NULL;
	pp0 = pp1 = pp;
	pp->f.line = 1;
	pp->f.nl = 1;
	n_row++;
      } else {
	free(pp);
	return NULL;
      }
    } else return NULL;
  }
  for (c = i = n = n_attr = f_kanji = 0, ce = Alloc.line; cs < end; ) {
    /* skip string `NNNN:' (<== f(e)grep -n ...) */
    if ((f_M == T_cut || f_M == M_cut || f_M == M_key) && i > 10 && !len) {
      for (len = 0; Alloc.line[len] != ':' && len < 10; len++) ;
      if (len == 10) len = 0;
    }
    if ((c = *cs++) != '\n') {
      if (c == '\t') { /* convert '\t' to ' ' */
	if (f_M == M_du)
	  for (h = 7 - (i & 7); h && i < vt_col(0) + len; h--, i++) 
	    *ce++ = ' ';
        else c = ' ';
      }
      if (c == '_') {
	if (*cs == '_' && cs[1] == K_h && cs[2] == K_h) {
	  cs += 3;
	  continue;
	} else if (*cs == K_h) {
	  cs++;
	  continue;
	}
      } else if (c == K_h) {
	if (*cs == K_h && cs[1] >= 0x80) {
	  cs += 3;
	  continue;
	} else if (*cs >= 0x80) {
	  cs += 2;
	  continue;
	} else {
	  cs++;
	  continue;
	}
      }
      if (!f_ascii) {
	if (is_euckana_1_byte(c) && is_euckana_2_byte(*cs)) {
	  f_kanji++;
	  *ce++ = c;
	  c = *cs++;
	  n++;
	  i--;
	} else if (is_euc_1_byte(c)) f_kanji++;
      }
      *ce++ = c;
#if 1
      if (i < vt_col(0) + len - 1 && c < 0x80) {
#else
      if (i < vt_col(0) + len - 1) {
#endif
	i++;
	n++;
	continue;
      }
      while (cs < end && (c = *cs) != '\n') 
	/* 
	 * save string of file or directory up to '\n'. 
	 * (but display with the length of COLUMNS environment variable)
	 */
	*ce++ = *cs++;
      cs++;
      n = 0;
    }
    *ce = 0;
    cs += i_dir;
    if ((pp = calloc(sizeof(PP), 1))) {
      if (f_M == T_cut || f_M == M_cut || f_M == M_key) {
	strcpy(Alloc.line, tome(Alloc.line, 0));
	if (!(cp = strchr(Alloc.line, ':'))) {
	  pp->tell = tell;
	  cp = Alloc.line;
	} else {
	  *cp = 0;
	  tell = pp->tell = atoi(Alloc.line);
	  cp++;
	}
      } else cp = Alloc.line;
      if ((i = strlen(cp))) {
	u_char *p = alloca(i + 1);
	pp->cs = strdup(cp);
	pp->f.str = 1;
	strcpy(p, cp);
	if (f_ascii) {
	  for (i = n = 0, len = strlen(p); len; len--) {
	    if ((p[i] >= 0x80 && (n += 4) <= vt_col(0))
		|| (p[i] < ' ' && (n += 2) <= vt_col(0))
		|| ++n <= vt_col(0)) i++;
	    else break;
	  }
	  p[i] = '\0';
	  pp->f.len = strlen(p);
	} else {
	  if (strlen(p) > vt_col(0)) p[vt_col(0)] = '\0';
	  pp->f.len = strlen(p);
	  if (count_k(p, pp->f.len)) pp->f.len--;
	}
	if ((n_attr = set_attr(Alloc.attr, p, 0, 0))) {
	  Alloc.attr[n_attr++].n = -1;
	  if ((pp->attr = malloc(sizeof(ATTR)*n_attr)))
	    memcpy(pp->attr, Alloc.attr, sizeof(ATTR)*n_attr);
	  else {
	    free(pp->cs);
	    free(pp);
	    save_intr(S_errl);
	    break;
	  }
	}
	pp->prev = pp1;
	if (!pp0) pp0 = pp;
	else pp1->next = pp;
	pp1 = pp;
	if (pp->prev) pp->f.line = pp->prev->f.line + pp->prev->f.nl;
	else pp->f.line = 1;
	pp->f.nl = c == '\n';
	n = i = len = n_attr = f_kanji = 0;
	ce = Alloc.line;
	n_row++;
      } else {
	free(pp);
	save_intr(S_errl);
	break;
      }
    } else {
      save_intr(S_errl);
      break;
    }
  }
  if (load_intr() == S_errl)
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  if (f_M == M_cut) pp->split = st_size;
  if (rowp) *rowp = n_row;
  if (f_M0 >= 0 && f_M != T_dir) {
    for (i = 0; i < 4; i++) 
      if (tab[i].name && !strcmp(tab[i].name, name)) 
	break;
    if (i == 4 && *name == '/') i++;
    tab[i].f_M = f_M;
    tab[i].split = st_size;
  }
  return pp0;
}

PP *alloc_head(u_char buff0[], int f_M0, int size0, int *rowp)/*tcb*/
{
  int c, i, j, k, n_row, split;
  u_char *end, *cs, *ce, *cp;
  char *p;
  PP *pp, *pp1, *pp2 = NULL;
  static u_char *buff;
  static PP *pp0;
  static int f_M, size;

  if (f_M0 < 0) pp2 = pp0;
  else {
    f_M = f_M0;
    size = size0;
    buff = buff0;
  }
  pp0 = pp1 = NULL;
  cp = buff;
  end = cp + size;
  i = j = k = split = 0;
  n_row = 0;
  switch(f_M) {
  case T_lha:
    /* 
       skip two lines:
       "PERMSSN  UID GID    SIZE  RATIO     STAMP    NAME"
       "----------------- ------- ------ ------------ --------------------"
    */
    while (*cp++ != '\n') ;
    while (*cp++ != '\n') ;
    break;
  case T_unzip:
    /* 
       skip three lines:
       "Archive:  *.zip"
       " Length    Date    Time    Name"
       " ------    ----    ----    ----"
    */
    while (1) {
      for (i = 0; (Alloc.line[i] = *cp++) != '\n' && i < 256; i++) ;
      if (i == 256) Alloc.line[i-1] = '\0';
      if (!strncmp(Alloc.line, " --", 3)) break;
    } 
    break;
  }
  for (ce = Alloc.line; cp < end; ) {
    if ((c = *cp++) != '\n') {
      *ce = c;
      if (j < 255) {
	ce++;
	j++;
	continue;
      }
      *ce = 0;
      while (*cp++ != '\n') ;
    } else *ce = 0;
    switch(f_M) {
    case T_tar:
    case T_deb:
      if ((cs = strstr(Alloc.line, " -> "))) *cs = '\0';
      for (i = 0, ce = strchr(Alloc.line, '\0') - 1; ; i++) {
	for (; ce > Alloc.line; ce--)
	  if (*ce == ' ' || (i && *ce == '/')) break;
	if (!i) {
	  if (cs) *cs = ' ';
	  cs = &ce[1];
	}
	if (i && *ce == '/') {
	  while (1) {
	    while (*ce != ' ') ce++;
	    while (*ce == ' ') ce++;
	    for (p = ce; *p >= '0' && *p <= '9'; p++) ;
	    if (*p == ' ' || *p == ',') break;
	    else ce = p;
	  } 
	  break;
	} else while (*ce == ' ') ce--;
      }
      if (ce > Alloc.line) { /* get file size */
	for (k = split = 0; *ce != ' '; ce++) {
	  if (*ce == ',') split = -1; /* special device */
	  else if (!split) k = k * 10 + *ce - '0';
	}
	ce++;
      } else k = 0;
      ce = cs;
      break;
    case T_lha:
      /* search for string "NNN%" or "******" */
      for (ce = Alloc.line, i = 0; *ce != '%' && *ce != '*' && i < 40; i++) 
	ce++;
      if (strchr(Alloc.line, '\0')[-1] == '/') split = -1; /* directory */
      if (i == 40) {
	/*
	   within the line:
	   "----------------- ------- ------ ------------ --------------------"
	*/
	cp = end;
	continue;
      }
      cs = ce;
      for (i = 0; i < 4; i++) {
	while (*ce != ' ') ce++;
	while (*ce == ' ') ce++;
      }
      cs++;
      for (i = 0; i < 2; i++) {
	while (*cs == ' ') cs--;
	while (*cs != ' ') cs--;
      }
      cs++;
      /* get file size */
      for (k = 0; *cs != ' '; cs++) k = k * 10 + *cs - '0';
      break;
    case T_unzip:
      for (cs = Alloc.line; *cs == ' ' && *cs != '-'; cs++) ;
      if (*cs == '-') {
	/*
	   within the line:
	   "------                    -------"
	*/
	cp = end;
	continue;
      }
      /* get file size */
      for (k = 0; *cs != ' '; cs++) k = k * 10 + *cs - '0';
      for (ce = cs, i = 0; i < 2; i++) {
	while (*ce == ' ') ce++;
	if (!i) memcpy(Alloc.line, ce, 9);
	while (*ce != ' ') ce++;
      }
      while (*ce == ' ') ce++;
      break;
    case T_rpm:
      if ((cs = strstr(Alloc.line, " -> "))) *cs = '\0';
      for (ce = strchr(Alloc.line, '\0') - 1; ce > Alloc.line; ce--) 
	if (*ce == ' ') break;
      if (cs) *cs = ' ';
      cs = &ce[1];
      for (ce = Alloc.line, i = 0; i < 3; i++) {
	while (*ce != ' ') ce++; 
	while (*ce == ' ') ce++; 
      }
      if (*ce >= '0' && *ce <= '9') { /* get file size */
	for (k = split = 0; *ce != ' '; ce++) {
	  if (*ce == ',') split = -1; /* special device */
	  else if (!split) k = k * 10 + *ce - '0';
	}
	ce++;
      } else k = 0;
      ce = cs;
      break;
    }
    if ((pp = calloc(sizeof(PP), 1))) {
      if ((pp->split = split) < 0) k = 0; /* no size */ 
      else pp->split = k;
      sprintf(&Alloc.line[9], "%10d ", k);
      cs = &Alloc.line[20];
      while ((*cs = *ce++) != '\n' && *cs) cs++;
      if (*cs == '\n') *cs = '\0';
      if (*Alloc.line == 'd' || *Alloc.line == 'l')  /* directory or symlink */
	pp->split = -1;
      if ((pp->cs = strdup(Alloc.line))) {
	pp->f.len = ((i = strlen(pp->cs)) > vt_col(0)) ? vt_col(0) : i;
	pp->f.str = 1;
	pp->prev = pp1;
	if (!pp0) pp0 = pp;
	else pp1->next = pp;
	pp1 = pp;
	if (pp->prev) pp->f.line = pp->prev->f.line + pp->prev->f.nl;
	else pp->f.line = 1;
	pp->f.nl = c == '\n';
	j = split = 0;
	ce = Alloc.line;
	n_row++;
      } else {
	free(pp);
	save_intr(S_errl);
	break;
      }
    } else {
      save_intr(S_errl);
      break;
    }
  }
  if (load_intr() == S_errl)
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  if (f_M0 < 0) { /* <== SIGWINCH (menu.c:(5)) */
    for (pp = pp0, pp1 = pp2; pp; pp = pp->next, pp1 = pp1->next) {
      pp->tell = pp1->tell;
      pp->split = pp1->split;
    }
    free_pp(pp2);
  } else if (rowp) *rowp = n_row;
  return pp0;
}

void alloc_tar(u_char cp0[], PP pp0[], int size)/*tcb*/
{
  int i;
  u_char *cp, *end, *sp;
  PP *pp;

  end = cp0 + size;
  for (cp = cp0, pp = pp0, end--; ;) {
    if ((cp += 512) > end) /* skip header */
      cp = end;
    pp->tell = (u_int)cp;
    if (pp->split < 0) pp->split = 0;
    else {
      for (i = pp->split; i && cp < end; i--) cp++;
      if (cp == end) pp->split = -2;
    }
    if (!(pp = pp->next)) break;
    strcpy(Alloc.line, &pp->cs[20]);
    if ((sp = strchr(Alloc.line, ' '))) *sp = '\0';
    if (cp < end) {
      i = strlen(Alloc.line);
      while (strncmp(cp, Alloc.line, i)) /* search for next header */
	if (cp >= end) break;
	else cp++;
    }
  }
}

void alloc_lha(u_char cp0[], PP pp0[], int size)/*tcb*/
{
  int i;
  u_char *cp, *end;
  PP *pp;

  for (cp = cp0, end = cp + size, pp = pp0; ; ) {
    if (pp->split < 0) pp->split = 0;
    else
      for (i = 0; i < 3; i++)
	/*
	  skip three lines:
	  "::::::::"
	  "filename"
	  "::::::::"
	*/
	while (cp < end && *cp++ != '\n') ;
    pp->tell = (u_int)cp;
    if (pp->split > 0) {
      for (i = 0; i < pp->split && cp < end; i++) cp++;
      if (cp == end) pp->split = -2;
    }
    if (!(pp = pp->next)) break;
  }
}

void alloc_unzip(u_char cp0[], PP pp0[], int size)/*tcb*/
{
  int i;
  u_char *cp, *end;
  PP *pp;

  for (cp = cp0, end = cp + size, pp = pp0; ; ) {
    if (pp->split < 0) pp->split = 0;
    else {
      pp->tell = (u_int)cp;
      for (i = 0; i < pp->split && cp < end; i++) cp++;
      if (cp == end) pp->split = -2;
    }
    if (!(pp = pp->next)) break;
  }
}

char **alloc_arg(char *arg, int *argcp)/*tcb*/
{
  int c, i, j;
  char **argv, *cp, *p, *a;

  while (*arg == ' ') arg++;
  for (cp = arg; *cp; cp++) if (*cp == '\t') *cp = ' ';
  for (cp = arg, j = 2; *cp; ) { /* get number of argument */
    if ((c = *cp) == '\042' || c == '\047') 
      for (cp++; *cp && *cp != c; ) cp++; 
    while (*cp && *cp != ' ') cp++;
    while (*cp && *cp == ' ') cp++;
    if (*cp) j++;
  }
  if (argcp) *argcp = j;
  if ((argv = calloc(sizeof(char*), j))) {
    i = 0;
    cp = arg;
    a = NULL;
    do { /* set arguments for execvp() */
      while (*cp == ' ') cp++;
      if ((c = *cp) == '\042' || c == '\047') {
	for (a = ++cp; *a != c; a++) ;
	*a = '\0';
	if (a[1] == ' ') {
	  p = &a[1];
	  *p = '\0';
	} else p = NULL;
      } else if ((p = strchr(cp, ' '))) *p = '\0';
      if (!i) {
	if ((*argv = strdup(cp))) {
	  if (!argcp && !is_inst(s_S(S_path), *argv)) {
	    /* argv[0] isn't installed */
	    strcpy(arg, *argv);
	    free(*argv);
	    free(argv);
	    save_intr(S_errl);
	    break;
	  }
	} else {
	  free(argv);
	  save_intr(S_errl);
	  break;
	}
      } else if (!(argv[i] = strdup(cp))) {
	free_arg(argv);
	save_intr(S_errl);
	break;
      }
      if (j == ++i + 1) break;
      if (p) {
	*p = ' ';
	cp = &p[1];
      }
    } while (p);
    if (a) *a = c;
    if (load_intr() == S_errl) {
      save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
      return NULL;
    }
    argv[i] = NULL;
  } else {
    save_intr(S_errl);
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  }
  return argv;
}

void free_arg(char **argv)/*tcb*/
{
  int i;

  for (i = 0; argv[i]; i++) free(argv[i]);
  free(argv);
}

u_char *alloc_split(char name[], int tell, int size)/*tcb*/
{
  FILE *fp;
  u_char *cp;

  while (!(cp = malloc(size)) && (size /= 2)) ;
  if (cp && (fp = fopen(name, "r"))) {
    fseek(fp, tell, 0);
    fread(cp, 1, size, fp);
    fclose(fp);
  } else if (cp) {
    free(cp);
    cp = NULL;
  }
  return cp;
}

u_char *alloc_buff(int *sizep, u_int fd, int f_chkbin)/*tcb*/
{
  FILE *fp;
  int size, temp, fsize, code, f_getch = 1;
  u_char *cp, *buff;
  static int bsize;
  struct tmp {
    int len;
    u_char *buff;
    struct tmp *next;
  } *tp, *old, *tp0 = NULL;

  if (*sizep < 0) {
    bsize = f_chkbin;
    return NULL;
  }
  fsize = 0;
  switch(f_chkbin) {
  case 1:
    if (!*sizep) return NULL;
    else fsize = *sizep;
  case 2:
    f_getch = 0;
  case 0:
    fp = fdopen(fd, "r");
    if (f_chkbin == 2) f_chkbin = 0;
    break;
  case -1:
    fp = (FILE*)fd;
    fsize = *sizep;
    break;
  default:
    return NULL;
  }
  size = code = 0;
  old = NULL;
  errno = 0;
  tp = NULL;
  if (f_getch) vt_init(-3);
  do { /* read in N_buff bytes */
#if 0
    if (f_getch && vt_getch()) {
      killpg(getpgrp(), SIGPIPE);
      save_intr(S_err0);
      break;
    }
#endif
    if (!(tp = calloc(1, sizeof(struct tmp)))) {
      killpg(getpgrp(), SIGPIPE);
      save_intr(S_errl);
      break;
    }
    if (old) old->next = tp;
    else tp0 = tp;
    if (!(tp->buff = malloc(N_buff+2))) {
      killpg(getpgrp(), SIGPIPE);
      free(tp);
      if (old) old->next = NULL;
      else tp0 = NULL;
      save_intr(S_errl);
      break;
    }
    if (f_chkbin) {
      if (fsize >= N_buff) {
	tp->len = fread(tp->buff, 1, N_buff, fp);
	old = tp;
      } else {
	tp->len = fread(tp->buff, 1, fsize, fp);
	old = NULL;
      }
      fsize -= N_buff;
    } else 
      old = ((tp->len = fread(tp->buff, 1, N_buff, fp)) < N_buff) ? NULL : tp;
    if ((size += tp->len) >= bsize) { /* not enough buffer */
      killpg(getpgrp(), SIGPIPE);
      save_intr(S_err4);
    }
    if (f_chkbin == 1) {
      temp = size;
      /* seek binary-code in first N_buff bytes */
      if ((code = init_code(&size, tp->buff, 1)) < 0) old = NULL;
        else f_chkbin = -1; /* skip next check */
      size = temp;
    }
  } while (old && size < bsize);
  if (f_getch) vt_init(3);
  if (!size) buff = NULL;
  else {
    if (!(buff = calloc(1, size+2))) {
      save_intr(S_errl);
      /* `fp'(:fd) was closed at the end of read_buff() */
      fp = fopen(s_S(S_tmp), "w");
      for (tp = tp0, cp = buff; tp; cp += tp->len, tp = tp->next) 
	fwrite(tp->buff, 1, tp->len, fp);
      fclose(fp);
      fp = NULL;
    } else
      for (tp = tp0, cp = buff; tp; cp += tp->len, tp = tp->next) 
	memcpy(cp, tp->buff, tp->len);
  }
  for (tp = tp0; tp; tp = old) {
    free(tp->buff);
    old = tp->next;
    free(tp);
  }
  if (!fp) {
    while (!(buff = calloc(1, size+2)) && (size /= 2)) ;
    if (size) {
      fp = fopen(s_S(S_tmp), "r");
      fread(buff, 1, size, fp);
      fclose(fp);
    } else if (buff) {
      free(buff);
      buff = NULL;
    }
    unlink(s_S(S_tmp));
  }
  if (load_intr() == S_errl)
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  *sizep = size;
  return buff;
}

PP *alloc_sh(u_char *str, int pid0, int c)/*tcb*/
{
  PP *pp;
  static PP *pp0, *pp1;
  static int pid, s_len, bsize;

  if (!str) {
    bsize = c;
    return NULL;
  }
  if (pid0 < 0) {
    pp0 = pp1 = NULL;
    set_ppp(&pp1, &pp0, 1);
    pid = -pid0;
    s_len = 0;
  } else if (!pid) return NULL;
  if ((pp = calloc(sizeof(PP), 1))) {
    pp->prev = pp1;
    if (!pp0) pp0 = pp;
    else pp1->next = pp;
    pp1 = pp;
    if (pp->prev) {
      pp->f.line = pp->prev->f.line + pp->prev->f.nl;
      s_len += pp->prev->f.len;
    } else pp->f.line = 1;
    pp->f.nl = c == '\n' || c == '\\';
    if (str == F3) {
      pp->cs = F3; /* SHELL prompt (virtual line) */
      if (c == '\\') /* line continuation (prompt over plural line) */
	pp->prev->split = 1; /* (2) */
    } else {
      c = strlen(str);
      if (str[c-1] == '\n') str[c-1] = '\0';
      if ((pp->cs = strdup(str))) {
	pp->f.str = 1;
	pp->f.len = strlen(pp->cs);
	s_len = pp->f.len;
      } else {
	free(pp);
	save_intr(S_errl);
	kill(pid, SIGHUP);
	pid = 0;
      }
    }
  } else {
    save_intr(S_errl);
    kill(pid, SIGHUP);
    pid = 0;
  }
  if (load_intr() == S_errl)
    save_error("%s: %s: %s.\n"
	       , s_S(S_tcb), &s_P(P_mode)[M_shell][1], s_S(S_errl));
  return (s_len > bsize) ? F4 : pp0;
}  

void make_bp(BASE *bp, char mess[], char *name, char *cp)/*tcb*/
{ /* alloc string for error-message */
  free_bp(bp);
  strcpy(Alloc.line, mess);
  if (name) { 
    if (cp) save_error("%s: %s: %s (%s).\n", s_S(S_tcb), name, Alloc.line, cp);
    else save_error("%s: %s: %s.\n", s_S(S_tcb), name, Alloc.line);
  }
  if (cp) {
    strcat(Alloc.line, " (");
    strcat(Alloc.line, cp);
    strcat(Alloc.line, ")");
  }
  if ((bp->buff = strdup(toul(Alloc.line, 1)))) {
    if ((bp->pp = set_pp(bp->buff, strlen(bp->buff), &bp->nrow, 0, 0))) {
      bp->pp->tell = (u_int)F7; /* mark of error */
      bp->size = -1;
    } else {
      free(bp->buff);
      bp->buff = NULL;
    }
  }
}

PP *set_pp(u_char *cs, int size, int *rowp, int f_code, int f_bin)/*tcb*/
{
  int f_kanji, p_len;
  int c1, c2, h, i, j, n, n_row, n_attr;
  u_char c, *ce, *end;
  PP *pp, *pp0 = NULL, *pp1 = NULL;
  static int bsize;

  if (!cs) {
    if (size) bsize = size;
    return NULL;
  }
  end = cs + size;
  ce = cs;
  p_len = f_kanji = 0;
  c1 = c2 = 0;
  for (n = j = n_attr = 0, i = f_code, n_row = 0; ce < end; ) {
    c = *ce;
    if (f_code) ; /* ^[^M */
    else if (!f_bin && c == '\t')
      for (h = 7 - (i & 7); h && i < vt_col(0); h--, i++) ;
    else if (!f_bin && c == '\r' && ce[1] != '\n') c = '\n';
    else if (i <= vt_col(0) - 1 && c != '\n'
	     && (c == K_dl
		 || (c < ' ' && !f_bin && c != K_h)
		 || (c < ' ' && f_bin)
		 || (vt_charset(0) == C_ascii && c == K_h 
		     && (ce[-1] >= 0x80 
			 || (ce[-1] == K_h && ce[-2] >= 0x80)))))
      if (i >= vt_col(0) - 1) {
	i++;
	continue;
      } else {
	if (c == K_dl) c -= 0x80;
	save_attr(&c, '^', &n_attr, &n);
	i++;
      }
    else {
      if (c >= 0x80 && vt_charset(0) == C_ascii) {
	save_attr(&c, '<', &n_attr, &n);
	i += 3;
      }
      if (f_bin) ;
      else if (c == '_') { /* underline */
	if (ce[1] == K_h) {
	  c1 = 1;
	  if (ce[2] != '_') {
	    ce = save_attr(ce, A_us, &n_attr, &n);
	    continue;
	  }
	} else if (ce[1] == '_' && ce[2] == K_h && ce[3] == K_h) {
	  ce = save_attr(ce, A_us2, &n_attr, &n);
	  continue;
	}
      } else if (c == K_h) { /* bold */
	c2 = 1;
	if (ce[1] != K_h) {
	  if (ce[1] >= 0x80) ce = save_attr(ce, A_md3, &n_attr, &n);
	  else ce = save_attr(ce, A_md, &n_attr, &n);
	} else ce = save_attr(ce, A_md2, &n_attr, &n);
	continue;
      } else if (c1) {
	if (c1 < 2) c1++;
	else c1 = 0;
      } else if (c2) c2++;
    }
    if (!f_code && vt_charset(0) != C_ascii && i < vt_col(0)) {
      if (is_euckana_1_byte(ce[0]) && is_euckana_2_byte(ce[1])) {
	ce += 2;
	n += 2;
	i++;
	continue;
      }
      if (is_euc_1_byte(c)) f_kanji++;
    }
    if ((!f_code && (j = c == '\n')) || i >= vt_col(0)) {
      if ((pp = calloc(sizeof(PP), 1))) {
	pp->cs = cs;
	if (f_code) pp->f.code = 1;
	else if (n_attr) {
	  Alloc.attr[n_attr++].n = -1; /* mark end of attribute */
	  if ((pp->attr = malloc(sizeof(ATTR) * n_attr)))
	    memcpy(pp->attr, Alloc.attr, sizeof(ATTR) * n_attr);
	  else {
	    free(pp);
	    save_intr(S_errl);
	    ce = end;
	    continue;
	  }
	}
	pp->prev = pp1;
	if (!pp0) pp0 = pp;
	else pp1->next = pp;
	pp1 = pp;
	if (pp->prev) 
	  pp->f.line = pp->prev->f.line + ((f_code) ? 1 : pp->prev->f.nl);
	else pp->f.line = 1;
	pp->f.nl = j;
	p_len += sizeof(PP) + ((n_attr) ? sizeof(ATTR) * n_attr : 0);
	if (p_len > bsize) {
	  ce = end;
	  save_intr(S_err4);
	  continue;
	}
	if (!f_code && j) {
	  cs = ce + 1;
	  pp->f.len = ((h = ce - pp->cs) < 0) ? 0 : h;
	  ce--;
	} else {
	  pp->f.len = ((h = ce - cs - c1 - (f_kanji % 2)) < 0) ? 0 : h;
	  cs = pp->cs + pp->f.len;
	}
	c1 = c2 = 0;
	ce = cs - !f_code;
	i = -!f_code;
	n = -1;
	f_kanji = 0;
	n_attr = 0;
	n_row++;
      } else {
	ce = end;
	save_intr(S_errl);
	continue;
      }
    }
    i++;
    ce++;
    n++;
  }
  if (ce > cs && !j && ce == end) { /* end of buffer is not '\n' */
    if ((pp = calloc(sizeof(PP), 1))) {
      do {
	pp->cs = cs;
	pp->f.len = ce - cs;
	if (f_code) pp->f.code = 1;
	else if (n_attr) {
	  Alloc.attr[n_attr++].n = -1;
	  if ((pp->attr = malloc(sizeof(ATTR) * n_attr)))
	    memcpy(pp->attr, Alloc.attr, sizeof(ATTR) * n_attr);
	  else {
	    free(pp);
	    save_intr(S_errl);
	    continue;
	  }
	}
	pp->prev = pp1;
	if (!pp0) pp0 = pp;
	else pp1->next = pp;
	pp1 = pp;
	if (pp->prev) pp->f.line = pp->prev->f.line + pp->f.nl;
	else pp->f.line = 1;
	n_row++;
      } while (0);
    } else save_intr(S_errl);
  }
  if (load_intr() == S_errl)
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  *rowp = n_row;
  return pp0;
}

PP **set_ppp(void *p, void *p0, int f_set)/*tcb*/
{ /* replace old line-pointer with adjusted one after resize window */
  static PP **ppp, **ppp0;

  if (f_set) {
    ppp = p;
    ppp0 = p0;
  } else if (!f_set) {
    *ppp = p;
    *ppp0 = p0;
  }
  return ppp;
}

int alloc_winch(BASE *bp, int f_ppp)/*tcb*/
{ /* adjust row-pointer after resize window */
  u_char *cp, *buff;
  PP *pp;
  int size, row, f_bin, f_code;
  FILE *fp;

  f_code = bp->pp->f.code;
  f_bin = bp->pp->f.bin;
  for (row = size = 0, pp = bp->pp; pp; pp = pp->next) {
    size += pp->f.len;
    if (!f_code && pp->f.nl) {
      size++;
      row++;
    }
  }
  if (!(buff = malloc(size))) {
    save_intr(S_errl);
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
    fp = fopen(s_S(S_tmp), "w");
    for (row = 0, pp = bp->pp; pp; pp = pp->next)
      if (pp->cs != F3) {
	fwrite(pp->cs, 1, pp->f.len, fp);
	if (!f_code && pp->f.nl) {
	  fputc('\n', fp);
	  row++;
	}
      }
    fclose(fp);
  } else 
    for (row = 0, cp = buff, pp = bp->pp; pp; pp = pp->next)
      if (pp->cs != F3) {
	memcpy(cp, pp->cs, pp->f.len);
	cp += pp->f.len;
	if (!f_code && pp->f.nl) {
	  *cp++ = '\n';
	  row++;
	}
      }
  free_bp(bp);
  if (!buff) {
    while (!(buff = malloc(size)) && (size /= 2)) ;
    if (size) {
      fp = fopen(s_S(S_tmp), "r");
      fread(buff, 1, size, fp);
      fclose(fp);
    }
  }
  bp->buff = buff;
  if ((bp->size = size)) {
    bp->pp = set_pp(bp->buff, bp->size, &bp->nrow, f_code, f_bin);
    bp->pp->f.bin = f_bin;
    for (pp = bp->pp; pp && pp->next; pp = pp->next) ;
  }
  if (f_ppp) set_ppp(pp, bp->pp, 0);
  return bp->nrow;
}

void free_bp(BASE *bp)/*tcb*/
{
  if (!bp) return;
  if (bp->buff) {
    free(bp->buff);
    bp->buff = NULL;
  }
  if (bp->pp) bp->pp = free_pp(bp->pp);
  bp->size = bp->nrow = 0;
}


static void is_intr(int dummy)
{
  Alloc.f_intr = 1;
  signal(SIGINT, SIG_IGN);
}

FILE *seek_intr(FILE *pin, int *nrowp)/*tcb*/
{
  int pid = 0;

  echo_mess(2);
  signal(SIGINT, is_intr);
#if 0
  if (!(pid = fork())) {
    signal(SIGINT, SIG_DFL);
    while (!vt_getch()) ;
    save_intr(S_err0);
    kill(getppid(), SIGINT);
    exit(0);
  }
#endif
  for (Alloc.f_intr = 0; !Alloc.f_intr && pin; ) 
    alloc_pipe(0, &pin, nrowp, NULL, 0);
  if (!kill(pid, 0)) {
    kill(pid, SIGINT);
    signal(SIGINT, SIG_IGN);
  } else Alloc.f_intr = 0;
  return pin;
}


