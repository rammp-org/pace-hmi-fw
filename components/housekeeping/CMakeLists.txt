# housekeeping: the board's IMU, battery monitor, RTC and system clock (CS-LAY-02).
# README.md says what is in it.
idf_component_register(SRCS "src/housekeeping.cpp"
                            "src/system_clock.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES filters logger m5stack-tab5 rammp_rtps_messages task
                       PRIV_REQUIRES esp_timer hmi_format)

fw_component_options(${COMPONENT_LIB})
