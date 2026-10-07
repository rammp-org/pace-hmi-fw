# hmi_ui: the UI island, everything the UI task draws (CS-UI-02, CS-UI-04). README.md has the
# concern, the file map and the requirements.
#
# Header-only until the first view's source lands; fw_component_options() comes with it
# (it sets PRIVATE options, which an INTERFACE library cannot take).
idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES lvgl)
