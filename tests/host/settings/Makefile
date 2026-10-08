# Host L1 app for the persisted user settings: components/settings/src/settings.cpp (the
# settings.txt codec, the clamps and the save-on-change) built as it is, against a fake storage
# layer (fake_storage.cpp: storage_path/storage_write in a temporary folder). AGENT_BRIEF host L1
# contract; rules in tests/host/common.mk. `make test` | `make coverage` | `make clean`.
# The app was written while settings.cpp was in main/; it stays here, under its manifest entry.

NAME := settings
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC := $(REPO)/managed_components

SRCS_UNDER_TEST  := $(REPO)/components/settings/src/settings.cpp
TEST_SRCS        := $(wildcard $(HERE)/test_*.cpp) $(HERE)/fake_storage.cpp
THIRD_PARTY_SRCS := $(MC)/espp__logger/src/logger.cpp
INCLUDES         := $(REPO)/components/settings/include $(REPO)/components/storage/include $(HERE)
SYS_INCLUDES     := $(MC)/espp__logger/include \
                    $(MC)/espp__format/include \
                    $(MC)/espp__format/detail/fmt/include

include $(REPO)/tests/host/common.mk
