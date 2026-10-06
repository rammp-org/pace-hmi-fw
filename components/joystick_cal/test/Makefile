# Host L1 app for the joystick calibration (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
#
# main/joystick_cal.cpp is compiled as it is, against two test-only fakes in fakes/:
#   lvgl.h + fake_lvgl.cpp        the dozen lv_* calls the calibration run makes; found before
#                                 the real lvgl.h because LVGL is not on this include path
#   fake_storage.cpp              storage_path()/storage_write() on a host temp folder
# Logging is the real espp::Logger. The cases use only main/joystick_cal.hpp, so they pin the
# behaviour the rest of the firmware sees, whatever the file looks like inside.

NAME := joystick_cal
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC := $(REPO)/managed_components

SRCS_UNDER_TEST  := $(REPO)/main/joystick_cal.cpp
TEST_SRCS        := $(HERE)/test_joystick_cal.cpp \
                    $(HERE)/fakes/fake_lvgl.cpp \
                    $(HERE)/fakes/fake_storage.cpp
THIRD_PARTY_SRCS := $(MC)/espp__logger/src/logger.cpp
INCLUDES         := $(HERE)/fakes $(REPO)/main
SYS_INCLUDES     := $(MC)/espp__base_component/include \
                    $(MC)/espp__logger/include \
                    $(MC)/espp__format/include \
                    $(MC)/espp__format/detail/fmt/include

include $(REPO)/tests/host/common.mk

# Commit 1 only (characterisation of the file as it is): main/joystick_cal.cpp indexes
# std::array with int in 20 places, which -Wsign-conversion rejects. The IDF build does not
# enable that warning, and the file is not changed in this commit, so its object alone builds
# without it. The refactor that follows makes the file clean and deletes this line.
$(call obj_of,$(REPO)/main/joystick_cal.cpp): CXX_WARN := $(WARN_FLAGS) -Wno-sign-conversion
