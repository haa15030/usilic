installprefix=$(ROOT)

default: src/tcb

all: src/tcb

src/tcb: src/config.h
	@echo 'Making tcb:'
	@cd src; make; cd ..

src/config.h: config.sh
	@./config.sh $(USR_DIR)	# $(USR_DIR): Debian (Linux)

install: src/tcb doc/tcb.1.gz
	@./install.sh $(installprefix)	# $(installprefix): RPM (Linux)

doc/tcb.1.gz: doc/tcb.1
	gzip -c doc/tcb.1 > doc/tcb.1.gz

clean:
	rm -f .config install.sh doc/tcb.1.gz
	rm -f src/tcb src/*.[oP] src/*~ src/config.h src/Makefile*
	rm -rf src/debug
