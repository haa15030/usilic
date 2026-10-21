#! /usr/bin/perl
# See `debug:' described in ../src/Makefile. 

$Alloc = 'alloc';
$Dst = $ARGV[0];
if (!$Dst) {
    print STDERR "debug.pl [destination-directory] [file..] " . "\n"; 
    exit 1;
}
if (!(-d $Dst)) {
    print STDERR "$Dst: Not a directory" . "\n";
    exit 1;
}
$Dst .= '/' if (!($Dst =~ /\/$/));
$Dst0 = $Dst;
for ($argc = 1; ($Src = $ARGV[$argc]); $argc++) {
    open(SRC, $Src);
    $Dst = $Dst0 . $Src;
    open(DST, '>' . $Dst);
    if ($Src ne $Alloc . '.h') {
	for ($i = 0; <SRC>; print DST) {
	    next if (/ALLOC_/);
	    if (/calloc\(/) {
		s/calloc\(/ALLOC_calloc\(\042$Src:$i\042\, /g;
		$i++;
	    }
	    if (/malloc\(/) {
		s/malloc\(/ALLOC_malloc\(\042$Src:$i\042\, /g;
		$i++;
	    }
	    if (/strdup\(/) {
		s/strdup\(/ALLOC_strdup\(\042$Src:$i\042\, /g;
		$i++;
	    }
	    if (/free\(/) {
		s/free\(/ALLOC_free\(\042$Src:$i\042\, /g;
		$i++;
	    }
	}
    } else {
print DST <<EOF;
#ifndef _ALLOC_H

#define CHECK_FREE

#ifdef CHECK_FREE
/* check free() */
#define ALLOC_calloc		ALLOCcalloc
#define ALLOC_malloc		ALLOCmalloc
#define ALLOC_strdup		ALLOCstrdup
#define ALLOC_free		ALLOCfree
#undef	calloc
#undef	malloc
#undef	strdup
#undef	free
#define calloc			ALLOCcalloc
#define malloc			ALLOCmalloc
#define strdup			ALLOCstrdup
#define free			ALLOCfree
void *ALLOCcalloc(char name[], int n, int size);
void *ALLOCmalloc(char name[], int size);
char *ALLOCstrdup(char name[], const char *s);
void ALLOCfree(char name[], void *vp);
#else
#define ALLOC_calloc(s,n,i)	calloc(n,(i))
#define ALLOC_malloc(s,n)	malloc((n))
#define ALLOC_strdup(s,p)	strdup(p)
#define ALLOC_free(s,p)		free(p)
#endif CHECK_FREE
int	exit_ALLOC(char *name);
void	init_ALLOC(void);

#define	_ALLOC_H
#else

#ifdef CHECK_FREE
#define N_hash 0x10000
#undef calloc
#undef malloc
#undef strdup
#undef free
#include <string.h>
typedef struct ALLOC {
  char *name;
  u_int addr;
  struct {
    u_int type	: 2;
    u_int size	: 30;
  } f;
  struct ALLOC *next;
} ALLOC; 

static ALLOC *hashALLOC[N_hash];

static void *incALLOC(u_int addr, int type, int size, char name[])
{
  ALLOC *ap;
  int i;

  if ((ap = (ALLOC*)malloc(sizeof(ALLOC)))) {
    ap->name = name;
    ap->addr = addr;
    ap->f.type = type;
    ap->f.size = size;
    i = addr & (N_hash-1);
    ap->next = hashALLOC[i];
    hashALLOC[i] = ap;
  }
  return ap;
}

static ALLOC *decALLOC(u_int addr, char name[])
{
  ALLOC *ap, *ab, *a0;
  int i;

  /* delete `addr' from list */
  i = addr & (N_hash-1);
  a0 = hashALLOC[i];
  for (ab = ap = a0; ap && ap->addr != addr; ap = ap->next) ab = ap;
  if (ap)
    if (ap == a0) {
      hashALLOC[i] = ap->next;
      free(ap);
    } else {
      ab->next = ap->next;
      free(ap);
    }
  else incALLOC(addr, 3, addr, name);
  return ap;
}

int exit_ALLOC(char *name)
{
  ALLOC *ap, *ai;
  int i, j;

  fprintf(stderr, "CHECK_FREE: ");
  for (i = j = 0; i < N_hash; i++) {
    for (ap = hashALLOC[i]; ap; ) {
      if (!j) {
	if (name) fprintf(stderr, "error!! (%s)\\n", name);
	else fprintf(stderr, "error!!\\n");
      }
      switch(ap->f.type) {
      case 0:
	fprintf(stderr, "calloc: %s=%d", ap->name, ap->f.size);
	break;
      case 1:
	fprintf(stderr, "malloc: %s=%d", ap->name, ap->f.size);
	break;
      case 2:
	fprintf(stderr, "strdup: %s=%d", ap->name, ap->f.size);
	break;
      case 3:
	fprintf(stderr, "free: %s=0x%x", ap->name, ap->addr);
	break;
      }
      if (name) fprintf(stderr, " (%s)\\n", name);
      else fprintf(stderr, "\\n");
      j = 1;
      ai = ap;
      ap = ap->next;
      free(ai);
    }
  }
  if (!j) {
    if (name) fprintf(stderr, "ok. (%s)\\n", name);
    else fprintf(stderr, "ok. \\n");
  }
  return j;
}

void *ALLOCcalloc(char name[], int n, int size)
{
  void *vp;

  if ((vp = calloc(n, size))) 
    if (!incALLOC((u_int)vp, 0, n*size, name)) vp = NULL;
  return vp;
}

void *ALLOCmalloc(char name[], int size)
{
  void *vp;

  if ((vp = malloc(size))) 
    if (!incALLOC((u_int)vp, 1, size, name)) vp = NULL;
  return vp;
}

char *ALLOCstrdup(char name[], const char *s)
{
  char *cp;

  if ((cp = strdup(s))) 
    if (!incALLOC((u_int)cp, 2, strlen(s) + 1, name)) cp = NULL;
  return cp;
}

void ALLOCfree(char name[], void *vp)
{
  if (decALLOC((u_int)vp, name)) free(vp);
}

void init_ALLOC(void)
{ /* for SHELL (child process) */
  int i;

  for (i = 0; i < N_hash; i++) hashALLOC[i] = NULL;
}
#else
int exit_ALLOC(char *name)
{
  return 0;
}

void init_ALLOC(void)
{
}
#endif CHECK_FREE

#endif	_ALLOC_H

EOF
    }
    close (SRC);
    close (DST);
}
