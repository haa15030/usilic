/*
 * u_char/u_int/u_longはsys/types.hに由来する。Linux/glibcでは、
 * _DEFAULT_SOURCEが有効になっていれば、これらは別のヘッダー
 * (unistd.h等)経由でたまたま間接的に見えるようになっていたが、
 * これは移植性が無い: macOS/clangのlibcは同じようにはそれらを
 * 引き込まないため、明示的なincludeが無いとそちらでは"unknown
 * type name 'u_char'"等のエラーで失敗する。他のヘッダーが何を
 * するかに関わらずどこでも動くよう、直接includeする。
 *
 * 別件として、glibcはPOSIXのpty呼び出し(posix_openpt/grantpt/
 * unlockpt/ptsname)のために_XOPEN_SOURCEも必要とするが、
 * _XOPEN_SOURCE単体では上のBSDのtypedefが隠れてしまうため、
 * _DEFAULT_SOURCEも合わせて必要になる。この部分はglibc/Linux
 * 固有である: macOSと*BSD群はどちらの系統もデフォルトで公開して
 * おり、そこで_XOPEN_SOURCEを定義すると逆にそれら自身のBSD拡張が
 * 隠れてしまうことがあるため、それらのヘッダーには手を出さない。
 */
#ifdef __linux__
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif
#include <sys/types.h>
#ifndef _USILIC_H
#if 1
#define SKIP_SPC
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>

#include "progname.h"
#include "mode.h"

#define N_buff	0x10000
#define N_pipe	0xfff
#define N_char	0x100
#define N_num	0x200
#define N_line	0x400
#define N_attr	0x200
#define N_man	0x1000
#define N_macro	256
#define N_base	12
#define N_ss 	16
#define N_ll 	9
#define N_f	10
#define N_c	80
#define N_m	20
#define N_ls	16

#define is_euckana_1_byte(n)	(n == 0x8e)
#define is_euckana_2_byte(n)	(0xa1<=n && n<=0xdf)
#define is_euc_1_byte(n)	(n>=0xa1 && n<=0xff)
#define is_euc_2_byte(n)	(n>=0xa1 && n<=0xff)
#define is_sjis_1_byte(n)	((n>=0x81 && n<=0x9f) || (n>=0xe0 && n<=0xea))
#define is_sjis_2_byte(n)	(n>=0x40)
#define is_sjiskana(n)		(n >= 0xa1 && n <= 0xdf)
#define is_sjis(n)		(n >= 0x81 && n <= 0x9f && n != 0x8e)

enum {
  K_00, K_a, K_b, K_c, K_d,
  K_e, K_f, K_g, K_h, K_i,	

  K_j, K_k, K_l, K_m, K_n,
  K_o, K_p, K_q, K_r, K_s,

  K_t, K_u, K_v, K_w, K_x,
  K_y, K_z, K_ec, K_krt, K_klt,

  K_kup, K_kdn,

  K_dl=127, K_at, K_up, 
  K_lt, K_dn, K_rt, K_prev, K_next, 

  K_a0=160,

  A_null=200, A_us, A_ue, A_md, A_me,
  A_us2, A_ue2, A_md2, A_me2, A_md3, 

  A_cs, A_ce,

  C_ascii=300, C_bin, C_sjis, C_euc, C_jis, 
  C_base64, C_quote, C_shift_jis, C_us_ascii, C_euc_jp, C_utf8, 

  S_macro=0, 
  S_label=S_macro+N_m,

  S_usilic=S_label+M_usilic, S_code, S_usilicrc, S_lb2, S_path, 
  S_sh, S_w, S_write, S_mess, S_hist, 

  S_intr, S_version, S_mark, S_home, S_error, 
  S_log, S_help, S_ls, S_du, S_cp, 

  S_mv, S_rm, S_groff, S_man, S_locate, 
  S_file, S_egrep, S_fgrep, S_gzip, S_bzip2, 

  S_tar, S_lha, S_unzip, S_rpm, S_rpm2cpio, 
  S_deb, S_tty, S_err0, S_err1, S_err2, 

  S_err3, S_err4, S_err5, S_err6, S_err7, 
  S_err8, S_err9, S_erra, S_errb, S_errc, 

  S_errd, S_erre, S_errf, S_errg, S_errh, 
  S_erri, S_errj, S_errk, S_errl, S_errm, 

  S_errn, S_erro, S_errp, S_errq, S_errr, 
  S_errs, S_errt, S_erru, S_errv, S_errw,

  S_errx, S_erry, S_errz, S_pid, S_tmp, 
  S_nop, S_lib, S_stderr, S_arc1, S_arc2, 

  S_arc3, S_arc4, S_arc5, S_enter, S_LANG, 
  S_USILIC, S_option, 
  S_LS=S_option, S_DU, S_CP, S_MV, S_RM,
  S_nn=S_option+9,

  P_child=0, P_mode, P_usilicrc, P_code, P_arc,
  P_nn,
};

typedef struct { /* 属性 */
  short n;
  short attr;
} ATTR;

typedef struct PP { /* 行のポインタ */
  struct PP *prev;
  struct PP *next;
  u_char *cs;
  ATTR *attr;
  uintptr_t tell; /* アーカイブバッファ(alloc_tar/alloc_lha/
		      alloc_unzip + init_arc等)への生のu_char*も
		      保持している; u_intでは64bitでそれが切り
		      詰められてしまい、ARCモードがセグフォしたり
		      ゴミを読んだりする原因になっていた */
  int split;
  struct {
    u_int nl	: 1;
    u_int code	: 1;
    u_int bin	: 1;
    u_int str	: 1;
    u_int len	: 9;
    u_int line	: 19;
  } f;
} PP;

typedef struct { /* 逆順文字列 */
  u_char str[N_line], copy[N_line];
  int row, col;
  PP *ps, *pe, *pp;
} RV;

typedef struct CHILD { /* 子プロセス */
  char **argv;
  char *suffix;
  struct {
    u_long chp	: 1;
    u_long exec	: 1;
    u_long argc	: 4;
  } f;
  struct CHILD *next;
} CHILD;

typedef struct PN { /* 位置 */
  int row;			/* カーソル行 */
  int nrow;			/* 総行数 */
  u_int top;			/* 現在のページの先頭行 */
  int line;			/* 行番号 */
} PN;

typedef struct LIST { /* 文字列リスト */
  PN pn;
  u_char *name;
  struct LIST *left;
  struct LIST *right;
} LIST;

typedef struct STR { /* バックアップ */
  PN pn;
  u_char *name;
  struct STR *next;
} STR;

typedef struct { /* ウィンドウのポインタ */
  int nrow;			/* 総行数 */
  int size;			/* 表示サイズ */
  u_char *buff;
  PP *pp;
} BASE;

typedef struct {
  u_char name[N_line];
  u_char *buff, str[N_line];
  CHILD *chp;
  BASE *bp;
} CUT;

typedef struct { /* モードライン */
  u_char dir[N_line];
  u_char old[N_line];
  u_char arg[N_char];
  u_char prev[N_line];
  CUT cut;
} LINE;

typedef struct GREP { /* GREP用の作業領域 */
  int f_M;
  u_char name[N_line], str[N_line];
  LINE l;
  BASE *bp;
  struct GREP *prev;
} GREP;

typedef struct STACK { /* モードスタック */
  struct {
    u_int bp		: 1;
    u_int M		: 16;
  } f;
  u_char *prev;
  u_char *name;
  u_char *dir;
  u_char *old;
  char *arg;
  CUT cut;
  BASE *bp;
  struct STACK *next;
} STACK;

typedef union {
  struct {
    u_int i : 1; /* インデックスリスト */
    u_int q : 1; /* quoted printable */
    u_int e : 1; /* エスケープコード */
    u_int s : 4; /* シーケンス */
  } f;
  u_int c;
} JIS;

typedef union { /* カーソル位置のリスト */
  struct {
    STR *file;		/* 0 */
    STR *dir; 		/* 1 */
    STR *prog;		/* 2 */
    STR *man;		/* 3 */
    STR *grep;		/* 4 */
    STR *cmd;		/* 5 */
    STR *hist;		/* 6 */
    STR *rprog;		/* 7 */
    STR *rgrep;		/* 8 */
    STR *hfile;		/* 9 */
    STR *hdir;		/* 10 */
    STR *hman;		/* 11 */
    STR *hlocate;	/* 12 */
    STR *hfgrep;	/* 13 */
    STR *hegrep;	/* 14 */
    STR *mode;		/* 15 */
  } p;
  u_long a[N_ss];
} SS;

typedef union { /* カーソル位置のリスト */
  struct {
    LIST *mode;		/* 0 */
    LIST *file;		/* 1 */
    LIST *dir; 		/* 2 */
    LIST *prog;		/* 3 */
    LIST *man;		/* 4 */
    LIST *grep;		/* 5 */
    LIST *cmd;		/* 6 */
    LIST *list;		/* 7 */
    LIST *split;	/* 8 */
  } p;
  u_long a[N_ll];
} LL;

typedef struct {
  void (*flush)(void);
  int (*printf)(const char*, va_list);
  int (*put_char)(int);
  void (*endstand)(void);
  void (*stand)(void);
  void (*move)(int, int);
  int  (*row)(int);
  int  (*col)(int);
  void (*clear)(void);
  void (*eeol)(void);
  void (*left)(void);
  void (*sf)(void);
  void (*sr)(void);
  void (*rc)(void);
  void (*sc)(void);
  void (*cs)(int, int);
  void (*seta)(int);
  char *(*geta)(int);
  void (*ks)(char *[]);
  int  (*mode)(int);
  int  (*cursor)(int);
} VT;

#define F1		(void *)-1  /* pipeではない */
#define F2		(void *)-2  /* print_line() */
#define F3		(void *)-3  /* 仮想行の目印 */
#define F4		(void *)-4  /* メモリ不足(alloc_sh()) */
#define F5		(void *)-5  /* conf_key() */
#define F6		(void *)-6  /* save_history()等 */
#define F7		(void *)-7  /* make_bp()のエラー */
#define F8		(void *)-8  /* print_intr() */
#define F9		(void *)-9  /* fork_exec() */
#define F10		(void *)-10 /* save_child() */
#define F11		(void *)-11 /* stand_str() */
#define F12		(void *)-12 /* is_inter() */
#define F13		(void *)-13 /* set_help() */

#define sig_child SIG_IGN

#define debug			save_error
#define is_prev(p,m)		!strncmp(p, &s_P(P_mode)[m][1] \
					 , strlen(&s_P(P_mode)[m][1]))
#define is_name(n,m)		!strncmp(n, s_P(P_mode)[m] \
					 , strlen(s_P(P_mode)[m]))
#define is_arg(a, m)		strstr(a, s_S(m))
#define is_pg()			(vt_set(0) & (0x20 | 0x10 | 0x04))
#define is_fm()			(vt_set(0) & (0x80 | 0x40))
#include "usilic.P"
#include "alloc.h"

#define _USILIC_H
#endif
