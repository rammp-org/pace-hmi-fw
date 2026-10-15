# Host L1 app for the stick's mapping engine: espp's joystick (third party) and the twist's own
# range mapper, as components/stick wires them (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
# Was components/joystick/test, the vendored espp fork's app (L1-JOY); the fork is gone.

NAME := joystick
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)
# TEST ONLY (dev_ai_integ_espp_prs): espp from the local espp worktree beside this one (main/idf_component.yml)
ESPP ?= $(abspath $(REPO)/../espp-test/components)

SRCS_UNDER_TEST := $(REPO)/components/stick/src/mapping_selftest.cpp
TEST_SRCS       := $(HERE)/test_joystick.cpp
THIRD_PARTY_SRCS := $(ESPP)/joystick/src/joystick.cpp $(ESPP)/logger/src/logger.cpp
INCLUDES        := $(REPO)/components/stick/include
SYS_INCLUDES    := $(ESPP)/joystick/include \
                   $(ESPP)/base_component/include \
                   $(ESPP)/logger/include \
                   $(ESPP)/format/include \
                   $(ESPP)/format/detail/fmt/include \
                   $(ESPP)/math/include

include $(REPO)/tests/host/common.mk
