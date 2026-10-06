# Shared make rules for a host L1 test app (AGENT_BRIEF "host L1 app contract").
#
# spec-deviation(TS-UNIT-01): L1 runs as host-native g++ in WSL, not on the IDF `linux`
# target (no IDF/cmake in WSL; approved by the owner on 2026-10-06, see the project profile).
#
# An app Makefile sets these, then includes this file last:
#   NAME             app name (also the default build folder name)
#   HERE             the app folder, absolute
#   SRCS_UNDER_TEST  sources under test (C++), built with the full flag set
#   TEST_SRCS        the app's test sources (C++), built with the full flag set
#   INCLUDES         project include folders (-I)
#   SYS_INCLUDES     third-party include folders (-isystem: their warnings are not ours)
#   THIRD_PARTY_SRCS optional: third-party C++ sources the app links (e.g. espp's logger.cpp);
#                    same standard and sanitizers, but their warnings are not ours (-w)
#   COVER_SRCS       optional: sources reported by `make coverage` (default SRCS_UNDER_TEST)
#
# Targets:
#   make test        build and run; exit code 0 = PASS; prints Unity's summary line
#   make coverage    the same with --coverage, then `COVERAGE <file> lines <pct> branches <pct>`
#   make clean
# Variables a caller may override: BUILD_DIR, REPO, UNITY_DIR, TEST_FILTER (case-name
# prefixes passed to the app), HOST_TEST_SEED (shuffles the case order; 0 = declared order).

ifndef NAME
$(error NAME is not set: set it in the app Makefile before including common.mk)
endif

HOST_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO       ?= $(abspath $(HOST_DIR)/../..)
BUILD_DIR  ?= $(HOME)/hmi-build/$(NAME)
UNITY_DIR  ?= /mnt/c/esp/v6.0/esp-idf/components/unity/unity/src
COVER_SRCS ?= $(SRCS_UNDER_TEST)

ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif

WARN_FLAGS := -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion
SAN_FLAGS  := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
BASE_FLAGS := -O0 -g $(SAN_FLAGS) -MMD -MP

ifeq ($(COVERAGE),1)
OUT       := $(BUILD_DIR)/cov
COV_FLAGS := --coverage
else
OUT       := $(BUILD_DIR)/test
COV_FLAGS :=
endif

CXX_WARN     := $(WARN_FLAGS)
CXXFLAGS_ALL  = -std=gnu++23 $(BASE_FLAGS) $(CXX_WARN) $(COV_FLAGS) \
                $(addprefix -I,$(INCLUDES) $(HOST_DIR)) \
                $(addprefix -isystem ,$(SYS_INCLUDES) $(UNITY_DIR))
# Unity is third-party C: built with gcc, the sanitizers, and its own warnings left alone.
CFLAGS_UNITY := -std=gnu11 $(BASE_FLAGS) -w

obj_of    = $(patsubst /%,$(OUT)/obj/%.o,$(abspath $(1)))
CXX_SRCS := $(SRCS_UNDER_TEST) $(TEST_SRCS) $(HOST_DIR)/test_main.cpp
TP_OBJS  := $(call obj_of,$(THIRD_PARTY_SRCS))
OBJS     := $(call obj_of,$(CXX_SRCS)) $(TP_OBJS) $(OUT)/obj/unity.c.o
BIN      := $(OUT)/test_$(NAME)

export ASAN_OPTIONS  ?= detect_leaks=1:abort_on_error=0:halt_on_error=1
export UBSAN_OPTIONS ?= print_stacktrace=1:halt_on_error=1
export HOST_TEST_SEED ?= 0

.PHONY: test coverage clean
.DEFAULT_GOAL := test

test: $(BIN)
	@echo "ARTIFACT $$(sha256sum $(BIN) | cut -d' ' -f1) $(BIN)"
	$(BIN) $(TEST_FILTER)

coverage:
	@rm -f $$(find $(BUILD_DIR)/cov -name '*.gcda' 2>/dev/null)
	@$(MAKE) --no-print-directory test COVERAGE=1
	@python3 $(HOST_DIR)/gcov_summary.py --root $(REPO) $(BUILD_DIR)/cov/obj $(abspath $(COVER_SRCS))

clean:
	rm -rf $(BUILD_DIR)

$(TP_OBJS): CXX_WARN := -w

$(BIN): $(OBJS)
	$(CXX) $(SAN_FLAGS) $(COV_FLAGS) $(OBJS) -o $@

$(OUT)/obj/%.o: /%
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_ALL) -c $< -o $@

$(OUT)/obj/unity.c.o: $(UNITY_DIR)/unity.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_UNITY) -c $< -o $@

-include $(OBJS:.o=.d)
