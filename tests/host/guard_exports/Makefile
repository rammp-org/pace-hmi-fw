# Host app for tools/guards/exports.py (AGENT_BRIEF "host L1 app contract"): `make test` runs the
# tool's selftest on inline samples - no toolchain, no build folder, no board - and prints
# Unity's case lines and summary; the exit code is the verdict. See tools/guards/README.md.
#
# Python, not C++: there is nothing to compile, so BUILD_DIR and UNITY_DIR are unused and
# `make coverage` runs the same cases without a gcov report (no COVERAGE line).
# No manifest entry yet: run.py's L1 check wants TEST_CASE in C/C++ sources (README, Parked).

NAME := guard_exports
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)
PYTHON ?= python3
export PYTHONDONTWRITEBYTECODE := 1

.PHONY: test coverage clean
.DEFAULT_GOAL := test

test:
	$(PYTHON) $(REPO)/tools/guards/exports.py selftest

coverage: test

clean:
