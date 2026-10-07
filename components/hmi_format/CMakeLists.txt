# hmi_format: pure text formatting for the screens (no LVGL, host-tested in test/).
idf_component_register(SRCS "src/about.cpp"
                            "src/diag.cpp"
                            "src/net.cpp"
                            "src/speed.cpp"
                            "src/stepper.cpp"
                            "src/topbar.cpp"
                            "src/update.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src")

fw_component_options(${COMPONENT_LIB})
