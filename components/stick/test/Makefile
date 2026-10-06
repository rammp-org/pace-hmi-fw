# Host L1 app for components/stick (AGENT_BRIEF host L1 contract; rules in tests/host/common.mk).
#   make test       build and run; exit code 0 = PASS
#   make coverage   the same with gcov; COVERAGE lines for the stick pipeline
#   make golden     build gen_golden and write $(BUILD_DIR)/golden_stick.inc from the legacy
#                   harness. The committed golden_stick.inc is FROZEN: regenerating it is a
#                   reviewed act, never a way to make a test pass (CORE never-list).
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).

NAME := stick
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC := $(REPO)/managed_components

SRCS_UNDER_TEST := $(wildcard $(REPO)/components/stick/src/*.cpp)
TEST_SRCS       := $(HERE)/test_stick.cpp $(HERE)/legacy_adc_cycle.cpp
# components/joystick is the mapping engine today; it is linked as it is (vendored legacy,
# tested by its own app), and espp's logger is third party.
THIRD_PARTY_SRCS := $(REPO)/components/joystick/src/joystick.cpp $(MC)/espp__logger/src/logger.cpp
INCLUDES        := $(REPO)/components/stick/include $(HERE)
SYS_INCLUDES    := $(REPO)/components/joystick/include \
                   $(MC)/espp__base_component/include \
                   $(MC)/espp__logger/include \
                   $(MC)/espp__format/include \
                   $(MC)/espp__format/detail/fmt/include \
                   $(MC)/espp__math/include
COVER_SRCS      := $(SRCS_UNDER_TEST) $(wildcard $(REPO)/components/stick/include/stick/*.hpp)

include $(REPO)/tests/host/common.mk

# The legacy harness is a verbatim copy of firmware code that was never built with
# -Wconversion: `(level - 1) * (0.29f / 9.0f)` converts int to float implicitly. Its copy must
# stay verbatim, so that one file builds without -Wconversion (not without
# -Werror). The extracted code builds with the full set.
$(call obj_of,$(HERE)/legacy_adc_cycle.cpp): CXX_WARN := $(WARN_FLAGS) -Wno-conversion

GEN_OBJS := $(call obj_of,$(HERE)/gen_golden.cpp $(HERE)/legacy_adc_cycle.cpp) $(TP_OBJS)
GEN_BIN  := $(OUT)/gen_golden

.PHONY: golden
golden: $(GEN_BIN)
	$(GEN_BIN) > $(BUILD_DIR)/golden_stick.inc
	@echo "GOLDEN $(BUILD_DIR)/golden_stick.inc ($$(grep -c '^  {' $(BUILD_DIR)/golden_stick.inc) rows)"

$(GEN_BIN): $(GEN_OBJS)
	$(CXX) $(SAN_FLAGS) $(GEN_OBJS) -o $@
