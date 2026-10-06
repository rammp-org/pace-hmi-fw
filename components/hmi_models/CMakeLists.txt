# hmi_models: screen logic as plain C++ models (no LVGL, host-tested in test/).
idf_component_register(SRCS "src/grid.cpp"
                            "src/pin.cpp"
                       INCLUDE_DIRS "include")

if(COMMAND fw_component_options)
  fw_component_options(${COMPONENT_LIB})
endif()
