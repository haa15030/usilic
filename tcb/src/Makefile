# Makefile for macOS (Darwin) version
LIBS=	-lncurses
CC=gcc
CFLAGS= -Wall -O3 -g
#CFLAGS= -Wall -O3
LDFLAGS=
#LDFLAGS= -s
OBJS=	alloc.o child.o code.o disp.o fork.o init.o \
    	key.o menu.o mode.o scroll.o shell.o str.o sub.o tc.o tcb.o vt.o

PROT=	code.P mode.P tc.P tcb.P

# Program name this build is compiled/branded as (2026-08-05): every
# runtime-visible string (banner, ~/.PROG/ config directory and
# ~/.PROG/PROGrc rcfile, PROG/PROGCHARSET/PROGVT env var names, etc --
# see str.c's own S_tcb/PROGNAME usage) is generated into progname.h
# from this one variable. Override on the command line to build under
# any other name, e.g. `make PROG=foo`; defaults to this checkout's own
# actual use (see CLAUDE.md's own project history for why).
PROG ?= usilic
PROG_UPPER = $(shell echo $(PROG) | tr '[:lower:]' '[:upper:]')

.SUFFIXES: .P
.c.P:
	@grep -h '\/\*$*\*\/' $< |sed -e 's/\/\*$*\*\//;/' >$@
.c.o:
	$(CC) -c $(CFLAGS) $<

default: $(PROG)

$(PROG):	progname.h $(PROT) $(OBJS)
	$(CC) $(LDFLAGS) -o $@  $(OBJS) $(LIBS)

# Regenerated on every build rather than checked in/hand-edited (see
# PROG's own comment above) -- always matches whatever PROG the most
# recent `make` invocation used. .PHONY since Make has no way to detect
# "PROG changed since the last build" from file timestamps alone; every
# object file lists this as a prerequisite too (see $(OBJS): below) so
# a PROG change always triggers a full rebuild, matching this project's
# own established `make clean && make` habit anyway.
.PHONY: progname.h
progname.h:
	@printf '#ifndef _PROGNAME_H\n#define _PROGNAME_H\n#define PROGNAME "%s"\n#define PROGNAME_UPPER "%s"\n#define ENV_PROG "%s"\n#define ENV_CHARSET "%sCHARSET"\n#define ENV_VT "%sVT"\n#endif\n' "$(PROG)" "$(PROG_UPPER)" "$(PROG_UPPER)" "$(PROG_UPPER)" "$(PROG_UPPER)" > $@

tcb.P:	alloc.c child.c code.c disp.c fork.c init.c \
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c tcb.c vt.c
	@grep '\/\*tcb\*\/' alloc.c child.c code.c disp.c fork.c init.c \
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c tcb.c vt.c -h|\
	sed -e 's/\/\*tcb\*\//;/' >$@

$(OBJS): mode.h progname.h

menu.o mode.o: menu.h

tcb.o:	version.h

alloc.o: alloc.h

debug:
	mkdir -p debug
	/bin/ls Makefile *.[ch]|xargs -t ../perl/debug.pl debug
	ln -sf ../config.h debug

clean:
	rm -rf $(PROG) $(OBJS) $(PROT) progname.h *~ debug
