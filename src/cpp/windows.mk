# Windowsのネイティブ(MinGW-w64、互換層無し)のusilic.exe(窓、.steering/20261015-windows-native-gui/)と
# usilic-c.exe(コンソールのCUI、.steering/20261102-windows-console-cui/)。同じオブジェクトをサブシステムだけ変えてリンクする。
# Macからのクロスビルド: brew install mingw-w64、SDL2・SDL2_ttfのMinGW向けの配布物(SDL2-devel-*-mingw・
# SDL2_ttf-devel-*-mingw)のx86_64-w64-mingw32のinclude・lib・binをWIN_SDL_DIRへ。Windows上のMSYS2の
# UCRT64/MINGW64の端末でも、CXX=g++ WINDRES=windresで同じく作れる。
# 出力: build-win/usilic.exe。動かす時はWIN_SDL_DIR/binのSDL2.dll・SDL2_ttf.dllを隣に置く(make WINDOWS=1 dist)。

WIN_TRIPLE  ?= x86_64-w64-mingw32
CXX         := $(WIN_TRIPLE)-g++
WINDRES     ?= $(WIN_TRIPLE)-windres
WIN_SDL_DIR ?= $(HOME)/.local/mingw
OPTFLAGS    ?= -O2
CXXFLAGS    := -std=gnu++17 $(OPTFLAGS) -Wall -Wextra -Wno-missing-field-initializers -I$(WIN_SDL_DIR)/include \
               -DUSILIC_NATIVE_WINDOWS -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
# -mwindows: コンソールの窓を出さない。SDL2mainがWinMainからmain()を呼ぶ。
# SDL2・SDL2_ttfはDLL(libSDL2.dll.a)でリンクし、libstdc++・libgcc・winpthreadだけ静的に(usilic.exeとSDLの
# DLLだけで動くように)。
LDLIBS      := -L$(WIN_SDL_DIR)/lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -mwindows \
               -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic -lshell32 -lole32 -luuid -ladvapi32

SRC_DIR   = src
APP_DIR   = app
BUILD_DIR = build-win
OBJ_DIR   = $(BUILD_DIR)/obj

# Windowsでは使わないもの(擬似端末・Macのカーソル・GUI無しのスタブ)は、同じ名前の*_win.cppへ差し替える。
WIN_REPLACED = graphical_display_stub child_process
SRCS := $(filter-out $(foreach f,$(WIN_REPLACED),$(SRC_DIR)/$(f).cpp), $(wildcard $(SRC_DIR)/*.cpp))
SRCS := $(filter-out $(SRC_DIR)/%_win.cpp, $(SRCS)) $(wildcard $(SRC_DIR)/*_win.cpp)
APP_SRCS = $(wildcard $(APP_DIR)/*.cpp)
OBJS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS) $(APP_SRCS)) $(OBJ_DIR)/usilic_res.o

.PHONY: usilic dist clean
usilic: $(BUILD_DIR)/usilic.exe $(BUILD_DIR)/usilic-c.exe

# コンソールのサブシステム(libmingw32のmain()がSDL2mainのWinMain()を呼ぶ)。build_stampはusilic.exeのものを使う。
$(BUILD_DIR)/usilic-c.exe: $(BUILD_DIR)/usilic.exe
	$(CXX) $(OBJS) $(OBJ_DIR)/build_stamp.o -o $@ $(subst -mwindows,-mconsole,$(LDLIBS))

# 窓のタイトルに出すビルドの日時(2026-10-15、どのusilic.exeを動かしているかを見分けるため、graphical_display.cpp
# のusilicBuildStamp参照)。リンクのたびに作り直す。
$(BUILD_DIR)/usilic.exe: $(OBJS)
	printf 'namespace fm { extern const char usilicBuildStamp[] = "%s"; }\n' "$$(date '+%Y-%m-%d %H:%M')" > $(OBJ_DIR)/build_stamp.cpp
	$(CXX) $(CXXFLAGS) -c $(OBJ_DIR)/build_stamp.cpp -o $(OBJ_DIR)/build_stamp.o
	$(CXX) $(OBJS) $(OBJ_DIR)/build_stamp.o -o $@ $(LDLIBS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -I$(SRC_DIR) -c $< -o $@

# ヘッダ(posix_compat.hpp等)を直した時も、それを読むものを作り直す(2026-10-15、usleepの差し替えがmain.oに
# 入っていなかった)。
-include $(OBJS:.o=.d)

$(OBJ_DIR)/usilic_res.o: packaging/windows/usilic.rc packaging/windows/usilic.manifest packaging/windows/usilic.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -I packaging/windows $< -o $@

STRIP ?= $(WIN_TRIPLE)-strip
# HELPのEPUB版(usilic -eで作ったもの)があれば一緒に入れる。
WIN_HELP_EPUB ?= $(HOME)/.local/lib/usilic/HELP.epub
# 英語版(^Ojの後の^H、2026-10-20、直接のユーザー依頼"入れるようにしてほしい")。
WIN_HELP_EN_EPUB ?= $(HOME)/.local/lib/usilic/HELP.en.epub

# 配る形: build-win/usilic/にusilic.exe(strip済み)・SDLのDLL・HELP等、それをbuild-win/usilic-win64.zipに。
dist: $(BUILD_DIR)/usilic.exe $(BUILD_DIR)/usilic-c.exe
	rm -rf $(BUILD_DIR)/usilic $(BUILD_DIR)/usilic-win64.zip
	mkdir -p $(BUILD_DIR)/usilic/lib/usilic
	$(STRIP) -o $(BUILD_DIR)/usilic/usilic.exe $(BUILD_DIR)/usilic.exe
	$(STRIP) -o $(BUILD_DIR)/usilic/usilic-c.exe $(BUILD_DIR)/usilic-c.exe
	$(STRIP) -o $(BUILD_DIR)/usilic/SDL2.dll $(WIN_SDL_DIR)/bin/SDL2.dll
	$(STRIP) -o $(BUILD_DIR)/usilic/SDL2_ttf.dll $(WIN_SDL_DIR)/bin/SDL2_ttf.dll
	cp ../usilicrc ../usilicrc.en ../../doc/HELP ../../doc/HELP.notes $(BUILD_DIR)/usilic/lib/usilic/
	if [ -f $(WIN_HELP_EPUB) ]; then cp $(WIN_HELP_EPUB) $(BUILD_DIR)/usilic/lib/usilic/; fi
	if [ -f $(WIN_HELP_EN_EPUB) ]; then cp $(WIN_HELP_EN_EPUB) $(BUILD_DIR)/usilic/lib/usilic/; fi
	# 2026-10-20、直接のユーザー依頼("Win版のMANはusilicのみでいいかも。造りは嘘っぽ…でいい"): Windowsにはmanも
	# groffも無いので、MANのusilicのページ(英語HELP.1・日本語HELP.ja.1)をここでgroffで78桁に組んで入れる
	# (ManMode::runAndWrap()が読む、^Ojで切り替え)。
	# 2026-10-21: 日本語版がHELP.1、英語版がHELP_en.1になった(組んだ平文の名前はそのまま)。
	env GROFF_NO_SGR=1 groff -mandoc -Kutf8 -Tutf8 -rLL=78n ../../doc/HELP_en.1 > $(BUILD_DIR)/usilic/lib/usilic/HELP.man
	env GROFF_NO_SGR=1 groff -mandoc -Kutf8 -Tutf8 -rLL=78n ../../doc/HELP.1 > $(BUILD_DIR)/usilic/lib/usilic/HELP.ja.man
	# 2026-10-22、直接のユーザー報告("WindowsではHELPが最新じゃない"): HELP・KEYの本文もroff版(doc/HELP.1・
	# doc/HELP_en.1)から。groffが無いので、ここでHELPの組み方(-rusilic-help=1)で組んだものを隣の<roff>.txtに置く
	# (Application::helpPrerenderedPath_参照)。以前は10/5から直していない平文のdoc/HELPになっていた。
	cp ../../doc/HELP.1 ../../doc/HELP_en.1 $(BUILD_DIR)/usilic/lib/usilic/
	env GROFF_NO_SGR=1 groff -mandoc -Kutf8 -Tutf8 -rLL=78n -rusilic-help=1 ../../doc/HELP.1 > $(BUILD_DIR)/usilic/lib/usilic/HELP.1.txt
	env GROFF_NO_SGR=1 groff -mandoc -Kutf8 -Tutf8 -rLL=78n -rusilic-help=1 ../../doc/HELP_en.1 > $(BUILD_DIR)/usilic/lib/usilic/HELP_en.1.txt
	cd $(BUILD_DIR) && zip -qr usilic-win64.zip usilic

clean:
	rm -rf $(BUILD_DIR)
