#include "tcb.h"
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/stat.h>

static struct {
  void (*segv)(int);
  int abort; /* mark for segmentation fault */
  jmp_buf jb;
} Init;

static void init_clear(int dummy)
{
  signal(SIGSEGV, Init.segv);
  Init.abort = 1;
  longjmp(Init.jb, 0);
}

void init_man(BASE *bp, FILE **pinp, char name[], u_char prev[], char arg[], int *klenp)/*tcb*/
{
  free_bp(bp);
  sprintf(arg, "%c%c", K_z, 0);
  *pinp = fork_man(name);
  bp->pp = alloc_pipe(T_man, pinp, &bp->nrow, bp, strlen(name));
  bp->nrow = 1;
  while (*pinp) /* read the whole man page up front, not just one screen */
    alloc_pipe(0, pinp, &bp->nrow, NULL, 0);
  if (!bp->pp) make_bp(bp, s_S(S_err6), NULL, NULL);
  else { /* alloc_pipe() never maintains bp->size (it's only tracked
	    for overflow detection internally) -- now that the whole
	    page is loaded, total it up so the mode-line can show a
	    real byte count, the same as File/Directory. */
    PP *pp;
    int size = 0;
    for (pp = bp->pp; pp; pp = pp->next) size += pp->f.len + 1;
    bp->size = size;
  }
}
  
int init_pipe(BASE *bp, FILE **pinp, int f_Mb, char mess[], char prev[], char arg[], int *klenp, CHILD **chpp, int ppid)/*tcb*/
{
  int f_M, nlen, pid = 0;
  char *p;

  free_bp(bp);
  if (f_Mb == T_pipe) {
    f_M = f_Mb;
    f_Mb = 1;
    *chpp = NULL;
  } else {
    f_M = f_Mb;
    if ((*chpp = check_ext(mess, NULL, 0)) && is_chp(*chpp, NULL))
      sprintf(arg, " (%s)", (*chpp)->argv[0]);
    else *chpp = NULL;
  }
  if (!*chpp) sprintf(arg, "%c%c", K_z, 0);
  p = (f_M == T_pipe2) ? s_P(P_mode)[T_pipe2] : mess;
  nlen = disp_window(bp->pp, bp->pp, f_M, p, prev, arg, klenp);
  bp->pp = alloc_pipe(f_Mb, pinp, &bp->nrow, bp, nlen);
  if (f_M == T_pipe3 && (!ppid || (ppid < 0 && kill(-ppid, 0)))) ;
  else if (!(pid = fork())) {
    while (1) {
      if (vt_getch()) {
	switch(f_M) {
	case T_pipe:
	  killpg(getpgrp(), SIGINT);
	  break;
	case T_pipe3:
	  killpg(getpgrp(), SIGPIPE);
	  break;
	case T_pipe2:
	  kill(ppid, SIGHUP); /* <== from_shell() */
	  break;
	}
	save_intr(S_err0);
	break;
      }
    }
    exit(0);
  }
  while (*pinp && bp->nrow < vt_row(0)) 
    alloc_pipe(0, pinp, &bp->nrow, NULL, 0);
  if (pid && !kill(pid, 0)) kill(pid, SIGKILL);
  print_line(bp->pp, bp->pp, -nlen, NULL);
  return nlen;
}
  
void init_cut(BASE *bp, char name[], u_char str[], BASE *cut_bp)/*tcb*/
{
  int f_M, code;

  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    return;
  }
  free_bp(bp);
  if (!(bp->buff = fork_grep(&bp->size, str, cut_bp->pp, 0)))
    make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
	    ? s_S(S_err2) : s_S(S_err0), name, NULL);
  else {
    bp->buff = check_type(bp->buff, &bp->size, NULL, &f_M, 3, &code);
    bp->nrow = cut_bp->nrow + 1;
    bp->pp = alloc_pp(bp->buff, M_cut, bp->size, &bp->nrow, name);
  }
  signal(SIGSEGV, Init.segv);
}
  
void init_cut2(BASE *bp, u_char *buff, char name[], u_char str[])/*tcb*/
{
  struct stat st;
  int f_M, code;

  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    return;
  }
  free_bp(bp);
  if (!(bp->buff = fork_grep2(&bp->size, buff, str, name))) 
    make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
	    ? s_S(S_err2) : s_S(S_err0), name, NULL);
  else {
    bp->buff = check_type(bp->buff, &bp->size, NULL, &f_M, 3, &code);
    if (*name == ' ') sscanf(name, "%d", (int*)(&(st.st_size)));
    else stat(name, &st);
    bp->nrow = st.st_size;
    bp->pp = alloc_pp(bp->buff, M_cut, bp->size, &bp->nrow, name);
  }
  signal(SIGSEGV, Init.segv);
}
  
CHILD *init_arc(BASE *bp, CHILD *arc, char name[], u_int bsize, u_char prev[], char arg[], PP *pp, int f_chkbin)/*tcb*/
{
  int size, f_M, code;
  u_char *buff, *buff2;
  CHILD *roff;
  static CHILD *chp = NULL;
  
  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    return chp;
  }
  do {
    if (!strcmp(arc->argv[0], s_S(S_rpm)))
      bp->buff = read_arc(arc, prev, name, &bp->size);
    else if ((bp->size = pp->split) < 0 || !bp->size) {
      if (bp->size) bp->buff = read_arc(arc, prev, name, &bp->size);
    } else {
      if (bp->size > bsize) bp->size = bsize;
      while (!(bp->buff = malloc(bp->size + 2)) && (bp->size /= 2)) ;
      memcpy(bp->buff, (u_char*)pp->tell, bp->size);
    }
    if (!bp->size) {
      make_bp(bp, s_S(S_err1), name, NULL);
      continue;
    }
    strcpy(arg, name);
    do {
      chp = check_ext(arg, bp->buff, bp->size);
      if (!f_chkbin || !chp) {
	if (chp) chp = NULL;
	*arg = '\0';
      } else
	if (chp->f.exec) {
	  fork_exec(chp, arg, bp->buff, bp->size);
	  signal(SIGSEGV, Init.segv);
	  return F9; 
	} else {
	  buff = fork_chp(chp, bp->buff, &bp->size, arg);
	  if (bp->size) {
	    if ((roff = check_ext(NULL, buff, bp->size))) {
	      buff2 = fork_chp(roff, buff, &bp->size, arg);
	      free(buff);
	      buff = buff2;
	      chp = roff;
	    } 
	    free(bp->buff);
	    bp->buff = buff;
	    continue;
	  }
	}
      *arg = '\0';
    } while (0);
    if (!bp->size) make_bp(bp, strerror(EINVAL), name, NULL);
    else {
      size = bp->size;
      bp->buff = check_type(bp->buff, &bp->size, NULL, &f_M, f_chkbin, &code);
      bp->pp = set_pp(bp->buff, bp->size, &bp->nrow, (code == C_bin), 0);
      if (f_M == T_type) {
	if (check_chp(S_file)) chp = get_child(NULL, "..."
					       , s_S(S_file), NULL, NULL);
	else chp = NULL;
	bp->size = size;
      }
    }
  } while (0);
  signal(SIGSEGV, Init.segv);
  return chp;
}

FILE *init_arc2(BASE *bp, CHILD *arc, char name[], PP *pp, char prev[], char arg[])/*tcb*/
{
  u_char *buff;
  int i, f_rpm, f_chp, size, pv[6];
  CHILD *chp, *chp2;
  FILE *fp;

  if (!pp->split) {
    make_bp(bp, s_S(S_err1), name, NULL);
    return F1;
  }
  f_chp = 0;
  chp2 = check_ext(prev, NULL, 0);
  f_rpm = !strcmp(chp2->argv[0], s_S(S_rpm));
  chp2 = NULL;
  if ((chp = check_ext(name, NULL, 0)) && (f_chp = is_chp(chp, NULL)))
    switch(f_chp) {
    case 2: /* bzip2 */
      chp = get_child(NULL, NULL
		      , s_S(S_bzip2), NULL, NULL); /* set -d option */
    case 1: /* gzip etc. */
    case 4: /* lha */
    case 5: /* tar */
      strcpy(chp->argv[chp->f.argc], "-");
      break;
    case 3: /* unzip */
    case 8: /* rpm */
    case 9: /* deb */
      strcpy(chp->argv[chp->f.argc], s_S(S_tmp));
      break;
    case 7: /* bzip2+tar */
    case 6: /* gzip+tar */
      chp2 = (f_chp == 7) ? get_child(NULL, NULL, s_S(S_bzip2), NULL, NULL)
	: (CHILD*)chp->argv[chp->f.argc + 1];
      strcpy(chp2->argv[chp2->f.argc], "-");
      break;
    default:
      break;
    }
  else chp = NULL;
  if (f_rpm || (size = pp->split) < 0) 
    buff = read_arc(arc, prev, name, &size);
  else buff = NULL;
  pipe(pv);
  if (!fork()) {
    int pid = getpid();
    load_signal();
    if (chp) {
      pipe(&pv[2]);
      if (!fork()) {
	if (chp2) {
	  pipe(&pv[4]);
	  if (!fork()) {
	    close(pv[4]);
	    fp = fdopen(pv[5], "w");
	    if (!buff) buff = (u_char*)pp->tell;
	    for (i = size; i; i--) {
	      if (kill(pid, 0)) break;
	      fputc(*buff++, fp);
	    }
	    fclose(fp);
	    exit(0);
	  }
	  dup_error();
	  close(0);
	  dup2(pv[4], 0);
	  close(pv[4]);
	  close(pv[5]);
	  close(1);
	  dup2(pv[3], 1);
	  close(pv[3]);
	  close(pv[2]);
	  execvp(*chp2->argv, chp2->argv);
	} else {
	  close(pv[2]);
	  fp = fdopen(pv[3], "w");
	  if (!buff) buff = (u_char*)pp->tell;
	  for (i = size; i; i--) {
	    if (kill(pid, 0)) break;
	    fputc(*buff++, fp);
	  }
	  fclose(fp);
	  exit(0);
	}
      }
      dup_error();
      close(0);
      dup2(pv[2], 0);
      close(pv[2]);
      close(pv[3]);
      close(1);
      dup2(pv[1], 1);
      close(pv[1]);
      close(pv[0]);
      switch(f_chp) {
      case 3: case 8: case 9:
	fp = fopen(s_S(S_tmp), "w");
	while ((i = fgetc(stdin)) != EOF) {
	  if (kill(pid, 0)) break;
	  fputc(i, fp);
	}
	fclose(fp);
	break;
      }
      execvp(*chp->argv, chp->argv);
    } else {
      close(pv[0]);
      fp = fdopen(pv[1], "w");
      if (!buff) buff = (u_char*)pp->tell;
      for (i = size; i > 0; i -= N_pipe, buff += N_pipe)
	fwrite(buff, 1, (i - N_pipe < 0) ? i : N_pipe, fp);
      fclose(fp);
      exit(0);
    }
  }
  close(pv[1]);
  if (buff) free(buff);
  return fdopen(pv[0], "r");
}

void init_grep(BASE *bp, char name[], u_char str[], PP *pp, int fgrep)/*tcb*/
{
  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    return;
  }
  if (*name == ' ' || !is_access(name, bp)) {
    if (!(bp->buff = fork_grep(&bp->size, str, pp, fgrep)))
      make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
	      ? s_S(S_err2) : s_S(S_err0), name, NULL);
    else bp->pp = alloc_pp(bp->buff, T_cut, bp->size, &bp->nrow, name);
  }
  signal(SIGSEGV, Init.segv);
}
  
void init_ls(BASE *bp, char name[], int f_M, int f_ls)/*tcb*/
{
  struct stat st;
  int i, nrow, *split;
  char *p;
  CHILD *chp;
  PP *pp;

  if (f_M != T_locate && f_M != M_du)
    if (stat(name, &st) < 0) {
      getcwd(name, N_line);
      if (strchr(name, '\0')[-1] != '/') strcat(name, "/");
    }
  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    return;
  }
  if (f_M == T_dir) {
    nrow = bp->nrow;
    split = malloc(bp->nrow * sizeof(int));
    for (i = 0, pp = bp->pp; i < bp->nrow; pp = pp->next, i++) 
      split[i] = pp->split;
  } else {
    split = NULL;
    nrow = 0;
  }
  free_bp(bp);
  do {
    switch(f_M) {
    case T_dir:
      if (!S_ISDIR(st.st_mode)) get_dir(name); /* get current directory name */
      chdir(name);
      p = alloca(N_line+1);
      getcwd(p, N_line);
      chp = get_child(NULL, NULL, s_S(S_ls), p, NULL); /* `ls -aqlF' */
      if (!(bp->buff = fork_bin(chp, &bp->size, T_dir, 2))) {
	make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
		? strerror(EACCES) : s_S(S_err0), name, NULL);
	continue;
      } else if (bp->size < 16) {
	make_bp(bp, s_S(S_err1), name, NULL);
	continue;
      }
      break;
    case T_locate:
      if (!(bp->buff
	    = fork_bin(get_child(NULL, NULL, s_S(S_locate), name, NULL)
		       , &bp->size, M_locate, 2))) {
	make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
		? s_S(S_err3) : s_S(S_err0), name, NULL);
	continue;
      }
      strcpy(name, s_P(P_mode)[M_locate]);
      break;
    }
    bp->pp = alloc_pp(bp->buff, f_M, bp->size, &bp->nrow, name);
    if (f_M == T_dir) {
      int f_dot;
      for (i = f_dot = 0, pp = bp->pp; pp; pp = pp->next, i++) {
	if (!f_dot && strstr(pp->cs, "../")) {
	  skip_spc(pp->cs, -1);
	  f_dot = 1;
	}
	if (i < nrow) pp->split = split[i];
      }
    }
  } while (0);
  if (split) free(split);
  signal(SIGSEGV, Init.segv);
}
  
void init_split(BASE *bp, char name[], PP *pp, BASE *cut_bp, int f_chkbin)/*tcb*/
{
  int code, f_M = T_split;

  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    return;
  }
  if (!(bp->buff = fork_split(&bp->size, pp, cut_bp->pp)))
    make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
	    ? s_S(S_err2) : s_S(S_err0), name, NULL);
  else {
    if (is_name(name, M_log)) code = C_ascii;
    else bp->buff = check_type(bp->buff, &bp->size, NULL, &f_M
			       , (f_chkbin != 0), &code);
    bp->pp = set_pp(bp->buff, bp->size, &bp->nrow, (code == C_bin), 0);
  }
  signal(SIGSEGV, Init.segv);
}

void init_split2(BASE *bp, u_char *buff, u_int bsize, u_char name[], PP *pp, int f_chkbin)/*tcb*/
{
  int code, f_M;

  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    return;
  }
  if ((bp->size = (pp->next) 
	? pp->next->tell - pp->tell : pp->split - pp->tell) <= 0)
    make_bp(bp, s_S(S_err1), name, NULL);
  else {
    if (bp->size > bsize) bp->size = bsize;
    if (*name == ' ') {
      while (!(bp->buff = malloc(bp->size)) && (bp->size /= 2)) ;
      memcpy(bp->buff, &buff[pp->tell], bp->size);
    } else bp->buff = alloc_split(name, pp->tell, bp->size);
    bp->buff = check_type(bp->buff, &bp->size, NULL, &f_M
			  , (f_chkbin != 0), &code);
    bp->pp = set_pp(bp->buff, bp->size, &bp->nrow, (code == C_bin), 0);
  }
  signal(SIGSEGV, Init.segv);
}

FILE *init_split3(BASE *bp, char name[], PP *pp, PP *ps)/*tcb*/
{
  int i, pv[2];
  PP *pe;
  FILE *fp;
  
  if (!pp->prev) {
    for (i = 1, pe = ps; i < pp->tell; pe = pe->next, i++) ;
    i = 1;
  } else for (i = 1; i < pp->tell; ps = ps->next, i++) ;
  i = ((pp->next) ? pp->next->tell : pp->split) - i;
  for (pe = ps; i; pe = pe->next,i--) ;
  pipe(pv);
  if (!fork()) {
    load_signal();
    close(pv[0]);
    fp = fdopen(pv[1], "w");
    for (; ps != pe; ps = ps->next) {
      fwrite(ps->cs, 1, ps->f.len, fp);
      if (ps->f.nl) fputc('\n', fp);
    }
    fclose(fp);
    exit(0);
  }
  close(pv[1]);
  return fdopen(pv[0], "r");
}

FILE *init_split4(BASE *bp, u_char *buff, u_char name[], PP *pp)/*tcb*/
{
  int i, pv[2];
  FILE *fp, *fp1;
  
  if (!(i = (pp->next) ? pp->next->tell - pp->tell : pp->split - pp->tell)) {
    make_bp(bp, s_S(S_err1), name, NULL);
    return F1;
  }
  if (!pp->prev) {
    i += pp->tell;
    pp->tell = 0;
  }
  pipe(pv);
  if (!fork()) {
    load_signal();
    close(pv[0]);
    fp1 = fdopen(pv[1], "w");
    if (*name == ' ') for (; i; i--) fputc(*buff++, fp1);
    else {
      fp = fopen(name, "r");
      fseek(fp, pp->tell, 0);
      for (; i; i--) fputc(fgetc(fp), fp1);
      fclose(fp);
    }
    fclose(fp1);
    exit(0);
  }
  close(pv[1]);
  return fdopen(pv[0], "r");
}

int init_bin(BASE *bp, BASE *head, CHILD **chpp, int f_M, char name[], int f_chkbin)/*tcb*/
{
  int i, fd, f_Mb, code;
  u_char *buff;
  CHILD *roff;
  static CHILD *chp;
  struct stat st;

  if (*name == '/' && is_access(name, bp) < 0) return f_M;
  if (head && head->buff) free_bp(head);
  Init.abort = 0;
  Init.segv = signal(SIGSEGV, init_clear);
  setjmp(Init.jb);
  if (Init.abort) {
    if (head && head->buff) free_bp(head);
    make_bp(bp, strerror(EINVAL), name, s_S(S_err8));
    *chpp = chp;
    return f_M;
  }
  errno = 0;
  chp = *chpp;
  fd = 0;
  switch(f_M) {
  case T_key:
  case M_key:
    bp->buff = fork_key(&bp->size);
    bp->pp = alloc_pp(bp->buff, M_key, bp->size, &bp->nrow, name);
    break;
  case M_du:
    free_bp(bp);
    unlink(s_S(S_enter));
    bp->buff = read_buff(&bp->size, f_chkbin, 2);
    close(f_chkbin);
    if (f_M == M_du) {
      i = bp->size * 2;
      bp->buff[bp->size] = '\0';
      if (init_code(&bp->size, bp->buff, 0) != C_ascii)
	bp->size = sjistoeuc(i, &bp->buff, bp->size, NULL);
    }
    bp->pp = alloc_pp(bp->buff, f_M, bp->size, &bp->nrow, name);
    break;
  case M_error:
    if (!fd) {
      stat(s_S(S_error), &st);
      fd = open(s_S(S_error), O_RDONLY);
    }
  case M_log:
    if (!fd) {
      stat(s_S(S_log), &st);
      fd = open(s_S(S_log), O_RDONLY);
    }
  case T_file:
  case T_file2:
    if (!fd) {
      if (stat(name, &st) < 0) {
	make_bp(bp, strerror(ENOENT), name, NULL);
	break;
      }
      fd = open(name, O_RDONLY);
    } else if (fd < 0) {
      make_bp(bp, s_S(S_err1), name, NULL);
      break;
    }
    bp->size = st.st_size;
    bp->buff = read_buff(&bp->size, fd, f_chkbin);
    close(fd);
    if (f_M == M_du) unlink(s_S(S_enter));
  case M_tcb:
    if (f_M == M_tcb) {
      free_bp(bp);
      bp->size = strlen(s_S(S_TCB));
      bp->buff = strdup(s_S(S_TCB));
    }
    if (!bp->size) make_bp(bp, s_S(S_err1), name, NULL);
    else {
      f_Mb = f_M;
      bp->buff = check_type(bp->buff, &bp->size, name, &f_M, f_chkbin, &code);
      i = f_M != M_log && f_M != M_error && f_M != M_tcb && code == C_bin;
      if (f_M == T_type) {
	chp = check_chp(S_file);
	if (f_Mb == T_file2 && code < 0) {
	  free(bp->buff);
	  fd = open(name, O_RDONLY);
	  bp->size = st.st_size;
	  bp->buff = read_buff(&bp->size, fd, 1);
	  close(fd);
	  code = C_bin;
	}
      }
      bp->pp = set_pp(bp->buff, bp->size, &bp->nrow, i, 0);
    }
    break;
  case T_tar:
  case T_lha:
  case T_unzip:
  case T_rpm:
  case T_deb:
    if (!(head->buff 
	  = fork_arc(name, f_M, chp, &head->size, &bp->size, bp)))
      make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
	      ? strerror(EINVAL) : s_S(S_err0), name, NULL);
    else {
      bp->pp = alloc_head(head->buff, f_M, head->size, &bp->nrow);
      if (!bp->pp) make_bp(bp, (access(s_S(S_intr), F_OK) < 0)
			   ? strerror(EINVAL) : s_S(S_err0), name, NULL);
      else
	switch(f_M) {
	case T_tar:
	case T_deb:
	  alloc_tar(bp->buff, bp->pp, bp->size);
	  break;
	case T_lha:
	  alloc_lha(bp->buff, bp->pp, bp->size);
	  break;
	case T_unzip:
	  alloc_unzip(bp->buff, bp->pp, bp->size);
	  break;
	case T_rpm:
	  break;
	}
    }
    break;
  case T_bin:
  case T_gz:
  case T_roff:
    if (!(bp->buff = fork_bin(chp, &bp->size, T_bin, 0)))
      make_bp(bp, (access(s_S(S_intr), F_OK) < 0) 
	      ? strerror(EINVAL) : s_S(S_err0), name, NULL);
    else {
      bp->buff = check_type(bp->buff, &bp->size, NULL, &f_M, 1, &code);
      if ((roff = check_ext(NULL, bp->buff, bp->size))) {
	buff = fork_chp(roff, bp->buff, &bp->size, name);
	free(bp->buff);
	bp->buff = buff;
	chp = roff;
      }
      bp->pp = set_pp(bp->buff, bp->size, &bp->nrow, 0, 0);
    }
    break;
  default:
    break;
  }
  *chpp = chp;
  signal(SIGSEGV, Init.segv);
  return f_M;
}
  
