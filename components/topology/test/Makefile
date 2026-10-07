# Host L1 app for components/topology (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make mnc` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner,
# 2026-10-06).
#
# The topology is header-only. fw_core's sources link in for the Topology's channels (host
# port); espp's task.hpp is used for Task::BaseConfig only (no espp source is linked).

NAME := topology
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC   := $(REPO)/managed_components
TOPO := $(REPO)/components/topology
FWC  := $(REPO)/components/fw_core

TEST_SRCS       := $(HERE)/test_topology.cpp
# fw_core is not under test here (its own app tests it), but it is first-party: full flags.
SRCS_UNDER_TEST := $(FWC)/src/check.cpp $(FWC)/src/error.cpp $(FWC)/src/queue.cpp \
                   $(FWC)/src/thread_checker.cpp $(FWC)/src/port_host.cpp
INCLUDES        := $(TOPO)/include $(FWC)/include $(FWC)/src
SYS_INCLUDES    := $(MC)/espp__task/include \
                   $(MC)/espp__base_component/include \
                   $(MC)/espp__logger/include \
                   $(MC)/espp__format/include \
                   $(MC)/espp__format/detail/fmt/include

# The headers under test. They have no object of their own: their coverage comes from the
# test's object, which gcov_summary.py reads through links named after each header.
COVER_SRCS := $(TOPO)/include/topology_validate.hpp $(TOPO)/include/topology_espp.hpp

include $(REPO)/tests/host/common.mk

ifeq ($(COVERAGE),1)
TEST_OBJ  := $(call obj_of,$(HERE)/test_topology.cpp)
COV_LINKS := $(foreach h,$(COVER_SRCS),$(call obj_of,$(h)) $(patsubst %.o,%.gcno,$(call obj_of,$(h))) \
               $(patsubst %.o,%.gcda,$(call obj_of,$(h))))
test: $(COV_LINKS)
$(COV_LINKS): $(TEST_OBJ)
	@mkdir -p $(dir $@)
	ln -sf $(patsubst %.o,%$(suffix $@),$(TEST_OBJ)) $@
endif

# --- must-not-compile (TS-UNIT-06) ------------------------------------------------------------
# Each mnc/mnc_*.cpp builds twice: without MNC_FAULT it must compile (the twin), with
# -DMNC_FAULT it must fail with the text on its "// expect-error:" line. The runner is
# fw_core's (same contract).
MNC_CXX := $(CXX) -std=gnu++23 $(WARN_FLAGS) $(addprefix -I,$(INCLUDES) $(HERE)/mnc) \
           $(addprefix -isystem ,$(SYS_INCLUDES)) -fsyntax-only

.PHONY: mnc
mnc:
	@mkdir -p $(BUILD_DIR)/mnc
	bash $(FWC)/test/mnc/run_mnc.sh "$(MNC_CXX)" $(HERE)/mnc $(BUILD_DIR)/mnc
