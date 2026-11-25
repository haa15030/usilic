# 2026-10-15、直接のユーザー依頼("Bを-gで始めて"、.steering/20261015-windows-native-gui/): `make WINDOWS=1 usilic`は
# MinGW-w64でWindowsのネイティブのusilic.exe(-gだけ)を作る。設定はwindows.mk。それ以外はこのファイルの残り。
ifeq ($(WINDOWS),1)
include windows.mk
else
CXX      ?= c++
# graphical_display.cpp (the SDL2/SDL2_ttf-backed GUI-mode Display,
# see docs/ideas/IDEAS.md) needs both SDL2 and SDL2_ttf's headers/libs
# -- resolved via pkg-config rather than hardcoded paths so this works
# on both Homebrew (Mac) and apt (Ubuntu) layouts unchanged. A missing
# pkg-config or missing packages makes these expand to empty strings
# (2>/dev/null swallows the "package not found" error) rather than
# failing the Makefile itself -- the actual compile error surfaces
# from the missing SDL2/SDL_ttf.h includes instead, same as before
# this comment existed for anyone building without them installed.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf 2>/dev/null)
# -Wextra's -Wmissing-field-initializers fights an established idiom in
# this codebase: ContentView::Line/ModeLineFields etc. are aggregates
# that grow trailing optional fields over time (see content_view.hpp's
# own field comments -- "他の全ての呼び出し元はデフォルトのままにする"),
# and existing call sites are meant to keep omitting new trailing fields
# rather than being revisited every time one is added. Disabled globally
# instead of patched at each of the 100+ call sites this warning fires on.
# -O2(2026-09-25追加、直接のユーザー依頼): それまでは最適化無し(-gのみ)で
# ビルドしており、92MB/100万行のファイルの^Mに約10.6秒かかっていた。-gは
# デバッガ用にそのまま残す。
# OPTFLAGS(2026-10-05): CI(.github/workflows/build.yml)は-O0でビルド時間を縮める(`make OPTFLAGS=-O0`)。
OPTFLAGS ?= -O2
CXXFLAGS ?= -std=c++17 $(OPTFLAGS) -Wall -Wextra -Wno-missing-field-initializers -g $(SDL_CFLAGS)
# terminal_display.cpp uses ncurses' terminfo API (setupterm/tputs/
# tgoto), same as the legacy C's config.sh (LIBS= -lncurses).
# mac_font_resolver.cpp's CoreText/CoreFoundation use is #ifdef
# __APPLE__-guarded (a no-op stub elsewhere), but the frameworks still
# need linking on macOS specifically -- `-framework` isn't a flag
# Linux's linker understands, so this is gated on uname rather than
# added unconditionally.
LDLIBS   ?= -lncurses $(SDL_LIBS)
ifeq ($(shell uname), Darwin)
LDLIBS += -framework CoreText -framework CoreFoundation -framework Cocoa -framework Carbon
# 2026-08-24 fix (docs/CHANGES.md item 419): a real user report on an
# Intel Mac (Sequoia, fresh `brew install sdl2 sdl2_ttf`) -- `ld`
# reported every `TTF_*` symbol from SDL2_ttf as "not found for
# architecture x86_64" despite the library itself being confirmed
# x86_64 (`file`/`lipo -info`), correctly on the link line at the
# right path (`nm -gU` confirmed `_TTF_Init` genuinely exported from
# exactly that .dylib), and the compile succeeding fine beforehand
# (only SDL2_ttf's OWN symbols were missing, not SDL2's). This matches
# a known Xcode 15+ "ld-prime" (the new default linker)
# incompatibility with certain Homebrew dylib chains -- confirmed by
# reproducing the exact failure, then having the user re-link the
# EXACT SAME command with `-Wl,-ld_classic` appended, which fixed it
# immediately. Verified NOT to regress Apple Silicon (this port's own
# primary dev machine): a full clean rebuild with `-ld_classic` added
# still linked successfully (`ld` only emits its own "-ld_classic is
# deprecated" warning, not an error) -- so this is added
# unconditionally for Darwin rather than gated to Intel specifically,
# since there's no cheap, reliable way to detect "this machine's Xcode
# actually needs the workaround" short of trying the link and seeing
# if it fails. `-ld_classic` has no Linux equivalent (that OS was
# never affected), hence staying inside this ifeq block. NOTE: Apple's
# own linker already warns `-ld_classic` will be removed in a future
# Xcode release -- if that happens, revisit whether the underlying
# Homebrew/ld-prime incompatibility has been fixed upstream by then.
# 2026-09-25: that removal has now happened -- a real user report
# from a MacBook with a newer Xcode, where `-ld_classic` made the link
# fail outright (the user had to delete this line by hand). Probe the
# linker once and only add the flag when it's still accepted.
LD_CLASSIC_OK := $(shell printf 'int main(){return 0;}' | $(CXX) -x c++ - -Wl,-ld_classic -o /dev/null >/dev/null 2>&1 && echo yes)
ifeq ($(LD_CLASSIC_OK),yes)
LDLIBS += -Wl,-ld_classic
endif
endif

SRC_DIR   = src
APP_DIR   = app
TEST_DIR  = tests
BUILD_DIR = build

# every .cpp under src/ becomes part of both the real build and the
# test build, so tests always compile against the real classes.
# main.cpp lives outside src/ (in app/) specifically so it never joins
# this wildcard -- run_tests already gets its own main() from
# tests/test_main.cpp (doctest's), and two main()s would conflict.
SRCS     = $(wildcard $(SRC_DIR)/*.cpp)
# 2026-10-31、直接のユーザー報告(FreeBSDで、application.hppだけが変わったgit pullの後のgmake usilicが
# "Nothing to be done"): 下の規則は.cppだけを前提にしていて、ヘッダだけの変更では作り直されなかった。ヘッダも。
HDRS     = $(wildcard $(SRC_DIR)/*.hpp $(SRC_DIR)/*.h $(APP_DIR)/*.hpp)
# *_win.cppはWindowsのネイティブ(windows.mk)だけのもの。
SRCS    := $(filter-out $(SRC_DIR)/%_win.cpp, $(SRCS))
# mac_cursor.mm/mac_cursor_stub.cpp (2026-08-10, the real NSCursor.openHand
# for content dragging -- see mac_cursor.hpp's own class comment) are
# mutually exclusive, same-symbols implementations, picked by platform
# exactly like mac_scrollbar.mm/mac_scrollbar_stub.cpp's own established
# pattern: the plain C++ stub is what $(wildcard) already picked up in
# SRCS by default (works unchanged on Linux), swapped out for the real
# Objective-C++ (.mm) implementation on Darwin.
ifeq ($(shell uname), Darwin)
SRCS := $(filter-out $(SRC_DIR)/mac_cursor_stub.cpp, $(SRCS)) $(SRC_DIR)/mac_cursor.mm
endif
# GUI無しのビルド(2026-10-05、.steering/20261005-windows-gitbash/): SDL2がpkg-configで見つからない
# 時(MSYS2のMSYS環境等)、または`make NO_GUI=1`の時は、graphical_display.cppの代わりに空の定義の
# graphical_display_stub.cpp(tools/gen_graphical_display_stub.pyが作る)をビルドし、SDLをリンク
# しない。-gは使えない(main.cppのUSILIC_NO_GUI参照)。
ifeq ($(strip $(SDL_LIBS)),)
NO_GUI := 1
endif
ifeq ($(NO_GUI),1)
SRCS := $(filter-out $(SRC_DIR)/graphical_display.cpp, $(SRCS))
SDL_CFLAGS :=
SDL_LIBS :=
CXXFLAGS += -DUSILIC_NO_GUI
else
SRCS := $(filter-out $(SRC_DIR)/graphical_display_stub.cpp, $(SRCS))
endif
# MSYS2/Cygwin(2026-10-05): Cygwin系のヘッダは厳密なISOの-std=c++17だとPOSIXの関数の一部を
# 隠すのでgnu++17に。libstdc++/libgccは静的にリンクする(MSYS2でビルドしたexeを、MSYS2の
# libstdc++のDLLの無いGit Bashでも動かすため)。
UNAME_S := $(shell uname -s)
ifneq ($(filter MSYS_% CYGWIN_%,$(UNAME_S)),)
CXXFLAGS := $(subst -std=c++17,-std=gnu++17,$(CXXFLAGS))
LDLIBS += -static-libstdc++ -static-libgcc
# MSYS2のncurses-develはワイド文字版(ncursesw)で、ヘッダは/usr/include/ncursesw/の下。
NCURSESW_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null)
NCURSESW_LIBS   := $(shell pkg-config --libs ncursesw 2>/dev/null)
ifneq ($(strip $(NCURSESW_LIBS)),)
CXXFLAGS += $(NCURSESW_CFLAGS)
LDLIBS := $(subst -lncurses,$(NCURSESW_LIBS),$(LDLIBS))
endif
endif
APP_SRCS = $(wildcard $(APP_DIR)/*.cpp)
TOOLS_DIR = tools
TESTS    = $(wildcard $(TEST_DIR)/*.cpp)

# 2026-08-25 (a direct user request, reversing the roles this comment
# used to describe): this C++ port now owns the "usilic" name outright
# -- the legacy C build installs as "U" instead (src/config.sh's own
# PROGNAME=U, see its comment), freeing "usilic" up here. PREFIX
# defaults to ~/.local (no sudo needed; already on PATH ahead of
# /usr/local/bin on this machine) -- override for the traditional
# system-wide location with e.g. `make install PREFIX=/usr/local`
# (needs sudo then). Mirrors src/config.sh's own identical PREFIX
# convention for the legacy build, including the SAME
# $(PREFIX)/lib/<name>/<name>rc layout (usilicrc here, Urc there) --
# see Application's own usilicrcPath_ fallback (application.cpp) for
# where this installed copy actually gets read from at runtime.
PREFIX      ?= $(HOME)/.local
INSTALL_NAME = usilic
# 2026-11-25(make deb): DESTDIRは入れる先の頭に付けるだけ(.desktopに書くパスには付けない)。
DESTDIR     ?=
INSTALL_DIR  = $(DESTDIR)$(PREFIX)/bin
LIB_DIR      = $(DESTDIR)$(PREFIX)/lib/usilic

.PHONY: test usilic install clean app help-epub help-epub-en help-man help harness

test: $(BUILD_DIR)/run_tests
	$(BUILD_DIR)/run_tests

usilic: $(BUILD_DIR)/usilic

# 2026-08-30、直接のユーザー依頼(HELP.jp本体をソースへ統合): HELP/
# HELP.jpはもはやsrc/help_text.hpp埋め込みテキストがデフォルトなので、
# $(LIB_DIR)へのインストールは不要になった(main.cpp自身のコメント
# 参照――$USILICHELPが設定されている場合だけ、指定されたファイルを
# そのまま読む)。src/doc/HELP(.jp)自体はレガシー版(old/、
# src/config.shが生成する別のmake installが今も使う)専用として残して
# あり、このMakefileはそちらには一切触れない。
# 2026-09-14追加、直接のユーザー依頼("README改めTUTORIALとして...
# ~/.local/lib/usilic/TUTORIAL...が登録済み"): src/TUTORIAL(旧
# src/README.jp、legacy自身のsrc/doc/HELP.jpから昇格した本物のnroff
# 重ね打ちバイト列)を、このポート自身のFILE履歴の「真っ新起動時の
# 事前登録エントリ」が指す先としてインストールする――
# Application::seedFreshFileHistory()自身のコメント参照。TUTORIAL.en
# (英語版)は今のところこのポート自身のランタイムからは参照されない
# (GitHub上のリポジトリ説明用の資料として残すのみ)ので、意図的に
# インストールしない。
# 2026-10-18、直接のユーザー依頼("doc/って開発中のディレクトリなのでそこからHELPを作成は明らかにおかしい"):
# HELPのEPUB版は開発の手順で作ってdoc/HELP.epubに置き、make installはそれを入れるだけ。原稿(HELP_TXT)と、
# 注釈に使うリポジトリのusilicrc・doc/HELP.notesから作る。注釈の無い斜体の名前は"usilic -e: no note:"に出る。
# 表紙はusilic.appのアイコンを256ピクセルに縮めたもの(packaging/usilic_icon_256.png、2026-10-20)。
HELP_TXT ?= $(HOME)/HELP.txt
help-epub: $(BUILD_DIR)/usilic
	USILIC_EPUB_RC=../usilicrc USILIC_EPUB_NOTES=../../doc/HELP.notes USILIC_EPUB_COVER=packaging/usilic_icon_256.png \
	  $(BUILD_DIR)/usilic -e $(HELP_TXT) > ../../doc/HELP.epub
	# 2026-10-29: ^HのHELPはusilicがHELP.1から作る(Application::helpEpubPath())ので、doc/HELP.epubは確かめ用で入れない。
# 2026-10-19、直接のユーザー依頼(^Ojで英語にした^HのHELPが古い平文だった): 英語版HELP.en.epub(^Ojの後の^H)。原稿は
# src/HELP_en_.txtからユーザーのスクリプトで作る~/HELP_en.txt、注釈はusilicrc.enのコメントとdoc/HELP.en.notes。
HELP_EN_TXT ?= $(HOME)/HELP_en.txt
help-epub-en: $(BUILD_DIR)/usilic
	USILIC_EPUB_RC=../usilicrc.en USILIC_EPUB_NOTES=../../doc/HELP.en.notes USILIC_EPUB_COVER=packaging/usilic_icon_256.png \
	  $(BUILD_DIR)/usilic -e $(HELP_EN_TXT) > ../../doc/HELP.en.epub
# 2026-10-26、直接のユーザー依頼(~/HELP.txt・~/HELP_en.txtを直したら、make 1回でHELPを全部最新に): -H・^T^Hの子が
# 開く平文のHELP(HELP.1、英語はHELP_en.1)もusilic -nで原稿から作って入れる(以前は手で作ってdoc/に置いていた)。
help-man: $(BUILD_DIR)/usilic
	# 2026-10-29: 注釈の元のHELP.notes・usilicrc(英語はHELP.en.notes・usilicrc.en)と表紙の画像もHELP.1へroffの
	# コメントで埋め込む(usilic -e HELP.1だけで表紙・注釈まで同じHELP.epubになる)。
	USILIC_EPUB_RC=../usilicrc USILIC_EPUB_NOTES=../../doc/HELP.notes USILIC_EPUB_COVER=packaging/usilic_icon_256.png \
	  $(BUILD_DIR)/usilic -n $(HELP_TXT) > ../../doc/HELP.1
	USILIC_EPUB_RC=../usilicrc.en USILIC_EPUB_NOTES=../../doc/HELP.en.notes USILIC_EPUB_COVER=packaging/usilic_icon_256.png \
	  $(BUILD_DIR)/usilic -n $(HELP_EN_TXT) > ../../doc/HELP_en.1
	install -d $(LIB_DIR) && install -m 644 ../../doc/HELP.1 ../../doc/HELP_en.1 $(LIB_DIR)/
# make help: HELPの4つ(HELP.epub・HELP.en.epub・HELP.1・HELP_en.1)を原稿から作って入れる。
help: help-epub help-epub-en help-man

install: $(BUILD_DIR)/usilic
	install -d $(INSTALL_DIR)
	install -m 755 $(BUILD_DIR)/usilic $(INSTALL_DIR)/$(INSTALL_NAME)
	install -d $(LIB_DIR)
	install -m 644 ../usilicrc $(LIB_DIR)/usilicrc
	install -m 644 ../usilicrc.en $(LIB_DIR)/usilicrc.en  # 英語コメント版(2026-09-25、設定値はusilicrcと同一)
	# 2026-10-02: TUTORIALはHELPへ改名し、正本はリポジトリ直下のdoc/HELP(HELP.enがあればそれも)。
	install -m 644 ../../doc/HELP $(LIB_DIR)/HELP
	if [ -f ../../doc/HELP.en ]; then install -m 644 ../../doc/HELP.en $(LIB_DIR)/HELP.en; fi
	# 2026-10-06: usilicのmanファイル(usilic -nで英語のHELPの原稿から作る、画面の幅に追随する)。usilic自身のMANの
	# "usilic"はHELPの本文を出すので使わない、usilicの外の`man ~/.local/lib/usilic/HELP.1`用。公的なmanの
	# ディレクトリには入れない。日本語版は同梱しない(groffが濁点付きの仮名の幅を数え違え、78桁に決め打ちになる)。
	# 2026-10-21: 日本語版がHELP.1、英語版がHELP_en.1(以前はHELP.ja.1・HELP.1)。
	if [ -f ../../doc/HELP_en.1 ]; then install -m 644 ../../doc/HELP_en.1 $(LIB_DIR)/HELP_en.1; fi
	# 2026-10-09: HELPのroff版(usilic -nで作る、日本語)。あればusilicはHELPをこれからgroffで今の幅に組む。
	if [ -f ../../doc/HELP.1 ]; then install -m 644 ../../doc/HELP.1 $(LIB_DIR)/HELP.1; fi
	# 2026-10-14: usilic -e(HELPのEPUB版)の注釈の文。HELPの原稿の隣に無ければここを読む。
	install -m 644 ../../doc/HELP.notes $(LIB_DIR)/HELP.notes
	install -m 644 ../../doc/HELP.en.notes $(LIB_DIR)/HELP.en.notes
	# 2026-10-30: HELPの表紙(手でusilic -n・-eを使った時も、$$USILIC_EPUB_COVERが無ければこれ)。
	install -m 644 packaging/usilic_icon_256.png $(LIB_DIR)/usilic_icon_256.png
	# 2026-10-18〜10-28: HELPのEPUB版(doc/HELP.epub)を入れていた。2026-10-29、直接のユーザー依頼("HELP.epubは同梱
	# せず、内部的にHELP.1からHELP.epubを作成して表示"): usilicが~/.usilic/HELP.epubをHELP.1から作る
	# (Application::helpEpubPath())ので入れず、以前に入れたものは消す。
	rm -f $(LIB_DIR)/HELP.epub $(LIB_DIR)/HELP.en.epub
	# 2026-10-04: EPUBのWebのリンクのPdf(usilicrcのURL節)。Playwrightの準備はtools/url2pdf自身のコメント参照。
	install -m 755 tools/url2pdf $(LIB_DIR)/url2pdf
	install -m 644 tools/url2pdf.py $(LIB_DIR)/url2pdf.py
	# 2026-10-11: 「小説家になろう」の作品をEPUB3にする(NarouToEpub3、Apache-2.0を改変、tools/narou2epub自身のコメント参照)。
	install -m 755 tools/narou2epub $(LIB_DIR)/narou2epub
	install -m 644 tools/narou2epub.py $(LIB_DIR)/narou2epub.py
	install -d $(LIB_DIR)/narou2epub-assets
	install -m 644 tools/narou2epub-assets/stylesheet.css tools/narou2epub-assets/cover.css tools/narou2epub-assets/LICENSE $(LIB_DIR)/narou2epub-assets/
	# 2026-10-04: ページの中のリンク先をまとめてPDFにする(tools/urls2pdf.py参照、加えてpypdfが要る)。
	install -m 755 tools/urls2pdf $(LIB_DIR)/urls2pdf
	install -m 644 tools/urls2pdf.py $(LIB_DIR)/urls2pdf.py
	# 2026-10-14: usilic-web(src/web/)の原稿置き場(^J^Eのhttp://localhost/usilic-web/から落とした本もここ、macOSの
	# src/web/config.phpもここを使う)と、見本の.epub(既にあれば上書きしない)。
	# 2026-11-25: .deb(DESTDIRあり)では入れない(利用者ごとのもの。usilicは^J^Eで落とす時にtextsを作る)。
	if [ -z "$(DESTDIR)" ]; then install -d $(HOME)/.local/usilic-web/texts $(HOME)/.local/usilic-web/books $(HOME)/.local/usilic-web/marks; fi
	if [ -z "$(DESTDIR)" ] && [ ! -f $(HOME)/.local/usilic-web/texts/sample.epub ]; then install -m 644 ../web/sample/sample.epub $(HOME)/.local/usilic-web/texts/sample.epub; fi
# 2026-10-31、直接のユーザー依頼(FreeBSD+Xfceでも"最初からアイコン起動"): FreeBSDも同じXDGのランチャー。
ifneq ($(filter Linux FreeBSD,$(shell uname)),)
ifneq ($(NO_GUI),1)
	# 2026-10-16、直接のユーザー依頼(Ubuntu、アイコンからの起動): デスクトップのランチャー(-gで起動。制御端末の無い
	# 起動はmain()がGUIにする)とアイコン。パスは絶対で書く(デスクトップのセッションのPATHに~/.local/binが無い
	# ことがある)。端末から-g・^Ogで使った時の色・フォント・大きさ(~/.usilic/history)で開く。
	install -m 644 packaging/usilic_icon_1024.png $(LIB_DIR)/usilic.png
	install -d $(DESTDIR)$(PREFIX)/share/applications
	# 2026-11-04、直接のユーザー依頼(FreeBSD、"最初のアイコン起動は明朝に"): -gを付けるとmain()のアイコンからの起動の判定
	# (制御端末が無い)を通らず、-gの組(ゴシック体の既定等)になっていた。$$USILIC_ICON_LAUNCHで明示する。
	printf '[Desktop Entry]\nType=Application\nName=usilic\nComment=Text-based file manager\nExec=env USILIC_ICON_LAUNCH=1 %s -g\nIcon=%s\nTerminal=false\nCategories=Utility;FileManager;\n' \
	  "$(PREFIX)/bin/$(INSTALL_NAME)" "$(PREFIX)/lib/usilic/usilic.png" > $(DESTDIR)$(PREFIX)/share/applications/usilic.desktop
	chmod 644 $(DESTDIR)$(PREFIX)/share/applications/usilic.desktop
	-[ -n "$(DESTDIR)" ] || ! command -v update-desktop-database >/dev/null 2>&1 || update-desktop-database $(PREFIX)/share/applications
endif
endif

# 2026-11-25、直接のユーザー依頼(16GB-KDEにg++が無く、usilicを入れ直せなかった。"U.shから-iを排除したかわりに、.deb化は
# どうかなと。dpkgにて同じ-iで入る"): Debian・Ubuntu用の.deb(/usr/bin/usilic・/usr/lib/usilic/・/usr/share/applications/)。
# build/usilic_版_アーキテクチャ.debを作る。版は今日の日付とgitの短いハッシュ。Dependsはusilicが使う共有ライブラリの
# パッケージ(dpkg -S)、Recommendsはusilicが使うツール(U.shのUSILIC_TOOLSと同じ)。入れ方は
# "sudo apt install ./build/usilic_….deb"(依存も入る。dpkg -iなら依存は入らない)。U.shの-d・-rは、DEBIAN区画の
# /optにあればこれを入れる。
DEB_VERSION ?= $(shell date +%Y%m%d).$(shell git rev-parse --short HEAD 2>/dev/null || echo 0)
DEB_ARCH    := $(shell dpkg --print-architecture 2>/dev/null)
DEB_ROOT     = $(BUILD_DIR)/deb
DEB_FILE     = $(BUILD_DIR)/usilic_$(DEB_VERSION)_$(DEB_ARCH).deb
DEB_RECOMMENDS = groff, mg, info, p7zip-full | 7zip, w3m, unzip, fonts-noto-cjk, python3, mpv
.PHONY: deb
deb: $(BUILD_DIR)/usilic
	@command -v dpkg-deb >/dev/null 2>&1 || { echo "make deb: dpkg-deb not found (Debian/Ubuntu only)"; exit 1; }
	rm -rf $(DEB_ROOT)
	$(MAKE) install DESTDIR=$(CURDIR)/$(DEB_ROOT) PREFIX=/usr
	install -d $(DEB_ROOT)/DEBIAN
	{ echo "Package: usilic"; echo "Version: $(DEB_VERSION)"; echo "Architecture: $(DEB_ARCH)"; \
	  echo "Maintainer: usilic <usilic@localhost>"; echo "Section: utils"; echo "Priority: optional"; \
	  echo "Depends: $$(ldd $(BUILD_DIR)/usilic | awk '/=> \//{print $$3}' | xargs -r readlink -f | xargs -r dpkg -S 2>/dev/null | cut -d: -f1 | sort -u | paste -sd, - | sed -e 's/,/, /g')"; \
	  echo "Recommends: $(DEB_RECOMMENDS)"; \
	  echo "Description: Text-based file manager"; } > $(DEB_ROOT)/DEBIAN/control
	cat $(DEB_ROOT)/DEBIAN/control
	dpkg-deb --root-owner-group --build $(DEB_ROOT) $(DEB_FILE)
	@echo "-> $(DEB_FILE)  (sudo apt install ./$(DEB_FILE))"

# macOS .app bundle (2026-08-24, docs/CHANGES.md item 421, a direct
# user request: "アイコンをクリックして起動する形にはできる？"):
# wraps the SAME build/usilic binary `make usilic` already produces --
# no separate build, no wrapper script. Works because argv-less
# `usilic` already defaults to GUI mode on macOS (see main.cpp's own
# `#ifdef __APPLE__ forceGui = true` comment, item ~2026-08-21) --
# LaunchServices double-clicking an app bundle invokes
# Contents/MacOS/usilic with no arguments, landing on that exact same
# default path, so this bundle needed no new app-side code at all.
# `packaging/Info.plist`/`packaging/usilic.icns` are the checked-in
# inputs; the bundle itself lands under $(BUILD_DIR) (already
# gitignored) since it's a build artifact, not source.
# `packaging/usilic.icns`(2026-08-30、ユーザー自身が用意した1024x1024
# のusilic.pngから`sips`+`iconutil`で生成、直接のユーザー依頼)は
# Contents/Resources/へコピーし、Info.plist自身のCFBundleIconFile
# キーがそれを指す。Darwin-only in practice (an .app bundle is a
# macOS-specific concept) but the rule itself is just file copying, so
# left unguarded rather than wrapped in another ifeq.
APP_BUNDLE = $(BUILD_DIR)/usilic.app

# 2026-10-20、直接のユーザー依頼("make appのたびに/Applicationsへコピーするようにして"): 作った.appを
# APP_INSTALL_DIR(既定は/Applications)の.appと置き換える(古い.appを開いて変更が反映されていない、を防ぐ)。
# macOSだけ。置き換えたくない時は`make app APP_INSTALL_DIR=`。
APP_INSTALL_DIR ?= /Applications

app: $(APP_BUNDLE)
ifeq ($(shell uname), Darwin)
	if [ -n "$(APP_INSTALL_DIR)" ]; then rm -rf "$(APP_INSTALL_DIR)/usilic.app" && cp -R $(APP_BUNDLE) "$(APP_INSTALL_DIR)/"; fi
endif

$(APP_BUNDLE): $(BUILD_DIR)/usilic packaging/Info.plist packaging/usilic.icns
	mkdir -p $(APP_BUNDLE)/Contents/MacOS
	mkdir -p $(APP_BUNDLE)/Contents/Resources
	cp packaging/Info.plist $(APP_BUNDLE)/Contents/Info.plist
	cp packaging/usilic.icns $(APP_BUNDLE)/Contents/Resources/usilic.icns
	cp $(BUILD_DIR)/usilic $(APP_BUNDLE)/Contents/MacOS/usilic
ifeq ($(shell uname), Darwin)
	# Re-signs the WHOLE bundle (Info.plist + executable together) with
	# a fresh ad-hoc signature -- `make usilic`'s own binary already
	# carries the Xcode toolchain's own linker-applied ad-hoc signature
	# (needed just to RUN at all on Apple Silicon), but that signature
	# was computed for a bare executable, not this bundle shape; left
	# as-is, `codesign -dv`/`spctl` on the assembled .app both flag
	# "code has no resources but signature indicates they must be
	# present" -- confirmed by trying it. `--force` replaces the
	# inherited one; `--deep` reaches the nested executable; `-s -`
	# is codesign's own "ad-hoc" (no real identity) shorthand, same
	# trust level the bare binary already had.
	codesign --force --deep -s - $(APP_BUNDLE)
endif
	# 2026-10-20、直接のユーザー報告("タイムスタンプが2026年9月21日 23:29のままなんだが。miniでの話。"): 中のファイルを
	# 上書きしても.appのフォルダ自体の日時は変わらない(Finderはこの日時を出す)ので、作り直したら更新する。
	touch $(APP_BUNDLE)

$(BUILD_DIR)/run_tests: $(SRCS) $(TESTS) $(HDRS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(TEST_DIR) -I$(SRC_DIR) $(SRCS) $(TESTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/usilic: $(SRCS) $(APP_SRCS) $(HDRS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRCS) $(APP_SRCS) -o $@ $(LDLIBS)

# -g(GUI版)を窓を出さずに動かす試験用プログラム(tools/gui_harness.cpp参照、2026-10-27)。usilic本体と同じ
# ソースから、app/main.cppの代わりにtools/gui_harness.cppを組み込む。
harness: $(BUILD_DIR)/harness

$(BUILD_DIR)/harness: $(SRCS) $(TOOLS_DIR)/gui_harness.cpp $(HDRS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRCS) $(TOOLS_DIR)/gui_harness.cpp -o $@ $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
endif  # WINDOWS
