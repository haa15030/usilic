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
LDLIBS   ?= -lncurses $(SDL_LIBS)

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
APP_SRCS = $(wildcard $(APP_DIR)/*.cpp)
TESTS    = $(wildcard $(TEST_DIR)/*.cpp)

.PHONY: test usilic clean

test: $(BUILD_DIR)/run_tests
	$(BUILD_DIR)/run_tests

usilic: $(BUILD_DIR)/usilic

$(BUILD_DIR)/run_tests: $(SRCS) $(TESTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(TEST_DIR) -I$(SRC_DIR) $(SRCS) $(TESTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/usilic: $(SRCS) $(APP_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRCS) $(APP_SRCS) -o $@ $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
