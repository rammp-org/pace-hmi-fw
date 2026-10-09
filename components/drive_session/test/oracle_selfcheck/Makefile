# L1 host app: self-check of components/drive_session/include/drive_session_table.hpp.
# Contract: make test (exit code = verdict), make coverage. Run in WSL.
# Deviation (owner-approved 2026-10-06): L1 runs host-native g++ in WSL instead of
# the IDF linux target (TS-UNIT-01, CS-HAL-04).

NAME      := drive_session_oracle_selfcheck
HERE      := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO      ?= $(abspath $(HERE)/../../../..)
BUILD_DIR ?= $(HOME)/hmi-build/$(NAME)
UNITY_DIR ?= /mnt/c/esp/v6.0/esp-idf/components/unity/unity/src

HEADER    := $(REPO)/components/drive_session/include/drive_session_table.hpp
FPRINT    := $(REPO)/components/drive_session/include/drive_session_fingerprint.hpp
TYPES     := $(REPO)/components/drive_session/include/drive_session_types.hpp
SESSION   := $(REPO)/components/drive_session/include/drive_session.hpp
HOLD      := $(REPO)/components/drive_session/include/drive_session_hold.hpp
SRC       := $(HERE)/test_table_selfcheck.cpp
# The shared host main: runs the TEST_CASEs registered through tests/host/test_case.hpp.
HOST_DIR  := $(REPO)/tests/host
MAIN_SRC  := $(HOST_DIR)/test_main.cpp

CXX       ?= g++
CC        ?= gcc
WARN      := -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion
SAN       := -fsanitize=address,undefined -fno-omit-frame-pointer
CXXFLAGS  := -std=gnu++23 -O0 -g $(WARN) $(SAN) -I$(REPO)/components/drive_session/include -I$(HOST_DIR) -I$(UNITY_DIR)
CFLAGS    := -O0 -g $(SAN) -I$(UNITY_DIR)

.PHONY: test coverage header clean

# The header on its own, with exactly the flags the brief names (no sanitizers needed).
header:
	@mkdir -p $(BUILD_DIR)
	printf '#include "drive_session_table.hpp"\n' > $(BUILD_DIR)/header_only.cpp
	$(CXX) -std=gnu++23 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow \
	  -I$(REPO)/components/drive_session/include -fsyntax-only $(BUILD_DIR)/header_only.cpp
	printf '#include "drive_session_fingerprint.hpp"\n' > $(BUILD_DIR)/fingerprint_only.cpp
	$(CXX) -std=gnu++23 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow \
	  -I$(REPO)/components/drive_session/include -fsyntax-only $(BUILD_DIR)/fingerprint_only.cpp

$(BUILD_DIR)/unity.o: $(UNITY_DIR)/unity.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(NAME): $(SRC) $(MAIN_SRC) $(HEADER) $(TYPES) $(FPRINT) $(SESSION) $(HOLD) $(BUILD_DIR)/unity.o
	$(CXX) $(CXXFLAGS) $(EXTRA) $(SRC) $(MAIN_SRC) $(BUILD_DIR)/unity.o -o $@

test: header
	$(MAKE) -f $(HERE)/Makefile $(BUILD_DIR)/$(NAME)
	$(BUILD_DIR)/$(NAME)

coverage: header
	@mkdir -p $(BUILD_DIR)/cov
	$(MAKE) -f $(HERE)/Makefile BUILD_DIR=$(BUILD_DIR)/cov EXTRA=--coverage $(BUILD_DIR)/cov/$(NAME)
	cd $(BUILD_DIR)/cov && rm -f *.gcda && ./$(NAME)
	cd $(BUILD_DIR)/cov && gcov -b -o . $(NAME)-test_table_selfcheck.gcda > gcov.txt 2>/dev/null || \
	  gcov -b -o . $(SRC) > gcov.txt
	@awk -v want="drive_session_table.hpp" ' \
	  /^File / { f = $$0; gsub(/^File .|.$$/, "", f); keep = (index(f, want) > 0) } \
	  keep && /^Lines executed:/ { split($$2, a, "%"); sub(/.*:/, "", a[1]); l = a[1] } \
	  keep && /^Taken at least once:/ { split($$4, b, "%"); sub(/.*:/, "", b[1]); br = b[1]; \
	    printf "COVERAGE %s lines %s branches %s\n", want, l, br; keep = 0 } \
	  ' $(BUILD_DIR)/cov/gcov.txt

clean:
	rm -rf $(BUILD_DIR)

# Must-not-compile (TS-UNIT-06): one-line mutations of the header, each of which must trip
# its own static_assert text. The mutated copy goes first on the include path; the types
# header comes from the component. The unmutated twin is `make header`. The fingerprint header is
# compiled with every mutation; `fingerprint` changes only a code citation, which no other
# check reads, so only the fingerprint can catch it.
MNC := \
  'overlap|0,/when({Guard::LINK_CONNECTED}, {Guard::DRIVING_OK}), Phase::LOCKED, kF2LockStopped/s//when({}, {Guard::DRIVING_OK}), Phase::LOCKED, kF2LockStopped/|can both match' \
  'ringrest|s/acts({Action::UNLOCK_TIMER_DONE, Action::GO_DRIVE_SCREEN})/acts({Action::UNLOCK_TIMER_DONE, Action::GO_DRIVE_SCREEN, Action::RING_REST})/|contradicts the row' \
  'thenmenu|s/acts({Action::CLEAR_EXIT_DEADLINE, Action::CLEAR_THEN_MENU, Action::SHOW_EXIT_REFUSED})/acts({Action::CLEAR_EXIT_DEADLINE, Action::SHOW_EXIT_REFUSED})/|breaks a phase invariant' \
  'fingerprint|0,/"frag_lock.inc:14-18 (orig 1079-1083)"}/s//"frag_lock.inc:14-18 (orig 1079-1084)"}/|data changed'

.PHONY: mnc
mnc: header
	@mkdir -p $(BUILD_DIR)/mnc
	@fail=0; for c in $(MNC); do \
	  name=$${c%%|*}; rest=$${c#*|}; expr=$${rest%|*}; want=$${rest##*|}; \
	  d=$(BUILD_DIR)/mnc/$$name; mkdir -p $$d; \
	  sed "$$expr" $(HEADER) > $$d/drive_session_table.hpp; \
	  if cmp -s $(HEADER) $$d/drive_session_table.hpp; then echo "MNC $$name: mutation did not apply"; fail=1; continue; fi; \
	  cp $(FPRINT) $$d/; \
	  printf '#include "drive_session_table.hpp"\n#include "drive_session_fingerprint.hpp"\n' > $$d/t.cpp; \
	  if $(CXX) -std=gnu++23 -fsyntax-only -I$$d -I$(REPO)/components/drive_session/include $$d/t.cpp > $$d/log.txt 2>&1; then \
	    echo "MNC $$name: compiled (FAIL)"; fail=1; \
	  elif grep -q "$$want" $$d/log.txt; then echo "MNC $$name: rejected as expected"; \
	  else echo "MNC $$name: rejected for another reason (FAIL)"; fail=1; fi; \
	done; exit $$fail
