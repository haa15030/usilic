CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -g

SRC_DIR   = src
TEST_DIR  = tests
BUILD_DIR = build

# every .cpp under src/ becomes part of both the real build and the
# test build, so tests always compile against the real classes.
SRCS  = $(wildcard $(SRC_DIR)/*.cpp)
TESTS = $(wildcard $(TEST_DIR)/*.cpp)

.PHONY: test clean

test: $(BUILD_DIR)/run_tests
	$(BUILD_DIR)/run_tests

$(BUILD_DIR)/run_tests: $(SRCS) $(TESTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(TEST_DIR) -I$(SRC_DIR) $(SRCS) $(TESTS) -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
