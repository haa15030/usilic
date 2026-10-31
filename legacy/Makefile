installprefix=$(ROOT)

default: src/usilic.old

all: src/usilic.old

src/usilic.old: src/config.h
	@echo 'Making usilic.old:'
	@cd src; make; cd ..

src/config.h: config.sh
	@./config.sh $(USR_DIR)	# $(USR_DIR): Debian (Linux)

install: src/usilic.old doc/usilic.1.gz
	@./install.sh $(installprefix)	# $(installprefix): RPM (Linux)

doc/usilic.1.gz: doc/usilic.1
	gzip -c doc/usilic.1 > doc/usilic.1.gz

clean:
	rm -f .config install.sh doc/usilic.1.gz
	rm -f src/usilic src/usilic.old src/*.[oP] src/*~ src/config.h src/Makefile*
	rm -rf src/debug
