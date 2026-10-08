# hmi_ui: the UI island, everything the UI task draws (CS-UI-02, CS-UI-04). README.md has the
# concern, the file map and the requirements.
idf_component_register(SRCS "src/about_view.cpp"
                            "src/actions_view.cpp"
                            "src/bench_pin_view.cpp"
                            "src/brightness_view.cpp"
                            "src/button_grid.cpp"
                            "src/diagnostics_view.cpp"
                            "src/display_flip.cpp"
                            "src/drive_band_view.cpp"
                            "src/drive_ui.cpp"
                            "src/fps_meter.cpp"
                            "src/hold_gesture.cpp"
                            "src/internet_view.cpp"
                            "src/joystick_view.cpp"
                            "src/log_view.cpp"
                            "src/nav_view.cpp"
                            "src/on_demand_screens.cpp"
                            "src/overdraw.cpp"
                            "src/refusal_view.cpp"
                            "src/resident_chrome.cpp"
                            "src/rtps_label_view.cpp"
                            "src/rtps_ui_bridge.cpp"
                            "src/seat_view.cpp"
                            "src/setting_subjects.cpp"
                            "src/settings_view.cpp"
                            "src/status_band_view.cpp"
                            "src/stick_button.cpp"
                            "src/topbar_view.cpp"
                            "src/ui_build.cpp"
                            "src/ui_island.cpp"
                            "src/ui_poll.cpp"
                            "src/update_view.cpp"
                            "src/widget_tree.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES drive_adapter
                                drive_session
                                esp_driver_ppa
                                hmi_format
                                hmi_models
                                logger
                                lvgl
                                ota_parse
                                rammp_rtps_messages
                                settings
                                stick
                                task
                       PRIV_REQUIRES esp_mm
                                     esp_timer
                                     ui)

fw_component_options(${COMPONENT_LIB})
