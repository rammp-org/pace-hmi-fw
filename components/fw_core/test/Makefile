# Host L1 test app for fw_core (the host L1 app contract in the overnight brief).
# Deviation, approved by the owner on 2026-10-06: L1 runs as host-native g++ in WSL instead of
# the ESP-IDF linux target (TS-UNIT-01, CS-HAL-04).
#
#   make test       build and run; exit code 0 = PASS
#   make coverage   the same with gcov; prints COVERAGE <file> lines <pct> branches <pct>
#   make mnc        must-not-compile cases (TS-UNIT-06)

NAME      := fw_core
TEST_DIR  := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
REPO      ?= $(abspath $(TEST_DIR)/../../..)
BUILD_DIR ?= $(HOME)/hmi-build/$(NAME)
UNITY_DIR ?= /mnt/c/esp/v6.0/esp-idf/components/unity/unity/src

COMP      := $(REPO)/components/fw_core
CXX       := g++
CC        := gcc
SAN       := -fsanitize=address,undefined -fno-omit-frame-pointer
# The contract's flags, plus the rest of fw_component_options' warning set (CS-LNG-02).
CXXFLAGS  := -std=gnu++23 -O0 -g -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion \
             -Wnon-virtual-dtor -Woverloaded-virtual -Wsign-compare -Wnull-dereference \
             -Wimplicit-fallthrough -Wunused-parameter $(SAN) -pthread
CFLAGS    := -std=gnu11 -O0 -g $(SAN)
# Test builds abort on a failed ownership check (CS-OWN-11); tests inject their own handler.
DEFS      := -DCONFIG_HMI_OWNERSHIP_CHECKS_ABORT=1
INCS      := -I$(COMP)/include -I$(UNITY_DIR)

SRCS      := $(COMP)/src/check.cpp $(COMP)/src/error.cpp $(COMP)/src/queue.cpp \
             $(COMP)/src/thread_checker.cpp $(COMP)/src/port_host.cpp $(TEST_DIR)/test_fw_core.cpp
HEADERS   := $(wildcard $(COMP)/include/fw_core/*.hpp $(COMP)/include/fw_core/port/*.hpp \
             $(COMP)/src/*.hpp)

# The files coverage is reported for: everything under test, not the test itself.
COV_FILES := $(COMP)/src/check.cpp $(COMP)/src/error.cpp $(COMP)/src/queue.cpp \
             $(COMP)/src/thread_checker.cpp $(COMP)/src/port_host.cpp \
             $(wildcard $(COMP)/include/fw_core/*.hpp) \
             $(COMP)/include/fw_core/port/raw_queue_host.hpp

.PHONY: test coverage mnc clean

# --- test ----------------------------------------------------------------------------------
TBIN := $(BUILD_DIR)/test/test_$(NAME)
TOBJ := $(patsubst %.cpp,$(BUILD_DIR)/test/%.o,$(notdir $(SRCS))) $(BUILD_DIR)/test/unity.o

test: $(TBIN)
	$(TBIN)

$(TBIN): $(TOBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/test/%.o: $(COMP)/src/%.cpp $(HEADERS) | $(BUILD_DIR)/test
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCS) -I$(COMP)/src -c $< -o $@
$(BUILD_DIR)/test/%.o: $(TEST_DIR)/%.cpp $(HEADERS) | $(BUILD_DIR)/test
	$(CXX) $(CXXFLAGS) $(DEFS) $(INCS) -c $< -o $@
$(BUILD_DIR)/test/unity.o: $(UNITY_DIR)/unity.c | $(BUILD_DIR)/test
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -c $< -o $@

# --- coverage ------------------------------------------------------------------------------
CBIN := $(BUILD_DIR)/cov/test_$(NAME)
COBJ := $(patsubst %.cpp,$(BUILD_DIR)/cov/%.o,$(notdir $(SRCS))) $(BUILD_DIR)/cov/unity.o

coverage: $(CBIN)
	rm -f $(BUILD_DIR)/cov/*.gcda
	$(CBIN)
	python3 $(TEST_DIR)/coverage_report.py $(BUILD_DIR)/cov $(REPO) $(COV_FILES)

$(CBIN): $(COBJ)
	$(CXX) $(CXXFLAGS) --coverage -o $@ $^

$(BUILD_DIR)/cov/%.o: $(COMP)/src/%.cpp $(HEADERS) | $(BUILD_DIR)/cov
	$(CXX) $(CXXFLAGS) --coverage $(DEFS) $(INCS) -I$(COMP)/src -c $< -o $@
$(BUILD_DIR)/cov/%.o: $(TEST_DIR)/%.cpp $(HEADERS) | $(BUILD_DIR)/cov
	$(CXX) $(CXXFLAGS) --coverage $(DEFS) $(INCS) -c $< -o $@
$(BUILD_DIR)/cov/unity.o: $(UNITY_DIR)/unity.c | $(BUILD_DIR)/cov
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -c $< -o $@

# --- must-not-compile ----------------------------------------------------------------------
# Each mnc/*.cpp is built twice: without MNC_FAULT it must compile (the twin), and with
# -DMNC_FAULT it must fail with the text on its "// expect-error:" line.
mnc: | $(BUILD_DIR)/mnc
	bash $(TEST_DIR)/mnc/run_mnc.sh "$(CXX) $(CXXFLAGS) $(DEFS) $(INCS) -fsyntax-only" \
	    $(TEST_DIR)/mnc $(BUILD_DIR)/mnc

$(BUILD_DIR)/test $(BUILD_DIR)/cov $(BUILD_DIR)/mnc:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
