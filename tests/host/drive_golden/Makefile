# Host L1 app: the drive goldens (GLD-*), app-main-shrink.md S1 / V5, hazard-c1-spec.md §5.2.
# Contract and rules in tests/host/common.mk: `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).
#
# golden_port.txt and golden_raw.txt are the retired GLD-001/002/004's frozen logs, kept as history
# (hazard-c1-spec.md E1); nothing reads or writes them. The C1 goldens' expected data is in
# goldens_c1.cpp, written from the spec.

NAME := drive_golden
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

# The code under test: drive_ui's DrivePort (components/drive_ui/include/drive_ui/drive_port.hpp,
# moved from main/frag_drive.inc), compiled verbatim by main_unit.cpp, and
# components/drive_adapter's DriveAdapter, instantiated there.
SRCS_UNDER_TEST := $(HERE)/main_unit.cpp \
                   $(REPO)/components/drive_session/src/drive_session.cpp
TEST_SRCS       := $(HERE)/world.cpp $(HERE)/scripts.cpp $(HERE)/goldens.cpp \
                   $(HERE)/goldens_c1.cpp $(HERE)/test_drive_golden.cpp
COVER_SRCS      := $(HERE)/main_unit.cpp
# shim/ first: its logger.hpp stands in for espp's, its lvgl.h, ui.h, esp_timer.h and
# messages/*.hpp for the firmware's (main_unit_shims.hpp).
INCLUDES        := $(HERE)/shim $(HERE) $(REPO)/components/drive_adapter/include \
                   $(REPO)/components/drive_session/include $(REPO)/components/drive_ui/include \
                   $(REPO)/components/stick/include

include $(REPO)/tests/host/common.mk
