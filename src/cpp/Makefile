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
CXXFLAGS ?= -std=c++17 -Wall -Wextra -g $(SDL_CFLAGS)
# terminal_display.cpp uses ncurses' terminfo API (setupterm/tputs/
# tgoto), same as the legacy C's config.sh (LIBS= -lncurses).
# mac_font_resolver.cpp's CoreText/CoreFoundation use is #ifdef
# __APPLE__-guarded (a no-op stub elsewhere), but the frameworks still
# need linking on macOS specifically -- `-framework` isn't a flag
# Linux's linker understands, so this is gated on uname rather than
# added unconditionally.
LDLIBS   ?= -lncurses $(SDL_LIBS)
ifeq ($(shell uname), Darwin)
LDLIBS += -framework CoreText -framework CoreFoundation -framework Cocoa
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
APP_SRCS = $(wildcard $(APP_DIR)/*.cpp)
TESTS    = $(wildcard $(TEST_DIR)/*.cpp)

# Installed name is deliberately NOT "usilic" -- /usr/local/bin/usilic
# is already occupied by the legacy C build (src/install.sh's own
# `install ... tcb $(DESTDIR)/usr/local/bin/usilic`, predating this
# C++ port entirely), which stays there untouched since it's still in
# daily use and this C++ port isn't yet at feature parity with it.
# "U" was the user's own choice (2026-07-31) for a short, easy-to-type
# name for trying the C++ build day-to-day without colliding with the
# legacy binary.
INSTALL_NAME = U
INSTALL_DIR  = /usr/local/bin

.PHONY: test usilic install clean

test: $(BUILD_DIR)/run_tests
	$(BUILD_DIR)/run_tests

usilic: $(BUILD_DIR)/usilic

install: $(BUILD_DIR)/usilic
	install -d $(INSTALL_DIR)
	install -m 755 $(BUILD_DIR)/usilic $(INSTALL_DIR)/$(INSTALL_NAME)

$(BUILD_DIR)/run_tests: $(SRCS) $(TESTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(TEST_DIR) -I$(SRC_DIR) $(SRCS) $(TESTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/usilic: $(SRCS) $(APP_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRCS) $(APP_SRCS) -o $@ $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
