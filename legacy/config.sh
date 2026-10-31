#!/bin/bash

# Working directory.
TMP=/tmp/.usilic.old	# usilic.old: was /tmp/.usilic

# Default character set.
CHARSET="euc"

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
#    USR_DIR=/usr
#    USR_DIR=/usr/local
    USR_DIR=/tmp/usr/local
    MAN=/tmp/usr/man/man1
    PERM='-m 644'
    echo '# Makefile for Linux version' > .config
    echo 'LIBS=	-lncurses'>> .config
else
    if [ `uname` = FreeBSD ]; then	# FreeBSD
        patch -p0 < doc/patch.bsd
        make install
        exit 0
    else
	# usilic.old: macOS (Darwin) wasn't supported (f1ca2a8)
	if [ `uname` = Darwin ]; then	# macOS (f1ca2a8)
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
	fi	# usilic.old: closes the Darwin branch added above
    fi
fi

# usilic.old: The new C++ usilic owns ~/.usilic/ and ~/.local/{bin,lib}/usilic, so this
# legacy version is installed side by side as usilic.old: ~/.local/bin/usilic.old,
# ~/.local/lib/usilic.old/ (usilic.oldrc, HELP), its data in ~/.usilic.old/.
USR_DIR=$HOME/.local
LIB=$USR_DIR/lib/usilic.old
BIN=$USR_DIR/bin
MAN=$USR_DIR/share/man/man1	# usilic.old: was $USR_DIR/man/man1

cat >> .config <<EOF
CC=gcc	
CFLAGS= -Wall -O3 -g 
#CFLAGS= -Wall -O3
LDFLAGS= 
#LDFLAGS= -s
OBJS=	alloc.o child.o code.o disp.o fork.o init.o \\
    	key.o menu.o mode.o scroll.o shell.o str.o sub.o tc.o usilic.o vt.o
EOF

cat >> .config <<EOF

PROT=	code.P mode.P tc.P usilic.P

.SUFFIXES: .P
.c.P:
	@grep -h '\/\*\$*\*\/' \$< |sed -e 's/\/\*\$*\*\//;/' >\$@
.c.o:
	\$(CC) -c \$(CFLAGS) \$<

# usilic.old: the binary was usilic
default: usilic.old

usilic.old:	\$(PROT) \$(OBJS)
	\$(CC) \$(LDFLAGS) -o \$@  \$(OBJS) \$(LIBS)

EOF

if which gmake > /dev/null; then
cat >> .config <<EOF
usilic.P:	\${subst .o,.c,\$(OBJS)}
	@grep -h '\/\*usilic\*\/' \${subst .o,.c,\$(OBJS)}|\\
	sed -e 's/\/\*usilic\*\//;/' >\$@
EOF
else
cat >> .config <<EOF
usilic.P:	alloc.c child.c code.c disp.c fork.c init.c \\
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c usilic.c vt.c
	@grep '\/\*usilic\*\/' alloc.c child.c code.c disp.c fork.c init.c \\
    	key.c menu.c mode.c scroll.c shell.c str.c sub.c tc.c usilic.c vt.c -h|\\
	sed -e 's/\/\*usilic\*\//;/' >\$@
EOF
fi

cat >> .config <<EOF

\$(OBJS): mode.h

menu.o mode.o: menu.h

usilic.o:	version.h

alloc.o: alloc.h

debug:
	mkdir -p debug
	/bin/ls Makefile *.[ch]|xargs -t ../perl/debug.pl debug
	ln -sf ../config.h debug

clean:
	# usilic.old: was usilic
	rm -rf usilic.old \$(OBJS) \$(PROT) *~ debug
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
echo 'Installing usilic:'
mkdir -p \$1$BIN
mkdir -p \$1$LIB
mkdir -p \$1$MAN
# usilic.old: installed as usilic.old, usilic.oldrc and usilic.old.1.gz (were usilic, usilicrc, usilic.1.gz)
echo "install $COPY -m 755 src/usilic.old \$1$BIN/usilic.old"
install $COPY -m 755 src/usilic.old \$1$BIN/usilic.old
echo "install $COPY -m 644 usilicrc \$1$LIB/usilic.oldrc"
install $COPY -m 644 usilicrc \$1$LIB/usilic.oldrc
echo "install $COPY -m 644 doc/HELP \$1$LIB"
install $COPY -m 644 doc/HELP \$1$LIB
echo "install $COPY -m 644 doc/HELP.jp \$1$LIB"
install $COPY -m 644 doc/HELP.jp \$1$LIB
# usilic.old: was \$1$MAN (installed as usilic.1.gz)
echo "install $COPY $PERM doc/usilic.1.gz \$1$MAN/usilic.old.1.gz"
install $COPY $PERM doc/usilic.1.gz \$1$MAN/usilic.old.1.gz
EOF
# usilic.old: was [ -d $JMAN ], true for an empty JMAN on macOS (0b5a0df)
if [ -n "$JMAN" ] && [ -d "$JMAN" ]; then
cat >> install.sh <<EOF
mkdir -p \$1$JMAN
echo "install $COPY $PERM doc/usilic.1.gz \$1$JMAN"
install $COPY $PERM doc/usilic.1.gz \$1$JMAN
EOF
fi

chmod 755 install.sh

