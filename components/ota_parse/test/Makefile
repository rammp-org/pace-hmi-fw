# Host L1 app for the OTA and firmware-info parsers (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
#
# The sources under test are components/ota_parse/src. legacy_ota_parse.cpp is the pre-move
# code from main/, kept as the oracle the goldens were recorded from; every golden runs
# against both. It includes github_ota.hpp and fw_info.hpp (components/ota) for its structs (both
# are plain C++), and espp's fmt (header-only) as the firmware formats its messages.
# cJSON is the firmware's own copy (managed_components/espressif__cjson), built as C with gcc
# and the sanitizers, with the Kconfig default nesting limit (CONFIG_CJSON_NESTING_LIMIT=1000).

NAME := ota_parse
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

COMP      := $(REPO)/components/ota_parse
CJSON_DIR := $(REPO)/managed_components/espressif__cjson/cJSON
FMT_DIR   := $(REPO)/managed_components/espp__format

SRCS_UNDER_TEST := $(COMP)/src/fw_record.cpp \
                   $(COMP)/src/image_header.cpp \
                   $(COMP)/src/marker_search.cpp \
                   $(COMP)/src/release_list.cpp
TEST_SRCS       := $(HERE)/test_ota_parse.cpp \
                   $(HERE)/subject_component.cpp \
                   $(HERE)/subject_legacy.cpp \
                   $(HERE)/legacy_ota_parse.cpp
INCLUDES        := $(HERE) $(COMP)/include $(REPO)/components/ota/include
SYS_INCLUDES    := $(CJSON_DIR) $(FMT_DIR)/include $(FMT_DIR)/detail/fmt/include
COVER_SRCS      := $(SRCS_UNDER_TEST)

include $(REPO)/tests/host/common.mk

CJSON_DEFS := -DCJSON_NESTING_LIMIT=1000 -DCJSON_CIRCULAR_LIMIT=10000
CJSON_OBJ  := $(OUT)/obj/cjson/cJSON.c.o
OBJS += $(CJSON_OBJ)
$(BIN): $(CJSON_OBJ)
$(CJSON_OBJ): $(CJSON_DIR)/cJSON.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_UNITY) $(CJSON_DEFS) -c $< -o $@
