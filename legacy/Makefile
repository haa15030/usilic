installprefix=$(ROOT)

default: src/usilic

all: src/usilic

src/usilic: src/config.h
	@echo 'Making usilic:'
	@cd src; make; cd ..

src/config.h: config.sh
	@./config.sh $(USR_DIR)	# $(USR_DIR): Debian (Linux)

install: src/usilic doc/usilic.1.gz
	@./install.sh $(installprefix)	# $(installprefix): RPM (Linux)

doc/usilic.1.gz: doc/usilic.1
	gzip -c doc/usilic.1 > doc/usilic.1.gz

clean:
	rm -f .config install.sh doc/usilic.1.gz
	rm -f src/usilic src/*.[oP] src/*~ src/config.h src/Makefile*
	rm -rf src/debug
