# Host L1 app for components/control (AGENT_BRIEF host L1 contract; rules in tests/host/common.mk).
# Cases CTL-0nn: the motion guard and the per-cycle sequence (hazard fix C4,
# docs/plans/hazard-c4-spec.md §7), on fake time only (TS-DET-02), with the host allocation
# guard (TS-DET-08).
#   make test       build and run; exit code 0 = PASS
#   make coverage   the same with gcov; COVERAGE lines for the control headers
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).

NAME := control
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC := $(REPO)/managed_components

ALLOC_GUARD := 1

# The real stick pipeline runs inside the cycle (CTL-014, CTL-018, CTL-019): its source is
# linked as it is (tested by L1-STK), with the joystick mapping engine and espp's logger as
# third party, as the stick app does.
TEST_SRCS        := $(HERE)/test_motion_guard.cpp $(HERE)/test_cycle.cpp $(HERE)/test_adc_window.cpp \
                    $(REPO)/components/stick/src/stick_pipeline.cpp
THIRD_PARTY_SRCS := $(REPO)/components/joystick/src/joystick.cpp $(MC)/espp__logger/src/logger.cpp
# adc_window.hpp is the vendored ContinuousAdc's pure bookkeeping (CTL-024, REQ-CTL-16).
INCLUDES         := $(REPO)/components/control/include $(REPO)/components/stick/include \
                    $(REPO)/components/espp_adc/include \
                    $(REPO)/components/hmi_rtps_spec/include $(HERE)
SYS_INCLUDES     := $(REPO)/external/rammp-rtps/components/rammp_rtps_messages/include \
                    $(REPO)/components/joystick/include \
                    $(MC)/espp__base_component/include \
                    $(MC)/espp__logger/include \
                    $(MC)/espp__format/include \
                    $(MC)/espp__format/detail/fmt/include \
                    $(MC)/espp__math/include
GUARD_HEADER     := $(REPO)/components/control/include/control/motion_guard.hpp
CYCLE_HEADER     := $(REPO)/components/control/include/control/cycle.hpp
WINDOW_HEADER    := $(REPO)/components/espp_adc/include/adc_window.hpp
COVER_SRCS       := $(GUARD_HEADER) $(CYCLE_HEADER) $(WINDOW_HEADER)

include $(REPO)/tests/host/common.mk

# Coverage of the headers: they are header-only, so their counters live in the test objects
# that use them. gcov_summary.py reads <obj dir>/<source>.o and the .gcno/.gcda beside it; these
# links give each header the name of the test object that exercises it (the guard: its own
# test file; the cycle: test_cycle.cpp; adc_window.hpp: test_adc_window.cpp). Remade on every
# coverage run (`coverage` deletes *.gcda).
ifeq ($(COVERAGE),1)
GUARD_TEST_OBJ := $(call obj_of,$(HERE)/test_motion_guard.cpp)
CYCLE_TEST_OBJ := $(call obj_of,$(HERE)/test_cycle.cpp)
WINDOW_TEST_OBJ := $(call obj_of,$(HERE)/test_adc_window.cpp)
.PHONY: header-cov-links
header-cov-links: $(BIN)
	@set -e; for pair in "$(call obj_of,$(GUARD_HEADER)) $(GUARD_TEST_OBJ)" \
	                     "$(call obj_of,$(CYCLE_HEADER)) $(CYCLE_TEST_OBJ)" \
	                     "$(call obj_of,$(WINDOW_HEADER)) $(WINDOW_TEST_OBJ)"; do \
	  set -- $$pair; mkdir -p $$(dirname $$1); \
	  ln -sf $$2 $$1; ln -sf $${2%.o}.gcno $${1%.o}.gcno; ln -sf $${2%.o}.gcda $${1%.o}.gcda; \
	done
test: header-cov-links
endif
