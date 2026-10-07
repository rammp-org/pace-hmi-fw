# drive_session: the drive session state machine (lock, ask, unlock, drive, exit) and its
# AS-IS transition table. Safety-relevant (CS-SAF-01). Pure C++: no LVGL, no ESP-IDF.
# The host L1 oracle app is test/oracle (TS-UNIT-08); the table self-check is
# test/oracle_selfcheck.
idf_component_register(
  SRCS "src/drive_session.cpp"
  INCLUDE_DIRS "include")

fw_component_options(${COMPONENT_LIB})

# Safety code: a switch over an enum names every enumerator, and its default goes to the
# safe state (CS-TYP-06).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch-enum -Werror=switch-enum)
