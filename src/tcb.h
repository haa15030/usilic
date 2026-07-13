/*
 * u_char/u_int/u_long come from sys/types.h. On Linux/glibc these
 * happened to become visible transitively through another header
 * (e.g. unistd.h) once _DEFAULT_SOURCE is active, but that is not
 * portable: macOS/clang's libc does not pull them in the same way,
 * so without an explicit include this fails there with "unknown
 * type name 'u_char'" etc. Include it directly so it works
 * everywhere regardless of what other headers happen to do.
 *
 * Separately, glibc also needs _XOPEN_SOURCE for the POSIX pty
 * calls (posix_openpt/grantpt/unlockpt/ptsname), and _XOPEN_SOURCE
 * alone would hide the BSD typedefs above, hence _DEFAULT_SOURCE
 * too. This part is glibc/Linux-specific: macOS and the *BSDs
 * expose both families by default, and defining _XOPEN_SOURCE
 * there can instead hide their own BSD extensions, so leave their
 * headers alone.
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
#ifndef _TCB_H
#if 1
#define SKIP_SPC
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>

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

  S_tcb=S_label+M_tcb, S_code, S_tcbrc, S_lb2, S_path, 
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
  S_TCB, S_option, 
  S_LS=S_option, S_DU, S_CP, S_MV, S_RM,
  S_nn=S_option+9,

  P_child=0, P_mode, P_tcbrc, P_code, P_arc,
  P_nn,
};

typedef struct { /* attribute */
  short n;
  short attr;
} ATTR;

typedef struct PP { /* line's pointer */
  struct PP *prev;
  struct PP *next;
  u_char *cs;
  ATTR *attr;
  uintptr_t tell; /* also holds a raw u_char* into an archive buffer
		      (alloc_tar/alloc_lha/alloc_unzip + init_arc etc.);
		      u_int truncated it on 64-bit, causing ARC mode to
		      segfault or read garbage */
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

typedef struct { /* reverse string */
  u_char str[N_line], copy[N_line];
  int row, col;
  PP *ps, *pe, *pp;
} RV;

typedef struct CHILD { /* child process */
  char **argv;
  char *suffix;
  struct {
    u_long chp	: 1;
    u_long exec	: 1;
    u_long argc	: 4;
  } f;
  struct CHILD *next;
} CHILD;

typedef struct PN { /* position */
  int row;			/* cursor line */
  int nrow;			/* total line */
  u_int top;			/* current page's top line */
  int line;			/* line number */
} PN;

typedef struct LIST { /* string list */
  PN pn;
  u_char *name;
  struct LIST *left;
  struct LIST *right;
} LIST;
  
typedef struct STR { /* backup */
  PN pn;
  u_char *name;
  struct STR *next;
} STR;
  
typedef struct { /* window's pointer */	
  int nrow;			/* total line */
  int size;			/* display size */
  u_char *buff;
  PP *pp;
} BASE;

typedef struct {
  u_char name[N_line];
  u_char *buff, str[N_line];
  CHILD *chp;
  BASE *bp;
} CUT;

typedef struct { /* mode-line */
  u_char dir[N_line];
  u_char old[N_line];
  u_char arg[N_char];
  u_char prev[N_line];
  CUT cut;
} LINE;

typedef struct GREP { /* work area for GREP */
  int f_M;
  u_char name[N_line], str[N_line];
  LINE l;
  BASE *bp;
  struct GREP *prev;
} GREP;

typedef struct STACK { /* mode stack */
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
    u_int i : 1; /* index list */
    u_int q : 1; /* quoted printable */
    u_int e : 1; /* escape code */
    u_int s : 4; /* sequence */	
  } f;
  u_int c;
} JIS;

typedef union { /* cursor position's list */
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

typedef union { /* cursor position's list */
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

#define F1		(void *)-1  /* not pipe */
#define F2		(void *)-2  /* print_line() */
#define F3		(void *)-3  /* mark for virtual line */
#define F4		(void *)-4  /* not enough memory (alloc_sh()) */
#define F5		(void *)-5  /* conf_key() */
#define F6		(void *)-6  /* save_history() etc. */
#define F7		(void *)-7  /* error of make_bp() */
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
#include "tcb.P"
#include "alloc.h"

#define _TCB_H
#endif
