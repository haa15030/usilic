#include "usilic.h"
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/stat.h>

static struct {
  void (*segv)(int);
  int abort; /* segmentation faultの目印 */
  jmp_buf jb;
} Init;

/*
 * 【機能】SIGSEGVのハンドラ。直前のシグナル処理を元に戻し、目印(Init.abort)を立てて、longjmpで
 *       init_*の入口へ戻る(クラッシュしても、その画面をエラー表示にして続行するため)。
 * 【C++】なし(C++版はSIGSEGVを回収しない。範囲チェックとRAIIで防ぐ設計)
 */
static void init_clear(int dummy)
{
  signal(SIGSEGV, Init.segv);
  Init.abort = 1;
  longjmp(Init.jb, 0);
}

/*
 * 【機能】manページ(name)をfork_manで取得し、1画面分ではなくページ全体を先に読み込んで、バッファ(bp)へ
 *       載せる。バイト数も合計しておく(モードラインに出すため)。無ければ`No manual entry`。
 * 【C++】ManMode::loadPage (src/cpp/src/man_mode.cpp)
 */
void init_man(BASE *bp, FILE **pinp, char name[], u_char prev[], char arg[], int *klenp)/*usilic*/
{
  free_bp(bp);
  sprintf(arg, "%c%c", K_z, 0);
  *pinp = fork_man(name);
  bp->pp = alloc_pipe(T_man, pinp, &bp->nrow, bp, strlen(name));
  bp->nrow = 1;
  while (*pinp) /* 1画面分だけでなく、マニュアルページ全体を前もって読む */
    alloc_pipe(0, pinp, &bp->nrow, NULL, 0);
  if (!bp->pp) make_bp(bp, s_S(S_err6), NULL, NULL);
  else { /* alloc_pipe()はbp->sizeを一切維持しない(内部的にオーバー
	    フロー検出のためだけに追跡されている)――ページ全体が
	    読み込まれた今、mode-lineがFile/Directoryと同じように
	    本物のバイト数を表示できるよう、合計を計算する。 */
    PP *pp;
    int size = 0;
    for (pp = bp->pp; pp; pp = pp->next) size += pp->f.len + 1;
    bp->size = size;
  }
}
  
/*
 * 【機能】パイプ入力(標準入力、SHELLからの引き渡し、パイプ系のT_pipe*)のページャを初期化する。モードラインを
 *       描き、最初の1画面分まで読み込む。読み込み中は子プロセスがキー入力を見張り、何かキーが
 *       押されたら中断のシグナルを送る。
 * 【C++】FileMode::loadStream (src/cpp/src/file_mode.cpp); Application::startPagerStdin (src/cpp/src/application.cpp)
 * 【備考】旧は子プロセスでキーを監視して中断する。C++は入力を少しずつ読み込む(ペーシング)ので、
 *       読み込みの合間に^Gを確認できる。
 */
int init_pipe(BASE *bp, FILE **pinp, int f_Mb, char mess[], char prev[], char arg[], int *klenp, CHILD **chpp, int ppid)/*usilic*/
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
  
/*
 * 【機能】CUT(バッファの分割)。検索文字列strで、分割元(cut_bp)の行を検索して(fork_grep)、結果をbpへ
 *       載せる。SEGV時はエラー表示に切り替えて戻る。
 * 【C++】CutMode::split (src/cpp/src/cut_mode.cpp)
 */
void init_cut(BASE *bp, char name[], u_char str[], BASE *cut_bp)/*usilic*/
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
  
/*
 * 【機能】CUT(ファイルを直接分割)。検索文字列strで、バッファ・ファイルを検索して(fork_grep2)、結果を
 *       bpへ載せる。
 * 【C++】CutMode::split (src/cpp/src/cut_mode.cpp)
 */
void init_cut2(BASE *bp, u_char *buff, char name[], u_char str[])/*usilic*/
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
  
/*
 * 【機能】アーカイブの1項目(pp)の内容を取り出す(read_arc、または記憶済みの領域から複写)。必要なら解凍や
 *       groff変換(fork_chp)を続けて掛け、結果をbpへ載せる。EXEC対象なら外部プログラムへ
 *       渡して終わる(F9を返す)。
 * 【C++】ArchiveMode::extractSelected (src/cpp/src/archive_mode.cpp); FileMode::loadAndTransformContent (src/cpp/src/file_mode.cpp)
 */
CHILD *init_arc(BASE *bp, CHILD *arc, char name[], u_int bsize, u_char prev[], char arg[], PP *pp, int f_chkbin)/*usilic*/
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

/*
 * 【機能】アーカイブの1項目を、展開プログラム(tar/gzip/bzip2/unzip/rpm/deb等)のパイプラインを子プロセスで
 *       組んで流し込み、その出力を読むFILE*を返す。
 * 【C++】ChildProcess::runPipeline (src/cpp/src/child_process.cpp); ArchiveMode::extractSelected (src/cpp/src/archive_mode.cpp)
 * 【備考】旧はfork/pipe/dup2を手で組む。C++はChildProcessがパイプラインの構築を担う。
 */
FILE *init_arc2(BASE *bp, CHILD *arc, char name[], PP *pp, char prev[], char arg[])/*usilic*/
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
		      , s_S(S_bzip2), NULL, NULL); /* -dオプションを設定する */
    case 1: /* gzip等 */
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

/*
 * 【機能】GREP(検索)。検索文字列strで、対象(pp)を検索して(fork_grep)、結果をbpへ載せる。
 * 【C++】GrepMode::search (src/cpp/src/grep_mode.cpp)
 */
void init_grep(BASE *bp, char name[], u_char str[], PP *pp, int fgrep)/*usilic*/
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
  
/*
 * 【機能】ディレクトリ一覧(T_dir: `ls -alF`を実行し、以前のsplit(マーク)を引き継ぐ)、またはlocateの
 *       結果(T_locate)を作る。パスがディレクトリでなければ、カレントディレクトリに置き換える。
 * 【C++】DirectoryMode::refresh; DirectoryMode::enterDirectory (src/cpp/src/directory_mode.cpp); LocateMode::search (src/cpp/src/locate_mode.cpp)
 */
void init_ls(BASE *bp, char name[], int f_M, int f_ls)/*usilic*/
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
      if (!S_ISDIR(st.st_mode)) get_dir(name); /* カレントディレクトリ名を取得する */
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
  
/*
 * 【機能】CUTで分割した1セグメントの内容(fork_split)を取り出して、bpへ載せる。文字コード判定(check_type)と
 *       改行位置の決定(set_pp)も行う。
 * 【C++】CutMode::segmentContent (src/cpp/src/cut_mode.cpp)
 */
void init_split(BASE *bp, char name[], PP *pp, BASE *cut_bp, int f_chkbin)/*usilic*/
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

/*
 * 【機能】init_splitと同じく、CUTの1セグメントの内容を、記憶済みのバッファ、または元ファイルの範囲から
 *       取り出してbpへ載せる。
 * 【C++】CutMode::segmentContent (src/cpp/src/cut_mode.cpp)
 */
void init_split2(BASE *bp, u_char *buff, u_int bsize, u_char name[], PP *pp, int f_chkbin)/*usilic*/
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

/*
 * 【機能】CUTのセグメント(ps〜pe)を、外部プログラムへ流し込むためのパイプを作り、その読み側を返す。
 * 【C++】CutMode::segmentContent (src/cpp/src/cut_mode.cpp)
 * 【備考】C++はメモリ上のStyledLineを直接扱うので、この受け渡し用のパイプは不要。
 */
FILE *init_split3(BASE *bp, char name[], PP *pp, PP *ps)/*usilic*/
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

/*
 * 【機能】init_split3と同じく、CUTのセグメントを、ファイルまたはバッファの範囲から書き出すパイプを作り、
 *       その読み側を返す。
 * 【C++】CutMode::segmentContent (src/cpp/src/cut_mode.cpp)
 * 【備考】C++はメモリ上のStyledLineを直接扱うので、この受け渡し用のパイプは不要。
 */
FILE *init_split4(BASE *bp, u_char *buff, u_char name[], PP *pp)/*usilic*/
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

/*
 * 【機能】汎用のバッファ初期化。モード(f_M)に応じて、KEY一覧(fork_key)、DUの結果、ERROR/LOG・ファイルの
 *       読み込み(read_buff、文字コード判定check_type)、アーカイブ一覧(T_tar等)、解凍・groff変換
 *       (T_bin/T_gz/T_roff)を、bpへ作る。
 * 【C++】FileMode::loadFile (src/cpp/src/file_mode.cpp); ArchiveMode::loadArchive (src/cpp/src/archive_mode.cpp); DuMode::loadUsage (src/cpp/src/du_mode.cpp); Application::startKey (src/cpp/src/application.cpp)
 * 【備考】旧は1つの巨大なswitchに全モードの読み込みが集まっている。C++はモードごとのクラスに分かれている。
 */
int init_bin(BASE *bp, BASE *head, CHILD **chpp, int f_M, char name[], int f_chkbin)/*usilic*/
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
  case M_usilic:
    if (f_M == M_usilic) {
      free_bp(bp);
      bp->size = strlen(s_S(S_USILIC));
      bp->buff = strdup(s_S(S_USILIC));
    }
    if (!bp->size) make_bp(bp, s_S(S_err1), name, NULL);
    else {
      f_Mb = f_M;
      bp->buff = check_type(bp->buff, &bp->size, name, &f_M, f_chkbin, &code);
      i = f_M != M_log && f_M != M_error && f_M != M_usilic && code == C_bin;
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
  
