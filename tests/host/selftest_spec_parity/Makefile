# Host L1 app: the self-test check table and its host-script copy agree (TS-UNIT-04,
# CS-TYP-03). Rules in tests/host/common.mk. `make test` | `make coverage` | `make clean`.
#
# rows.py turns golden_rows.json, and the host scripts' parse of main/selftest_spec.h, into
# C++ initialisers under the build folder; test_selftest_spec.cpp compares both with the
# firmware's own table. To re-characterise after a deliberate table change (never to
# silence this test): python3 rows.py golden, and review the diff of golden_rows.json.

NAME := selftest_spec
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

TEST_SRCS := $(HERE)/test_selftest_spec.cpp
# the table is a header: no source of its own to report
COVER_SRCS := $(TEST_SRCS)
INCLUDES = $(REPO)/main $(OUT)/gen

include $(REPO)/tests/host/common.mk

GEN_INCS := $(OUT)/gen/golden_rows.inc $(OUT)/gen/script_rows.inc
$(call obj_of,$(TEST_SRCS)): $(GEN_INCS)

$(OUT)/gen/golden_rows.inc: $(HERE)/golden_rows.json $(HERE)/rows.py
	python3 $(HERE)/rows.py emit --repo $(REPO) --source golden --out $@

$(OUT)/gen/script_rows.inc: $(REPO)/main/selftest_spec.h $(REPO)/scripts/rammp_rtps.py $(HERE)/rows.py
	python3 $(HERE)/rows.py emit --repo $(REPO) --source script --out $@
