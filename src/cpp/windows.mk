# Windowsのネイティブ(MinGW-w64、互換層無し)の-gだけのusilic.exe(.steering/20261015-windows-native-gui/)。
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
               -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic -lshell32 -lole32 -luuid

SRC_DIR   = src
APP_DIR   = app
BUILD_DIR = build-win
OBJ_DIR   = $(BUILD_DIR)/obj

# Windowsでは使わないもの(端末・擬似端末・Macのカーソル・GUI無しのスタブ)は、同じ名前の*_win.cppへ差し替える。
WIN_REPLACED = terminal_display graphical_display_stub child_process
SRCS := $(filter-out $(foreach f,$(WIN_REPLACED),$(SRC_DIR)/$(f).cpp), $(wildcard $(SRC_DIR)/*.cpp))
SRCS := $(filter-out $(SRC_DIR)/%_win.cpp, $(SRCS)) $(wildcard $(SRC_DIR)/*_win.cpp)
APP_SRCS = $(wildcard $(APP_DIR)/*.cpp)
OBJS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS) $(APP_SRCS)) $(OBJ_DIR)/usilic_res.o

.PHONY: usilic dist clean
usilic: $(BUILD_DIR)/usilic.exe

$(BUILD_DIR)/usilic.exe: $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDLIBS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -c $< -o $@

$(OBJ_DIR)/usilic_res.o: packaging/windows/usilic.rc packaging/windows/usilic.manifest
	@mkdir -p $(dir $@)
	$(WINDRES) -I packaging/windows $< -o $@

STRIP ?= $(WIN_TRIPLE)-strip
# HELPのEPUB版(usilic -eで作ったもの)があれば一緒に入れる。
WIN_HELP_EPUB ?= $(HOME)/.local/lib/usilic/HELP.epub

# 配る形: build-win/usilic/にusilic.exe(strip済み)・SDLのDLL・HELP等、それをbuild-win/usilic-win64.zipに。
dist: $(BUILD_DIR)/usilic.exe
	rm -rf $(BUILD_DIR)/usilic $(BUILD_DIR)/usilic-win64.zip
	mkdir -p $(BUILD_DIR)/usilic/lib/usilic
	$(STRIP) -o $(BUILD_DIR)/usilic/usilic.exe $(BUILD_DIR)/usilic.exe
	$(STRIP) -o $(BUILD_DIR)/usilic/SDL2.dll $(WIN_SDL_DIR)/bin/SDL2.dll
	$(STRIP) -o $(BUILD_DIR)/usilic/SDL2_ttf.dll $(WIN_SDL_DIR)/bin/SDL2_ttf.dll
	cp ../usilicrc ../usilicrc.en ../../doc/HELP ../../doc/HELP.notes $(BUILD_DIR)/usilic/lib/usilic/
	if [ -f $(WIN_HELP_EPUB) ]; then cp $(WIN_HELP_EPUB) $(BUILD_DIR)/usilic/lib/usilic/; fi
	cd $(BUILD_DIR) && zip -qr usilic-win64.zip usilic

clean:
	rm -rf $(BUILD_DIR)
