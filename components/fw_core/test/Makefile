# Host L1 app for components/fw_core (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk).
#
#   make test       build and run; exit code 0 = PASS (TEST_FILTER, HOST_TEST_SEED as in common.mk)
#   make coverage   the same with gcov; prints COVERAGE <file> lines <pct> branches <pct>
#   make mnc        must-not-compile cases (TS-UNIT-06)
#   make clean

NAME := fw_core
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

COMP := $(REPO)/components/fw_core

SRCS_UNDER_TEST := $(COMP)/src/check.cpp $(COMP)/src/error.cpp $(COMP)/src/queue.cpp \
                   $(COMP)/src/thread_checker.cpp $(COMP)/src/port_host.cpp
TEST_SRCS       := $(HERE)/test_fw_core.cpp
INCLUDES        := $(COMP)/include

# Most of fw_core is templates in headers, so coverage is reported for the headers too, merged
# over every object (coverage_report.py) rather than per object.
COV_FILES := $(SRCS_UNDER_TEST) $(wildcard $(COMP)/include/fw_core/*.hpp) \
             $(COMP)/include/fw_core/port/raw_queue_host.hpp

include $(REPO)/tests/host/common.mk

# The rest of fw_component_options' warning set (CS-LNG-02), on top of common.mk's.
CXX_WARN += -Wnon-virtual-dtor -Woverloaded-virtual -Wsign-compare -Wnull-dereference \
            -Wimplicit-fallthrough -Wunused-parameter
# Test builds abort on a failed ownership check (CS-OWN-11); the tests inject their own handler.
BASE_FLAGS += -DCONFIG_HMI_OWNERSHIP_CHECKS_ABORT=1

.PHONY: mnc

# Replaces common.mk's coverage recipe (make warns that it is overridden): common.mk reports
# one object per source, which leaves out the headers where the templates live.
coverage:
	@rm -f $$(find $(BUILD_DIR)/cov -name '*.gcda' 2>/dev/null)
	@$(MAKE) --no-print-directory test COVERAGE=1
	@python3 $(HERE)/coverage_report.py $(BUILD_DIR)/cov/obj $(REPO) $(COV_FILES)

# Each mnc/*.cpp is built twice: without MNC_FAULT it must compile (the twin), and with
# -DMNC_FAULT it must fail with the text on its "// expect-error:" lines.
mnc:
	@mkdir -p $(BUILD_DIR)/mnc
	bash $(HERE)/mnc/run_mnc.sh \
	    "$(CXX) -std=gnu++23 -O0 -g $(SAN_FLAGS) $(CXX_WARN) \
	     -DCONFIG_HMI_OWNERSHIP_CHECKS_ABORT=1 $(addprefix -I,$(INCLUDES)) -fsyntax-only" \
	    $(HERE)/mnc $(BUILD_DIR)/mnc
