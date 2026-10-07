# Host L1 app for the joystick calibration (AGENT_BRIEF host L1 contract; rules in
# tests/host/common.mk). `make test` | `make coverage` | `make clean`.
#
# Two layers are under test:
#   components/joystick_cal/src   the component, through its own headers (test_cal_*.cpp)
#   main/joystick_cal.cpp         the LVGL view and file I/O, through main/joystick_cal.hpp
#                                 only (test_joystick_cal.cpp: the characterisation cases)
# main/joystick_cal.cpp is compiled as it is, against two test-only fakes in fakes/:
#   lvgl.h -> fake_lvgl.hpp/.cpp  the dozen lv_* calls the calibration run makes; found before
#                                 the real lvgl.h because LVGL is not on this include path
#   fake_storage.cpp              storage_path()/storage_write() on a host temp folder
# Logging is the real espp::Logger. The characterisation cases use only main/joystick_cal.hpp,
# so they pin the behaviour the rest of the firmware sees, whatever the files look like inside.

NAME := joystick_cal
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC  := $(REPO)/managed_components
CMP := $(REPO)/components/joystick_cal

SRCS_UNDER_TEST  := $(CMP)/src/cal_record.cpp \
                    $(CMP)/src/cal_run.cpp \
                    $(REPO)/main/joystick_cal.cpp
TEST_SRCS        := $(HERE)/test_joystick_cal.cpp \
                    $(HERE)/test_cal_record.cpp \
                    $(HERE)/test_cal_run.cpp \
                    $(HERE)/fakes/fake_lvgl.cpp \
                    $(HERE)/fakes/fake_storage.cpp
THIRD_PARTY_SRCS := $(MC)/espp__logger/src/logger.cpp
INCLUDES         := $(HERE)/fakes $(CMP)/include $(REPO)/main
SYS_INCLUDES     := $(MC)/espp__base_component/include \
                    $(MC)/espp__logger/include \
                    $(MC)/espp__format/include \
                    $(MC)/espp__format/detail/fmt/include

include $(REPO)/tests/host/common.mk
