# Host L1 app for components/remote_ui's bench verbs (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner,
# 2026-10-06).
#
# bench_verbs.hpp is header-only and plain C++ (no IDF, no LVGL): its parser, the STATE line
# and the hook dispatch are tested here. The socket server around it (remote_ui.cpp) is a
# bench tool and is exercised on the board (tools/bench).

NAME := remote_ui
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

RUI := $(REPO)/components/remote_ui

TEST_SRCS       := $(HERE)/test_bench_verbs.cpp
SRCS_UNDER_TEST :=
INCLUDES        := $(RUI)/include
SYS_INCLUDES    :=

# The header under test has no object of its own: its coverage comes from the test's object,
# which gcov_summary.py reads through a link named after the header (as components/topology).
COVER_SRCS := $(RUI)/include/bench_verbs.hpp

include $(REPO)/tests/host/common.mk

ifeq ($(COVERAGE),1)
TEST_OBJ  := $(call obj_of,$(HERE)/test_bench_verbs.cpp)
COV_LINKS := $(foreach h,$(COVER_SRCS),$(call obj_of,$(h)) $(patsubst %.o,%.gcno,$(call obj_of,$(h))) \
               $(patsubst %.o,%.gcda,$(call obj_of,$(h))))
test: $(COV_LINKS)
$(COV_LINKS): $(TEST_OBJ)
	@mkdir -p $(dir $@)
	ln -sf $(patsubst %.o,%$(suffix $@),$(TEST_OBJ)) $@
endif
