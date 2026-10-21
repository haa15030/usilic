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
static void decode_base64(u_char *code, u_char mime[], u_char end[]);
static void decode_quote(u_char *code, u_char mime[], u_char end[]);
static int conv_q(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy);
static int conv_jis(int buff_i, int len, u_char *cp, u_char euc[], JIS *jisp);
static int decode_mime(u_char **cpp, int *buff_ip, int *lenp, u_char euc[], u_char *end);
static int conv_sjis(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy);
static int conv_euc(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy);
void set_rv(NUM *np, PN *pnp, RV *rvp, HEAP *hp, int etop);
int load_ll(CHILD *chp, ARRAY *ap, u_char prev[], CUT *cutp, HEAP *hp, PN *pnp, int f_M, FILE **pinp);
int mode(HEAP *hp, NUM *np, ARRAY *ap, LINE *lp, RV *rvp);
void clear_stack(HEAP *hp, LINE *lp, char *name, int *f_Mp, int f_clear);
char *set_cut(HEAP *hp, CUT *cutp, u_char mess[], char name[], char dir[], char old[]);
void check_dir(HEAP *hp, LINE *lp, char *dir, LIST **dirp);
int do_edit(int row, char name[], char arg[], int f_M);
int is_write(u_char name[], int f_dir);
void ins_q(u_char str[]);
static int tc_row(int i);
static void tc_rc(void);
static void tc_sc(void);
static void tc_cs(int m, int n);
int set_attr(ATTR attr[], u_char str[], int f_stand, int f_grep);
PP *alloc_pipe(int init, FILE **pinp, int *nrowp, BASE *bp0, int nlen0);
PP *alloc_list(LIST *list, int *rowp, int f_grep, int f_ls);
PP *alloc_str(STR s0[], int *rowp, int f_grep);
PP *alloc_pp(u_char buff[], int f_M0, int size, int *rowp, char name[]);
PP *alloc_head(u_char buff0[], int f_M0, int size0, int *rowp);
void alloc_tar(u_char cp0[], PP pp0[], int size);
void alloc_lha(u_char cp0[], PP pp0[], int size);
void alloc_unzip(u_char cp0[], PP pp0[], int size);
char **alloc_arg(char *arg, int *argcp);
void free_arg(char **argv);
u_char *alloc_split(char name[], int tell, int size);
u_char *alloc_buff(int *sizep, u_int fd, int f_chkbin);
PP *alloc_sh(u_char *str, int pid0, int c);
void make_bp(BASE *bp, char mess[], char *name, char *cp);
PP *set_pp(u_char *cs, int size, int *rowp, int f_code, int f_bin);
PP **set_ppp(void *p, void *p0, int f_set);
int alloc_winch(BASE *bp, int f_ppp);
void free_bp(BASE *bp);
FILE *seek_intr(FILE *pin, int *nrowp);
CHILD *save_child(CHILD *chp, char *cp0, int f_child, LIST **lpp);
CHILD *check_ext(char *name, u_char *split, int size);
CHILD *init_child(void);
CHILD *get_child(CHILD *chp0, char *suffix, char *bin, char *name, CHILD *next);
int cat_child(CHILD *chp, char pathname[]);
void free_chp(CHILD *chp);
void free_child(CHILD *chp0);
int is_child(char name[], CHILD *chp);
CHILD *load_chp(char arg[]);
int is_chp(CHILD *chp, char *arg);
int is_man(char *man);
char *type_pipe(char *buff, int size, int code);
char *type_file(char *buff, char name[], int code);
u_char *check_type(u_char *buff, int *sizep, char *name, int *f_Mp, int f_chkbin, int *codep);
int jistoeuc(int buff_i, u_char jis[], JIS *jisp);
int sjistoeuc(int buff_i, u_char **sjisp, int sjis_i, JIS *dummy);
int euctoeuc(int buff_i, u_char **eucp, int euc_i, JIS *dummy);
int count_k(u_char *cp, int ci);
int init_code(int *sizep, u_char *buff, int f_chkbin);
void make_map(RV *rvp, int nlen);
int toeuc(u_char sjis[]);
void put_str(PP *pp, int f_mark);
PP *disp_pp(PP *pp, int f_mark);
int disp_window(PP *ps0, PP *pp0, int f_M0, char name0[], u_char prev0[], char *argp, int *lenp0);
int print_line(void *ps0, PP *pp, int len, u_char *linep);
void disp_prev(u_char *prev, void *dir, int klen0);
void disp_copy(void *p, int nlen);
u_char *read_buff(int *sizep, u_int fd, int f_chkbin);
u_char *fork_key(int *sizep);
u_char *fork_bin(CHILD *chp, int *sizep, int f_M, int f_chkbin);
void fork_exec(CHILD *chp, char arg[], u_char buff[], int size);
u_char *fork_chp(CHILD *chp, u_char *buff, int *sizep, char arg[]);
u_char *fork_arc(char name[], int f_M, CHILD *chp0, int *tmp_sizep, int *bp_sizep, BASE *bp);
u_char *fork_split(int *sizep, PP *pp, PP *ps);
u_char *fork_grep(int *sizep, u_char str[], PP *pp, int f_fgrep);
u_char *fork_grep2(int *sizep, u_char *buff, u_char str[], char name[]);
FILE *fork_man(char str[]);
FILE *fork_pipe(int f_chp, CHILD *chp, char name[]);
char *fork_file(char name[]);
int fork_du(u_char dir[]);
char *read_arc(CHILD *arc, char prev[], char name[], int *sizep);
void init_man(BASE *bp, FILE **pinp, char name[], u_char prev[], char arg[], int *klenp);
int init_pipe(BASE *bp, FILE **pinp, int f_Mb, char mess[], char prev[], char arg[], int *klenp, CHILD **chpp, int ppid);
void init_cut(BASE *bp, char name[], u_char str[], BASE *cut_bp);
void init_cut2(BASE *bp, u_char *buff, char name[], u_char str[]);
CHILD *init_arc(BASE *bp, CHILD *arc, char name[], u_int bsize, u_char prev[], char arg[], PP *pp, int f_chkbin);
FILE *init_arc2(BASE *bp, CHILD *arc, char name[], PP *pp, char prev[], char arg[]);
void init_grep(BASE *bp, char name[], u_char str[], PP *pp, int fgrep);
void init_ls(BASE *bp, char name[], int f_M, int f_ls);
void init_split(BASE *bp, char name[], PP *pp, BASE *cut_bp, int f_chkbin);
void init_split2(BASE *bp, u_char *buff, u_int bsize, u_char name[], PP *pp, int f_chkbin);
FILE *init_split3(BASE *bp, char name[], PP *pp, PP *ps);
FILE *init_split4(BASE *bp, u_char *buff, u_char name[], PP *pp);
int init_bin(BASE *bp, BASE *head, CHILD **chpp, int f_M, char name[], int f_chkbin);
void key_winch(int spid0);
void key_term(int dummy);
int read_m(RV *rvp, int f_M, int klen, u_char *cva, char *cname, LIST **listp, STR *cmd, u_char *iname, char *dir, char *old);
void load_signal(void);
void save_signal(int f_usilic);
void make_cmd(STR *sp, LIST **listp);
int make_list(u_char dir0[], u_char buff[], int *lenp, LIST **listp, int f_M);
int edit_line(int m, int mb, u_char buff[], int len, int row, int col0, int *colp, int *colep, int *shiftp, u_char *copy);
int read_esc(int f_M, int *key_r0, int *key_c0);
void menu(char pathname[], FILE *pin);
int echo_mess(int f_mess);
void free_h(void *hp0);
int skip_spc(u_char cs[], int f_spc);
int fmark(int f_M);
int nmark(PP *pp, int f_M);
int send_pp(PP *pp, char fmode[]);
int do_rm(LL *llp, SS *ssp, u_char prev[], FILE *fp);
int get_hup(int pid0);
void do_prog(char *str, STR **cmdp, STR **histp);
void save_ll(int n_ll, LL *llp, SS *ssp, char full[], PN *pnp);
void do_exec(char name[], CHILD *chp);
void clear_base(BASE *base);
PP *free_pp(PP *pp);
char *command(int f_Mb, u_char str[], BASE *bp, LINE *lp, int klen, u_char du[], int *fdp);
CHILD *set_cutchp(char arg[]);
void print_intr(char *save, char *mess);
int move_page(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M);
void move_row(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M);
PP *sr_row(PP *pp, int f_mark);
PP *sf_row(PP *pp, int f_mark);
int init_col(int f_M, char str[], int size);
int init_row(PP **ppp, PN *pnp, int tell, int etop);
int get_col(char str[]);
void write_comm(int pfd, int m, char *comm, int spid);
int from_main(char name[], char str[], char *mess, FILE **pinp, int *ppidp);
int wait_comm(int pfd, int *posp, int *rowp, int col0p, u_char *buff, u_char pp_str[]);
int start_shell(int shell_pid, int disp_pid, FILE **ofpp, FILE **ifpp, char *mess, char pty[]);
int do_shell(u_char prev[], char str[], FILE **pinp, u_char *w, int *ppidp, LIST *prog, LIST **listp, STR **cmdp, STR **histp, int f_comm);
int is_inter(int *pv0, int pid0, int spid0);
void r_S(int n_S);
char *s_S(int n_S);
char **s_P(u_int n_P);
int conf_wait(FILE *fp, char str[]);
int conf_ls(FILE *fp, char *str);
int conf(char *temp, char *args, char *version, int f_usilic);
void free_S(void);
u_char *print_key(int *key_c);
int conf_key(FILE *fp, char str[], int **key_mp, int **key_cp, char *comm);
int init_config(CHILD *chp, LIST **lcpp, LIST **lgpp, STR **scpp, STR **sgpp, u_int *bsizep, int **key_mp, int **key_cp, char name[], u_char comm[]);
char *set_usilicrc(int *errep);
void rspc(u_char *cp, u_char *np);
int find_prog(LIST *prog, u_char buff[]);
GREP *load_grep(GREP *grep);
GREP *save_grep(GREP *grep);
int find_grep(GREP *grep, BASE *bp);
LIST *delete_name(LIST *lp, STR *sp, STR *rsp, FILE *fp);
STR *save_history(STR *hist, char name[], int f_find);
STR *free_history(STR *hist, char tmp[], int n_hist);
void find_list(LIST *lp, char name[], int *ip);
STR *save_str(STR *sp, char name[], PN *pnp);
STACK *read_stack(STACK *sp, void *p, int f_read);
STACK *load_stack(STACK *sp, LINE *lp, int *f_Mp, char *name, BASE **bpp, BASE base[]);
STACK *save_stack(STACK *sp, LINE *lp, int f_M, char name[], BASE *bp, int f_bp);
STACK *save_stack_dir(STACK *sp, char dir[], char old[], BASE *bp);
void get_name(char *pathname);
int get_dir(char *pathname);
void erase_dot(char name[]);
void read_list(LL *llp, SS *ssp, LINE *lp, char name[]);
void write_list(LL ll, SS ss, LINE *lp, char name[]);
LIST *free_list(LIST *list);
void free_str(STR *sp);
BASE *init_str(int f_M, BASE *bp, STR s0[], char name[]);
BASE *init_list(int f_M, BASE *bp, LIST *lp, char name[]);
int load_list(LIST *list, char name[], PN *pn);
LIST *save_list(LIST *list, char name[], PN *pnp);
char *utoa(u_int u);
char *tobo(char str[], int f_nl);
char *toul(char str[], int f_nl);
char *tome(char str[], int f_nl);
int ichrstr(char str0[], char c, char *str);
void print_arg(char arg[], int size, u_char *buff, CHILD *chp, int f_M);
void dup_error();
void save_error(char *fmt, ...);
void save_stderr(char *fmt, ...);
void save_intr(int n_error);
int load_intr(void);
char *is_inst(char *path, char *prog);
int is_access(char name[], BASE *bp);
int is_str(int f_M, int *n_bp);
int is_list(int f_M, int *n_bp);
int open_pty(char str[], int f_open);
u_char *mtoa(int f_M);
void print_buff(u_char buff[], int kanji_s);
void print_ctrl(int cc, u_char str[]);
void unlink_file();
void print_pid(int pid);
int strint(int tab[], int u);
int is_comm(int f_M, int f_comm);
CHILD *check_chp(int s_i);
void print_stderr(char *err0);
int is_ls(int f_M);
char *set_help(char *cp);
VT *tc_load(void);
void exit_usilic(void);
void vt_printf(char *fmt, ...);
int vt_putchar(int c);
void vt_flush(void);
void vt_endstand(void);
void vt_stand(void);
void vt_move(int row, int col);
int vt_row(int row);
int vt_col(int col);
void vt_clear(void);
void vt_eeol(void);
void vt_left(void);
void vt_sf(void);
void vt_sr(void);
void vt_rc(void);
void vt_sc(void);
void vt_cs(int m, int n);
void vt_seta(int attr);
char *vt_geta(int attr);
void vt_ks(char *ks[]);
int vt_mode(int mode);
int vt_cursor(int f_cursor);
int vt_set(int f_usilic0);
int get_winsize(int *rowp, int *colp);
int set_winsize(int row, int col);
int vt_getch(void);
int vt_charset(int i);
void vt_winsize(void);
void vt_setwin(int row, int col);
int vt_tfd(int i);
int vt_init(int init);
#include "usilic.h"
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

int set_attr(ATTR attr[], u_char str[], int f_stand, int f_grep)/*usilic*/
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
  save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
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

PP *alloc_pipe(int init, FILE **pinp, int *nrowp, BASE *bp0, int nlen0)/*usilic*/
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
	      save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
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
	  save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
	  return;
	}
      }
    }
    alloc_l(list->right, rowp, f_grep, f_ls);
  }  
}

PP *alloc_list(LIST *list, int *rowp, int f_grep, int f_ls)/*usilic*/
{
  int n_row = 0;

  Alloc.pp0 = Alloc.pp1 = NULL;
  alloc_l(list, &n_row, f_grep, f_ls);
  *rowp = n_row;
  return Alloc.pp0;
}  

PP *alloc_str(STR s0[], int *rowp, int f_grep)/*usilic*/
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
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
  return pp0;
}  

PP *alloc_pp(u_char buff[], int f_M0, int size, int *rowp, char name[])/*usilic*/
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
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
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

PP *alloc_head(u_char buff0[], int f_M0, int size0, int *rowp)/*usilic*/
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
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
  if (f_M0 < 0) { /* <== SIGWINCH (menu.c:(5)) */
    for (pp = pp0, pp1 = pp2; pp; pp = pp->next, pp1 = pp1->next) {
      pp->tell = pp1->tell;
      pp->split = pp1->split;
    }
    free_pp(pp2);
  } else if (rowp) *rowp = n_row;
  return pp0;
}

void alloc_tar(u_char cp0[], PP pp0[], int size)/*usilic*/
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

void alloc_lha(u_char cp0[], PP pp0[], int size)/*usilic*/
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

void alloc_unzip(u_char cp0[], PP pp0[], int size)/*usilic*/
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

char **alloc_arg(char *arg, int *argcp)/*usilic*/
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
      save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
      return NULL;
    }
    argv[i] = NULL;
  } else {
    save_intr(S_errl);
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
  }
  return argv;
}

void free_arg(char **argv)/*usilic*/
{
  int i;

  for (i = 0; argv[i]; i++) free(argv[i]);
  free(argv);
}

u_char *alloc_split(char name[], int tell, int size)/*usilic*/
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

u_char *alloc_buff(int *sizep, u_int fd, int f_chkbin)/*usilic*/
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
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
  *sizep = size;
  return buff;
}

PP *alloc_sh(u_char *str, int pid0, int c)/*usilic*/
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
	       , s_S(S_usilic), &s_P(P_mode)[M_shell][1], s_S(S_errl));
  return (s_len > bsize) ? F4 : pp0;
}  

void make_bp(BASE *bp, char mess[], char *name, char *cp)/*usilic*/
{ /* alloc string for error-message */
  free_bp(bp);
  strcpy(Alloc.line, mess);
  if (name) { 
    if (cp) save_error("%s: %s: %s (%s).\n", s_S(S_usilic), name, Alloc.line, cp);
    else save_error("%s: %s: %s.\n", s_S(S_usilic), name, Alloc.line);
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

PP *set_pp(u_char *cs, int size, int *rowp, int f_code, int f_bin)/*usilic*/
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
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
  *rowp = n_row;
  return pp0;
}

PP **set_ppp(void *p, void *p0, int f_set)/*usilic*/
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

int alloc_winch(BASE *bp, int f_ppp)/*usilic*/
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
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
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

void free_bp(BASE *bp)/*usilic*/
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

FILE *seek_intr(FILE *pin, int *nrowp)/*usilic*/
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


#include "usilic.h"
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>

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
      /* argv[0] ==> chp->suffix, argv[1..] ==> chp->argv[0..] */
      p3 = NULL;
      if (f_child == 4 && (!n_str || n_str == 2)) { /* EDITOR (usilicrc) */
	if (!n_str)
	  p3 = strdup(s_P(P_mode)[M_edit]); /* mark of editor */
	else if (n_str == 2) /* line-No. for editor */
	  p3 = strdup("+??????????");
	i = strlen(p3);
      } else if (f_child == 5 && n_str != 1) { /* SHELL (usilicrc) */
	if (!n_str) 
	  p3 = strdup(s_P(P_mode)[M_shell]); /* mark for shell */
	else if (n_str == 2) p3 = strdup("-i");
	i = (n_str == 3) ? 0 : strlen(p3);
      } else {
	while (*cp == ' ') cp++;
	if (!n_str && !f_child) {
	  argv[0] = strdup("...");
	  continue;
	}
	if (*cp == '#') i = 0; /* comment line */
        else { /* string enclosed in '' or "" */
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
      if (!i) {	/* no more string */
	k = !strcmp(argv[0], s_P(P_mode)[M_shell]);
	chp->f.argc = n_str - 1; /* chp->argv[chp->f.argc] <== filename */
	chp->argv = calloc(sizeof(char*), chp->f.argc + 2 - k);
	if (is_fm() && !strcmp(s_S(S_du), argv[1])) 
	  ; /* need no filename */
	else if (chp2) chp->argv[chp->f.argc + 1] = (char*)chp2;
	else if (!k) /* area for argument */
	  chp->argv[chp->f.argc] = calloc(1, N_line);
	for (j = 0; j < chp->f.argc+1; j++) {
	  if (j) chp->argv[j-1] = strdup(argv[j]);
	  else chp->suffix = strdup(argv[j]);
	}
	if (!is_inst(s_S(S_path), *chp->argv)) {
	  /* chp->argv[0] isn't installed */
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
	    chp->f.exec = 1; /* EXEC (usilicrc) */
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

CHILD *check_ext(char *name, u_char *split, int size)/*usilic*/
{ /* check suffix to set program-name */
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
  /* read `N_man' bytes to check format of manual-page */
    size0 = size;
    if (split) { /* read memory */
      st.st_size = -1;
      if (size > N_man) size = N_man;
    } else /* read file */
      if (stat(name, &st) < 0 || !st.st_size) split = NULL;
      else {
	size0 = size = N_man;
	split = calloc(1, size
			   +3); /* for man[3] (is_man()) */
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

CHILD *init_child(void)/*usilic*/
{ /* built-in program */
  int i;
  char **app;
  CHILD *chp;
  
  app = s_P(P_child);
  i = (is_fm()) ? 0 : S_egrep - S_ls;
  for (chp = NULL; app[i]; i++) chp = save_child(chp, app[i], 0, NULL);
  return chp;
}

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
  if (chp && name) /* set argument */
    strcpy(chp->argv[chp->f.argc], name);
  return chp;
}

int cat_child(CHILD *chp, char pathname[])/*usilic*/
{  /* set `filename (program)' to FILE-list */
  int i;

  if (!chp) return 0;
  i = strlen(pathname);
  if (pathname[0] == ' ' || pathname[i-1] == '/') return 0;
  strcat(pathname, " (");
  strcat(pathname, *chp->argv);
  strcat(pathname, ")");
  return i;
}

void free_chp(CHILD *chp)/*usilic*/
{
  int argc, f_pipe;

  f_pipe = !chp->argv[chp->f.argc] 
    && strcmp(chp->suffix, "...")
    && *chp->suffix != ' '; /* s_P(P_mode)[M_shell] */
  for (argc = 0; chp->argv[argc]; argc++)
    free(chp->argv[argc]);
  if (f_pipe) free_chp((CHILD*)chp->argv[argc + 1]);
  if (chp->suffix) free(chp->suffix);
  free(chp->argv);
  free(chp);
}
  
void free_child(CHILD *chp0)/*usilic*/
{
  CHILD *chp, *chb;

  for (chb = chp = chp0; chp; chp = chb) {
    chb = chp->next;
    free_chp(chp);
  }  
}

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

#include "usilic.h"
#include "code.P"
#include <signal.h>
#include <sys/wait.h>
typedef int (CONV) (int, int, u_char*, u_char[], JIS *);
typedef void (DECODE) (u_char*, u_char[], u_char[]);

int is_man(char *man)/*usilic*/
{ /*
   * \012.\\042
   * \012.B 
   * \012.I
   * \012.aa \012.ab ... \012.zy \012.zz
   * \012.Aa \012.Ab ... \012.Zy \012.Zz 
   * \012.AA \012.AB ... \012.ZY \012.ZZ
   */
  int i, c1, c2;

  if (man[0] == '.') {
    c1 = man[1];
    c2 = man[2];
    if (c1 == '\\' && c2 == '\042') return 1;
    if ((c1 == 'B' || c1 == 'I') && c2 == ' ') return 1;
    if ((i = c1 >= 'A' && c1 <= 'Z') || (c1 >= 'a' && c1 <= 'z'))
      if ((c2 >= 'a' && c2 <= 'z') || (i && c2 >= 'A' && c2 <= 'Z'))
	if (man[3] == ' ' || man[3] == '\n') return 1;
  }
  return 0;
}

char *type_pipe(char *buff, int size, int code)/*usilic*/
{ /* print file-type (from pipe) */
  CHILD *chp;
  int pv[4];
  char *cp, type[N_line];
  FILE *fp;
  
  if ((chp = check_chp(S_file))) {
    if (!(chp = get_child(NULL, "...", s_S(S_file), "-", NULL))) return NULL;
    pipe(pv);
    if (!fork()) {
      load_signal();
      pipe(&pv[2]);
      if (!fork()) {
	close(pv[2]);
	/* check N_pipe bytes */
	fp = fdopen(pv[3], "w");
	fwrite(buff, 1, (size > N_pipe) ? N_pipe : size, fp);
	fclose(fp);
	exit(0);
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
      execvp(*chp->argv, chp->argv);
      exit(0);
    }
    close(pv[1]);
    sprintf(type, "(^%c) ", '@'-code-1);
    fp = fdopen(pv[0], "r");
    cp = &type[5];
    fgets(cp, N_pipe, fp);
    fclose(fp);
    if ((cp = strchr(cp, '\n'))) *cp = '\0';
    for (cp = &(strchr(type,':'))[1]; *cp == ' ' || *cp == '\t'; cp++);
    strcpy(&type[5], cp);
  } else sprintf(type, "%s `%s\047", s_S(S_errc), s_S(S_file));
  cp = type;
  free(buff);
  buff = strdup((chp) ? tobo(cp, 1) : toul(cp, 1));
  return buff;
}

char *type_file(char *buff, char name[], int code)/*usilic*/
{ /* print file-type (from file) */
  CHILD *chp;
  int pv[2];
  char *cp, type[N_line];
  FILE *fp;

  if ((chp = check_chp(S_file))) {
    if (!(chp = get_child(NULL, "...", s_S(S_file), name, NULL))) return NULL;
    pipe(pv);
    if (!fork()) {
      load_signal();
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[1]);
      close(pv[0]);
      execvp(*chp->argv, chp->argv);
      exit(0);
    }
    close(pv[1]);
    sprintf(type, "(^%c) ", '@'-code-1);
    fp = fdopen(pv[0], "r");
    cp = &type[5];
    fgets(cp, N_pipe, fp);
    fclose(fp);
    if ((cp = strchr(cp, '\n'))) *cp = '\0';
    for (cp = &(strchr(type,':'))[1]; *cp == ' ' || *cp == '\t'; cp++);
    strcpy(&type[5], cp);
  } else sprintf(type, "%s `%s\047", s_S(S_errc), s_S(S_file));
  cp = type;
  if (buff) free(buff);
  save_error("%s: %s: %s.\n", s_S(S_file), name, cp);
  buff = strdup((chp) ? tobo(cp, 1) : toul(cp, 1));
  return buff;
}

u_char *check_type(u_char *buff, int *sizep, char *name, int *f_Mp, int f_chkbin, int *codep)/*usilic*/
{
  int size, oldsize, code;
  JIS jis;
  
  if (!f_chkbin) {
    *codep = C_bin;
    return buff;
  }
  oldsize = size = *sizep;
  switch((code = init_code(&size, buff, f_chkbin))) {
  case C_jis: case C_base64: case C_quote:
    jis.c = 0;
    do {
      if (code == C_base64) jis.f.e = 1;
      else if (code == C_quote) jis.f.q = 1;
      else continue;
      code = C_jis;
    } while (0);
    if (f_chkbin == 3) jis.f.i = 1;
    size = jistoeuc(size, buff, &jis);
    break;
  case C_sjis:
    size = sjistoeuc(size, &buff, oldsize, NULL);
    break;
  case C_euc_jp:
    size = euctoeuc(size, &buff, oldsize, NULL);
    break;
  default:
    if (code < 0 && f_chkbin == 1) {
      buff = (name) 
	? type_file(buff, name, code) : type_pipe(buff, size, code);
      if (buff) size = strlen(buff);
      *f_Mp = T_type;
    }
    break;
  }
  *codep = code;
  *sizep = size;
  return buff;
}

static int b_code(int c)
{
  if (c >= '[') c -= 'G';
  else if (c >= 'A') c -= 'A';
  else if (c >= '0') c -= ('0' - '4');
  else if (c == '+') c = '>';
  else c = '?';
  return c & 0x3f;
}

static int q_code(int c)
{
  if (c >= '0' && c <= '9') c -= '0';
  else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
  else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
  else c = 0;
  return c;
}

static void decode_base64(u_char *code, u_char mime[], u_char end[])/*code*/
{
  u_char *mp;
  int i, ma[4];

  for (mp = mime; mp < end; ) {
    for (i = 0; i < 4; i++) ma[i] = b_code(*mp++);
    *code++ = ((ma[0] << 2) & 0xfc) | ((ma[1] >> 4) & 0x03);
    *code++ = ((ma[1] << 4) & 0xf0) | ((ma[2] >> 2) & 0x0f);
    *code++ = ((ma[2] << 6) & 0xc0) | (ma[3] & 0x3f);
  }
  *code = 0;
}

static void decode_quote(u_char *code, u_char mime[], u_char end[])/*code*/
{
  u_char *mp;

  for (mp = mime; mp < end; mp++)
    if (mp[0] == '_') *code++ = ' ';
    else if (mp[0] != '=') *code++ = mp[0];
    else {
      mp++;
      if (mp[0] <= ' ') *code++ = mp[0];
      else {
	*code++ = (q_code(mp[0]) << 4) + q_code(mp[1]);
	mp++;
      }
    }
  *code = 0;
}

static int set_func(u_char *cp, CONV **convp, DECODE **decodep)
{
  static CONV *conv[] = {
    conv_jis,
    conv_q,
    conv_sjis,
    conv_q,
    conv_euc,
    NULL,
  };
  static DECODE *decode[] = {
    decode_base64,
    decode_quote,
    decode_base64,
    decode_quote,
    decode_base64,
    NULL,
  };
  char **s_pp;
  int i, len;
  
  for (i = 0, s_pp = &s_P(P_code)[C_base64 - C_ascii]; s_pp[i]; i++)
    if (!strncasecmp(s_pp[i], cp, strlen(s_pp[i]))) break;
  if (s_pp[i]) {
    len = strlen(s_pp[i]);
    *convp = conv[i];
    *decodep = decode[i];
  } else {
    len = 0;
    *convp = NULL;
    *decodep = NULL;
  }
  return len;
}

static int conv_q(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy)/*code*/
{
  u_char *p = cp;
  int i;
  JIS jis;

  i = len;
  switch (init_code(&len, p, 0)) {
  case C_jis:
    p = malloc(len);
    jis.c = 0;
    len = conv_jis(0, len, cp, p, &jis);
    break;
  case C_sjis:
    p = malloc(len);
    len = conv_sjis(0, i, cp, p, NULL);
    break;
  default:
    break;
  }
  if (buff_i) buff_i--;
  for (i = 0; len > 0; len--, i++) euc[buff_i++] = p[i];
  if (p != cp) free(p);
  return buff_i;
}

static int conv_jis(int buff_i, int len, u_char *cp, u_char euc[], JIS *jisp)/*code*/
{
  static int jis_i;
  int  c, f_mime, c0, c1, c2;
  u_char *cp0 = cp;

  if ((f_mime = buff_i < 0)) {
    jis_i = len; 
    buff_i = 0;
  }
  for (c0 = c1 = c2 = 0; len > 0 && buff_i < jis_i; c0 = 0) {
    switch(c = *cp) {
    case K_j:
      if (jisp->f.s) {
	jisp->f.s = 0;
	break;
      }
    case K_i:
    case K_h:
    case K_n:
    case K_o:
      break;
    case K_ec:
      cp++;
      jisp->f.s++;
      len--;
      break;
    case '=':
      if (cp[1] == '?' && f_mime
	  && decode_mime(&cp, &buff_i, &len, euc, cp0 + jis_i - 1))
	continue;
      else if ((q_code(cp[1]) << 4) + q_code(cp[2]) == K_ec) {
	cp += 3;
	jisp->f.s++;
	len -= 3;
	break;
      }
    default:
      if (c < 0x20) {
	if (jisp->f.s == 3) buff_i--;
	cp++;
	len--;
	continue;
      }
      break;
    }
    if ((c = *cp) == '=' && !jisp->f.e)
      if (jisp->f.q && !(c0 = q_code(cp[1]))) {
	cp += 2;
	len -= 2;
	if (jisp->f.i) {
	  if (jisp->f.s) jisp->f.s = 0;
	  cp--;
	  len++;
	} else if (jisp->f.s 
		   && jisp->f.s != 3
		   && *cp == '=' 
		   && (q_code(cp[1]) << 4) + q_code(cp[2]) == K_ec)
	  jisp->f.s = 0;
	continue;
      }
    switch(jisp->f.s) {
    case 0:
      if (c0) {
	if ((c = (c0 << 4) + q_code(cp[2])) == '=') ;
	else if (c == 0xa0) c &= ~0x80;
	euc[buff_i++] = c;
	cp += 3;
	len -= 3;
      } else {
	if (c == K_n) {
	  jisp->f.s = 6;
	  cp++;
	  len--;
	  continue;
	} else euc[buff_i++] = c;
	cp++;
      }
      break;
    case 1:
    case 8:
      if (c == '$') jisp->f.s = 2;
      else if (c == '(') jisp->f.s = 5;
      else if (jisp->f.s == 1) {
	len += jisp->f.s;
	jisp->f.s = 0;
	euc[buff_i++] = K_ec;
	if (buff_i < jis_i) euc[buff_i++] = c;
      }
      cp++;
      break;
    case 2:
      if (c == 'B' || c == '@') jisp->f.s = 3;
      if (cp[1] == K_ec) jisp->f.s = 0;
      cp++;
      break;
    case 3:
      if (c0) {
	if ((c = (c0 << 4) + q_code(cp[2])) == '=') {
	  c |= 0x80;
	  euc[buff_i++] = c;
	} else if (c2) {
	  euc[buff_i++] = ' ';
	  euc[buff_i++] = ' ';
	  c1 = c2 = 0;
	} else if (c >= 0x80) c2 = c;
	else euc[buff_i++] = c;
	cp += 2;
	len -= 2;
      } else if (c2) {
	euc[buff_i++] = ' ';
	c1 = c2 = 0;
      } else euc[buff_i++] = (c >= ' ') ? (c | 0x80) : c;
      cp++;
      break;
    case 4:
      if (c == '(') jisp->f.s = 5;
      cp++;
      break;
    case 5:
      if (c == 'B' || c == 'J') jisp->f.s = 0;
      else if (c == 'I') jisp->f.s = 7;
      cp++;
      break;
    case 6:
      if (c == K_o) {
	cp++;
	jisp->f.s = 0;
	len--;
	continue;
      }
    case 7:
      euc[buff_i++] = 0x8e;
      if (buff_i < jis_i) euc[buff_i++] = c | 0x80;
      cp++;
      break;
    }  
    len--;
  }
  return buff_i;
}

static int decode_mime(u_char **cpp, int *buff_ip, int *lenp, u_char euc[], u_char *end)/*code*/
{ 
  int i, len, buff_i, f_nl;
  u_char *code, *mime, *cp_b, *cp = *cpp;
  CONV *conv;
  DECODE *decode;
  JIS jis;

  buff_i = *buff_ip;
  len = *lenp;
  jis.c = 0;
  if ((i = set_func(cp, &conv, &decode))) {
    mime = &cp[i];
    if ((f_nl = (cp_b = strchr(mime, '\n')) != NULL)) {
      for (cp = mime; cp < cp_b; cp++) if (!strncmp(cp, "?=", 2)) break;
      if (cp < cp_b) cp_b = cp;
    } else cp = cp_b = end;
    code = malloc(cp_b - mime + 1);
    decode(code, mime, cp_b);
    if (*cp != '\n') cp_b += 2;
    len -= i + cp_b - mime;
    buff_i = conv(buff_i, strlen(code), code, euc, &jis);
    free(code);
    *cpp = cp_b;
    *buff_ip = buff_i;
    *lenp = len;
  }
  return i;
}

static int conv_sjis(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy)/*code*/
{
  static int euc_e;
  int c1, c2, sjis_i, f_mime, f_len = 0, f_quit = 0;
  u_char *cp0 = cp;

  if ((f_mime = buff_i < 0)) {
    euc_e = -buff_i;
    buff_i = 0;
  } else if ((f_quit = len < 0)) {
    euc_e = len = -len;
    buff_i = 0;
  } else f_len = buff_i;
  for (sjis_i = len; len > 0; len--) { 
    if (!f_len && buff_i >= euc_e) break;
    c1 = *cp;
    if (f_quit && !c1) break;
    if (is_sjis_1_byte(c1)) {
      c2 = cp[1];
      c1 = (c1 << 1) -((c1 > 0x9f) ? 0x161 : 0xe1);
      if (c2 < 0x9f) c2 = c2 - ((c2 > 0x7f) ? 0x20 : 0x1f);
      else {
	c2 = c2 - 0x7e;
	c1++;
      }
      euc[buff_i++] = c1 | 0x80;
      if (f_len || buff_i < euc_e) euc[buff_i++] = c2 | 0x80;
      cp += 2;
      len--;
    } else if (is_sjiskana(c1)) {
      euc[buff_i++] = 0x8e;
      if (f_len || buff_i < euc_e) euc[buff_i++] = c1;
      cp++;
    } else if (c1 == '=' && f_mime) {
      if (cp[1] == '?' 
	  && decode_mime(&cp, &buff_i, &len, euc, cp0 + sjis_i - 1))
	continue;
      euc[buff_i++] = c1;  
      cp++;
    } else {
      if (c1 < ' ' && !strchr(s_S(S_code), c1)) ;
      else euc[buff_i++] = c1;  
      cp++;
    }
  }
  if (f_quit) euc[buff_i] = '\0';
  return buff_i;
}

static int conv_euc(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy)/*code*/
{
  static int euc_e;
  int c, euc_i, f_mime, f_len = 0, f_quit = 0;
  u_char *cp0 = cp;

  if ((f_mime = buff_i < 0)) {
    euc_e = -buff_i;
    buff_i = 0;
  } else if ((f_quit = len < 0)) {
    euc_e = len = -len;
    buff_i = 0;
  } else f_len = buff_i;
  for (euc_i = len; len > 0; len--) { 
    if (!f_len && buff_i >= euc_e) break;
    c = *cp;
    if (f_quit && !c) break;
    if (c == '=' && f_mime) {
      if (cp[1] == '?' 
	  && decode_mime(&cp, &buff_i, &len, euc, cp0 + euc_i - 1))
	continue;
      euc[buff_i++] = c;  
      cp++;
    } else {
      if (c < ' ' && !strchr(s_S(S_code), c)) ;
      else euc[buff_i++] = c;  
      cp++;
    }
  }
  if (f_quit) euc[buff_i] = '\0';
  return buff_i;
}

int jistoeuc(int buff_i, u_char jis[], JIS *jisp)/*usilic*/
{ /* convert jis to euc */
  buff_i = conv_jis(-1, buff_i, jis, jis, jisp);
  return buff_i;
}

int sjistoeuc(int buff_i, u_char **sjisp, int sjis_i, JIS *dummy)/*usilic*/
{ /* convert sjis to euc */
  u_char *euc;

  if ((euc = malloc(buff_i + 1))) {
    buff_i = conv_sjis(-buff_i, sjis_i, *sjisp, euc, NULL);
    free(*sjisp);
    *sjisp = euc;
  } else {
    save_intr(S_errl);
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
  }
  return buff_i;
}

int euctoeuc(int buff_i, u_char **eucp, int euc_i, JIS *dummy)/*usilic*/
{ /* convert sjis to euc without allocation of memory */
  u_char *euc;

  if ((euc = malloc(buff_i + 1))) {
    buff_i = conv_euc(-buff_i, euc_i, *eucp, euc, NULL);
    free(*eucp);
    *eucp = euc;
  } else {
    save_intr(S_errl);
    save_error("%s: %s.\n", s_S(S_usilic), s_S(S_errl));
  }
  return buff_i;
}

int count_k(u_char *cp, int ci)/*usilic*/
{
  int i, f_kanji;
 
  for (i = f_kanji = 0; i < ci; i++)
    f_kanji = (*cp++ >= 0x80) ? !f_kanji : 0;
  return f_kanji;
}

static int count_h(u_char *cp, int ci)
{
  int i, j, h, col;

  for (col = i = 0; i < ci; i++)
    if (!(h = *cp++)) {
      col += N_line;
      break;
    } else if (h == '\t') {
      for (j = 8, h = col % 8; h; h--) j--;
      col += j;
    } else col++;
  return col;
}

static int count_i(u_char *cp, int col)
{
  int ci;

  for (ci = 0; count_h(cp, ci) < col; ci++) ;
  return ci;
}

int init_code(int *sizep, u_char *buff, int f_chkbin)/*usilic*/
{
  int i, code, size, c1, c2, c3, c4, c5, kn[3];
  int f_bin; /* for an unexpected control-character in JIS-code */
  u_char *end;

  kn[0] = kn[1] = kn[2] = 0;
  c1 = c4 = c5 = f_bin = 0;
  size = *sizep;
  code = C_ascii;
  end = buff + size;
  for (; buff < end; buff++) {
    if (!c1 && (c1 = buff[0]) == '\n') {
      c1 = 0;
      continue;
    }
    if (!f_bin && *s_S(S_code) && (f_chkbin == 1 || f_chkbin == 3)
	&& (!c1 || (c1 < ' ' && !strchr(s_S(S_code), c1)))) f_bin = -(c1+1);
    else if (c1 == K_h && buff < end - 1)
      switch(buff[1]) {
      case 0:
	f_bin = -1;
	break;
      case '\n':
      case '\r':
	if (buff[-1] == ' ') break;
	f_bin = -(K_h+1);
	break;
      case K_h:
	if (buff < end - 2 && buff[2] == K_h) f_bin = -(K_h+1);
	break;
      }
    if (vt_charset(0) == C_ascii && c5) {
      c1 = 0;
      continue;
    }
    if (is_sjiskana(c1)) kn[2]++; /* count sjis-kana */
    if (buff == end - 1) break;
    c2 = buff[1];
    if (is_sjis_1_byte(c1) && is_sjis_2_byte(c2)) {
      if (f_bin) return f_bin;
      kn[0]++;
      buff++;
      i++;
    } else 
      if ((is_euckana_1_byte(c1) && is_euckana_2_byte(c2)) ||
	  (is_euc_1_byte(c1) && is_euc_2_byte(c2))) {
	if (f_bin) return f_bin;
	kn[1]++;
	buff++;
	i++;
	if (is_sjiskana(c2)) kn[2]++; /* sjis-kana sequence ? */
      } else if (c1 == K_ec)
	if (buff >= end - 2) break;
	else {
	  c3 = buff[2];
	  if (c2 == '$' && (c3 == 'B' || c3 == '@')) c5 = C_jis;
	  else if (c2 == '(' && (c3 == 'B' || c3 == 'J') && c5 == C_jis) 
	    return C_jis;
	  else if (c5) c5 = 0;
	}
      else if (!kn[0] && !kn[1]) {
	if (!strncmp(buff, "=?", 2) 
	    && buff < end - strlen(s_P(P_code)[C_base64 - C_ascii])) {
	  if (!strncasecmp(buff, s_P(P_code)[C_base64 - C_ascii]
			   , strlen(s_P(P_code)[C_base64 - C_ascii]))) 
	    c5 = C_base64;
	  else if (!strncasecmp(buff, s_P(P_code)[C_quote - C_ascii]
				, strlen(s_P(P_code)[C_quote - C_ascii]))) 
	    c5 = C_quote;
	  else if (!strncasecmp(buff, s_P(P_code)[C_shift_jis - C_ascii]
				, strlen(s_P(P_code)[C_shift_jis - C_ascii]))) 
	    c5 = C_sjis;
	  else if (!strncasecmp(buff, s_P(P_code)[C_euc_jp - C_ascii]
				, strlen(s_P(P_code)[C_euc_jp - C_ascii]))) 
	    c5 = C_euc_jp;
	} else if (c1 == '=' && buff < end - 3
		   && ((q_code(buff[1]) << 4) + q_code(buff[2])) == K_ec
		   && (buff[3] == '$' || buff[3] == '(')) 
	  c5 = C_quote;
      } else if (!f_bin) {
	if (c1 == K_n) c4 = C_jis;
	else if (c1 == K_o && c4 == C_jis) return C_jis;
      }
    c1 = 0;
  }
  if (c5) return (vt_charset(0) == C_ascii) ? C_ascii : c5;
  if (f_bin) return f_bin;
  if (vt_charset(0) == C_ascii || (!kn[0] && !kn[1])) code = C_ascii;
  else if (kn[0] >= kn[1]) {
    code = C_sjis;
    size += kn[2]; /* kn[2]: euc-kana */
  } else code = C_euc;
  *sizep = size;
  return code;
}

void make_map(RV *rvp, int nlen)/*usilic*/
{
  static int key_copy[] = {
    M_up, M_dn, M_mark, M_kprev, M_knext, 
    M_kup, M_kdn, M_head, M_tail, M_clear, 
    M_forw, M_back, M_kforw, M_kback, M_prev, 
    M_next, 0
  };
  int c, i, j, h, m, f_copy, f_kanji, f_sign;
  int row, rowe, col, col0, ci, c0;
  PP *pp = rvp->ps;
  u_char *p, *p1, *cp, **cpp;

  cpp = malloc(sizeof(u_char*) * (vt_row(0) - 1));
  for (i = 0; i < vt_row(0) - 1; i++) 
    cpp[i] = calloc(1, vt_col(0) + 1);
  for (j = 0; j < vt_row(0) - 1; j++) {
    if (!pp) break;
    cp = pp->cs;
    p = cpp[j];
    for (i = h = 0, c = 1; c && i < vt_col(0); ) {
      if (cp > pp->cs + pp->f.len) c = 0;
      else 
	switch((c = *cp)) {
	case 0:
	  break;
	case '\n':
	case '\r':
	  c = ' ';
	  break;
	case '_':
	  if (cp[1] == K_h) {
	    cp += 2;
	    continue;
	  }
	  break;
	case K_h:
	  if (cp[1] == K_h) cp += 4;
	  else if (cp[1] >= 0x80) cp += 3;
	  else cp += 2;
	  continue;
	default:
	  break;
	}
      *p++ = c;
      i++;
      cp++;
      if (cp >= pp->cs + pp->f.len) *p = '\0';
    }
    if (count_k(cpp[j], vt_col(0))) cpp[j][vt_col(0)-1] = '\0';
    if (pp) pp = pp->next;
  }
  disp_copy(cpp, nlen);
  rowe = j - 1;
  row = rvp->row;
  vt_move(row, 0);
  col = ci = 0;
  col0 = c0 = 0;
  f_copy = f_kanji = c = h = 0;
  f_sign = 1;
  p = calloc(1, vt_col(0) + 1);
  p1 = malloc(vt_col(0) + 1);
  while (1) {
    m = read_m(F11, M_copy, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (m < N_char && !strint(key_copy, m)) break;
    switch(m) {
    case 0:
      continue;
    case M_clear:
      disp_copy(cpp, nlen);
      vt_move(row, col);
      break;
    case M_head: case M_back: case M_kback: case M_prev:
      row = 0;
      col = 0;
      vt_move(row, col);
      ci = 0;
      break;
    case M_tail: case M_forw: case M_kforw: case M_next:
      row = rowe;
      col = strlen(cpp[row]) - 1;
      vt_move(row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci++;
	else ci--;
	col = count_h(cpp[row], ci);
      }
      break;
    case M_up: /* ^P */
    case M_kup: /* up-arrow */
      if (!f_copy && row) vt_move(--row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci += f_sign;
	else ci--;
	col = count_h(cpp[row], ci);
	f_sign = -f_sign;
      }
      break;
    case M_dn: /* ^N */
    case M_kdn: /* down-arrow */
      if (!f_copy && row < rowe) vt_move(++row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci += f_sign;
	else ci--;
	col = count_h(cpp[row], ci);
	f_sign = -f_sign;
      }
      break;
    case K_a + N_char: /* ^A */
      vt_endstand();
      vt_move(row, 0);
      vt_printf("%s", cpp[row]);
      ci = col = 0;
      break;
    case K_e + N_char: /* ^E */
      vt_endstand();
      vt_move(row, 0);
      vt_printf("%s", cpp[row]);
      col = strlen(cpp[row]);
      vt_move(row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci++;
	else ci--;
	col = count_h(cpp[row], ci);
      }
      break;
    case K_b + N_char: /* ^B */
    case M_kprev: /* left-arrow */
      if (col) {
	if (ci && !(col % 8) && cpp[row][ci-1] == '\t') {
	  ci--;
	  col = count_h(cpp[row], ci);
	  vt_move(row, col);
	} else {
	  vt_move(row, --col);
	  ci--;
	  if (count_k(cpp[row], col)) {
	    col--;
	    ci--;
	  }
	}
      }
      break;
    case K_f + N_char: /* ^F */
    case M_knext: /* right-arrow */
      if (cpp[row][ci] == '\t') {
	for (i = 8, h = col % 8; h; h--) i--;
	col += i;
	vt_move(row, col);
	ci++;
      } else if (cpp[row][ci] && col < vt_col(0)) {
	if (col > vt_col(0) - 1) vt_move(row, vt_col(0) - 1);
	else vt_move(row, col + 1);
	col++;
	ci++;
	if (count_k(cpp[row], ci)) {
	  col++;
	  ci++;
	}
      }
      break;
    case M_mark:
      if ((f_copy = !f_copy)) vt_move(row, col); 
      else {
	vt_endstand();
	if ((c = (col > col0) ? col0 : col) > vt_col(0) - 1) 
	  vt_move(row, vt_col(0) - 1);
	else vt_move(row, c);
	vt_printf("%s", p);
	if (c == col) 
	  if (c > vt_col(0) - 1) vt_move(row, vt_col(0) - 1);
	  else vt_move(row, c);
	else vt_flush();
	*p = '\0';
      }
      col0 = col;
      c0 = ci;
      continue;
      break;
    default:
      break;
    }
    if (f_copy) {
      if (ci > c0) {
	memcpy(p, &cpp[row][c0], ci - c0);
	p[ci - c0] = '\0';
	strcpy(p1, &cpp[row][ci]);
	vt_move(row, col0);
	vt_stand();
	vt_printf("%s", p);
	vt_endstand();
	vt_printf("%s", p1);
      } else {
	memcpy(p1, cpp[row], ci);
	p1[ci] = '\0';
	i = count_k(cpp[row], c0 + 1);
	memcpy(p, &cpp[row][ci], c0 - ci + 1 + i);
	p[c0 - ci + 1 + i] = '\0';
	vt_move(row, 0);
	vt_endstand();
	vt_printf("%s", p1);
	vt_stand();
	vt_printf("%s", p);
      }
    }
    if (col > vt_col(0) - 1) vt_move(row, vt_col(0) - 1);
    else vt_move(row, col);
    vt_flush();
  }
  if (*p) strcpy(rvp->copy, p);
  for (i = 0; i < vt_row(0) - 1; i++) free(cpp[i]);
  free(cpp);
  strcpy(p, mtoa(M_bl));
  if (m != M_bl) strcat(p, mtoa(m));
  strcat(p, mtoa(M_clear));
  read_m(NULL, 2, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
  free(p);
  free(p1);
}

int toeuc(u_char sjis[])/*usilic*/
{ /* convert sjis to euc without allocation of memory */
  int buff_i;
  u_char *euc, *p;

  if (!(buff_i = strlen(sjis) * 2)) return 0;
  p = strchr(sjis, '\n');
  euc = alloca(buff_i);
  buff_i = conv_sjis(0, -buff_i, sjis, euc, NULL);
  strcpy(sjis, euc);
  if (p && !strchr(sjis, '\n')) {
    strcat(sjis, "\n"); 
    buff_i++;
  }
  return buff_i;
}

#include "usilic.h"
static void tabtospc(char str[], char tmp[])
{ /* skip underline or bold attribute */
  /* convert '\t' to ' ' */
  int c, i = 0, j = 0;

  do {
    if ((c = tmp[i]) == '_') {
      if (tmp[i+1] == K_h) {
	i += 2;
	continue;
      } else if (tmp[i+1] == '_' && tmp[i+2] == K_h && tmp[i+3] == K_h) {
	i += 4;
	continue;
      }
    } else if (c == K_h) {
      if (tmp[i+1] != K_h) {
	i += 2;
	continue;
      } else {
	i += 4;
	continue;
      }
    }
    str[j++] = (c == '\t') ? ' ' : c;
    i++;
  } while (c && j < 256);
  if (j == 256) str[255] = '\0';
}

void put_str(PP *pp, int f_mark)/*usilic*/
{
  int c2, c3;
  int i, n, i_attr, n_attr, f_reset, f_code, f_ctrl, f_grep;
  static int attr;
  ATTR *ap;

  if (!pp || pp->cs == F3) /* virtual line */
    return;
  if ((f_code = pp->f.code) || !pp->attr) {
    ap = NULL;
    i_attr = -1;
  } else {
    ap = pp->attr;
    i_attr = ap[0].n;
  }
  f_grep = pp->split == -T_grep;
  for (n = n_attr = c3 = f_ctrl = i = 0; n < pp->f.len; n++, i++) {
    if (f_mark && !n)
      if (pp->split == -f_mark) vt_stand();
      else vt_endstand();
    else if (ap && i_attr >= 0 && i_attr == n) {
      if ((attr = ap[n_attr++].attr) == '^' || attr == -'^') {
	if (attr > 0) vt_stand();
	else vt_endstand();
	vt_printf("^%c", ap[n_attr++].attr);
	if (attr > 0) vt_endstand();
	else vt_stand();
	i_attr = ap[n_attr].n;
	i++;
	continue;
      } else if (attr == '<' || attr == -'<') {
	if (attr > 0) vt_stand();
	else vt_endstand();
	vt_printf("<%2X>", ap[n_attr++].attr);
	if (attr > 0) vt_endstand();
	else vt_stand();
	i_attr = ap[n_attr].n;
	i += 3;
	continue;
      } else {
	if ((f_reset = attr < 0)) attr = -attr;
	if (attr == A_md3) vt_putchar(K_h);
	vt_seta(attr);
	i_attr = ap[n_attr].n;
	if (f_reset) {
	  n--;
	  i--;
	  continue;
	}
      }
    }
    c2 = pp->cs[n];
    if (f_code) {
      if (c2 == K_dl) c2 -= 0x80;
      if ((c3 = c2 < ' ')) {
	vt_seta(A_cs);
	c2 += '@';
      } else if ((c3 = c2 >= 0x80)) {
	vt_seta(A_cs);
	c2 = '*';
      }
    } else if (c2 == K_a0) {
      f_ctrl = 1;
      if (f_grep) vt_endstand();
      else vt_stand();
      vt_putchar('^');
      continue;
    } else if (c2 < ' ' && c2 != K_ec && c2 != K_i && c2 != K_h) continue;
    do {
      if (c2 == K_h) { 
	i -= 2;
	if (i == vt_col(0) - 2) {
	  vt_rc(); /* `backspace' at the right end of the window */
	  continue;
	}
      } else if (i == vt_col(0) - 1 && pp->cs[n+1] == K_h) vt_sc();
      vt_putchar(c2);
    } while (0);
    if (c3) {
      c3 = 0;
      vt_seta(A_ce);
    }
    if (f_ctrl) {
      f_ctrl = 0;
      if (f_grep) vt_stand();
      else vt_endstand();
    }
  }
  if (ap && i_attr >= 0 && i_attr == n) 
    vt_seta(((attr = ap[n_attr].attr) < 0) ? -attr : attr);
  if (f_mark) vt_stand();
  else vt_endstand();
}

PP *disp_pp(PP *pp, int f_mark)/*usilic*/
{ /* print line */
  int i, f_pp, f_rc = 0;
  
  if ((f_pp = (int)pp)) i = 0;
  else { /* no line */
    vt_move(0, 0);
    vt_eeol();
    i = 1;
  }
  for (; i < vt_row(0) - 1; i++) {
    vt_endstand();
    vt_move(i, 0);
    vt_eeol();
    if (f_pp) put_str(pp, f_mark);
    if ((f_pp = pp && pp->next)) pp = pp->next;
    else if (!f_rc) {
      vt_sc();
      f_rc = 1;
    }
  }
  if (f_rc) vt_rc();
  return pp;
}

int disp_window(PP *ps0, PP *pp0, int f_M0, char name0[], u_char prev0[], char *argp, int *lenp0)/*usilic*/
{
  int f_mark, f_arc, i, j, n, len, llen;
  u_char line[30];
  PP p = {0,};
  static int f_M;
  static PP *ps, *pp;
  static char name[N_char];
  static u_char prev[N_char], *arg;
  static int vlen, k;
  static int nlen, f_enter, *lenp;
  static char *copy[3];

  if (!vlen) vlen = strlen(getenv("USILIC")) + 2;
  switch(f_M0) {
  case -1:
    return strlen(name);
    break;
  case -2:
    if (*copy) for (i = 0; i < 3; i++) free(copy[i]);
    return 0;
    break;
  default:
    ps = ps0;
    pp = pp0;
    f_M = f_M0;
    lenp = lenp0;
    break;
  }
  if (f_M == T_shell2) {
    f_M = M_shell;
    vt_mode(-'d');
  }
  if (argp) {
    arg = argp;
    i = strlen(name0) - 1;
    /* mode-line [1] (access-name) */
    if ((j = *name0 == '/'))
      if (*prev0 == K_y) strcpy(name, name0); /* pager */
      else if (name0[i] == '/')
	if (*prev0 == K_z) strcpy(name, name0); /* file-selector */
	else strcpy(name, ".");
      else {
	while (i && name0[i] != '/') i--;
	strcpy(name, &name0[i+1]);
      }
    else if (i) 
      /* name[0] == ' ': mode-name */
      strcpy(name, (*name0 == ' ') ? &name0[1] : name0);
    else *name = '\0';
    k = vt_col(0) / 4 - 1;
    switch(f_M) {
    case M_man:
      k = vt_col(0) / 2 - 1;
    case M_prog:
    case M_egrep: case M_cut:
    case M_lfile: case M_ldir:
    case M_hist: case T_hist:
    case T_hfile: case T_hdir: case T_hman: case T_hlocate:
    case T_hfgrep: case T_hegrep:
    case M_cmd: case T_cmd:
    case M_list: case T_list:
    case T_locate:
      /* enter string at mode-line */
      f_enter = 1;
      break;
    case M_man2:
    case M_cut2:
    case M_fgrep:
    case M_locate:
      k = vt_col(0) / 2 - 1;
    case M_prog2:
    case M_lfile2:
    case M_ldir2:
      /* enter string at mode-line */
      f_enter = 2;
      break;
    default:
      k = vlen;
      f_enter = 0;
      break;
    }
  }
  llen = 3;
  switch(f_M) {
  case M_exit:
    sprintf(line, "%s (y/n) ?  ", s_S(S_errg));
    break;
  case M_copy:
    strcpy(line, "---");
    llen += strlen(line);
    break;
  case T_shell:
    llen += 17; /* "000000/000000 END" */
    break;
  default:
    llen += print_line(ps, pp
		       , (f_M == M_shell || f_M == T_pipe2 
			  || f_M == T_man || f_M == T_pipe 
			  || f_M == T_pipe4 || f_M == T_pipe5
			  || f_M == T_pipe3) 
		       ? -1 : 1, line);
    break;
  }
  if (argp) {
    if (f_M == M_exit) j = strlen(line);
    else j = (*arg == K_z && !arg[1]) ? 0 : strlen(arg);
    nlen = strlen(name);
    /* previous-mode (mode-line [6]) */
    tabtospc(prev, prev0);
    if ((len = (f_enter) ? k : vt_col(0) - (nlen + llen + j) + 1) < k) 
      j = k - 1;
    else j = len;
    i = k;
    if (*prev != K_z && *prev != K_y) {
      if (strlen(prev) > j) {
	n = strlen(prev) - j + 3;
	if (count_k(prev, n)) sprintf(prev, "... %s", &prev[n + 1]);
	else sprintf(prev, "...%s", &prev[n]);
      } else i = strlen(prev) + 1;
    }
    if (len < i - 1) {
      k = (*arg == K_z && !arg[1]) ? 0 : strlen(arg);
      j = vt_col(0) - (llen + k + i) + 4;
      n = nlen - j + 5;
      if (count_k(name, n)) sprintf(name, "... %s", &name[n + 1]);
      else sprintf(name, "...%s", &name[n]);
      nlen = strlen(name);
      k = i;
    }
  }
  vt_move(vt_row(0) - 1, 0);
  vt_stand();
  p.cs = name;
  p.f.len = strlen(p.cs);
  put_str(&p, 0);
  vt_endstand();
  vt_putchar(' ');
  if (f_M == M_copy) len = llen + 2;
  else if (f_enter == 2) len = *lenp = 0;
  else len = (f_M == T_shell) ? *lenp : print_line(ps, F2, nlen, line) + 1;
  if (len) {
    vt_move(vt_row(0) - 1, len);
    vt_putchar(' ');
  }
  if (f_M != M_exit && f_enter != 2 && arg[1]) {
    vt_stand();
    vt_printf("%s", &arg[1]);
    vt_endstand();
    vt_putchar(' ');
    len += strlen(&arg[1]) + 1;
  }
  f_mark = fmark(f_M);
  f_arc = f_M == T_tar || f_M == T_lha || f_M == T_unzip 
    || f_M == T_rpm || f_M == T_deb;
  if (f_enter) {
    *lenp = (f_enter == 1) ? len + 1 : nlen + 1;
    len = vt_col(0) - (k + 1);
    vt_eeol();
  }
  if (*prev != K_z && *prev != K_y) {
    char *cp, *cp2;
    vt_move(vt_row(0) - 1, len + 1);
    while ((cp = strchr(prev, '\033')))
      if ((cp2 = strchr(cp, 'm'))) {
	strcpy(cp, &cp2[1]);
      } else break;
    if (argp) {
      for (i = strlen(prev), j = vt_col(0) - (len + i + 1); j > 0; i++, j--)
	prev[i] = ' ';
      prev[i] = '\0';
    }
    if (f_M == M_copy) {
      if (!*copy) {
	copy[0] = strdup(name);
	copy[1] = strdup(line);
      } else free(copy[2]);
      copy[2] = strdup(prev);
      disp_copy(copy, 0);
    } else {
      vt_stand();
      p.cs = prev;
      p.f.len = strlen(p.cs);
      /* for prev[] contains underline or bold attribute */
      put_str(&p, 0);
    }
    if (f_mark || f_arc) *lenp = len + 1; 
  } else {
    vt_eeol();
    vt_move(vt_row(0) - 1, vt_col(0) - vlen);
    vt_printf(" %s%s%s ", vt_geta(A_md), getenv("USILIC"), vt_geta(A_me));
  }
  vt_endstand();
  if (!f_mark && !f_arc && !f_enter && f_M != T_shell && lenp) *lenp = 0;
  if (f_M == T_arc3 || f_M == M_exit) f_mark = 0;
  if (f_M != M_copy && (!ps0 || (argp && vt_mode('D')))) disp_pp(ps, f_mark);
  return (f_M == M_shell) ? len - strlen(arg) : nlen;
}

int print_line(void *ps0, PP *pp, int len, u_char *linep)/*usilic*/
{ /* print current-line and total-lines at mode-line */
  int c, i, j, k, row, e_row, n_row, f_code;
  u_char str[10], line[32], tmp[32], num[8];
  static int tlen, tcol;
  static FILE **pinp;
  PP *ps;

  if (pp == F2 && !linep) {
    pinp = ps0;
    return 0;
  }
  if (ps0 == F2) {
    vt_sc();
    vt_endstand();
    vt_move(vt_row(0) - 1, tcol + 1);
    vt_printf("Mark se");
    if ((i = tlen - 8) > 0) for (vt_putchar('t'); i > 0; i--) vt_putchar(' ');
    vt_endstand();
    vt_rc();
    return 0;
  }
  if (linep == F2) {
    vt_sc();
    vt_endstand();
    if (len < 0) {
      len = -len;
      j = strlen(s_P(P_mode)[M_shell]) - 1;
      k = 16;
    } else {
      j = tcol;
      k = tlen;
    }
    vt_move(vt_row(0) - 1, j + 1);
    vt_putchar(len);
    num[0] = len;
    if ((i = k - 1) > 0) for (vt_putchar(' '); i > 0; i--) vt_putchar(' ');
    i = 1;
    vt_move(vt_row(0) - 1, j + 1 + i);
    c = 0;
    do {
      if ((c = vt_getch()) != K_m && c != K_g && c != K_dl && c != K_h
	  && (c < '0' || c > '9')) continue;
      if (c >= '0' && c <= '9') {
	if (i >= 7) continue;
	num[i++] = c;
	vt_move(vt_row(0) - 1, j + i);
	vt_putchar(c);
	vt_flush();
      } else if (c == K_m) num[i] = '\0';
      else if (c == K_dl || c == K_h) {
	if (i) {
	  vt_move(vt_row(0) - 1, j + i);
	  vt_putchar(' ');
	  vt_flush();
	  vt_move(vt_row(0) - 1, j + i);
	  i--;
	} else c = K_g;
      }
    } while (c != K_m && c != K_g);
    i = (c == K_g) ? 0 : atoi(num);
    vt_rc();
    return i;
  }
  vt_seta(A_me);
  ps = ps0;
  *line = '\0';
  if (!*line && pp != F2) {
    f_code = pp && pp->f.code;
    if (!pp || (pp && pp->tell == (u_int)F7)) row = n_row = e_row = 0;
    else {
      row = pp->f.line;
      while (pp->next) pp = pp->next;
      n_row = pp->f.line;
      for (i = vt_row(0) - 2; i && ps->next; i--) ps = ps->next;
      e_row = ps->f.line;
      if (pinp) n_row++;
    }
    if (len <= 0) { 
      /* always set current-line and total-lines for six-digit number */
      for (j = 6, i = 0; i < j; i++) str[i] = '0';
      str[i] = '\0';
      strcpy(tmp, str);
      strcpy(line, utoa(row));
      for (i = strlen(line), k = j; i; i--, j--) str[j-1] = line[i-1];
      strcpy(line, utoa(n_row));
      for (i = strlen(line), j = k; i; i--, j--) tmp[j-1] = line[i-1];
      len = -len;
    } else {
      strcpy(tmp, utoa(n_row));
      for (j = strlen(tmp), i = 0; i < j; i++) str[i] = '0';
      str[i] = '\0';
      strcpy(line, utoa(row));
      for (i = strlen(line); i; i--, j--) str[j-1] = line[i-1];
    }
    if (f_code)
      if (e_row == n_row || !n_row)
	sprintf(line, "--- END");
      else
	sprintf(line, "--- %2d%%", e_row * 100 / n_row);
    else if (e_row == n_row || !n_row)
      sprintf(line, "%s/%s END", str, tmp);
    else
      sprintf(line, "%s/%s %2d%%", str, tmp, e_row * 100 / n_row);
  }
  if (!len) len = -1;
  if (pp != F2 && linep) {
    strcpy(linep, line);
    return strlen(line);
  }
  if (linep) strcpy(line, linep);
  if (!strncmp(line, s_S(S_errg), strlen(s_S(S_errg)))) vt_endstand();
  else vt_stand();
  vt_move(vt_row(0) - 1, len + 1);
  vt_printf("%s", line);
  vt_move(vt_row(0) - 2, 0);
  vt_endstand();
  tlen = strlen(line);
  tcol = len;
  print_intr(NULL, F8);
  return tlen + tcol;
}

void disp_prev(u_char *prev, void *dir, int klen0)/*usilic*/
{
  int i, j, k, col;
  u_char str[N_line];
  static PP p = {0,};
  static int klen, f_mark;
  static PP *ps;
 
  if (!prev) {
    ps = dir;
    f_mark = klen0;
    return;
  }
  if (klen0) {
    if (klen0 > 0) {
      klen = klen0;
      k = vt_col(0) / 4 - 1;
      col = vt_col(0) - k;
    } else {
      klen = -klen0;
      k = vt_col(0) - klen;
      col = klen;
    }
  } else {
    k = vt_col(0) - klen;
    col = klen;
  }
  strcpy(str, (*prev == K_z) ? dir : (char*)prev);
  if ((i = strlen(str)) > k) {
    j = strlen(str) - k + 3;
    if (count_k(str, j)) sprintf(str, "... %s", &str[j + 1]);
    else sprintf(str, "...%s", &str[j]);
    if ((j = strlen(str)) > 0) j--;
    str[j] = '\0';
  } else {
    for (j = vt_col(0) - col - i; j > 0; i++, j--) str[i] = ' ';
    i--;
    str[i] = '\0';
  }
  if (dir) {
    vt_move(vt_row(0) - 1, klen);
    vt_eeol();
    vt_stand();
    vt_move(vt_row(0) - 1, col);
    p.cs = str;
    p.f.len = strlen(p.cs);
    p.f.len--;
    put_str(&p, 0);
  } else strcpy(prev, str);
}
  
void disp_copy(void *p, int nlen)/*usilic*/
{
  int i;
  char **cpp;
  static char **copyp;

  if (!nlen) {
    copyp = p;
    return;
  }
  cpp = p;
  for (i = 0; i < vt_row(0) - 1; i++) {
    vt_move(i, 0);
    vt_eeol();
    if (cpp[i]) vt_printf("%s", cpp[i]);
  }
  vt_move(vt_row(0) - 1, 0);
  for (i = 0; i < 3; i++) {
    vt_stand();
    vt_printf("%s", copyp[i]);
    if (i == 2) break;
    vt_endstand();
    vt_putchar(' ');
  }
}

#include "usilic.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/wait.h>

static char *w_arg(char arg[], char argv[])
{ /* set filename for program (dvi2tty etc.) needs a specific suffix (.dvi etc.) */
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
  if (f_chkbin) { /* not file-selector */
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
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    execvp(*chp->argv, chp->argv);
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
  if ((n_error = load_intr()) == S_err0) { /* store n_error */
    tmp_buff = NULL;
    *tmp_sizep = 0;
  } else {
    pipe(pv);
    if (!fork()) {
      load_signal();
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
    if (n_error) save_intr(n_error); /* restore n_error */
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
{ /* concatenate mime */
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
	  else if (!f_bs && pp->attr) /* bold or underline attribute */
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
	    fwrite(pp->cs, 1, strlen(pp->cs), fp); /* for complete filename */
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
      /* convert EUC to JIS */
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

#include "usilic.h"
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

void init_man(BASE *bp, FILE **pinp, char name[], u_char prev[], char arg[], int *klenp)/*usilic*/
{
  free_bp(bp);
  sprintf(arg, "%c%c", K_z, 0);
  *pinp = fork_man(name);
  bp->pp = alloc_pipe(T_man, pinp, &bp->nrow, bp, strlen(name));
  bp->nrow = 1;
  while (*pinp && bp->nrow < vt_row(0)) 
    alloc_pipe(0, pinp, &bp->nrow, NULL, 0);
  if (!bp->pp) make_bp(bp, s_S(S_err6), NULL, NULL);
}
  
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
  
#include "usilic.h"
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

static struct {
  struct {
    u_int winch	: 1;
    u_int term	: 1;
    u_int ctrl	: 1;
  } f;
} Key;

typedef int (*GETCH)(void);

static int is_esc(char *ks[], char esc[])
{
  int i;

  for (i = 0; ks[i] && (ks[i][0] != esc[0] || ks[i][2] != esc[2]); i++) ;
  return (ks[i]) ? i : 0;
}

static void set_name(STR *sp0, char name[])
{
  STR *sp;
  int i, c, len;

  if (!sp0) return;
  for (len = strlen(name), i = 0; ; len++) {
    if (!(c = sp0->name[len])) break;
    for (sp = sp0->next; sp && sp->name[len] == c; sp = sp->next) ;
    if (sp) break;
  }
  if ((len -= (i = strlen(name)))) {
    memcpy(&name[i], &sp0->name[i], len);
    name[i + len] = '\0';
  }
}

static int key_getch(int f_key, u_char cva[])
{
  static u_char *cvp, cvp0[N_line];
  int i;

  if (f_key == 2 || f_key == 3) {
    if ((i = strlen(cva)) >= N_line) {
      memcpy(cvp0, cva, N_line - 1);
      cvp0[N_line-1] = '\0';
      i = N_line - 1;
    } else strcpy(cvp0, cva);
    cvp = cvp0;
    if (f_key == 2) vt_mode(-'d');
    return 0;
  } else if (!(i = *cvp++)) return -1;
  else if (load_intr() == S_err0) while ((i = *cvp)) cvp++;
  if (!*cvp) vt_mode('d');
  return i;
}

static int set_km(int m, int **p_kmp, int f_M)
{ /* keyboard macro */
  static int f_me, n_km, a_km[N_macro];

  switch(f_M) {
  case -1:
    return *a_km;
  case M_ms:
    f_me = n_km = 0;
    break;
  case 0:
    a_km[n_km++] = m;
    a_km[n_km] = 0;
    if (n_km < N_macro-1) break;
  case M_me:
    f_me = 1;
    n_km = -1;
    break;
  case M_mm:
    if (!f_me) {
      a_km[n_km++] = f_M; /* endless loop */
      a_km[n_km] = 0;
      f_me = 1;
    }
    *p_kmp = a_km;
    break;
  }
  return n_km + 1;
}

static void key_ttou(int dummy)
{
  signal(SIGTTOU, key_ttou);
  if (s_S(S_pid)) print_pid(getpid());
  if (s_S(S_error)) kill(getpid(), SIGTSTP);
  else killpg(getpgrp(), SIGTSTP);
}

static void key_cont(int dummy)
{
  signal(SIGCONT, key_cont);
  if (s_S(S_pid)) print_pid(getpid());
  vt_init(-9); /* check SIGTTOU */
}

void key_winch(int spid0)/*usilic*/
{
  static int spid;

  if (spid0 < 0) {
    spid = -spid0;
    return;
  }   
  signal(SIGWINCH, key_winch);	
  vt_winsize();
  if (spid && !kill(spid, 0)) vt_mode('w');
  Key.f.winch = 1;
}

static void key_segv(int dummy)
{
  save_stderr("%s: %s.\n", s_S(S_usilic), s_S(S_err8));
  exit_usilic();
  print_stderr(NULL);
  exit(1);
}

void key_term(int dummy)/*usilic*/
{
  int pid;

  if ((pid = get_hup(0))) kill(pid, SIGKILL);
  unlink_file();
  Key.f.term = 1;
}

static void stand_str(RV *rvp0, int f_M)
{
  static RV *rvp;
  static PP p;
  int i, len, n, f_enter, n_attr, f_stand, f_grep;
  static ATTR pp_attr[N_attr]; /* display control-code (^?) */

  if (!vt_mode('D') || rvp0 == F11 || f_M == M_shell 
      || f_M == T_arc3 || f_M == T_input) return;
  if (is_comm(f_M, 0)) f_enter = f_M;
  else if ((i = is_comm(f_M, 1))) f_enter = i;
  else f_enter = 0;
  if (rvp0) {
    rvp = rvp0;
    vt_stand();
    f_stand = 1;
  } else if (!rvp) return;
  else {
    vt_endstand();
    f_stand = -1;
  }
  vt_move(rvp->row, rvp->col);
  p.attr = NULL;
  f_grep = f_M == M_egrep || f_M == M_cut 
    || f_M == T_hfgrep || f_M == T_hegrep;
  if (strlen(rvp->str)) p.cs = strdup(&rvp->pp->cs[(f_enter) ? 0 : rvp->col]);
  else return;
  n_attr = set_attr(pp_attr, p.cs, f_stand, f_grep);
  if (vt_charset(0) == C_ascii) {
    for (i = 0, n = rvp->col, len = strlen(p.cs); len; len--) {
      if ((p.cs[i] >= 0x80 && (n += 4) <= vt_col(0))
	  || (p.cs[i] < ' ' && (n += 2) <= vt_col(0))
	  || ++n <= vt_col(0)) i++;
      else break;
    }
    p.cs[i] = '\0';
    p.f.len = strlen(p.cs);
  } else {
    if (strlen(p.cs) + rvp->col > vt_col(0)) p.cs[vt_col(0) - rvp->col] = '\0';
    p.f.len = strlen(p.cs);
    if (count_k(p.cs, p.f.len)) p.f.len--;
  }
  if (f_enter) p.split = rvp->pp->split;
  else p.split = (f_M == T_grep && f_stand > 0) ? -f_M : 0;
  if (n_attr) {
    pp_attr[n_attr++].n = -1;
    p.attr = pp_attr;
  }
  put_str(&p, f_enter);
  if (f_enter) vt_move(rvp->row, 0);
  free(p.cs);
  vt_endstand();
}

static int kton(int k, int key_c[])
{
  int i, i_tab;

  for (i_tab = 0; key_c[i_tab]; i_tab++) ;
  for (i = 0; i < i_tab && key_c[i] != k; i++) ;
  return (i < i_tab) ? i : 0;
}

static void key_eeol(int row, int col0, int len, int f_exit)
{
  if (!f_exit) vt_endstand();
  vt_move(row, col0);
  while (len-- > 0) vt_putchar(' ');
  vt_move(row, col0);
}

static int input_key(int f_M, u_char mess[], u_char *cs, int col0, char *cname, LIST **listp, STR *cmd, u_char iname[], char dir[], char old[], u_char copy[])
{
  int i, j, k, m, len, row, col, cole, shift, f_list, ti;
  u_char *cp, buff[N_line];
  static int ilen;
  static u_char buffb[N_line];

  row = vt_row(0) - 1;
  k = vt_col(0) / 4;
  switch(f_M) {
  case M_man2:
  case M_cut2:
  case M_locate:
  case T_locate:
  case M_fgrep:
    k = vt_col(0) / 2;
  case M_prog2:
  case T_rm:
  case T_cp:
  case T_mv:
  case T_arc:
  case T_arc3:
  case M_lfile2:
  case M_ldir2:
    *buff = '\0';
    break;
  case M_exit:
    strcpy(buff, "y");
    k = 17;
    break;
  case M_man:
    k = vt_col(0) / 2;
  default:
    if (!cs || cs == F3) *buff = '\0';
    else strcpy(buff, cs);
    if (cname) sprintf(strchr(buff, '\0'), " %s", cname);
    break;
  }
  if ((i = *iname)) {
    if (i == K_a0 && !*buffb) {
      m = 0;
      *iname = '\0';
    } else {
      if (*buffb) strcpy(buff, buffb);
      else ilen = 0;
      if (i == K_a0) buff[ilen] = *buffb = '\0';
      else strcpy(&buff[ilen], iname);
      *iname = '\0';
      col = cole = strlen(buff) + col0;
      m = -K_e;
    }
  } else m = 0;
  col = cole = col0 + strlen(buff);
  len = vt_col(0) - col0 - k - 1;
  if (f_M == M_exit) len = 2;
  col = (col > col0 + len) ? col0 + len : col;
  shift = f_list = 0;
  key_eeol(row, col0, len, f_M == M_exit);
  vt_move(row, col0);
  if ((i = *buff)) {
    if (i == K_a0) *buff = '\0';
    else if ((i = cole - col0 - len) > 0) {
      shift = i;
      print_buff(&buff[shift], count_k(buff, shift));
    } else print_buff(buff, 0);
  }
  vt_flush();
  for (m = 0; ; m = (m > 0) ? 0 : m
       , f_list = (f_list != 1) ? 0 : f_list) {
    m = edit_line(m, T_input, buff, len, row, col0, &col, &cole, &shift, copy);
    switch(m) {
    case 0:
    case N_char:
      break;
    case M_hist:
      switch(f_M) {
      case T_hist:
      case T_hfile:
      case T_hdir:
      case T_hman:
      case T_hlocate:
      case T_hfgrep:
      case T_hegrep:
	echo_mess(S_errv);
	continue;
      }
    case M_cmd:
      if (f_M == T_cmd) continue;
    case M_list:
      if (m == M_cmd || m == M_hist) { 
	if (f_M != M_fgrep && f_M != M_egrep) ilen = 0;
      } else if (f_M == T_list 
	  || f_M == M_locate || f_M == T_locate
	  || f_M == M_fgrep || f_M == M_egrep) continue;
      if (m == M_list) {
	*listp = free_list(*listp);
	if ((ti = col + shift - col0)) buff[ti] = '\0';
	if (strchr(buff, '\0')[-1] == '~')
	  strcpy(&strchr(buff, '\0')[-1], s_S(S_home));
	else if (strstr(buff, "~/")) {
	  char *p;
	  cp = &strstr(buff, "~/")[2];
	  p = alloca(strlen(cp) + 1);
	  strcpy(p, cp);
	  sprintf(&cp[-2], "%s%s", s_S(S_home), p);
	}
	{
	  char *p = alloca(strlen(buff) + 1);
	  if (*buff != '/') {
	    p = alloca(strlen(dir) + 1);
	    strcpy(p, dir);
	  } else {
	    p = alloca(strlen(buff) + 1);
	    strcpy(p, buff);
	    get_dir(p);
	  }
	  i = make_list(p, buff, &ilen, listp, f_M);
	}
	if (*buffb) *buffb = '\0';
	col = cole = col0 + strlen(buff);
	len = vt_col(0) - col0 - k - 1;
	col = (col > col0 + len) ? col0 + len : col;
	if ((shift = cole - col0 - len) < 0) shift = 0;
      } else i = -m;
      switch(i) {
      case 0:
	break;
      case 1:
	col = cole = strlen(buff) + col0;
	m = -K_e;
	continue;
      default:
	if (i < 0) ;
	else if (!f_list) {
	  f_list = 1;
	  col = cole = strlen(buff) + col0;
	  m = -K_e;
	  vt_putchar(K_g);
	  continue;
	} else f_list = 0;
	strcpy(mess, &buff[ilen]);
	if (i < 0) {
	  m = (i == -M_cmd) ? -T_cmd : -T_hist;
	  strcat(buffb, buff);
	} else {
	  m = -T_list;
	  strcpy(buffb, buff);
	}
	goto jump;
      }
      break;
    case M_susp:
      vt_init(-2);
      killpg(getpgrp(), SIGTSTP);
      vt_winsize();
      vt_init(2);
      m = -M_clear;
      goto jump;
      break;
    default:
      if (m < N_char || m >= N_num) goto jump;
      else
	switch((m - N_char)) {
	case '\r':
	  i = M_cr;
	  if (!*buff)
	    if (f_M == T_locate)
	      if (!cs || cs == F3) *buff = '\0';
	      else strcpy(buff, cs);
	    else {
	      vt_putchar(K_g);
	      i = 0;
	    }
	  else {
	    j = 0;
	    switch(f_M) {
	    case T_locate:
	      i = T_cr;
	      break;
	    case T_hfile:
	    case M_lfile:
	      if ((cp = strstr(buff, " (")) || (cp = strstr(buff, " \042"))) {
		j = cp - buff;
		*cp = '\0';
	      } else j = 0;
	    case T_hdir:
	    case M_ldir:
	    case M_lfile2:
	    case M_ldir2:
	      if ((cp = strstr(buff, "//"))) strcpy(buff, &cp[1]);
	      else if (strchr(buff, '\0')[-1] == '~')
		strcpy(buff, s_S(S_home));
	      else if ((cp = strstr(buff, "~/"))) {
		char *p = alloca(strlen(&cp[2]) + 1);
		strcpy(p, &cp[2]);
		sprintf(buff, "%s%s", s_S(S_home), p);
	      }
	      while (*(cp = &strchr(buff, '\0')[-1]) == ' ' || *cp == '\t') 
		*cp = '\0';
	      if (access(buff, F_OK) < 0) {
		*buff = '\0';
		vt_putchar(K_g);
		if ((i = f_M == M_lfile || f_M == M_ldir
		     || f_M == T_hfile || f_M == T_hdir)) {
		  m = -M_clear;
		  goto jump;
		}
	      } else if ((f_M == M_lfile || f_M == T_hfile) && j) 
		buff[j] = ' ';
	      break;
	    default:
	      break;
	    }
	  }
	  if (shift) shift = 0;
	  col = cole = col0;
	  strcpy(mess, buff);
	  switch(f_M) {
	  case M_cut:
	  case M_cut2:
	  case M_fgrep:
	  case M_egrep:
	  case T_hegrep:
	  case T_hfgrep:
	    break;
	  default:
	    for (j = strlen(mess) - 1; j >= 0 && mess[j] == ' '; j--) ;
	    if (mess[j+1]) mess[j+1] = '\0';
	    break;
	  }
	  *buff = '\0';
	  m = i;
	  goto jump;
	  break;
	case K_s:
	  m = M_fgrep;
	  goto jump;
	  break;
	case K_w:
	  if (f_M == T_cp || f_M == T_mv || f_M == T_arc || f_M == T_arc3) {
	    char *p = alloca(1 + 1 + strlen(old) + 1);
	    sprintf(p, "%c%c%s", K_u, K_k, old);
	    read_m(NULL, 3, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
	  } else vt_putchar(K_g);
	  break;
	default:
          m = -m;
	  break;
	}
      break;
    }
  }
 jump:
  return m;
}  

int read_m(RV *rvp, int f_M, int klen, u_char *cva, char *cname, LIST **listp, STR *cmd, u_char *iname, char *dir, char *old)/*usilic*/
{
  static int key_k[32], key_r[8], key_g[14], key_y[6];
  static int n_bm, n_km, *p_km;
  static u_char *p_bm;
  static int *key_m, *key_c, k_wait;
  static GETCH getch = vt_getch;
  u_char *cp;
  int i, c, k, f_enter, f_list, rowb, colb;
  
  if (f_M == T_key2) {
    key_m = (int*)cva;
    key_c = (int*)cname;
    k_wait = conf_wait(NULL, NULL);
    read_esc(-1, key_r, key_c);

    /* copy buffer key */
    key_y[0] = -1;
    key_y[1] = K_a;
    key_y[2] = K_e;
    key_y[3] = K_b;
    key_y[4] = K_f;
    key_y[5] = 0;
    
    /* invalid command-line key */
    for (i = 0; i < 29; i++)
      switch(i) {
      case 0:
	key_k[i] = -1; /* return value 0 from ktoc() */
	break;
      case K_j:  /* ^J */	
      case K_o:  /* ^O */
      case K_t:  /* ^T */
      case K_x:  /* ^X */
      case K_ec:  /* ^[ */
	key_k[i] = -1; /* can be used for `F-?' in usilicrc */
	break;
      case 28:
	key_k[i] = 0;
	break;
      default:
	key_k[i] = i;
	break;
      }

    /* cursor key */
    key_r[0] = -1;
    key_r[1] = key_c[M_kup];
    key_r[2] = key_c[M_kdn];
    key_r[3] = key_c[M_knext];
    key_r[4] = key_c[M_kprev];
    key_r[5] = key_c[M_kback];
    key_r[6] = key_c[M_kforw];
    key_r[7] = 0;

    /* invalid key in T_input */
    key_g[0] = -1;
    key_g[1] = M_up;
    key_g[2] = M_dn;
    key_g[3] = M_fgrep;
    key_g[4] = M_egrep;
    key_g[5] = M_kforw;
    key_g[6] = M_kback;
    key_g[7] = M_head;
    key_g[8] = M_tail;
    key_g[9] = M_prev;
    key_g[10] = M_next;
    key_g[11] = M_kprev;
    key_g[12] = M_knext;
    key_g[13] = 0;

    return 0;
  }
  if (cva) {
    key_getch(f_M, cva);
    getch = (GETCH)key_getch;
    return 0;
  }
  if (is_comm(f_M, 1) || f_M == M_exit) f_enter = 2;
  else if (is_comm(f_M, 0)) f_enter = 1;
  else f_enter = 0;
  f_list = 1;
  switch(f_M) {
  case M_copy:
    c = 0;
    break;
  case M_fgrep:
  case M_locate:
  case M_man2:
  case M_cut2:
  case M_prog2:
  case M_lfile2:
  case M_ldir2:
    f_list = 0;
  case M_egrep: case M_cut:
  case M_man:
  case M_lfile: case M_ldir:
  case M_prog:
  case M_hist: case T_hist:
  case T_hfile: case T_hdir: case T_hman: case T_hlocate:
  case T_hfgrep: case T_hegrep:
  case M_cmd: case T_cmd:
  case M_list: case T_list:
  case T_locate:
    c = 1;
    break;
  default:
    c = f_enter == 2;
    break;
  }
  if (f_enter == 2) {
    vt_endstand();
    vt_move(vt_row(0) - 1, klen);
    switch(f_M) {
    case T_rm:
      vt_printf("Delete ? %s ", s_S(S_erry));
      klen += 9 + strlen(s_S(S_erry)) + 1;
      break;
    case M_exit:
      klen -= 2;
      break;
    default:
      vt_printf("Destination ? ");
      klen += 14;
      break;
    }
  }
  if (c) {
    int f_stand = 1;
    u_char *p = (rvp->pp && rvp->pp->tell != (u_int)F7) ? rvp->pp->cs : NULL;
    if (*iname || !p) /* filename with no attribute on the file-list after */
      f_stand = 0;    /* `Enter' (select file) */
    if (f_list && f_stand) stand_str(rvp, f_M);
    while (1) {
      c = input_key(f_M, rvp->str, p, klen, (*cname) ? cname : NULL
		    , listp, cmd, iname, dir, old, rvp->copy);
      if (!f_list && strint(key_g, c)) vt_putchar(K_g);
      else break;
    }
    if (f_list && f_stand && c != M_cr && c != T_cr) stand_str(NULL, f_M);
    return c;
  }
  stand_str(rvp, f_M);  
  k = c = 0;
  do {
    if (Key.f.winch) {
      Key.f.winch = 0;
      return T_winch;
    } else if (Key.f.term) {
      Key.f.term = 0;
      return T_exit;
    } else if (!c) {
      if (getch != vt_getch) {
	if ((c = getch()) < 0) {
	  getch = vt_getch;
	  c = 0;
	  continue;
	}
      } else if (p_bm) {
	c = p_bm[n_bm++];
	if (!p_bm[n_bm]) {
	  if (getch == vt_getch) vt_mode('d');
	  free(p_bm);
	  p_bm = NULL;
	  n_bm = 0;
	}
	usleep(k_wait);
      } else if (p_km) {
	if ((c = p_km[n_km++]) == M_mm) {
	  set_km(0, &p_km, c);
	  n_km = 0;
	  c = p_km[n_km++];
	}
	if (vt_getch() || !p_km[n_km]) {
	  if (getch == vt_getch) vt_mode('d');
	  p_km = NULL;
	  n_km = 0;
	  vt_init(3);
	} else usleep(k_wait);
	stand_str(NULL, f_M);
	return c;
      }	else c = getch();
      if (f_M == M_getch && c) goto SET_KM;
      if (((c > 0 && c < ' ') || c == K_at) && Key.f.ctrl) goto SET_KM;
      if (f_M == M_copy && strint(key_y, c)) {
	c += N_char;
	goto SET_KM;
      }
      switch(c) {
      case K_at:
	if (f_M == M_shell || f_M == T_input) {
	  vt_putchar(K_g);
	  c = 0;
	}
	break;
      default:
	if (c >= 'A' && c <= 'D' && (f_M != M_shell && f_M != T_input)) 
	  c += 'a'-'A';
	break;
      }
    }
    if (c == K_ec)
      if (k >> 8 == K_ec) {
	c = M_exit;
	continue;
      } else {
	i = -1;
	if (getch == vt_getch && !(i = read_esc(f_M, NULL, NULL))) {
	  c = 0;
	  continue;
	} else if (i < 0) {
	  k = K_ec << 8;
	  c = 0;
	  continue;
	} else {
	  c = i;
	  goto SET_KM;
	}
      }
    else if (c == K_v) {
      c = M_kforw;
      continue;
    } else if (c == 'w' && k >> 8 == K_ec) {
      c = M_kback;
      continue;
    } else if (c == K_z) {
      c = M_susp;
      continue;
    } else if (c >= '0' && c <= '9') {
      if (c >= '1' && k >> 8 == key_c[M_num]) {
	if (rvp != F11)
	  disp_window(rvp->ps, rvp->pp, f_M, NULL, NULL, NULL, &k);
	if (!(c = print_line(NULL, NULL, (f_M == M_shell) ? -c : c, F2))) {
	  c = M_clear;
	  goto SET_KM;
	} else {
	  c += N_num;
	  goto SET_KM;
	}
      } else if (k >> 8 == key_c[M_cd]) {
	c += T_0 - '0';
	goto SET_KM;
      }
    }
    if (c) {
      if (Key.f.winch || Key.f.term) {
	c = 0;
	continue;
      }
      do {
	if (k) {
	  k |= c;
	  if (strint(key_m, k)) {
	    k <<= 8;
	    c = 0;
	    continue;
	  } else if (!(c = kton(k, key_c))) {
	    k = c = 0;
	    continue;
	  } else if (f_M == M_shell || f_M == T_input) continue;
	} else if (strint(key_m, c)
		   && (((f_M == M_shell || f_M == T_input) 
			&& !strint(key_k, c) && c < ' ')
		       || (f_M != M_shell && f_M != T_input))) { 
	  k = c << 8;
	  c = 0;
	  continue;
	} else k = c;
	if (f_M == M_shell || f_M == T_input)
	  if ((!strint(key_k, k) && k < ' ')
	      || (f_M == M_shell 
		  && k == K_s) /* ^S */
	      || (f_M == T_input && k == K_c)) { /* ^C */
	    vt_putchar(K_g);
	    k = c = 0;
	  } else {
	    switch(k) {
	    case K_l:	/* ^L */
	      c = M_clear;
	      break;
	    case K_i:	/* ^I */
	      c = M_list;
	      break;
	    case K_s:	/* ^S */
	      c = M_fgrep;
	      break;
	    case K_r:	/* ^R */
	      c = M_hist;
	      break;
	    case K_n:	/* ^N */
	      c = M_dn;
	      break;
	    case K_p:	/* ^P */
	      c = M_up;
	      break;
	    case K_g:	/* ^G */
	      c = M_bl;
	      break;
	    case K_z:	/* ^Z */
	      c = M_susp;
	      break;
	    case K_w:	/* ^W */
	      if (f_M != T_input) {
		c = T_w;
		break;
	      }
	    default:
	      c = k + N_char;
	      break;
	    }
	    k = 0;
	  }
	else if (!(c = kton(k, key_c))
		 || strchr(print_key(NULL), c)
		 || c == M_list) k = c = 0;
	else 
	  switch(c) {
	  case M_cmd:
	    c = T_cmd3;
	    break;
	  case M_hist:
	    c = T_hist2;
	    break;
	  case M_ms:
	    echo_mess(S_errp);
	    n_km = set_km(0, NULL, c);
	    if (p_km) p_km = NULL;
	    c = k = 0;
	    continue;
	  case M_me:
	    if (n_km) {
	      k = S_errq;
	      set_km(0, NULL, c);
	    } else k = S_errr;
	    echo_mess(k);
	    n_km = c = k = 0;
	    continue;
	  case M_mm:
	    if (!set_km(0, NULL, -1)) {
	      echo_mess(S_errr);
	      c = k = 0;
	      continue;
	    }
	    set_km(0, &p_km, c);
	    n_km = 0;
	    c = p_km[n_km++];
	    vt_init(-3);
	    break;
	  default:
	    if (c >= M__0 && c <= M__19) {
	      if (!(cp = s_S(S_macro + c - M__0))) {
		char *p = alloca(strlen(s_S(S_usilic)) + 10);
		sprintf(p, "%src: M-%d", s_S(S_usilic), c - M__0);
		save_intr(S_errs);
		print_intr(p, NULL);
	      } else p_bm = strdup(cp);
	      c = k = n_bm = 0;
	    }
	    break;
	  }
      } while (0);
      if (getch == vt_getch) stand_str(rvp, f_M);
    } else if (f_M == M_shell && !k) /* check access() ==> shell.c(2) */
      return 0;
#if 1
    if (!access(s_S(S_enter), F_OK)) {
      c = T_du2;
      vt_move(vt_row(0) - 1, 0);
      vt_putchar(K_g);
      vt_printf(" (%s%s%s)    ", vt_geta(A_md), s_S(S_errh), vt_geta(A_me));
      vt_move(vt_row(0) - 1, 2 + strlen(s_S(S_errh)) + 2);
      vt_flush();
      while (vt_getch() != K_m) ;
      f_M = T_input;
    }
#endif
  } while (!c);
  if (getch == vt_getch) stand_str(NULL, f_M);
  if (f_M != M_shell)
    if (c == M_susp) {
      rowb = vt_row(0);
      colb = vt_col(0);
      vt_endstand();
      vt_move(vt_row(0) - 1, 0);
      if (is_fm()) {
	vt_init(-2);
	kill(getpid(), SIGTSTP);
	vt_init(2);
      } else {
	vt_init(-2);
	killpg(getpgrp(), SIGTSTP);
	killpg(getpgrp(), SIGCONT);
	vt_init(2);
      }
      vt_winsize();
      if (rowb != vt_row(0) || colb != vt_col(0)) key_winch(0);
      c = M_clear;
    }
  if (c == M_ascii) {
    vt_charset((vt_charset(0) == C_ascii) ? C_euc : C_ascii);
    c = M_sel;
  }
SET_KM:
  if (getch == vt_getch && n_km && !p_km) n_km = set_km(c, NULL, 0);
  return c;
}

void load_signal(void)/*usilic*/
{
  int i;

  for (i = 1; i < NSIG; i++) signal(i, SIG_DFL);
}

void save_signal(int f_usilic)/*usilic*/
{
  int i;

  for (i = 1; i < NSIG; i++) 
    switch(i) {
    case SIGWINCH:
      signal(i, key_winch);
      break;
    case SIGTERM:
    case SIGHUP:
      signal(i, key_term);
      break;
    case SIGSEGV:
      signal(i, key_segv);
      break;
    case SIGTTIN:
      signal(i, SIG_DFL);
      break;
    case SIGTTOU:
      signal(i, key_ttou);
      break;
    case SIGCONT:
      signal(i, key_cont);
      break;
    case SIGTSTP:
    case SIGCHLD:
      break;
    default:
      signal(i, SIG_IGN);
      break;
    }
}

int find_name(int n_name, STR **spp, LIST **listp, char name[], char str[], char bak[], int f_find)
{
  DIR *dirp;
  struct dirent *dp;
  struct stat st;
  STR *sp;
  PN pn;
  int i;
  char tmp[N_line];

  sp = *spp;
  if (!(dirp = opendir(str))) return n_name;
  while ((dp = readdir(dirp)))
    if ((dp->d_ino || !strcmp(dp->d_name, ".."))
	&& (!*name || (*name && !strncmp(dp->d_name, name, strlen(name))))) {
      if (f_find) {
	strcpy(tmp, str);
	strcat(tmp, dp->d_name);
	stat(tmp, &st);
	if ((((i = S_ISDIR(st.st_mode)) || !(st.st_mode & S_IXUSR))
	     && f_find == 1)
	    || (!i && f_find == 2)) continue;
      } else {
	strcpy(tmp, str);
	strcat(tmp, dp->d_name);
	stat(tmp, &st);
	i = S_ISDIR(st.st_mode);
      }
      if (i && strchr(dp->d_name, '\0')[-1] != '/') strcat(dp->d_name, "/");
      strcpy(bak, dp->d_name);
      if (*name) sp = save_str(sp, dp->d_name, &pn);
      *listp = save_list(*listp, dp->d_name, &pn);
      n_name++;
    }
  *spp = sp;
  closedir(dirp);
  return n_name;
}
  
void make_cmd(STR *sp, LIST **listp)/*usilic*/
{
  static PN pn;

  for (; sp; sp = sp->next) 
    if (sp->name) *listp = save_list(*listp, sp->name, &pn);
}

int make_list(u_char dir0[], u_char buff[], int *lenp, LIST **listp, int f_M)/*usilic*/
{
  int c, i, n_name, f_prog, f_dir;
  static char nc[] = "/<>()|; ";
  char *cp, *dir, name[N_line], bak[N_line];
  STR *sp;

  switch(f_M) {
  case M_prog: case M_prog2:
  case M_man: case M_man2:
  case M_shell:
    if (*buff != '.') {
      for (i = 0; (c = buff[i]) && !strchr(nc, c); i++) ;
      f_prog = !c;
    } else f_prog = 0;
    f_dir = 0;
    break;
  case M_ldir: case M_ldir2:
  case T_arc: case T_arc3:
    f_prog = 0;
    f_dir = 2;
    break;
  default:
    f_dir = f_prog = 0;
    break;
  }
  if (f_prog) {
    strcpy(name, buff);
    dir = malloc(strlen(s_S(S_path)) + 1);
  } else {
    if (f_M != M_shell && (i = ichrstr(buff, 0, "//"))) i--;
    else 
      for (i = strlen(buff); i; i--) 
	if ((c = buff[i]) && strchr(&nc[1], c)) break;
    if (i) i++;
    if (buff[i]) {
      if (buff[i] == '/') {
	dir = malloc(strlen(&buff[i]) + 1 + 1);
	strcpy(dir, &buff[i]);
      } else {
	u_char *p = alloca(strlen(&buff[i]) + 1);
	strcpy(p, &buff[i]);
	dir = malloc(strlen(dir0) + strlen(p) + 1 + 1);
	strcpy(dir, dir0);
	strcat(dir, p);
      }
      strcpy(name, dir);
      get_dir(dir);
      if (strchr(dir, '\0')[-1] != '/') strcat(dir, "/");
      get_name(name);
    } else {
      dir = malloc(strlen(dir0) + 1);
      strcpy(dir, dir0);
      *name = '\0';
    }
  }
  sp = NULL;
  if (f_prog) {
    char *path = malloc(strlen(s_S(S_path)) + 1);
    char *str = malloc(strlen(s_S(S_path)) + 1);
    n_name = 0;
    strcpy(path, s_S(S_path));
    cp = path;
    while (1) {
      strcpy(dir, cp);
      if ((cp = strchr(dir, ':'))) {
	*cp = '\0';
	strcpy(str, dir);
	strcat(str, "/");
        n_name = find_name(n_name, &sp, listp, name, str, bak, f_prog);
	*cp++ = ':';
      } else {
	strcat(dir, "/");
        n_name = find_name(n_name, &sp, listp, name, dir, bak, f_prog);
	break;
      }
    }
    free(path);
    free(str);
  } else n_name = find_name(0, &sp, listp, name, dir, bak, f_dir);
  if (f_prog) i = 0;
  else {
    c = 0;
    i = strlen(buff);
    do {
      if ((c = buff[i]) && strchr(nc, c)) break;
      if (!i) break;
    } while (i--);
    if ((!i && c == '/') || i) i++;
  }
  if (!*name) {
    if (!f_prog && n_name == 1) strcpy(&buff[i], bak);
  } else if (n_name) {
    set_name(sp, name);
    strcpy(&buff[i], name);
  }
  if (n_name == 1) {
    if (f_M == M_shell)
      if (strchr(bak, '\0')[-1] == '/') {
	if (strchr(buff, '\0')[-1] != '/') strcat(buff, "/");
      } else strcat(buff, " ");
    else if (!f_prog && f_dir && strchr(buff, '\0')[-1] != '/') 
      strcat(buff, "/");
  }
  free_str(sp);
  *lenp = i;
  free(dir);
  return n_name;
}

static void add_buff(int m, int *shiftp, int row, int *colp, int col0, int len, int *colep, u_char back[], u_char buff[])
{
  int i;

  if (strlen(buff) > N_line - 2) {
    vt_putchar(K_g);
    return;
  }
  if (*colp < col0 + len && (*shiftp || (!*shiftp && *colp < *colep))) {
    i = *shiftp + *colp - col0;
    strcpy(back, &buff[i]);
    strcpy(&buff[i+1], back);
    buff[i] = m;
    strcpy(back, &buff[*shiftp]);
    back[len] = '\0';
    vt_move(row, col0);
    print_buff(back, count_k(buff, *shiftp));
    (*colp)++;
    (*colep)++;
    vt_move(row, *colp);
  } else {
    i = 0;
    if (*colp == col0 + len) {
      (*shiftp)++;
      vt_move(row, col0);
      if ((i = buff[*colp - col0 + *shiftp - 1])) {
	strcpy(back, &buff[*colp - col0 + *shiftp - 1]);
	buff[*colp-col0 + *shiftp - 1] = '\0';
      }
      print_buff(&buff[*shiftp], count_k(buff, *shiftp));
    } 
    if ((m > 0 && m < ' ') || m == K_at) {
      vt_stand();
      vt_putchar(((m == K_at) ? 0 : m) + '@');
      vt_endstand();
    } else vt_putchar(m);
    vt_flush();
    sprintf(&buff[*colp-col0 + ((*shiftp) ? *shiftp - 1 : 0)], "%c", m);
    (*colep)++;
    if (i) strcat(buff, back);
    if (!*shiftp) *colp = *colep;
    else strcpy(back, &buff[*shiftp]);
    vt_move(row, *colp);
  }
}

int edit_line(int m, int mb, u_char buff[], int len, int row, int col0, int *colp, int *colep, int *shiftp, u_char *copy)/*usilic*/
{
  int i, col, cole, shift;
  static u_char back[N_line+1], yank[N_line+1];

  col = *colp;
  cole = *colep;
  shift = *shiftp;
  for (; ; m = (m > 0) ? 0 : m) {
    if (!m) m = read_m(F11, mb, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (Key.f.ctrl) {
      Key.f.ctrl = 0;
      if (((m > 0 && m < ' ') || m == K_dl || m == K_at)) {
	vt_move(row, col);
	add_buff(m, &shift, row, &col, col0, len, &cole, back, buff);
	vt_flush();
	continue;
      }
    }
    if (m == M_paste)
      if (copy) {
	vt_move(row, col0);
	if ((i = strlen(buff) + strlen(copy) + 1 - N_line) > 0) {
	  vt_putchar(K_g);
	  strchr(copy, '\0')[-i] = '\0';
	}
	if (col == cole) strcat(buff, copy);
	else {
	  i = col - col0 + shift;
	  memmove(&buff[i + strlen(copy)], &buff[i], strlen(&buff[i]) + 1);
	  memcpy(&buff[i], copy, strlen(copy));
	}
	cole = strlen(buff) + col0;
	if ((i = cole - col0 - len) > 0) {
	  shift = i;
	  strcpy(back, &buff[shift]);
	  print_buff(back, count_k(buff, shift));
	  col = col0 + len;
	} else {
	  key_eeol(row, col0, len, 0);
	  print_buff(buff, 0);
	  col = cole;
	}
	vt_flush();
      } else m = 0;
    else if ((m >= 0 && m <= N_char) || m >= N_num) break;
    else {
      switch(((m > 0) ? m - N_char : m)) {
      case 0:
	continue;
      case K_q:
	Key.f.ctrl = 1;
	break;
      case K_u: /* ^U */
	i = shift + col - col0;
	cole -= i;
	memcpy(yank, buff, i);
	yank[i] = '\0';
	strcpy(buff, &buff[i]);
	shift = ((i = cole - col0 - len) > 0) ? i : 0;
	strcpy(back, &buff[shift]);
	col = col0;
	key_eeol(row, col0, len, 0);
	print_buff(back, count_k(buff, shift));
	vt_move(row, col);
	vt_flush();
	break;
      case K_k: /* ^K */
	if (shift) {
	  cole = shift + col;
	  strcpy(yank, &buff[cole - col0]);
	  buff[cole - col0] = '\0';
	  if ((i = cole - col0 - len) > 0) {
	    shift = i;
	    col = col0 + len;
	  } else {
	    shift = 0;
	    col = cole;
	  }
	  strcpy(back, &buff[shift]);
	  key_eeol(row, col0, len, 0);
	  print_buff(back, count_k(buff, shift));
	} else {
	  strcpy(yank, &buff[col - col0]);
	  buff[col - col0] = '\0';
	  key_eeol(row, col, len - (col - col0), 0);
	  cole = col;
	}
	break;
      case K_y: /* ^Y */
	vt_move(row, col0);
	if ((i = strlen(buff) + strlen(yank) + 1 - N_line) > 0) {
	  vt_putchar(K_g);
	  strchr(yank, '\0')[-i] = '\0';
	}
	if (col == cole) strcat(buff, yank);
	else {
	  i = col - col0 + shift;
	  memmove(&buff[i + strlen(yank)], &buff[i], strlen(&buff[i]) + 1);
	  memcpy(&buff[i], yank, strlen(yank));
	}
	cole = strlen(buff) + col0;
	if ((i = cole - col0 - len) > 0) {
	  shift = i;
	  strcpy(back, &buff[shift]);
	  print_buff(back, count_k(buff, shift));
	  col = col0 + len;
	} else {
	  key_eeol(row, col0, len, 0);
	  print_buff(buff, 0);
	  col = cole;
	}
	break;
      case K_dl:
      case K_h: /* ^H */
	if (col <= col0) {
	  vt_putchar(K_g);
	  continue;
	} else {
	  if (shift) {
	    if ((i = count_k(buff, col - col0 + shift - 1))) cole--;
	    strcpy(&buff[col - col0 + shift - 1 - i]
		   , &buff[col - col0 + shift
			  + count_k(buff, col - col0 + shift)]);
	    if (i && shift) shift--;
	    if (shift) shift--;
	    else if (i) col--;
	    strcpy(back, &buff[shift]);
	  } else {
	    if (col == cole) {
	      i = (count_k(buff, col - col0 - 1)) ? 2 : 1;
	      buff[cole - col0 - i--] = '\0';
	      col -= i;
	      cole -= i;
	    } else {
	      if ((i = count_k(buff, col - col0 - 1))) cole--;
	      strcpy(&buff[col - col0 - 1 - i], &buff[col - col0]);
	      if (i) col--;
	    }
	    col--;
	    strcpy(back, buff);
	  }
	  cole--;
	  back[len] = '\0';
	  key_eeol(row, col0, len, 0);
	  print_buff(back, count_k(buff, shift));
	  vt_move(row, col);
	}
	break;
      case K_d: /* ^D */
	if ((shift && col >= col0 + len)
	    || (!shift && (cole == col0 || cole == col))) {
	  vt_putchar(K_g);
	  continue;
	}
	if (shift) {
	  if ((i = count_k(buff, col - col0 + shift + 1))) cole--;
	  strcpy(&buff[col - col0 + shift], &buff[col - col0 + shift + 1 + i]);
	  if (i) {
	    shift--;
	    col++;
	  }
	  if (shift) shift--;
	  else if (i) col++;
	  strcpy(back, &buff[shift]);
	  col++;
	} else {
	  i = buff[col - col0] >= 0x80;
	  strcpy(&buff[col - col0], &buff[col - col0 + 1]);
	  if (i && count_k(buff, col - col0 + 1)) {
	    cole--;
	    strcpy(&buff[col - col0], &buff[col - col0 + 1]);
	  }
	  strcpy(back, buff);
	}
	back[len] = '\0';
	cole--;
	key_eeol(row, col0, len, 0);
	print_buff(back, count_k(buff, shift));
	vt_move(row, col);
	break;
      case K_a: /* ^A */
	vt_move(row, (col = col0));
	if (shift) {
	  strcpy(back, buff);
	  back[len] = '\0';
	  print_buff(back, 0);
	  vt_move(row, col);
	  vt_flush();
	  shift = 0;
	}
	break;
      case -K_e:
	m -= m;
      case K_e: /* ^E */
	vt_move(row, col0);
	if ((i = cole - col0 - len) > 0) {
	  shift = i;
	  strcpy(back, &buff[shift]);
	  print_buff(back, count_k(buff, shift));
	  col = col0 + len;
	} else {
	  key_eeol(row, col0, len, 0);
	  print_buff(buff, 0);
	  col = cole;
	}
	break;
      case K_b: /* ^B */
	if (!*buff) {
	  vt_putchar(K_g);
	  continue;
	}
	if (col <= col0) {
	  if (shift) {
	    if (count_k(buff, shift - 1)) shift--;
	    strcpy(back, &buff[--shift]);
	    back[len] = '\0';
	    vt_move(row, col0);
	    print_buff(back, count_k(buff, shift));
	    vt_move(row, col0);
	    vt_flush();
	  } else {
	    vt_putchar(K_g);
	    continue;
	  }
	} else {
	  vt_move(row, --col);
	  if (count_k(buff, col - col0 + shift)) vt_move(row, --col);
	}
	break;
      case K_f: /* ^F */
	if (col >= cole) {
	  vt_putchar(K_g);
	  continue;
	}
	if (col < col0 + len - 1) {
	  vt_move(row, ++col);
	  if (count_k(buff, col - col0 + shift)) vt_move(row, ++col);
	} else {
	  if (shift+len >= cole - col0) {
	    vt_putchar(K_g);
	    continue;
	  }
	  if (count_k(buff, shift + 1)) shift++; 
	  strcpy(back, &buff[++shift]);
	  back[len] = '\0';
	  vt_move(row, col0);
	  print_buff(back, count_k(buff, shift));
	  vt_flush();
	}
	break;
      case K_c: /* ^C */
      case '\r':
	goto jump;
      default:
	if (m > 0) goto jump;
	else m = -m - N_char;
	vt_move(row, col);
	add_buff(m, &shift, row, &col, col0, len, &cole, back, buff);
	break;
      }
      vt_flush();
    }
  }
 jump:
  *colp = col;
  *colep = cole;
  *shiftp = shift;
  return m;
}

int read_esc(int f_M, int *key_r0, int *key_c0)/*usilic*/
{
  int i, c;
  static char *ks[7], esc[8] = {K_ec, };
  static int *key_r, *key_c;
  char *cp;

  if (f_M < 0) {
    vt_ks(ks);
    key_r = key_r0;
    key_c = key_c0;
    return 0;
  }
  i = strlen(ks[0]) - 1;
  for (cp = &esc[1]; i && (*cp = vt_getch()); i--, cp++) ;
  if (esc[1]) {
    i = is_esc(ks, esc);
    esc[1] = '\0';
    if (ks[i]) {
      if (i >= 4) vt_getch();
      stand_str(NULL, f_M);
      c = kton(key_r[i+1], key_c) + ((f_M == T_shell) ? N_char : 0);
    } else c = 0;
  } else c = -1;
  return c;
}
#include "usilic.h"
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "menu.h"

static struct {
  int f_Mb;
  jmp_buf jb;
} Menu;

static void menu_clear(int dummy)
{
  signal(SIGSEGV, menu_clear);
  Menu.f_Mb = T_skip;
  longjmp(Menu.jb, 0);
}

static void clear_cut(CUT *cutp)
{
  if (*cutp->name) *cutp->name = '\0';
  if (cutp->buff) {
    free(cutp->buff);
    cutp->buff = NULL;
  }
}

static void m_copy(LINE *lp, NUM *np, HEAP *hp, char name[])
{
  if (is_name(name, M_copy)) return;
  hp->sp = save_stack(hp->sp, lp, np->M, name, hp->bp, 0);
  strcpy(lp->prev, &name[*name == ' ']);
  *lp->arg = K_z;
  strcpy(name, s_P(P_mode)[M_copy]);
  np->M = M_copy;
  np->Mb = 0;
}

static void m_num(NUM *np, int m, HEAP *hp, FILE **pinp, PN *pnp, char full[])
{
  int i, j, f_pipe;
  PP *pp, *pp1 = NULL;

  m -= N_num;
  if ((f_pipe = hp->bp == &hp->base[4] && *pinp && *pinp != F1)) 
    pnp->nrow = hp->bp->nrow;
  for (i = 0, pp = hp->bp->pp; pp; i++) {
    if (pp->f.line == m) break;
    while (!pp->next && f_pipe && *pinp && *pinp != F1)
      alloc_pipe(0, pinp, &pnp->nrow, NULL, 0);
    if (f_pipe && !*pinp) break;
    pp1 = pp; 
    pp = pp->next;
  }
  pnp->line = (pp1) ? pp1->f.line + 1 : 1;
  if (f_pipe)
    for (j = 0; j < vt_row(0) - 1 && *pinp; j++)
      alloc_pipe(0, pinp, &pnp->nrow, NULL, 0);
  if ((j = pnp->nrow - vt_row(0) + 2) < 0) j = 0;
  if (i > j) pnp->row = i - j;
  else {
    pnp->top = i;
    pnp->row = 0;
  }
  if (f_pipe) {
    hp->bp->nrow = pnp->nrow; /* ==> (2) */
    pnp->row = 0;
  }
  if (np->Mb == T_split || np->Mb == T_buff) {
    strcpy(full, s_S(S_usilic));
    np->ll = 0;
    np->M = T_split2;
  }
}

static int m_dir(int m, HEAP *hp, LINE *lp, char name[], RV *rvp, ARRAY *ap, int *f_Mp)
{
  static u_char dir[N_line];
  u_char *cp, *prev = NULL;
  int i, f_list;

  f_list = strlen(rvp->str) && rvp->pp->cs && *rvp->pp->cs == '/';
  strcpy(dir, (f_list) ? rvp->pp->cs : lp->dir);
  cp = rindex(dir, '/');
  switch(m) {
  case M_m:
    if (cp > dir) *--cp = '\0';
    rindex(dir, '/')[1] = '\0'; 
    break;
  case M_sel:
    prev = alloca(strlen(lp->prev) + 1);
    strcpy(prev, lp->prev);
    cp[1] = '\0'; 
    break;
  default:
    for (i = m - T_0 + 1, cp = dir; i && (cp = index(cp, '/')); i--, cp++) ;
    if (cp) *cp = '\0';
    break;
  }
  i = access(dir, F_OK);
  if (i < 0) {
    if (*name == '/') strcpy(dir, s_S(S_home));
    else {
      sprintf(ap->intr, ": %s", dir);
      return echo_mess(-ENOENT);
    }
  }
  clear_stack(hp, lp, name, &i, K_z);
  if (prev && is_prev(prev, M_du)) {
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, &prev[4]);
    strcpy(ap->name, lp->dir);
  }
  check_dir(hp, lp, dir, &hp->ll.p.dir);
  strcpy(name, dir);
  *f_Mp = T_dir;
  return 0;
}

static void m_row(int m, HEAP *hp, RV *rvp, PN *pnp, NUM *np, FILE **pinp)
{
  static FILE *pin0 = F1;
  int i;

  move_row(m, rvp, pnp, (hp->bp == &hp->base[4]) ? pinp : &pin0, np->Mb);
  if (hp->bp->nrow < pnp->nrow) hp->bp->nrow = pnp->nrow; /* ==> (2) */
  disp_window(rvp->ps, rvp->pp, np->M, NULL, NULL, NULL, &i);
  print_intr(NULL, NULL); /* Not enough buffer && T_pipe? && M_tail */ 
}

static int m_page(int m, HEAP *hp, RV *rvp, PN *pnp, NUM *np, FILE **pinp)
{
  int i, j;
  static FILE *pin0 = F1;

  i = move_page(m, rvp, pnp, (hp->bp == &hp->base[4]) ? pinp : &pin0, np->Mb);
  if (hp->bp->nrow < pnp->nrow) hp->bp->nrow = pnp->nrow; /* ==> (2) */
  if (((j = m == M_forw || m == M_kforw) 
       || (m == M_back || m == M_kback)) && i && i <= vt_row(0) - 2) {
    if (j)
      for (rvp->row = i = vt_row(0) - i - 1; i <= vt_row(0) - 3; i++)
	rvp->pp = rvp->pp->prev;
    else
      for (rvp->row = --i; i; i--) rvp->pp = rvp->pp->next;
    pnp->row = rvp->row;
    pnp->line = rvp->pp->f.line;
    if (strlen(rvp->str)) {
      strcpy(rvp->str, &rvp->pp->cs[rvp->col]);
      rvp->str[vt_col(0) - rvp->col] = '\0';
    }
  }
  disp_window(rvp->ps, rvp->pp, np->M, NULL, NULL, NULL, &i);
  print_intr(NULL, NULL); /* Not enough buffer && T_pipe? && M_tail */ 
  return ((i = pnp->nrow - (vt_row(0) - 1)) > 0) ? i : 0;
}

static u_char *m_w(int m, PP *pp, u_char name[], u_char dir[], char comm[])
{
  u_char *cp = NULL;
  FILE *tp;

  switch(m) {
  case M_w:
    tp = fopen(s_S(S_w), "w");
    fprintf(tp, "%s", name);
    fclose(tp);
    cp = malloc(11 + strlen(s_S(S_w)) + 1);
    sprintf(cp, "echo `cat %s`", s_S(S_w));
    break;
  case M_w2:
    send_pp(pp, "w");
    cp = malloc(4 + strlen(s_S(S_w)) + 1);
    sprintf(cp, "cat %s", s_S(S_w));
    break;
  case M_chdir:
    cp = strdup(comm);
    break;
  case M_chdir2:
    cp = malloc(4 + strlen(dir) + 2 + 1);
    sprintf(cp, "cd \047%s\047\r", dir);
    break;
  }
  return cp;
}

static int m_arc(int m, int f_Mb)
{
  if ((m == M_cp && !check_chp(S_cp))
      || (m == M_mv && !check_chp(S_mv))
      || (m == M_rm && !check_chp(S_rm))) return -echo_mess(S_errc);
  if (m == M_arc 
      && (f_Mb == T_tar || f_Mb == T_lha || f_Mb == T_unzip 
	  || f_Mb == T_rpm || f_Mb == T_deb)) 
    m = T_arc2;
  else if (f_Mb == T_dir || f_Mb == T_du
	   || (m == M_rm 
	       && (f_Mb == M_lfile
		   || f_Mb == M_ldir 
		   || f_Mb == M_man
		   || f_Mb == M_prog
		   || f_Mb == M_egrep
		   || f_Mb == M_cmd
		   || f_Mb == T_cmd))) ;
  else return -echo_mess(S_errv);
  return m;
}

static int m_exit(int m, HEAP *hp, LINE *lp, ARRAY *ap, int *f_Mp, int f_comm)
{
  int i, f_M;

  f_M = (m == T_arc2) ? M_arc : m;
  while (read_stack(hp->sp, s_P(P_mode)[f_M], 1) || is_name(ap->name, f_M))
    hp->sp = load_stack(hp->sp, lp, f_Mp, ap->name, &hp->bp, hp->base);
  if (((i = *ap->name == ' ') 
       && !is_name(ap->name, M_du) && !is_name(ap->name, T_pipe2)) 
      || m == M_log || m == M_error) ;
  else if (is_inter(NULL, -1, 0))
    return echo_mess(S_errf); /* for su etc. */
  if (f_comm == 2 || m == M_exit) f_comm = 1;
  hp->sp = save_stack(hp->sp, lp, *f_Mp, ap->name, hp->bp, !f_comm);
  strcpy(lp->prev, &ap->name[i]);
  *lp->arg = K_z;
  strcpy(ap->name, s_P(P_mode)[f_M]);
  *f_Mp = m;
  return 0;
}

static int m_locate(int m, ARRAY *ap, HEAP *hp, NUM *np, LINE *lp, RV *rvp)
{
  if (!check_chp(S_locate)) return m;
  if (is_name(ap->name, M_locate)) return echo_mess(S_errv);
  if (m == M_locate2) {
    if (!hp->base[5].pp) return echo_mess(S_errv);
    strcpy(rvp->str, ap->lstr);
    m = T_locate;
  }
  clear_stack(hp, lp, ap->name, &np->M, '/');
  hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
  strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  strcpy(ap->name, s_P(P_mode)[M_locate]);
  *lp->arg = K_z;
  if ((np->M = m) == T_locate) {
    hp->bp = &hp->base[5];
    np->Mb = T_skip;
  }
  return 0;
}

static int m_grep(int m, ARRAY *ap, LINE *lp, FLAG *fp, HEAP *hp, NUM *np)
{
  if (!check_chp((m == M_fgrep) ? S_fgrep : S_egrep)) return m;
  if ((is_name(ap->name, M_hist)
       && (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep)
	   || is_prev(lp->prev, M_cut)))
      || is_name(ap->name, M_cut) 
      || is_name(ap->name, M_fgrep) 
      || is_name(ap->name, M_egrep)) return echo_mess(S_errv);
  if (fp->grep)
    clear_stack(hp, lp, ap->name, &np->M, 'g');
  fp->grep = (m == M_fgrep) ? 2 : 1;
  hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
  strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  strcpy(ap->gname, ap->name);
  strcpy(ap->name, s_P(P_mode)[m]);
  *lp->arg = K_z;
  np->M = m;
  return 0;
}

static int m_cut(int m, ARRAY *ap, HEAP *hp, NUM *np, PN *pnp, FILE **pinp, LINE *lp)
{
  int i;

  if (is_name(ap->name, M_cut)) return echo_mess(S_errv);
  if (hp->bp == &hp->base[4] && ((i = !*pinp) || (*pinp && *pinp != F1))) { 
    if (!i) {
      *pinp = seek_intr(*pinp, &pnp->nrow);
      if (*pinp) return echo_mess(S_errv);
    }
    alloc_winch(hp->bp, 1); /* set bp->size for init_cut() */
  }
  hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0); /* ==> (8) */ 
  if ((i = m == T_fcut) || m == T_fcut2) m = (i) ? M_cut2 : M_cut;
  else clear_cut(&lp->cut); /* ==> (7) */
  if (hp->ll.p.split) hp->ll.p.split = free_list(hp->ll.p.split);
  strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  strcpy(ap->name, s_P(P_mode)[M_cut]);
  lp->cut.chp = set_cutchp(lp->arg);
  *lp->arg = K_z;
  np->M = m;
  return 0;
}

static int m_man(int m, HEAP *hp, LINE *lp, int *f_Mp, char name[])
{
  if (!check_chp(S_man)) return m;
  clear_stack(hp, lp, name, f_Mp, '/');
  hp->sp = save_stack(hp->sp, lp, *f_Mp, name, hp->bp, 0);
  strcpy(lp->prev, &name[*name == ' ']);
  *lp->arg = K_z;
  strcpy(name, s_P(P_mode)[M_man]);
  *f_Mp = m;
  return 0;
}

static void m_help(int m, ARRAY *ap, LINE *lp, HEAP *hp, NUM *np)
{
  if (m == M_key || m == T_key) {
    if (is_name(ap->name, M_key)) 
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 1);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    strcpy(ap->name, s_P(P_mode)[M_key]);
  } else {
    if ((!strcmp(ap->name, s_S(S_help)) && hp->bp == &hp->base[7])
	|| read_stack(hp->sp, &hp->base[7], 0))
      clear_stack(hp, lp, ap->name, &np->M, 'h');
    else if (is_name(ap->name, M_key) || is_name(ap->name, M_usilic)) 
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    strcpy(ap->name, s_S(S_help));
  }
  *lp->arg = K_z;
  np->M = m;
  np->Mb = 0;
}

static int m_prog(int m, LINE *lp, NUM *np, HEAP *hp, char name[])
{
  np->Mb = 0;
  switch(m) {
  case M_prog2:
    if (is_inter(NULL, 0, 0)) return echo_mess(S_errf);
    np->Mb = M_prog;
  case M_lfile2:
    if (!np->Mb) np->Mb = M_lfile;
  case M_ldir2:
    if (!np->Mb) np->Mb = M_ldir;
  case M_lfile: case M_ldir:
  case M_prog:
    if (!np->Mb) np->Mb = m;
    if (is_fm() && *name) clear_stack(hp, lp, name, &np->M, '/');
    hp->sp = save_stack(hp->sp, lp, np->M, name, hp->bp, 0);
    strcpy(lp->prev, &name[*name == ' ']);
    *lp->arg = K_z;
    strcpy(name, s_P(P_mode)[np->Mb]);
    np->M = m;
    np->Mb = 0;
  }
  return 0;
}

static int m_list(int m, HEAP *hp, NUM *np, ARRAY *ap, LINE *lp)
{
  int f_fgrep;

  if (m == T_hist) {
    if (is_name(ap->name, M_lfile)) m = T_hfile;
    else if (is_name(ap->name, M_ldir)) m = T_hdir;
    else if (is_name(ap->name, M_man)) m = T_hman;
    else if (is_name(ap->name, M_locate)) m = T_hlocate;
    else if (((f_fgrep = is_name(ap->name, M_fgrep)) 
	      || is_name(ap->name, M_egrep) 
	      || is_name(ap->name, M_cut))) {
      if (is_prev(lp->prev, M_hist)) return echo_mess(S_errv);
      m = (f_fgrep) ? T_hfgrep : T_hegrep;
    } else if (is_inter(NULL, 0, 0)) return echo_mess(S_errf);
  } else if (m == T_cmd && is_name(ap->name, M_hist)) 
    return echo_mess(S_errf);
  else if (m != T_list && is_inter(NULL, 0, 0)) echo_mess(S_errf);
  if (m == T_list || m == T_cmd || m == T_hist
      || m == T_hfile || m == T_hdir || m == T_hman 
      || m == T_hlocate || m == T_hfgrep || m == T_hegrep) {
    clear_stack(hp, lp, ap->name, &np->M, 'g');
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
  }
  switch(m) {
  case T_hfile:
  case T_hdir:
  case T_hman:
  case T_hlocate:
  case T_hfgrep:
  case T_hegrep:
    np->Mb = M_hist;
    break;
  case T_list:
    np->Mb = M_list;
    break;
  case T_cmd:
    np->Mb = M_cmd;
    break;
  case T_hist:
    np->Mb = M_hist;
    break;
  default:
    np->Mb = m;
    break;
  }
  *lp->arg = K_z;
  strcpy(ap->name, s_P(P_mode)[np->Mb]);
  np->M = m;
  np->Mb = 0;
  return 0;
}

static int m_edit(int line, char name[], char arg[], HEAP *hp, NUM *np)
{
  int m;

  if ((m = do_edit(line, name, arg, np->M))) return m;
  np->Mb = 0;
  free_bp(&hp->base[1]); /* ==> (3) */
  if ((hp->bp && hp->bp < &hp->base[0]) || hp->bp > &hp->base[N_base-1]) {
    free_bp(hp->bp);
    free(hp->bp);
  }
  return m;
}

static int m_shell(int m, HEAP *hp, LINE *lp, NUM *np, ARRAY *ap, PN *pnp, FILE **pinp, FLAG *fp, struct stat st0)
{
  static char name[N_line];
  int i;
  struct stat st;
  FILE *pin;
  STACK *sp;

  if ((pin = *pinp) != F1) 
    if ((hp->bp == &hp->base[4] && is_name(ap->name, T_pipe2))
	|| ((sp = read_stack(hp->sp, &hp->base[4], 0))
	    && is_name(sp->name, T_pipe2))) {
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      clear_stack(hp, lp, ap->name, &np->M, 'p');
    }
  switch(np->Mb) {
  case M_list:
  case M_hist:
  case M_cmd:
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
  case T_cmd:
  case T_hist:
  case M_prog:
  case M_prog2:
    clear_stack(hp, lp, ap->name, &np->M, 'p');
    if (np->Mb != M_list) {
      save_ll(np->ll, &hp->ll, &hp->ss, ap->full, pnp);
      fp->comm = 3;
    }
    break;
  default:
    save_ll(np->ll, &hp->ll, &hp->ss, ap->full, pnp);
    if ((i = is_comm(np->Mb, 2)) || np->Mb == T_arc3) {
      if (!i)
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    } else {
      if (hp->bp != &hp->base[4]) 
	clear_stack(hp, lp, ap->name, &np->M, 'P'); /* clear only "(stdin)" */
      if (np->Mb != M_du) fp->comm = 0;
    }
    *ap->mess = '\0';
  }
  np->ll = -1;
  strcpy(name, &ap->name[*ap->name == ' ']);
  np->Mb = M_shell;
  if (load_intr() == S_errg) unlink(s_S(S_intr));
  m = do_shell(name, ap->mess, pinp, hp->w, &np->ppid, hp->ll.p.prog, 
	       &hp->ll.p.list, &hp->ss.p.cmd, &hp->ss.p.hist, fp->comm);
  vt_mode('d');
  if (hp->w) {
    free(hp->w);
    hp->w = NULL;
  }
  if (fp->comm) fp->comm = 0;
  do {
    switch(-m) {
    case M_cmd:
      make_cmd(hp->ss.p.cmd, &hp->ll.p.cmd);
    case M_hist:
    case M_list:
      hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
      strcpy(lp->prev, &s_P(P_mode)[M_shell][1]);
      continue;
      break;
    case T_dir:
      check_dir(hp, lp, ap->mess, &hp->ll.p.dir);
      strcpy(ap->name, ap->mess);
      stat(ap->name, &st);
      free_bp(hp->bp);
      break;
    case T_file:
      strcpy(ap->name, name);
      check_dir(hp, lp, ap->mess, &hp->ll.p.dir);
      hp->sp = save_stack(hp->sp, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, ap->mess);
      fp->cr = 1;
      np->Mb = 0;
      break;
    case T_pipe2:
      if (hp->bp == &hp->base[4]) /* ^O^M or MAN */
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
      sprintf(lp->prev, "%s", &ap->name[*ap->name == ' ']);
      strcpy(ap->name, s_P(P_mode)[T_pipe2]);
      *lp->arg = K_z;
      np->Mb = 0;
      break;
    case M_bl:
      if (vt_mode('W')) fp->comm = 4;
      m = -T_disp;
      if (np->M == T_dir) {
	chdir(ap->name);
	stat(ap->name, &st);
	if (st.st_ctime != st0.st_ctime || st.st_ino != st0.st_ino) 
	  free_bp(hp->bp);
      }
      continue;
      break;
    case T_winch:
    case M_hs:
      fp->comm = 4;
      return m;
    case M_du:
      clear_stack(hp, lp, ap->name, &np->M, K_z);
      if (strcmp(lp->dir, ap->mess)) {
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->mess);
	strcpy(ap->name, ap->mess);
      }
    default:
      if (-m >= M__0 && -m <= M__19) { 
	char *p = alloca(4 + 1);
	strcpy(p, mtoa(-m));
	read_m(NULL, 3, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
	m = -T_disp;
      }
      continue;
      break;
    }
    np->M = -m;
    m = 0;
  } while (0);
  return m;
}

static int m_du(int m, HEAP *hp, ARRAY *ap, NUM *np, LINE *lp, FILE **pinp, struct stat *st0p)
{
  if (m == M_du2 && !hp->base[2].pp) return echo_mess(S_errv);
  alloc_pipe(-1, pinp, NULL, NULL, 0);
  clear_stack(hp, lp, ap->name, &np->M, K_z);
  if (m == M_du2) {
    stat(lp->dir, st0p);
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, ap->du);
    strcpy(ap->name, ap->du);
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    chdir(ap->du);
    strcpy(lp->prev, ap->du);
    strcpy(ap->name, s_P(P_mode)[M_du]);
    np->Mb = T_skip;
    np->M = T_du;
    hp->bp = &hp->base[2];
  } else {
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, ap->du);
    chdir(ap->du);
    strcpy(ap->name, s_P(P_mode)[M_du]);
    np->M = M_du;
  }
  return 0;
}

static int m_yn(int m, PP *pp, int f_Mb, u_char prev[])
{
  int i, j;
  char *cp;

  if (!is_comm(f_Mb, 0)) return echo_mess(S_errv);
  for (; pp; pp = pp->next) 
    if (m == M_yes) {
      pp->split = -f_Mb;
      if (is_prev(prev, M_du)) j = ichrstr(pp->cs, 0, "./");
      else j = get_col(pp->cs) - 1;
      j = strlen((cp = &pp->cs[j]));
      if (f_Mb == M_rm && *prev != '/') i = 1;
      else i = (f_Mb == M_mv || f_Mb == M_rm) ? is_write(&cp[1], 0) : 1;
      if (i <= 0 || !strcmp(&cp[1], "./") || !strcmp(&cp[1], "../"))
	pp->split = 0;
    } else pp->split = 0;
  return -M_clear;
}

static int m_dest(int m, LINE *lp, RV *rvp, NUM *np, HEAP *hp, ARRAY *ap, FLAG *fp)
{
  FILE *tp;
  int i, j, fd;
  u_char *p, tmp[N_line];
  struct stat st;

  if ((i = is_comm(np->Mb, 0)) || is_comm(np->Mb, 1)) {
    if (i) {
      np->Mb = i;
      disp_prev(lp->prev, lp->dir, np->klen);
    } else {
      if (np->Mb != T_rm) {
	while ((p = strstr(ap->mess, "//"))) strcpy(ap->mess, &p[1]);
	if (*ap->mess == '~') {
	  strcpy(tmp, getenv("HOME"));
	  if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
	  if (strlen(ap->mess) > 1) 
	    strcat(tmp, &ap->mess[1 + (ap->mess[1] == '/')]);
	  strcpy(ap->mess, tmp);
	} else strcpy(tmp, ap->mess);
	j = 1;
	if (np->Mb == T_cp || np->Mb == T_mv) {
	  if (np->Mb == T_mv && access(lp->dir, W_OK) < 0) j = -EACCES;
	  else {
	    PP *pp;
	    int i_mark;
	    for (i_mark = 0, pp = hp->bp->pp; i_mark < 2 && pp; pp = pp->next) 
	      if (pp->split < 0) i_mark++;
	    if (strchr(tmp, '\0')[-1] != '/') {
	      if (!access(tmp, F_OK)) {
		stat(tmp, &st);
		if (!S_ISDIR(st.st_mode)) get_dir(tmp);
		else if (strcmp(tmp, "/")) strcat(tmp, "/");
	      } else if (!strchr(tmp, '/')) strcpy(tmp, ".");
	      else get_dir(tmp);
	    }
	    if (access(tmp, W_OK)) j = -EACCES;
	  }
	} else if (np->Mb == T_arc || np->Mb == T_arc3) {
	  if (!getuid()) {
	    if (stat(tmp, &st) >= 0 && !S_ISDIR(st.st_mode))/* Not directory */
	      j = 0;
	  } else j = is_write(tmp, 1);
	}
	if (j <= 0) {
	  save_intr(j);
	  np->Mb = T_skip;
	  return 0;
	}
	if (np->Mb == T_arc3) {
	  clear_stack(hp, lp, ap->name, &np->M, 'g');
	  hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	  strcpy(&strchr(ap->mess, '\0')[1], ap->name);
	}
      } else if (np->Mb == T_rm) {
	if (*ap->mess != 'y') {
	  disp_prev(lp->prev, lp->dir, 0);
	  np->Mb = M_rm;
	  return 0;
	}
      } else if (((i = nmark(hp->bp->pp, np->Mb)) > 1 
		  && (j = is_write(ap->mess, 1)) <= 0)
		 || (i == 1 && (j = is_write(ap->mess, -1)) < 0)) {
	save_intr((j < -128) ? - (j - 128) : ((j) ? j : S_err7));
	disp_prev(lp->prev, lp->dir, 0);
	np->Mb = is_comm(np->Mb, 1);
	return 0;
      }
      clear_stack(hp, lp, ap->name, &np->M, 'g');
      if (!(hp->w = command(np->Mb, ap->mess, hp->bp, lp, np->klen
			    , ap->du, &fd))) {
	tp = fdopen(fd, "r");
	/* (4) RM at EGREP */
	if ((m = do_rm(&hp->ll, &hp->ss, lp->prev, tp)) == -M_egrep
	    || m == -T_cmd2)
	  hp->sp = load_stack(hp->sp, lp, &np->M
			      , ap->name, &hp->bp, hp->base);
	hp->sp = load_stack(hp->sp, lp, &np->M
			    , ap->name, &hp->bp, hp->base);
	fclose(tp);
      } else m = -M_shell;
      np->clen = 0;
    }
  }
  return m;
}

static FILE *m_pipe(NUM *np, ARRAY *ap, LINE *lp)
{
  int f_chp;
  CHILD *chp;
  FILE *pin;

  np->M = 0;
  if (!is_prev(lp->prev, M_du)) sprintf(lp->prev, "%s", lp->dir);
  *lp->arg = K_z;
  strcpy(ap->mess, ap->name);
  if ((chp = check_ext(ap->name, NULL, 0)) && (f_chp = is_chp(chp, NULL))) {
    pin = fork_pipe(f_chp, chp, ap->name);
    np->M = T_pipe3;
  } else {
    if (chp) chp = NULL;
    pin = fopen(ap->name, "r");
  }
  if (!np->M) np->M = T_pipe3;
  np->Mb = 0;
  return pin;
}

static int m_cr(int m, NUM *np, HEAP *hp, LINE *lp, ARRAY *ap, RV *rvp, FLAG *fp, PN *pnp, FILE **pinp)
{
  int i, j, size;
  u_char *cp;

  if (!*pinp && m == M_fp) { /* ^O^M after ^O^M ^J^C */
    echo_mess(S_errv);
    return m;
  } else if (m == M_fcut || m == M_fcut2)
    switch(np->Mb) {
    case T_tar: case T_lha: case T_unzip: 
    case T_rpm: case T_deb:
    case T_dir: case T_du: case T_locate:
      break;
    default:
      echo_mess(S_errv);
      return m;
      break;
    }
  else if (!strlen(rvp->str) && !is_comm(np->Mb, 0)) return m;
  i = 0;
  switch(np->Mb) {
  case M_exit:
    if (*rvp->str == 'y') {
      np->Mb = 0;
      return -T_exit;
    } else {
      disp_prev(lp->prev, lp->dir, 0);
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      np->Mb = T_skip;
      return 0;
    }
    break;
  case T_cmd: case T_hist:
    i = np->Mb;
  case M_list: case M_cmd: case M_hist:
    clear_stack(hp, lp, ap->name, &np->M, 'g');
    if (i)
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    strcpy(ap->mess, rvp->str);
    return -M_shell;
  case T_list: case T_hfile: case T_hdir: case T_hman:
    clear_stack(hp, lp, ap->name, &np->M, 'g');
    strcpy(ap->iname, rvp->str);
    j = np->Mb;
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if ((i = is_comm(np->M, 0)) || (is_comm(np->M, 1))) {
      if (i) np->M = np->Mb = i;
      else np->Mb = np->M;
      strcat(ap->mess, rvp->str);
      *rvp->str = '\0';
      m = -T_disp;
    } else {
      m = -np->M;
      if (j != T_list)
	read_m(NULL, 3, 0, "\r", NULL, NULL, NULL, NULL, NULL, NULL);
      else if ((j = is_name(ap->name, M_hist)) || is_name(ap->name, M_cmd)) {
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	if (j) m = (is_prev(lp->prev, M_shell)) ? -M_hist : -T_hist;
	else m = (is_prev(lp->prev, M_shell)) ? -M_cmd : -T_cmd;
      }
    }
    return m;
  case M_key:
    if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))
      clear_stack(hp, lp, ap->name, &np->M, 'g');
    if (rvp->pp->tell) {
      u_char *p = alloca(4 + 4 + 1);
      strcpy(p, mtoa(M_bl));
      if (rvp->pp->tell != M_bl) strcat(p, mtoa(rvp->pp->tell));
      read_m(NULL, 3, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    return 0;
  case M_man: case M_man2:
    if (hp->bp == &hp->base[4] && *pinp != F1) 
      alloc_pipe(-1, pinp, NULL, NULL, 0);
    clear_stack(hp, lp, ap->name, &np->M, 'p');
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    strcpy(ap->name, rvp->str);
    *lp->arg = K_z;
    np->M = T_man;
    np->Mb = 0;
    return 0;
    break;
  case T_hlocate:
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
  case M_locate:
    if (np->Mb == M_locate) 
      hp->ss.p.hlocate = save_history(hp->ss.p.hlocate, rvp->str, 1);
    strcpy(ap->lstr, rvp->str);
    np->M = T_locate;
    return 0;
  case T_tar: case T_lha: case T_unzip: case T_rpm: case T_deb:
    if ((i = m != M_fcut && m != M_fcut2)) 
      fp->cr = (m == M_cr) ? 1 : ((m == M_fp) ? 2 : 0);
    else {
      if (lp->cut.buff) free(lp->cut.buff);
      if (np->Mb == T_rpm || (size = rvp->pp->split) < 0) 
	lp->cut.buff = read_arc(hp->arc, ap->name, rvp->str, &size);
      else {
	if (size > hp->bsize) size = hp->bsize;
	while (!(lp->cut.buff = malloc(size)) && (size /= 2)) ; 
	memcpy(lp->cut.buff, (u_char*)rvp->pp->tell, size);
      }
      sprintf(lp->cut.name, " %d", size);
      fp->cut = (m == M_fcut2) ? 2 : 1;
    }
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp, i);
    break;
  case M_egrep:
    if (!load_list(hp->ll.p.grep, rvp->str, NULL)) {
      hp->ll.p.grep = save_list(hp->ll.p.grep, rvp->str, pnp);
      if (is_fm())
	hp->ss.p.grep = save_str(hp->ss.p.grep, rvp->str, pnp);
    }
  case T_hfgrep:
  case T_hegrep:
  case M_fgrep:
    if ((i = np->Mb == T_hfgrep) || np->Mb == T_hegrep) {
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      np->Mb = (is_name(ap->name, M_cut)) ? M_cut : ((i) ? M_fgrep : M_egrep);
    }
    if (np->Mb != M_cut) {
      if (is_name(ap->name, M_fgrep))
	hp->ss.p.hfgrep = save_history(hp->ss.p.hfgrep, rvp->str, 1);
      else
	hp->ss.p.hegrep = save_history(hp->ss.p.hegrep, rvp->str, 1);
      strcpy(ap->gstr, rvp->str);
      if (np->Mb != M_cut) strcpy(ap->name, ap->gname);
      break;
    }
  case M_cut: case M_cut2:
    hp->ss.p.hegrep = save_history(hp->ss.p.hegrep, rvp->str, 1);
    if (np->Mb == M_cut) {
      hp->ll.p.grep = save_list(hp->ll.p.grep, rvp->str, pnp);
      if (is_fm())
	hp->ss.p.grep = save_str(hp->ss.p.grep, rvp->str, pnp);
    }
    i = *lp->cut.name; /* (7) */
    hp->sp = load_stack(hp->sp, lp	/* load lp->cut */
			, &np->M, ap->name, &hp->bp, hp->base);
    j = *lp->cut.name; /* (8) */
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 1);
    strcpy(lp->cut.str, rvp->str);
    if (!i && j) *lp->cut.name = '\0'; /* !i : ^J^C, j : ^J^B */
    np->M = T_cut;
    return 0;
  case T_grep:
    np->tell = rvp->pp->tell;
    strcpy(rvp->str, ap->gstr);
    if (is_name(ap->name, M_fgrep))
      hp->ss.p.hfgrep = save_history(hp->ss.p.hfgrep, rvp->str, 1);
    else
      hp->ss.p.hegrep = save_history(hp->ss.p.hegrep, rvp->str, 1);
    break;
  case T_locate:
    if ((i = m == M_fcut) || m == M_fcut2) {
      fp->cut = (i) ? 1 : 2;
      break;
    }
  case M_ldir:
  case M_ldir2:
  case M_lfile:
  case M_lfile2:
    if (*(cp = &strchr(rvp->str, '\0')[-2]) == '/' && cp[1] == '~') 
      strcpy(rvp->str, &cp[1]);
    else if ((cp = strchr(rvp->str, '~')) && cp != rvp->str 
	     && cp[-1] == '/' && (!cp[1] || cp[1] == '/'))
      strcpy(rvp->str, cp);
    if (*rvp->str != '/') {
      if (*rvp->str == '~') 
	sprintf(ap->name, "%s%s", s_S(S_home), &rvp->str[1]);
      else sprintf(ap->name, "%s%s", lp->dir, rvp->str);
      strcpy(rvp->str, ap->name);
    } else strcpy(ap->name, rvp->str);
    if ((cp = strstr(rvp->str, " ("))) *cp = '\0';
    lp->cut.chp = NULL;
    if (set_cut(hp, &lp->cut, rvp->str, ap->name, lp->dir, lp->old)) {
      *lp->prev = K_z;
      hp->grep = NULL;
      hp->sp = save_stack(NULL, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, lp->dir);
      np->M = T_cut;
      return 0;
    }
    if (np->Mb == M_lfile) fp->cr = (strstr(ap->name, " (")) ? 3 : 1;
    else fp->cr = (np->Mb == T_locate && m == M_fp) ? 2 : 1;
    break;
  case T_dir:
  case T_cut:
    if ((i = m == M_fcut) || m == M_fcut2) fp->cut = (i) ? 1 : 2;
    fp->cr = (m == M_cr) ? 1 : ((m == M_fp) ? 2 : 0);
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp
			, (np->Mb == T_cut));
    break;
  case T_du:
    strcpy(rvp->str, ap->du);
  case M_prog:
  case M_prog2:
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp
			, (np->Mb != M_prog2));
    break;
  default:
    if (is_comm(np->Mb, 0)) {
      if (is_prev(lp->prev, M_du)) i = ichrstr(rvp->pp->cs, 0, "./");
      else if (np->Mb == T_arc2 || *lp->prev == '/') 
	i = get_col(rvp->pp->cs) - 1;
      else i = 0;
      i = strlen((cp = &rvp->pp->cs[i]));
      if (rvp->pp->split == -np->Mb) rvp->pp->split = 0;
      else rvp->pp->split = -np->Mb;
      if (rvp->pp->split) {
	if (np->Mb == M_rm && *lp->prev != '/') np->clen = 0;
	else {
	  j = (np->Mb == M_mv || np->Mb == M_rm) ? is_write(&cp[1], 0) : 1;
	  if (j <= 0 || !strcmp(&cp[1], "./") || !strcmp(&cp[1], "../")) {
	    vt_putchar(K_g);
	    np->clen -= i;
	    rvp->pp->split = 0;
	  } else np->clen += i;
	}
      } else np->clen -= i;
      return -M_clear;
    } else if (m == M_cr && (is_comm(np->Mb, 1))) {
      strcpy(ap->mess, rvp->str);
      if (np->Mb == T_arc3) strcpy(&strchr(ap->mess, '\0')[1], ap->name);
      return -M_dest;
    }
    if (!strlen(rvp->str)) {
      if (m == M_code && np->Mb == T_file && strstr(lp->arg, "(file)")) {
	*lp->arg = K_z;
	fp->cr = 0;
	np->M = T_file;
	m = 0;
      }
      return m;
    }
    hp->sp = save_stack(hp->sp, lp, np->Mb, ap->name, hp->bp, 1);
    break;
  }
  if (!(m = mode(hp, np, ap, lp, rvp)))
    switch(np->M) {
    case T_buff:
      if (fp->cut) {
	*lp->cut.name = ' ';
	m = -((fp->cut == 2) ? T_fcut : T_fcut2);
	fp->cut = 0;
	np->M = np->Mb;
	return m;
      }
      break;
    case T_file2:
    case T_file:
    case T_dir:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      if (!access(lp->dir, F_OK)) 
	check_dir(NULL, lp, NULL, &hp->ll.p.dir);
      if (fp->cut) {
	strcpy(lp->cut.name, ap->name);
	*lp->arg = K_z;
	m = -((fp->cut == 2) ? T_fcut : T_fcut2);
	fp->cut = 0;
	return m;
      }
      if (fp->cr == 2 && np->M != T_dir) *pinp = m_pipe(np, ap, lp);
      break;
    case T_grep:
      if (hp->bp == &hp->base[4] && *pinp && *pinp != F1) {
	pnp->nrow = hp->bp->nrow;
	*pinp = seek_intr(*pinp, &pnp->nrow);
	print_intr(NULL, NULL);
	if (hp->bp->nrow < pnp->nrow) hp->bp->nrow = pnp->nrow;
      }
      strcpy(hp->grep->str, ap->gstr);
      break;
    case M_shell:
      m = -M_shell;
      break;
    }
  else
    switch(-m) {
    case M_edit:
    case M_w:
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
      break;
    case T_prog:
      if (np->M == T_pipe5) *pinp = m_pipe(np, ap, lp);
      else {
	u_char *p = alloca(4 + 4 + 1);
	strcpy(p, mtoa(M_sel));
	strcat(p, mtoa(M_bl2));
	read_m(NULL, 2, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
      }
      np->Mb = 0;
      m = 0;
      break;
    case T_skip:
      np->Mb = T_skip;
      m = 0;
    case M_clear:
      break;
    }
  return m;
}

static void m_clear(RV *rvp, PN *pnp, LINE *lp, NUM *np, ARRAY *ap, HEAP *hp, FLAG *fp)
{
  vt_mode('d');
  print_intr(F8, NULL);
  if (rvp->row > vt_row(0) - 2) pnp->row = rvp->row = vt_row(0) - 2;
  disp_window(rvp->ps, rvp->pp, np->M, ap->name, lp->prev, lp->arg, &np->klen);
  if (np->Mb != M_locate && np->Mb != T_locate && np->Mb != M_exit)
    print_line(rvp->ps, rvp->pp
	       , (hp->bp == &hp->base[4]) ? -np->nlen : np->nlen, NULL);
  print_intr(NULL, (*ap->intr) ? ap->intr : NULL);
  if (*ap->intr) *ap->intr = '\0';
}

static int m_bl(int m, NUM *np, ARRAY *ap, LINE *lp, HEAP *hp, FLAG *fp, FILE **pinp, PN *pnp, struct stat st0)
{
  int i;
  char tmp[N_line];
  struct stat st;

  do {
    switch(np->Mb) {
    case M_list: case M_cmd: case M_hist:
      if (is_prev(lp->prev, M_shell)) {
	*ap->mess = '\0';
	return -M_shell;
      }
      break;
    case T_list: case T_cmd: case T_hist:
    case T_hfile: case T_hdir:
    case T_hman: case T_hlocate:
    case T_hfgrep: case T_hegrep:
      break;
    default:
      if ((i = is_comm(np->Mb, 2))) {
	disp_prev(lp->prev, lp->dir, 0);
	np->Mb = i;
	return m;
      }
      continue;
      break;
    }
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if ((i = is_comm(np->M, 0)) || is_comm(np->M, 1)) {
      if (i) np->M = np->Mb = i;
      else {
	np->Mb = np->M;
	*ap->iname = K_a0;
      }
      m = -T_disp;
    } else m = -T_disp;
    return m;
  } while (0);
  if (is_pg() && !hp->sp) return m;
  if (hp->sp) {
    *tmp = '\0';
    switch(np->M) {
    case M_cut2:
    case T_cut:
    case M_cut:
#if 1
      if ((i = *lp->cut.name)) {
	if (hp->bp != &hp->base[7]
	    && !is_prev(lp->prev, M_egrep) 
	    && !is_prev(lp->prev, M_fgrep))
	  hp->sp = load_stack(hp->sp, lp, &np->M
			      , ap->name, &hp->bp, hp->base);
      }
#endif
      break;
    case T_grep:
      if ((hp->grep = load_grep(hp->grep))) strcpy(ap->gstr, hp->grep->str);
      pnp->row = pnp->top = 0;
      break;
    case T_man:
      if (!is_prev(lp->prev, M_egrep) && !is_prev(lp->prev, M_fgrep))
	alloc_pipe(-1, pinp, NULL, NULL, 0);
      if (!pnp->nrow) np->ll = -1;
      break;
    case T_pipe4:
    case T_pipe5:
      if (!is_prev(lp->prev, M_egrep) && !is_prev(lp->prev, M_fgrep))
	alloc_pipe(-1, pinp, NULL, NULL, 0);
      pnp->row = pnp->top = 0;
      break;
    case T_dir:
      strcpy(tmp, ap->name);
      break;
    }
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if ((is_prev(lp->prev, M_egrep) || is_prev(lp->prev, M_fgrep))
	&& !hp->bp->pp)
      clear_stack(hp, lp, ap->name, &np->M, 'g');
    if (is_name(ap->name, M_shell)) return -M_shell;
    if ((is_fm()) 
	&& !strcmp(ap->name, s_S(S_help))) np->Mb = T_skip;
    else if (np->M == T_du) {
      if (strcmp(lp->dir, ap->du)) {
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->du);
      }
      chdir(lp->dir);
    } else if (*lp->prev == K_z && hp->bp == &hp->base[1]) {
      clear_cut(&lp->cut);
      if (*tmp && !strcmp(tmp, ap->name)) {
	strcpy(ap->name, lp->old);
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->name);
      }
      stat(ap->name, &st);
      np->Mb = T_skip;
      do {
	if (fp->winch) {
	  fp->winch = 0;
	  continue;
	} else if (!hp->bp->pp) ; /* (3) */
	else if (st.st_ctime == st0.st_ctime && st.st_ino == st0.st_ino)
	  continue;
	np->Mb = 0;
      } while (0);
    } else np->Mb = (hp->bp && hp->bp != &hp->base[0]) ? T_skip : 0;
  } else if (!strcmp(lp->old, lp->dir) 
	     && !strcmp(lp->dir, ap->name)) {
    getcwd(tmp, N_line);
    if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
    if (strcmp(lp->dir, tmp)) chdir(lp->dir);
    np->Mb = T_skip;
  } else {
    strcpy(ap->name, lp->old);
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, ap->name);
  }
  return 0;
}

static int get_m(HEAP *hp, ARRAY *ap, FLAG *fp, NUM *np, LINE *lp, RV *rvp, FILE **pinp, PN *pnp, CHILD *chp)
{
  char tmp[N_line];
  int i, m;
  PN pn_bak;
  STR *sp;
  static struct stat st0;
  static PN pn_mark;

  if (np->M == T_dir) stat(ap->name, &st0);
  do {
    switch(np->Mb) {
    case T_exec:
      m = -M_bl;
      break;
    case M_copy:
      make_map(rvp, np->nlen);
    default:
      m = read_m(rvp, np->Mb, np->klen, NULL, ap->cname
		 , &hp->ll.p.list, hp->ss.p.hist, ap->iname, lp->dir, lp->old);
      break;
    }
    do {
      if (m < 0) m = -m;
      else if (m > N_num) {
	if (hp->bp->pp->f.code) echo_mess(S_errv);
	else {
	  m_num(np, m, hp, pinp, pnp, ap->full);
	  m = -T_disp;
	}
	continue;
      }
      switch(m) {
      case 0:
      case M_clear: /* ^L */
	m_clear(rvp, pnp, lp, np, ap, hp, fp);
	break;
      case T_0: case T_1: case T_2: case T_3: case T_4: /* ^J? */
      case T_5: case T_6: case T_7: case T_8: case T_9: 
      case M_m: /* ^O^K */
      case M_sel: /* ^J^K */
	m = m_dir(m, hp, lp, ap->name, rvp, ap, &np->M);
	break;
      case M_cp: /* ^X^Kc */
      case M_mv: /* ^X^Km */
      case M_rm: /* ^X^Kr */
      case M_arc: /* ^X^Ka */
	fp->comm = 1;
      case M_cp2: /* ^Xc */
      case M_mv2: /* ^Xm */
      case M_rm2: /* ^Xr */
      case M_arc2: /* ^Xa */
	if (is_inter(NULL, 0, 0)) {
	  echo_mess(S_errf);
	  continue;
	}
	if (!is_comm(m, 0)) {
	  fp->comm = 2;
	  m = is_comm(m, 3);
	}
	if ((i = m_arc(m, np->Mb)) < 0) break;
	m = i; 
      case M_usilic: /* ^T */
      case M_error: /* ^X^Ke */
      case M_log: /* ^X^Kl */
      case M_exit: /* ^X^C */
	if ((m = m_exit(m, hp, lp, ap, &np->M, fp->comm)) && fp->comm) 
	  fp->comm = 0;
	break;
      case T_exit:
	vt_move(vt_row(0) - 1, 0);
	return -1;
	break;
      case M_mark: /* ^Space */
	memcpy(&pn_mark, pnp, sizeof(PN));
	if (np->Mb != M_locate && np->Mb != T_locate) {
	  disp_window(rvp->ps, rvp->pp, np->M, NULL, NULL, NULL, &i);
	  print_line(F2, NULL, 0, NULL);
	}
	break;
      case M_jump: /* ^X^X */
	memcpy(&pn_bak, pnp, sizeof(PN));
	memcpy(pnp, &pn_mark, sizeof(PN));
	memcpy(&pn_mark, &pn_bak, sizeof(PN));
	m = -T_disp;
	continue;
      case M_prev: case M_next: /* k, j */
      case M_up: case M_dn: /* ^P, ^N */
      case M_kprev: case M_knext: /* left-arrow, right-arrow */
      case M_kup: case M_kdn: /* up-arrow, down-arrow */
	m_row(m, hp, rvp, pnp, np, pinp);
	break;
      case M_forw2: case M_back2:
	m = (m == M_forw2) ? M_forw : M_back;
      case M_forw: case M_back: case M_kforw: case M_kback:
      case M_head: case M_tail:
	/* ` ', b, ^V, ^[v, ^[<, ^[> */
	m_page(m, hp, rvp, pnp, np, pinp);
	break;
      case M_w4: /* ^X^W */
	i = send_pp(hp->bp->pp, "a");
	save_intr(S_erra);
	sprintf(tmp, " %s; %d line(s)", s_S(S_write), i);
	print_intr(NULL, tmp);
	continue;
	break;
      case M_w: /* ^Jw */
      case M_w2: /* ^J^W */
      case M_chdir: /* ^J, */
      case M_chdir2: /* ^J. */
	if (is_inter(NULL, 0, 0)) echo_mess(S_errf);
	else if ((hp->w = m_w(m, hp->bp->pp, ap->name, lp->dir, ap->comm))) {
	  *ap->mess = '\0';
	  m = -M_shell;
	}
	break;
      case M_locate2: /* ^Jl */
      case M_locate: /* ^J^L */	
	m = m_locate(m, ap, hp, np, lp, rvp);
	break;
      case M_fgrep: /* ^S */
      case M_egrep: /* ^J^S */
	m = m_grep(m, ap, lp, fp, hp, np);
	break;
      case T_fcut2:
      case T_fcut:
      case M_cut2: /* ^Jc */
      case M_cut: /* ^J^C */
	m = m_cut(m, ap, hp, np, pnp, pinp, lp);
	break;
      case M_man: /* ^J^M */
      case M_man2: /* ^Jm */
	m = m_man(m, hp, lp, &np->M, ap->name);
	break;
      case M_quit: /* q */
	vt_move(vt_row(0) - 1, 0);
	return -1;
	break;
      case M_ls: /* ^Ol */
	if (*lp->prev != K_z) {
	  echo_mess(S_errv);
	  continue;
	}
	fp->ls = !fp->ls;
	conf_ls(NULL, utoa(fp->ls));
	m = 0;
	break;
      case M_hs: /* ^Oh */
	if (!vt_mode('D')) vt_mode('d');
	if (vt_mode('H')) {
	  vt_mode(-'h');
	  vt_cs(vt_row(0) - 2, 0);
	} else {
	  vt_mode('h');
	  vt_cs(vt_row(0) - 1, 0);
	}
	m = -((fp->comm == 4) ? M_shell : M_clear);
	continue;
	break;
      case T_winch:
	fp->winch = 1; 
	if (np->M == T_dir || is_str(np->M, &i) || is_list(np->M, &i)) {
	  m = (fp->comm == 4) ? T_winch : -T_disp;
	  free_bp(hp->bp);
	} else {
	  i = 0;
	  if (np->M == T_tar || np->M == T_lha || np->M == T_unzip
	      || np->M == T_rpm || np->M == T_deb)
	    hp->bp->pp = alloc_head(NULL, -1, 0, NULL); /* (5) */
 	  else if (!init_col(np->M, NULL, 0)) { 
	    free_pp(hp->bp->pp);
	    hp->bp->pp = alloc_pp(hp->bp->buff, -1
				  , hp->bp->size, NULL, ap->name); /* (6) */
	  } else {
	    i = hp->bp == &hp->base[4];
	    pnp->nrow = alloc_winch(hp->bp, i);
	  }
	  if (i) {
	    if (*pinp && *pinp != F1)
	      for (m = vt_row(0) - pnp->nrow; m > 0 && *pinp; m--)
		alloc_pipe(0, pinp, &pnp->nrow, NULL, 0);
	    else i = 0;
	  }
	  set_rv(np, pnp, rvp, hp
		 , ((m = pnp->nrow - (vt_row(0) - !i)) > 0) ? m : 0);
	  if (rvp->col < 0) rvp->col = 0;
	  m = (fp->comm == 4) ? T_winch : -M_clear;
	}
	if (fp->comm == 4) {
	  fp->comm = 0;
	  read_m(NULL, 2, 0, mtoa(M_shell)
		 , NULL, NULL, NULL, NULL, NULL, NULL);
	}
	continue;
	break;
      case M_help: /* ^H */
	if (is_pg()) m = M_key;
      case M_key:
	m_help(m, ap, lp, hp, np);
	m = 0;
	break;
      case M_copy: /* ^Oc */
	m_copy(lp, np, hp, ap->name);
	m = 0;
	break;
      case M_prog2: /* ^Jp */
      case M_lfile2: /* ^Xf */
      case M_ldir2: /* ^Xd */
      case M_lfile: /* ^X^F */
      case M_ldir: /* ^X^D */
      case M_prog: /* ^J^P */
	m = m_prog(m, lp, np, hp, ap->name);
	break;
      case T_cmd2:
      case T_cmd3:
      case T_cmd:
	if (m != T_cmd2) make_cmd(hp->ss.p.cmd, &hp->ll.p.cmd);
	m = T_cmd;
      case M_hist: /* ^R */
      case T_hist2:
	if (m == T_hist2) m = T_hist;
      case T_hist:
      case M_cmd: /* ^J^R */
      case T_list:
      case M_list: /* ^I */
	m = m_list(m, hp, np, ap, lp);
	break;
      case M_edit: /* ^Je */
	if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))
	  clear_stack(hp, lp, ap->name, &np->M, 'g');
	m = m_edit(pnp->line, ap->name, lp->arg, hp, np);
	break;
      case M_hup: /* ^Xs */
	if (is_inter(NULL, 0, 0)) echo_mess(S_errf);
	else {
	  is_inter(F12, 0, 0);
	  m = -M_shell;
	}
	break;
      case M_shell: /* ^[s */
	strcpy(rvp->str, "");
	m = m_shell(m, hp, lp, np, ap, pnp, pinp, fp, st0);
	break;
      case M_du: /* ^Jd */
	free_bp(&hp->base[2]);
	if ((i = fork_du(lp->dir)) < 0) {
	  echo_mess(S_errj);
	  continue;
	}
	np->fd = i;
	strcpy(ap->du, lp->dir);
	break;
      case T_du2:
      case M_du2: /* ^J^D */
	if (m == T_du2) m = M_du;
	m = m_du(m, hp, ap, np, lp, pinp, &st0);
	break;
      case T_cr:
	strcpy(ap->lstr, rvp->str);
	np->M = T_locate;
	m = 0;
	break;
      case M_yes: /* ^X^Y */
      case M_no: /* ^X^N */
	m = m_yn(m, hp->bp->pp, np->Mb, lp->prev);
	break;
      case M_dest: /* ^X^M */
	m = m_dest(m, lp, rvp, np, hp, ap, fp);
	break;
      case M_bl2: /* ^J^J */
	if (!(sp = hp->ss.p.hfile)) {
	  echo_mess(S_err2);
	  continue;
	}
	if (sp->next) {
	  if (!(i = (*ap->name == '/' && strchr(ap->name, '\0')[-1] == '/')
		|| *ap->name == ' ')
	      && *ap->full == '/' && strchr(ap->full, '\0')[-1] != '/')
	    hp->ss.p.hfile = save_history(sp, ap->full, 1);
	  while (sp->next->next) sp = sp->next;
	  if (i) sp = sp->next;
	}
	np->Mb = M_lfile;
	strcpy(rvp->str, sp->name);
	m = M_cr;
      case M_cr: case M_code: case M_fp: case M_fcut: case M_fcut2: 
	/* ^M, ^[^M, ^O^M, ^J^B, ^Jb */
	if ((m = m_cr(m, np, hp, lp, ap, rvp, fp, pnp, pinp)) == -T_exit)
	  return -1;
	break;
      case T_disp:
	np->Mb = (hp->bp && hp->bp->pp) ? T_skip : 0;
	m = 0;
	break;
      case M_bl: /* ^G */
	m = m_bl(m, np, ap, lp, hp, fp, pinp, pnp, st0);
	break;
      }
    } while (m < 0);
  } while (m);
  switch(np->M) {
  case M_rm:
  case M_cp:
  case M_mv:
  case M_arc:
    break;
  }
  save_ll(np->ll, &hp->ll, &hp->ss, ap->full, pnp);
  if (load_intr() == S_errg) unlink(s_S(S_intr));
  return 0;
}

static int init_menu(HEAP *hp, ARRAY *ap, NUM *np, LINE *lp, FILE **pinp)
{
  char tmp[N_line];
  struct stat st;
  int i, j;
  CHILD *chp;
  static PN pn;

  hp->child = init_child();
  np->history = 200;
  sprintf(tmp, "%d", np->history);
  save_history(F6, tmp, 0);
  vt_clear();
  if (is_fm()) {
    get_child(hp->child, NULL, NULL, NULL, NULL);
    if (init_config(hp->child, &hp->ll.p.prog, &hp->ll.p.grep
		    , &hp->ss.p.prog, &hp->ss.p.grep, &hp->bsize
		    , &ap->key_m, &ap->key_c, ap->name, ap->comm) < 0)
      return -1;
    save_history(F6, tmp, 0);
    sscanf(tmp, "%d", &np->history);
    if ((j = *ap->name)) {
      if (j != '/') {
	getcwd(lp->dir, 128);
	if (strchr(lp->dir, '\0')[-1] != '/') strcat(lp->dir, "/");
	strcat(lp->dir, ap->name);
	strcpy(ap->name, lp->dir);
      }
      if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
      erase_dot(ap->name);
      strcpy(lp->dir, ap->name);
    } else if (vt_set(0) & 0x80) {
      read_list(&hp->ll, &hp->ss, lp, ap->name);
      save_history(hp->ss.p.hist, tmp, -1);
      hp->ss.p.hist = free_history(hp->ss.p.hist, tmp, np->history);
      save_history(hp->ss.p.hfile, tmp, -1);
      hp->ss.p.hfile = free_history(hp->ss.p.hfile, tmp, np->history);
      save_history(hp->ss.p.hdir, tmp, -1);
      hp->ss.p.hdir =  free_history(hp->ss.p.hdir, tmp, np->history);
      save_history(hp->ss.p.hman, tmp, -1);
      hp->ss.p.hman =  free_history(hp->ss.p.hman, tmp, np->history);
      save_history(hp->ss.p.hlocate, tmp, -1);
      hp->ss.p.hlocate =  free_history(hp->ss.p.hlocate, tmp, np->history);
      save_history(hp->ss.p.hfgrep, tmp, -1);
      hp->ss.p.hfgrep =  free_history(hp->ss.p.hfgrep, tmp, np->history);
      save_history(hp->ss.p.hegrep, tmp, -1);
      hp->ss.p.hegrep =  free_history(hp->ss.p.hegrep, tmp, np->history);
      print_pid(getpid());
    }
    if (!(i = *lp->dir) || j) {
      pn.nrow = 10000;
      if (!j) getcwd(lp->dir, 128);
      hp->ll.p.man = save_list(hp->ll.p.man, s_S(S_usilic), &pn);
      if (strchr(lp->dir, '\0')[-1] != '/') strcat(lp->dir, "/");
      strcpy(lp->old, lp->dir);
      sprintf(lp->arg, "%c%c", K_z, 0);
      strcpy(lp->prev, lp->arg);
      strcpy(ap->name, lp->dir);
    }
    if (j) np->M = T_dir;
    else {
      hp->sp = save_stack_dir(hp->sp, lp->dir, lp->old, &hp->base[1]);
      load_list(hp->ll.p.dir, lp->dir, &pn);/*load pn if lp->dir exist */
      hp->ss.p.dir = save_str(hp->ss.p.dir, lp->dir, &pn);
      hp->ss.p.hdir = save_history(hp->ss.p.hdir, lp->dir, 1);
      hp->ll.p.dir = save_list(hp->ll.p.dir, lp->dir, &pn);
      if (!i) {
	strcpy(lp->prev, lp->dir);
	strcpy(ap->name, s_S(S_help));
	np->M = M_help;
      } else if (strstr(lp->arg, s_S(S_man))) np->M = T_man;
      else {
	if ((i = stat(ap->name, &st)) < 0) strcpy(ap->name, lp->dir);
	chdir(lp->dir);
	if ((np->M = (i || S_ISDIR(st.st_mode)) ? T_dir : T_file) == T_dir) {
	  if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
	  hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	} else if (*lp->cut.str) {
	  if (lp->cut.chp) {
	    ins_q(lp->cut.str);
	    sprintf(tmp, "%s%s\r", mtoa(M_cut2), lp->cut.str);
	  } else {
	    strcpy(lp->cut.name, ap->name);
	    np->M = T_cut;
	  }
	}
      }
    }
    if (np->M != M_help && *lp->cut.str) 
      vt_printf("Reading %s \042%s\042 ... ", ap->name, lp->cut.str);
    else vt_printf("Reading %s ... ", ap->name);
    vt_flush();
    if (*lp->cut.str && np->M != T_cut) 
      read_m(NULL, 2, 0, tmp, NULL, NULL, NULL, NULL, NULL, NULL);
    alloc_sh(NULL, 0, hp->bsize);
  } else {
    sprintf(lp->arg, "%c%c", K_z, 0);
    sprintf(lp->prev, "%c%c", K_y, 0);
    hp->child = save_child(hp->child,s_P(P_child)[S_egrep - S_ls], 0, NULL);
    hp->child = save_child(hp->child,s_P(P_child)[S_fgrep - S_ls], 0, NULL);
    save_child(F10, NULL, 0, NULL);
    get_child(hp->child, NULL, NULL, NULL, NULL);
    if (init_config(hp->child, NULL, &hp->ll.p.grep, NULL, NULL, &hp->bsize
		    , &ap->key_m, &ap->key_c, ap->name, ap->comm) < 0)
      return -1;
    if (vt_set(0) & 0x04) {
      np->M = T_key;
      strcpy(ap->name, s_P(P_mode)[M_key]);
    } else if (vt_set(0) & 0x08) {
      np->M = T_cat;
      strcpy(ap->name, s_P(P_mode)[M_key]);
    } else if ((np->M = (*ap->name == ' ') ? T_pipe : T_pipe3) == T_pipe3) {
      if ((chp = check_ext(ap->name, NULL, 0)) && (i = is_chp(chp, NULL))) {
	sprintf(lp->arg, " (%s)", chp->argv[0]);
	*pinp = fork_pipe(i, chp, ap->name);
      } else *pinp = fopen(ap->name, "r");
    }
    strcpy(ap->mess, ap->name);
    vt_printf("Reading %s ... ", ap->name);
    vt_flush();
  }
  i = -1;
  alloc_buff(&i, 0, hp->bsize);
  alloc_pipe(-2, NULL, NULL, NULL, hp->bsize);
  set_pp(NULL, hp->bsize, NULL, 0, 0);
  return conf_ls(NULL, NULL);
}

static void init_bp(NUM *np, HEAP *hp, ARRAY *ap, CHILD **chpp, FILE **pinp, CUT *cutp, u_char prev[], char arg[], RV *rvp, FLAG *fp)
{
  int c, i;

  if (np->M != M_copy) echo_mess(2);
  if ((np->ll = is_str(np->M, &i)))
    hp->bp = init_str(np->M, &hp->base[i], (STR*)hp->ss.a[np->ll], ap->name);
  else if ((np->ll = is_list(np->M, &i))) {
    if (np->M == M_prog) sprintf(ap->cname, "\047%s\047", prev);
    hp->bp = init_list(np->M, &hp->base[i], (LIST*)hp->ll.a[np->ll], ap->name);
  } else {
    switch(np->M) {
    case M_cut2:
    case M_man2:
    case M_fgrep:
    case M_locate:
    case M_prog2:
    case M_lfile2:
    case M_ldir2:
    case M_copy:
      *chpp = NULL;
      break;
    case M_key:
    case T_key:
      free_bp(hp->base);
      hp->bp = hp->base;
      init_bin(hp->bp, NULL, chpp, np->M, ap->name, 0);
      break;
    case M_usilic:
    case M_error:
    case M_log:
      hp->bp = calloc(sizeof(BASE), 1);
      init_bin(hp->bp, NULL, chpp, np->M, ap->name, 0);
      break;
    case M_help:
    case T_help:
      hp->bp = &hp->base[7];
      init_cut2(hp->bp, NULL, set_help(NULL), "^  [1-6][1-6]+\\.");
      np->M = T_cut;
      break;
    case T_buff:
      if (fp->cr <= 1) {
	hp->bp = calloc(sizeof(BASE), 1);
	if ((*chpp = init_arc(hp->bp, hp->arc, ap->name
			      , hp->bsize, prev, arg, rvp->pp, fp->cr)) 
	    == F9) {
	  np->M = T_exec2;
	  *chpp = NULL;
	}
      } else {
	hp->bp = &hp->base[4];
	alloc_pipe(-1, pinp, NULL, NULL, 0);
	if ((*pinp = init_arc2(hp->bp, hp->arc, ap->name, rvp->pp
			       , prev, arg)) != F1) {
	  np->nlen = init_pipe(hp->bp, pinp, T_pipe3, ap->name, prev, arg
			       , &np->klen, chpp, np->ppid);
	  np->M = T_pipe3;
	}
      }
      break;
    case T_split:
      c = 0;
      if ((i = hp->bp == &hp->base[7]) && !fp->help) fp->help = 1;
      if (fp->cr <= 1) {
	hp->bp = calloc(sizeof(BASE), 1);
	if (i || *cutp->name)
	  init_split2(hp->bp, cutp->buff, hp->bsize
		      , (i) ? (u_char*)set_help(NULL) : cutp->name
		      , rvp->pp, fp->cr);
	else 
	  init_split(hp->bp, ap->name, rvp->pp, cutp->bp, fp->cr);
      } else {
	hp->bp = &hp->base[4];
	alloc_pipe(-1, pinp, NULL, NULL, 0);
	if (i || *cutp->name) {
	  if ((*pinp = init_split4(hp->bp, cutp->buff
				   , (i) ? (u_char*)set_help(NULL) : cutp->name
				   , rvp->pp)) != F1) {
	    np->nlen = init_pipe(hp->bp, pinp, T_pipe3, ap->name, prev, arg
				 , &np->klen, chpp, 0);
	    np->M = T_pipe3;
	  }
	} else 
	  if ((*pinp = init_split3(hp->bp, ap->name
				   , rvp->pp, cutp->bp->pp)) != F1) {
	    np->nlen = init_pipe(hp->bp, pinp, T_pipe3, ap->name
				 , prev, arg, &np->klen, chpp, 0);
	    np->M = T_pipe3;
	  }
      }
      break;
    case T_du:
      break;
    case M_cp:
    case M_mv:
    case M_rm:
    case M_arc:
      if (*prev == '/') chdir(prev);
    case T_arc2:
    case M_exit:
      break;
    case T_dir:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      hp->bp = &hp->base[1];
      init_ls(hp->bp, ap->name, np->M, fp->ls);
      break;
    case M_du:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      hp->bp = &hp->base[2];
      init_bin(hp->bp, NULL, chpp, np->M, ap->name, np->fd);
      np->fd = 0;
      break;
    case T_locate:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      hp->bp = &hp->base[5];
      init_ls(hp->bp, rvp->str, np->M, 0);
      break;
    case T_man:
      hp->bp = &hp->base[4];
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      init_man(hp->bp, pinp, ap->name, prev, arg, &np->klen);
      break;
    case T_grep:
      rvp->pp = hp->bp->pp;
      hp->bp = calloc(sizeof(BASE), 1);
      init_grep(hp->bp, ap->name, ap->gstr, rvp->pp, (fp->grep == 2));
      *chpp = get_child(NULL, NULL
			, (fp->grep == 2) ? s_S(S_fgrep) : s_S(S_egrep)
			, NULL, NULL);
      break;
    case T_pipe3:
    case T_pipe:
    case T_pipe2:
      hp->bp = &hp->base[4];
      np->nlen = init_pipe(hp->bp, pinp, np->M, ap->mess, prev, arg
			   , &np->klen, chpp, np->ppid);
      break;
    case T_cut:
      if ((c = *cutp->name)) {
	hp->bp = calloc(sizeof(BASE), 1);
	if (c != ' ')
	  if (access(cutp->name, F_OK) < 0) 
	    make_bp(hp->bp, s_S(S_err3), cutp->name, NULL);
	  else init_cut2(hp->bp, NULL, cutp->name, cutp->str);
	else init_cut2(hp->bp, cutp->buff, cutp->name, cutp->str);
      } else {
	cutp->bp = hp->bp;
	hp->bp = calloc(sizeof(BASE), 1);
	init_cut(hp->bp, ap->name, cutp->str, cutp->bp);
      }
      *chpp = NULL;
      break;
    default:
      alloc_pipe(-1, pinp, NULL, NULL, 0);
      if ((is_fm()) 
	   && (*chpp = (fp->cr) ? check_ext(ap->name, NULL, 0) : NULL))
	np->M = is_child(ap->name, *chpp);
      hp->bp = calloc(sizeof(BASE), 1);
      switch(np->M) {
      case T_exec:
	break;
      case T_tar:
      case T_lha:
      case T_unzip:
      case T_rpm:
      case T_deb:
	hp->arc = *chpp;
      default:
	np->M = init_bin(hp->bp, &hp->base[9], chpp, np->M, ap->name, fp->cr);
	break;
      }
    }
  }
}

void menu(char pathname[], FILE *pin)/*usilic*/
{
  /*
   *  base[?]
   *	0: M_key, M_lfile, M_ldir, M_log, M_prog, M_man
   *	1: T_dir
   *	2: M_du
   *	3: M_egrep
   *	4: T_pipe, T_pipe2, T_pipe3
   *	5: M_locate
   *	6: M_cmd, T_cmd, M_hist, T_hfile, T_hdir, T_hman, T_hlocate
   *	7: M_help
   *	8: M_list, T_list
   *	9: Header of T_tar, T_lha, T_unzip, T_rpm, T_deb
   *   10: M_cut	
   *   11: T_hfgrep, T_hegrep
   *
   *  T_pipe?
   *	 : " "
   *	2: " (stdin)"
   *	3: pipe or regular-file
   */
  int i;
  RV rv;
  CHILD *chp;
  static FLAG f;
  static PN pn;
  static LINE l;
  static HEAP h;
  static ARRAY a;
  static NUM n;
  
  strcpy(a.name, pathname);
  free_h(&h);
  if ((i = init_menu(&h, &a, &n, &l, &pin)) < 0) return; 
  f.ls = i;
  f.cr = 1;
  read_m(NULL, T_key2, 0, (u_char *)a.key_m, (char*)a.key_c
	 , NULL, NULL, NULL, NULL, NULL);
  signal(SIGSEGV, menu_clear);
  setjmp(Menu.jb);
  if ((n.Mb = Menu.f_Mb) == T_skip) {
    if (is_pg() || (n.M == T_dir && *l.prev == K_z)) {
      vt_move(vt_row(0) - 1, 0);
      save_stderr("%s: %s.\n", s_S(S_usilic), s_S(S_err8));
      exit_usilic();
      print_stderr(NULL);
      exit(exit_ALLOC(NULL));
    }
    sprintf(a.intr, ": %s", a.name);
    save_intr(S_err8);
    clear_stack(&h, &l, a.name, &n.M, K_z);
    n.Mb = 0;
  }
  n.clen = 0;
  n.error = load_intr(); /* for erre */
  do {
    chp = load_chp(l.arg);
    n.nlen = 1;
    *a.cname = '\0';
    if (n.Mb != T_skip || !h.bp->pp) {
      init_bp(&n, &h, &a, &chp, &pin, &l.cut, l.prev, l.arg, &rv, &f);
      if (n.M == T_exec2) {
	n.Mb = n.M = T_exec;
	continue;
      } else if ((n.Mb = n.M) == T_exec) do_exec(a.name, chp);
    } else n.Mb = n.M;
    if (n.M != T_exec) {
      pn.nrow = h.bp->nrow; /* (2) */
      if (!h.bp->pp) {
	make_bp(h.bp, s_S(S_err1), a.name, NULL);
	pn.nrow = h.bp->nrow = 0;
      }
    }
    if (h.bp->size < 0 || n.M == T_type || (h.bp->pp && h.bp->pp->f.code)) {
      n.ll = -1;
      pn.top = pn.row = 0;
    } else {
      n.ll = load_ll(chp, &a, l.prev, &l.cut, &h, &pn, n.M, &pin);
      if (h.bp->nrow < pn.nrow) h.bp->nrow = pn.nrow; /* ==> (2) */
    }
    if (n.Mb != T_exec) {
      int j = h.bp == &h.base[4] && pin && pin != F1;
      set_rv(&n, &pn, &rv, &h 
	     , ((i = pn.nrow - (vt_row(0) - !j)) > 0) ? i : 0);
      print_arg(l.arg, (h.bp == &h.base[4] || (rv.pp && rv.col >= 0)) 
		? -1 : h.bp->size, h.bp->buff, chp, n.M);
      if (n.M != T_pipe && n.M != T_pipe2 && n.M != T_pipe3)
	n.nlen = disp_window(rv.ps, rv.pp, n.Mb, a.name, 
			     l.prev, l.arg, &n.klen);
      else n.Mb = n.M = (n.M == T_pipe3) ? T_pipe5 : T_pipe4;
      if (rv.col == -1) rv.col = 0;
      if (is_comm(n.Mb, 1) || n.Mb == M_exit) 
	disp_prev(l.prev, l.dir, n.klen * ((n.Mb == M_exit) ? -1 : 1));
    }
    if (n.error) {
      save_intr(n.error);
      n.error = 0;
    }
    if (!f.help) {
      if (h.bp == &h.base[7]) {
	vt_move(0, 21);
	vt_printf("[ Press %sEnter%s key ]", vt_geta(A_md), vt_geta(A_me));
      }
    }
    print_intr(NULL, (*a.intr) ? a.intr : NULL);
    if (*a.intr) *a.intr = '\0';
  } while (!get_m(&h, &a, &f, &n, &l, &rv, &pin, &pn, chp));
  if (vt_set(0) & 0x80) {
    alloc_pipe(-1, &pin, NULL, NULL, 0);
    save_ll(n.ll, &h.ll, &h.ss, a.full, &pn);
    do {
      clear_stack(&h, &l, a.name, &n.M, 'g');
      if (h.bp == &h.base[7]) {
	h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	continue;
      }
      if (*a.name != ' ') {
	if (*a.name != '/') {
	  if (strstr(l.arg, s_S(S_man))) { /* T_man */
	    strcpy(l.prev, l.dir);
	    break;
	  } else ; /* file in T_tar, T_lha, T_unzip, T_rpm, T_deb */
	} else if (is_prev(l.prev, M_du)) {
	  if (n.M == T_dir) *l.prev = K_z;
	  else strcpy(l.prev, l.dir);
	  break;
	} else if (n.M != T_split && n.M != T_buff 
		   && (*l.prev == '/' || n.M == T_dir) && h.bp->nrow) 
	  if (!h.bp->pp || !h.bp->pp->f.code) break;
      }
      h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
    } while (h.sp);
    if (*l.cut.str)
      do {
	if (n.M != T_cut) ;
	else if (strchr(l.prev, '\0')[-1] != '/') {
	  h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	  if (n.M == T_buff)
	    h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	  if (n.M == T_tar || n.M == T_lha || n.M == T_unzip 
	      || n.M == T_rpm || n.M == T_deb)
	    h.sp = load_stack(h.sp, &l, &n.M, a.name, &h.bp, h.base);
	} else continue;
	*l.cut.str = '\0';
      } while (0);
    if (*a.name == '/' && *l.arg == K_z) /* != T_man */
      l.arg[1] = '\0';
    if (*l.prev == K_z) l.prev[1] = '\0';
    write_list(h.ll, h.ss, &l, a.name);
  }
  disp_window(NULL, NULL, -2, NULL, NULL, NULL, NULL);
  clear_cut(&l.cut);
  set_help(F13);
}

#include "usilic.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "menu.h"
int echo_mess(int f_mess)/*usilic*/
{
  switch(f_mess) {
  case 0:
    break;
  case 1:
    fprintf(stdout, "\n%s%s: %s%s "
	    , s_S(S_usilic), vt_geta(A_md), s_S(S_err9), vt_geta(A_me));
    fflush(stdout);
    vt_init(2);
    read_m(F11, M_getch, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    break;
  case 2:
    vt_move(vt_row(0) - 1, 0);
    vt_printf("%s%s%s", vt_geta(A_md), s_S(S_errk), vt_geta(A_me));
    vt_flush();
    break;
  default:
    save_intr(f_mess);
    print_intr(NULL, NULL);
    return 1;
    break;
  }
  return 0;
}

void free_h(void *hp0)/*usilic*/
{
  int i;
  static HEAP *hp;

  if (hp0) {
    hp = hp0;
    return;
  }
  clear_stack(hp, NULL, NULL, NULL, 0);
  if (hp->w) free(hp->w);
  for (i = 0; i < N_base; i++) free_bp(&hp->base[i]);
  for (i = 0; i < N_ll; i++) 
    hp->ll.a[i] = (u_long)free_list((LIST*)hp->ll.a[i]);
  for (i = 0; i < N_ss; i++) {
    free_str((STR*)hp->ss.a[i]);
    hp->ss.a[i] = 0;
  }
  free_child(hp->child);
}

int skip_spc(u_char cs[], int f_spc)/*usilic*/
{
  static char mark[] = "*=|@";
  static int i_dir;
  int c, i, j;
  u_int ino;
  u_char *cp;
  struct stat st;

  if (f_spc < 0) {
    if ((cp = strstr(cs, "../"))) i_dir = cp - cs;
    return i_dir;
  } else if (f_spc) {
    if ((cp = strstr(cs, " -> "))) *cp = '\0';
    switch(f_spc) {
    case 1:
      while (1) {
	if ((cp = strchr(cs, ' ')) && cp[-1] != '\\') {
	  if (cp[1]) {
	    strcpy(cs, &cp[1]);
	    continue;
	  } else *cp = '\0';
	}
	break;
      }
      break;
    case 2:
      for (cp = &cs[i_dir]; *cp == ' '; cp++) ;
      strcpy(cs, cp);
      break;
    }
  }
  for (cp = cs; ; cp++) 
    if ((cp = strchr(cp, '\\')) && cp[1]) {
      for (i = 1, j = 0; i <= 3 && (c = cp[i]) >= '0' && c <= '9'; i++)
	j = (j << 3) + c - '0';
      if (j) {
	*cp++ = j;
	i--;
      }
      strcpy(cp, &cp[i]);
    } else break;
#ifdef SKIP_SPC
 debug("1:cs=%s\n",cs);
#endif
  for (i = 0; (c = mark[i]); i++)
    if ((cp = strchr(cs, c))) {
      if (stat(cs, &st) < 0) {
	*cp = '\0';
	break;
      }
      *cp = '\0';
      ino = st.st_ino;
      if (!stat(cs, &st) && ino == st.st_ino) break;
      *cp = c;
    }
#ifdef SKIP_SPC
 debug("2:cs=%s\n",cs);
#endif
  return 0;
}

static void init_rv(NUM *np, PN *pnp, RV *rvp, HEAP *hp, int etop)
{
  int i;
  PP *pp;

  if (np->Mb == M_du) np->Mb = np->M = T_du;
  if (pnp->top > etop) pnp->top = etop;
  else if (pnp->top < 0) pnp->top = 0;
  pp = hp->bp->pp;
  if ((rvp->row = init_row(&pp, pnp, np->tell, etop)) > vt_row(0) - 2) 
    pnp->row = rvp->row = vt_row(0) - 2;
  if ((rvp->ps  /* beginning row of window */
       = rvp->pe = rvp->pp = pp)) {
    for (i = 0; i < vt_row(0) - 2 && rvp->pe->next; i++)
      rvp->pe = rvp->pe->next; /* end row of window */
    for (i = 0; i < pnp->row && rvp->pp->next; i++) 
      rvp->pp = rvp->pp->next; /* current row */
  }
  if (np->M == T_dir && hp->bp->size >= 0) 
    for (pp = hp->bp->pp; pp && !strstr(pp->cs, "../"); pp = pp->next) ;
  else pp = hp->bp->pp;
  rvp->col = init_col(np->M, pp->cs, hp->bp->size);
  if (hp->bp->pp->tell != (u_int)F7 /* F7: "No size" etc. */
      && rvp->pp && rvp->col >= 0) { /* reverse attr. */
    strcpy(rvp->str, &rvp->pp->cs[rvp->col]);
  } else strcpy(rvp->str, "");
}

void set_rv(NUM *np, PN *pnp, RV *rvp, HEAP *hp, int etop)/*mode*/
{
  PP *pp;

  init_rv(np, pnp, rvp, hp, etop);
  if (np->tell) np->tell = 0;
  else if (pnp->line == -2) ;
  else if (pnp->line > 0) {
    if (pnp->line > rvp->pp->f.line)
      for (pp = rvp->pp; pnp->line > pp->f.line && pp->next; pp = pp->next)
	if (pnp->row < vt_row(0) - 2) pnp->row++;
	else pnp->top++;
    else if (pnp->line < rvp->pp->f.line) {
      for (pp = rvp->pp; pnp->line <= pp->f.line && pp->prev; pp = pp->prev)
	if (pnp->row) pnp->row--;
	else pnp->top--;
      if (!pnp->row) pnp->top++;
      else pnp->row++;
    }
    if (pnp->line != rvp->pp->f.line) init_rv(np, pnp, rvp, hp, etop);
  }
  pnp->line = rvp->pp->f.line;
}

int load_ll(CHILD *chp, ARRAY *ap, u_char prev[], CUT *cutp, HEAP *hp, PN *pnp, int f_M, FILE **pinp)/*mode*/
{
  int i, skip;
  PN pn;
  LIST **lp;
  char **mp;

  mp = s_P(P_mode);
  lp = NULL;
  skip = 0;
  if (*ap->name != ' ') {
    strcpy(ap->full, ap->name);
    cat_child(chp, ap->full);
  }
  switch(f_M) {
  case M_du:
    skip = 1; /* display from the beginning of buffer */
  case T_du:
    /* set `lp' and `full' for saving PN of N_num save_ll() */
    lp = &hp->ll.p.mode;
    strcpy(ap->full, ap->du);
    break;
  case T_pipe2:
  case T_pipe:
    skip = 1;
    strcpy(ap->full, mp[T_pipe2]);
  case T_pipe4:
    if (*ap->name == ' ') {
      strcpy(ap->full, mp[T_pipe2]);
      lp = &hp->ll.p.mode;
    } else lp = &hp->ll.p.man;
    break;
  case T_pipe3:
    skip = 1;
  case T_pipe5:
    strcat(ap->full, mp[T_pipe2]);
    lp = &hp->ll.p.mode;
    break;
  case M_cmd:
  case T_cmd:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_cmd]);
    break;
  case M_man:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_man]);
    break;
  case T_man:
    lp = &hp->ll.p.man;
    break;
  case M_cut:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_cut]);
    break;
  case T_cut:
    if (hp->bp == &hp->base[7]) lp = &hp->ll.p.mode;
    else {
      if ((chp = check_ext(prev, NULL, 0))
	  && (is_arg(chp->argv[0], S_tar)
	      || is_arg(chp->argv[0], S_lha)
	      || is_arg(chp->argv[0], S_unzip)
	      || is_arg(chp->argv[0], S_rpm)
	      || is_arg(chp->argv[0], S_deb))) lp = &hp->ll.p.mode;
      else {
	if (*ap->name != '/') {
	  sprintf(ap->full, "%s %s", s_P(P_mode)[M_cut]
		  , &ap->name[*ap->name == ' ']);
	  lp = &hp->ll.p.mode;
	} else {
	  if ((cutp->chp = check_ext(ap->name, NULL, 0)))
	    lp = &hp->ll.p.mode;
	  else lp = &hp->ll.p.file;
	  if (*cutp->str) cutp->chp = NULL;
	}
	if (!*cutp->name && lp != &hp->ll.p.mode) lp = &hp->ll.p.mode;
	if (lp == &hp->ll.p.mode) strcat(ap->full, prev);
	sprintf(strchr(ap->full, '\0'), " \042%s\042", cutp->str);
      }
    }
    chp = NULL;
    break;
  case T_locate:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_locate]);
    break;
  case M_egrep:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_egrep]);
    break;
  case T_grep:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_egrep]);
    strcat(ap->full, prev);
    strcat(ap->full, ap->gstr);
    break;
  case M_rm:
  case T_rm:
    lp = &hp->ll.p.mode;
    if (is_prev(prev, M_du)) strcpy(ap->full, ap->du);
    else if (is_prev(prev, M_lfile)) strcpy(ap->full, mp[M_lfile]);
    else if (is_prev(prev, M_ldir)) strcpy(ap->full, mp[M_ldir]);
    else if (is_prev(prev, M_man)) strcpy(ap->full, mp[M_man]);
    else if (is_prev(prev, M_prog)) strcpy(ap->full, mp[M_prog]);
    else if (is_prev(prev, M_egrep)) strcpy(ap->full, mp[M_egrep]);
    else if (is_prev(prev, M_cmd)) strcpy(ap->full, mp[M_cmd]);
    else {
      lp = &hp->ll.p.dir;
      strcpy(ap->full, prev);
    }
    break;
  case T_arc2:
  case T_arc3:
    lp = &hp->ll.p.file;
    break;
  case M_cp:
  case M_mv:
  case M_arc:
  case T_cp:
  case T_mv:
  case T_arc:
    if (is_prev(prev, M_du)) {
      strcpy(ap->full, ap->du);
      lp = &hp->ll.p.mode;
    } else {
      strcpy(ap->full, prev);
      lp = &hp->ll.p.dir;
    }
    break;
  case T_dir:
    lp = &hp->ll.p.dir;
    break;
  case T_file:
  case T_file2:
  case T_exec:
  case T_bin:
  case T_gz:
  case T_roff:
  case T_rpm:
  case T_deb:
  case T_unzip:
  case T_lha:
  case T_tar:
  case T_type:
    lp = &hp->ll.p.file;
    break;
  case T_split:
    sprintf(ap->full, "%s.%d", prev, pnp->nrow);
  case T_buff:
    lp = &hp->ll.p.split;
    break;
  case T_split2:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, s_S(S_usilic));
    break;
  case M_prog:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_prog]);
    break;
  case M_key:
  case T_key:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_key]);
    break;
  case M_error:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_error]);
    break;
  case M_log:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_log]);
    break;
  case M_lfile:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_lfile]);
    break;
  case M_ldir:
    lp = &hp->ll.p.mode;
    strcpy(ap->full, mp[M_ldir]);
    break;
  default:
    lp = NULL;
    *ap->full = 0;
    break;
  }  
  if (lp) {
    memcpy(&pn, pnp, sizeof(PN));
    if (skip || !load_list(*lp, ap->full, pnp))
      pnp->row = pnp->top = pnp->line = 0;
    else if (pnp->nrow < 0) { /* (1) */
      pnp->row = pnp->top = pnp->line = 0;
      pnp->nrow = pn.nrow;
    } else {
      if (*pinp && *pinp != F1 && hp->bp == &hp->base[4]
	  && (i = pnp->top) > 0)
	while (i-- && *pinp) 
	  alloc_pipe(0, pinp, &pn.nrow, NULL, 0);
      if (pnp->nrow != pn.nrow) pnp->nrow = pn.nrow;
    }
    if (pnp->row && pnp->row > pnp->nrow - 1) /* cursor line > total line */
      if ((pnp->row = pnp->nrow - 1) < 0) /* total line == 0 */
	pnp->row = 0;
    i = 0;
  } else {
    i = -1;
    switch(f_M) {
    case M_hist:
    case T_hist:
    case T_hfile: 
    case T_hdir: 
    case T_hman: 
    case T_hlocate:
    case T_hfgrep:
    case T_hegrep:
      pnp->line = -2;
      if ((pnp->row = hp->bp->nrow - 1) < 0) pnp->row = 0;
      if ((pnp->top = hp->bp->nrow - vt_row(0) + 1) < 0) pnp->top = 0;
      break;
    case M_fgrep:
    case M_locate:
    case M_man2:
    case M_cut2:
    case M_prog2:
    case M_lfile2:
    case M_ldir2:
    case M_exit:
    case M_copy:
      i = -2;
      break;
    default:
      pnp->row = pnp->top = pnp->line = 0;
      break;
    }
  }
  return (lp) ? (((u_int)lp - (u_int)&hp->ll.a[0]) / sizeof(u_int)) : i;
}

int mode(HEAP *hp, NUM *np, ARRAY *ap, LINE *lp, RV *rvp)/*mode*/
{
  struct stat st, l_st;
  int i, f_M, m;
  char *cp;
  u_char *p;
  
  m = 0;
  switch(f_M = np->Mb) {
  case M_egrep:
  case M_fgrep:
    hp->grep = save_grep(hp->grep);
    hp->sp = load_stack(hp->sp, &(hp->grep->l), &(hp->grep->f_M)
		       , hp->grep->name, &(hp->grep->bp), hp->base);
    hp->bp = hp->grep->bp;
    hp->sp = save_stack(hp->sp, &hp->grep->l, hp->grep->f_M
		       , hp->grep->name, hp->grep->bp, 1);
    strcpy(lp->prev, &ap->name[*ap->name == ' ']);
    *lp->arg = K_z;
    strcpy(ap->name, s_P(P_mode)[f_M]);
    np->M = T_grep;
    break;
  case T_grep:
    hp->sp = save_stack(hp->sp, lp, np->M, ap->name, hp->bp, 0);
    p = alloca(strlen(ap->name) + 1);
    strcpy(p, ap->name);
    memcpy(lp, &(hp->grep->l), sizeof(LINE));
    np->M = hp->grep->f_M;
    hp->bp = hp->grep->bp;
    strcpy(ap->name, hp->grep->name);
    print_ctrl('^', rvp->str);
    sprintf(lp->prev, "%s: %s", &p[1], rvp->str);
    np->Mb = T_skip;
    break;
  case T_dir:
    p = alloca(strlen(ap->name) + strlen(rvp->pp->cs));
    strcpy(p, ap->name);
    if (!is_prev(lp->prev, M_du) 
	&& !is_prev(lp->prev, M_egrep) && !is_prev(lp->prev, M_fgrep))
      clear_stack(hp, lp, NULL, &f_M, 0);
    else 
      hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if (strcmp(p, ap->name)) strcpy(ap->name, p);
    strcpy(p, rvp->pp->cs);
#ifdef SKIP_SPC
 debug("T_dir:%s\n", p);
#endif
    skip_spc(p, 2);
    errno = 0;
    if (strcmp(lp->dir, ap->name)) {
      strcpy(lp->old, lp->dir);
      strcpy(lp->dir, ap->name);
    }
    chdir(ap->name);
    lstat(p, &l_st);
    if (access(p, R_OK) < 0) {
      save_error("%s: %s%s: %s.\n", s_S(S_usilic), lp->dir, p, strerror(errno));
      save_intr(-errno);
      f_M = T_dir;
      if (S_ISLNK(l_st.st_mode)) m = -M_clear;
    } else {
      if ((i = !strcmp(p, "../")) || !strcmp(p, "./")) {
	if (i && strcmp(ap->name, "/")) {
	  for (cp = &strchr(ap->name, '\0')[-2]; *cp != '/'; cp--) ;
	  cp[1] = '\0';
	}
	f_M = T_dir;
      } else {
	stat(p, &st);
	if ((f_M = (S_ISDIR(st.st_mode)) ? T_dir : T_file) == T_dir) {
	  strcat(ap->name, p);
	  if (S_ISLNK(l_st.st_mode) && strchr(ap->name, '\0')[-1] != '/') 
	    strcat(ap->name, "/"); /* symlnk */
	}
      }
      if (f_M == T_dir && *lp->prev != K_z) 
	clear_stack(hp, lp, NULL, NULL, 0);
    }
    if (f_M == T_file) {
      hp->sp = save_stack(hp->sp, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, lp->dir);
      strcpy(ap->name, lp->dir);
      strcat(ap->name, p);
    } else {
      if (strcmp(ap->name, lp->dir)) {
	strcpy(lp->old, lp->dir);
	strcpy(lp->dir, ap->name);
      }
      chdir(ap->name);
    }
    np->M = f_M;
    break;
  case T_cut:
    strcpy(lp->prev, rvp->pp->cs);
    *lp->arg = K_z;
    np->M = T_split;
    break;
  case T_lha:
  case T_tar:
  case T_unzip:
  case T_rpm:
  case T_deb:
    strcpy(lp->prev, ap->name);
    p = alloca(strlen(rvp->pp->cs) + 1);
    strcpy(p, rvp->pp->cs);
#ifdef SKIP_SPC
 debug("T_lha..:%s\n", p);
#endif
    skip_spc(p, 1);
    strcpy(ap->name, p);
    np->M = T_buff;
    break;
  case T_du:
    strcpy(ap->name, rvp->str);
    if ((cp = strstr(rvp->pp->cs, "./"))) {
      p = alloca(strlen(&cp[2]) + 1);
      strcpy(p, &cp[2]);
      strcat(ap->name, p);
    } else chdir(ap->name);
    stat(ap->name, &st);
    np->M = (S_ISDIR(st.st_mode)) ? T_dir : T_file;
    strcpy(lp->old, lp->dir);
    if (np->M == T_dir) {
      if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
      strcpy(lp->dir, ap->name);
    } else {
      strcpy(lp->dir, ap->name);
      get_dir(lp->dir);
    }
    sprintf(lp->prev, "%s: %s", &s_P(P_mode)[M_du][1], lp->dir);
    break;
  case M_prog2:
  case M_prog:
    hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
    if (m == M_prog && *ap->name == ' ')
      save_error("%s: %s: %s.\n", s_S(S_usilic), ap->name, s_S(S_err3));
    else {
      for (cp = rvp->str; *cp == ' ' || *cp == '\t'; cp++) ;
      p = alloca(strlen(cp) + 1);
      strcpy(p, cp);
      if ((cp = strchr(p, ' '))) *cp = '\0';
      if (!find_prog(hp->ll.p.prog, p)) {
	strcpy(ap->mess, rvp->str);
	m = -M_shell;
      } else {
	if ((i = stat(lp->prev, &st)) < 0) i = 0;
	else i = st.st_ctime;
	do_prog(rvp->str, &hp->ss.p.cmd, &hp->ss.p.hist);
	if (i) stat(lp->prev, &st);
	hp->sp = load_stack(hp->sp, lp, &np->M, ap->name, &hp->bp, hp->base);
	i = (!i || (i && i == st.st_ctime));
	if (!i) {
	  free_bp(&hp->base[1]);
	  m = -T_prog;
	  if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))
	    clear_stack(hp, lp, ap->name, &np->M, 'g');
	  if ((hp->bp && hp->bp < &hp->base[0]) 
	      || hp->bp > &hp->base[N_base-1]) {
	    free_bp(hp->bp);
	    free(hp->bp);
	  }
	} else m = -T_skip;
      }
    }
    break;
  case M_ldir:
  case M_lfile:
  case M_ldir2:
  case M_lfile2:
  case T_locate:
    strcpy(ap->name, lp->dir);
    clear_stack(hp, lp, ap->name, &f_M, 0);
    strcpy(lp->dir, ap->name);
    strcpy(ap->name, ((cp = strstr(rvp->str, "//"))) 
	   ? &cp[1] : (char*)rvp->str);
    if ((*(cp = ap->name) == '~' && !cp[1]) || (cp = strstr(ap->name, "~/"))) {
      p = alloca(strlen(s_S(S_home)) + strlen(&cp[1]) + 1);
      sprintf(p, "%s%s", s_S(S_home), &cp[1]);
      strcpy(cp, p);
    }
    while (*(cp = &strchr(ap->name, '\0')[-1]) == ' ' || *cp == '\t') 
      *cp = '\0';
    if ((i = (!stat(ap->name, &st) && S_ISDIR(st.st_mode))))
      if (strchr(ap->name, '\0')[-1] != '/') strcat(ap->name, "/");
    erase_dot(ap->name);
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, ap->name);
    get_dir(lp->dir);
    if (i) {
      strcpy(lp->dir, ap->name);
      chdir(ap->name);
      *lp->prev = K_z;
      np->M = T_dir;
    } else {
      strcpy(lp->dir, ap->name);
      get_dir(lp->dir);
      chdir(lp->dir);
      *lp->prev = K_z;
      hp->sp = save_stack(hp->sp, lp, T_dir, lp->dir, &hp->base[1], 1);
      strcpy(lp->prev, lp->dir);
      np->M = (strstr(rvp->str, " (")) ? T_file2 : T_file;
    }
    break;
  }
  return m;
}  

void clear_stack(HEAP *hp, LINE *lp, char *name, int *f_Mp, int f_clear)/*mode*/
{
  int f_M;
  LINE l;
  STACK *sp;
  GREP *grep;
  BASE *bp;

  if (!lp) lp = &l;
  f_M = (f_Mp) ? *f_Mp : T_dir;
  grep = hp->grep;
  sp = hp->sp;
  bp = hp->bp;
  while (1) {
    switch(f_clear) {
    case 0:
      if (!sp) goto jump;
      break;
    case K_z:
      if (*lp->prev == K_z) goto jump;
      break;
    case '/':
      if ((*name != ' ' || !strcmp(name, " ") || is_name(name, T_pipe2)))
	goto jump;
      else if (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep)) {
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	grep = load_grep(grep);
      } 
      break;
    case 'p':
    case 'P':
      if (bp == &hp->base[4]
	&& (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))) {
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	grep = load_grep(grep);
      }
      if (bp == &hp->base[7] || !strcmp(name, s_S(S_help))) ;
      else if (!is_name(name, M_help))
	if (!read_stack(sp, &hp->base[4], 0)
	    && ((f_clear == 'p' && bp != &hp->base[4] && *name != ' ')
		|| (f_clear == 'P' && *name != ' ')))
	  goto jump;
      break;
    case 'h':
      if (!(!strcmp(name, s_S(S_help)) && bp == &hp->base[7])
	  && !read_stack(hp->sp, &hp->base[7], 0)) goto jump;
    case 'g':
      if (strchr(lp->prev, ':') /* skip RM at EGREP (menu.c(4)) */
	  && (is_prev(lp->prev, M_fgrep) || is_prev(lp->prev, M_egrep))) {
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
	grep = load_grep(grep);
      }
      if (f_clear == 'g') goto jump;
      break;
    }
    sp = load_stack(sp, lp, &f_M, name, &bp, hp->base);
  }
 jump:
  hp->sp = sp;
  hp->bp = bp;
  if (!f_clear || f_clear == K_z) while ((grep = load_grep(grep))) ;
  hp->grep = grep;
  if (f_Mp) *f_Mp = f_M;
}
  
char *set_cut(HEAP *hp, CUT *cutp, u_char mess[], char name[], char dir[], char old[])/*mode*/
{
  int c;
  char *cp, *tmp;

  if ((cp = strchr(mess, '\042'))) {
    c = cp[-1];
    cp[-1] = '\0';
    *strchr(&cp[1], '\042') = '\0';
    strcpy(cutp->str, &cp[1]);
    cutp->chp = NULL;
    strcpy(cutp->name, mess);
    clear_stack(hp, NULL, NULL, NULL, 0);
    strcpy(name, mess);
    cp[-1] = c;
    tmp = alloca(strlen(name) + 2);
    strcpy(tmp, name);
    get_dir(tmp);
    if (strchr(tmp, '\0')[-1] != '/') strcat(tmp, "/");
    if (strcmp(dir, tmp)) {
      strcpy(old, dir);
      strcpy(dir, tmp);
      chdir(dir);
      free_bp(&hp->base[1]);
    }
  }
  return cp;
}

void check_dir(HEAP *hp, LINE *lp, char *dir, LIST **dirp)/*mode*/
{
  PN pn;

  if (hp) clear_stack(hp, lp, NULL, NULL, 0);
  if (dir && strcmp(lp->dir, dir)) { /* exchange current directory name */
    strcpy(lp->old, lp->dir);
    strcpy(lp->dir, dir);
    chdir(lp->dir);
  }
  if (!load_list(*dirp, lp->dir, &pn)) {
    /* save current directory name */
    pn.nrow = -1; /* ==> (1) */
    *dirp = save_list(*dirp, lp->dir, &pn);
  }
}

int fmark(int f_M)/*usilic*/
{
  int f_mark;

  if (is_comm(f_M, 0)) f_mark = f_M;
  else if (!(f_mark = is_comm(f_M, 1))) f_mark = f_M == M_exit;
  return f_mark;
}

int nmark(PP *pp, int f_M)/*usilic*/
{
  int n_mark, f_mark;

  for (n_mark = 0, f_mark = fmark(f_M); pp; pp = pp->next)
    if (pp->split == -f_mark) n_mark++;
  return n_mark;
}

int send_pp(PP *pp, char fmode[])/*usilic*/
{
  FILE *fp;
  int i, row;
  u_char *cs;

  fp = fopen((!strcmp(fmode, "a")) ? s_S(S_write) : s_S(S_w), fmode);
  for (row = 0; pp; pp = pp->next) {
    for (i = pp->f.len, cs = pp->cs; i; i--, cs++) fputc(*cs, fp);
    if (!pp->f.code && pp->f.nl) {
      fputc('\n', fp);
      row++;
    }
  }
  fclose(fp);
  return row;
}

int do_rm(LL *llp, SS *ssp, u_char prev[], FILE *fp)/*usilic*/
{
  int m;

  if (is_prev(prev, M_lfile)) {
    llp->p.file = delete_name(llp->p.file, ssp->p.file, NULL, fp);
    m = -M_lfile;
  } else if (is_prev(prev, M_ldir)) {
    llp->p.dir = delete_name(llp->p.dir, ssp->p.dir, NULL, fp);
    m = -M_ldir;
  } else if (is_prev(prev, M_man)) {
    llp->p.man = delete_name(llp->p.man, ssp->p.man, NULL, fp);
    m = -M_man;
  } else if (is_prev(prev, M_prog)) {
    llp->p.prog = delete_name(llp->p.prog, ssp->p.prog, ssp->p.rprog, fp);
    m = -M_prog;
  } else if (is_prev(prev, M_egrep)) {
    llp->p.grep = delete_name(llp->p.grep, ssp->p.grep, ssp->p.rgrep, fp);
    m = -M_egrep;
  } else if (is_prev(prev, M_cmd)) {
    llp->p.cmd = delete_name(llp->p.cmd, ssp->p.cmd, NULL, fp);
    m = -T_cmd2;
  } else m = 0;
  return m;
}

int get_hup(int pid0)/*usilic*/
{ /* kill child process after exit of usilic */
  static int pid;

  if (pid0 > 0) pid = pid0;
  else if (pid0 < 0) pid = 0;
  return pid;
}

static void mode_cont(int dummy)
{
  signal(SIGCONT, mode_cont);
  if (s_S(S_pid)) print_pid(getpid());
}

void do_prog(char *str, STR **cmdp, STR **histp)/*usilic*/
{
  int pid;
  void (*cont)(int);
  static PN pn;

  *cmdp = save_str(*cmdp, str, &pn);
  *histp = save_history(*histp, str, 1);
  cont = signal(SIGCONT, mode_cont);
  vt_clear();
  vt_init(-2);
#if 0
  signal(SIGCHLD, SIG_DFL);
#endif
  if (!(pid = fork())) {
    load_signal();
    unsetenv("USILIC"); /* enable to launch `usilic' from PROGRAM-mode */
    execlp("sh", "sh", "-c", str, NULL);
  }
  fprintf(stdout, "\n%s: %s\n", s_S(S_usilic), str);
  get_hup(pid);
#if 0
  wait(NULL);
#else
  while (!kill(pid, 0)) usleep(100000);
#endif
  get_hup(-1);
#if 0
  signal(SIGCHLD, sig_child);
#endif
  signal(SIGCONT, cont);
  echo_mess(1);
}

void save_ll(int n_ll, LL *llp, SS *ssp, char full[], PN *pnp)/*usilic*/
{
  int f_cut = 0;

  do {
    switch(n_ll) {
    case 0:
      do {
	if (*full == ' ') {
	  if (is_name(full, M_fgrep)
	      || is_name(full, T_pipe2)
	      || is_name(full, M_du)
	      || is_name(full, M_error)
	      || is_name(full, M_log)) continue;
	  if (is_name(full, M_egrep) 
	      && strlen(full) != strlen(s_P(P_mode)[M_egrep])) continue;
	  if (is_name(full, M_cut) && strchr(full, '\042')) continue;
	} else if (!is_name(full, M_help)) continue;
	ssp->p.mode = save_str(ssp->p.mode, full, pnp);
      } while (0);
      break;
    case 1:
      if (*full != '/' || strchr(full, '\0')[-1] == '/') continue; 
      ssp->p.file = save_str(ssp->p.file, full, pnp);
      ssp->p.hfile = save_history(ssp->p.hfile, full, 1);
      break;
    case 2:
      if (*full != '/' || strchr(full, '\0')[-1] != '/') continue;
      ssp->p.dir = save_str(ssp->p.dir, full, pnp);
      ssp->p.hdir = save_history(ssp->p.hdir, full, 1);
      break;
    case 3:
      if (*full == '/' || *full == ' ') continue;
      ssp->p.prog = save_str(ssp->p.prog, full, pnp);
      break;
    case 4:
      if (*full == '/' || *full == ' ') continue;
      ssp->p.man = save_str(ssp->p.man, full, pnp);
      ssp->p.hman = save_history(ssp->p.hman, full, 1);
      break;
    default:
      if (n_ll < 0) continue;
      break;
    }
    f_cut = 1;
    llp->a[n_ll] = (u_long)save_list((LIST*)llp->a[n_ll], full, pnp);
  } while (0);
  if (is_name(full, M_cut) && !f_cut) 
    llp->a[0] = (u_long)save_list((LIST*)llp->a[0], full, pnp);
}

int do_edit(int row, char name[], char arg[], int f_M)/*mode*/
{
  int pid;
  void (*cont)(int);
  CHILD *chp;
  struct stat st, st_file;

  switch(f_M) {
  case T_tar:
  case T_lha:
  case T_unzip:
  case T_rpm:
  case T_deb:
  case T_buff:
  case T_cut:
  case T_split:
    return echo_mess(S_errv);
    break;
  default:
    if (strchr(arg, '(') 
	|| *name == ' '
	|| ((is_fm()) && *name != '/'))
      return echo_mess(S_errv);
    else if (!(chp = get_child(NULL, s_P(P_mode)[M_edit], NULL, name, NULL)))
      return echo_mess(-ENOENT);
    break;
  }
  vt_init(-2);
  sprintf(chp->argv[1], "+%d", row);
  stat(name, &st_file);
  cont = signal(SIGCONT, mode_cont);
#if 0
  signal(SIGCHLD, SIG_DFL);
#endif
  if (!(pid = fork())) {
    load_signal();
    execvp(*chp->argv, chp->argv);
  }
  get_hup(pid);
#if 0
  wait(NULL);
#else
  while (!kill(pid, 0)) usleep(100000);
#endif
  get_hup(-1);
#if 0
  signal(SIGCHLD, sig_child);
#endif
  signal(SIGCONT, cont);
  vt_init(2);
  stat(name, &st);
  return (st.st_ctime == st_file.st_ctime) ? -M_clear : 0;
}

void do_exec(char name[], CHILD *chp)/*usilic*/
{
  int i, j, pid;
  void (*cont)(int);
  char **argv, *arg;

  i = j = 0;
  j = strlen(chp->argv[i++]) + 1;
  while (i < chp->f.argc) j += strlen(chp->argv[i++]) + 1;
  j += strlen(name) + 1 + 2;
  arg = alloca(j);
  i = 0;
  sprintf(arg, "%s ", chp->argv[i++]);
  while (i < chp->f.argc) sprintf(strchr(arg, '\0'), "%s ", chp->argv[i++]);
  sprintf(strchr(arg, '\0'), "\047%s\047", name);
  vt_clear();
  vt_init(-2);
  cont = signal(SIGCONT, mode_cont);
  if (!(argv = alloc_arg(arg, NULL)))
    save_error("%s: %s: %s.\n", s_S(S_usilic), arg, strerror(ENOENT));
  else {
#if 0
    signal(SIGCHLD, SIG_DFL);
#endif
    if (!(pid = fork())) {
      load_signal();
      execvp(*argv, argv);
    }
    fprintf(stdout, "\n%s: %s\n", s_S(S_usilic), arg);
    get_hup(pid);
#if 0
    wait(NULL);
#else
    while (!kill(pid, 0)) usleep(100000);
#endif
    get_hup(0);
#if 0
    signal(SIGCHLD, sig_child);
#endif
  }
  signal(SIGCONT, cont);
  echo_mess(1);
  free_arg(argv);
}

void clear_base(BASE *base)/*usilic*/
{
  if (base->buff) {
    free(base->buff);
    base->buff = NULL;
  }
  base->size = 0;
}

PP *free_pp(PP *pp)/*usilic*/
{
  PP *p0;

  do {
    if (pp->f.str) free(pp->cs);
    if (pp->attr) free(pp->attr);
    p0 = pp;
    pp = pp->next;
    free(p0);
  } while (pp);
  return pp;
}

static int print_arc(char *cp, char dir[], FILE *fp)
{
  CHILD *chp, *chp2;
  char *p;
  int i;

#ifdef SKIP_SPC
 debug("print_arc:%s\n", cp);
#endif
  skip_spc(cp, 1);
  if (!(chp = check_ext(cp, NULL, 0))) return 0;
  do {
    switch(is_chp(chp, NULL)) {
    case 1:
    case 2:
      return 0;
      continue;
    case 3:
      fprintf(fp, "%s %s ", chp->argv[0], s_S(S_arc3));
      break;
    case 4:
      fprintf(fp, "%s %s ", chp->argv[0], s_S(S_arc2));
      break;
    case 5:
      fprintf(fp, "%s %s ", chp->argv[0], s_S(S_arc1));
      break;
    case 7:
    case 6:
      chp2 = (CHILD*)chp->argv[chp->f.argc + 1];
      p = alloca(1 + strlen(dir) + strlen(cp) + 1 + 1);
      sprintf(p, "\047%s%s\047", dir, cp);
      *cp = '\0';
      chp2->argv[chp2->f.argc] = p;
      for (i = 0; chp2->argv[i]; i++) fprintf(fp, "%s ", chp2->argv[i]);
      fprintf(fp, "| %s %s %s ", chp->argv[0], s_S(S_arc1), chp->argv[2]);
      return 2;
    case 8:
      fprintf(fp, "%s \047%s%s\047 | cpio -%s "
	      , s_S(S_rpm2cpio), dir, cp, s_S(S_arc4));
      return 3;
      break;
    case 9:
      chp = get_child(NULL, NULL, s_S(S_gzip), "-f", NULL);
      chp2 = get_child(NULL, ".tar", s_S(S_tar), "-", NULL);
      fprintf(fp, "ar %s \047%s%s\047 data.tar.gz | %s %s %s | %s %s %s "
	      , s_S(S_arc5), dir, cp
	      , chp->argv[0], chp->argv[1], chp->argv[2]
	      , chp2->argv[0], s_S(S_arc1), chp2->argv[2]);
      return 3;
      break;
    }
    fprintf(fp, "\047%s", dir);
  } while (0);
  return 1;
}

char *command(int f_Mb, u_char str[], BASE *bp, LINE *lp, int klen, u_char du[], int *fdp)/*usilic*/
{
  PP *pp;
  char *rm;
  u_char *p, *cp, *dir;
  u_char tmp[N_char], tmp2[N_char];
  int f_M, i, pv[2], pid, f_sh, f_du;
  struct stat st;
  FILE *fp;

  if (*str != '/') {
    p = alloca(strlen(lp->dir) + strlen(str) + 1);
    strcpy(p, lp->dir);
    strcat(p, str);
    strcpy(str, p);
  }
  f_sh = (f_du = is_prev(lp->prev, M_du)) || *lp->prev == '/';
  if (f_Mb == T_arc || f_Mb == T_arc3) {
    f_du = 0;
    cp = str;
  } else cp = (f_du) ? du : lp->dir;
  dir = alloca(strlen(cp) + 1);
  strcpy(dir, cp);
  if (f_sh) {
    rm = malloc(strlen(s_S(S_path)) + 2);
    strcpy(rm, is_inst(s_S(S_path), s_S(S_RM)));
  } else rm = NULL;
  pipe(pv);
  if (!(pid = fork())) {
    load_signal();
    close(pv[0]);
    fp = fdopen(pv[1], "w");
    if (f_sh) {
      fprintf(fp, "cd \047%s\047\n", dir);
      fprintf(fp, "if ! ");
      p = alloca(strlen(s_S(S_path)) + 2);
      do {
	switch(f_Mb) {
	case T_cp:
	  strcpy(p, is_inst(s_S(S_path), s_S(S_CP)));
	  break;
	case T_mv:
	  strcpy(p, is_inst(s_S(S_path), s_S(S_MV)));
	  break;
	case T_rm:
	  strcpy(p, rm);
	  break;
	default:
	  continue;
	}
	fprintf(fp, "%s ", p);
      } while (0);
    }
    if (f_Mb == T_arc3) {
      cp = &strchr(str, '\0')[1];
      if (*cp != '/') {
	memmove(&cp[strlen(lp->dir)], cp, strlen(cp) + 1);
	memcpy(cp, lp->dir, strlen(lp->dir));
      }
      if ((i = print_arc(cp, "", fp)) != 3 && i != 2) 
	fprintf(fp, "%s\047 ", cp);
    }
    f_M = is_comm(f_Mb, 1);
    *tmp2 = '\0';
    for (pp = bp->pp; pp; pp = pp->next) {
      strcpy(tmp, pp->cs);
      if (pp->split == -f_M) {
	if (f_sh) {
	  if (f_du) {
	    if (!(cp = strchr(tmp, '/'))) continue;
	    cp++;
	  } else if (f_Mb == T_arc3 
		     && strchr(tmp, '\0')[-1] == '/'
		     && !strstr(tmp, " -> ")) {
	    if (*tmp2) {
	      cp = &tmp[get_col(tmp)];
	      while (cp[-1] != ' ') cp--;
	      if (strncmp(cp, tmp2, strlen(tmp2))) fprintf(fp, "%s ", tmp2);
	    } else cp = NULL;
	    if (!cp) {
	      cp = &tmp[get_col(tmp)];
	      while (cp[-1] != ' ') cp--;
	    }
	    strcpy(tmp2, cp);
	    continue;
	  } else {
	    if (*tmp2) *tmp2 = '\0';
	    if ((cp = strchr(tmp, '\t'))) cp++; /* for ARC on DU */
	    else cp = tmp;
#ifdef SKIP_SPC
 debug("!T_arc3=%s\n", cp);
#endif
	    skip_spc(cp, (f_Mb == T_arc3) ? 1 : 2);
	  }
	} else cp = tmp;
	i = 0;
	if (f_sh && f_Mb == T_arc)
	  if (!(i = print_arc(cp, lp->dir, fp)) || i == 3) {
	    if (!i) fprintf(fp, "# %s.", s_S(S_errc));
	    continue;
	  }
	if (f_sh) {
	  if (*cp == '/') cp++;
	  if (i != 1 && (i = *cp)) fputc('\047', fp);
	  while (*cp) fputc(*cp++, fp);
	  if (i) fputc('\047', fp);
	  fputc(' ', fp);
	} else {
	  for (; *cp; cp++) fputc(*cp, fp);
	  fputc('\n', fp);
	}
      }
    }
    if (*tmp2) fprintf(fp, "%s ", tmp2);
    if (f_sh) {
      if (f_Mb != T_rm && f_Mb != T_arc && f_Mb != T_arc3) {
	fputc('\047', fp);
#ifdef SKIP_SPC
 debug("!T_rm..:%s\n", str);
#endif
	skip_spc(str, 0);
	fprintf(fp, "%s", str);
	fputc('\047', fp);
      }
      fprintf(fp, "; then\necho %d > %s\nfi\n", S_errw, s_S(S_intr));
      fputc('\n', fp);
    }
    fclose(fp);
    exit(0);
  } else for (pp = bp->pp; pp; pp = pp->next) if (pp->split) pp->split = 0;
  close(pv[1]);
  if (f_sh) {
    FILE *ifp, *ofp;
    u_char *p;
    CHILD *chp;
    ofp = fopen(s_S(S_w), "w");
    ifp = fdopen(pv[0], "r");
    chp = get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL);
    fprintf(ofp, "cat %s\n", s_S(S_w));
    if ((cp = strchr(rm, ' '))) *cp = '\0';
    fprintf(ofp, "%s %s\n", rm, s_S(S_w)); /* ==> shell.c (9) */
    while ((i = fgetc(ifp)) != EOF) fputc(i, ofp);
    fclose(ifp);
    fclose(ofp);
    chmod(s_S(S_w), S_IXUSR|S_IWUSR|S_IRUSR);
    if (f_Mb != T_rm && f_Mb != T_arc && f_Mb != T_arc3)
      if (!stat(str, &st))
	if (S_ISDIR(st.st_mode))
	  if (*str == '/') {
	    p = alloca(strlen(str) + 1);
	    strcpy(p, str);
	  } else {
	    p = alloca(strlen(dir) + strlen(str) + 1);
	    strcpy(p, dir);
	    strcat(p, str);
	  }
	else {
	  get_dir(str);
	  p = alloca(strlen(str) + 1);
	  strcpy(p, str);
	}
      else {
	cp = &strchr(str, '\0')[-1];
	if (*cp == '/') *cp = '\0';
	get_dir(str);
	p = alloca(strlen(str) + 1);
	strcpy(p, str);
      }
    else {
      p = alloca(strlen(dir) + 1);
      strcpy(p, dir);
    }
    cp = alloca(strlen(s_S(S_w)) + strlen(s_S(S_usilic)) + strlen(p) + 3);
    sprintf(cp, "%s\r%s %s", s_S(S_w), s_S(S_usilic), p);
    free(rm);
    return strdup(cp);
  } else {
    *fdp = pv[0];
    return NULL;
  }
}

int is_write(u_char name[], int f_dir)/*mode*/
{
  u_char *cp;
  struct stat st;
  int i, f_symlink;

  if (!getuid()) return 1;
  cp = alloca(strlen(name) + 1);
  strcpy(cp, name);
#ifdef SKIP_SPC
debug("is_write:%s\n", cp);
#endif
  skip_spc(cp, 1);
  lstat(cp, &st);
  f_symlink = S_ISLNK(st.st_mode);
  i = stat(cp, &st);
  switch(f_dir) {
  case 1:
    if (i < 0) {
      save_error("%s: %s: %s.\n", s_S(S_usilic), cp, strerror(ENOENT));
      return -ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) {
      save_error("%s: %s: %s.\n", s_S(S_usilic), cp, s_S(S_err7));
      return 0;
    }
    break;
  case -1:
    if (i < 0) return 1;
    break;
  }
  if (f_symlink || !access(cp, W_OK) || (!i && st.st_uid == getuid())) 
    return 1;
  return -EACCES;
}

CHILD *set_cutchp(char arg[])/*usilic*/
{
  char *cp;

  if (*arg != K_z && (cp = strchr(arg, '('))) {
    char *p = alloca(strlen(&cp[1]) + 1);
    strcpy(p, &cp[1]);
    *strchr(p, ')') = '\0';
    return get_child(NULL, NULL, p, NULL, NULL);
  }
  return NULL;
}

void print_intr(char *save, char *mess)/*usilic*/
{
  static int f_errg;
  char *p, *cp;
  int n_error, i, j, nlen;

  if (save == F8) {
    if (load_intr() == S_errg) unlink(s_S(S_intr));
    f_errg = 0;
    return;
  }
  if (mess == F8 && !f_errg) return;
  if (f_errg) {
    n_error = S_errg;
    nlen = 0;
    vt_sc();
    vt_endstand();
    cp = p = NULL;
    vt_move(vt_row(0) - 1, nlen);
    p = s_S(n_error);
    cp = malloc(strlen(vt_geta(A_md)) + strlen(p) 
		+ strlen(vt_geta(A_me)) + 5);
    sprintf(cp, " (%s%s%s) ", vt_geta(A_md), p, vt_geta(A_me));
    i = strlen(vt_geta(A_md)) + strlen(vt_geta(A_me));
    if ((j = strlen(cp) - i - vt_col(0)) > 0) {
      j = strlen(cp) - i - j;
      cp[j + strlen(vt_geta(A_md))] = '\0';
    }
    vt_printf("%s", cp);
    vt_flush();
    vt_rc();
    free(cp);
  }
  if (!access(s_S(S_intr), F_OK)) {
    if ((n_error = load_intr()) == S_errg || n_error == -S_errg) {
      if ((f_errg = 1 + (n_error < 0)) == 2) {
	save_intr(S_errg);
	n_error = -n_error;
      }
      nlen = 0;
    } else nlen = disp_window(NULL, NULL, -1, NULL, NULL, NULL, NULL);
    vt_sc();
    vt_endstand();
    cp = p = NULL;
    if (!mess || mess == F8) mess = "";
    vt_move(vt_row(0) - 1, nlen);
    p = (n_error < 0) ? strerror(-n_error) : s_S(n_error);
    cp = alloca(strlen(vt_geta(A_md)) + strlen(p) + strlen(mess)
		+ strlen(vt_geta(A_me)) + 5);
    sprintf(cp, " (%s%s%s%s) ", vt_geta(A_md), p, mess, vt_geta(A_me));
    if (n_error == S_errp || n_error == S_errq) p = NULL;
    else if (f_errg != 2) vt_putchar(K_g);
    i = strlen(vt_geta(A_md)) + strlen(vt_geta(A_me));
    if ((j = strlen(cp) - i + nlen - vt_col(0)) > 0) {
      j = strlen(cp) - i - j;
      cp[j + strlen(vt_geta(A_md))] = '\0';
    }
    vt_printf("%s", cp);
    vt_flush();
    vt_rc();
    if (!vt_mode('D')) 
      ; /* S_errm in SHELL etc. */
    else unlink(s_S(S_intr));
    if (save && p) save_error("%s: %s.\n", save, p);
  }
}
  
void ins_q(u_char str[])/*mode*/
{
  u_char *cp;

  for (cp = str; *cp; cp++) 
    if (*cp < ' ') {
      memmove(&cp[1], cp, strlen(cp) + 1);
      *cp++ = K_q;
    }
}

#include "usilic.h"
#include <signal.h>
#include <termios.h>
static void rv_str(RV *rvp, int f_M)
{
  if (f_M == M_shell /* rvp->pp->cs == F3 : virtual line (SHELL prompt) */
      || !strlen(rvp->str)) return; /* T_file: rvp->str == "" */
  strcpy(rvp->str, &rvp->pp->cs[rvp->col]);
  if (!strlen(rvp->str)) strcpy(rvp->str, " ");
  rvp->str[vt_col(0) - rvp->col - (is_ls(f_M) != 0)] = '\0';
}

static int set_top(PP *pp)
{
  int top;

  for (top = 0; pp->prev; top++, pp = pp->prev) ;
  return top;
}

int move_page(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M)/*usilic*/
{
  FILE *pin;
  int i = 0, j, f_mark, top;

  pin = (f_M == M_egrep || f_M == M_fgrep || f_M == M_shell) ? NULL : *pinp;
  f_mark = fmark(f_M);
  switch(m) {
  case M_back:
  case M_kback:
  case M_head:
    if (!rvp->ps->prev) {
      while (rvp->pp->prev) {
	rvp->pp = rvp->pp->prev;
	rvp->row--;
	pnp->row--;
      }
      rv_str(rvp, f_M);
      if (pinp) *pinp = pin;
      pnp->line = rvp->pp->f.line;
      return 0;
    }
    while (rvp->row) {
      rvp->pp = rvp->pp->prev;
      rvp->row--;
      pnp->row--;
    }
    if (pnp->nrow > vt_row(0) - 1) {
      if (m == M_head)
	while (rvp->ps->prev) {
	  rvp->ps = rvp->ps->prev;
	  rvp->pe = rvp->pe->prev;
	  rvp->pp = rvp->pp->prev;
	}
      else  
	for (i = 0; i < vt_row(0) - 1 && rvp->ps->prev; i++) {
	  rvp->ps = (vt_mode('H')) 
	    ? sr_row(rvp->ps->prev, f_mark) : rvp->ps->prev;
	  rvp->pe = rvp->pe->prev;
	  rvp->pp = rvp->pp->prev;
	}
    }
    if (!vt_mode('H') || m == M_head) disp_pp(rvp->ps, f_mark);
    rv_str(rvp, f_M);
    break;
  case M_tail:
    if (pin && pin != F1) pin = seek_intr(pin, &pnp->nrow);
  case M_forw:
  case M_kforw:
    while (1) {
      if (!rvp->pe->next) {
	if (pin && pin != F1) {
	  for (i = pnp->nrow; pin && pnp->nrow - i < vt_row(0) - 2; ) 
	    alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	  if (pin) continue;
	}
	while (rvp->pp->next) {
	  rvp->pp = rvp->pp->next;
	  rvp->row++;
	  pnp->row++;
	}
	rv_str(rvp, f_M);
	if (pinp) *pinp = pin;
	pnp->line = rvp->pp->f.line;
	return 0;
      }
      break;
    }
    while (rvp->row < vt_row(0) - 2) {
      rvp->pp = rvp->pp->next;
      rvp->row++;
      pnp->row++;
    }
    if (pnp->nrow > vt_row(0) - 1) {
      if (m == M_tail)
	if (pin && pin != F1)
	  while (rvp->pe->next->next) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = rvp->pe->next;
	    rvp->pp = rvp->pp->next;
	  }
        else
	  while (rvp->pe->next) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = rvp->pe->next;
	    rvp->pp = rvp->pp->next;
	  }
      else
	for (i = 0; ; ) {
	  for (; i < vt_row(0) - 1 && rvp->pe->next; i++) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = (vt_mode('H')) 
	      ? sf_row(rvp->pe->next, f_mark) : rvp->pe->next;
	    if (!rvp->pp->next) break;
	    rvp->pp = rvp->pp->next;
	  }
	  if (!rvp->pe->next && pin && pin != F1) {
	    for (j = pnp->nrow; pin && pnp->nrow - j < vt_row(0) - i; )
	      alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	    continue;
	  }
	  break;
	}
    }
    if (!vt_mode('H') || m == M_tail) disp_pp(rvp->ps, f_mark);
    rv_str(rvp, f_M);
    break;
  }
  if (pinp) *pinp = pin;
  top = pnp->top;
  pnp->top = set_top(rvp->ps);
  pnp->line = rvp->pp->f.line;
  tcflush(vt_tfd(0), TCIFLUSH);
  return i;
}

void move_row(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M)/*usilic*/
{
  FILE *pin;
  int f_mark, top;

  pin = (f_M == M_egrep || f_M == M_fgrep || f_M == M_shell) ? NULL : *pinp;
  f_mark = is_comm(f_M, 0) || is_comm(f_M, 1);
  switch(m) {
  case M_prev:
  case M_kprev:
    if (!rvp->ps->prev) {
      while (rvp->pp->prev) {
	rvp->pp = rvp->pp->prev;
	rvp->row--;
	pnp->row--;
      }
      rv_str(rvp, f_M);
    }
    if (pnp->nrow <= vt_row(0) - 1) return;
    while (rvp->row) {
      rvp->pp = rvp->pp->prev;
      rvp->row--;
      pnp->row--;
    }
  case M_kup:
  case M_up:
    do {
      if (rvp->row) {
	rvp->row--;
	pnp->row--;
      } else if (rvp->ps->prev) {
	rvp->ps = sr_row(rvp->ps->prev, f_mark);
	rvp->pe = rvp->pe->prev;
      } else continue;
      rvp->pp = rvp->pp->prev;
    } while (0);
    rv_str(rvp, f_M);
    break;
  case M_next:
  case M_knext:
    if ((!pin || pin == F1) && !rvp->pe->next) {
      while (rvp->pp->next) {
	rvp->pp = rvp->pp->next;
	rvp->row++;
	pnp->row++;
      }
      rv_str(rvp, f_M);
    }
    if (pnp->nrow <= vt_row(0) - 1) return;
    while (rvp->row < vt_row(0) - 2) {
      rvp->pp = rvp->pp->next;
      rvp->row++;
      pnp->row++;
    }
  case M_kdn:
  case M_dn:
    do {
      if ((!pin || pin == F1) && pnp->row > pnp->nrow - 2 - (f_M == M_shell))
	continue;
      if (rvp->row < vt_row(0) - 2) {
	rvp->row++;
	pnp->row++;
      } else if (pin && pin != F1 && rvp->pe->next && !rvp->pe->next->next) {
	alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	rvp->pe = sf_row(rvp->pe->next, f_mark);
	rvp->ps = rvp->ps->next;
      } else if (rvp->pe->next) {
	rvp->pe = sf_row(rvp->pe->next, f_mark);
	rvp->ps = rvp->ps->next;
      } else continue;
      rvp->pp = rvp->pp->next;
    } while (0);
    rv_str(rvp, f_M);
    break;
  }
  if (pinp) *pinp = pin;
  top = pnp->top;
  pnp->top = set_top(rvp->ps);
  pnp->line = rvp->pp->f.line;
  tcflush(vt_tfd(0), TCIFLUSH);
}

PP *sr_row(PP *pp, int f_mark)/*usilic*/
{
  if (!vt_mode('D')) return pp;
  vt_move(0, 0);
  vt_sr();
  put_str(pp, f_mark);
  return pp;
}  

PP *sf_row(PP *pp, int f_mark)/*usilic*/
{
  if (!vt_mode('D')) return pp;
  if (vt_mode('H')) vt_move(vt_row(0) - 1, 0);
  else vt_move(vt_row(0) - 2, 0);
  if (vt_mode('H')) {
    vt_eeol();
    put_str(pp, f_mark);
    vt_sf();
  } else {
    vt_sf();
    vt_eeol();
    put_str(pp, f_mark);
  }
  return pp;
}  

int init_col(int f_M, char str[], int size)/*usilic*/
{
  int col, row;

  if (size < 0) return size;
  row = vt_row(0);
  col = 0;
  switch(f_M) { 
  case M_lfile: case M_ldir: 
  case M_cut: case T_cut:
  case M_prog: 
  case M_man:
  case M_egrep: case T_grep:
  case T_locate:
  case M_hist: case T_hist:
  case T_hfile: case T_hdir: case T_hman: case T_hlocate:
  case T_hfgrep: case T_hegrep:
  case M_cmd: case T_cmd:
  case M_list: case T_list:
  case M_du: case T_du:
  case M_key:
    col = 0;
    break;
  case T_dir:
  case T_tar:
  case T_lha:
  case T_unzip:
  case T_rpm:
  case T_deb:
    col = get_col(str);
    break;
  case M_cp:
  case M_mv:
  case M_rm:
  case M_arc:
  case T_arc2:
  case T_key:
  default:
    col = -1;
    break;
  }  
  return col;
}

int init_row(PP **ppp, PN *pnp, int tell, int etop)/*usilic*/
{
  int i, j, row;
  PP *pp;

  pp = *ppp;
  row = pnp->row;
  if (pnp->nrow > vt_row(0) - 1) {
    if (tell) {
      row = 0;
      if ((j = (pnp->top = tell - 1) > etop)) pnp->top = etop;
    } else j = 0;
    for (i = 0; i < pnp->top && pp->next; i++) pp = pp->next;
    if (j) for (i = tell - etop - 1; i > 0; i--) row++;
  } else if (tell) for (row = 0, i = tell - 1; i > 0; i--) row++;
  pnp->row = row;
  *ppp = pp;
  return row;
}

int get_col(char str[])/*usilic*/
{
  int col;
  char *cp, *p = alloca(strlen(str) + 1);

  strcpy(p, str);
  if ((cp = strstr(p, " -> "))) *cp = '\0';
  if ((cp = strchr(p, '\\'))) {
    while (*cp != ' ' && cp > p) cp--;
    col = cp - p + 2;
  } else col = (cp = strrchr(p, ' ')) ? cp - p + 2 : 1;
  return col - 1;
}

#include "usilic.h"
#if 0
#define CHECK_SFD
#endif
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

static struct {
  struct {
    u_int nobl	: 1;
    u_int init	: 2;
    u_int alen	: 4;
    u_int blen	: 4;
    u_int clen	: 5;
  } f;
} Shell;

typedef struct {
  u_int bash	: 1;
  u_int bs	: 2;
  u_int i_errp	: 2;
  u_int i_err	: 5;
  u_int n_err	: 8;
} FLAG;

typedef union {
  struct { /* for write_shell() (parent process) */
    int *crp;
    int *colp, *rowp;
    int *col0p, *colep;
    int *colbp;
    int *lenp;
    u_char *dir, *version;
  } w;
  struct { /* for read_shell() (child process) */
    int *row0p, *lenp, *ip;
    RV *rvp;
    PN *pnp;	
    PP **p0p;
    u_char *buff, *prev;
    u_char *name, *prompt, *arg;
  } r;
} SHELL;

static void write_shell(void *p)
{
  static int row, col, col0, colb, cole, len, f_cr, *pv;
  static char dir[N_line], version[80];
  SHELL *sp;
  u_char *cp, *cp2;
  int f_disp;

  switch(Shell.f.init) {
  case 3:
    pv = NULL;
    return;
  case 2:
    sp = p;
    sp->w.crp = &f_cr;
    sp->w.colp = &col;
    sp->w.rowp = &row;
    sp->w.col0p = &col0;
    sp->w.colbp = &colb;
    sp->w.colep = &cole;
    sp->w.lenp = &len;
    sp->w.dir = dir;
    sp->w.version = version;
    strcpy(version, vt_geta(A_md));
    strcat(version, getenv("USILIC"));
    strcat(version, vt_geta(A_me));
    break;
  case 1:
    pv = p;
    break;
  case 0:
    if (!pv) return;
    read(pv[4], &len, sizeof(int));
    if (!len) /* <== (4) */
      f_cr = 1;
    else {
      read(pv[4], dir, len);
      if ((cp = strstr(dir, version))) {
	if ((cp2 = strstr(dir, ":~"))) {
	  memmove(&dir[strlen(s_S(S_home))], &cp2[3], strlen(&cp2[3]) + 1);
	  memcpy(dir, s_S(S_home), strlen(s_S(S_home)));
	} else strcpy(dir, &cp[strlen(version) + 1]);
	if (!strlen(dir)) ; /* garbage */
	else {
	  strcpy(&strchr(dir, '\0')[-2], "/");
	  chdir(dir); /* go to the directory specified by SHELL-prompt */
	} 
      }
      read(pv[4], &row, sizeof(int));
      read(pv[4], &col0, sizeof(int));
      read(pv[4], &f_disp, sizeof(int));
      col = cole = colb = col0; /* (7) */
      len = vt_col(0) - col0 - 1;
      if (!f_disp && row > 1 && load_intr() != S_errw) {
	if (!Shell.f.nobl)
	  echo_mess(S_errg); /* "Exit" */
	else echo_mess(-S_errg); /* without BL (0x07) */
      }
    }
    write(pv[7], &len, sizeof(int));
    break;
  default:
    break;
  }
}

static int input_shell(int spid, void *w, u_char prev[], char str[], FILE *ofp, FILE **pinp, LIST *prog, LIST **listp, STR **cmdp, STR **histp, int *ppidp, int f_shell)
{
  u_char *cp;
  int c, i, m, n, colb, f_list, ti, f_esc, st_size = 0;
  struct stat st;
  static PN pn;
  static int *pv, pos, ilen, shift, col_b;
  static u_char buff[N_pipe+1], pp_str[N_pipe+1];
  static u_char *mess, *wb, comm[N_pipe+1], *w2;
  static struct {
    u_int cmd	: 1;
    u_int skip	: 2;
    u_int w2	: 2;
  } ff;
  static SHELL s;

  if (!s.w.dir && f_shell >= 0) {
    Shell.f.init = 2;
    write_shell(&s);
    Shell.f.init = 0;
  }
  i = m = 0;
  switch(f_shell) {
  case -1:
    if (wb) free(wb);
    if (w2) {
      free(w2);
      w2 = NULL;
    }
    return 0;
  case 0:
    pv = (int*)w;
    mess = prev;
    pos = M_tail;
    Shell.f.init = 1;
    write_shell(pv);
    Shell.f.init = 0;
    return 0;
  case 1:
  case 2:
  case 3:
    Shell.f.init = 0;
    if (f_shell == 2) {
      m = T_tag2;
      vt_mode(-'d');
    } else m = T_tag;
    i = vt_mode('W'); /* 'W' == 1 : resize window */
    sprintf(comm, "%d\n%d\n%s\n%d\n%d\n%d\n"
	    , vt_row(0), vt_col(0), prev, vt_charset(0), vt_mode('H'), i);
    write_comm(pv[1], m, comm, spid);
    i = wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
    if (vt_mode('W')) {
      vt_mode(-'w');
      if (col_b != vt_col(0)) {
	if (col_b > vt_col(0)) col_b = vt_col(0); 
	if (i >= col_b / 4 * 3 + Shell.f.alen)
	  read_m(NULL, 2, 0, "\r"
		 , NULL, NULL, NULL, NULL, NULL, NULL); /* (5) */
      }
    }
    print_intr(NULL, NULL); /* S_errm etc. */
  case 4:
    if (*str) {
      do {
	if (ff.cmd || f_shell >= 3) 
	  ff.cmd = ilen = 0;
	strcpy(&buff[ilen], str);
	*s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	m = -K_e;
      } while (0);
    } else {
      m = 0;
      if (w) {
	if (shift) shift = 0;
	*s.w.colp = *s.w.colep = *s.w.col0p;
	ilen = 0;
	*buff = '\0';
      }
      if (ff.cmd) ff.cmd = 0;
    }
    chdir(s.w.dir);
    if (i || f_shell == 4) {
      vt_move(*s.w.rowp, *s.w.colp);
      if (f_shell == 4) vt_eeol();
    }
    if (!access(mess, F_OK)) unlink(mess);
    break;
  default:
    m = 0;
    break;
  }
  for (f_list = f_esc = 0; ; m = (m > 0) ? 0 : m, f_list = f_list == 1) {
    if (w) {
      if (pos != M_tail) {
	write_comm(pv[1], M_tail, NULL, spid);
	wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      }
      if ((!strncmp(w, "cat", 3) || !strncmp(w, "echo", 4))
	  && strstr(w, s_S(S_w))) { /* ^Jw / ^J^W */
	ff.skip = 1;
	strcpy(&buff[ilen], w);
	*s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	m = -K_e;
	if (wb) free(wb);
	wb = strdup(w);
      } else {
	if (!strncmp(w, s_S(S_w), strlen(s_S(S_w)))) {
	  /* CP / MV / RM / ARC */
	  cp = &strchr(w, '\r')[1];
	  Shell.f.nobl = 1;
	  w2 = strdup(cp);
	  *cp = '\0';
	  ff.w2 = 0;
	  st_size = 0;
	  while (1) { /* (11) */
	    /* prompt for `rm -i' etc. doesn't work until (12) done. */
	    write_comm(pv[1], T_in, NULL, spid);
	    if (!wait_comm(pv[2], NULL, NULL, 0, buff, pp_str)) break;
	    usleep(10000);
	  }
	  write_comm(pv[1], T_out, NULL, spid);
	  wait_comm(pv[2], NULL, NULL, 0, NULL, NULL);
	}
	fwrite(w, 1, strlen(w), ofp);
	fflush(ofp);
	if (w2) 
	  while (!access(s_S(S_w), F_OK)) /* (9) */
	    usleep(10000); 
	if (!strncmp(w, "cd", 2)) *histp = save_history(*histp, w, 1);
      }
      w = NULL;
    } else if (!access(mess, F_OK)) {
      vt_mode(-'d');
      write_comm(pv[1], T_off, NULL, spid);
      wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
      if ((m = -from_main(prev, str, mess, pinp, ppidp))) break;
    }
    if (kill(spid, 0)) {
      pv = NULL;
      m = -M_bl;
      break;
    }
    colb = *s.w.col0p;
    n = 0;
    if (!m) {
      write_comm(pv[1], T_in, NULL, spid);
      if (wait_comm(pv[2], NULL, NULL, 0, buff, pp_str)) {
	/* Interaction */
	do {
	  if (vt_mode('W')) {
	    sprintf(comm, "%d\n%d\n%s\n%d\n%d\n%d\n"
		    , vt_row(0), vt_col(0), prev, vt_charset(0)
		    , vt_mode('H'), 1);
	    write_comm(pv[1], T_tag, comm, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	    vt_mode(-'w');
	    c = 0;
	  } else if ((c = vt_getch()) == K_ec)
	    f_esc = (c = read_esc(T_shell, NULL, NULL)) < 0;
	  if (c > N_char)
	    switch(c - N_char) {
	    case M_kup:
	    case M_kdn:
	    case M_knext:
	    case M_kprev:
	    case M_kback:
	    case M_kforw:
	      n = c - N_char;
	      continue;
	    default:
	      break;
	    }
	  if (!c) {
	    if (w2 && f_shell == 2) {
	      do {
		stat(s_S(S_log), &st);
		if (st.st_size == st_size) {
		  if (++ff.w2 == 3) {
		    FILE *err = fopen(s_S(S_log), "r");
		    fseek(err, -1, 2);
		    for (i = 0; i < 80; i++) {
		      if ((c = fgetc(err)) == '\r' || c == '?') break;
		      fseek(err, -2, 1);
		    }
		    fclose(err);
		    if (c == '?') {
		      vt_mode('d');
		      sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
		      write_comm(pv[1], T_disp, comm, spid);
		      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
				, &buff[shift], pp_str);
		      f_shell = 1;
		      continue;
		    }
		  } else continue;
		}
		ff.w2 = 0;
		st_size = st.st_size; 
	      } while (0);
	    }
	    continue;
	  }
	  if (w2 && f_shell != 5) {
	    if (f_shell == 2) {
	      vt_mode('d');
	      sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
	      write_comm(pv[1], T_disp, comm, spid);
	      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
			, &buff[shift], pp_str);
	      f_shell = 1;
	      continue;
	    }
	    f_shell = 5;
	    fputc(K_u, ofp); /* (10) */
	    fflush(ofp);
	  }
	  switch(c) {
	  case 0:
	    continue;
	  case K_v: /* ^V */
	    n = M_kforw;
	    continue;
	  case 'v':
	    if (f_esc) {
	      f_esc = 0;
	      n = M_kback;
	      continue;
	    }
	    break;
	  case K_h: /* ^H */
	    c = K_dl;
	  case K_dl: /* Back space */
	  case K_c: /* ^C */
	  case K_z: /* ^Z */
	  case K_d: /* ^D */
	  case K_m: /* ^M */
	    break;
	  case K_g: /* ^G */
	    write_comm(pv[1], T_off, NULL, spid);
	    wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
	    m = -M_bl;
	    goto jump;
	  case K_l:  /* ^L */
	    sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
	    write_comm(pv[1], T_disp, comm, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	    continue;
	    break;
	  default:
	    if (c < ' ') continue;
	    break;
	  }
	  fputc(c, ofp);
	  fflush(ofp);
	} while (0);
	if (*s.w.crp) {
	  *s.w.crp = 0;
	  fputc(K_c, ofp);
	  fflush(ofp);
	}
	if (!n) continue;
      } else if (f_esc) f_esc = 0;
    }
    if (w2) {
      fprintf(ofp, "%s\r", w2);
      fflush(ofp);
      free(w2);
      w2 = NULL;
      ff.w2 = 0;
    }
    if (!n) {
      col_b = *s.w.col0p;
      m = edit_line(m, M_shell, buff, *s.w.lenp, *s.w.rowp, *s.w.col0p
		    , s.w.colp, s.w.colep, &shift, NULL);
      if (col_b != *s.w.col0p) {
	/* set `*s.w.col0p' at (5),(6),(7) */
	col_b = *s.w.col0p - col_b;
	*s.w.colep += col_b;
	*s.w.colp += col_b;
      }
    } else m = n;
    switch(m) {
    case 0:
      if (*s.w.crp) {
	*s.w.crp = 0;
	fputc(K_c, ofp);
	fflush(ofp);
      }
      break;
    case M_clear:
      sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
      write_comm(pv[1], T_disp, comm, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
    case N_char:
    case M_egrep:
      break;
    case M_hist:
    case M_cmd:
      ff.cmd = 1;
    case M_list:
      if (*listp) *listp = free_list(*listp);
      if (m == M_list) {
	if ((ti = *s.w.colp + shift - *s.w.col0p)) buff[ti] = '\0';
	do {
	  if (!(i = ichrstr(buff, 0, "/~")))
	    if (!(i = ichrstr(buff, 0, " ~")))
	      if (*buff != '~') continue;
	  strcpy(comm, getenv("HOME"));
	  strcat(comm, &buff[i+1]);
	  strcpy(&buff[i], comm);
	} while (0);
	i = make_list(s.w.dir, buff, &ilen, listp, M_shell);
      } else i = -m;
      switch(i) {
      case 0:
	break;
      case 1:
	*s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	m = -K_e;
	continue;
      default:
	if (i < 0) vt_mode(-'d');
	else if (!f_list) {
	  f_list = 1;
	  *s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	  vt_putchar(K_g);
	  m = -K_e;
	  continue;
	} else {
	  f_list = 0;
	  vt_mode(-'d');
	}
	write_comm(pv[1], T_off, NULL, spid);
	wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
	strcpy(str, &buff[ilen]);
	m = (i < 0) ? i : -M_list;
	goto jump;
      }
      break;
    case M_susp:
      fputc(K_z, ofp);
      fflush(ofp);
      break;
    case M_kanji:
    case M_mark:
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
      break;
    case M_kup:
    case M_up:
    case M_prev: 
    case M_kprev: 
    case M_kback: case M_head:
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      break;
    case M_kdn:
    case M_dn:
    case M_next: 
    case M_knext: 
    case M_kforw: case M_tail:
      if (pos == M_tail) vt_putchar(K_g);
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      break;
    case M_jump:
      write_comm(pv[1], m, NULL, spid);
      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      break;
    case T_w:
      if (!wb || !*wb) {
	vt_putchar(K_g);
	continue;
      }
      ff.skip = 1;
      if (pos != M_tail) {
	write_comm(pv[1], M_tail, NULL, spid);
	wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      }
      i = strlen(wb);
      vt_move(*s.w.rowp, *s.w.colp);
      fputc(K_u, ofp); /* (3) */
      fflush(ofp);
      strcpy(&buff[ilen], wb);
      *s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
      m = -K_e;
      break;
    default:
      if (m < N_char)
	switch(m) {
	case M_du:
	  fprintf(ofp, "%s -u\n", s_S(S_usilic));
	  fflush(ofp);
	  continue;
	case M_chdir2:
	  i = strlen(s.w.dir);
	default:
	  vt_mode(-'d');
	  write_comm(pv[1], T_off, NULL, spid);
	  wait_comm(pv[2], &pos, NULL, 0, NULL, pp_str);
	  m = -m;
	  goto jump;
	  break;
	}
      else if (m >= N_num) {
	sprintf(comm, "%d\n", m - N_num - 1);
	write_comm(pv[1], N_num, comm, spid);
	wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
      } else
	switch(m - N_char) {
	case K_q:
	case K_s:
	  fputc(m - N_char, ofp);
	  fflush(ofp);
	  break;
	case K_r:
	  if (pos != M_tail) {
	    write_comm(pv[1], M_tail, NULL, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	  }
	  m = -K_r;
	  break;
	case K_c:
	  vt_move(*s.w.rowp, *s.w.col0p);
	  vt_eeol();
	  fputc(K_c, ofp);
	  fflush(ofp);
	  shift = f_list = 0;
	  *s.w.colp = *s.w.colep = *s.w.col0p;
	  *buff = '\0';
	  if (ff.skip) ff.skip = 0;
	  break;
	case '\r':
	  write_comm(pv[1], T_out, NULL, spid);
	  wait_comm(pv[2], NULL, NULL, 0, NULL, NULL);
	  if (pos != M_tail) {
	    if (strlen(pp_str) > (i = strlen(s.w.version)) 
		&& (!strncmp(pp_str, s.w.version, i))) { /* past prompt */
	      if (pp_str[i+1] == '~') i -= strlen(s_S(S_home)) - 2;
	      strcpy(buff, &(strchr(&pp_str[i+strlen(s.w.dir)], ' '))[1]);
	      write_comm(pv[1], M_tail, NULL, spid);
	      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, NULL, pp_str);
	      if ((cp = strchr(buff, '\r'))) *cp = '\0';
	      *s.w.colp = *s.w.colep = strlen(buff) + *s.w.col0p;
	    } else {
	      write_comm(pv[1], M_tail, NULL, spid);
	      wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
			, &buff[shift], pp_str);
	    }
	  }
	  if (*buff) {
	    if (Shell.f.nobl) Shell.f.nobl = 0;
	    vt_move(*s.w.rowp, 0);
	    vt_eeol();
	    for (cp = buff; *cp == ' ' || *cp == '\t'; cp++) ;
	    if (*cp) {
	      strcpy(comm, cp);
	      while (comm[(i = strlen(comm) - 1)] == ' ') comm[i] = '\0';
	      if ((cp = strchr(comm, '|'))) {
		char *p;
		while (1) {
		  p = &cp[1];
		  strcpy(buff, &cp[1]);
		  for (cp = buff; *cp == ' ' || *cp == '\t'; cp++) ;
		  strcpy(buff, cp);
		  if ((cp = strchr(buff, ' '))) *cp = '\0';
		  if (!strcmp(buff, s_S(S_usilic))) i = -1;
		  else i = find_prog(prog, buff);
		  if (i) break;
		  if (!(cp = strchr(p, '|'))) break;
		}
	      } else if (strchr(comm, '<')) {
		strcpy(buff, comm);
		*strchr(buff, '<') = '\0';
		if ((cp = strchr(buff, ' '))) *cp = '\0';
		if (!strcmp(buff, s_S(S_usilic))) i = -1;
		else i = find_prog(prog, buff);
	      } else {
		strcpy(buff, comm);
		if ((cp = strchr(buff, ' '))) *cp = '\0';
		if (!strcmp(buff, s_S(S_usilic))) i = -1;
		else i = (find_prog(prog, buff)) ? 2 : 0;
	      }
	      if (i > 0) {
		do_prog(comm, cmdp, histp);
		strcpy(buff, comm);
		sprintf(comm, "%d\n%d\n", vt_row(0), vt_col(0));
		write_comm(pv[1], T_disp, comm, spid);
		wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p
			  , &buff[shift], pp_str);
 		fputc(K_c, ofp);
	      } else {
		fwrite(comm, 1, strlen(comm), ofp);
		fputc('\r', ofp);
		if (!ff.skip) *cmdp = save_str(*cmdp, comm, &pn);
		if (ff.skip != 1) *histp = save_history(*histp, comm, 1);
	      }
	    } else fputc('\r', ofp);
	  } else fputc('\r', ofp);
	  fflush(ofp);
	  if (m >= 0) {
	    if (shift) shift = 0;
	    *s.w.colp = *s.w.colep = *s.w.col0p;
	    *buff = '\0';
	    f_list = 0;
	  }
	  if (ff.skip) ff.skip = 0;
	  break;
	default:
	  if (ff.skip == 1) ff.skip = 2;
	  if (pos != M_tail) {
	    write_comm(pv[1], M_tail, NULL, spid);
	    wait_comm(pv[2], &pos, s.w.rowp, *s.w.col0p, &buff[shift], pp_str);
	  }
	  m = -m;
	  break;
	}
      break;
    }
  }
jump:  
  vt_mode(-'d');
  col_b = vt_col(0);
  return m;
}

void write_comm(int pfd, int m, char *comm, int spid)/*usilic*/
{
  int len;

  kill(spid, SIGALRM);
  if (comm) {
    write(pfd, &m, sizeof(int));
    len = strlen(comm) + 1;
    write(pfd, &len, sizeof(int));
    write(pfd, comm, len);
  } else {
    m = -m;
    write(pfd, &m, sizeof(int));
  }
}

static int read_comm(int pfd, char comm[])
{
  int m, len;

  read(pfd, &m, sizeof(int));
  if (m > 0) {
    read(pfd, &len, sizeof(int));
    read(pfd, comm, len);
  } else {
    *comm = '\0';
    m = -m;
  }
  return m;
}

static PP *hide_ps(PP *ps)
{ /* save virtual line (prompt line) */
  while (ps->next) ps = ps->next;
  if (ps->prev) ps->prev->next = NULL;
  else return NULL;
  return ps;
}

static void disp_shell(RV *rvp, char name[], u_char prev[], char arg[], int row0, u_char prompt[], int len, u_char buff[])
{
  PP *ps = NULL;
  int i;

  if (!vt_mode('D')) vt_mode('d');
  print_line(rvp->ps, rvp->pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
  if ((i = strlen(prompt))) ps = hide_ps(rvp->ps);
  else {
    for (ps = rvp->ps; ps->next; ps = ps->next) ;
    ps->cs = buff;
    ps->f.len = strlen(ps->cs);
  }
  disp_window(rvp->ps, rvp->pp, T_shell, name, prev, arg, &len);
  if (i) {
    /* Command-line */
    if (ps) ps->prev->next = ps;
    if (!rvp->pe->next) { /* bottom page */
      vt_sc();
      vt_move(row0, 0); /* row0: cursor position (prompt line) */
      if (*rvp->str) {
	i = prompt[Shell.f.clen];
	prompt[Shell.f.clen] = '\0';
	vt_printf("%s...%s", prompt, rvp->str);
	prompt[Shell.f.clen] = i;
      } else vt_printf("%s", prompt);
      vt_eeol();
      vt_flush();
      vt_rc();
    }
    vt_move(rvp->row, rvp->col);
  } else {
    /* Interaction */
    for (ps = rvp->ps; ps->next; ps = ps->next) ;
    ps->cs = F3;
  }
}

static u_char *read_string(u_char *cp, u_char str[])
{
  int i = 0;

  for (i = 0; (str[i] = *cp++) != '\n'; i++) ;
  str[i] = '\0';
  return cp;
}

static void shell_row(RV *rvp, PN *pnp, int f_M, u_char buff[])
{
  PP *ps;

  ps = (!rvp->pp->next) ? hide_ps(rvp->ps) : NULL; /* save virtual line */
  move_row(f_M, rvp, pnp, NULL, M_shell);
  disp_window(rvp->ps, rvp->pp, M_shell, NULL, NULL, NULL, NULL);
  if (ps) ps->prev->next = ps; /* load virtual line */
}

static int shell_page(RV *rvp, PN *pnp, int f_M, u_char prompt[], u_char buff[])
{
  PP *ps;
  int i = 0;

  if (f_M == M_kback || f_M == M_head) ps = NULL;
  else if ((i = strlen(prompt))) 
    ps = hide_ps(rvp->ps); /* save SHELL-prompt line */
  else {
    for (ps = rvp->ps; ps->next; ps = ps->next) ;
    ps->cs = buff;
    ps->f.len = strlen(ps->cs);
  }
  move_page(f_M, rvp, pnp, NULL, M_shell);
  if (i) {
    ps->prev->next = ps; /* load SHELL-prompt line */
    if (rvp->pp->next) { /* not end line */
      if (rvp->pe->next && rvp->pe->next->cs != F3) 
	; /* not end page */
      else /* stopped at the end line after saving */
	shell_row(rvp, pnp, M_dn, buff);
    }
  } else if (ps) {
    ps->cs = F3;
    i = -1;
  }
  disp_window(rvp->ps, rvp->pp, M_shell, NULL, NULL, NULL, NULL);
  return i;
}

static int next_line(RV *rvp, PN *pnp, int c, int sf)
{
  PP *pp;
  if (!(pp = alloc_sh(F3, 1, c))) return rvp->row;
  if (rvp->row < vt_row(0) - 2) {
    rvp->pp = rvp->pp->next;
    rvp->pe = rvp->pe->next;
    pnp->nrow++;
    pnp->row++;
  } else {
    rvp->pp = rvp->pp->next;
    rvp->pe = rvp->pe->next;
    rvp->ps = rvp->ps->next;
    rvp->row--;
    pnp->nrow++;
    pnp->top++;
    if (sf && vt_mode('D')) {
      if (vt_mode('H')) vt_move(vt_row(0) - 1, 0);
      else vt_move(vt_row(0) - 2, 0);
      vt_sf();
      if (vt_mode('H')) vt_move(vt_row(0) - 2, 0);
      vt_eeol();
    }
  }
  rvp->row++;
  if (vt_mode('D'))
    disp_window(rvp->ps, rvp->pp, M_shell, NULL, NULL, NULL, NULL);
  if (rvp->row > vt_row(0) - 2) rvp->row = vt_row(0) - 2;
  if (pp == F4) echo_mess(S_err4);
  vt_move(rvp->row, 0);
  return rvp->row;
}

static int read_line(int c, u_char buff[], char prompt[], RV *rvp, int *row0p, PN *pnp, FLAG *fp)
{
  static int plen;
  static int n_buff, n_col;
  static struct {
    u_int nl	: 1;
    u_int bs	: 1;
    u_int esc	: 1;
    u_int md	: 2;
    u_int us	: 2;
  } ff;
  int i;

  if (rvp == F7) {
    ff.nl = ff.bs = ff.esc = ff.md = ff.us = 0;
    plen = n_buff = n_col = 0;
    return 0;
  }
  plen = strlen(prompt);
  if (!plen && n_col == vt_col(0) && (c == '\r' || c == '\n')) {
    *buff = '\0';
    return '\r'; /* new line was already added at (8), so skip next_line() */
  }
  if (n_buff >= N_pipe || c == '\n' || (n_col == vt_col(0) && c == '\r')) { 
    int j;
    u_char *p, *p0, *cp;
    ff.nl = 0;
    p0 = p = NULL;
    if (plen && strlen(buff) >= vt_col(0) + Shell.f.alen 
	&& strlen(buff) > vt_col(0) + Shell.f.alen) {
      u_char *p2;
      p0 = p = calloc(strlen(buff), 2);
      strcpy(p, buff);
      p2 = alloca(strlen(p) + 1);
      j = Shell.f.alen;
      do {
	strcpy(p2, p);
	cp = &p[vt_col(0) + j];
	*cp = '\0';
	/* set string of prompt to virtual line */
	rvp->pp->cs = strdup(p);
	rvp->pp->f.str = 1;
	rvp->pp->f.len = strlen(rvp->pp->cs);
	next_line(rvp, pnp, '\\', 0);
	/* '\\' : line continuation ==> alloc.c (2) */
	if (!fp->bash && cp[-1] == '\r') ff.nl = 1;
	strcpy(p, p2);
	strcpy(p, &p[vt_col(0) + j]);
        if (!*p) break;
	if (*p == '\r') ff.nl = 1;
	j = 0;
      } while (strlen(p) >= vt_col(0));
    }
    if (c != '\r') c = '\n';
    if (plen) {
      if ((cp = strchr(buff, '\r'))) *cp = '\0';
      vt_move(*row0p, 0);
      cp = buff;
      j = Shell.f.alen;
      do {
	for (i = 0; *cp && i < vt_col(0) + j; i++, cp++) {
	  if (is_euckana_1_byte(*cp)) i--;
	  vt_putchar(*cp);
	}
	vt_flush();
	if (i == vt_col(0) + j) {
	  if ((i = *row0p < vt_row(0) - 2)) (*row0p)++;
	  else {
	    if (vt_mode('H')) vt_move(vt_row(0) - 1, 0);
	    vt_sf();
	  }
	  vt_move(*row0p, 0);
	  j = 0;
	}
	vt_eeol();
      } while (*cp);
      if (p) strcpy(buff, p);
    }
    if (*buff) {
      if ((cp = strchr(buff, '\r'))) *cp = '\0';
      if (*buff) {
	rvp->pp->cs = strdup(buff); 
	rvp->pp->f.str = 1;
	rvp->pp->f.len = strlen(rvp->pp->cs);
	rvp->pp->f.line = rvp->pp->prev->f.line + rvp->pp->prev->f.nl;
	rvp->pp->f.nl = 1;
      } else c = '\n';
    } else if (n_col == vt_col(0))
      c = '\r'; /* skip alloc_sh() at next_line() */
    if (plen) {
      if ((p = strchr(prompt, ' ')))
	p = strchr(&p[1], ' ');
    } else p = NULL;
    if (p) {
      /* SHELL-prompt */
      p[1] = '\0';
      if ((j = *row0p) < vt_row(0) - 2) {
	j = ++(*row0p);
	if (plen > vt_col(0) + Shell.f.alen) {
	  j++;
	  if (plen > vt_col(0) + Shell.f.alen + vt_col(0)) j++;
	}
      }
      vt_move(j, 0);
      vt_flush();
      *prompt = '\0';
    } else if (*row0p < vt_row(0) - 2) (*row0p)++;
    *rvp->str = *buff = '\0';
    ff.us = ff.md = ff.esc = n_buff = n_col = 0;
    if (ff.nl) vt_putchar('\n');
    if (p0) free(p0);
  } else {
    if (!plen && n_col >= vt_col(0)) 
      ff.us = ff.md = ff.esc = n_buff = 0;
    if (!n_buff) n_col = 0;
    buff[n_buff++] = c;
    buff[n_buff] = '\0';
    do {
      if (n_col < vt_col(0) && !ff.us && c == '_') {
	if (++n_col == vt_col(0)) continue;
	else {
	  ff.us = 1;
	  vt_putchar('_');
	  vt_flush();
	}
      } else if (ff.us) {
	if (ff.us == 1)
	  if (c == K_h) {
	    vt_putchar(c);
	    vt_flush();
	    ff.us = 0;
	    if ((n_buff -= 2) < 0) n_buff = 0;
	    n_col--;
	    buff[n_buff] = '\0';
	  } else {
	    ff.us = 0;
	    n_col++;
	    continue;
	  }
	else if (ff.us == 2)
	  if (c == K_h) ff.us = 3;
	  else {
	    ff.us = 0;
	    n_col += 3;
	    vt_putchar('_');
	    vt_putchar('_');
	    vt_putchar(c);
	  }
	else if (ff.us == 3) {
	  if (c == K_h) {
	    ff.us = 0;
	    if ((n_buff -= 4) < 0) n_buff = 0;
	    buff[n_buff] = '\0';
	  } else {
	    ff.us = 0;
	    n_col += 4;
	    vt_putchar('_');
	    vt_putchar('_');
	    vt_putchar(K_h);
	    vt_putchar(c);
	  }
	}	  
      } else if (c == K_h)
	if (ff.bs) { /* ^H ^H */
	  ff.bs = 0;
	  vt_left();
	  vt_putchar(' ');
	  vt_flush();
	  vt_left();
	  if ((n_buff -= 3) < 0) n_buff = 0;
	  buff[n_buff] = '\0';
	  ff.md = 0;
	} else ff.md++; /* bold */
      else if (ff.md) {
	if (ff.md == 1)
	  if (c == ' ') ff.bs = 1;
	  else {
	    ff.md = 0;
	    if ((n_buff -= 2) < 0) n_buff = 0;
	    buff[n_buff] = '\0';
	  }
	else if (ff.md == 2) ff.md = 3;
	else if (ff.md == 3) {
	  ff.md = 0;
	  if ((n_buff -= 4) < 0) n_buff = 0;
	  buff[n_buff] = '\0';
	}
      } else if (c == K_ec && !*prompt) {
	ff.esc = 1; /* escape sequence */
	if (!plen) buff[--n_buff] = '\0';
      } else if (ff.esc) {
	ff.esc = (c < 'A' || c > 'Z') && (c < 'a' || c > 'z');
	if (!plen) buff[--n_buff] = '\0';
      } else {
	if (!is_euckana_1_byte(c)) n_col++;
	continue;
      }
      if (!plen) return c;
    } while(0);
    if (c == '\t' && (i = n_col % 8)) n_col += 8 - i;
    if (!plen && n_col >= vt_col(0)) {
      if (*row0p < vt_row(0) - 2) (*row0p)++;
      vt_putchar(c);
      rvp->pp->cs = strdup(buff);
      rvp->pp->f.str = 1;
      rvp->pp->f.len = strlen(rvp->pp->cs);
      rvp->pp->f.line = rvp->pp->prev->f.line + rvp->pp->prev->f.nl;
      rvp->pp->f.nl = 0;
      n_buff = ff.us = ff.md = 0;
      /* (8) */
      c = '\n'; /* add new line at next_line() */
    } else
      do {
	if (pnp->line == 1) return c;
	if (plen) {
	  if (plen >= vt_col(0) / 4 * 3 + Shell.f.alen) {
	    vt_move(*row0p, Shell.f.blen);
	    i = Shell.f.blen + plen - vt_col(0) + vt_col(0) / 4 + 4;
	    if (count_k(prompt, i)) sprintf(rvp->str, " %s", &prompt[i + 1]);
	    else strcpy(rvp->str, &prompt[i]);
	    vt_printf("...%s", rvp->str);
	    vt_flush();
	  } else {
	    if (*rvp->str) *rvp->str = '\0';
	    vt_move(*row0p, 0);
	    vt_printf("%s", prompt);
	  }
	  vt_flush();
	} else vt_putchar(c);
      } while (0);
  }
  return c;
}

void print_prompt(RV *rvp, int row0, char prompt[], char buff[])
{
  int i;

  if (vt_mode('D')) {
    print_line(rvp->ps, rvp->pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
    vt_move(rvp->row, rvp->col);
    if (!strlen(prompt)) {
      if (!rvp->pe->next && rvp->row == row0) {
	vt_move(row0, 0);
	vt_printf("%s", buff);
	vt_flush();
      }
      return;
    }
    if (rvp->pe && !rvp->pe->next) {
      vt_sc();
      vt_move(row0, 0);
      if (*rvp->str) {
	i = prompt[Shell.f.clen];
	prompt[Shell.f.clen] = '\0';
	vt_printf("%s...%s", prompt, rvp->str);
	prompt[Shell.f.clen] = i;
      } else vt_printf("%s", prompt);
      vt_eeol();
      vt_flush();
      vt_rc();
    } 
    vt_flush();
  }
}
  
static int set_pos(PP *pp, int *posp)
{
  int m;

  if (!pp->prev)
    if (*posp != M_head) m = *posp = M_head;
    else m = M_cmd;
  else if (*posp == M_head) m = *posp = (!pp->next) ? M_tail: T_not;
  else if (!pp->next)
    if (*posp != M_tail) m = *posp = M_tail;
    else m = M_cmd;
  else if (*posp == M_tail) m = *posp = (!pp->prev) ? M_head : T_not;
  else m = M_cmd;
  return m;
}

static void output_shell(FILE **, FILE **, char[], int[], int, int);
static void shell_hup(int dummy)
{
  signal(SIGALRM, SIG_IGN);
  output_shell(NULL, NULL, NULL, NULL, 0, -1);
}

static void shell_pipe(int dummy)
{
  signal(SIGPIPE, shell_pipe);
}

static BASE *print_shell(BASE *bp, RV *rvp, PN *pnp, PP *p0, int *rowp, int pid)
{
  char str[256];
  int i;
  PP *pp;
  BASE *bp0;

  bp->pp = p0;
  pnp->nrow = alloc_winch(bp, 1);
  bp0 = calloc(sizeof(BASE), 1);
  for (i = 0, pp = bp->pp; pp->next; i++, pp = pp->next) {
    if (pp->cs == F3) break;
    memcpy(str, pp->cs, pp->f.len);
    str[pp->f.len] = '\0';
    bp0->pp = alloc_sh(str, (!i) ? -pid : 1, (pp->f.nl) ? '\n' : 0);
  }
  free_bp(bp);
  free(bp);
  bp = bp0;
  alloc_sh(F3, 1, '\n');
  for (pp = bp->pp; pp->prev; pp = pp->prev) ;
  pnp->top = pnp->nrow - (vt_row(0) - 1);
  rvp->pp = bp->pp;
  init_row(&rvp->pp, pnp, 0, ((i = pnp->nrow - (vt_row(0) - 1)) > 0) ? i : 0);
  rvp->ps = rvp->pp;
  for (rvp->row = 0, pp = rvp->pp; pp->cs != F3; pp = pp->next, rvp->row++) ;
  rvp->pe = rvp->pp = pp;
  *rowp = pnp->row = rvp->row; 
  return bp;
}

static void read_shell(void *p)
{
  static char comm[N_pipe+1], str[N_pipe+1];
  static char *name, *prompt, *arg;
  static u_char *buff, *prev;
  static int *pv, *row0p, *lenp, *ip;
  static RV *rvp;
  static PN *pnp;
  static int pos;
  static PP **p0p;
  static int row_mark, tell;
  static int i, m;
  static char *cp;
  static SHELL *sp;
  static PP *pp;
  
  switch(Shell.f.init) {
  case 2:
    sp = p;
    name = sp->r.name;
    prompt = sp->r.prompt;
    arg = sp->r.arg;
    buff = sp->r.buff;
    prev = sp->r.prev;
    rvp = sp->r.rvp;
    pnp = sp->r.pnp;
    p0p = sp->r.p0p;
    row0p = sp->r.row0p;
    lenp = sp->r.lenp;
    ip = sp->r.ip;
    break;
  case 1:
    pv = p;
    pos = M_tail;
    break;
  case 0:
    m = read_comm(pv[0], comm);
    switch(m) {
    case T_in:
      write(pv[3], &m, sizeof(int));
      write(pv[3], ip, sizeof(int));
      return;
    case T_out:
      write(pv[3], &m, sizeof(int));
      *ip = 1;
      return;
    case T_off:
      vt_mode(-'d');
      m = M_cmd;
      break;
    case T_disp:
      cp = read_string(comm, str);
      i = atoi(str);
      read_string(cp, str);
      vt_setwin(i, atoi(str));
      disp_shell(rvp, name, prev, arg, *row0p, prompt, *lenp, buff);
      m = M_cmd;
      break;
    case M_mark:
      row_mark = rvp->pp->f.line;
      print_line(F2, NULL, 0, NULL);
      m = M_cmd;
      break;
    case M_jump:
      tell = row_mark - 1;
      row_mark = rvp->pp->f.line;
    case N_num:
      if (m == N_num) {
	read_string(comm, str);
	tell = atoi(str);
      }
      if (tell > 1) {
	for (pp = *p0p, i = 1; pp->next->next; pp = pp->next, i++)
	  if (pp->f.line == tell) break;
	tell = i;
      }
      if (pnp->nrow > vt_row(0)) {
	tell++;
	rvp->pp = *p0p;
	rvp->row = init_row(&rvp->pp, pnp, tell
			    , ((i = pnp->nrow - (vt_row(0) - 1)) > 0) ? i : 0);
	if ((rvp->pe = rvp->ps = rvp->pp))
	  for (i = 0; i < vt_row(0) - 2 && rvp->pe->next; i++)
	    rvp->pe = rvp->pe->next;
	for (i = 0; i < pnp->row && rvp->pp->next; i++) 
	  rvp->pp = rvp->pp->next;
	while (pnp->row > i++) rvp->row--;
      } else {
	shell_page(rvp, pnp, M_head, prompt, buff);
	for (i = tell; i; i--)
	  shell_row(rvp, pnp, M_dn, buff);
      }
      disp_shell(rvp, name, prev, arg, *row0p, prompt, *lenp, buff);
      m = set_pos(rvp->pp, &pos);
      break;
    case T_tag:
    case T_tag2:
      cp = read_string(comm, str);
      i = atoi(str);
      cp = read_string(cp, str);
      vt_setwin(i, atoi(str));
      cp = read_string(cp, prev);
      cp = read_string(cp, str);
      vt_charset(atoi(str));
      cp = read_string(cp, str);
      if (atoi(str)) {
	vt_mode('h');
	vt_cs(vt_row(0) - 1, 0);
      } else {
	vt_mode(-'h');
	vt_cs(vt_row(0) - 2, 0);
      }
      cp = read_string(cp, str);
      if (atoi(str)) output_shell(NULL, NULL, NULL, NULL, 0, 0);
    case M_clear:
      if (m != T_tag2) 
	disp_shell(rvp, name, prev, arg, *row0p, prompt, *lenp, buff);
      if (m == T_tag && rvp->pp->next) m = M_tail;
      else {
	m = M_cmd;
	break;
      }
    default:
      i = 0;
      do {
	switch(m) {
	case M_dn: case M_up: 
	case M_kdn: case M_kup: 
	case M_prev: case M_next: 
	case M_kprev: case M_knext: 
	  shell_row(rvp, pnp, m, buff);
	  break;
	case M_kback: case M_head:
	case M_kforw: case M_tail:
	  shell_page(rvp, pnp, m, prompt, buff);
	  break;
	default:
	  m = M_cmd;
	  continue;
	}
	m = set_pos(rvp->pp, &pos);
	print_prompt(rvp, *row0p, prompt, buff);
	break;
      } while (0);
      break;
    }
    i = 0;
    if (rvp->pp->cs != F3)
      for (pp = rvp->pp, *comm = '\0'; ; pp = pp->next) {
	if (pp->cs == F3) break;
	i += strlen(pp->cs) + 1;
	strcat(comm, pp->cs);
	if (pp->split) 	/* (1) <== alloc.c (2) */
	  ;
	else break;
      }
    write(pv[3], &m, sizeof(int));
    write(pv[3], &i, sizeof(int));
    if (i) write(pv[3], comm, i);
    m = strlen(prompt);
    write(pv[3], &m, sizeof(int));
    write(pv[3], row0p, sizeof(int));
    m = (m != T_off && rvp->pe && !rvp->pe->next) ? T_prompt : 0;
    write(pv[3], &m, sizeof(int));
    break;
  }
}
  
static int add_line(int row, int c, u_char buff[], char prompt[], RV *rvp, int *row0p, PN *pnp, int pv[], FLAG *fp)
{
  int i;

  if ((i = read_line(c, buff, prompt, rvp, row0p, pnp, fp)) == '\n' || i < 0) {
    row = next_line(rvp, pnp, (i < 0) ? 0 : '\n', 1);
    if (i < 0) vt_putchar (-i);
    fp->i_err = fp->i_errp = fp->n_err = 0;
    *prompt = pnp->line = 0;
  }
  return row;
}

static void output_shell(FILE **ifpp0, FILE **ofpp0, char prev0[], int pv0[], int pid0, int f_shell)
{
  static char prompt[N_pipe+1], name[N_line], arg[10], usilic[10];
  static PP *p0;
  static FILE *ifp, *ofp, **ifpp, **ofpp;
  static BASE *bp;
  static RV rv;
  static PN pn;
  static int sfd, pid, len, row0, *pv, inter;
  static struct sigaction alrm = {
#ifdef __CYGWIN32__
    (void(*)(int))read_shell,
    0,
    SA_RESTART,
#else
    {(__sighandler_t)read_shell,},
    {{0,},},
    SA_RESTART,
    NULL,
#endif
  };
  static char *errp[3], nn[4] = {0,};
  int c, i, j, c1, row, col, f_cr, f_bin;
  u_char prev[N_line], buff[N_pipe+1], str[N_pipe+1];
  char *p, *p2, err[30];
  SHELL s;
  CHILD *chp;
  FLAG f;

  switch(f_shell) {
  case 0:
    bp = print_shell(bp, &rv, &pn, p0, &row0, pid);
    p0 = bp->pp; /* s_S(S_lb2) */
    bp->pp = bp->pp->next;
    return;
  case 1:
  case 2:
    if ((j = (f_shell == 1) ? M_shell : T_shell2) == T_shell2) vt_mode(-'d');
    init_ALLOC();
    Shell.f.init = 1;
    read_shell(pv0);
    if (!pv) {
      Shell.f.init = 2;
      s.r.name = name;
      s.r.prompt = prompt;
      s.r.arg = arg;
      s.r.buff = buff;
      s.r.prev = prev;
      s.r.rvp = &rv;
      s.r.pnp = &pn;
      s.r.p0p = &p0;
      s.r.row0p = &row0;
      s.r.lenp = &len;
      s.r.ip = &inter;
      read_shell(&s);
    }
    Shell.f.init = 0;
    inter = 1; /* ==> (11) */
    strcpy(usilic, getenv("USILIC"));
    errp[0] = s_S(S_mark);
    errp[1] = usilic;
    errp[2] = ":";
    ifpp = ifpp0;
    ofpp = ofpp0;
    ifp = *ifpp;
    ofp = *ofpp;
    chp = get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL);
    f.bash = !strcmp(chp->argv[0], "bash");
    signal(SIGCHLD, SIG_DFL);
    while (fgetc(ifp) == EOF) ; /* (12) */
    if (!fork()) {
      if (f.bash)
	fprintf(ofp, "export PS1=\047%s%s:\\w\\$ %s\047; set -o ignoreeof; export COLUMNS=%d; unset HISTFILE; export PAGER=\047%s -p %d\047\n#\n"
		, s_S(S_mark), usilic, s_S(S_mark), vt_col(0), s_S(S_usilic), pid0);
      else
	fprintf(ofp, "set prompt = \042%s%s:%%~%%# %s\042; set ignoreeof; setenv PAGER \047%s -p %d\047\n"
		, s_S(S_mark), usilic, s_S(S_mark), s_S(S_usilic), pid0);
      fflush(ofp);
      exit(0);
    }
#if 0
    while ((c = fgetc(ifp)) != '\r') ;
#else
    while ((c = fgetc(ifp)) != '#') ;
#endif
    wait(0);
    strcpy(prev, prev0);
    sfd = fileno(fopen(s_S(S_log), "a"));
    pv = pv0;
    pid = pid0;
    pn.row = pn.top =0;
    pn.nrow = 2;
    sprintf(str, "%s", s_S(S_lb2));
    bp = calloc(sizeof(BASE), 1);
    /* virtual line contains the string "SHELL  version ..." */
    p0 = bp->pp = alloc_sh(str, -pid, '\n'); 
    /* first SHELL-prompt */
    alloc_sh(F3, 1, '\n');
    *rv.str = '\0';
    rv.row = rv.col = 0;
    strcpy(name, s_P(P_mode)[M_shell]);
    sprintf(arg, "%c(%s)", K_z, chp->argv[0]);
    len = disp_window(bp->pp, bp->pp, j, name, prev, arg, &i);
    rv.ps = bp->pp;
    /* set current line to first SHELL-prompt */
    bp->pp = bp->pp->next;
    rv.pp = rv.pe = bp->pp;
    print_line(rv.ps, rv.pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
    vt_move(++rv.row, rv.col);
    row = row0 = rv.row;
    for (i = 1; i < NSIG; i++) 
      switch(i) {
      case SIGALRM:
	sigaction(i, &alrm, NULL);
	break;
      case SIGHUP:
      case SIGSEGV:
	signal(i, shell_hup);
	break;
      case SIGPIPE:
	signal(i, shell_pipe);
	break;
      default:
	signal(i, SIG_IGN);
	break;
      }
    *buff = *prompt = '\0';
    f_cr = f_bin = 0;
    read_line(0, NULL, NULL, F7, NULL, NULL, NULL);
    pn.line = -1; 
    for (c1 = f.i_err = f.i_errp = f.n_err = 0; ; ) {
      if (c1) {
	c = c1;
	c1 = 0;
      } else if ((c = fgetc(ifp)) == EOF) break;
      else if (c == K_g) {
#ifdef	CHECK_SFD
	write(sfd, &c, 1);
#endif
	c1 = 0;
	continue; /* <== (3) */
      }
      if (c == ' ' && *prompt && ((c1 = fgetc(ifp)) == '\r' || c1 == K_h)) { 
	/* " ^M" or " ^H" is the mark for length of winsize.ws_col. (tcsh) */
#ifdef	CHECK_SFD
	write(sfd, &c1, 1);
#endif
	c1 = 0;
	continue;
      }
      if (!*prompt) {
	if (!c && !f_bin) f_bin = 1;
        if (f_bin && (c >= 0x80 || (c < ' ' && c != '\n'))) c = ' ';
	if (!f_bin) {
	  switch(f.bs) {
	  case 0:
	    if (c == '\047') f.bs++;
	    break;
	  case 1:
	    if (c == '?') f.bs++;
	    else f.bs = 0;
	    break;
	  case 2:
	    if (c == ' ') f.bs++;
	    else f.bs = 0;
	    break;
	  case 3:
	    if (c == ' ' || c == K_h) continue; /* <== (10) */
	    else f.bs = 0;
	    break;
	  }
	}
      }
      if (vt_mode('D') && rv.pp->next) {
	/* go to the line of SHELL-prompt from current line */
	print_line(rv.ps, rv.pp, -strlen(&s_P(P_mode)[M_shell][1]), NULL);
	if (shell_page(&rv, &pn, M_tail, prompt, buff) >= 0) {
	  vt_move(row0, 0);
	  if (*rv.str) {
	    i = prompt[Shell.f.clen];
	    prompt[Shell.f.clen] = '\0';
	    vt_printf("%s...%s", prompt, rv.str);
	    prompt[Shell.f.clen] = i;
	  } else vt_printf("%s", prompt);
	  vt_eeol();
	} else vt_move(rv.row, strlen(buff));
      }
      if (c != '\r') {
	if (!*prompt) {
	  /* detect the string PS1 environment variable */
	  if (c == errp[f.i_errp][f.i_err]) {
	    err[f.n_err + f.i_err++] = c;
	    err[f.n_err + f.i_err] = '\0';
	    if (c != ':') continue;
	    /* matched to PS1 */
	    sprintf(prompt, "%s%s%s%s"
		    , vt_geta(A_md), errp[1], vt_geta(A_me), errp[2]);
	    c = errp[0][0];
	    p = &prompt[strlen(prompt)];
	    for (c1 = 0; (j = fgetc(ifp)) != c; ) {
#ifdef	CHECK_SFD
	      write(sfd, &j, 1);
#endif
	      if (j == '\r') { 
		if (c1 == ' ') {
		  p--;
		  c1 = 0;
		  continue;
		} else *p++ = c1;
	      }
	      if (j == '\\') { /* code above 0x80 (tcsh) */
		fread(nn, 1, 3, ifp);
		j = (u_char)strtol(nn, NULL, 8);
	      }
	      c1 = j;
	      if (!j) j = ' ';
	      *p++ = j;
	    }
	    *p = '\0';
	    c1 = 0;
	    for (i = 1; (j = errp[0][i]) && (c = fgetc(ifp)) == j; i++) {
#ifdef	CHECK_SFD
	      write(sfd, &c, 1);
#endif
	      /* read current directory name */
	      ;
	    }
	    if (*buff) {
	      /* 
	       * force to start a new line when there is any string before 
	       * SHELL-prompt (`ftp Enter ^D' etc.), with the result that 
	       * `set Enter' whose output contains PS1 will make 
	       * a extra new line.
	       */
	      kill(getppid(), SIGALRM);
	      i = 0;
	      write(pv[5], &i, sizeof(int)); /* (4) */
	      read(pv[6], &i, sizeof(int));
	      f_cr = 1;
	    }
	    p = malloc(strlen(prompt) + 1);
	    p2 = malloc(strlen(prompt) * 2);
	    strcpy(p, prompt);
	    strcpy(p2, prompt);
#ifndef	CHECK_SFD
 	    write(sfd, p2, strlen(p2));
#endif
	    free(p2);
	    memset(prompt, 0, strlen(p));
	    row0 = rv.row;
	    for (i = 0; (c = p[i]); i++) {
	      prompt[i] = c;
	      read_line(c, buff, prompt, &rv, &row0, &pn, &f);
	      vt_eeol();
	    }
	    strcpy(prompt, p);
	    free(p);
	    pn.line = 1;
	    vt_flush();
	    if (f_bin) f_bin = 0;
	    if (f_cr) f_cr = 0;
	    else {
	      /*
	       * inform parent process of SHELL-prompt contains 
	       * current directory name.
	       */
	      kill(getppid(), SIGALRM);
	      i = strlen(prompt) + 1;
	      write(pv[5], &i, sizeof(int));
	      write(pv[5], prompt, i);
	      write(pv[5], &row0, sizeof(int));
	      i = strlen(rv.str);
	      col = (i) ? Shell.f.blen + 3 + i : strlen(prompt) - Shell.f.alen;
	      write(pv[5], &col, sizeof(int)); /* (6) */
	      i = vt_mode('D');
	      write(pv[5], &i, sizeof(int));
	      read(pv[6], &i, sizeof(int));
	    }
	    f.i_errp = f.n_err = 0;
	    f.i_err = strlen(buff);
	    inter = 0;
	    continue;
	  } else if (f.i_err) {
	    if (!errp[f.i_errp][f.i_err]) {
	      /* next checking */
	      f.n_err += f.i_err;
	      f.i_err = 0;
	      f.i_errp++;
	      c1 = c;
	      continue;
	    } else {
	      /* not SHELL-prompt */
	      c1 = c; 
	      for (i = 0; (c = err[i]); i++)
		row = add_line(row, c, buff, prompt, &rv, &row0, &pn, pv, &f);
#ifndef	CHECK_SFD
 	      write(sfd, err, strlen(err));
#endif
	      f.i_err = f.i_errp = f.n_err = *prompt = 0;
	      continue;
	    }
	  }
	}
      }
      if (pn.line < 0) continue; /* garbage */
#ifndef CHECK_SFD
      if (c != '\r') write(sfd, &c, 1);
#endif
      row = add_line(row, c, buff, prompt, &rv, &row0, &pn, pv, &f);
    }
  case -1:
    bp->pp = p0;
    free_bp(bp);
    free(bp);
    close(sfd);
    fclose(ofp);
    fclose(ifp);
    *ifpp = *ofpp = NULL;
    if (!kill(pid, 0)) kill(pid, SIGHUP);
    exit_ALLOC(&s_P(P_mode)[M_shell][1]);
    exit(0);
    break;
  default:
    break;
  }
}

int from_main(char name[], char str[], char *mess, FILE **pinp, int *ppidp)/*usilic*/
{
  FILE *fp;
  int len, sfd;
  u_char m;
  struct stat st; 
  struct sockaddr_un cli_addr;
  static char sname[N_line];
  
  if (!mess) {
    if (*sname && !access(sname, F_OK)) unlink(sname);
    return 0;
  }
  stat(mess, &st);
  if (S_ISSOCK(st.st_mode)) {
    if ((sfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return 0;
    cli_addr.sun_family = AF_UNIX;
    strcpy(cli_addr.sun_path, mess);
    if (connect(sfd, (struct sockaddr *)&cli_addr, sizeof(cli_addr)) < 0)
      return 0;
    fp = fdopen(sfd, "r");
  } else fp = fopen(mess, "r");
  fread(&m, 1, 1, fp);
  switch(m) {
  case T_pipe2:
    fread(ppidp, 1, 4, fp);
    if (*pinp && *pinp != F1) /* ^O^M or MAN */
      alloc_pipe(-1, pinp, NULL, NULL, 0);
    *pinp = fp;
    strcpy(sname, mess);
    break;
  default:
    fread(&len, 1, 4, fp);
    fread(str, 1, len, fp);
    if (m != M_du) {
      fread(&len, 1, 4, fp);
      fread(name, 1, len, fp);
    }
    fclose(fp);
    unlink(mess);
    break;
  }
  return m;
}

int wait_comm(int pfd, int *posp, int *rowp, int col0p, u_char *buff, u_char pp_str[])/*usilic*/
{
  int m, row, len, f_prompt;

  read(pfd, &m, sizeof(int));
  switch(m) {
  case T_in:
    if (!vt_mode('D')) vt_mode('d');
    read(pfd, &f_prompt, sizeof(int));
    return f_prompt;
    break;
  case T_out:
    return 0;
    break;
  default:
    if (m != M_cmd && m != T_off) *posp = m;
    break;
  }
  read(pfd, &len, sizeof(int));
  if (len) read(pfd, pp_str, len);
  read(pfd, &len, sizeof(int)); /* strlen(prompt) */
  read(pfd, &row, sizeof(int));
  read(pfd, &m, sizeof(int));
  if (rowp) *rowp = row;
  if (len && m == T_prompt && buff) {
    if (*posp != M_tail) vt_sc();
    vt_move(*rowp, col0p);
    print_buff(buff, 0);
    vt_flush();
    if (*posp != M_tail) vt_rc();
  }
  return len;
}

static int make_shell(FILE **ifpp, FILE **ofpp, int pv[], u_char prev[], int pid, char mess[], int f_comm)
{
  int i, spid;
  static struct sigaction alrm = {
#ifdef __CYGWIN32__
    (void(*)(int))write_shell,
    0,
    SA_RESTART,
#else
    {(__sighandler_t)write_shell,},
    {{0,},},
    SA_RESTART,
    NULL,
#endif
  };
  sigaction(SIGALRM, &alrm, NULL);
  for (i = 0; i < 8; i++) if (pv[i]) close(pv[i]);
  pipe(pv);
  pipe(&pv[2]);
  pipe(&pv[4]);
  pipe(&pv[6]);
  input_shell(0, pv, mess, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
  if (!(spid = fork())) {
    usleep(100000); /* following is preceded by pause() of parent process */
    output_shell(ifpp, ofpp, prev, pv, pid, (f_comm) ? 2 : 1);
  } else pause();
  start_shell(0, spid, NULL, NULL, NULL, NULL);
  return spid;
}

int start_shell(int shell_pid, int disp_pid, FILE **ofpp, FILE **ifpp, char *mess, char pty[])/*usilic*/
{
  static FILE *ifp, *ofp;
  static int pid, spid;
  static char *messp;
  int i, tfd, fd;
  
  if (shell_pid < 0) return pid;
  if (disp_pid) {
    spid = disp_pid;
    return 0;
  }
  if (mess) {
    messp = mess;
    if (!ofpp && !ifpp) tfd = 0;
#ifdef __CYGWIN32__
    else if ((tfd = open ("/dev/ptmx", O_RDWR)) < 0) return 0;
#else
    else if ((tfd = open_pty(pty, 1)) < 0) return 0;
#endif
    unlink(s_S(S_mess));
    if (!(pid = fork())) {
      char str[256];
      CHILD *chp = get_child(NULL, s_P(P_mode)[M_shell], NULL, NULL, NULL);
      pty[5] = 't';
#ifdef __CYGWIN32__
      grantpt(tfd);
      unlockpt(tfd);
      fd = open(ptsname(tfd), O_RDWR|O_NOCTTY);   
      setsid();
#else
      fd = open(pty, O_RDWR);
      setsid();
      ioctl(fd, TIOCSCTTY, 0);
#endif
      dup2(fd, 0);
      dup2(fd, 1);
      dup2(fd, 2);
      close(fd);
      strcpy(str, s_S(S_mess));
      strcat(str, ".tmp");
      ofp = fopen(str, "w");
      fprintf(ofp, "%d", (int)getpid());
      fclose(ofp);
      rename(str, s_S(S_mess));
      for (i = 0; i <= 31; i++) signal(i, SIG_DFL);
      if (!strcmp(chp->argv[0], "bash")) 
	/*
	 * bash inserts string "^H^[^A..." in order to redisplay 
	 * prompt whose length is equal to COLUMNS environment variable.
	 * Following setting avoids inserting above string.
	 * (the value will be reset in output_shell()) 
	 */
	setenv("COLUMNS", "1024", 1);
      execvp(*chp->argv, chp->argv);
      exit(0);
    }
    while (access(s_S(S_mess), F_OK) < 0) usleep(10000);
    ifp = fopen(s_S(S_mess), "r");
    fscanf(ifp, "%d", &pid);
    fclose(ifp);
    unlink(s_S(S_mess));
    strcpy(mess, s_S(S_mess));
    sprintf(&strstr(mess, "/s.")[3], "%d.%d", (int)getuid(), pid);
    if (!ofpp && !ifpp) return pid;
    *ofpp = ofp = fdopen(tfd, "w");
    *ifpp = ifp = fdopen(tfd, "r");
  } else {
    if (spid && !kill(spid, 0)) {
      kill(spid, SIGHUP);
      while (!kill(spid, 0)) usleep(10000);
    }
    if (ofp) {
      fclose(ofp);
      ofp = NULL;
    }
    if (ifp) {
      fclose(ifp);
      ifp = NULL;
    }
    if (!access(messp, F_OK)) unlink(messp);
    input_shell(0, NULL, NULL, NULL, NULL, NULL, 
		NULL, NULL, NULL, NULL, NULL, -1);
    from_main(NULL, NULL, NULL, NULL, NULL); /* unlink socket */
  }
  return pid;
}

int do_shell(u_char prev[], char str[], FILE **pinp, u_char *w, int *ppidp, LIST *prog, LIST **listp, STR **cmdp, STR **histp, int f_comm)/*usilic*/
{
  int m, f_shell, f_init;
  char dir[N_line];
  static int pid, spid, pv[8];
  static FILE *ifp, *ofp;
  static char mess[N_line], pty[256];

  if (!Shell.f.alen) {
    Shell.f.alen = strlen(vt_geta(A_md)) + strlen(vt_geta(A_me));
    Shell.f.blen = strlen(getenv("USILIC")) + 1; 
    Shell.f.clen = Shell.f.alen + Shell.f.blen;
  }
  f_init = !(pid && !kill(pid, 0));
  if (!ofp || (spid && kill(spid, 0))) {
    if ((f_shell = (f_comm && f_comm < 3) ? f_comm : 4) != 4) vt_mode(-'d');
    else f_comm = 0;
    if (ofp) {
      fclose(ofp);
      fclose(ifp);
      ofp = NULL;
      ifp = NULL;
    }
    if (!(pid = start_shell(0, 0, &ofp, &ifp, mess, pty))) {
      save_error("%s: %s.\n", s_S(S_usilic), s_S(S_err5));
      save_intr(S_err5);
      return -M_bl;
    }
    if ((spid = make_shell(&ifp, &ofp, pv, prev, pid, mess, f_comm)) < 0) {
      kill(pid, SIGHUP);
      kill(getpid(), SIGHUP);
      return 0;
    }
    vt_cursor(spid);
    is_inter(pv, pid, spid);
  } else f_shell = (f_comm) ? f_comm : 1;
  getcwd(dir, N_line);
  print_intr(F8, NULL); /* erase "Exit" */
  m = input_shell(spid, w, prev, str, ofp, pinp, prog, 
		  listp, cmdp, histp, ppidp, f_shell);
  chdir(dir);
  return m;
}

int is_inter(int *pv0, int pid0, int spid0)/*usilic*/
{
  int f_inter = 0;
  static int pid, spid, i_inter, *pv;

  if (pv0 == F12) {
    if (spid && !kill(spid, 0)) {
      kill(pid, SIGHUP);
      kill(spid, SIGHUP);
      while (!kill(spid, 0) || !kill(pid, 0)) usleep(10000);
    }
  } else if (!pv0) {
    if (spid && !kill(spid, 0)) {
      write_comm(pv[1], T_in, NULL, spid);
      f_inter = wait_comm(pv[2], NULL, NULL, 0, NULL, NULL);
      if (pid0 < 0 && f_inter) return ++i_inter == 1;
      else if (i_inter) i_inter = 0;
    }
  } else {
    pv = pv0;
    pid = pid0;
    spid = spid0;
  }
  return f_inter;
}

#include "usilic.h"
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>

static struct {
  u_char *S[S_nn];
} Str;

void r_S(int n_S)/*usilic*/
{
  if (n_S < S_nn && Str.S[n_S]) {
    free((char*)Str.S[n_S]);
    Str.S[n_S] = 0;
  }
}

char *s_S(int n_S)/*usilic*/
{
  return (n_S < S_nn) ? (char*)Str.S[n_S] : NULL;
}

char **s_P(u_int n_P)/*usilic*/
{
  static char *child[] = { /* built-in program */
    /* file manager */
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
  static char *usilicrc[] = { /* setting name defined in usilicrc */
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
    {" USILIC",	M_usilic},
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

    /* for ~/.usilic/history */
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

static void get_prog(char str[])
{ 
  int i;
  char *p;

  p = alloca(strlen(str) + 1);
  for (i = 0; str[i] && str[i] != ' '; i++) p[i] = str[i];
  p[i] = '\0';
  strcpy(str, p);
}

static void print_usilic(char *str, int f_usilic)
{
  char *cp, *p = str, tmp[256];
  
  strcpy(p, s_S(S_version));
  *p = 'V';
  sprintf(strchr(p, '\0'), "--\nStartup file : %s\n", toul(s_S(S_usilicrc), 0));
  if (f_usilic & 0x80)
    sprintf(strchr(p, '\0'), "History file : %s\n", toul(s_S(S_hist), 0));
  sprintf(strchr(p, '\0'), "Startup mode : %s\n"
	  , (f_usilic & (0x80 | 0x40)) 
	  ? tobo("file manager", 0) : tobo("pager", 0));
  sprintf(strchr(p, '\0'), "Command line : %s\n", s_S(S_option+7));
  strcat(p, "User name    : ");
  do {
    if ((cp = getenv("user"))) sprintf(strchr(p, '\0'), "%s", cp);
    else if ((cp = getenv("USER"))) sprintf(strchr(p, '\0'), "%s", cp);
    else {
      sprintf(strchr(p, '\0'), "---\n");
      continue;
    }
    if ((!(cp = getenv("hostname")) 
	&& !(cp = getenv("HOSTNAME"))
	&& !(cp = getenv("HOST")))
	|| !strlen(cp)) {
      int i, pv[2];
      pipe(pv);
      if (!fork()) {
	close(1);
	dup2(pv[1], 1);
	close(pv[1]);
	close(pv[0]);
	execlp("hostname", "hostname", NULL);
      }
      close(pv[1]);
      for (i = 0; read(pv[0], &tmp[i], 1) == 1 && i < 256; i++) ;
      if (i == 256) i--;
      tmp[i] = '\0';
      close(pv[0]);
      if ((cp = strchr(tmp, '\n'))) *cp = '\0';
      cp = (i) ? tmp : NULL;
    }
    if (cp) sprintf(strchr(p, '\0'), "@%s\n", cp);
    else strcat(p, "\n");
  } while (0);
  if (f_usilic & (0x80 | 0x40)) {
    sprintf(strchr(p, '\0')
	    , "%s log    : %s\n"
	    , tobo(&s_P(P_mode)[M_error][1], 0)
	    , toul(s_S(S_error), 0));
    sprintf(strchr(p, '\0')
	    , "%s log    : %s\n"
	    , tobo(&s_P(P_mode)[M_shell][1], 0)
	    , toul(s_S(S_log), 0));
  }
}

static char *get_cp(char str[])
{
  char *cp;

  if ((cp = strchr(str, '#'))
      || (cp = strchr(str, '\n'))) *cp = '\0';
  for (cp = str; *cp && (*cp == ' ' || *cp == '\t'); cp++) ;
  return (*cp) ? cp : NULL;
}

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
	  strcpy(str, cp);
	  save_error("conf_arc(): \042%s\042: %s.\n", str, s_S(S_errm));
	  save_stderr("conf_arc(): \042%s\042: %s.\n", str, s_S(S_errm));
	} else save_child(chp, cp, 1, NULL);
      }
    }
  return -1;
}

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
	  strcpy(str, cp);
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
      strcpy(str, p);
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

static int conf_history(FILE *fp, char str[])
{
  char *cp;
  int i;

  if ((cp = get_val(fp, str, &i)) == NULL) return i;
  save_history(F6, cp, 0);
  return 0;
}


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
  if (cp > str) strcpy(str, cp);
  return 0;
}

static void key_err(FILE *fp, char mark[], int f_err)
{
  fclose(fp);
  save_stderr("%src: %s: %s.\n", s_S(S_usilic), mark, s_S(f_err));
  exit_usilic();
  print_stderr(NULL);
  exit(1);
}

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
    "Edit `SHELL' of usilicrc",				/* S_errd */
    "~/.usilic/usilicrc ignored",				/* S_erre */
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
  static char usilic[] = "usilic";
  static char jman[] = "man"; 
  static char *groff[] = {
    "groff",
    "-mandoc",
    "-Tnippon",
  };
  char *com[] = {LS, DU, CP, MV, RM};
  int i, j, pv[2], erre = 0;
  int i_mess, i_home;
  char *cp;
  struct stat st;
  char home[256], tmp[256], mess[N_line];

  if (temp) {
    strcpy(temp, TMP);
    /*
     * following files will be used at "TMP" (defined in config.h) directory.
     *   d... notice of termination of `du'.
     *   e... log of ERROR after startup with directory argument.
     *   i... information about error message No..
     *   l... log of SHELL after startup with directory argument.
     *   s... output from `usilic' launched at SHELL-prompt.
     *   w... content of ^Jw and ^J^W.
     *   t... work file used instead of memory failed to be allocated.
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
  fprintf(stderr, "S_mess=%s\n", Str.S[S_mess]);
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
  if (!getenv("USILICCHARSET")) setenv("USILICCHARSET", CHARSET, 1);
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
      strcat(cp, "-Tascii");
    }
    close(pv[0]);
    pipe(pv);
    if (!fork()) {
      dup_error();
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
    strcpy(tmp, getenv("SHELL"));
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
	return -1; /* s_S(S_sh) isn't installed */
      }
    }
    sprintf(tmp, "%s  %s", &s_P(P_mode)[M_shell][1], version);
    *strchr(tmp, '\n') = '\0';
    Str.S[S_lb2] = strdup(tmp);	/* S_lb2 */
    for (i = j = 0; i < S_tty - S_ls; i++, j++) {
      cp = s_P(P_child)[j];
      /* set Str.S[S_ls]  ... */ 
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
      /* set Str.S[S_file]  ... */ 
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
  s_P((u_int)ARCHIVE);
  return 0;
}

void free_S(void)/*usilic*/
{
  int i;

  for (i = 0; i < S_nn; i++) r_S(i);
}

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
  static u_char inval_0[] = {/* pager */
    M_help, M_sel, M_fp, M_code, M_cd, M_m, M_bl2,
    M_fcut2, M_fcut, M_du, M_du2, M_locate, M_locate2, M_prog2, M_prog, 
    M_man2, M_man, M_lfile2, 
    M_lfile, M_ldir2, M_ldir, M_cmd, M_cp, M_cp2, M_mv, M_mv2, M_rm, 
    M_rm2, M_arc, M_arc2, M_dest, M_yes, M_no, M_shell, M_hup, 
    M_chdir2, M_chdir, M_w, M_w2, M_list, M_error, M_log, 
    M_ascii, M_kanji, M_exit, M_ls,
    0,
  };
  static u_char inval_2[] = { /* filemanager */
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

void set_key(int **key_mp, int **key_cp, int key_m0[], int key_m[], int key_c[])
{
  int i;

  *key_mp = key_m;
  *key_cp = key_c;
  *key_m = *key_m0 = -1;
  for (i = 0; i < M_nn; i++) key_c[i] = -1;
  /* built-in keys */
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
	      strcpy(&p[1], &p[3]);
	    }
	  while ((p = strstr(label[i], "\\c")))
	    if (p[2] >= 'A' && p[2] <= 'Z') {
	      sprintf(p, "^%c", p[2]);
	      strcpy(&p[2], &p[3]);
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

static void sub_clear(int dummy)
{
  Sub.abort = 1;
  longjmp(Sub.jb, 0);
}

static void sspc(u_char *cp, u_char *np)
{ /* convert space character for sscanf() */
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

void rspc(u_char *cp, u_char *np)/*usilic*/
{ /* convert space character for sscanf() */
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

static void print_list(LIST *lp, FILE *fp)
{
  if (lp) {
    print_list(lp->left, fp);
    fprintf(fp, "%s\n", lp->name);
    print_list(lp->right, fp);
  }
}

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

static void write_str(FILE *fp, STR *sp, LIST *lp, int f, int m)
{ /* save strings to ~/.usilic/history in STR-list order */
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

static void get_par(char *dir)
{ /* set parent directory name to dir[] */
  if (!strcmp(dir, "/")) return;
  for (dir = &strchr(dir, '\0')[-2]; *dir != '/'; dir--) ;
  dir[1] = '\0';
}

static void get_time(char str[])
{
  struct tm tt;
  time_t ll;

  time(&ll);
  memcpy(&tt, localtime(&ll), sizeof(struct tm));
  strcpy(str, asctime(&tt));
  *strchr(str, '\n') = '\0';
}

int find_prog(LIST *prog, u_char buff[])/*usilic*/
{ /* search for buff[] in the program-list of PROGRAM */
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

GREP *load_grep(GREP *grep)/*usilic*/
{
  GREP *gp;

  if (!grep) return grep;
  gp = grep->prev;
  free(grep);
  return gp;
}

GREP *save_grep(GREP *grep)/*usilic*/
{
  GREP *gp;

  gp = calloc(sizeof(GREP), 1);
  gp->prev = grep;
  return gp;
}


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
    while (rsp) { /* build-in */
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

static STR *save_hist(STR *hist, char name[], PN pn)
{
  if (!hist) {
    hist = calloc(sizeof(STR), 1);
    hist->name = strdup(name);
    memcpy(&hist->pn, &pn, sizeof(PN));
  } else hist->next = save_hist(hist->next, name, pn);
  return hist;
}

static int find_hist(STR *hist, char *name, int *countp)
{
  int count;

  if (!hist) count = 0;
  else for (count = 1; hist->next; hist = hist->next) count++;
  *countp = count;
  if (!hist || name == F6 || strcmp(name, hist->name)) return 0;
  return 1;
}

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
  if (name[count] == '\r') /* '\r' SHELL-prompt */
    name[count] = '\0';
  if (f_find && find_hist(hist, name, &count)) return hist;
  if (hist && count == n_history) {
    sp = save_hist(hist->next, name, pn);
    free(hist->name);
    free(hist);
  } else sp = save_hist(hist, name, pn);
  return sp;
}

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

void find_list(LIST *lp, char name[], int *ip)/*usilic*/
{
  if (lp && !*ip) {
    find_list(lp->left, name, ip);
    if (!strcmp(lp->name, name)) *ip = 1;
    find_list(lp->right, name, ip);
  }
}

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

STACK *save_stack_dir(STACK *sp, char dir[], char old[], BASE *bp)/*usilic*/
{ /* save current directory (bottom of the stack) */
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

void get_name(char *pathname)/*usilic*/
{ /* set regular filename to pathname */
  char *cp, *cp0 = pathname;

  while ((cp = strchr(pathname, '/'))) pathname = &cp[1];
  if (cp0 != pathname) strcpy(cp0, pathname);
}

int get_dir(char *pathname)/*usilic*/
{ /* set current directory name to pathname */
  char *cp;

  if (!strchr(pathname, '/') || !strcmp(pathname, "/")) return 0;
  while ((cp = strchr(pathname, '/'))) pathname = &cp[1];
  *pathname = 0;
  return 1;
}

void erase_dot(char name[])/*usilic*/
{ /* rid name[] of current ("./") or parent ("../") directory name */
  int i, j;
  char *cp, tmp[N_line];

  for (cp = name; *cp && (cp = strstr(cp, "./")); )
    if (cp > name && cp[-1] == '.') { /* skip "../" */
      cp++;
      continue;
    } else if (cp[2]) strcpy(cp, &cp[2]); /* erase "./" */
    else *cp = '\0';
  if ((cp = strstr(name, "../"))) {
    strcpy(tmp, cp);
    *cp = '\0';
    while (strlen(tmp) && (cp = strstr(tmp, "../")))
      if (cp == tmp) {
	get_par(name);
	strcpy(tmp, &cp[3]);
      } else {
	i = strlen(name);
	j = cp - tmp;
	memcpy(&name[i], tmp, j);
	name[i + j] = '\0';
	strcpy(tmp, cp);
      }
  } else *tmp = '\0';
  if (strlen(tmp)) strcat(name, tmp);
}

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
    unlink(s_S(S_hist)); /* rm ~/.usilic/history */
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
    if (!strchr(str, K_z)) { /* ~/.usilic/history doesn't contain `EOF (^Z)' */
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
  if (*lp->old == '.') { /* mode-line: ./ (ls) */
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
      vt_charset((charset) ? charset : C_euc);
    }
    f = 1;
  } while (0);
  if (!f) { /* ~/.usilic/history is broken or old format */
    save_error("%s: %s: %s.\n", s_S(S_usilic), s_S(S_hist), s_S(S_errm));
    kill(getpid(), SIGSEGV);
  }
  fclose(fp);
}

void write_list(LL ll, SS ss, LINE *lp, char name[])/*usilic*/
{
  FILE *fp;
  char str[N_line], str1[N_line];
  
  if (!s_S(S_hist)) return;
  strcpy(str, s_S(S_hist));
  strcat(str, ".old");
  rename(s_S(S_hist), str); /* backup */
  if (!(fp = fopen(s_S(S_hist), "w"))) return;
  get_time(str);
  fprintf(fp, "# ~/.usilic/history: %s\n", getenv("USILIC"));
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

char *utoa(u_int u)/*usilic*/
{ /* convert unsigned-int to string */ 
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

char *tobo(char str[], int f_nl)/*usilic*/
{ /* convert str[] to bold attribute */
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

char *toul(char str[], int f_nl)/*usilic*/
{ /* convert str[] to underline attribute */
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

int ichrstr(char str0[], char c, char *str)/*usilic*/
{
  char *cp;

  if ((str && (cp = strstr(str0, str))) || (!str && (cp = strchr(str0, c))))
    return (int)cp - (int)str0 + 1;
  else return 0;
}

void print_arg(char arg[], int size, u_char *buff, CHILD *chp, int f_M)/*usilic*/
{
  *arg = K_z; /* doesn't display mode-line[4] */
  if (chp)
    if (size >= 0 && s_S(S_sh) && f_M != T_pipe5)
      sprintf(arg, " %d (%s)", size, *chp->argv);
    else sprintf(arg, " (%s)", *chp->argv);
  else 
    switch(f_M) {
    case T_dir:
      sprintf(&arg[1], "(%s)", s_S(S_ls));
      break;
    case T_du:
      sprintf(&arg[1], "(%s)", s_S(S_du));
      break;
    case T_man:
      sprintf(&arg[1], "(%s)", s_S(S_man));
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

void dup_error()/*usilic*/
{
  int fd;
  
  if (!s_S(S_error)) return;
  fd = fileno(fopen(s_S(S_error), "a"));
  dup2(fd, 2);
  close(fd);
}

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

void save_stderr(char *fmt, ...)/*usilic*/
{ /* print to `stderr' after reset terminal */
  FILE *fp;
  va_list ap;

  if (!s_S(S_stderr)) return;
  fp = fopen(s_S(S_stderr), "w");
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  fclose(fp);
}

void save_intr(int n_error)/*usilic*/
{
  FILE *fp;

  if (!n_error) return;
  fp = fopen(s_S(S_intr), "w");
  fprintf(fp, "%d\n", n_error);
  fclose(fp);
}

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

char *is_inst(char *path, char *prog)/*usilic*/
{ /* search `prog' on `path' */
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

int is_access(char name[], BASE *bp)/*usilic*/
{
  struct stat st;

  if (access(name, R_OK|F_OK) < 0) { /* No such file or directory */
    make_bp(bp, strerror(errno), name, NULL);
    return -1;
  } else {
    stat(name, &st);
    if (!st.st_size) { /* No size */
      make_bp(bp, s_S(S_err1), name, NULL);
      return -1;
    }
  }
  return 0;
}  

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
  
int open_pty(char str[], int f_open)/*usilic*/
{
  char *cp, *np;
  int tfd, i;

  for (cp = "pqrs", np = NULL; *cp; cp++)
    for (np = "0123456789abcdef"; *np; np++) {
      sprintf(str, "/dev/pty%c%c", *cp, *np);
      if ((tfd = open(str, O_RDWR)) >= 0) {
	str[5] = 't';
	if ((i = open(str, O_RDWR)) >= 0) {
	  str[5] = 'p';
	  close(i);
	  if (f_open) return tfd;
	  else close(tfd);
	  return 1;
	}
	str[5] = 'p';
	close(tfd);
      }
    }
  return -1;
}

u_char *mtoa(int f_M)/*usilic*/
{ /* exchange order of m.i */
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

void print_buff(u_char buff[], int kanji_s)/*usilic*/
{ /* display control character with reverse attribute */
  int c, i, len, kanji_e;

  len = strlen(buff);
  if ((kanji_e = count_k(&buff[kanji_s], len - kanji_s))) len--;
  if (kanji_s) {
    i = 1;
    vt_putchar(' ');
  } else i = 0;
  for (; i < len; i++)
    if ((c = buff[i]) && (c < ' ' || c == K_dl || c == K_at)) {
      if (c == K_dl) c -= 0x80;
      else if (c == K_at) c = 0;
      vt_stand();
      vt_putchar(c + '@');
      vt_endstand();
    } else vt_putchar(buff[i]);
  if (kanji_e) vt_putchar(' ');
}

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

void print_pid(int pid)/*usilic*/
{
  FILE *fp = fopen(s_S(S_pid), "w");
  fprintf(fp, "%d\n", pid);
  fclose(fp);
}

int strint(int tab[], int u)/*usilic*/
{
  int i;

  for (i = 0; tab[i]; i++) if (tab[i] == u) break;
  return tab[i];
}

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

#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <curses.h>
#ifdef __CYGWIN32__
#include <ncurses/term.h>
#else
#include <term.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "tc.P"
#define N_LB (1+2+2+2+2+2+1+2+2+2+2+2+2)
static struct TC { /* termcap function */
  char *cs;
  char *ho, *cm;
  char *cl, *ce;
  char *so, *se;
  char *us, *ue;
  char *md, *me;
  char *le;
  char *sc, *rc;
  char *kl, *kr;
  char *ku, *kd;
  char *kP, *kN;
  char *sf, *sr;
  char *ti, *te;
} Tc;

static int outc(int c)
{
  if (!vt_mode('D')) return 0;
  putchar(c);
  return c;
}

static int tc_putchar(int c)
{
  if (c == K_g) {
    putchar(K_g);
    fflush(stdout);
    return K_g;
  }
  if (!vt_mode('D')) return 0;
  putchar(c);
  return c;
}

static void tc_flush(void)
{
  if (!vt_mode('D')) return;
  fflush(stdout);
}

static void tc_endstand(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.se, 1, outc);
  fflush(stdout);
}

static void tc_stand(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.so, 1, outc);
  fflush(stdout);
}

static void tc_move(int row, int col)
{
  if (!vt_mode('D')) return;
  tputs(tgoto(Tc.cm, col, row), 1, outc);
  fflush(stdout);
}

static int tc_row(int i)/*tc*/
{
  static int row;
  
#ifdef __CYGWIN32__
  if (i) 
#else
  if (i) 
#endif
    row = i;
  return row;
}

static int tc_col(int i)
{
  static int col;

  if (i) col = i;
  return col;
}

static void tc_clear(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.cl, vt_row(0), outc);
  fflush(stdout);
}

static void tc_eeol(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.ce, 1, outc);
  fflush(stdout);
}

static void tc_left(void)
{
  if (!vt_mode('D')) return;
  tputs(Tc.le, 1, outc);
  fflush(stdout);
}

static void tc_sf(void)
{
  tputs(Tc.sf, 1, outc);
}

static void tc_sr(void)
{
  tputs(Tc.sr, 1, outc);
}

static void tc_rc(void)/*tc*/
{
  if (!vt_mode('D') || !Tc.rc) return;
  tputs(Tc.rc, 1, outc);
  fflush(stdout);
}

static void tc_sc(void)/*tc*/
{
  if (!vt_mode('D') || !Tc.sc) return;
  tputs(Tc.sc, 1, outc);
  fflush(stdout);
}

static void tc_cs(int m, int n)/*tc*/
{
  if (Tc.cs) {
    tputs(tgoto(Tc.cs, m, n), 1, outc);
    fflush(stdout);
  }
}

static void tc_seta(int attr)
{
  if (!vt_mode('D')) return;
  switch(attr) {
  case A_us:
  case A_us2:
    if (Tc.us) tputs(Tc.us, 1, outc);
    else if (Tc.so) tputs(Tc.so, 1, outc);
    break;
  case A_ue:
    if (Tc.ue) tputs(Tc.ue, 1, outc);
    else if (Tc.se) tputs(Tc.se, 1, outc);
    break;
  case A_md:
  case A_md2:
  case A_md3:
    if (Tc.md) tputs(Tc.md, 1, outc);
    break;
  case A_me:
    if (Tc.me) tputs(Tc.me, 1, outc);
    break;
  case A_cs:
    if (Tc.so) tputs(Tc.so, 1, outc);
    break;
  case A_ce:
    if (Tc.se) tputs(Tc.se, 1, outc);
    break;
  }
}

static char *tc_geta(int attr)
{
  switch(attr) {
  case A_us:
  case A_us2:
    return (Tc.us) ? Tc.us : Tc.so;
    break;
  case A_ue:
    return (Tc.ue) ? Tc.ue : Tc.se;
    break;
  case A_md:
  case A_md2:
    return Tc.md;
    break;
  case A_me:
    return Tc.me;
    break;
  case A_cs:
    return Tc.so;
    break;
  case A_ce:
    return Tc.se;
    break;
  default:
    return NULL;
  }
  return NULL;
}

static void tc_ks(char *ks[])
{
  ks[0] = Tc.ku;
  ks[1] = Tc.kd;
  ks[2] = Tc.kr;
  ks[3] = Tc.kl;
  ks[4] = Tc.kP;
  ks[5] = Tc.kN;
}

static int tc_mode(int mode)
{
  static struct {
    u_int disp	: 1; /* display window */
    u_int win	: 1; /* SIGWINCH at SHELL */
    u_int hs	: 1;
  } f;

  switch(mode) {
  case 0:
    tputs(Tc.ti, 1, outc);
    if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
    break;
  case 'D':
    return f.disp;
  case 'd':
    f.disp = 1;
    break;
  case -'d':
    f.disp = 0;
    break;
  case 'W':
    return f.win;
  case 'w':
    f.win = 1;
    break;
  case -'w':
    f.win = 0;
    break;
  case 'H':
    return f.hs;
  case 'h':
    f.hs = 1;
    break;
  case -'h':
    f.hs = 0;
    break;
  default:
    break;
  }
  return 0;
}

static int tc_cursor(int f_cursor)
{
  key_winch(-f_cursor);
  return 0;
}

VT *tc_load(void)/*usilic*/
{
  int i, j;
  char *term;
  static VT vt = {
    tc_flush,
    vprintf,
    tc_putchar,
    tc_endstand,
    tc_stand,
    tc_move,
    tc_row,
    tc_col,
    tc_clear,
    tc_eeol,
    tc_left,
    tc_sf,
    tc_sr,
    tc_rc,
    tc_sc,
    tc_cs,
    tc_seta,
    tc_geta,
    tc_ks,
    tc_mode,
    tc_cursor,
  };
  int state;

  if ((term = getenv("TERM")) == NULL) {
    fprintf(stderr, "%s: TERM environment variable is not set.\n", 
	    s_S(S_usilic));
    free_S();
    exit(1);
  }
  if (!strcmp(term, "screen")) setenv("TERM", "vt100", 1);
  else if (!strcmp(term, "cygwin")) setenv("TERM", "kterm", 1);
  setupterm(term, 1, &state);
  if (state != 1) {
    fprintf(stderr, "%s: %s.\n", s_S(S_usilic), s_S(S_erru));
    free_S();
    exit(1);
  }
  Tc.cs = change_scroll_region;
  Tc.ho = cursor_home; 
  Tc.cm = cursor_address;
  Tc.cl = clear_screen; 
  Tc.ce = clr_eol;
  Tc.so = enter_standout_mode; 
  Tc.se = exit_standout_mode;
  Tc.us = enter_underline_mode; 
  Tc.ue = exit_underline_mode;
  Tc.md = enter_bold_mode; 
  Tc.me = exit_attribute_mode;
  Tc.le = cursor_left;
  Tc.sc = save_cursor; 
  Tc.rc = restore_cursor;
  Tc.kl = key_left; 
  Tc.kr = key_right;
  Tc.ku = key_up; 
  Tc.kd = key_down;
  Tc.kP = key_ppage; 
  Tc.kN = key_npage;
  Tc.sf = scroll_forward; 
  Tc.sr = scroll_reverse;
  Tc.ti = enter_ca_mode; 
  Tc.te = exit_ca_mode;
  if (!Tc.cm) {
    fprintf(stderr, "%s: %s.\n", s_S(S_usilic), s_S(S_erru));
    free_S();
    exit(1);
  }
  get_winsize(&i, &j);
  tc_row(i);
  tc_col(j);
  if (!getenv("LINES") || !getenv("COLUMNS")) {
    setenv("LINES", utoa(i), 1);
    setenv("COLUMNS", utoa(j), 1);
  }
  return &vt;
}

#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

static struct {
  int sfd;
  char path[N_line];
} Usilic;

static void usage(char usilic[], char version[])
{

  char *cp;
  static char tmp[256];

  memcpy(tmp, version, 80);
  *strchr(tmp, '\n') = '\0';
  if ((cp = strchr(tmp, '\n'))) *cp = '\0';
  fprintf(stderr, "Usage: %s\n", usilic);
  fprintf(stderr, "       %s [directory]\n", usilic);
  fprintf(stderr, "       %s [file] [< file]\n", usilic);
  fprintf(stderr, "%s\n", tmp);
}
 
static void main_pipe(int dummy)
{
  if (Usilic.sfd) {
    close(Usilic.sfd);
    Usilic.sfd = 0;
    unlink(Usilic.path);
  }
  free_S();
  exit(0);
}

static int set_path(char dir[], char name[])
{
  int f_M, i;
  struct stat st;
  
  if (*name != '/') {
    char *p = alloca(strlen(dir) + strlen(name) + 1);
    strcpy(p, dir);
    strcat(p, name);
    strcpy(name, p); /* full path */
  }
  stat(name, &st);
  if ((i = strchr(name, '\0')[-1] == '/') || S_ISDIR(st.st_mode)) {
    f_M = T_dir;
    if (!i) strcat(name, "/");
  } else f_M = T_file;
  erase_dot(name);
  strcpy(dir, name);
  get_dir(dir);
  return f_M;
}

static void from_shell(char *name, int pid, int f, char *mess)
{
  char str[N_pipe+1];
  int i, c, len, row;
  u_char m;

  get_winsize(&row, &c);
  sprintf(&strstr(mess, "/s.")[3], "%d.%d", (int)getuid()
	  , (pid) ? pid : (int)getppid());
  if (!name && (!f || f == 'p')) {
    struct sockaddr_un cli_addr, serv_addr;
    int sfd, clilen;
    m = T_pipe2;
    signal(SIGPIPE, main_pipe);
    strcpy(Usilic.path, mess);
    if ((sfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
      fprintf(stderr, "%s: Can't open socket\n", s_S(S_usilic));
      free_S();
      exit(0);
    }
    serv_addr.sun_family = AF_UNIX;
    strcpy(serv_addr.sun_path, Usilic.path);
    unlink(Usilic.path);
    if (bind(sfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0) {
      clilen = sizeof(cli_addr);
      if (listen(sfd, 5) >= 0)
	if ((Usilic.sfd = accept(sfd, (struct sockaddr *)&cli_addr, &clilen)) 
	    >= 0) {
	  send(Usilic.sfd, &m, sizeof(u_char), 0);
	  c = getpid();
	  send(Usilic.sfd, &c, sizeof(int), 0);
	  for (i = row-1; i && (c = fgetc(stdin)) != EOF; ) {
	    send(Usilic.sfd, &c, 1, 0);
	    if (c == '\n') i--;
	    else if (c == '\r') {
	      if ((c = fgetc(stdin)) == '\n') i--;
	      send(Usilic.sfd, &c, 1, 0);
	    }
	  }
	  if (c != EOF) 
	    do {
	      c = fread(str, 1, N_pipe, stdin);
	      send(Usilic.sfd, str, c, 0);
	    } while (c == N_pipe);
	  close(Usilic.sfd);
	  unlink(Usilic.path);
	} else perror("accept");
      else perror("listen");
    } else perror("bind");
  } else {
    FILE *fp;
    char *cp, dir[N_line];
    getcwd(dir, N_line);
    if (strchr(dir, '\0')[-1] != '/') strcat(dir, "/");
    switch(f) {
    case 'u':
      m = M_du;
      break;
    case 'U':
      m = T_du2;
      break;
    default:
      if ((cp = strchr(name, '\r'))) *cp = '\0';
      m = set_path(dir, name);
      if (access(name, F_OK) < 0) {
	printf("%s: %s: %s\n", s_S(S_usilic), name, strerror(ENOENT));
	exit(1);
      }
      break;
    }
    sprintf(str, "%s.%s", mess, s_S(S_usilic));
    fp = fopen(str, "w");
    fwrite(&m, 1, sizeof(u_char), fp);
    len = strlen(dir) + 1;
    fwrite(&len, 1, sizeof(int), fp);
    fwrite(dir, 1, len, fp);
    if (m != M_du) {
      len = strlen(name) + 1;
      fwrite(&len, 1, sizeof(int), fp);
      fwrite(name, 1, len, fp);
    }
    fclose(fp);
    rename(str, mess);
  }
  free_S();
  exit(0);
}

static int check_ps(int f_tty, int pid, char **err)
{
  int n_pid, pv[4];
  char str[256];
  FILE *fp;
  static char n[4];
  static char *sp[] = {
    "ps",
    "grep",
    "usilic",
    n,
  };
  
  if (f_tty) sprintf(sp[3], "\\-%d", f_tty);
  pipe(pv);
  if (!fork()) {
    pipe(&pv[2]);
    if (!fork()) {
      close(1);
      dup2(pv[3], 1);
      close(pv[3]);
      close(pv[2]);
      execlp(sp[0], sp[0], "a", NULL);
    }
    close(0);
    dup2(pv[2], 0);
    close(pv[3]);
    close(pv[2]);
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    if (f_tty) execlp(sp[1], sp[1], sp[3], "-", NULL);
    else execlp(sp[1], sp[1], sp[2], "-", NULL);
  }
  close(pv[1]);
  fp = fdopen(pv[0], "r");
  n_pid = 0;
  if (f_tty) {
    do {
      if (!fgets(str, 256, fp)) break;
      if (strstr(str, sp[2])) n_pid++;
    } while (n_pid < 2);
    if (n_pid == 2) 
      fprintf(stderr, "%s -%d: Invalid terminal\n", sp[2], f_tty);
    else {
      fclose(fp);
      return 0;
    }
  } else {
    do {
      if (!fgets(str, 256, fp)) break;
      sscanf(str, "%d", &n_pid);
    } while (n_pid != pid);
    if (!n_pid) fprintf(stderr, "Unknown error\n");
    else fprintf(stderr, "\n     Already running.\n");
  }
  fclose(fp);
  return 1;
}
    
static void check_pl(int f_tty, char **err)
{
  struct stat st;
  int i, pid;
  FILE *fp;
  char *s_pid;

  if (!f_tty) {
    char *cp = getenv("HOME");
    s_pid = alloca(strlen(cp) + 10);
    strcpy(s_pid, cp);
    if (strchr(s_pid, '\0')[-1] != '/') strcat(s_pid, "/");
    strcat(s_pid, ".usilic/pid");
    if (!stat(s_pid, &st)) {
      fp = fopen(s_pid, "r");
      fscanf(fp, "%d", &pid);
      fclose(fp);
      if (pid && !kill(pid, 0)) {
	struct stat old;
	stat(s_pid, &old);
	fprintf(stderr, "usilic: Process-ID %d ... ", pid);
	fflush(stdout);
	for (i = 6; i; i--) {
	  kill(pid, SIGCONT);
	  stat(s_pid, &st);
	  if (st.st_mtime != old.st_mtime) break;
	  sleep(1);
	}
	if (i) {
	  if (check_ps(0, pid, err)) {
	    free_S();
	    exit(1);
	  }
	} else unlink(s_pid);
      }
    }
  } else if (check_ps(f_tty, 0, err)) {
    free_S();
    exit(1);
  }
} 

int main(int argc, char *argv[])
{
  char pathname[N_line], args[N_line] = {'\0',};
  struct stat st;
  struct termios otty;
  static struct {
    u_int shell	: 1;
    u_int usilic	: 8;
  } f;
  static char usilic[] = "usilic";
  static char tmp[N_line];
  static char *err[] = {
    "Invalid option",
    "USILIC environment variable is set",
  };
  /*
   * f.usilic
   * 	& 0x80	: file manager (with history-list)
   *	& 0x40	: file manager [directory]
   *	& 0x20	: pager (stdin)
   *	& 0x10	: pager [file]
   *	& 0x08	: -h
   *	& 0x04	: -?
   */
  static char version[] = {
#include "version.h"
  };
  int i = 0;
  FILE *pin = F1;

  printf("ggg\n");
  conf(tmp, NULL, NULL, 0);
  if (stat(tmp, &st) < 0) {
    if (mkdir(tmp, 0) < 0) {
      fprintf(stderr, "%s: %s: %s\n", usilic, tmp, strerror(errno));
      exit(1);
    }
    chmod(tmp, S_IRWXU|S_IRWXG|S_IRWXO);
  } else if (!S_ISDIR(st.st_mode) || access(tmp, R_OK|W_OK) < 0 ) {
    fprintf(stderr, "%s: %s: %s\n", usilic, tmp, strerror(errno));
    exit(1);
  }
  sprintf(tmp, "%s/s.", tmp);
  *pathname = '\0';
  if (!getenv("USILICVT") && tcgetattr(0, &otty) < 0) f.usilic |= 0x20;
  f.shell = getenv("USILIC") != NULL;
  if (argc == 1) {
    if (f.shell)
      if (f.usilic & 0x20) from_shell(NULL, 0, 0, tmp);
      else {
	fprintf(stderr, "%s: %s\n", usilic, err[1]);
	exit(1);
      }
    else if (!(f.usilic & 0x20)) f.usilic |= 0x80;
  } else {
    while (*++argv)
      if (**argv == '-')
	switch(i = (*argv)[1]) {
	case 'p':
	  if (!f.shell || !(f.usilic & 0x20)) {
	    fprintf(stderr, "%s: %s -p\n", usilic, err[0]);
	    usage(usilic, version);
	    exit(1);
	  }
	  from_shell(NULL, atoi(argv[1]), i, tmp);
	  break;
	case 'l':
	  if (f.shell) {
	    fprintf(stderr, "%s: %s\n", usilic, err[1]);
	    exit(1);
	  }
	  f.usilic |= 0x80;
	  strcpy(args, *argv);
	  break;
	case 'u':
	case 'U':
	  if (!f.shell) {
	    fprintf(stderr, "%s: %s -%c\n", usilic, err[0], i);
	    usage(usilic, version);
	    exit(1);
	  }
	  from_shell(NULL, 0, i, tmp);
	  break;
	case '?':
	case 'h':
	  f.usilic &= ~(0x80 | 0x20);
	  f.usilic |= (i == '?') ? 0x04 : 0x08;
	  sprintf(strchr(args, '\0'), " %s", *argv);
	  break;
	default:
	  fprintf(stderr, "%s: %s -%c\n", usilic, err[0], i);
	  usage(usilic, version);
	  exit(1);
	  break;
	}
      else if (f.usilic & 0x20) continue;
      else
	if (stat(*argv, &st) < 0 || access(*argv, R_OK) < 0) {
	  fprintf(stderr, "%s: %s: %s\n", usilic, *argv, strerror(errno));
	  exit(1);
	} else {
	  if (f.shell) from_shell(*argv, 0, 0, tmp);
	  f.usilic &= ~0x80;
	  f.usilic |= (S_ISDIR(st.st_mode)) ? 0x40 : 0x10;
	  strcpy(pathname, *argv);
	  sprintf(strchr(args, '\0'), " %s", *argv);
	}
  }  
  if (f.usilic & 0x20) {
    strcpy(pathname, s_P(P_mode)[T_pipe]);
    pin = stdin;
  }
  if (f.shell) {
    fprintf(stderr, "%s: %s\n", usilic, err[1]);
    free_S();
    exit(1);
  }
  if (f.usilic & 0x80) check_pl(0, err);
  signal(SIGCHLD, sig_child);
  if (conf(NULL, args, version, f.usilic) < 0) {
    free_S();
    exit(1);
  }
  if (vt_set(f.usilic) < 0) {
    free_S();
    exit(1);
  }
  save_signal(f.usilic);
  sprintf(tmp, "%s v%s", usilic, &(strchr(version, ' '))[1]);
  *strchr(tmp, '-') = '\0';
  setenv("USILIC", tmp, 1);
  if (access(s_S(S_usilicrc), F_OK) < 0) {
    fprintf(stderr, "%s: %s: %s\n", usilic, s_S(S_usilicrc), strerror(ENOENT));
    free_S();
    exit(1);
  }
  if (vt_init(1) < 0) {
    free_S();
    exit(1);
  }
  print_stderr(s_S(S_stderr));
  menu(pathname, pin);
  exit_usilic();
  print_stderr(NULL);
  exit(exit_ALLOC(NULL));
  return 0;
}

void exit_usilic(void)/*usilic*/
{
  signal(SIGSEGV, SIG_DFL);
  if (is_fm()) {
    start_shell(0, 0, NULL, NULL, NULL, NULL);
    if (vt_set(0) & 0x80) {
      int i;
      char str[256];
      strcpy(str, s_S(S_write));
      i = ichrstr(str, 0, "w.usilic");
      strcpy(&str[i-1], "shell");
      rename(s_S(S_log), str);
      strcpy(&str[i-1], "error");
      rename(s_S(S_error), str);
    }
  }
  unlink_file();
  vt_init(-1);
  free_S();
  free_h(NULL);
}  

#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static VT *vt;

void vt_printf(char *fmt, ...)/*usilic*/
{
  va_list ap;

  if (!vt_mode('D')) return;
  va_start(ap, fmt);
  vt->printf(fmt, ap);
  va_end(ap);
}

int vt_putchar(int c)/*usilic*/
{
  return vt->put_char(c);
}

void vt_flush(void)/*usilic*/
{
  vt->flush();
}

void vt_endstand(void)/*usilic*/
{
  vt->endstand();
}

void vt_stand(void)/*usilic*/
{
  vt->stand();
}

void vt_move(int row, int col)/*usilic*/
{
  vt->move(row, col);
}

int vt_row(int row)/*usilic*/
{
  return vt->row(row);
}

int vt_col(int col)/*usilic*/
{
  return vt->col(col);
}

void vt_clear(void)/*usilic*/
{
  vt->clear();
}

void vt_eeol(void)/*usilic*/
{
  vt->eeol();
}

void vt_left(void)/*usilic*/
{
  vt->left();
}

void vt_sf(void)/*usilic*/
{
  vt->sf();
}

void vt_sr(void)/*usilic*/
{
  vt->sr();
}

void vt_rc(void)/*usilic*/
{
  vt->rc();
}

void vt_sc(void)/*usilic*/
{
  vt->sc();
}

void vt_cs(int m, int n)/*usilic*/
{
  vt->cs(m, n);
}

void vt_seta(int attr)/*usilic*/
{
  vt->seta(attr);
}

char *vt_geta(int attr)/*usilic*/
{
  return vt->geta(attr);
}

void vt_ks(char *ks[])/*usilic*/
{
  vt->ks(ks);
}

int vt_mode(int mode)/*usilic*/
{
  return vt->mode(mode);
}

int vt_cursor(int f_cursor)/*usilic*/
{
  return vt->cursor(f_cursor);
}

int vt_set(int f_usilic0)/*usilic*/
{
  static int f_usilic;

  if (f_usilic0) {
    f_usilic = f_usilic0;
    vt = tc_load();
  }
  return f_usilic;
}

int get_winsize(int *rowp, int *colp)/*usilic*/
{
  struct winsize winsize;
  int fd;

  winsize.ws_row = 0;
  fd = open("/dev/tty", O_RDONLY);
  if (ioctl(fd, TIOCGWINSZ, &winsize) < 0)
    fprintf(stderr, "%s: get_winsize(): %s\n", s_S(S_usilic), strerror(errno));
  else {
    if (!winsize.ws_row) {
      *rowp = 80;
      *colp = 30;
    } else {
      *rowp = winsize.ws_row;
      *colp = winsize.ws_col;
    }
  }
  close(fd);
  return 0;
}

int set_winsize(int row, int col)/*usilic*/
{
  struct winsize winsize;

  winsize.ws_row = row;
  winsize.ws_col = col;
  if (ioctl(1, TIOCSWINSZ, &winsize) < 0)
    save_error("%s: set_winsize(): %s.\n", s_S(S_usilic), strerror(errno));
  return 0;
}

int vt_getch(void)/*usilic*/
{
  int i;
  u_char c;

  if (!(i = read(vt_tfd(0), &c, 1)) || i < 0) return 0;
  return (c) ? c : K_at; /* Ctrl+Space */
}

int vt_charset(int i)/*usilic*/
{
  static int charset;

  if (i) charset = i;
  return charset;
}

void vt_winsize(void)/*usilic*/
{
  int row, col;

  get_winsize(&row, &col);
  vt_row(row);
  vt_col(col);
  setenv("LINES", utoa(row), 1);
  setenv("COLUMNS", utoa(col), 1);
  if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
}

void vt_setwin(int row, int col)/*usilic*/
{
  vt_row(row);
  vt_col(col);
  if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
}

int vt_tfd(int i)/*usilic*/
{
  static int tfd;

  if (i < 0) close(tfd);
  else if (i > 0) tfd = i;
  return tfd;
}

int vt_init(int init)/*usilic*/
{
  static struct termios tty_termio, vt_termio;
  static int tfd; 
  int row, col;
  char *usiliccharset;

  switch(init) {
  case 1:
    get_winsize(&row, &col);
    vt_row(row);
    vt_col(col);
    if ((tfd = open(s_S(S_tty), O_RDONLY)) < 0) tfd = 2;
    fprintf(stderr, "s_S(S_tty)=%s,tfd=%d\n", s_S(S_tty),tfd);
    if (!isatty(tfd)) {
      fprintf(stderr, "%s: %s\n", s_S(S_usilic), s_S(S_erru));
      if (tfd != 2) close(tfd);
      return -1;
    }
    vt_tfd(tfd);
    tcgetattr(tfd, &tty_termio);
    vt_termio = tty_termio;
#if 0
    cfmakeraw(&vt_termio);
    vt_termio.c_iflag &= ~(INLCR|ICRNL|ISTRIP|IGNBRK|IGNCR);
    vt_termio.c_oflag &= OPOST;
    vt_termio.c_oflag &= ~ONLCR;
    vt_termio.c_lflag &= ~(ICANON|ECHO|NOFLSH);
#else
    vt_termio.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
			    |INLCR|IGNCR|ICRNL|IXON);
    vt_termio.c_oflag &= ~OPOST;
    vt_termio.c_oflag &= ~ONLCR;
    vt_termio.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN|NOFLSH);
    vt_termio.c_cflag &= ~(CSIZE|PARENB);
    vt_termio.c_cflag |= CS8;
#endif
    vt_termio.c_cc[VMIN] = 0;
    vt_termio.c_cc[VTIME] = 1;
    tcsetattr(tfd, TCSANOW, &vt_termio);
    vt_mode('d');
    vt_mode(0);
    if ((usiliccharset = getenv("USILICCHARSET")))
      vt_charset((!strcasecmp(usiliccharset, "ascii")) ? C_ascii : C_euc);
    break;
  case 2:
    if (!vt_mode('H')) vt_cs(vt_row(0) - 2, 0);
  case -9:
    tcsetattr(tfd, TCSANOW, &vt_termio);	
    break;
  case -1:
    vt_seta(A_me);
  case -2:
    if (!vt_mode('H')) vt_cs(vt_row(0) - 1, 0);
    vt_move(vt_row(0) - 1, 0);
    vt_eeol();
    tcsetattr(tfd, TCSANOW, &tty_termio);
    if (init == -1) {
      close(tfd);
      tfd = 0;
    }
    break;
  case 3:
    vt_termio.c_cc[VMIN] = 0;
    vt_termio.c_cc[VTIME] = 1;
    tcsetattr(tfd, TCSANOW, &vt_termio);
    break;
  case -3:
    vt_termio.c_cc[VMIN] = 0;
    vt_termio.c_cc[VTIME] = 0;
    tcsetattr(tfd, TCSANOW, &vt_termio);
    break;
  default:
    break;
  } 
  return 0;
}

