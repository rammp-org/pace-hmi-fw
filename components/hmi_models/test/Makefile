# Host L1 app for components/hmi_models (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
#
# legacy_ui.cpp is the pre-move code, kept as the oracle the goldens were recorded from and
# the differential cases (MOD-008, MOD-009) compare against.

NAME := hmi_models
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

COMP := $(REPO)/components/hmi_models

SRCS_UNDER_TEST := $(COMP)/src/grid.cpp \
                   $(COMP)/src/pin.cpp
TEST_SRCS       := $(HERE)/test_hmi_models.cpp \
                   $(HERE)/legacy_ui.cpp
INCLUDES        := $(HERE) $(COMP)/include

include $(REPO)/tests/host/common.mk
