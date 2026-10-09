# Host L1 app for components/post, the quick POST evaluator (AGENT_BRIEF host L1 contract; rules
# in tests/host/common.mk). `make test` | `make coverage` | `make clean`.
# spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner, 2026-10-06).
#
# test_idf_parity.cpp reads esp_system.h and esp_flash_partitions.h as text from the IDF tree
# that UNITY_DIR is in, to prove the mirrored enums match (POST-030, POST-031).

NAME := post
HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO ?= $(abspath $(HERE)/../../..)

COMP := $(REPO)/components/post

SRCS_UNDER_TEST := $(COMP)/src/post.cpp $(COMP)/src/runner.cpp $(COMP)/src/indicator.cpp
TEST_SRCS       := $(HERE)/test_post.cpp \
                   $(HERE)/test_idf_parity.cpp \
                   $(HERE)/test_runner.cpp
INCLUDES        := $(HERE) $(COMP)/include $(REPO)/components/stick/include
# POST-048: the accumulator and the runner under the host allocation guard (TS-DET-08).
ALLOC_GUARD     := 1

include $(REPO)/tests/host/common.mk

# Safety code: every switch over an enum names every enumerator (CS-TYP-06), as in the
# component's CMakeLists.txt.
$(call obj_of,$(SRCS_UNDER_TEST)): CXX_WARN := $(WARN_FLAGS) -Wswitch-enum

IDF_ROOT ?= $(abspath $(UNITY_DIR)/../../../..)
$(call obj_of,$(HERE)/test_idf_parity.cpp): CXXFLAGS_ALL += -DPOST_IDF_ROOT='"$(IDF_ROOT)"'

# POST-051 reads main/selftest.cpp's reset_reason_name, to keep the copy in step.
$(call obj_of,$(HERE)/test_runner.cpp): CXXFLAGS_ALL += -DPOST_REPO_ROOT='"$(REPO)"'
