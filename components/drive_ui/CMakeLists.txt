# drive_ui: the drive UI on the UI task: the drive adapter's port (DrivePort, it sends the
# DriveCommand), the padlock and the stick gate it acts on (DriveUi), the stick button's edges
# (StickButton). Safety-relevant (CS-SAF-01). README.md has the files and the requirements.
#
# DrivePort (include/drive_ui/drive_port.hpp) is header-only and needs `ui` and `esp_timer`,
# private here: a component that includes it requires them too (main, hmi_ui's ui_app.cpp).
idf_component_register(SRCS "src/drive_ui.cpp"
                            "src/stick_button.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES drive_adapter
                                drive_session
                                lvgl
                                rammp_rtps_messages
                                stick
                       PRIV_REQUIRES esp_timer
                                     ui)

fw_component_options(${COMPONENT_LIB})

# Safety code: a switch over an enum names every enumerator, and its default goes to the
# safe state (CS-TYP-06).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch-enum -Werror=switch-enum)
