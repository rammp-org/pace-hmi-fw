# Host L1 app for components/joystick (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
# The joystick sources are vendored legacy and are tested as they are, unmodified.

NAME := joystick
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC := $(REPO)/managed_components

SRCS_UNDER_TEST := $(REPO)/components/joystick/src/joystick.cpp
TEST_SRCS       := $(HERE)/test_joystick.cpp
THIRD_PARTY_SRCS := $(MC)/espp__logger/src/logger.cpp
INCLUDES        :=$(REPO)/components/joystick/include
SYS_INCLUDES    := $(MC)/espp__base_component/include \
                   $(MC)/espp__logger/include \
                   $(MC)/espp__format/include \
                   $(MC)/espp__format/detail/fmt/include \
                   $(MC)/espp__math/include

include $(REPO)/tests/host/common.mk
