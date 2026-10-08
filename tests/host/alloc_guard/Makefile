# Host L1 app for the host allocation guard itself (tests/host/alloc_guard.{hpp,cpp}, TS-DET-08):
# the guard counts malloc and operator new while armed. Rules in tests/host/common.mk.
# `make test` | `make coverage` | `make clean`.

NAME := alloc_guard
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

ALLOC_GUARD := 1

TEST_SRCS := $(HERE)/test_alloc_guard.cpp
# the guard is a test helper: nothing of ours to report coverage for
COVER_SRCS := $(TEST_SRCS)

include $(REPO)/tests/host/common.mk
