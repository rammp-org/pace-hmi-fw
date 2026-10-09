# Host L1 app for components/drive_session, on demand: the full-product transition-table oracle
# (TS-UNIT-08), DRV-001..012. Out of CI by the owner's decision G2 (hazard-decisions.md;
# hazard-c1-spec.md E5): `make full` (or `make test`) runs it. Contract and rules in
# tests/host/common.mk: `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).

NAME := drive_session_full
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../../..)

SRCS_UNDER_TEST := $(REPO)/components/drive_session/src/drive_session.cpp
TEST_SRCS       := $(HERE)/test_drive_session_oracle_full.cpp
INCLUDES        := $(REPO)/components/drive_session/include

include $(REPO)/tests/host/common.mk

# The component's own flags on top of the contract's (CMakeLists.txt), as in ../oracle.
$(call obj_of,$(SRCS_UNDER_TEST) $(TEST_SRCS)): CXX_WARN += -Wswitch-enum -Wsign-compare \
    -Wnull-dereference -Wimplicit-fallthrough -Wunused-parameter -Wnon-virtual-dtor \
    -Woverloaded-virtual

.PHONY: full
full: test
