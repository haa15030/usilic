#include "usilic.h"
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>

static struct {
  u_char *S[S_nn];
} Str;

/*
 * 【機能】文字列の表(Str.S)の、番号n_Sの1件を解放する。
 * 【C++】なし(C++はstd::stringの自動解放で足りる)
 */
void r_S(int n_S)/*usilic*/
{
  if (n_S < S_nn && Str.S[n_S]) {
    free((char*)Str.S[n_S]);
    Str.S[n_S] = 0;
  }
}

/*
 * 【機能】文字列の表(Str.S)から、番号n_Sの文字列(メッセージ、パス、コマンド等)を返す。
 * 【C++】Configuration::commandFor (src/cpp/src/configuration.cpp)
 * 【備考】C++は、文字列を番号で引かず、ConfiguredRoleで役割ごとのProgramCommandを引く。
 */
char *s_S(int n_S)/*usilic*/
{
  return (n_S < S_nn) ? (char*)Str.S[n_S] : NULL;
}

/*
 * 【機能】組み込みの文字列の配列(外部プログラムの定義child[]、モード名、ラベル等)を、番号で返す。
 * 【C++】Configuration::Configuration (src/cpp/src/configuration.cpp)
 * 【備考】C++は、組み込みのプログラム定義をConfigurationのコンストラクタで作る。
 */
char **s_P(uintptr_t n_P)/*usilic*/
{
  static char *child[] = { /* 組み込みプログラム */
    /* ファイルマネージャ */
    NULL,		/* S_ls */
    NULL,		/* S_du */
    NULL,		/* S_cp */
    NULL,		/* S_mv */
    NULL,		/* S_rm */
    NULL,		/* S_groff */
    NULL,		/* S_man */
    "locate",		/* S_locate */	

    "file",		/* S_file */
    "egrep -n",		/* S_egreap */
    "fgrep -ne",	/* S_fgrep */
    "gzip -dc",		/* S_gzip */
    "bzip2 -d",		/* S_bzip2 */
    "tar",		/* S_tar */
    "lha p",		/* S_lha */
    "unzip -p",		/* S_unzip */
    "rpm",		/* S_rpm */
    "rpm2cpio",		/* S_rpm2cpio */
    "ar p",		/* S_deb */
    NULL,
  };
  static char *usilicrc[] = { /* usilicrcで定義される設定名 */
    "ARCHIVE", 
    "FILE", 
    "EXEC", 
    "EDITOR",
    "SHELL",
    "GREP",
    "PROGRAM",
    "BUFFER",
    "CODE", 
    "HISTORY", 
    "WAIT",
    "SCROLL",
    "LS",
    "KEY",
    "CP",
    "MV",
    "RM",
    NULL,
  };
  static char *code[] = {				
    "ASCII",						/* C_ascii */
    "BINARY",						/* C_binary */
    "Shift-JIS",					/* C_sjis */
    "EUC",						/* C_euc */
    "JIS",						/* C_jis */
    "\075?ISO-2022-JP?B?",				/* C_base64 */
    "\075?ISO-2022-JP?Q?",				/* C_quote */
    "\075?SHIFT_JIS?B?",				/* C_shift_jis */
    "\075?US-ASCII?Q?",					/* C_us_ascii */
    "\075?EUC-JP?B?",					/* C_euc_jp */
    NULL,
  };
  static char **P[] = {
    child,     						/* P_child */
    NULL,  						/* P_mode */
    usilicrc,     						/* P_usilicrc */
    code,						/* P_code */
    NULL,						/* P_arc */
  };
  static struct {
    char *s;
    int m;
  } mp [] = {
    {" " PROGNAME_UPPER,	M_usilic},
    {" HELP",	M_help},
    {" KEY",	M_key},
    {" FGREP",	M_fgrep},
    {" EGREP",	M_egrep},
    {" CUT",	M_cut},
    {" DU",	M_du},
    {" LOCATE",	M_locate},
    {" PROGRAM",	M_prog},
    {" MAN",	M_man},
    {" FILE",	M_lfile},
    {" DIR",	M_ldir},
    {" HISTORY",	M_hist},
    {" COMMAND",	M_cmd},
    {" CP",	M_cp},
    {" MV",	M_mv},
    {" RM",	M_rm},
    {" ARC",	M_arc},
    {" SHELL",	M_shell},
    {" LIST",	M_list},
    {" EDIT",	M_edit},
    {" ERROR",	M_error},
    {" LOG",	M_log},
    {" COPY",	M_copy},
    {" EXIT",	M_exit},
    {" ",	T_pipe},
    {" (stdin)",	T_pipe2},

    /* ~/.usilic/historyのため */
    {" FILE_HISTORY",	T_hfile},
    {" DIR_HISTORY",	T_hdir},
    {" MAN_HISTORY",	T_hman},
    {" LOCATE_HISTORY",	T_hlocate},
    {" FGREP_HISTORY",	T_hfgrep},
    {" EGREP_HISTORY",	T_hegrep},
    {" MODE", T_mode},
    {NULL,	T_null},
  };
  static char *mode[T_null];
  int i, m;
  
  if (n_P > P_nn && !P[P_arc]) P[P_arc] = (char**)n_P;
  if (!*child) for (i = 0; i < 7; i++) child[i] = Str.S[S_option + i];
  if (!P[P_mode]) {
    for (i = 0; (m = mp[i].m) != T_null; i++) mode[m] = mp[i].s;
    P[P_mode] = mode;
  }
  return (n_P < P_nn) ? P[n_P] : NULL;
}

/*
 * 【機能】コマンド行の文字列から、最初の単語(空白まで)を取り出す。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static void get_prog(char str[])
{ 
  int i;
  char *p;

  p = alloca(strlen(str) + 1);
  for (i = 0; str[i] && str[i] != ' '; i++) p[i] = str[i];
  p[i] = '\0';
  strcpy(str, p);
}

/*
 * 【機能】INFO(バージョンと設定の要約)の画面を表示する。C++版のINFO画面のフィールド構成に合わせてある。
 * 【C++】Application::executeShowUsilicBanner (src/cpp/src/application.cpp)
 */
static void print_usilic(char *str, int f_usilic)
{
  /* 新(C++移植)のINFO画面(application.cpp、field()、kLabelWidth=12)
     と同じフィールド構成に合わせてある。旧はここに実名とメール
     アドレスを表示していたが、実名は伏せたい・メールは無効という
     直接の依頼を受け、その"Version ..."見出し(version.h)自体を
     表示対象から外した。"History file"/"Startup mode"/"User name"
     も新には無いフィールドのため削除した。 */
  char *p = str;

  *p = '\0';
  sprintf(strchr(p, '\0'), "Boot options : %s\n", s_S(S_option+7));
  sprintf(strchr(p, '\0'), "Startup file : %s\n", toul(s_S(S_usilicrc), 0));
  if (f_usilic & (0x80 | 0x40)) {
    sprintf(strchr(p, '\0'), "       ERROR : %s\n", toul(s_S(S_error), 0));
    sprintf(strchr(p, '\0'), "       SHELL : %s\n", toul(s_S(S_log), 0));
    sprintf(strchr(p, '\0'), "        WORK : %s\n", toul(s_S(S_w), 0));
  }
}

/*
 * 【機能】設定の行から、`#`と改行以降を取り除き、先頭の空白を飛ばした位置を返す(空ならNULL)。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static char *get_cp(char str[])
{
  char *cp;

  if ((cp = strchr(str, '#'))
      || (cp = strchr(str, '\n'))) *cp = '\0';
  for (cp = str; *cp && (*cp == ' ' || *cp == '\t'); cp++) ;
  return (*cp) ? cp : NULL;
}

/*
 * 【機能】設定の値の行を、コメント・空行を飛ばして読む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static char *get_val(FILE *fp, char str[], int *ip)
{
  int i = 0;
  char *p, *cp;

  do {
    if (!fgets(str, N_line, fp) || (i = strchr(s_S(S_nop), *str) == NULL)) {
      *ip = (i) ? 1 : -1;
      return NULL;
    }
  } while (!(cp = get_cp(str)) || *cp < '0' || *cp > '9');
  if ((p = strchr(cp, ' ')) || (p = strchr(cp, '\t'))) *p = '\0';
  *ip = 0;
  return cp;
}

/*
 * 【機能】usilicrcのARCHIVEの設定(`.tar tar tvf`のような、suffixと展開コマンド)を読み込む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_arc(FILE *fp, char *str, CHILD *chp)
{
  char *cp, *p, *p2, **cpp;

  if (fp) {
    while (fgets(str, N_line, fp)) {
      if (!strchr(s_S(S_nop), *str)) return 2;
      if ((cp = get_cp(str))) {
	if (!(p = strchr(cp, ' '))) p = strchr(cp, '\t');
	if (!p) return 1;
	*p = '\0';
	p2 = cp;
	cp = &p[1];
	for (cpp = s_P(P_arc); *cpp; cpp++) {
	  p = *cpp;
	  if (!strncmp(p, cp, strlen(cp))) {
	    p = strchr(p, ' ');
	    p = ((cp = strchr(p, ' '))) ? &cp[1] : &p[1];
	    cp = malloc(strlen(p2) + 1 + strlen(p) + 1);
	    sprintf(cp, "%s %s", p2, p);
	    save_child(chp, cp, 1, NULL);
	    free(cp);
	  }
	}
      }
    }
  } else
    for (cpp = s_P(P_arc); *cpp; cpp++) {
      strcpy(str, *cpp);
      if ((cp = get_cp(str))) {
	if (!(p = strchr(cp, ' '))) p = strchr(cp, '\t');
	if (!p) p = cp;
	if ((p = strchr(p, '|')) && strchr(&p[1], '|')) {
	  memmove(str, cp, strlen(cp) + 1);
	  save_error("conf_arc(): \042%s\042: %s.\n", str, s_S(S_errm));
	  save_stderr("conf_arc(): \042%s\042: %s.\n", str, s_S(S_errm));
	} else save_child(chp, cp, 1, NULL);
      }
    }
  return -1;
}

/*
 * 【機能】外部プログラムの設定(EXEC、EDITOR、SHELL、PROGRAM等。種類はf_child)を読み込む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_bin(FILE *fp, char str[], int f_child, CHILD *chp, char sh[])
{
  CHILD *chp2, *chp3;
  char *cp, *p;
  u_char **spp;
  int f_set = 1;

  while (fgets(str, N_line, fp)) {
    if (!strchr(s_S(S_nop), *str)) return 1 + f_set;
    if ((cp = get_cp(str))) {
      if (!(p = strchr(cp, ' '))) p = strchr(cp, '\t');
      if (!p) p = cp;
      if ((p = strchr(p, '|')))
	if (strchr(&p[1], '|')) {
	  memmove(str, cp, strlen(cp) + 1);
	  return 1;
	}
      if (f_child < 14) save_child(chp, cp, f_child + 1, NULL);
      else if (is_fm()){
	spp = &Str.S[S_option + f_child - 12];
	if ((p = strchr(*spp, ' '))) *p = '\0';
	chp2 = get_child(NULL, NULL, *spp, NULL, NULL);
	chp3 = save_child(NULL, cp, 0, NULL);
	chp3->next = chp2->next;
	(get_child(NULL, NULL, NULL, NULL, chp2))->next = chp3;
	free_chp(chp2);
	free(*spp);
	*spp = strdup(cp);
      }
      if (f_child == 4) strcpy(sh, cp);
      f_set = 2;
    }
  }
  return -1;
}

/*
 * 【機能】usilicrcのGREPの設定(検索文字列の候補)を読み込む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_grep(FILE *fp, char str[], LIST **lgpp, STR **sgpp)
{
  int size, code;
  char *cp;
  PN pn;
  JIS jis;

  jis.c = 0;
  while (fgets(str, N_line, fp)) {
    char *p;
    if (!strchr(s_S(S_nop), *str)) return 1;
    if ((cp = get_cp(str))) {
      p = cp;
      if (*cp == '\042' || *cp == '\047') {
	p++;
	cp++;
	while (*cp != '\042' && *cp != '\047') cp++;
      } else while (*cp != ' ' && *cp != '\t') cp++;
      *cp = '\0';
      memmove(str, p, strlen(p) + 1); /* pはstrと重なり得る; ここでstrcpy()は
					  未定義動作であり、macOSのfortified
					  libcの下ではabortする */
      size = strlen(str);
      if ((code = init_code(&size, str, 0)) == C_sjis) toeuc(str);
      else if (code == C_jis) {
	size = jistoeuc(size, str, &jis);
	str[size] = '\0';
      }
      *lgpp = save_list(*lgpp, str, &pn);
      if (sgpp) *sgpp = save_str(*sgpp, str, &pn);
    }
  }
  return -1;
}

/*
 * 【機能】usilicrcのPROGRAMの設定(外部プログラムの候補)を読み込む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_prog(FILE *fp, char str[], LIST **lcpp, STR **scpp)
{
  int i;
  char *cp;
  static PN pn;

  for (i = 1; fgets(str, N_line, fp) != 0; ) {
    if (!strchr(s_S(S_nop), *str)) return 1;
    if ((cp = get_cp(str))) {
      *lcpp = save_list(*lcpp, cp, &pn);
      *scpp = save_str(*scpp, cp, &pn);
      i++;
    }
  }
  return -1;
}

/*
 * 【機能】usilicrcのBUFFERの設定(読み込みバッファの大きさ、1〜100)を読み込む。範囲外ならエラー。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_bsize(FILE *fp, char str[], int *bsizep)
{
  char *cp;
  int i;

  if ((cp = get_val(fp, str, &i)) == NULL) return i;
  if ((i = atoi(cp)) > 100 || !i) {
    save_error("%src: BUFFER: %s.\n", s_S(S_usilic), s_S(S_errm));
    i = 1;
  }
  *bsizep = i * 1024 * 1024;
  return 0;
}

/*
 * 【機能】usilicrcのCODEの設定(バイナリ判定で許可する制御文字)を読み込む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_code(FILE *fp, char str[])
{
  char *cp, *sp, val[3];
  int code, i = 0;

  do {
    if (!fgets(str, N_line, fp) || (i = strchr(s_S(S_nop), *str) == NULL))
      return (i) ? 1 : -1;
    if ((cp = strchr(str, '\n'))) *cp = '\0';
    for (cp = str; *cp && *cp != '#' && (*cp < '0' || *cp > '9'); cp++) ;
  } while (*cp < '0' || *cp > '9');
  for (cp = str, sp = s_S(S_code); ; ) {
    while (*cp && (*cp < '0' || *cp > '9')) cp++;
    if (!*cp) break;
    for (i = 0; i < 2 && *cp >= '0' && *cp <= '9'; i++, cp++) val[i] = *cp;
    val[i] = '\0';
    if ((code = atoi(val)) < 32) { 
      if (!code && sp == s_S(S_code)) break;
      else *sp++ = code;
    }
  }
  *sp = '\0';
  return 0;
}

/*
 * 【機能】usilicrcのHISTORYの設定(履歴のファイル)を読み込む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_history(FILE *fp, char str[])
{
  char *cp;
  int i;

  if ((cp = get_val(fp, str, &i)) == NULL) return i;
  save_history(F6, cp, 0);
  return 0;
}


/*
 * 【機能】usilicrcのWAITの設定(表示の待ち時間、既定100000マイクロ秒)を読み込む/返す。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
int conf_wait(FILE *fp, char str[])/*usilic*/
{
  static int k_wait = 100000;
  char *cp;
  int i;

  if (fp) {
    if ((cp = get_val(fp, str, &i)) == NULL) return i;
    k_wait = atoi(cp) * 10000;
    return 0;
  } else return k_wait;
}

/*
 * 【機能】usilicrcのSCROLLの設定を読み込む/返す。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int conf_scroll(FILE *fp, char *str)
{
  static int f_scroll;
  char *cp;
  int i = 0;

  if (fp) {
    if ((cp = get_val(fp, str, &i)) == NULL) return i;
    f_scroll = atoi(cp);
  }
  if (f_scroll) {
    vt_mode('h');
    vt_cs(vt_row(0) - 1, 0);
  } else {
    vt_mode(-'h');
    vt_cs(vt_row(0) - 2, 0);
  }
  return i;
}

/*
 * 【機能】環境変数(<プログラム名>CHARSET)が、明示的に設定されたかを返す。明示された場合は、前回の
 *       セッションで保存された文字コードより優先する。
 * 【C++】main (src/cpp/app/main.cpp); Configuration::setEnvironmentCharset (src/cpp/src/configuration.cpp)
 */
int env_charset(int f)/*usilic*/
{ /* ユーザーがUSILICCHARSETを明示的にexportしたかどうか(下の
     自動既定値によって設定されただけなのとは対照的に)。明示的な
     exportは、前回のセッションで~/.usilic/historyへたまたま
     保存されていた文字コードが何であれ、常にそれより優先される
     べきである。 */
  static int f_env;

  if (f >= 0) f_env = f;
  return f_env;
}

/*
 * 【機能】usilicrcのLSの設定(lsのオプション)を読み込む/返す。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
int conf_ls(FILE *fp, char *str)/*usilic*/
{
  static int f_ls;
  char *cp;
  int i = 0;

  if (fp) {
    if ((cp = get_val(fp, str, &i)) == NULL) return i;
    f_ls = atoi(cp);
    return i;
  } else if (str) f_ls = atoi(str);
  return f_ls;
}

/*
 * 【機能】usilicrcを1行(最大N_char)読む。`P-?`の行の扱いを含む。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 */
static int get_str(FILE *fp, u_char str[])
{
  u_char *p, *cp;

  if (!fgets(str, N_char, fp)) return -1;
  if (!strncmp(str, "P-?", 3))
    if (is_pg()) return 3;
    else *str = '#';
  else if (!strncmp(&str[1], "-?", 2)) return 2;
  if (!strchr(s_S(S_nop), *str)) return 1;
  if (strchr(str, '\042') >= strchr(str, '#')) cp = str;
  else
    for (cp = str; (p = strchr(cp, '\042')); cp++) {
      if (!(cp = strchr(&p[1], '\042'))) return -1;
      if ((p = strchr(cp, '#'))) {
	*p = '\0';
	break;
      }
    }
  if ((cp = strchr(cp, '#'))) *cp = '\0';
  if ((cp = strchr(str, '\n'))) *cp = '\0';
  for (cp = str; *cp && (*cp == ' ' || *cp == '\t'); cp++) ;
  if (cp > str) memmove(str, cp, strlen((char *)cp) + 1); /* cpはstrの内部を
    指している: strcpy()は重なる範囲では未定義動作であり、ここで実際に
    データを壊すことが観測された(glibc/aarch64)、例えば"K-11"から'-'が
    抜け落ちたり、"previous"が"reviouss"になったりする――以前
    write_shell()/input_shell()で見つかった重なったstrcpy()と同じ
    クラスのバグである。 */
  return 0;
}

/*
 * 【機能】usilicrcのKEYの設定の書式エラーを記録して、終了する。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
static void key_err(FILE *fp, char mark[], int f_err)
{
  fclose(fp);
  save_stderr("%src: %s: %s.\n", s_S(S_usilic), mark, s_S(f_err));
  exit_usilic();
  print_stderr(NULL);
  exit(1);
}

/*
 * 【機能】キーの番号から、表示用の名前(`^A`、`DEL`等)の文字列を作る。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
static char *save_ov(int k)
{
  int c, d;
  u_int u;
  char *cp;
  static char tmp[40];

  u = 0xffffffff;
  cp = tmp;
  *cp = '\0';
  do {
    u >>= 8;
    if (k > u) {
      d = k;
      k &= u;
      for (c = u; c; c >>= 8, d >>= 8) ;
      if (d < ' ') {
	sprintf(cp, "^%c", 'A' + d - 1);
	cp += 2;
      } else if (d == ' ') {
	sprintf(cp, "Space");
	cp += 5;
      } else if (d == K_at) {
	sprintf(cp, "^Space");
	cp += 6;
      } else if (!d) *cp = '\0';
      else {
	sprintf(cp, "%c", d);
	cp++;
      }
    }
  } while (u);
  return tmp;
}

/*
 * 【機能】KEYの設定の中の、数字(番号)を読み取る。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
static char *set_m(char *cp, int *mp)
{
  int i;
  char p[3];

  for (i = 0; *cp >= '0' && *cp <= '9'; i++) p[i] = *cp++;
  p[i] = '\0';
  *mp = atoi(p);
  while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
  return cp;
}

/*
 * 【機能】KEYの設定の中の`"..."`の文字列を読み取り、`\cX`等の記法を制御文字へ変換する。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
static char *set_s(u_char *cp, u_char s[])
{
  int i, len;
  u_char c, *p;

  len = (u_int)strchr(&cp[1], '\042') - (u_int)&cp[1];
  for (i = 0, p = s; i < len; i++)
    if (!strncmp(&cp[1+i], "\\c", 2) && (c = cp[1+i+2]) >= 'A' && c <= 'Z') {
      *p++ = c - 'A' + 1;
      i += 2;
    } else *p++ = cp[1+i];
  *p = '\0';
  cp += len + 2;
  while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
  return cp;
}

/*
 * 【機能】設定全体の初期化。組み込みの既定値(`ls -alFN`、`xvf`等)、ARCHIVEの表、エラーメッセージの表を
 *       文字列の表(Str.S)へ設定し、作業ディレクトリ、HOME、LANG、usilicrcの場所を決める。
 * 【C++】Configuration::Configuration (src/cpp/src/configuration.cpp); main (src/cpp/app/main.cpp)
 */
int conf(char *temp, char *args, char *version, int f_usilic)/*usilic*/
{
#include "config.h"
  static char LS[] = "ls -alFN";
  static char TAR[] = "xvf";
  static char LHA[] = "e";
  static char UNZIP[] = "";
  static char CPIO[] = "idv";
  static char AR[] = "p";
  static char *ARCHIVE[] = {
    ".tar tar tvf",
    ".tgz gzip -dc | tar tvf -",
    ".tar.bz2 bzip2 -dc | tar tvf -",
    ".lzh lha",
    ".zip unzip -l",
    ".rpm rpm -qplv",
    ".deb ar tv",
    NULL,
  };
  static char *err[] = {
    "Interrupted", 					/* S_err0 */
    "No size", 						/* S_err1 */
    "No string",					/* S_err2 */
    "No such file",					/* S_err3 */
    "Not enough buffer",				/* S_err4 */
    "Failed to open /dev/pty..",			/* S_err5 */
    "No manual entry",	 				/* S_err6 */
    "Not a directory", 					/* S_err7 */
    "Segmentation fault",				/* S_err8 */
    "Hit any key",					/* S_err9 */
    "Wrote",						/* S_erra */
    "Macro interrupted",				/* S_errb */
    "Not installed",					/* S_errc */
    "Edit `SHELL' of " PROGNAME "rc",			/* S_errd */
    "~/." PROGNAME "/" PROGNAME "rc ignored",		/* S_erre */
    "SHELL busy",					/* S_errf */
    "Exit",						/* S_errg */
    "Hit Enter",					/* S_errh */
    "SHELL: hangup",					/* S_erri */
    "DU busy",						/* S_errj */
    "Reading ...",					/* S_errk */
    "Failed to allocate memory",			/* S_errl */
    "Invalid format", 					/* S_errm */
    "Can't be set here",				/* S_errn */
    "Out of range",					/* S_erro */
    "Defining keyboard macro...",			/* S_errp */
    "Keyboard macro defined",				/* S_errq */
    "Not defining keyboard macro",			/* S_errr */
    "No builtin macro",					/* S_errs */
    "Invalid login shell",				/* S_errt */
    "Invalid terminal",					/* S_erru */
    "Invalid mode",					/* S_errv */
    "Exited abnormally",				/* S_errw */
    "Clear previous mode",				/* S_errx */
    "(y/n)",						/* S_erry */
    NULL,
  };
  static char *shell[] = {
    "bash",
    "tcsh",
  };
  static char usilic[] = PROGNAME;
  static char jman[] = "man";
  static char *groff[] = {
    "groff",
    "-mandoc",
    "-Tutf8",
  };
  char *com[] = {LS, DU, CP, MV, RM};
  int i, j, pv[2], erre = 0;
  int i_mess, i_home;
  char *cp;
  struct stat st;
  char home[256], tmp[25600], mess[N_line];

  if (temp) {
    strcpy(temp, TMP);
    /*
     * 以下のファイルは"TMP"(config.hで定義)ディレクトリで使われる。
     *   d... `du'の終了通知。
     *   e... ディレクトリ引数付きで起動した後のERRORのログ。
     *   i... エラーメッセージ番号についての情報。
     *   l... ディレクトリ引数付きで起動した後のSHELLのログ。
     *   s... SHELLプロンプトから起動された`usilic'からの出力。
     *   w... ^Jwと^J^Wの内容。
     *   t... メモリの確保に失敗した代わりに使われる作業ファイル。
     */
    return 0;
  }
  if (!(cp = ttyname(1))) {
    fprintf(stderr, "%s: %s.\n", usilic, strerror(errno));
    return -1;
  }
  Str.S[S_tty] = strdup(cp);
  Str.S[S_usilic] = strdup(usilic);	/* S_usilic */
  strcpy(home, getenv("HOME"));
  if (strchr(home, '\0')[-1] != '/') strcat(home, "/");
  Str.S[S_home] = strdup(home);
  strcpy(tmp, LIB);
  if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
  Str.S[S_lib] = strdup(tmp);
  if (!getenv("LANG")) setenv("LANG", "C", 1);
  Str.S[S_LANG] = strdup(getenv("LANG"));
  sprintf(strchr(home, '\0'), ".%s", s_S(S_usilic));
  if (!stat(home, &st)) {
    if (!S_ISDIR(st.st_mode)) {
      fprintf(stderr, "%s: %s: %s.\n", s_S(S_usilic), home, strerror(EEXIST));
      return -1;
    }
    if (access(home, W_OK) < 0) {
      fprintf(stderr, "%s: %s: %s.\n", s_S(S_usilic), home, strerror(errno));
      return -1;
    }
  } else if (mkdir(home, S_IRWXU|S_IRWXG|S_IRWXO) < 0) {
    fprintf(stderr, "%s: %s: %s.\n", usilic, home, strerror(errno));
    return -1;
  }
  sprintf(mess, "%s/s.", TMP);
  strcat(home, "/");
  i_home = strlen(home);
  i_mess = strlen(mess) - 2;
  strcat(mess, "XXXXXX");
  if (mkstemp(mess) < 0) {
    fprintf(stderr, "%s: mktemp: %s.\n", usilic, strerror(errno));
    return -1;
  }
  unlink(mess);
  Str.S[S_mess] = strdup(mess);	/* S_mess */
  mess[i_mess] = 'd';
  Str.S[S_enter] = strdup(mess);	/* S_enter */
  mess[i_mess] = 'i';
  Str.S[S_intr] = strdup(mess);	/* S_intr */
  sprintf(tmp, "%s.%s/w.%s", s_S(S_home), s_S(S_usilic), s_S(S_usilic));
  Str.S[S_write] = strdup(tmp);
  sprintf(tmp, "%s.%s/stderr", s_S(S_home), s_S(S_usilic));
  Str.S[S_stderr] = strdup(tmp);
  cp = set_usilicrc(&erre);
  if (erre < 0) {
    fprintf(stderr, "%src: %s.\n", usilic, strerror(ENOENT));
    free(cp);
    return -1;
  }
  Str.S[S_usilicrc] = cp;
  Str.S[S_code] = calloc(sizeof(char), 32);	/* S_code */
  Str.S[S_version] = strdup(version);
  Str.S[S_path] = strdup(getenv("PATH"));	/* S_path */
  Str.S[S_option+7] = malloc(strlen(s_S(S_usilic)) 
			     + strlen(args) + 1);
  sprintf(Str.S[S_option+7], "%s%s", s_S(S_usilic), args);
  Str.S[S_nop] = strdup("# \t\n");
  env_charset(getenv(ENV_CHARSET) != NULL);
  if (!getenv(ENV_CHARSET)) setenv(ENV_CHARSET, CHARSET, 1);
  if (f_usilic & (0x80|0x40)) {
    if (f_usilic & 0x80) {
      char *p;
      home[i_home] = '\0';
      strcat(home, "history");
      Str.S[S_hist] = strdup(home);
      strcpy(&home[i_home], "pid");
      Str.S[S_pid] = strdup(home);
      strcpy(&home[i_home], "error.old");
      p = alloca(strlen(home) + 1);
      strcpy(p, home);
      strcpy(&home[i_home], "error");
      rename(home, p);
      Str.S[S_error] = strdup(home);
      strcpy(&home[i_home], "shell.old");
      strcpy(p, home);
      strcpy(&home[i_home], "shell");
      Str.S[S_log] = strdup(home);
      rename(home, p);
    } else {
      mess[i_mess] = 'e';
      Str.S[S_error] = strdup(mess);
      unlink(mess);
      mess[i_mess] = 'l';
      Str.S[S_log] = strdup(mess);
      unlink(mess);
    }
    pipe(pv);
    if (!fork()) {
      int pv2[2];
      pipe(pv2); /* 一度も呼ばれていなかった: 下のdup2()/close()は
		    pv2[]にたまたまスタック上にあったゴミの値に対して
		    実行されており、本物の既に開かれているfdをエイリアス
		    してリダイレクト/closeしてしまう可能性があった。 */
      if (!fork()) {
	close(1);
	dup2(pv2[1], 1);
	close(pv2[1]);
	close(pv2[0]);
	write(1, ".\\\n", 3);
	exit(0);
      }
      close(0);
      dup2(pv2[0], 0);
      close(pv2[0]);
      close(pv2[1]);
      close(2);
      dup2(pv[1], 2);
      close(pv[1]);
      close(pv[0]);
      execlp(groff[0], groff[0], groff[2], NULL);
      exit(0);
    }
    close(pv[1]);
    cp = malloc(strlen(groff[0]) + 1
		+ strlen(groff[1]) + 1
		+ strlen(groff[2]) + 1);
    sprintf(cp, "%s %s ", groff[0], groff[1]);
    for (j = 0; read(pv[0], &tmp[j], 1) == 1 && j < 256; j++) ;
    if (!j) strcat(cp, groff[2]);
    else {
      if (j == 256) j--;
      tmp[j] = '\0';
      save_error("%s", tmp);
      strcat(cp, "-R");
    }
    close(pv[0]);
    pipe(pv);
    if (!fork()) {
      int fd;
      /* -Nの有無を試すだけなので、BSDのls(macOS)の「invalid option」は
	 ERRORへ流さず捨てる(2026-10-08) */
      if ((fd = open("/dev/null", O_WRONLY)) >= 0) {
	dup2(fd, 2);
	close(fd);
      }
      close(0);
      close(1);
      dup2(pv[1], 1);
      close(pv[0]);
      close(pv[1]);
      execlp("ls", "ls", "-N", NULL);
      exit(0);
    }
    close(pv[1]);
    for (j = 0; read(pv[0], &tmp[j], 1) == 1 && j < 256; j++) ;
    if (!j) *strchr(LS, 'N') = ' ';
    close(pv[0]);
    for (i = 0; i < 5; i++) Str.S[S_option+i] = strdup(com[i]);
    Str.S[S_mark] = malloc(6);
    sprintf(Str.S[S_mark], "%c%s", '^', "[[?m");
    Str.S[S_option+5] = cp;
    Str.S[S_option+6] = strdup(jman);
    sprintf(tmp, "%s%s", s_S(S_lib), &s_P(P_mode)[M_help][1]);
    Str.S[S_help] = strdup(tmp);
    mess[i_mess] = 'w';
    Str.S[S_w] = strdup(mess);		/* S_w */
    mess[i_mess] = 't';
    Str.S[S_tmp] = strdup(mess);	/* S_tmp */
    for (i = j = 0; i < 2; i++) 
      if (is_inst(s_S(S_path), shell[i]) && !j) j = 1;
    if (!j) {
      fprintf(stderr, "Install %s or %s.\n", shell[0], shell[1]);
      return -1;
    }
    strcpy(tmp, getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
    for (i = 0, cp = NULL; i < 2; i++) 
      if (strstr(tmp, shell[i])) {
	cp = shell[i];
	break;
      }
    if (!cp) {
      if (!strcmp(tmp, "/bin/sh")) {
	pipe(pv);
	if (!fork()) {
	  close(1);
	  dup2(pv[1], 1);
	  close(pv[1]);
	  close(pv[0]);
	  execlp("file", "file", "/bin/sh", NULL);
	}
	close(pv[1]);
	read(pv[0], tmp, 256);
	close(pv[0]);
	tmp[255] = '\0';
      }
      if (strstr(tmp, shell[0])) cp = shell[0];
    }
    if (cp) {
      char *p = alloca(strlen(cp) + 1);
      strcpy(p, cp);
      get_prog(p);
      Str.S[S_sh] = strdup(cp);
      if (!is_inst(s_S(S_path), p)) {
	fprintf(stderr, "%s: %s: %s.\n", usilic, p, err[S_errc - S_err0]);
	return -1; /* s_S(S_sh)がインストールされていない */
      }
    }
    sprintf(tmp, "%s  %s", &s_P(P_mode)[M_shell][1], version);
    *strchr(tmp, '\n') = '\0';
    Str.S[S_lb2] = strdup(tmp);	/* S_lb2 */
    for (i = j = 0; i < S_tty - S_ls; i++, j++) {
      cp = s_P(P_child)[j];
      /* Str.S[S_ls]等を設定する */
      if (!cp || !strlen(cp)) continue;
      else {
	char *p = alloca(strlen(cp) + 1);
	strcpy(p, cp); 
	get_prog(p);
	Str.S[i + S_ls] = strdup(p);
      }
    }
    Str.S[S_arc1] = strdup(TAR);
    Str.S[S_arc2] = strdup(LHA);
    Str.S[S_arc3] = strdup(UNZIP);
    Str.S[S_arc4] = strdup(CPIO);
    Str.S[S_arc5] = strdup(AR);
  } else
    for (i = 0, j = S_file - S_ls; i < S_tty - S_file; i++, j++) {
      /* Str.S[S_file]等を設定する */
      cp = s_P(P_child)[j];
      if (!cp || !strlen(cp)) continue;
      else { 
	char *p = alloca(strlen(cp) + 1);
	strcpy(p, cp);
	get_prog(p);
	Str.S[i + S_file] = strdup(p);
      }
    }
  for (i = 0; err[i]; i++) 
    Str.S[i + S_err0] = strdup(err[i]);
  if (erre) save_intr(S_erre);
  print_usilic(mess, f_usilic);
  Str.S[S_USILIC] = strdup(mess);
  s_P((uintptr_t)ARCHIVE);
  return 0;
}

/*
 * 【機能】文字列の表(Str.S)の全体を解放する。
 * 【C++】なし(C++はstd::stringの自動解放で足りる)
 */
void free_S(void)/*usilic*/
{
  int i;

  for (i = 0; i < S_nn; i++) r_S(i);
}

/*
 * 【機能】KEYモードの一覧(キーとコマンドの対応)を出力する。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 * 【備考】C++のキー一覧は、HELPの文章にまとめられている。
 */
u_char *print_key(int *key_c)/*usilic*/
{
  static struct {
    int key;
    int mode;
  } command[] = {
    {K_a, 0},			/* ^A */
    {K_b, 0},			/* ^B */
    {K_c, 0},			/* ^C */
    {K_d, 0},			/* ^D */
    {K_e, 0},			/* ^E */
    {K_f, 0},			/* ^F */
    {K_g, M_bl},		/* ^G */
    {K_h, 0},			/* ^H */
    {K_i, M_list},		/* ^I */
    				/* ^J */
    {K_k, 0},			/* ^K */
    {K_l, M_clear}, 		/* ^L */
    {K_m, M_cr},		/* ^M */
    {K_n, M_dn},		/* ^N */
				/* ^O */
    {K_p, M_up},		/* ^P */
    {K_q, 0},			/* ^Q */
    {K_r, M_hist},		/* ^R */
    {K_s, M_fgrep},		/* ^S */
				/* ^T */
    {K_u, 0},			/* ^U */
				/* ^V */
    {K_w, 0},			/* ^W */
				/* ^X */
    {K_z, 0},			/* ^Z */
    {0, 0},
  };
  static u_char inval_0[] = {/* ページャ */
    M_help, M_sel, M_fp, M_code, M_cd, M_m, M_bl2,
    M_fcut2, M_fcut, M_du, M_du2, M_locate, M_locate2, M_prog2, M_prog, 
    M_man2, M_man, M_lfile2, 
    M_lfile, M_ldir2, M_ldir, M_cmd, M_cp, M_cp2, M_mv, M_mv2, M_rm, 
    M_rm2, M_arc, M_arc2, M_dest, M_yes, M_no, M_shell, M_hup, 
    M_chdir2, M_chdir, M_w, M_w2, M_list, M_error, M_log, 
    M_ascii, M_kanji, M_exit, M_ls,
    0,
  };
  static u_char inval_2[] = { /* ファイルマネージャ */
    M_quit, 0,
  };
  u_char *p, *cp, **label;
  int i, j, k, t;

  p = (s_S(S_error)) ? inval_2 : inval_0;
  if (!key_c) return p;
  label = &Str.S[S_label];
  for (i = 0; i < M_usilic; i++) {
    if (i >= 80 && !s_S(S_macro + i - 80)) continue;
    if (!label[i]) continue;
    t = (i) ? i : M_usilic;
    if (strchr(p, t)) continue;
    printf("%d:", t);
    cp = save_ov(key_c[t]);
    k = (t == M_cd || t == M_num) ? 1 : 0;
    if (strlen(cp) < 8-k) for (j = 8-k-strlen(cp); j; j--) putchar(' ');
    printf("%s", tobo(cp, 0));
    if (k == 1) printf("N");
    printf(" : %s", label[i]);
    do {
      if (i >= 80) printf(" (M)");
      switch(key_c[t]) {
      case K_v:		/* ^V */
      case (K_ec<<8) + 'v':	/* ^[v */
      case K_z:		/* ^Z */
	printf(" (ignored)");
	continue;
	break;
      default:
	for (j = 0; key_c[j]; j++)
	  if (j != t && key_c[j] == key_c[t]) break;
	if (key_c[j]) {
	  printf(" (overlapped)");
	  continue;
	}
	if (key_c[t] >= ' ' && key_c[t] <= 0x7e) ;
	else {
	  for (j = 0; command[j].key; j++)
	    if (command[j].key == key_c[t] 
		&& command[j].mode != t) break;
	  if (!command[j].key) continue;
	}
	break;
      }
    } while (0);
    printf("\n");
  }
  return NULL;
}

/*
 * 【機能】キーの表(key_m、key_c)を初期化する。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
void set_key(int **key_mp, int **key_cp, int key_m0[], int key_m[], int key_c[])
{
  int i;

  *key_mp = key_m;
  *key_cp = key_c;
  *key_m = *key_m0 = -1;
  for (i = 0; i < M_nn; i++) key_c[i] = -1;
  /* 組み込みのキー */
  key_c[M_key] = K_h;
  key_c[M_klt] = K_lt;
  key_c[M_krt] = K_rt;
  key_c[M_kup] = K_up;
  key_c[M_kdn] = K_dn;
  key_c[M_knext] = K_next;
  key_c[M_kprev] = K_prev;
  key_c[M_kforw] = K_v;
  key_c[M_kback] = (K_ec<<8) + 'v';
  key_c[M_dl] = K_dl;
  key_c[M_susp] = K_z;
  key_c[M_hs] = (K_o<<8) + 'h';
  key_c[M_ascii] = (K_o<<8) + 'a';
  key_c[M_copy] = (K_o<<8) + 'c';
  key_c[M_paste] = (K_o<<8) + 'p';
  key_c[M_ms] = (K_x<<8) + '(';
  key_c[M_me] = (K_x<<8) + ')';
  key_c[M_mm] = (K_x<<8) + 'e';
}

/*
 * 【機能】usilicrcのKEYの節の中の見出し(`M-?`、`F-?`、`K-?`)まで読み進める。無ければ書式エラー。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
void read_mark(FILE *fp, char mark[], u_char str[])
{
  if (!strncmp(&str[1], &mark[1], strlen(mark) - 1)) {
    if (*str == *mark) return;
    else key_err(fp, mark, S_errm);
  }
  do {
    if (!fgets(str, N_char, fp)) key_err(fp, mark, S_errm);
  } while (strncmp(str, mark, strlen(mark)));
}

/*
 * 【機能】usilicrcのKEYの節の`M-?`(キーボードマクロの定義)を読み込む。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
static void conf_m(FILE *fp, u_char str[])
{
  static char mark[] = "M-?";
  int i, j, f_m;
  u_char *cp, line[N_line];
  
  read_mark(fp, mark, str);
  f_m = !is_pg();
  for (i = 0; ; i++) {
    if ((j = get_str(fp, str)) < 0) key_err(fp, mark, S_errm);
    else if (is_pg() && j == 3) {
      i--;
      f_m = 1;
      continue;
    } else if (j) return;
    if (!*str) {
      i--;
      continue;
    }
    *line = '\0';
    if (!f_m) continue;
    for (cp = str; *cp; )
      if (!strncmp(cp, "K-", 2)) {
	cp = set_m(&cp[2], &j);
	strcat(line, mtoa(j));
      } else if (*cp == '\042') cp = set_s(cp, strchr(line, '\0'));
      else if (*cp == '\\') {
	do {
	  if (get_str(fp, str)) key_err(fp, mark, S_errm);
	} while (!*str);
	cp = str;
      }
    Str.S[S_macro + i] = strdup(line);	/* S_m0 ... S_m19 */
    f_m = !is_pg();
  }
}

/*
 * 【機能】usilicrcのKEYの節の`F-?`(ファンクションキー等の割り当て)を読み込む。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
static void conf_f(FILE *fp, u_char str[], int *i_mp, int *n_mp, int key_m0[], int key_m[])
{
  int i, j, k;
  u_char *cp;
  static char mark[] = "F-?";
  static char errn[] = "^[ (Esc)";

  read_mark(fp, mark, str);
  for (i = *n_mp = *i_mp = 0; ; i++) {
    if ((j = get_str(fp, str)) == 2) return;
    else if (j) key_err(fp, mark, S_errm);
    if (!*str) {
      i--;
      continue;
    }
    for (j = k = 0, cp = str; *cp; j += 8) {
      if (!strncmp(cp, "C-", 2) 
	       || !strncmp(cp, "C_", 2)
	       || !strncmp(cp, "\\c", 2)) {
	cp += 2;
	if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	  k = (k << j) + K_at;
	  cp += 2;
	} else if (*cp >= 'a' && *cp <= 'z') k = (k << j) + *cp - 'a' + 1;
	else if (*cp == '[') key_err(fp, errn, S_errn);
	else if ((*cp >= 'A' && *cp <= 'Z')
		 || (*cp > '[' && *cp <= '^'))
	  key_err(fp, &cp[-2], S_errm);
	cp++;
	switch(*cp) {
	case 0:
	case ' ':
	case '\t':
	  break;
	default:
	  key_err(fp, &cp[-3], S_errm);
	  break;
	}
      } else if (!strncmp(cp, "ESC", 3)) key_err(fp, errn, S_errn);
      else if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	k = (k << j) + 0x20;
	cp += 3;
      } else k = (k << j) + *cp++;
      while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
    }
    *n_mp = i;
    j = 0;
    if (k < 0xff) ;
    else if (k < 0xffff) key_m[(*i_mp)++ + 1] = k >> 8;
    else if (k < 0xffffff) {
      key_m[(*i_mp)++ + 1] = k >> 16;
      key_m[(*i_mp)++ + 1] = (k >> 8) & 0xff;
    }
    key_m0[i+1] = k;
  }
}

/*
 * 【機能】usilicrcのKEYの節の`K-?`(キーとコマンドの割り当て)を読み込む。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
static int conf_c(FILE *fp, u_char str[], int i0, int *i_mp, int n_m, int key_m0[], int key_c[], int key_m[], char comm[])
{
  static char mark[] = "K-?";
  int i, j, k;
  u_char *cp, **label;

  label = &Str.S[S_label];
  read_mark(fp, mark, str);
  for (i = i0; ; i++) {
    j = get_str(fp, str);
    if (!i0 && j == 2 && i < M_end) {
    }
    if (!i0) {
      if (j == 2) {
	if (i < M_end) key_err(fp, mark, S_errm);
	else return j;
      } else if (j) key_err(fp, mark, S_errm);
    } else if (j) return j;
    if (!*str) {
      i--;
      continue;
    }
    if ((cp = strchr(str, '\042'))) {
      if ((j = (u_int)rindex(&cp[1], '\042') - (u_int)&cp[1]) > 0) {
	label[i] = calloc(1, j+1);
	memcpy(label[i], &cp[1], j);
      }
      *cp = '\0';
      if (i == M_chdir) {
	char *p;
	int len;
	if ((p = strchr(label[i], '`')) 
	    && (len = (u_int)strchr(&p[1], '\047') - (u_int)&p[1]) - 1 > 0) {
	  memcpy(comm, &p[1], len);
	  while ((p = strstr(comm, "\\c")))
	    if (p[2] >= 'A' && p[2] <= 'Z') {
	      *p = p[2] - 'A' + 1;
	      memmove(&p[1], &p[3], strlen(&p[3]) + 1);
	    }
	  while ((p = strstr(label[i], "\\c")))
	    if (p[2] >= 'A' && p[2] <= 'Z') {
	      sprintf(p, "^%c", p[2]);
	      memmove(&p[2], &p[3], strlen(&p[3]) + 1);
	    }
	}
      }
    } else {
      i--;
      continue;
    }
    for (j = k = 0, cp = str; *cp; j += 8) {
      if (!strncmp(cp, "F-", 2) || !strncmp(cp, "F_", 2)) {
	cp += 2;
	if (*cp > n_m + '0') {
	  char *p = alloca(4);
	  sprintf(p, "F-%c", *cp);
	  key_err(fp, p, S_erro);
	} else if (j)
	  if (key_m0[*cp - '0'+1] > 256) k = (k << 16) + key_m0[*cp - '0' + 1];
	  else k = (k << 8) + key_m0[*cp - '0' + 1];
	else k = key_m0[*cp - '0' + 1];
	cp++; 
      } else if (!strncmp(cp, "C-", 2) 
		 || !strncmp(cp, "C_", 2)
		 || !strncmp(cp, "\\c", 2)) {
	cp += 2;
	if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	  k = (k << j) + K_at;
	  cp += 2;
	} else if (*cp >= 'a' && *cp <= 'z') k = (k << j) + *cp - 'a' + 1;
	else if (*cp == '[') k = (k << j) + *cp - 0x40;
	else if ((*cp >= 'A' && *cp <= 'Z')
		 || (*cp > '[' && *cp <= '^'))
	  key_err(fp, &cp[-2], S_errm);
	cp++;
	switch(*cp) {
	case 0:
	case ' ':
	case '\t':
	  break;
	default:
	  key_err(fp, &cp[-3], S_errm);
	}
      } else {
	if (!strncmp(cp, "ESC", 3)) {
	  k = (k << j) + 0x1b;
	  cp += 3;
	} else if (!strncmp(cp, "SPC", 3) || *cp == '@') {
	  k = (k << j) + 0x20;
	  cp += 3;
	} else k = (k << j) + *cp++;
      }
      while (*cp && (*cp == ' ' || *cp == '\t')) cp++;
    }
    if ((key_c[(i) ? i : M_usilic] = k) > 0xff && k >> 8 != K_ec) {
      k >>= 8;
      do {
	for (j = 1; j < *i_mp && key_m[j] != k; j++) ;
	if (j == *i_mp) key_m[(*i_mp)++] = k;
	k >>= 8;
      } while (k);
    }
  }
  return 0;
}

/*
 * 【機能】usilicrcのKEYの節(M-?、F-?、K-?)の全体を読み込む。一覧の出力(print_key)にも使う。
 * 【C++】なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止)
 */
int conf_key(FILE *fp, char str[], int **key_mp, int **key_cp, char *comm)/*usilic*/
{
  static int key_m[N_f*5];
  static int key_m0[N_f+1];
  static int key_c[M_nn+1];
  int n_m, i_m, f_skip = 0;
 
  if (key_cp == F5) print_key(key_c);
  else if (!fp) set_key(key_mp, key_cp, key_m0, key_m, key_c);
  else if (fp == F5) *((int*)key_cp) = key_c[*((int*)key_mp)];
  else {
    conf_f(fp, str, &i_m, &n_m, key_m0, key_m);
    conf_c(fp, str, 0, &i_m, n_m, key_m0, key_c, key_m, comm);
    conf_m(fp, str);
    f_skip = conf_c(fp, str, N_c, &i_m, n_m, key_m0, key_c, key_m, comm);
  }
  return f_skip;
}

/*
 * 【機能】usilicrcの全体(ARCHIVE、EXEC、GREP、PROGRAM、EDITOR、BUFFER、CODE、SHELL、HISTORY、WAIT、
 *       SCROLL、LS、CP/MV/RM、KEY)を読み込んで、各設定へ反映する。
 * 【C++】Configuration::loadRcText (src/cpp/src/configuration.cpp)
 * 【備考】C++のloadRcTextは、KEYの節を意図的に読み飛ばす。
 */
int init_config(CHILD *chp, LIST **lcpp, LIST **lgpp, STR **scpp, STR **sgpp, u_int *bsizep, int **key_mp, int **key_cp, char name[], u_char comm[])/*usilic*/
{
  FILE *fp;
  int f_shell, f_usilicrc, n_usilicrc, f_skip;
  u_char *p, *cp, str[N_line];
  char **usilicrc, sh[5];
  
  conf_key(NULL, str, key_mp, key_cp, NULL);
  usilicrc = s_P(P_usilicrc);
  for (n_usilicrc = 0; usilicrc[n_usilicrc]; n_usilicrc++) ;
  p = alloca(n_usilicrc + 1);
  memset(p, ' ', n_usilicrc);
  p[n_usilicrc] = '\0';
  fp = fopen(s_S(S_usilicrc), "r");
  conf_arc(NULL, str, chp);
  for (f_shell = f_skip = 0; f_skip >= 0; ) {
    if (!f_skip)
      do {
	if (!fgets(str, N_line, fp)) f_skip = -1;
      } while (f_skip >= 0 && (strchr(s_S(S_nop), *str)));
    if (f_skip < 0) continue;
    f_usilicrc = 0;
    do {
      if (!(cp = strchr(str, ' ')))
	if (!(cp = strchr(str, '\t')))
	  if (!(cp = strchr(str, '\n'))) continue;
      *cp = '\0';
    } while (0);
    for (f_usilicrc = 0; usilicrc[f_usilicrc]; f_usilicrc++) 
      if (!strcmp(usilicrc[f_usilicrc], str)) break;
    if (usilicrc[f_usilicrc] && !strchr(p, f_usilicrc + '0')) f_skip = 0;
    else if (!strncmp(&str[1], "-?", 2)) {
      f_skip = 0;
      continue;
    } else {
      save_stderr("%src: %s: %s.\n", s_S(S_usilic), str, s_S(S_errm));
      return -1;
    }
    p[f_usilicrc] = f_usilicrc + '0';
    switch(f_usilicrc) {
    case 0:
      if ((f_skip = conf_arc(fp, str, chp)) == 1)
	save_error("%src: %s: %s.\n", s_S(S_usilic), str, s_S(S_errm));
      break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 14:
    case 15:
    case 16:
      if ((f_skip = conf_bin(fp, str, f_usilicrc, chp, sh)) == 1) {
	save_error("%src: %s: %s.\n", s_S(S_usilic), str, s_S(S_errm));
	return -1;
      }
      if (f_usilicrc == 4 && f_skip == 3) f_shell = 1;
      break;
    case 5:
      f_skip = conf_grep(fp, str, lgpp, sgpp);
      break;
    case 6:
      f_skip = (lcpp) ? conf_prog(fp, str, lcpp, scpp) : 0;
      break;
    case 7:
      f_skip = conf_bsize(fp, str, bsizep);
      break;
    case 8:
      f_skip = conf_code(fp, str);
      break;
    case 9:
      f_skip = conf_history(fp, str);
      break;
    case 10:
      f_skip = conf_wait(fp, str);
      break;
    case 11:
      f_skip = conf_scroll(fp, str);
      break;
    case 12:
      f_skip = conf_ls(fp, str);
      break;
    case 13:
      f_skip = conf_key(fp, str, key_mp, key_cp, comm);
      break;
    default:
      f_skip = 0;
      break;
    }
  }
  f_usilicrc = 0;
  if (lcpp)  
    do {
      if (!f_shell)
	if (s_S(S_sh)) continue;
        else save_stderr("%s: %s.\n", s_S(S_usilic), s_S(S_errd), NULL);
      else if (f_shell 
	       && !get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL)) {
	save_stderr("%s: %s: %s.  %s.\n"
		    , s_S(S_usilic), sh, s_S(S_errc), s_S(S_errd));
      } else continue;
      f_usilicrc = -1;
    } while (0);
  save_child(F10, NULL, 0, NULL);
  fclose(fp);
  return f_usilicrc;
}

/*
 * 【機能】usilicrcの場所を決めて返す。`~/.<プログラム名>/<プログラム名>rc`(ホーム側)があればそれを使う。
 *       ただし、LIB側の同名ファイルの方が新しい(ctime)と判定されたときは、*errepへ1を入れてLIB側を
 *       返す。ホーム側が無ければLIB側を返し、そのLIB側も無ければ*errepへ-1を入れる。
 * 【C++】main (src/cpp/app/main.cpp); Configuration::loadRcText (src/cpp/src/configuration.cpp)
 * 【備考】旧ソースでは、st_libを一度もstat()していないため、「LIB側が新しい」の判定は不定の値で
 *       行われている(既存の不具合の疑い)。
 */
char *set_usilicrc(int *errep)/*usilic*/
{
  char *home, *lib;
  struct stat st_lib, st_home;

  home = malloc(strlen(s_S(S_home)) + 1 
		    + strlen(s_S(S_usilic)) + 1 + strlen(s_S(S_usilic)) + 2 + 1);
  sprintf(home, "%s.%s/%src", s_S(S_home), s_S(S_usilic), s_S(S_usilic));
  lib = malloc(strlen(s_S(S_lib))
		   + strlen(s_S(S_usilic)) + 2 + 1);
  sprintf(lib, "%s%src", s_S(S_lib), s_S(S_usilic));
  if (stat(home, &st_home) >= 0) {
    if (access(lib, F_OK) >= 0 && st_lib.st_ctime > st_home.st_ctime) {
      if (errep) *errep = 1;
    } else {
      free(lib);
      return home;
    }
  }
  if (access(lib, F_OK) < 0 && errep) *errep = -1;
  free(home);
  return lib;
}
