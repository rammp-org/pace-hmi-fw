# Host L1 app: the RTPS spec, components/hmi_rtps_spec/include/hmi_rtps_spec.hpp, and the rammp-rtps messages on the wire,
# characterised (cases RTPS-001.. in test_rtps_spec.cpp, test_rtps_cdr.cpp, test_rtps_parity.cpp;
# TS-UNIT-04, TS-UNIT-07, CS-SAF-03). Rules in tests/host/common.mk.
# `make test` | `make coverage` | `make clean`.
#
# parity.py imports scripts/rammp_rtps.py (the host codec) and writes what it sees as C++
# initialisers under the build folder; test_rtps_parity.cpp compares them with the firmware.
# espp/cdr and reflect-cpp are header-only: they come from managed_components, read only.

NAME := rtps_spec
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

SPEC_INC := $(REPO)/components/hmi_rtps_spec/include
SPEC_HDR := $(SPEC_INC)/hmi_rtps_spec.hpp
MSG_INC  := $(REPO)/external/rammp-rtps/components/rammp_rtps_messages/include
TEST_SRCS := $(HERE)/test_rtps_spec.cpp $(HERE)/test_rtps_cdr.cpp $(HERE)/test_rtps_parity.cpp
# the code under test is headers only: coverage is reported for the spec header, through the
# object of the one test file that exercises it (see rtps-cov-links)
COVER_SRCS := $(SPEC_HDR)
INCLUDES = $(SPEC_INC) $(MSG_INC) $(HERE) $(OUT)/gen
SYS_INCLUDES = $(REPO)/managed_components/espp__cdr/detail/cdr/include \
               $(REPO)/managed_components/espp__cdr/include \
               $(REPO)/managed_components/espp__reflect_cpp/detail/reflect-cpp/include

include $(REPO)/tests/host/common.mk

GEN_INC := $(OUT)/gen/py_spec.inc
$(call obj_of,$(HERE)/test_rtps_parity.cpp): $(GEN_INC)

$(GEN_INC): $(HERE)/parity.py $(REPO)/scripts/rammp_rtps.py $(SPEC_HDR) \
            $(wildcard $(MSG_INC)/messages/*.hpp)
	python3 $(HERE)/parity.py --repo $(REPO) --out $@

# gcov finds a source's data next to "<source>.o": point the header's names at the spec test's
# object, notes and counters (made again on every run: `coverage` deletes the .gcda files).
ifeq ($(COVERAGE),1)
SPEC_TU_OBJ := $(call obj_of,$(HERE)/test_rtps_spec.cpp)
SPEC_HDR_OBJ := $(call obj_of,$(SPEC_HDR))
.PHONY: rtps-cov-links
test: rtps-cov-links
rtps-cov-links: $(BIN)
	@mkdir -p $(dir $(SPEC_HDR_OBJ))
	@ln -sf $(SPEC_TU_OBJ) $(SPEC_HDR_OBJ)
	@ln -sf $(SPEC_TU_OBJ:.o=.gcno) $(SPEC_HDR_OBJ:.o=.gcno)
	@ln -sf $(SPEC_TU_OBJ:.o=.gcda) $(SPEC_HDR_OBJ:.o=.gcda)
endif
