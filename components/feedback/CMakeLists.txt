# feedback: plays the user's haptic and sound cues (CS-LAY-02). README.md says what is in it.
idf_component_register(SRCS "src/click_sound.cpp"
                            "src/da7280_bench.cpp"
                            "src/feedback.cpp"
                            "src/haptics.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES base_component base_peripheral drv2605 i2c logger
                       PRIV_REQUIRES esp_timer format)

fw_component_options(${COMPONENT_LIB})
