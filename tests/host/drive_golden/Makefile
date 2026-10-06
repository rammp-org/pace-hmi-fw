# Host L1 app: the drive goldens (GLD-*), app-main-shrink.md S1 / V5. Contract and rules in
# tests/host/common.mk: `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).
#
# golden_port.txt and golden_raw.txt are frozen expected values (test_drive_golden.cpp): there is
# no target that writes them. On a mismatch the actual logs land in $(OUT) for a diff.

NAME := drive_golden
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

# The code under test: main/frag_drive.inc, compiled verbatim by main_unit.cpp, and
# components/drive_adapter's DriveAdapter, instantiated there and in fake_port.cpp.
SRCS_UNDER_TEST := $(HERE)/main_unit.cpp $(HERE)/fake_port.cpp \
                   $(REPO)/components/drive_session/src/drive_session.cpp
TEST_SRCS       := $(HERE)/world.cpp $(HERE)/scripts.cpp $(HERE)/test_drive_golden.cpp
COVER_SRCS      := $(HERE)/main_unit.cpp $(HERE)/fake_port.cpp
# shim/ first: its logger.hpp stands in for espp's.
INCLUDES        := $(HERE)/shim $(HERE) $(REPO)/components/drive_adapter/include \
                   $(REPO)/components/drive_session/include $(REPO)/main

include $(REPO)/tests/host/common.mk

export GOLDEN_DIR        := $(HERE)
export GOLDEN_ACTUAL_DIR := $(OUT)
