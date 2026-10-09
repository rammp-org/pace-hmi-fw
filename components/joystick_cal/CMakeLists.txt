# The joystick calibration record (file codec, plausibility) and the calibration run as a
# plain C++ model. No LVGL, no storage: main/joystick_cal.cpp is the view and the file I/O.
idf_component_register(
  SRCS "src/cal_record.cpp" "src/cal_run.cpp"
  INCLUDE_DIRS "include"
  REQUIRES format stick)

fw_component_options(${COMPONENT_LIB})

# joystick_cal is safety-relevant (it feeds the stick, refactor.md §2.3): a switch over an enum
# names every enumerator (CS-TYP-06), as in stick and post.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch-enum -Werror=switch-enum)
