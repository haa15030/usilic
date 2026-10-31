installprefix=$(ROOT)

# usilic.old: できるものは元は src/usilic(C++ 版の usilic と紛らわしいので改名)
default: src/usilic.old

all: src/usilic.old	# usilic.old: 元は src/usilic

# usilic.old: .PHONY にして、毎回 src/ の make へ降りる(作り直すかどうかは src/ の
# Makefile が .c と .o の新しさで決める)。無いと、src/config.h より新しい
# src/usilic.old があるだけで出来ているとみなされ、src/*.c を直しても作り直されなかった
.PHONY: src/usilic.old
src/usilic.old: src/config.h
	@echo 'Making usilic.old:'
	@cd src; make; cd ..

src/config.h: config.sh
	@./config.sh $(USR_DIR)	# $(USR_DIR): Debian (Linux)

install: src/usilic.old doc/usilic.1.gz	# usilic.old: 元は src/usilic
	@./install.sh $(installprefix)	# $(installprefix): RPM (Linux)

doc/usilic.1.gz: doc/usilic.1
	gzip -c doc/usilic.1 > doc/usilic.1.gz

# usilic.old: src/usilic は、できるものの前の名前(残っていれば消す)
clean:
	rm -f .config install.sh doc/usilic.1.gz
	rm -f src/usilic src/usilic.old src/*.[oP] src/*~ src/config.h src/Makefile*
	rm -rf src/debug
