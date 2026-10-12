# Host L1 app for components/seat, the seat gate and the seat reading (L1-SEAT; AGENT_BRIEF host
# L1 contract; rules in tests/host/common.mk). `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).
#
# The component is header-only (constexpr): its coverage comes from the test's object, which
# gcov_summary.py reads through links named after the headers (as components/remote_ui).

NAME := seat
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

SEAT_INC := $(REPO)/components/seat/include
TEST_SRCS       := $(HERE)/test_seat.cpp $(HERE)/test_seat_guard.cpp
SRCS_UNDER_TEST :=
INCLUDES        := $(SEAT_INC) \
                   $(REPO)/components/hmi_rtps_spec/include \
                   $(REPO)/components/hmi_format/include \
                   $(REPO)/components/stick/include \
                   $(REPO)/external/rammp-rtps/components/rammp_rtps_messages/include
# SEAT-010: the gate and the reading under the host allocation guard (TS-DET-08).
ALLOC_GUARD     := 1
COVER_SRCS := $(SEAT_INC)/seat/seat_gate.hpp $(SEAT_INC)/seat/seat_reading.hpp

include $(REPO)/tests/host/common.mk

# Safety code: every switch over an enum names every enumerator (CS-TYP-06).
$(call obj_of,$(TEST_SRCS)): CXX_WARN := $(WARN_FLAGS) -Wswitch-enum

# SEAT-011 reads the firmware sources under the worktree as text.
$(call obj_of,$(HERE)/test_seat_guard.cpp): CXXFLAGS_ALL += -DSEAT_REPO_ROOT='"$(REPO)"'

ifeq ($(COVERAGE),1)
TEST_OBJ  := $(call obj_of,$(HERE)/test_seat.cpp)
COV_LINKS := $(foreach h,$(COVER_SRCS),$(call obj_of,$(h)) $(patsubst %.o,%.gcno,$(call obj_of,$(h))) \
               $(patsubst %.o,%.gcda,$(call obj_of,$(h))))
test: $(COV_LINKS)
$(COV_LINKS): $(TEST_OBJ)
	@mkdir -p $(dir $@)
	ln -sf $(patsubst %.o,%$(suffix $@),$(TEST_OBJ)) $@
endif
