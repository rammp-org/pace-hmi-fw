# hmi_models: screen logic as plain C++ models (no LVGL, host-tested in test/).
idf_component_register(SRCS "src/grid.cpp"
                            "src/pin.cpp"
                       INCLUDE_DIRS "include")

fw_component_options(${COMPONENT_LIB})
