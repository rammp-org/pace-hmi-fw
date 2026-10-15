# Host L1 app for the persisted user settings: components/settings/src/settings.cpp (the
# settings.txt codec, the clamps and the save-on-change) built as it is, against a fake storage
# layer (fake_storage.cpp: storage_path/storage_write in a temporary folder). AGENT_BRIEF host L1
# contract; rules in tests/host/common.mk. `make test` | `make coverage` | `make clean`.
# The app was written while settings.cpp was in main/; it stays here, under its manifest entry.

NAME := settings
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

MC := $(REPO)/managed_components
# TEST ONLY (dev_ai_integ_espp_prs): espp from the local espp worktree beside this one (main/idf_component.yml)
ESPP ?= $(abspath $(REPO)/../espp-test/components)

SRCS_UNDER_TEST  := $(REPO)/components/settings/src/settings.cpp
TEST_SRCS        := $(wildcard $(HERE)/test_*.cpp) $(HERE)/fake_storage.cpp
THIRD_PARTY_SRCS := $(ESPP)/logger/src/logger.cpp
INCLUDES         := $(REPO)/components/settings/include $(REPO)/components/storage/include $(HERE)
SYS_INCLUDES     := $(ESPP)/logger/include \
                    $(ESPP)/format/include \
                    $(ESPP)/format/detail/fmt/include

include $(REPO)/tests/host/common.mk
