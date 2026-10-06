# Host L1 app for components/drive_session: the transition-table oracle (TS-UNIT-08) and the
# stick gate. Contract and rules in tests/host/common.mk: `make test` | `make coverage` |
# `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).
#
# Coverage without the sanitizers (their instrumentation adds branches gcov counts):
#   make coverage SAN_FLAGS= BUILD_DIR=$HOME/hmi-build/drive_session_nosan

NAME := drive_session
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../../..)

SRCS_UNDER_TEST := $(REPO)/components/drive_session/src/drive_session.cpp
TEST_SRCS       := $(HERE)/test_drive_session_oracle.cpp
INCLUDES        := $(REPO)/components/drive_session/include

include $(REPO)/tests/host/common.mk

# The component's own flags on top of the contract's (CMakeLists.txt): -Wswitch-enum for safety
# code (CS-TYP-06), and the rest of fw_component_options' warning set (CS-LNG-02).
$(call obj_of,$(SRCS_UNDER_TEST) $(TEST_SRCS)): CXX_WARN += -Wswitch-enum -Wsign-compare \
    -Wnull-dereference -Wimplicit-fallthrough -Wunused-parameter -Wnon-virtual-dtor \
    -Woverloaded-virtual
