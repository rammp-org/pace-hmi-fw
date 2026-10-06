# Fixture app for `run.py selftest`: one case per verdict path of the host L1 contract.
# Not in the manifest: most of its cases are meant to fail.

NAME := verdicts
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../../..)

TEST_SRCS := $(HERE)/test_verdicts.cpp

include $(REPO)/tests/host/common.mk
