# Host app for scripts/mcb_sim_logic.py (AGENT_BRIEF "host L1 app contract"): `make test` runs the
# simulated MCB's decision-logic cases (SIM-001..) - no network, no board, no tkinter - and prints
# Unity's case lines and summary; the exit code is the verdict. Needs external/rammp-rtps (the
# enum values come from the spec headers through scripts/rammp_rtps.py).
#
# Python, not C++: there is nothing to compile, so BUILD_DIR and UNITY_DIR are unused and
# `make coverage` runs the same cases without a gcov report (no COVERAGE line).
# No manifest entry yet: run.py's L1 check wants TEST_CASE in C/C++ sources (as tests/host/guard_*).

NAME := mcb_sim
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)
PYTHON ?= python3
export PYTHONDONTWRITEBYTECODE := 1

.PHONY: test coverage clean
.DEFAULT_GOAL := test

test:
	$(PYTHON) $(REPO)/scripts/rtps_mcb_sim.py selftest

coverage: test

clean:
