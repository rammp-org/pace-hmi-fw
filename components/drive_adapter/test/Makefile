# Host L1 app for components/drive_adapter: the adapter's own contract (DAD-001..006). Contract
# and rules in tests/host/common.mk: `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).
# The adapter against the firmware's behaviour: tests/host/drive_golden (GLD-001..004).

NAME := drive_adapter
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC := $(REPO)/managed_components

# The adapter is header-only: the test instantiates it.
SRCS_UNDER_TEST  := $(REPO)/components/drive_session/src/drive_session.cpp
TEST_SRCS        := $(HERE)/test_drive_adapter.cpp
COVER_SRCS       := $(HERE)/test_drive_adapter.cpp
THIRD_PARTY_SRCS := $(MC)/espp__logger/src/logger.cpp
INCLUDES         := $(REPO)/components/drive_adapter/include \
                    $(REPO)/components/drive_session/include
SYS_INCLUDES     := $(MC)/espp__logger/include \
                    $(MC)/espp__format/include \
                    $(MC)/espp__format/detail/fmt/include

include $(REPO)/tests/host/common.mk

# fw_component_options' warning set (CS-LNG-02), on top of common.mk's.
$(call obj_of,$(SRCS_UNDER_TEST) $(TEST_SRCS)): CXX_WARN += -Wsign-compare -Wnull-dereference \
    -Wimplicit-fallthrough -Wunused-parameter -Wnon-virtual-dtor -Woverloaded-virtual
