#!/bin/bash

# Program name. Drives the installed binary name, the ~/.$PROGNAME and
# /tmp/.$PROGNAME runtime directories, the $PROGNAME/${PROGNAME}rc config
# file, and the (uppercased) environment variable names ($PROGNAME,
# ${PROGNAME}CHARSET, ${PROGNAME}VT). Source files/Makefile build targets
# say "usilic" internally now (renamed 2026-08-30, formerly "tcb") --
# this variable only controls the RUNTIME-visible name layered on top.
#
# 2026-08-25: "U" (not "usilic") -- a direct user request to install
# this legacy build ALONGSIDE the C++ port (src/cpp/), which now owns
# the "usilic" name (see its own Makefile's INSTALL_NAME). Renaming
# PROGNAME here is the single point that keeps everything consistent:
# the installed binary becomes $PREFIX/bin/U, its own rc file
# $PREFIX/lib/U/Urc, its runtime dirs ~/.U//tmp/.U, and its env vars
# UCHARSET/UVT/U (previously USILICCHARSET/USILICVT/USILIC) -- all
# still "this same legacy/usilic program", just addressed as "U" now
# that a newer implementation has taken over the primary name.
PROGNAME=U

# 2026-08-25 (a direct user request): a single PREFIX controls every
# installed path below (BIN/LIB/MAN, and this script's own generated
# install.sh) -- override by exporting PREFIX before running this
# script, e.g. `PREFIX=/usr/local ./config.sh` for the traditional
# system-wide location (needs sudo for the install.sh step then).
# Defaults to ~/.local (XDG-ish per-user install, no sudo needed) --
# see src/cpp/Makefile's own PREFIX for the C++ port's identical
# convention. Baked into config.h's compile-time LIB constant below,
# so the BUILT BINARY's own rc/HELP fallback lookup only matches
# reality if install.sh (generated further down) is later run with
# this SAME PREFIX -- the generated script's own default already
# matches this run's PREFIX for exactly that reason; only an
# install.sh-time override needs to stay consistent with whatever
# PREFIX config.sh itself last used.
PREFIX=${PREFIX:-$HOME/.local}

# This script lives at src/config.sh; the legacy C source it configures
# was archived to old/src/ (a sibling of src/, named tcb/src/ until
# the 2026-08-30 tcb->usilic rename) when the repo was restructured
# around the claude-code-book chapter8 spec-driven-dev scaffold. Run
# this from src/ (its own directory).
CSRC=../old/src

# Working directory.
TMP=/tmp/.$PROGNAME

# Default character set.
CHARSET="utf-8"

# Command line for DU mode.
DU="du -ab"

# CP/MV/RM mode.
CP="cp -vRi"
MV="mv -vi"
RM="rm -vri"

echo "Creating $CSRC/Makefile:"
if [ `uname` = Linux ]; then	# Linux
#    MAN=/usr/man/man1
#    if [ ! "$USR_DIR" = "" ]; then 
#	JMAN=/usr/man/ja/man1
#    else
#	JMAN=/usr/man/ja_JP.ujis/man1
#    fi
    USR_DIR=$PREFIX
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
	    USR_DIR=$PREFIX
	    PERM='-m 644'
	    echo '# Makefile for macOS (Darwin) version' > .config
	    echo 'LIBS=	-lncurses'>> .config
	    # macOS's own (BSD) du has no -b (GNU coreutils-only "apparent
	    # size in exact bytes" flag) -- "du -ab" fails outright there
	    # ("du: invalid option -- b"), a real user report. -A (apparent
	    # size instead of disk-usage block count) is the closest BSD
	    # equivalent available; -B 1 (byte-sized blocks) was tried
	    # first to also match -b's BYTE-exact precision, but this du
	    # implementation doesn't honor -B while -A is also set (both
	    # `du -A -B 1` and `du -A` alone gave the identical, still
	    # 512-byte-block-rounded number on a real file -- confirmed by
	    # direct testing, not just documentation). DU mode's own output
	    # is displayed as opaque text (alloc_pipe(), the same generic
	    # external-command-output path `ls`'s own output goes through)
	    # rather than parsed/summed by usilic itself, so exact byte parity
	    # with Linux was never actually required -- block-granular
	    # sizes are a fine, honest platform difference, not a broken
	    # feature.
	    DU="du -a -A"
	else
	if [ `uname` = CYGWIN_98-4.10 ]; then	# cygwin
	    USR_DIR=$PREFIX
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

LIB=$USR_DIR/lib/$PROGNAME
BIN=$USR_DIR/bin
MAN=$USR_DIR/man/man1

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

default: $PROGNAME

$PROGNAME:	\$(PROT) \$(OBJS)
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
	rm -rf $PROGNAME \$(OBJS) \$(PROT) *~ debug
EOF

if [ -f $CSRC/Makefile ]; then
    cmp -s .config $CSRC/Makefile
    if [ ! $? = 0 ]; then
	mv $CSRC/Makefile $CSRC/Makefile.old
	mv .config $CSRC/Makefile
    else
	rm .config
	touch $CSRC/Makefile
    fi
else
    mv .config $CSRC/Makefile
fi

echo "Creating $CSRC/config.h:"

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

if [ -f $CSRC/config.h ]; then
    cmp -s .config $CSRC/config.h
    if [ ! $? = 0 ]; then
	mv $CSRC/config.h $CSRC/config.h.old
	mv .config $CSRC/config.h
    else
	rm .config
	touch $CSRC/config.h
    fi
else
    mv .config $CSRC/config.h
fi

echo "Creating $CSRC/progname.h:"

PROGNAME_UPPER=`echo "$PROGNAME" | tr '[:lower:]' '[:upper:]'`
cat > .config <<EOF
#ifndef _PROGNAME_H
#define _PROGNAME_H
#define PROGNAME "$PROGNAME"
#define PROGNAME_UPPER "$PROGNAME_UPPER"
#define ENV_PROG "$PROGNAME_UPPER"
#define ENV_CHARSET "${PROGNAME_UPPER}CHARSET"
#define ENV_VT "${PROGNAME_UPPER}VT"
#endif
EOF

if [ -f $CSRC/progname.h ]; then
    cmp -s .config $CSRC/progname.h
    if [ ! $? = 0 ]; then
	mv $CSRC/progname.h $CSRC/progname.h.old
	mv .config $CSRC/progname.h
    else
	rm .config
	touch $CSRC/progname.h
    fi
else
    mv .config $CSRC/progname.h
fi

cat > install.sh <<EOF
# 2026-08-25: PREFIX (a shell variable, not a positional \$1 argument
# prepended to already-resolved paths, per a direct user request)
# defaults to THIS run's own $PREFIX -- the exact value config.sh just
# baked into the BUILT BINARY's own compile-time rc/HELP fallback path
# (config.h's LIB constant), so running this script with no override
# always matches what the binary itself will look for. Override with
# e.g. \`PREFIX=/usr/local ./install.sh\` only if you ALSO re-ran
# config.sh with that same PREFIX first -- an install.sh-only override
# relocates where files are copied to, not where the already-built
# binary looks for them.
PREFIX=\${PREFIX:-$PREFIX}
cd $CSRC; make; cd -
echo 'Installing $PROGNAME:'
mkdir -p \$PREFIX/bin
mkdir -p \$PREFIX/lib/$PROGNAME
mkdir -p \$PREFIX/man/man1
echo "install $COPY -m 755 $CSRC/$PROGNAME \$PREFIX/bin/$PROGNAME"
install $COPY -m 755 $CSRC/$PROGNAME \$PREFIX/bin/$PROGNAME
echo "install $COPY -m 644 ${PROGNAME}rc \$PREFIX/lib/$PROGNAME"
install $COPY -m 644 ${PROGNAME}rc \$PREFIX/lib/$PROGNAME
echo "install $COPY -m 644 doc/HELP \$PREFIX/lib/$PROGNAME"
install $COPY -m 644 doc/HELP \$PREFIX/lib/$PROGNAME
echo "install $COPY -m 644 doc/HELP.jp \$PREFIX/lib/$PROGNAME"
install $COPY -m 644 doc/HELP.jp \$PREFIX/lib/$PROGNAME
echo "install $COPY $PERM doc/usilic.1.gz \$PREFIX/man/man1"
install $COPY $PERM doc/usilic.1.gz \$PREFIX/man/man1
EOF
if [ -n "$JMAN" ] && [ -d "$JMAN" ]; then
cat >> install.sh <<EOF
mkdir -p \$PREFIX/$JMAN
echo "install $COPY $PERM doc/usilic.1.gz \$PREFIX/$JMAN"
install $COPY $PERM doc/usilic.1.gz \$PREFIX/$JMAN
EOF
fi

chmod 755 install.sh

