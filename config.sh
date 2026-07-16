#!/bin/bash

# Working directory.
TMP=/tmp/.tcb

# Default character set.
CHARSET="utf-8"

# Command line for DU mode.
DU="du -ab"

# CP/MV/RM mode.
CP="cp -vRi"
MV="mv -vi"
RM="rm -vri"

echo 'Creating src/Makefile:'
if [ `uname` = Linux ]; then	# Linux
#    MAN=/usr/man/man1
#    if [ ! "$USR_DIR" = "" ]; then 
#	JMAN=/usr/man/ja/man1
#    else
#	JMAN=/usr/man/ja_JP.ujis/man1
#    fi
    USR_DIR=/usr
    USR_DIR=/usr/local
#    USR_DIR=/tmp/usr/local
#    MAN=/tmp/usr/man/man1
    PERM='-m 644'
    echo '# Makefile for Linux version' > .config
    echo 'LIBS=	-lncurses'>> .config
else
    if [ `uname` = FreeBSD ]; then	# FreeBSD
        patch -p0 < doc/patch.bsd
        make install
        exit 0
    else
	if [ `uname` = Darwin ]; then	# macOS
	    USR_DIR=/usr/local
	    MAN=/usr/local/share/man/man1
	    PERM='-m 644'
	    echo '# Makefile for macOS (Darwin) version' > .config
	    echo 'LIBS=	-lncurses'>> .config
	else
	if [ `uname` = CYGWIN_98-4.10 ]; then	# cygwin
	    USR_DIR=/usr/local
	    MAN=/usr/man/man1
	    PERM='-m 644'
	    echo 'LIBS= -lncurses'>> .config
	else
            echo 'Not supported yet "'`uname`'".'
            exit 1
	fi
	fi
    fi
fi

LIB=$USR_DIR/lib/tcb
BIN=$USR_DIR/bin
MAN=$USR_DIR/man/man1

cat >> .config <<EOF
CC=gcc	
CFLAGS= -Wall -O3 -g 
#CFLAGS= -Wall -O3
LDFLAGS= 
#LDFLAGS= -s
OBJS=	alloc.o child.o code.o disp.o fork.o init.o \\
    	key.o menu.o mode.o scroll.o shell.o str.o sub.o tc.o tcb.o vt.o
EOF

cat >> .config <<EOF

PROT=	code.P mode.P tc.P tcb.P

.SUFFIXES: .P
.c.P:
	@grep -h '\/\*\$*\*\/' \$< |sed -e 's/\/\*\$*\*\//;/' >\$@
.c.o:
	\$(CC) -c \$(CFLAGS) \$<

default: tcb

tcb:	\$(PROT) \$(OBJS)
	\$(CC) \$(LDFLAGS) -o \$@  \$(OBJS) \$(LIBS)

EOF

if which gmake > /dev/null; then
cat >> .config <<EOF
tcb.P:	\${subst .o,.c,\$(OBJS)}
	@grep -h '\/\*tcb\*\/' \${subst .o,.c,\$(OBJS)}|\\
	sed -e 's/\/\*tcb\*\//;/' >\$@
EOF
else
cat >> .config <<EOF
tcb.P:	alloc.c child.c code.c disp.c fork.c init.c \\
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c tcb.c vt.c
	@grep '\/\*tcb\*\/' alloc.c child.c code.c disp.c fork.c init.c \\
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c tcb.c vt.c -h|\\
	sed -e 's/\/\*tcb\*\//;/' >\$@
EOF
fi

cat >> .config <<EOF

\$(OBJS): mode.h

menu.o mode.o: menu.h

tcb.o:	version.h

alloc.o: alloc.h

debug:
	mkdir -p debug
	/bin/ls Makefile *.[ch]|xargs -t ../perl/debug.pl debug
	ln -sf ../config.h debug

clean:
	rm -rf tcb \$(OBJS) \$(PROT) *~ debug
EOF

if [ -f src/Makefile ]; then
    cmp -s .config src/Makefile
    if [ ! $? = 0 ]; then
	mv src/Makefile src/Makefile.old    
	mv .config src/Makefile
    else
	rm .config
	touch src/Makefile
    fi
else
    mv .config src/Makefile
fi 

echo 'Creating src/config.h:'

cat > .config <<EOF
#ifndef _CONFIG_H
static char TMP[] = "$TMP";
static char CHARSET[] = "$CHARSET";
static char DU[] = "$DU";
static char CP[] = "$CP";
static char MV[] = "$MV";
static char RM[] = "$RM";
static char LIB[] = "$LIB";
#endif _CONFIG_H
#define _CONFIG_H
EOF

if [ -f src/config.h ]; then
    cmp -s .config src/config.h
    if [ ! $? = 0 ]; then
	mv src/config.h src/config.h.old    
	mv .config src/config.h
    else
	rm .config
	touch src/config.h
    fi
else
    mv .config src/config.h
fi 

cat > install.sh <<EOF
cd src; make; cd ..	
echo 'Installing tcb:'
mkdir -p \$1$BIN
mkdir -p \$1$LIB
mkdir -p \$1$MAN
echo "install $COPY -m 755 src/tcb \$1$BIN"
install $COPY -m 755 src/tcb \$1$BIN
echo "install $COPY -m 644 tcbrc \$1$LIB"
install $COPY -m 644 tcbrc \$1$LIB
echo "install $COPY -m 644 doc/HELP \$1$LIB"
install $COPY -m 644 doc/HELP \$1$LIB
echo "install $COPY -m 644 doc/HELP.jp \$1$LIB"
install $COPY -m 644 doc/HELP.jp \$1$LIB
echo "install $COPY $PERM doc/tcb.1.gz \$1$MAN"
install $COPY $PERM doc/tcb.1.gz \$1$MAN
EOF
if [ -n "$JMAN" ] && [ -d "$JMAN" ]; then
cat >> install.sh <<EOF
mkdir -p \$1$JMAN
echo "install $COPY $PERM doc/tcb.1.gz \$1$JMAN"
install $COPY $PERM doc/tcb.1.gz \$1$JMAN
EOF
fi

chmod 755 install.sh

