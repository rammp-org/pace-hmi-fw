# The joystick calibration record (file codec, plausibility) and the calibration run as a
# plain C++ model. No LVGL, no storage: main/joystick_cal.cpp is the view and the file I/O.
idf_component_register(
  SRCS "src/cal_record.cpp" "src/cal_run.cpp"
  INCLUDE_DIRS "include"
  REQUIRES format)
