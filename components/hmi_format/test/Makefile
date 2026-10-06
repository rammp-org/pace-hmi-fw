# Host L1 app for components/hmi_format (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
#
# legacy_format.cpp is the pre-move code, kept as the oracle the goldens were recorded from.
# It prints with LVGL's own printf, so LVGL's lv_sprintf_builtin.c (the one the firmware runs,
# CONFIG_LV_USE_BUILTIN_SPRINTF=1) is compiled here as third-party C with LVGL's defaults
# (LV_CONF_SKIP): only %d and %s are used, which no lv_conf option changes.

NAME := hmi_format
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

LVGL_DIR := $(REPO)/managed_components/lvgl__lvgl

SRCS_UNDER_TEST :=
TEST_SRCS       := $(HERE)/test_hmi_format.cpp \
                   $(HERE)/legacy_format.cpp \
                   $(HERE)/lvgl_host_shim.cpp
INCLUDES        := $(HERE)

include $(REPO)/tests/host/common.mk

# LVGL's printf: C, built with gcc and the sanitizers like Unity; its warnings are not ours.
LVGL_PRINTF_OBJ := $(OUT)/obj/lvgl/lv_sprintf_builtin.c.o
OBJS += $(LVGL_PRINTF_OBJ)
$(BIN): $(LVGL_PRINTF_OBJ)
$(LVGL_PRINTF_OBJ): $(LVGL_DIR)/src/stdlib/builtin/lv_sprintf_builtin.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_UNITY) -DLV_CONF_SKIP -c $< -o $@
