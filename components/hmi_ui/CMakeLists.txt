# hmi_ui: the UI island, everything the UI task draws (CS-UI-02, CS-UI-04). README.md has the
# concern, the file map and the requirements.
idf_component_register(SRCS "src/bench_pin_view.cpp"
                            "src/brightness_view.cpp"
                            "src/button_grid.cpp"
                            "src/rtps_label_view.cpp"
                            "src/seat_view.cpp"
                            "src/status_band_view.cpp"
                            "src/topbar_view.cpp"
                            "src/widget_tree.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES hmi_format
                                hmi_models
                                lvgl
                                rammp_rtps_messages
                       PRIV_REQUIRES ui)

fw_component_options(${COMPONENT_LIB})
