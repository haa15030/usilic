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
