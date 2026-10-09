# drive_adapter: the drive session's adapter on the UI task: samples what a decision needs, steps
# the session (components/drive_session) and performs its actions through a port. Safety-relevant
# (CS-SAF-01). Pure C++ over its port: no LVGL, no ESP-IDF, nothing from main/.
#
# Header-only: DriveAdapter is a template over its port (CS-SAF-08, direct calls), so it is
# compiled in the unit that instantiates it (main/main.cpp, with hmi::ui::DrivePort<DriveUi>) and in the host
# apps (test/, tests/host/drive_golden) with the full fw-standards warning set. An INTERFACE
# library takes no PRIVATE options, so fw_component_options() does not apply here.
idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES drive_session logger stick)
