# hmi_format: pure text formatting for the screens (no LVGL, host-tested in test/).
idf_component_register(SRCS "src/diag.cpp"
                            "src/speed.cpp"
                            "src/stepper.cpp"
                            "src/topbar.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src")

if(COMMAND fw_component_options)
  fw_component_options(${COMPONENT_LIB})
endif()
