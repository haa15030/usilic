#include "usilic.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/wait.h>

static char *w_arg(char arg[], char argv[])
{ /* 特定のsuffix(.dvi等)を必要とするプログラム(dvi2tty等)のために
     ファイル名を設定する */
  static char wa[N_line];
  char *cp, *dp;

  for (cp = dp = arg; (cp = strchr(cp, '/')); dp = ++cp) ;
  sprintf(wa, "%s.%s.%s", s_S(S_w), argv, dp);
  return wa;
}

u_char *read_buff(int *sizep, u_int fd, int f_chkbin)/*usilic*/
{
  int pid = 0;
  u_char *buff;

#if 0
  if (f_chkbin) { /* file-selectorではない */
    echo_mess(2);
    if (!(pid = fork())) {
      signal(SIGPIPE, SIG_DFL);
      while (!vt_getch()) ;
      save_intr(S_err0);
      killpg(getpgrp(), SIGPIPE);
    }
  }
#else
  if (f_chkbin == 3) f_chkbin = 2;
  else echo_mess(2);
#endif
  buff = alloc_buff(sizep, fd, f_chkbin);
  if (pid && !kill(pid, 0)) kill(pid, SIGKILL);
  return buff;
}

u_char *fork_key(int *sizep)/*usilic*/
{
  int pv[2];
  u_char *buff;
  
  pipe(pv);
  if (!fork()) {
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    conf_key(NULL, NULL, NULL, F5, NULL);
    exit(0);
  }
  close(pv[1]);
  buff = read_buff(sizep, pv[0], 0);
  close(pv[0]);
  return buff;
}

u_char *fork_bin(CHILD *chp, int *sizep, int f_M, int f_chkbin)/*usilic*/
{
  int pv[2];
  u_char *buff;
  
  pipe(pv);
  if (!fork()) {
    load_signal();
    dup_error();
    if (f_M == T_dir) /* `ls'の出力は、ファイル名がどこから始まるかを
			  見つけるためバイト単位でパースされる
			  (get_col())――ローカライズされた月名(例えば
			  "7月")はUTF-8ではマルチバイトなので、その
			  バイト長はもはや1カラムの表示幅と一致せず、
			  それ以降の全てのカラム位置を狂わせ、画面上で
			  ハイライトされたファイル名の先頭文字を重複
			  させてしまう。この子プロセスに限ってCロケールを
			  強制し、`ls'が常にASCIIの日付を出力するように
			  する。 */
      setenv("LC_ALL", "C", 1);
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    execvp(*chp->argv, chp->argv);
    exit(0); /* 抜けていた: execvp()が失敗した場合、この子プロセスは
		 そのまま素通りしてしまい、usilicの2つ目の完全なコピー
		 (同じプロセスイメージ、同じ開かれたttyのfd)として
		 動き続け、あらゆるキー入力を巡って本物のプロセスと
		 競合していた。 */
  }
  close(pv[1]);
  buff = read_buff(sizep, pv[0], f_chkbin);
  close(pv[0]);
  return buff;
}

void fork_exec(CHILD *chp, char arg[], u_char buff[], int size)/*usilic*/
{
  FILE *fp;
  char *cp;

  cp = w_arg(arg, chp->argv[0]);
  fp = fopen(cp, "w");
  fwrite(buff, 1, size, fp);
  fclose(fp);
  do_exec(cp, chp);
  unlink(cp);
}

u_char *fork_chp(CHILD *chp, u_char *buff, int *sizep, char arg[])/*usilic*/
{
  CHILD *chp2 = NULL;
  int pv[4], size = *sizep;
  u_char *buff2, *cp;
  FILE *fp;

  cp = w_arg(arg, chp->argv[0]);
  if (!chp->argv[chp->f.argc]) {
    chp2 = (CHILD*)chp->argv[chp->f.argc + 1];
    strcpy(chp2->argv[chp2->f.argc], cp);
  } else strcpy(chp->argv[chp->f.argc], cp);
  fp = fopen(cp, "w");
  fwrite(buff, 1, size, fp);
  fclose(fp);
  pipe(pv);
  if (!fork()) {
    load_signal();
    dup_error();
    if (chp2) {
      pipe(&pv[2]);
      if (!fork()) {
	close(1);
	dup2(pv[3], 1);
	close(pv[3]);
	close(pv[2]);
	execvp(*chp2->argv, chp2->argv);
      }
      close(0);
      dup2(pv[2], 0);
      close(pv[2]);
      close(pv[3]);
    }
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    execvp(*chp->argv, chp->argv);
  }
  close(pv[1]);
  buff2 = read_buff(&size, pv[0], 0);
  close(pv[0]);
  unlink(cp);
  *sizep = size;
  return buff2;
}

u_char *fork_arc(char name[], int f_M, CHILD *chp0, int *tmp_sizep, int *bp_sizep, BASE *bp)/*usilic*/
{
  CHILD *chp = NULL, *chp2 = NULL;
  int fd, n_error, pv[6];
  char *tmp_buff;
  
  switch(f_M) {
  case T_deb:
    chp0 = get_child(NULL, ".tgz", s_S(S_tar), NULL, NULL);
  case T_tar:
    if (!chp0->argv[chp0->f.argc]) {
      chp2 = (CHILD*)chp0->argv[chp0->f.argc + 1];
      chp2 = get_child(chp2, NULL, chp2->argv[0] 
		       , (f_M == T_deb) ? "-f" : name, NULL);
      pipe(pv);
      if (!fork()) {
	load_signal();
	if (f_M == T_deb) {
	  pipe(&pv[2]);
	  if (!fork()) {
	    dup_error();
	    close(1);
	    dup2(pv[3], 1);
	    close(pv[3]);
	    close(pv[2]);
	    execlp("ar", "ar", "p", name, "data.tar.gz", NULL);
	  }
	  close(pv[3]);
	  close(0);
	  dup2(pv[2], 0);
	  close(pv[2]);
	} else dup_error();
	close(1);
	dup2(pv[1], 1);
	close(pv[0]);
	close(pv[1]);
	execvp(*chp2->argv, chp2->argv);
      }
      close(pv[1]);
      bp->buff = read_buff(&bp->size, pv[0], 0);
      close(pv[0]);
      if (!bp->buff) return NULL;
      *bp_sizep = bp->size;
      if (f_M == T_deb) {
	chp = chp0;
	chp2 = (CHILD*)chp0->argv[chp0->f.argc + 1];
	chp2 = get_child(chp2, NULL, chp2->argv[0], "-f", NULL); 
      } else chp = chp0;
    } else {
      struct stat st;
      stat(name, &st);
      fd = open(name, O_RDONLY);
      bp->size = st.st_size;
      bp->buff = read_buff(&bp->size, fd, 0);
      close(fd);
      *bp_sizep = bp->size;
      chp = get_child(NULL, ".tar", s_S(S_tar), name, NULL);
    }
    break;
  case T_lha:
    chp = get_child(NULL, NULL, s_S(S_lha), name, NULL); /* `.lzh lha p' */
    pipe(pv);
    if (!fork()) {
      load_signal();
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[0]);
      close(pv[1]);
      execvp(*chp->argv, chp->argv);
    }
    close(pv[1]);
    bp->buff = read_buff(&bp->size, pv[0], 0);
    close(pv[0]);
    if (!bp->buff) return NULL;
    *bp_sizep = bp->size;
    chp = get_child(chp->next, NULL, s_S(S_lha), name, NULL); /* `.lzh lha' */
    break;
  case T_unzip:
    chp = get_child(NULL, NULL, s_S(S_unzip), name, NULL); /* `.zip unzip -p' */
    pipe(pv);
    if (!fork()) {
      load_signal();
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[0]);
      close(pv[1]);
      execvp(*chp->argv, chp->argv);
    }
    close(pv[1]);
    bp->buff = read_buff(&bp->size, pv[0], 0);
    close(pv[0]);
    if (!bp->buff) return NULL;
    *bp_sizep = bp->size;
    /* `.zip unzip -l' */
    chp = get_child(chp->next, NULL, s_S(S_unzip), name, NULL);
    break;
  case T_rpm:
    chp = get_child((get_child(NULL, NULL, s_S(S_rpm2cpio), name, NULL))->next
		    , NULL, s_S(S_rpm), name, NULL); /* `.rpm rpm -qplv' */
    break;
  }
  if ((n_error = load_intr()) == S_err0) { /* n_errorを保存する */
    tmp_buff = NULL;
    *tmp_sizep = 0;
  } else {
    pipe(pv);
    if (!fork()) {
      load_signal();
      /* 2026-08-20修正、実際の報告+スクリーンショット(utf8.tar自身の
	 ARC一覧が、本物のUTF-8ファイル名の代わりに8進エスケープされた
	 ゴミ、例えば"utf8/\345\233..."を表示していた): 下の一律の
	 LC_ALL=C(それ自身の元々の、今も有効な理由のために残されている
	 ――すぐ下のコメント参照)が、tar(1)自身のLC_CTYPEも"C"に
	 強制してしまっており、そのためファイル名中の非ASCIIバイトを
	 全て表示不能として扱い8進エスケープしてしまっていることを
	 直接確認した――同じアーカイブに対する`tar tvf'を、空/"C"の
	 環境で実行するとそのようなバイトを全てエスケープするが、
	 LC_CTYPEだけをUTF-8ロケールに設定した(LC_TIMEは"C"のまま)
	 全く同じコマンドは、本物の文字を表示しつつ、alloc_head()が
	 依存する固定幅の英語の日付欄も保つ。LC_ALLを、関連する2つの
	 カテゴリ――LC_TIMEを"C"へ強制する(このコメント自身の元々の
	 理由、変更無し)と、usilic自身がUTF-8に設定されている時「だけ」
	 LC_CTYPEをUTF-8ロケールにする(vt_charset(0)に合わせる、この
	 コードベースの他の全てのUTF-8固有の修正が使うのと同じゲート
	 ――EUC/SJIS/ASCIIのユーザーにはここでの挙動変化は一切無い)
	 ――に分割することで、このコードが防ごうとしている元々の日付
	 パースのクラッシュを再発させることなくファイル名を修正できる。
	 LC_ALLは(明示的に未設定にすることも含めて)絶対に一緒に設定
	 してはならない: POSIXはLC_ALLを、一度設定されると個々のLC_*
	 カテゴリの設定を無条件に上書きするものと定義している。 */
      unsetenv("LC_ALL"); /* 呼び出し元のシェル自身の環境で既に設定
			      されている場合に備えて――LC_ALLは、もし
			      少しでも存在すれば、それ自身の値に関わらず
			      下のLC_TIME/LC_CTYPEの設定を無条件に上書き
			      してしまう */
      setenv("LC_TIME", "C", 1); /* アーカイブのTOC(tar tvf/unzip -l/
				    lha/rpm -qplv)は、alloc_head()によって
				    ASCIIとしてバイト単位でパースされる;
				    ローカライズされた日付(例えば"7月")は
				    そのスキャンを壊し、usilicをハング
				    させたりセグフォさせたりする */
      setenv("LC_CTYPE", (vt_charset(0) == C_utf8) ? "en_US.UTF-8" : "C", 1);
      if (chp2) {
	pipe(&pv[2]);
	if (!fork()) {
	  if (f_M == T_deb) {
	    pipe(&pv[4]);
	    if (!fork()) {
	      dup_error();
	      close(1);
	      dup2(pv[5], 1);
	      close(pv[5]);
	      close(pv[4]);
	      execlp("ar", "ar", "p", name, "data.tar.gz", NULL);
	    }
	    close(pv[5]);
	    close(0);
	    dup2(pv[4], 0);
	    close(pv[4]);
	  }
	  close(1);
	  dup2(pv[3], 1);
	  close(pv[3]);
	  close(pv[2]);
	  execvp(*chp2->argv, chp2->argv);
	}
	close(pv[3]);
	close(0);
	dup2(pv[2], 0);
	close(pv[2]);
      }
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[1]);
      close(pv[0]);
      execvp(*chp->argv, chp->argv);
    }
    close(pv[1]);
    tmp_buff = read_buff(tmp_sizep, pv[0], 0);
    close(pv[0]);
    if (n_error) save_intr(n_error); /* n_errorを復元する */
  }
  return tmp_buff;
}

u_char *fork_split(int *sizep, PP *pp, PP *ps)/*usilic*/
{
  int i, pv[2];
  u_char *cp;
  PP *pe;
  FILE *fp;
  
  if (!pp->prev) {
    for (i = 1, pe = ps; i < pp->tell; pe = pe->next, i++) ;
    i = 1;
  } else for (i = 1; i < pp->tell; ps = ps->next, i++) ;
  i = ((pp->next) ? pp->next->tell : pp->split) - i;
  for (pe = ps; i; pe = pe->next, i--) ;
  pipe(pv);
  if (!fork()) {
    load_signal();
    close(pv[0]);
    fp = fdopen(pv[1], "w");
    if (ps->f.code)
      for (; ps != pe; ps = ps->next) {
	for (i = pp->f.len, cp = ps->cs; i; i--, cp++)
	  if (*cp < ' ' || *cp == K_dl) fputc(*cp + '@', fp);
	  else if (*cp > K_dl) fputc(' ', fp);
	  else fputc(*cp, fp);
	fputc('\n', fp);
      }
    else
      for (; ps != pe; ps = ps->next) {
	fwrite(ps->cs, 1, ps->f.len, fp);
	if (ps->f.nl) fputc('\n', fp);
      }
    fclose(fp);
    exit(0);
  }
  close(pv[1]);
  cp = read_buff(sizep, pv[0], 0);
  close(pv[0]);
  return cp;
}  

static u_char *sgets(u_char *buff, int len, u_char *tp)
{
  for (; len; len--) 
    if ((*buff++ = *tp++) == '\n') break;
  *buff = '\0';
  return tp;
}

static int cat_mime(u_char line0[], FILE *fp, FILE *fp1, u_char **tpp)
{ /* mimeを連結する */
  static u_char line1[N_pipe];
  u_char *p, *cp0, *cp1, *tp = NULL;
  int i, j, len, f_nl;
  static char ss[] = "=?";
  static char se[] = "?=";
  
  tp = (tpp) ? *tpp : NULL;
  cp0 = line0;
  cp1 = line1;
  len = strlen(line0);
  f_nl = 0;
  while (1) {
    if (tp) tp = sgets(cp1, N_pipe, tp);
    else fgets(cp1, N_pipe, fp);
    len += strlen(cp1);
    if ((*cp1 == ' ' || *cp1 == '\t')
	&& ((j = (p = strstr(cp1, ss)) && strstr(&p[8], se))
	    || ((p = strstr(cp0, ss)) && strstr(&p[8], se)))) {
      if ((p = strchr(cp0, '\n'))) {
	*p = '\0';
	f_nl++;
      }
      if (j) for (i = 0; cp0[i] == ' ' || cp0[i] == '\t'; i++) ;
      else for (i = 0; cp0[i] == '\t'; i++) ;
      fwrite(&cp0[i], 1, strlen(cp0) - i, fp1);
      if (cp0 == line0) {
	cp0 = line1;
	cp1 = line0;
      } else {
	cp0 = line0;
	cp1 = line1;
      }
    } else {
      if ((p = strstr(cp0, ss)) && strstr(&p[8], se))
	for (i = 0; cp0[i] == ' ' || cp0[i] == '\t'; i++) ;
      else i = 0;
      fwrite(&cp0[i], 1, strlen(cp0) - i, fp1);
      while (f_nl--) fputc(' ', fp1);
      break;
    }
  }
  if (tpp) *tpp = tp;
  return len;
}

u_char *fork_grep(int *sizep, u_char str[], PP *pp, int f_fgrep)/*usilic*/
{
  u_char *cp, *ce;
  int i, pv[4], f_bs;
  CHILD *chp;
  FILE *fp;
  
  f_bs = strchr(str, K_h) != NULL;
  pipe(pv);
  if (!fork()) {
    load_signal();
    pipe(&pv[2]);
    if (!fork()) {
      close(pv[2]);
      fp = fdopen(pv[3], "w");
      if (pp->f.code)
	for (; pp; pp = pp->next) {
	  for (i = pp->f.len, cp = pp->cs; i; i--, cp++)
	    if (*cp < ' ' || *cp == K_dl)
	      fprintf(fp, "%c%c", K_a0, *cp+'@');
	    else if (*cp > K_dl) fputc('*', fp);
	    else fputc(*cp, fp);
	  fputc('\n', fp);
	}
      else
	for (; pp; pp = pp->next) {
	  if (!pp->f.len) fputc(' ', fp);
	  else if (!f_bs && pp->attr) /* boldまたはunderlineの属性 */
	    for (ce = pp->cs + pp->f.len, cp = pp->cs; cp < ce; cp++)
	      if (*cp == K_h)
		if (cp[1] == K_h) cp += 3;
	        else if (cp[1] >= 0x80) cp += 2;
		else cp++;
	      else if (*cp == '_')
		if (cp[1] == '_' && cp[2] == K_h && cp[3] == K_h) 
		  cp += 3;
		else if (cp[1] == K_h) cp++;
		else fputc(*cp, fp);
	      else fputc(*cp, fp);
	  else if (pp->f.nl && pp->f.str)
	    fwrite(pp->cs, 1, strlen(pp->cs), fp); /* 完全なファイル名のため */
	  else fwrite(pp->cs, 1, pp->f.len, fp);
	  fputc('\n', fp);
	}
      fclose(fp);
      exit(0);
    }
    close(0);
    dup2(pv[2], 0);
    close(pv[2]);
    close(pv[3]);
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    if (pp->f.code) print_ctrl(K_a0, str);
    chp = get_child(NULL, NULL
		    , (f_fgrep) ? s_S(S_fgrep) : s_S(S_egrep), str, NULL);
    execvp(*chp->argv, chp->argv);
  }
  close(pv[1]);
  cp = read_buff(sizep, pv[0], 0);
  close(pv[0]);
  return cp;
}  

u_char *fork_grep2(int *sizep, u_char *buff, u_char str[], char name[])/*usilic*/
{
  static u_char line0[N_pipe], line1[N_pipe];
  u_char *cp, *tp;
  int c, i, j, len, pos, tell, f_jis, pv[6];
  FILE *fp, *fp0, *fp1;

  if ((f_jis = vt_charset(0) != C_ascii)) {
    if (buff) {
      sscanf(name, "%d", &len);
      if (len > N_pipe) len = N_pipe;
      memcpy(line0, buff, len);
    } else if ((fp = fopen(name, "r"))) {
      len = fread(line0, 1, N_pipe, fp);
      fclose(fp);
    } else return NULL;
    switch(init_code(&len, line0, 0)) {
    case C_euc:
    case C_sjis:
      f_jis = 0;
      break;
    default:
      f_jis = 1;
      break;
    }
  }
  if (f_jis) {
    u_char *p = alloca(strlen(str) * 2);
    for (i = j = 0; str[i]; i++, j++) {
      /* EUCをJISへ変換する */
      if ((c = str[i]) >= 0x80)
	switch((c &= ~0x80)) {
	case '$': case '^': case '[': case ']':
	case '\\': case '.': case '+': case '*':
	case '|': case '(': case ')': case '?':
	  p[j++] = '\\';
	  break;
	default:
	  break;
	}
      p[j] = c;
    }
    p[j] = '\0';
    pipe(pv);
    if (!fork()) {
      load_signal();
      pipe(&pv[2]);
      if (!fork()) {
	if (buff) {
	  pipe(&pv[4]);
	  if (!fork()) {
	    close(pv[4]);
	    fp = fdopen(pv[5], "w");
	    sscanf(name, "%d", &i);
	    for (; i; i--) fputc(*buff++, fp);
	    fclose(fp);
	    exit(0);
	  }
	  close(0);
	  dup2(pv[4], 0);
	  close(pv[4]);
	  close(pv[5]);
	}
	dup_error();
	close(1);
	dup2(pv[3], 1);
	close(pv[3]);
	close(pv[2]);
	execlp(s_S(S_egrep), s_S(S_egrep), "-b", p
	       , (buff) ? NULL : name, NULL);
      }
      close(pv[3]);
      fp = (tp = buff) ? NULL : fopen(name, "r");
      fp0 = fdopen(pv[2], "r");
      fp1 = fdopen(pv[1], "w");
      len = f_jis = 0;
      tell = -1;
      while (fgets(line0, N_pipe, fp0)) {
	if (!(cp = strchr(line0, ':'))) continue;
	cp[1] = '\0';
	i = atoi(line0);
	if (tell < 0) pos = i;
	else pos = i - tell - len;
	tell = i;
	if (tp) {
	  tp += pos;
	  tp = sgets(line1, N_pipe, tp);
	} else {
	  fseek(fp, pos, 1);
	  fgets(line1, N_pipe, fp);
	}
	fwrite(line0, 1, strlen(line0), fp1);
	len = cat_mime(line1, fp, fp1, (tp) ? &tp : NULL);
      }
      if (!tp) fclose(fp);
      fclose(fp0);
      fclose(fp1);
      exit(0);
    }
  } else {     
    pipe(pv);
    if (!fork()) {
      load_signal();
      if (buff) {
	pipe(&pv[2]);
	if (!fork()) {
	  close(pv[2]);
	  fp = fdopen(pv[3], "w");
	  sscanf(name, "%d", &i);
	  for (; i; i--) fputc(*buff++, fp);
	  fclose(fp);
	  exit(0);
	}
	close(0);
	dup2(pv[2], 0);
	close(pv[2]);
	close(pv[3]);
      }
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[1]);
      close(pv[0]);
      execlp(s_S(S_egrep), s_S(S_egrep), "-b", str
	     , (buff) ? NULL : name, NULL);
    }
  }
  close(pv[1]);
  tp = read_buff(sizep, pv[0], 0);
  close(pv[0]);
  return tp;
}  

FILE *fork_man(char str[])/*usilic*/
{
  int argc;
  static int pv[2];
  char *p, *cp, **argv, env[N_char] = {'\0',};
  CHILD *chp, child;

  pipe(pv);
  argv = alloc_arg(str, &argc);
  if ((cp = strchr(argv[0], '='))) {
    strcpy(env, argv[0]);
    cp = &strchr(str, ' ')[1];
  } else cp = str;
  p = alloca(strlen(s_S(S_man)) + 1 + strlen(cp) + 1);
  sprintf(p, "%s %s", s_S(S_man), cp);
  free_arg(argv);
  argv = alloc_arg(p, &argc);
  chp = &child;
  chp->argv = argv;
  if (!fork()) {
    signal(SIGCHLD, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    dup_error();
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    unsetenv("LC_CTYPE");
    setenv("LANG", (vt_charset(0) == C_ascii) ? "C" : s_S(S_LANG), 1);
    setenv("PAGER", "cat", 1);
    if (*env) {
      cp = strchr(env, '=');
      *cp = '\0';
      setenv(env, &cp[1], 1);
    }
    execvp(*chp->argv, chp->argv);
  }
  free_arg(argv);
  close(pv[1]);
  return fdopen(pv[0], "r");
}

FILE *fork_pipe(int f_chp, CHILD *chp, char name[])/*usilic*/
{
  int pv[6];
  CHILD *chp2;

  if (f_chp == 9 || f_chp == 7 || f_chp == 6) {
    if (f_chp == 9)
      chp = get_child(NULL, ".tgz", s_S(S_tar), NULL, NULL);
    chp2 = (CHILD*)chp->argv[chp->f.argc + 1];
    chp2 = get_child(chp2, NULL, chp2->argv[0]
		     , (f_chp == 9) ? "-f" : name, NULL);
  } else {
    strcpy(chp->argv[chp->f.argc], name);
    chp2 = NULL;
  }
  pipe(pv);
  if (!fork()) {
    load_signal();
    dup_error();
    if (chp2) {
      pipe(&pv[2]);
      if (!fork()) {
	if (f_chp == 9) {
	  pipe(&pv[4]);
	  if (!fork()) {
	    close(1);
	    dup2(pv[5], 1);
	    close(pv[5]);
	    close(pv[4]);
	    execlp("ar", "ar", "p", name, "data.tar.gz", NULL);
	  }
	  close(pv[5]);
	  close(0);
	  dup2(pv[4], 0);
	  close(pv[4]);
	}
	close(1);
	dup2(pv[3], 1);
	close(pv[3]);
	close(pv[2]);
	execvp(*chp2->argv, chp2->argv);
      }
      close(0);
      dup2(pv[2], 0);
      close(pv[2]);
      close(pv[3]);
    }
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    execvp(*chp->argv, chp->argv);
  }
  close(pv[1]);
  return fdopen(pv[0], "r");
}

char *fork_file(char name[])/*usilic*/
{
  static char str[N_pipe];
  static CHILD *chp;
  int pid, pv[2];
  FILE *fp;
  
  if (!chp && !(chp = check_chp(S_file))) return NULL;
  pipe(pv);
  if (!(pid = fork())) {
    load_signal();
    close(1);
    dup2(pv[1], 1);
    close(pv[0]);
    close(pv[1]);
    strcpy(chp->argv[chp->f.argc], name);
    execvp(*chp->argv, chp->argv);
  }
  close(pv[1]);
  fp = fdopen(pv[0], "r"); 
  fgets(str, N_pipe, fp);
  fclose(fp);
  return str;
}

int fork_du(u_char dir[])/*usilic*/
{
  static char mess[N_line];
  static int pv[4];
  
  if (!*mess) {
    strcpy(mess, s_S(S_enter));
    (rindex(mess, '/'))[1] = 'D';
  } else if (!access(mess, F_OK)) return -1;
  pipe(pv);
  if (!fork()) {
    int i, j, size = 0;
    u_char *buff;
    CHILD *chp;
 
    creat(mess, 0);
    load_signal();
    chdir(dir);
    pipe(&pv[2]);
    if (!fork()) {
      chp = get_child(NULL, "...", s_S(S_du), NULL, NULL);
      dup_error();
      close(1);
      dup2(pv[3], 1);
      close(pv[3]);
      close(pv[2]);
      execvp(*chp->argv, chp->argv);
    }
    close(pv[3]);
    buff = read_buff(&size, pv[2], 3);
    close(pv[2]);
    creat(s_S(S_enter), 0);
    unlink(mess);
    close(pv[0]);
    for (i = 0; size >= 0; i += N_pipe, size -= N_pipe) {
      j = (size < N_pipe) ? size : N_pipe;
      write(pv[1], &buff[i], j);
    }
    close(pv[1]);
    exit(0);
  }
  close(pv[1]);
  return pv[0];
}

char *read_arc(CHILD *arc, char prev[], char name[], int *sizep)/*usilic*/
{
  CHILD *chp = NULL, *chp2 = NULL;
  int fd, i, pv[6], f_deb, mode, size, length, f_chkbin = 0;
  FILE *fp;
  char *p, head[N_pipe];
  u_char c, *buff;

  *sizep = 0;
  if ((f_deb = !strcmp(arc->argv[0], s_S(S_deb)))
      || !strcmp(arc->argv[0], s_S(S_tar))) {
    chp = (f_deb) ? get_child(NULL, ".tgz", s_S(S_tar), NULL, NULL) : arc;
    if (!chp->argv[chp->f.argc]) {
      chp2 = (CHILD*)chp->argv[chp->f.argc + 1];
      chp2 = get_child(chp2, NULL, chp2->argv[0], (f_deb) ? "-f" : prev, NULL);
      pipe(pv);
      if (!fork()) {
	load_signal();
	pipe(&pv[2]);
	if (!fork()) {
	  if (f_deb) {
	    pipe(&pv[4]);
	    if (!fork()) {
	      dup_error();
	      close(1);
	      dup2(pv[5], 1);
	      close(pv[5]);
	      close(pv[4]);
	      execlp("ar", "ar", "p", prev, "data.tar.gz", NULL);
	    }
	    close(0);
	    dup2(pv[4], 0);
	    close(pv[4]);
	    close(pv[5]);
	  } else dup_error();
	  close(1);
	  dup2(pv[3], 1);
	  close(pv[3]);
	  close(pv[2]);
	  execvp(*chp2->argv, chp2->argv);
	}
	close(1);
	dup2(pv[1], 1);
	close(pv[1]);
	close(pv[0]);
	close(0);
	dup2(pv[2], 0);
	close(pv[2]);
	close(pv[3]);
	execlp(chp->argv[0], chp->argv[0], "xfO", chp->argv[2]
	       , name, NULL);
      }
    } else {
      pipe(pv);
      if (!fork()) {
	load_signal();
	dup_error();
	close(1);
	dup2(pv[1], 1);
	close(pv[1]);
	close(pv[0]);
	execlp(chp->argv[0], chp->argv[0], "xfO", prev, name, NULL);
      }
    }
    close(pv[1]);
    fd = pv[0];
  } else if (!strcmp(arc->argv[0], s_S(S_lha))) {
    chp = get_child(NULL, NULL, s_S(S_lha), prev, NULL); /* `.lzh lha p' */
    pipe(pv);
    if (!fork()) {
      load_signal();
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[0]);
      close(pv[1]);
      execlp(chp->argv[0], chp->argv[0], chp->argv[1]
	     , chp->argv[2], name, NULL);
    }
    close(pv[1]);
    for (i = 3; i; ) {
      read(pv[0], &c, 1);
      if (c == '\n') i--;
    }
    fd = pv[0];
  } else if (!strcmp(arc->argv[0], s_S(S_unzip))) {
    chp = get_child(NULL, NULL, s_S(S_unzip), prev, NULL); /* `.zip unzip -p' */
    pipe(pv);
    if (!fork()) {
      load_signal();
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[0]);
      close(pv[1]);
      execlp(chp->argv[0], chp->argv[0], chp->argv[1]
	     , chp->argv[2], name, NULL);
    }
    close(pv[1]);
    fd = pv[0];
  } else if (!strcmp(arc->argv[0], s_S(S_rpm))) {
    chp = get_child(NULL, NULL, s_S(S_rpm2cpio), prev, NULL);
    if (!chp->argv[chp->f.argc]) {
      chp2 = (CHILD*)chp->argv[chp->f.argc + 1];
      chp2 = get_child(chp2, NULL, chp2->argv[0], name, NULL);
    }
    pipe(pv);
    if (!fork()) {
      load_signal();
      if (chp2) {
	pipe(&pv[2]);
	if (!fork()) {
	  dup_error();
	  close(1);
	  dup2(pv[3], 1);
	  close(pv[3]);
	  close(pv[2]);
	  execvp(*chp2->argv, chp2->argv);
	}
	close(pv[3]);
	close(0);
	dup2(pv[2], 0);
	close(pv[2]);
      } else dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[0]);
      close(pv[1]);
      execvp(*chp->argv, chp->argv);
    }
    close(pv[1]);
    fp = fdopen(pv[0], "r");
    while (1) {
      fread(head, 1, 6 + 8*1, fp);
      fscanf(fp, "%8x", &mode);
      fread(head, 1, 8*4, fp);
      fscanf(fp, "%8x", &size);
      fread(head, 1, 8*4, fp);
      fscanf(fp, "%8x", &length);
      fread(head, 1, 8*1, fp);
      p = calloc(1, length + 1);
      fread(p, 1, length, fp);
      for (i = (4 - ((length + 110) % 4)) % 4; i; i--) 
	if (fgetc(fp) == EOF) {
	  close(pv[0]);
	  *sizep = 0;
	  return NULL;
	}
      c = !strcmp(p, &name[*name == '/']);
      free(p);
      if (c) break;
      if (size) {
	for (i = size; i > 0; i -= N_pipe)
	  if (i > N_pipe) {
	    if (fread(&head, 1, N_pipe, fp) < N_pipe) break;
	  } else fread(&head, 1, i, fp);
      }
      for (i = (4 - (size % 4)) % 4; i; i--) fgetc(fp);
    }
    if (S_ISDIR(mode) || S_ISLNK(mode)) size = 0;
    if (!size) {
      close(pv[0]);
      *sizep = 0;
      return NULL;
    }
    *sizep = size;
    fd = (u_int)fp;
    f_chkbin = -1;
  } else {
    *sizep = 0;
    return NULL;
  }
  buff = read_buff(sizep, fd, f_chkbin);
  close(pv[0]);
  return buff;
}

