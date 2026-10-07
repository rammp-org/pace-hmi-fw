# hmi_ui: the UI island, everything the UI task draws (CS-UI-02, CS-UI-04). README.md has the
# concern, the file map and the requirements.
idf_component_register(SRCS "src/brightness_view.cpp"
                            "src/button_grid.cpp"
                            "src/rtps_label_view.cpp"
                            "src/status_band_view.cpp"
                            "src/topbar_view.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES hmi_models
                                lvgl
                       PRIV_REQUIRES hmi_format
                                     rammp_rtps_messages
                                     ui)

# CS-LNG-02. Today the top-level CMakeLists includes fw_standards.cmake only after project(),
# while this file runs inside project(), so fw_component_options does not exist yet here and
# the `if(COMMAND ...)` guard the other components use skips it silently. Until the repo-wide
# fix (branch dev_ai_refactor_fwopts) defines it before the components are processed, include
# it here. Once it is defined this block does nothing and the include can be dropped.
if(NOT COMMAND fw_component_options)
  include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/fw_standards.cmake)
endif()
fw_component_options(${COMPONENT_LIB})
