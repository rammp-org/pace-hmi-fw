# hmi_ui: the UI island, everything the UI task draws (CS-UI-02, CS-UI-04). README.md has the
# concern, the file map and the requirements.
idf_component_register(SRCS "src/rtps_label_view.cpp"
                            "src/status_band_view.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl
                       PRIV_REQUIRES rammp_rtps_messages
                                     ui)

fw_component_options(${COMPONENT_LIB})
