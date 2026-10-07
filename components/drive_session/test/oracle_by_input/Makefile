# Host L1 app for components/drive_session: the transition-table oracle by input (TS-UNIT-08,
# TS-UNIT-09; docs/plans/hazard-fixes.md B3). Contract and rules in tests/host/common.mk:
# `make test` | `make coverage` | `make clean`. Two more targets, on demand:
#   make equivalence   this oracle visits every (phase, input, read-bit) class the full-product
#                      oracle (../oracle) visits, with the same row and verdict (equivalence.cpp)
#   make mutants       one-line mutations of the table and of the code: both oracles must
#                      reject each one (mutants.sh)
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).

NAME := drive_session_by_input
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../../..)

SRCS_UNDER_TEST := $(REPO)/components/drive_session/src/drive_session.cpp
TEST_SRCS       := $(HERE)/test_drive_session_oracle_by_input.cpp
INCLUDES        := $(REPO)/components/drive_session/include

include $(REPO)/tests/host/common.mk

# The component's own flags on top of the contract's (CMakeLists.txt), as in ../oracle.
$(call obj_of,$(SRCS_UNDER_TEST) $(TEST_SRCS)): CXX_WARN += -Wswitch-enum -Wsign-compare \
    -Wnull-dereference -Wimplicit-fallthrough -Wunused-parameter -Wnon-virtual-dtor \
    -Woverloaded-virtual

EQ_BIN := $(BUILD_DIR)/equivalence/equivalence

.PHONY: equivalence mutants
equivalence:
	@mkdir -p $(dir $(EQ_BIN))
	$(CXX) -std=gnu++23 $(BASE_FLAGS) $(WARN_FLAGS) -Wswitch-enum $(addprefix -I,$(INCLUDES)) \
	  $(SRCS_UNDER_TEST) $(HERE)/equivalence.cpp -o $(EQ_BIN)
	$(EQ_BIN)

mutants:
	bash $(HERE)/mutants.sh "$(REPO)" "$(BUILD_DIR)/mutants" "$(UNITY_DIR)"
