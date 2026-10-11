# Host L1 app for components/alloc_guard's counting (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner,
# 2026-10-06).
#
# alloc_counter.hpp is header-only and plain C++ (no IDF): what counts where, arming, the
# slots, and that note() itself allocates nothing (the host allocation guard, ALLOC_GUARD).
# The heap hook around it (src/alloc_guard.cpp) is built in the alloc_guard CI variant and
# exercised on the board (the self test's mem.alloc_* rows).

NAME := alloc_counter
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

AGC := $(REPO)/components/alloc_guard

ALLOC_GUARD     := 1
TEST_SRCS       := $(HERE)/test_alloc_counter.cpp
SRCS_UNDER_TEST :=
INCLUDES        := $(AGC)/include
SYS_INCLUDES    :=

# The header under test has no object of its own: its coverage comes from the test's object,
# which gcov_summary.py reads through a link named after the header (as components/remote_ui).
COVER_SRCS := $(AGC)/include/alloc_guard/alloc_counter.hpp

include $(REPO)/tests/host/common.mk

ifeq ($(COVERAGE),1)
TEST_OBJ  := $(call obj_of,$(HERE)/test_alloc_counter.cpp)
COV_LINKS := $(foreach h,$(COVER_SRCS),$(call obj_of,$(h)) $(patsubst %.o,%.gcno,$(call obj_of,$(h))) \
               $(patsubst %.o,%.gcda,$(call obj_of,$(h))))
test: $(COV_LINKS)
$(COV_LINKS): $(TEST_OBJ)
	@mkdir -p $(dir $@)
	ln -sf $(patsubst %.o,%$(suffix $@),$(TEST_OBJ)) $@
endif
