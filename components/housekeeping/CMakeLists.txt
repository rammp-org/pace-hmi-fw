# housekeeping: reads the board's IMU, battery monitor and RTC on one slow task (CS-LAY-02).
# README.md says what is in it.
idf_component_register(SRCS "src/housekeeping.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES filters m5stack-tab5 task
                       PRIV_REQUIRES esp_timer)

fw_component_options(${COMPONENT_LIB})
