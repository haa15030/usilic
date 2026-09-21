#include "usilic.h"
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>

/*
 * 【機能】usilicrcの1行(`suffix 'プログラム' 引数... [| 後段のプログラム]`)を解析して、外部プログラムの
 *       定義(CHILD)を連結リストへ追加する。ARCHIVE/EXEC/EDITOR/SHELL/PROGRAMの各設定で共用し
 *       (f_childで区別)、`'...'`・`"..."`の引用と`#`のコメントを解釈する。パスにプログラムが無ければ、
 *       エラーを記録して捨てる。chpがF10のときは、エラー一覧の解放だけを行う。
 * 【C++】Configuration::Configuration; Configuration::load (src/cpp/src/configuration.cpp)
 * 【備考】C++は、設定の種類ごとにConfigurationのメンバー(表)へ読み込む。CHILDの連結リストに
 *       相当するものは、ProgramCommand/SuffixEntryの配列になっている。
 */
CHILD *save_child(CHILD *chp, char *cp0, int f_child, LIST **lpp)/*usilic*/
{
  static LIST *lp;

  if (chp == F10) {
    lp = free_list(lp);
    return NULL;
  }
  if (!chp) {
    int i, j, k, n_str;
    char *p, *p2, *p3, *cp;
    PN pn;
    CHILD *chp2 = NULL;
    char *argv[8] = {NULL,};

    for (i = 0; cp0[i]; i++) if (cp0[i] == '\t') cp0[i] = ' ';
    cp = alloca(strlen(cp0) + 1);
    strcpy(cp, cp0);
    if (!(p2 = strchr(cp, ' '))) p2 = cp;
    if ((p = strchr(p2, '|'))) {
      *p++ = '\0';
      i = p - cp;
      p = cp;
      chp2 = save_child(NULL, p, 2, (!lpp) ? &lp : lpp);
      p = strchr(p, ' ');
      strcpy(p, &cp[i]);
    }
    chp = calloc(sizeof(CHILD), 1);
    for (n_str = 0; n_str >= 0; n_str++) {
      /* argv[0] ==> chp->suffix、argv[1..] ==> chp->argv[0..] */
      p3 = NULL;
      if (f_child == 4 && (!n_str || n_str == 2)) { /* EDITOR(usilicrc) */
	if (!n_str)
	  p3 = strdup(s_P(P_mode)[M_edit]); /* エディタの目印 */
	else if (n_str == 2) /* エディタ用の行番号 */
	  p3 = strdup("+??????????");
	i = strlen(p3);
      } else if (f_child == 5 && n_str != 1) { /* SHELL(usilicrc) */
	if (!n_str)
	  p3 = strdup(s_P(P_mode)[M_shell]); /* シェルの目印 */
	else if (n_str == 2) p3 = strdup("-i");
	i = (n_str == 3) ? 0 : strlen(p3);
      } else {
	while (*cp == ' ') cp++;
	if (!n_str && !f_child) {
	  argv[0] = strdup("...");
	  continue;
	}
	if (*cp == '#') i = 0; /* コメント行 */
        else { /* ''または""で囲まれた文字列 */
	  if ((i = strlen(cp))) {
	    p3 = malloc(i);
	    if (*cp == '\047') {
	      for (i = 0, cp++; *cp != '\047'; cp++, i++)
		if (!(p3[i] = *cp)) break;
	      cp++;
	    } else if (*cp == '\042') {
	      for (i = 0, cp++; *cp != '\042'; cp++, i++)
		if (!(p3[i] = *cp)) break;
	      cp++;
	    } else
	      for (i = 0; *cp != ' '; cp++, i++)
		if (!(p3[i] = *cp)) break;
	  }
	}
      }
      if (!i) {	/* もう文字列が無い */
	k = !strcmp(argv[0], s_P(P_mode)[M_shell]);
	chp->f.argc = n_str - 1; /* chp->argv[chp->f.argc] <== filename */
	chp->argv = calloc(sizeof(char*), chp->f.argc + 2 - k);
	if (is_fm() && !strcmp(s_S(S_du), argv[1]))
	  ; /* filenameは不要 */
	else if (chp2) chp->argv[chp->f.argc + 1] = (char*)chp2;
	else if (!k) /* 引数用の領域 */
	  chp->argv[chp->f.argc] = calloc(1, N_line);
	for (j = 0; j < chp->f.argc+1; j++) {
	  if (j) chp->argv[j-1] = strdup(argv[j]);
	  else chp->suffix = strdup(argv[j]);
	}
	if (!is_inst(s_S(S_path), *chp->argv)) {
	  /* chp->argv[0]がインストールされていない */
	  if (!lpp) lpp = &lp;
	  if (!load_list(*lpp, chp->argv[0], &pn)) {
	    *lpp = save_list(*lpp, chp->argv[0], &pn);
	    save_error("%src: %s: %s.\n", s_S(S_usilic), *chp->argv, s_S(S_errc));
	  }
	  for (i = 0; i < chp->f.argc + 1; i++) 
	    if (chp->argv[i]) free(chp->argv[i]);
	  free(chp->argv);
	  if (chp->suffix) free(chp->suffix);
	  free(chp);
	  chp = NULL;
	} else
	  switch(f_child) {
	  case 1:
	    chp->f.chp = 1;
	    break;
	  case 2:
	    chp->f.chp = is_chp(NULL, chp->argv[0]);
	    break;
	  case 3:
	    chp->f.exec = 1; /* EXEC(usilicrc) */
	    break;
	  }
	for (i = 0; ; i++) {
	  char **app = &argv[i];
	  if (*app) {
	    free(*app);
	    *app = NULL;
	  } else break;
	}
	break;
      }	else {
	p3[i] = 0;
	argv[n_str] = strdup(p3);
	free(p3);
      }
    }  
  } else chp->next = save_child(chp->next, cp0, f_child, (!lpp) ? &lp : lpp);
  return chp;
}

/*
 * 【機能】ファイル名の末尾(suffix)から、対応する外部プログラム(CHILD)を探す(最長一致)。見つからなければ、
 *       ファイルの先頭N_man(0x1000)バイトを調べて、manページ形式ならgroffのCHILDを返す。
 * 【C++】Configuration::programForSuffix (src/cpp/src/configuration.cpp); FileMode::loadAndTransformContent (src/cpp/src/file_mode.cpp)
 * 【備考】suffixの照合はConfiguration、manページ形式(.TH等)の検出とgroffの適用はFileModeに分かれている。
 */
CHILD *check_ext(char *name, u_char *split, int size)/*usilic*/
{ /* プログラム名を設定するためsuffixを調べる */
  int i, len, size0;
  CHILD *chp, *chp0 = NULL;
  char *cp;
  struct stat st;
  FILE *fp;
  
  if (name && *name == ' ') return NULL;
  len = 0;
  if (name)
    for (chp = get_child(NULL, NULL, s_S(S_gzip), NULL, NULL); chp
	   ; chp = chp->next) {
      if (!chp->argv[0]) continue;
      cp = strchr(name, '\0');
      if (strlen(name) > (i = strlen(chp->suffix))
	  && !strcasecmp(&cp[-i], chp->suffix))
	if (i >= len) {
	  chp0 = chp;
	  len = i;
	}
    }
  if (!(chp = chp0)) {
  /* マニュアルページの形式を調べるため`N_man'バイト読む */
    size0 = size;
    if (split) { /* メモリを読む */
      st.st_size = -1;
      if (size > N_man) size = N_man;
    } else /* ファイルを読む */
      if (stat(name, &st) < 0 || !st.st_size) split = NULL;
      else {
	size0 = size = N_man;
	split = calloc(1, size
			   +3); /* man[3](is_man())のため */
	if ((fp = fopen(name, "r"))) {
	  size = fread(split, 1, size + 3, fp);
	  fclose(fp);
	} else size = 0;
      }
    if (split) {
      for (i = 0, cp = split; i < size; ) {
	if (is_man(cp)) break;
	while (i++ < size && *cp++ != '\n') ;
      }
      if (i < size) {
 	cp = (st.st_size < 0) ? NULL : name;
	chp = get_child(NULL, NULL, s_S(S_groff), cp, NULL);
      }
      if (st.st_size >= 0 && split) free(split);
    }
  }
  return chp;
}

/*
 * 【機能】組み込みのプログラム定義(s_P(P_child))を読み込んで、外部プログラムのリスト(CHILD)を初期化する。
 *       ファイルマネジャーかどうかで、読み始める位置が違う。
 * 【C++】Configuration::Configuration (src/cpp/src/configuration.cpp)
 * 【備考】C++は、組み込みの表をConfigurationのコンストラクタで作る。
 */
CHILD *init_child(void)/*usilic*/
{ /* 組み込みプログラム */
  int i;
  char **app;
  CHILD *chp;
  
  app = s_P(P_child);
  i = (is_fm()) ? 0 : S_egrep - S_ls;
  for (chp = NULL; app[i]; i++) chp = save_child(chp, app[i], 0, NULL);
  return chp;
}

/*
 * 【機能】外部プログラムのリスト(CHILD)を、suffix・プログラム名(bin)・次の要素のいずれかで検索する。
 *       name指定があれば、見つかった要素の引数の末尾へファイル名を設定する。全引数がNULLのときは、
 *       先頭を登録/取得する。
 * 【C++】Configuration::programForSuffix; Configuration::commandFor (src/cpp/src/configuration.cpp)
 * 【備考】旧はグローバルなリストを検索する。C++はConfigurationのメソッドで、役割(ConfiguredRole)や
 *       suffixから引く。
 */
CHILD *get_child(CHILD *chp0, char *suffix, char *bin, char *name, CHILD *next)/*usilic*/
{
  static CHILD *child;
  CHILD *chp;

  if (!suffix && !bin && !name && !next) {
    if (chp0) child = chp0;
    return child;
  }
  for (chp = (chp0) ? chp0 : child; chp; chp = chp->next) {
    if (next) {
      if (chp->next == next) break;
    } else if (suffix && ((!strcmp(suffix, s_P(P_mode)[M_edit]) 
		    || !strcmp(suffix, s_P(P_mode)[M_shell]))
		   && !strcmp(chp->suffix, suffix))) break;
    else if (!bin || (bin && !strcmp(*chp->argv, bin)))
      if (!suffix || !strcmp(chp->suffix, suffix)) break;
  }
  if (chp && name) /* 引数を設定する */
    strcpy(chp->argv[chp->f.argc], name);
  return chp;
}

/*
 * 【機能】FILE一覧の表示用に、パス名の末尾へ` (プログラム名)`を付ける。ディレクトリや空白始まりの
 *       行には付けない。
 * 【C++】なし(C++のFILE履歴は実パスだけを持ち、プログラムはConfiguration::programForSuffixで都度引く)
 */
int cat_child(CHILD *chp, char pathname[])/*usilic*/
{  /* FILE-listへ`filename (program)'を設定する */
  int i;

  if (!chp) return 0;
  i = strlen(pathname);
  if (pathname[0] == ' ' || pathname[i-1] == '/') return 0;
  strcat(pathname, " (");
  strcat(pathname, *chp->argv);
  strcat(pathname, ")");
  return i;
}

/*
 * 【機能】外部プログラム1件(CHILD)と、その引数・suffix・パイプ後段を解放する。
 * 【C++】なし(C++はデストラクタとstd::vectorの自動解放で足りる)
 */
void free_chp(CHILD *chp)/*usilic*/
{
  int argc, f_pipe;

  f_pipe = !chp->argv[chp->f.argc] 
    && strcmp(chp->suffix, "...")
    && *chp->suffix != ' '; /* s_P(P_mode)[M_shell]のこと */
  for (argc = 0; chp->argv[argc]; argc++)
    free(chp->argv[argc]);
  if (f_pipe) free_chp((CHILD*)chp->argv[argc + 1]);
  if (chp->suffix) free(chp->suffix);
  free(chp->argv);
  free(chp);
}
  
/*
 * 【機能】外部プログラムのリスト(CHILD)全体を解放する。
 * 【C++】なし(C++はデストラクタとstd::vectorの自動解放で足りる)
 */
void free_child(CHILD *chp0)/*usilic*/
{
  CHILD *chp, *chb;

  for (chb = chp = chp0; chp; chp = chb) {
    chb = chp->next;
    free_chp(chp);
  }  
}

/*
 * 【機能】ファイルを開くための外部プログラム(CHILD)から、そのファイルを表示するモード
 *       (T_tar/T_lha/T_unzip/T_rpm/T_deb/T_gz/T_roff/T_exec/T_bin)を決めて返す。ファイル名を
 *       プログラムの引数へ設定する。
 * 【C++】ArchiveKind (src/cpp/src/configuration.hpp); Application::openSelectedEntry (src/cpp/src/application.cpp)
 * 【備考】C++では、種別(ArchiveKind)を返し、どのモードで開くかはApplicationが決める。
 */
int is_child(char name[], CHILD *chp)/*usilic*/
{
  int i, f_M;
  static struct {
    int S, M;
  } p[] = {
    {S_groff,	T_roff},
    {S_tar,	T_tar},
    {S_lha,	T_lha},
    {S_unzip,	T_unzip},
    {S_rpm,	T_rpm},
    {S_deb,	T_deb},
    {S_gzip,	T_gz},
    {-1,	T_bin},
  };
  
  if (strcmp(chp->argv[chp->f.argc - 1], "-"))
    strcpy(chp->argv[chp->f.argc], name);
  if (chp->f.exec) f_M = T_exec;
  else
    for (i = f_M = 0; !f_M; i++) {
      if (!strcmp(*chp->argv, s_S(p[i].S))) f_M = p[i].M;
      else if (p[i].S < 0) f_M = p[i].M;
    }
  return f_M;
}

/*
 * 【機能】FILE一覧の`ファイル名 (プログラム名)`という行から、括弧内のプログラム名を取り出して、外部プログラム
 *       (CHILD)を引き当てる。
 * 【C++】なし(cat_childと対。C++のFILE履歴はプログラム名を持たない)
 */
CHILD *load_chp(char arg[])/*usilic*/
{
  CHILD *chp;
  char *cp;

  if (*arg == K_z) chp = NULL;
  else if ((cp = strchr(arg, '('))) {
    char *p = alloca(strlen(&cp[1]) + 1);
    strcpy(p, &cp[1]);
    *strchr(p, ')') = '\0';
    chp = get_child(NULL, NULL, p, NULL, NULL);
  } else chp = NULL;
  return chp;
}

/*
 * 【機能】外部プログラムが「展開して一覧を出す」種類か判定し、種別の番号を返す(deb=9、rpm=8、tar=5、
 *       lha=4、unzip=3、bzip2=2、パイプ後段付きは6/7)。argを渡した場合は、その名前が後段に
 *       使われているかを調べる。
 * 【C++】ArchiveKind (src/cpp/src/configuration.hpp)
 * 【備考】C++はArchiveKind(Tar/Lha/Zip/Bzip2/Rpm/Deb/Rar/SevenZip)という列挙型に置き換わっている。
 */
int is_chp(CHILD *chp, char *arg)/*usilic*/
{
  int i;
  static struct {
    int S, c;
  } p[] = {
    {S_deb,	9},
    {S_rpm,	8},
    {S_tar,	5},
    {S_lha,	4},
    {S_unzip,	3},
    {S_bzip2,	2},
    {-1,	0},
  };
  if (arg) {
    CHILD *chp1, *chp2;
    chp1 = get_child(NULL, NULL, NULL, NULL, NULL);
    while (chp1) {
      if (chp1->f.chp && (chp2 = (CHILD*)chp1->argv[chp1->f.argc + 1]))
	if (!strcmp(chp2->argv[0], arg)) return 1;
      chp1 = chp1->next;
    }
    return 0;
  } else {
    if (!chp->f.chp) return 0;
    if (!chp->argv[chp->f.argc])
      return (!strcmp(((CHILD*)chp->argv[chp->f.argc + 1])->argv[0]
		      , s_S(S_bzip2))) ? 7 : 6;
    for (i = 0; p[i].S >= 0; i++)
      if (!strcmp(chp->argv[0], s_S(p[i].S))) return p[i].c;
    return chp->f.chp;
  }
}  

