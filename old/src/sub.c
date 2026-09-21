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

/*
 * 【機能】SIGSEGVのハンドラ。中断の旗(Sub.abort)を立てて、longjmpで呼び出し元へ戻る(履歴ファイルの
 *       読み込み中のクラッシュ回収)。
 * 【C++】なし(C++版はSIGSEGVを回収しない)
 */
static void sub_clear(int dummy)
{
  Sub.abort = 1;
  longjmp(Sub.jb, 0);
}

/*
 * 【機能】sscanfで読めるよう、文字列中の制御文字と空白を、印(`K_o`+文字、`K_a`)へ変換する(履歴ファイル用)。
 * 【C++】History::serialize (src/cpp/src/history.cpp)
 */
static void sspc(u_char *cp, u_char *np)
{ /* sscanf()のために空白文字を変換する */
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

/*
 * 【機能】sspcの逆。印(`K_o`+文字、`K_a`)を、元の制御文字と空白に戻す(履歴ファイルの読み込み用)。
 * 【C++】History::loadHistoryText (src/cpp/src/history.cpp)
 */
void rspc(u_char *cp, u_char *np)/*usilic*/
{ /* sscanf()のために空白文字を変換する */
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

/*
 * 【機能】LIST(二分木)を、左・自分・右の順(=名前の順)に、1行ずつファイルへ書き出す。
 * 【C++】History::serialize (src/cpp/src/history.cpp)
 */
static void print_list(LIST *lp, FILE *fp)
{
  if (lp) {
    print_list(lp->left, fp);
    fprintf(fp, "%s\n", lp->name);
    print_list(lp->right, fp);
  }
}

/*
 * 【機能】履歴ファイルから、モードmの履歴(STR)を読み込む。見出し行(`K_l`+モード名)から次の見出しまで。
 * 【C++】History::loadHistoryText (src/cpp/src/history.cpp)
 */
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

/*
 * 【機能】履歴(STR)を、順序を保ったまま履歴ファイル(~/.<プログラム名>/history)へ書き出す。
 * 【C++】History::serialize (src/cpp/src/history.cpp)
 */
static void write_str(FILE *fp, STR *sp, LIST *lp, int f, int m)
{ /* STRリストの順序で~/.usilic/historyへ文字列を保存する */
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

/*
 * 【機能】履歴(STR)から、名前が一致する項目を削除する(名前をNULLにする)。
 * 【C++】History (src/cpp/src/history.cpp)
 */
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

/*
 * 【機能】ディレクトリ名(dir)を、親ディレクトリの名前に置き換える(`/`ならそのまま)。
 * 【C++】DirectoryMode::goToParent (src/cpp/src/directory_mode.cpp)
 */
static void get_par(char *dir)
{ /* 親ディレクトリ名をdir[]へ設定する */
  if (!strcmp(dir, "/")) return;
  for (dir = &strchr(dir, '\0')[-2]; *dir != '/'; dir--) ;
  dir[1] = '\0';
}

/*
 * 【機能】現在の時刻を、asctimeの形式の文字列にして返す(履歴ファイルの先頭用)。
 * 【C++】currentTimestampText (src/cpp/app/main.cpp)
 */
static void get_time(char str[])
{
  struct tm tt;
  time_t ll;

  time(&ll);
  memcpy(&tt, localtime(&ll), sizeof(struct tm));
  strcpy(str, asctime(&tt));
  *strchr(str, '\n') = '\0';
}

/*
 * 【機能】PROGRAMの候補リスト(prog)の中に、buffのコマンド名と一致するものがあるかを、二分木の順に探す。
 * 【C++】Shell::onTab (src/cpp/src/shell.cpp)
 */
int find_prog(LIST *prog, u_char buff[])/*usilic*/
{ /* PROGRAMのprogram-listの中でbuff[]を検索する */
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

/*
 * 【機能】GREPの状態を積んだ連結リストから、先頭の1つを取り除いて(解放)、その1つ前を返す。
 * 【C++】ModeStack (src/cpp/src/mode_stack.cpp)
 */
GREP *load_grep(GREP *grep)/*usilic*/
{
  GREP *gp;

  if (!grep) return grep;
  gp = grep->prev;
  free(grep);
  return gp;
}

/*
 * 【機能】GREPの状態を、連結リストへ新しく1つ積んで返す。
 * 【C++】ModeStack (src/cpp/src/mode_stack.cpp)
 */
GREP *save_grep(GREP *grep)/*usilic*/
{
  GREP *gp;

  gp = calloc(sizeof(GREP), 1);
  gp->prev = grep;
  return gp;
}


/*
 * 【機能】GREPの状態の連結リストの中に、表示領域(bp)が対応するものがあるかを探す。
 * 【C++】ModeStack (src/cpp/src/mode_stack.cpp)
 */
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

/*
 * 【機能】ファイル(fp)に書かれた名前を1行ずつ読み、それらを履歴(STR)から削除して、LISTを返す。
 * 【C++】History (src/cpp/src/history.cpp)
 */
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
    while (rsp) { /* 組み込み */
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

/*
 * 【機能】履歴(STR)の末尾へ、名前と位置(PN)を追加する(再帰で末尾まで辿る)。
 * 【C++】History::pushRecent (src/cpp/src/history.cpp)
 */
static STR *save_hist(STR *hist, char name[], PN pn)
{
  if (!hist) {
    hist = calloc(sizeof(STR), 1);
    hist->name = strdup(name);
    memcpy(&hist->pn, &pn, sizeof(PN));
  } else hist->next = save_hist(hist->next, name, pn);
  return hist;
}

/*
 * 【機能】履歴(STR)の末尾の項目が、名前と一致するかを調べ、個数をcountpへ返す。
 * 【C++】History::pushRecent (src/cpp/src/history.cpp)
 */
static int find_hist(STR *hist, char *name, int *countp)
{
  int count;

  if (!hist) count = 0;
  else for (count = 1; hist->next; hist = hist->next) count++;
  *countp = count;
  if (!hist || name == F6 || strcmp(name, hist->name)) return 0;
  return 1;
}

/*
 * 【機能】履歴(STR)へ、名前を追加する。直前と同じなら追加しない。保存する個数の上限も管理する。
 * 【C++】History::pushRecent; History::retentionCount (src/cpp/src/history.cpp)
 */
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
  if (name[count] == '\r') /* '\r'はSHELLプロンプト */
    name[count] = '\0';
  if (f_find && find_hist(hist, name, &count)) return hist;
  if (hist && count == n_history) {
    sp = save_hist(hist->next, name, pn);
    free(hist->name);
    free(hist);
  } else sp = save_hist(hist, name, pn);
  return sp;
}

/*
 * 【機能】履歴(STR)のうち、保存する個数(n_hist)を超えた古いものを、先頭から取り除く。
 * 【C++】History::pushRecent (src/cpp/src/history.cpp)
 * 【備考】C++は、上限を超えたら古いものを、追加時に落とす。
 */
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

/*
 * 【機能】LIST(二分木)の中に、名前があるかを探す(見つかれば*ipを1にする)。
 * 【C++】History::containsName (src/cpp/src/history.cpp)
 */
void find_list(LIST *lp, char name[], int *ip)/*usilic*/
{
  if (lp && !*ip) {
    find_list(lp->left, name, ip);
    if (!strcmp(lp->name, name)) *ip = 1;
    find_list(lp->right, name, ip);
  }
}

/*
 * 【機能】STR(連結リスト)へ、名前と位置(PN)を追加する。既にある名前は追加しない。
 * 【C++】History::pushRecent (src/cpp/src/history.cpp)
 */
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

/*
 * 【機能】モードのスタックを辿って、表示領域(f_read=0)または名前(f_read=1)が一致するものを探す。
 * 【C++】ModeStack (src/cpp/src/mode_stack.cpp)
 */
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

/*
 * 【機能】モードのスタックの先頭を降ろして(戻る)、その時の状態(LINE、モード、名前、表示領域)を復元する。
 * 【C++】ModeStack::pop (src/cpp/src/mode_stack.cpp)
 */
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

/*
 * 【機能】現在のモードの状態(直前のモード、名前、ディレクトリ、表示領域等)を、モードのスタックへ積む。
 * 【C++】ModeStack::push (src/cpp/src/mode_stack.cpp)
 */
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

/*
 * 【機能】カレントディレクトリを、モードのスタックの底として積む。
 * 【C++】ModeStack::pushFloor (src/cpp/src/mode_stack.cpp)
 */
STACK *save_stack_dir(STACK *sp, char dir[], char old[], BASE *bp)/*usilic*/
{ /* カレントディレクトリを保存する(スタックの底) */
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

/*
 * 【機能】パス名(pathname)を、最後の`/`より後(ファイル名だけ)に置き換える。
 * 【C++】PathResolver (src/cpp/src/path_resolver.cpp)
 */
void get_name(char *pathname)/*usilic*/
{ /* 通常のファイル名をpathnameへ設定する */
  char *cp, *cp0 = pathname;

  while ((cp = strchr(pathname, '/'))) pathname = &cp[1];
  if (cp0 != pathname) strcpy(cp0, pathname);
}

/*
 * 【機能】パス名(pathname)を、最後の`/`まで(ディレクトリ部分だけ)にする。`/`が無い/ルートなら0を返す。
 * 【C++】PathResolver (src/cpp/src/path_resolver.cpp)
 */
int get_dir(char *pathname)/*usilic*/
{ /* カレントディレクトリ名をpathnameへ設定する */
  char *cp;

  if (!strchr(pathname, '/') || !strcmp(pathname, "/")) return 0;
  while ((cp = strchr(pathname, '/'))) pathname = &cp[1];
  *pathname = 0;
  return 1;
}

/*
 * 【機能】パス名から、`./`(カレント)と`../`(親)を整理して取り除く。
 * 【C++】PathResolver::resolve (src/cpp/src/path_resolver.cpp)
 */
void erase_dot(char name[])/*usilic*/
{ /* name[]からカレント("./")または親("../")ディレクトリ名を取り除く */
  int i, j;
  char *cp, tmp[N_line];

  for (cp = name; *cp && (cp = strstr(cp, "./")); )
    if (cp > name && cp[-1] == '.') { /* "../"を読み飛ばす */
      cp++;
      continue;
    } else if (cp[2]) memmove(cp, &cp[2], strlen(&cp[2]) + 1); /* "./"を消す */
    else *cp = '\0';
  if ((cp = strstr(name, "../"))) {
    strcpy(tmp, cp);
    *cp = '\0';
    while (strlen(tmp) && (cp = strstr(tmp, "../")))
      if (cp == tmp) {
	get_par(name);
	memmove(tmp, &cp[3], strlen(&cp[3]) + 1);
      } else {
	i = strlen(name);
	j = cp - tmp;
	memcpy(&name[i], tmp, j);
	name[i + j] = '\0';
	memmove(tmp, cp, strlen(cp) + 1);
      }
  } else *tmp = '\0';
  if (strlen(tmp)) strcat(name, tmp);
}

/*
 * 【機能】履歴ファイル(~/.<プログラム名>/history)を読み込み、FILE/DIR/MAN/LOCATE/GREP/PROGRAM等の
 *       履歴(LL/SS)と、位置の記憶を復元する。
 * 【C++】History::loadHistoryText (src/cpp/src/history.cpp)
 * 【備考】C++は、履歴の読み込みをmainが行い、解釈をHistoryが行う。
 */
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
    unlink(s_S(S_hist)); /* ~/.usilic/historyを削除する */
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
    if (!strchr(str, K_z)) { /* ~/.usilic/historyに`EOF (^Z)'が含まれていない */
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
  if (*lp->old == '.') { /* mode-line: ./ (ls)の場合 */
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
      if (!env_charset(-1)) vt_charset((charset) ? charset : C_utf8);
      /* else: USILICCHARSETがユーザーによって明示的にexportされて
	 いたので、前回~/.usilic/historyに保存されていたものより
	 そちらが優先される。(ここでcharset==0は、保存済みの
	 history行がこのフィールドより前のものであるか、あるいは
	 何らかの理由でこれが欠けていることを意味する――EUCではなく、
	 文書化されたUTF-8の既定値へフォールバックする。) */
    }
    f = 1;
  } while (0);
  if (!f) { /* ~/.usilic/historyが壊れているか古い形式である */
    save_error("%s: %s: %s.\n", s_S(S_usilic), s_S(S_hist), s_S(S_errm));
    kill(getpid(), SIGSEGV);
  }
  fclose(fp);
}

/*
 * 【機能】履歴(LL/SS)を、履歴ファイルへ書き出す。書く前に、古いファイルを`.old`へ退避する。
 * 【C++】History::serialize (src/cpp/src/history.cpp)
 */
void write_list(LL ll, SS ss, LINE *lp, char name[])/*usilic*/
{
  FILE *fp;
  char str[N_line], str1[N_line];
  
  if (!s_S(S_hist)) return;
  strcpy(str, s_S(S_hist));
  strcat(str, ".old");
  rename(s_S(S_hist), str); /* バックアップ */
  if (!(fp = fopen(s_S(S_hist), "w"))) return;
  get_time(str);
  fprintf(fp, "# ~/." PROGNAME "/history: %s\n", getenv(ENV_PROG));
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

/*
 * 【機能】LIST(二分木)の全体を解放する。
 * 【C++】なし(C++はデストラクタとstd::vectorの自動解放で足りる)
 */
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

/*
 * 【機能】STR(連結リスト)の全体を解放する。
 * 【C++】なし(C++はデストラクタとstd::vectorの自動解放で足りる)
 */
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

/*
 * 【機能】履歴(STR)から、モード(f_M)に応じた表示用のバッファ(bp)を作る(履歴の一覧画面)。
 * 【C++】ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp)
 */
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

/*
 * 【機能】LIST(二分木)から、モード(f_M)に応じた表示用のバッファ(bp)を作る(候補・履歴の一覧画面)。
 * 【C++】ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp)
 */
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

/*
 * 【機能】LIST(二分木)から名前を探して、記憶していた位置(PN)を返す(見つかれば1)。
 * 【C++】History::positionFor (src/cpp/src/history.cpp)
 */
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

/*
 * 【機能】LIST(二分木)へ、名前と位置(PN)を、名前の順(ソート順)に追加する。
 * 【C++】History::setPosition; History::rememberName (src/cpp/src/history.cpp)
 */
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

/*
 * 【機能】unsigned intを、10進の文字列へ変換して返す。
 * 【C++】なし(C++はstd::to_stringで足りる)
 */
char *utoa(u_int u)/*usilic*/
{ /* unsigned-intを文字列へ変換する */
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

/*
 * 【機能】文字列を、太字の重ね打ち(`X^HX`)へ変換する。
 * 【C++】なし(C++は重ね打ちにせず、AttributeRunで太字を持つ)
 */
char *tobo(char str[], int f_nl)/*usilic*/
{ /* str[]をbold属性へ変換する */
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

/*
 * 【機能】文字列を、下線の重ね打ち(`_^HX`)へ変換する。
 * 【C++】なし(C++は重ね打ちにせず、AttributeRunで下線を持つ)
 */
char *toul(char str[], int f_nl)/*usilic*/
{ /* str[]をunderline属性へ変換する */
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

/*
 * 【機能】文字列から、重ね打ち(`X^H..`)の部分を取り除き、改行までを複写して返す。
 * 【C++】なし(C++は重ね打ちにせず、AttributeRunで属性を持つ)
 */
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

/*
 * 【機能】文字列(str0)の中で、文字cまたは文字列strが最初に現れる位置(1始まり)を返す(無ければ0)。
 * 【C++】なし(C++はstd::string::findで足りる)
 */
int ichrstr(char str0[], char c, char *str)/*usilic*/
{
  char *cp;

  if ((str && (cp = strstr(str0, str))) || (!str && (cp = strchr(str0, c))))
    return (int)(cp - str0) + 1;
  else return 0;
}

/*
 * 【機能】モードラインの引数欄(バイト数と`(プログラム名)`)を組み立てる。
 * 【C++】ContentView::ModeLineFields (src/cpp/src/content_view.cpp)
 */
void print_arg(char arg[], int size, u_char *buff, CHILD *chp, int f_M)/*usilic*/
{
  *arg = K_z; /* mode-line[4]を表示しない */
  if (chp)
    /* 2026-08-20修正(この同じ報告のsize=-1側についてはmenu.c自身の
       呼び出し箇所のコメント参照): s_S(S_sh)(設定されたSHELL
       コマンド文字列)が、アーカイブ表示とは全く明確な関係の無い、
       chp駆動の全てのモード(その主なものがARC)についてここの
       バイト数をゲートしていた――すぐ下のT_dir/T_man自身のほぼ
       同一な`size >= 0'の分岐にはそのようなゲートは無い。この2つに
       合わせて削除した。これは、usilicrcにSHELLセクションが設定
       されていない場合に、バイト数がそもそも表示され得る唯一の
       方法でもある(直接確認済み: その場合s_S(S_sh)はNULLであり、
       本物の有効なサイズに関わらず無条件にこれを抑制していた)。 */
    if (size >= 0 && f_M != T_pipe5)
      sprintf(arg, " %d (%s)", size, *chp->argv);
    else sprintf(arg, " (%s)", *chp->argv);
  else
    switch(f_M) {
    case T_dir:
      if (size >= 0) sprintf(&arg[1], "%d (%s)", size, s_S(S_ls));
      else sprintf(&arg[1], "(%s)", s_S(S_ls));
      break;
    case T_du:
      sprintf(&arg[1], "(%s)", s_S(S_du));
      break;
    case T_man:
      if (size >= 0) sprintf(&arg[1], "%d (%s)", size, s_S(S_man));
      else sprintf(&arg[1], "(%s)", s_S(S_man));
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

/*
 * 【機能】子プロセスの標準エラー出力を、エラーファイル(S_error)へ付け替える。
 * 【C++】ChildProcess (src/cpp/src/child_process.cpp)
 * 【備考】C++は、ChildProcess::Optionsのstderrへ書き出す先を渡す。
 */
void dup_error()/*usilic*/
{
  int fd;
  FILE *fp;

  if (!s_S(S_error)) return;
  fp = fopen(s_S(S_error), "a");
  if (!fp) return; /* fileno(NULL)は未定義動作である; fopen()は失敗し得る
		       (例えば親ディレクトリのENOENT)が、以前はここで
		       確認していなかった */
  fd = fileno(fp);
  dup2(fd, 2);
  close(fd);
}

/*
 * 【機能】書式付きのエラーメッセージを、エラーファイル(S_error)へ追記する(ERROR画面で見られる)。
 * 【C++】ChildProcess (src/cpp/src/child_process.cpp)
 */
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

/*
 * 【機能】端末を元に戻した後に、標準エラーへ出す内容を、ファイル(S_stderr)へ保存しておく。
 * 【C++】main (src/cpp/app/main.cpp)
 */
void save_stderr(char *fmt, ...)/*usilic*/
{ /* 端末をリセットした後に`stderr'へ表示する */
  FILE *fp;
  va_list ap;

  if (!s_S(S_stderr)) return;
  fp = fopen(s_S(S_stderr), "w");
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  fclose(fp);
}

/*
 * 【機能】中断・エラーの番号を、ファイル(S_intr)へ書く(別のプロセスへ知らせるため)。
 * 【C++】なし(C++は単一のプロセスなので、状態を変数で持つ)
 */
void save_intr(int n_error)/*usilic*/
{
  FILE *fp;

  if (!n_error) return;
  fp = fopen(s_S(S_intr), "w");
  fprintf(fp, "%d\n", n_error);
  fclose(fp);
}

/*
 * 【機能】save_intrが書いた、中断・エラーの番号を読む(無ければ0)。
 * 【C++】なし(C++は単一のプロセスなので、状態を変数で持つ)
 */
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

/*
 * 【機能】PATH(コロン区切り)の中に、プログラム(prog)があるかを探す(インストールの確認)。
 * 【C++】なし(C++は、実行の失敗で不在を知る。PATHの探索はTab補完(Shell::onTab)だけが行う)
 */
char *is_inst(char *path, char *prog)/*usilic*/
{ /* `path'の中から`prog'を探す */
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

/*
 * 【機能】ファイル名(name)にアクセスできるかを調べ、できなければエラーメッセージ用のバッファ(bp)を作って-1を
 *       返す。
 * 【C++】DirectoryMode::reportError (src/cpp/src/directory_mode.cpp)
 */
int is_access(char name[], BASE *bp)/*usilic*/
{
  struct stat st;

  if (access(name, R_OK|F_OK) < 0) { /* そのようなファイルやディレクトリは無い */
    make_bp(bp, strerror(errno), name, NULL);
    return -1;
  } else {
    stat(name, &st);
    if (!st.st_size) { /* サイズが無い */
      make_bp(bp, s_S(S_err1), name, NULL);
      return -1;
    }
  }
  return 0;
}  

/*
 * 【機能】モード(f_M)に対応する履歴(STR)の番号と、表示領域の番号(*n_bp)を返す(履歴の一覧画面の種類)。
 * 【C++】RingCategory (src/cpp/src/history.hpp)
 */
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
  
/*
 * 【機能】モード(f_M)に対応する候補・履歴(LIST)の番号と、表示領域の番号(*n_bp)を返す。
 * 【C++】RingCategory (src/cpp/src/history.hpp)
 */
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
  
static char pty_err[128] = "";

/*
 * 【機能】擬似端末を開けなかった理由の文字列を返す(記録が無ければ`unknown reason`)。
 * 【C++】Shell::start (src/cpp/src/shell.cpp)
 */
char *pty_strerror(void)/*usilic*/
{
  return pty_err[0] ? pty_err : "unknown reason";
}

/*
 * 【機能】擬似端末を開く。POSIX(Unix98)のAPIで、スレーブのパス(Linuxの`/dev/pts/N`、macOSの`/dev/ttysNNN`)を
 *       strへ返す。f_openで、開くか、名前だけかを切り替える。
 * 【C++】Shell::start (src/cpp/src/shell.cpp)
 */
int open_pty(char str[], int f_open)/*usilic*/
{
  /*
   * /dev/ptyp0.."/dev/ptysf"のようなレガシーなBSDデバイスノードを
   * 走査する代わりに、POSIX(Unix98)のpty APIを使った、移植性の
   * ある(Linux/macOS/*BSD)pty取得。そのようなデバイスノードは、
   * 特別なカーネル設定無しでは現代のカーネルにはもう存在しない
   * (そしてmacOSには元々存在しない)。str[]は*slave*デバイスパス
   * (Linuxでは例えば"/dev/pts/4"、macOSでは"/dev/ttys004")を
   * 受け取る; 呼び出し元はstr[]を直接openしてslave側を得ればよく、
   * もはや「5文字目を'p'と't'の間で切り替える」というトリックは
   * 不要である。
   */
  int tfd;
  char *slave;

  pty_err[0] = '\0';
  if ((tfd = posix_openpt(O_RDWR | O_NOCTTY)) < 0) {
    snprintf(pty_err, sizeof(pty_err), "posix_openpt: %s", strerror(errno));
    return -1;
  }
  if (grantpt(tfd) < 0) {
    snprintf(pty_err, sizeof(pty_err), "grantpt: %s", strerror(errno));
    close(tfd);
    return -1;
  }
  if (unlockpt(tfd) < 0) {
    snprintf(pty_err, sizeof(pty_err), "unlockpt: %s", strerror(errno));
    close(tfd);
    return -1;
  }
  if (!(slave = ptsname(tfd))) {
    snprintf(pty_err, sizeof(pty_err), "ptsname: %s", strerror(errno));
    close(tfd);
    return -1;
  }
  strcpy(str, slave);
  if (f_open) return tfd;
  close(tfd);
  return 1;
}

/*
 * 【機能】整数(モード番号)のバイト順を入れ替えて、文字列にする(モードのスタックの名前の照合用)。
 * 【C++】なし(C++はモードを列挙型で持つ)
 */
u_char *mtoa(int f_M)/*usilic*/
{ /* m.iの順序を入れ替える */
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

/*
 * 【機能】バッファを、制御文字を反転表示にしながら描く(入力欄など)。
 * 【C++】ContentView::renderLine (src/cpp/src/content_view.cpp)
 */
void print_buff(u_char buff[], int kanji_s)/*usilic*/
{ /* 制御文字を反転属性で表示する */
  int c, i, len, kanji_e;
  int f_utf8 = vt_charset(0) == C_utf8;

  len = strlen(buff);
  if ((kanji_e = count_k(&buff[kanji_s], len - kanji_s))) len--;
  if (kanji_s) {
    i = 1;
    vt_putchar(' ');
  } else i = 0;
  for (; i < len; i++)
    /* K_atは0x80である――EUC/ASCIIのコンテンツには決して現れない
       バイト値であり、「buff[i]をNULであるかのように反転表示で
       示す」ことを意味する番兵として安全に使い回されている(下の
       K_dl、そしてdisp.cのput_str()にある同一の経緯を持つK_a0
       参照、同じ理由で修正された)。しかし0x80は、断トツで最も
       よくあるUTF-8の継続バイトである(例えば全角スペース
       "\xe3\x80\x80"の2バイト目と3バイト目): UTF-8モードでは
       これが本物の文字バイトに一致してしまい、それらを静かに
       反転表示の"@"へすり替え、画面上でその文字を引き裂いていた
       (その1バイト目は生のまま描画され、次の2バイトが置き換え
       られ、その後に続くものはまた生のまま描画される)――`buff'
       自身はその間ずっと正しいバイトを保持し続けていたため、
       これは突き止めるのが難しかった。 */
    if ((c = buff[i]) && (c < ' ' || c == K_dl || (!f_utf8 && c == K_at))) {
      if (c == K_dl) c -= 0x80;
      else if (c == K_at) c = 0;
      vt_stand();
      vt_putchar(c + '@');
      vt_endstand();
    } else vt_putchar(buff[i]);
  if (kanji_e) vt_putchar(' ');
}

/*
 * 【機能】文字列の制御文字を、接頭文字ccと`X@`の形(`^A`など)に直す。
 * 【C++】ContentView::renderLine (src/cpp/src/content_view.cpp)
 */
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

/*
 * 【機能】作業ファイル(w、mess、enter、intr、pid、error等)を削除する(終了処理)。
 * 【C++】main (src/cpp/app/main.cpp)
 */
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

/*
 * 【機能】プロセスIDを、pidファイル(S_pid)へ書く。
 * 【C++】main (src/cpp/app/main.cpp)
 * 【備考】C++は~/.usilic/pidに書き、引数なしの多重起動の乗っ取りに使う。
 */
void print_pid(int pid)/*usilic*/
{
  FILE *fp = fopen(s_S(S_pid), "w");
  fprintf(fp, "%d\n", pid);
  fclose(fp);
}

/*
 * 【機能】整数の表(tab、0で終わり)の中にuがあれば、その値を返す(無ければ0)。
 * 【C++】なし(C++は標準ライブラリで足りる)
 */
int strint(int tab[], int u)/*usilic*/
{
  int i;

  for (i = 0; tab[i]; i++) if (tab[i] == u) break;
  return tab[i];
}

/*
 * 【機能】モード(f_M)が、CP/MV/RM/ARC等の、複数選択して実行するモード(f_comm=0)か、その入力・確認の段階
 *       (f_comm=1)かを判定して、対応するモード番号を返す。
 * 【C++】Application::startCopyPrompt; Application::startMovePrompt; Application::startRemovePrompt; Application::startArchivePrompt (src/cpp/src/application.cpp)
 */
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

/*
 * 【機能】設定に、外部プログラム(s_i)が登録されているかを確認し、なければ`Not installed`のエラーを出してNULLを
 *       返す。
 * 【C++】Configuration::commandFor (src/cpp/src/configuration.cpp)
 */
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

/*
 * 【機能】保存しておいた内容(save_stderr)を、標準エラーへ出す(端末を戻した後)。err0を渡すと、内容を設定する。
 * 【C++】main (src/cpp/app/main.cpp)
 */
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
 
/*
 * 【機能】モード(f_M)が、ls形式の一覧(DIR、DU、FILE/DIRの選択、履歴等)かどうかを返す。
 * 【C++】DirectoryEntry (src/cpp/src/directory_mode.hpp)
 */
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

/*
 * 【機能】HELPファイルの名前(文字コードに応じて、英語版・日本語版)を選んで覚える。cpがF13なら解放する。
 * 【C++】Application::startHelp (src/cpp/src/application.cpp); expandHelpMarkup (src/cpp/src/help_text.cpp)
 */
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

