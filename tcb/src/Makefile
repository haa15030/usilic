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

.SUFFIXES: .P
.c.P:
	@grep -h '\/\*$*\*\/' $< |sed -e 's/\/\*$*\*\//;/' >$@
.c.o:
	$(CC) -c $(CFLAGS) $<

default: usilic

usilic:	$(PROT) $(OBJS)
	$(CC) $(LDFLAGS) -o $@  $(OBJS) $(LIBS)

tcb.P:	alloc.c child.c code.c disp.c fork.c init.c \
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c tcb.c vt.c
	@grep '\/\*tcb\*\/' alloc.c child.c code.c disp.c fork.c init.c \
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c tcb.c vt.c -h|\
	sed -e 's/\/\*tcb\*\//;/' >$@

$(OBJS): mode.h

menu.o mode.o: menu.h

tcb.o:	version.h

alloc.o: alloc.h

debug:
	mkdir -p debug
	/bin/ls Makefile *.[ch]|xargs -t ../perl/debug.pl debug
	ln -sf ../config.h debug

clean:
	rm -rf usilic $(OBJS) $(PROT) *~ debug
