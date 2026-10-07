# hmi_ui: the UI island, everything the UI task draws (CS-UI-02, CS-UI-04). README.md has the
# concern, the file map and the requirements.
idf_component_register(SRCS "src/display_flip.cpp"
                            "src/fps_meter.cpp"
                            "src/overdraw.cpp"
                            "src/rtps_label_view.cpp"
                            "src/status_band_view.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_ppa
                                logger
                                lvgl
                       PRIV_REQUIRES esp_mm
                                     esp_timer
                                     rammp_rtps_messages
                                     ui)

# CS-LNG-02. The top-level CMakeLists includes fw_standards.cmake only after project(), and
# this file runs inside project(), so `if(COMMAND fw_component_options)` would be false here
# and the options silently missing (compile_commands.json, 2026-10-06). Include it directly.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/fw_standards.cmake)
fw_component_options(${COMPONENT_LIB})
