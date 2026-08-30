#ifndef _ALLOC_H

int exit_ALLOC(char *name);
void init_ALLOC(void);

#define _ALLOC_H
#else

int exit_ALLOC(char *name)
{
  return 0;
}

void init_ALLOC(void)
{
}

#endif
#ifndef _CONFIG_H
static char TMP[] = "/tmp/.usilic";
static char CHARSET[] = "euc";
static char DU[] = "du -ab";
static char CP[] = "cp -vRi";
static char MV[] = "mv -vi";
static char RM[] = "rm -vri";
static char LIB[] = "/tmp/usr/local/lib/usilic";
#endif _CONFIG_H
#define _CONFIG_H
#ifndef _MENU_H
typedef struct {
  u_int bsize;
  u_char *w;
  LL ll;
  SS ss;
  CHILD *child, *arc;
  STACK *sp;
  GREP *grep;
  BASE *bp;
  BASE base[N_base];
} HEAP;

typedef struct {
  char intr[N_line], comm[N_line];
  u_char name[N_line], full[N_line];
  u_char gname[N_line], cname[N_line];
  u_char du[N_line];
  u_char mess[N_line];
  u_char gstr[N_line], iname[N_line];
  u_char lstr[N_line];
  int *key_m, *key_c;
} ARRAY;

typedef struct {
  u_int help	: 1;
  u_int ls	: 1;
  u_int winch	: 1;
  u_int cut	: 2;
  u_int grep	: 2;
  u_int cr	: 2;
  u_int comm	: 3;
} FLAG;

typedef struct {
  int klen, nlen, clen, error;
  int M, Mb;
  int ll;
  int ppid;
  int history;
  int fd;
  u_int tell;
} NUM;

#include "mode.P"

#define _MENU_H
#endif
/* Following mode-No. must be equal to K-No. of usilicrc */
#define M_help	1
#define M_bl	2
#define M_clear	3
#define M_forw	4
#define M_back	5
#define M_forw2	6
#define M_back2	7
#define M_next	8
#define M_prev	9
#define M_dn	10
#define M_up	11
#define M_head	12
#define M_tail	13
#define M_num	14
#define M_mark	15
#define M_jump	16
#define M_w4	17
#define M_sel	18
#define M_m	19
#define M_cd	20
#define M_bl2	21
#define M_cr	22
#define M_fp	23
#define M_code	24
#define M_hs	25
#define M_ls	26
#define M_ascii	27
#define M_kanji	28
#define M_copy	29
#define M_paste	30
#define M_fgrep	31
#define M_egrep	32
#define M_cut2	33
#define M_cut	34
#define M_fcut2	35
#define M_fcut	36
#define M_du	37
#define M_du2	38
#define M_locate	39
#define M_locate2	40
#define M_prog2	41
#define M_prog	42
#define M_man2	43
#define M_man	44
#define M_lfile2	45
#define M_lfile	46
#define M_ldir2	47
#define M_ldir	48
#define M_hist	49
#define M_cmd	50
#define M_cp	51
#define M_cp2	52
#define M_mv	53
#define M_mv2	54
#define M_rm	55
#define M_rm2	56
#define M_arc	57
#define M_arc2	58
#define M_shell	59
#define M_hup	60
#define M_chdir2	61
#define M_chdir	62
#define M_w	63
#define M_w2	64
#define M_list	65
#define M_edit	66
#define M_error	67
#define M_log	68
#define M_exit	69
#define M_quit	70
#define M_dest	71
#define M_yes	72
#define M_no	73
#define M_ms	74
#define M_me	75
#define M_mm	76
#define M_end	77
/* M-? <== This string musn't be edited. See mode.pl */
#define M__0	80
#define M__1	81
#define M__2	82
#define M__3	83
#define M__4	84
#define M__5	85
#define M__6	86
#define M__7	87
#define M__8	88
#define M__9	89
#define M__10	90
#define M__11	91
#define M__12	92
#define M__13	93
#define M__14	94
#define M__15	95
#define M__16	96
#define M__17	97
#define M__18	98
#define M__19	99
/* mode[] in str.c needs following No. */
#define M_usilic	100
#define M_key	101
#define M_klt	102
#define M_krt	103
#define M_kup	104
#define M_kdn	105
#define M_knext	106
#define M_kprev	107
#define M_kforw	108
#define M_kback	109
#define M_dl	110
#define M_susp	111
#define M_getch	112
#define M_nn	113
#define T_pipe	114
#define T_pipe2	115
#define T_hfile	116
#define T_hdir	117
#define T_hman	118
#define T_hlocate	119
#define T_hfgrep	120
#define T_hegrep	121
#define T_mode	122
#define T_null	123
/* Following can be set in any sequence */
#define T_bin	124
#define T_gz	125
#define T_buff	126
#define T_dir	127
#define T_skip	128
#define T_tar	129
#define T_lha	130
#define T_unzip	131
#define T_rpm	132
#define T_deb	133
#define T_roff	134
#define T_type	135
#define T_label	136
#define T_off	137
#define T_out	138
#define T_not	139
#define T_prompt	140
#define T_disp	141
#define T_input	142
#define T_cr	143
#define T_in	144
#define T_w	145
#define T_mess	146
#define T_prog	147
#define T_man	148
#define T_cp	149
#define T_mv	150
#define T_rm	151
#define T_locate	152
#define T_list	153
#define T_help	154
#define T_exit	155
#define T_file	156
#define T_file2	157
#define T_split	158
#define T_split2	159
#define T_tag	160
#define T_tag2	161
#define T_winch	162
#define T_winch2	163
#define T_du	164
#define T_du2	165
#define T_grep	166
#define T_grep2	167
#define T_fcut	168
#define T_fcut2	169
#define T_shell	170
#define T_shell2	171
#define T_hist	172
#define T_hist2	173
#define T_exec	174
#define T_exec2	175
#define T_cut	176
#define T_cut2	177
#define T_key	178
#define T_key2	179
#define T_arc	180
#define T_arc2	181
#define T_arc3	182
#define T_cmd	183
#define T_cmd2	184
#define T_cmd3	185
#define T_pipe3	186
#define T_pipe4	187
#define T_pipe5	188
#define T_cat	189
#define T_0	190
#define T_1	191
#define T_2	192
#define T_3	193
#define T_4	194
#define T_5	195
#define T_6	196
#define T_7	197
#define T_8	198
#define T_9	199
#ifndef _USILIC_H
#if 1
#define SKIP_SPC
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

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
  C_base64, C_quote, C_shift_jis, C_us_ascii, C_euc_jp, 

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

typedef struct { /* attribute */
  short n;
  short attr;
} ATTR;

typedef struct PP { /* line's pointer */
  struct PP *prev;
  struct PP *next;
  u_char *cs;
  ATTR *attr;
  u_int tell;
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
#include "usilic.P"
#include "alloc.h"

#define _USILIC_H
#endif
"version 1.5b-1  20 Mar 2000\012Coded by Taketo Matsubara\012E-Mail: moody@osk.3web.ne.jp\012"
