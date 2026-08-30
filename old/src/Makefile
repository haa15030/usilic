# Makefile for macOS (Darwin) version
LIBS=	-lncurses
CC=gcc	
CFLAGS= -Wall -O3 -g 
#CFLAGS= -Wall -O3
LDFLAGS= 
#LDFLAGS= -s
OBJS=	alloc.o child.o code.o disp.o fork.o init.o \
    	key.o menu.o mode.o scroll.o shell.o str.o sub.o tc.o usilic.o vt.o

PROT=	code.P mode.P tc.P usilic.P

.SUFFIXES: .P
.c.P:
	@grep -h '\/\*$*\*\/' $< |sed -e 's/\/\*$*\*\//;/' >$@
.c.o:
	$(CC) -c $(CFLAGS) $<

default: U

U:	$(PROT) $(OBJS)
	$(CC) $(LDFLAGS) -o $@  $(OBJS) $(LIBS)

usilic.P:	alloc.c child.c code.c disp.c fork.c init.c \
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c usilic.c vt.c
	@grep '\/\*usilic\*\/' alloc.c child.c code.c disp.c fork.c init.c \
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c usilic.c vt.c -h|\
	sed -e 's/\/\*usilic\*\//;/' >$@

$(OBJS): mode.h

menu.o mode.o: menu.h

usilic.o:	version.h

alloc.o: alloc.h

debug:
	mkdir -p debug
	/bin/ls Makefile *.[ch]|xargs -t ../perl/debug.pl debug
	ln -sf ../config.h debug

clean:
	rm -rf U $(OBJS) $(PROT) *~ debug
