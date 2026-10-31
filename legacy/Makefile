installprefix=$(ROOT)

# usilic.old: the binary was src/usilic (renamed so it isn't mistaken for the new C++ usilic)
default: src/usilic.old

all: src/usilic.old	# usilic.old: was src/usilic

# usilic.old: .PHONY so that make always goes down to src/, whose own Makefile
# rebuilds whatever .c changed (without it, an existing src/usilic.old newer
# than src/config.h was taken as up to date and edits to src/*.c were ignored)
.PHONY: src/usilic.old
src/usilic.old: src/config.h
	@echo 'Making usilic.old:'
	@cd src; make; cd ..

src/config.h: config.sh
	@./config.sh $(USR_DIR)	# $(USR_DIR): Debian (Linux)

install: src/usilic.old doc/usilic.1.gz	# usilic.old: was src/usilic
	@./install.sh $(installprefix)	# $(installprefix): RPM (Linux)

doc/usilic.1.gz: doc/usilic.1
	gzip -c doc/usilic.1 > doc/usilic.1.gz

clean:
	rm -f .config install.sh doc/usilic.1.gz
	# usilic.old: src/usilic is the binary's former name
	rm -f src/usilic src/usilic.old src/*.[oP] src/*~ src/config.h src/Makefile*
	rm -rf src/debug
