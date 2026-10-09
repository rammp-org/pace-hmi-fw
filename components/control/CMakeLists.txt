# control: the control island, the "Read ADC" task that reads the joystick and runs its stick
# pipeline (CS-LAY-02). Header-only: StickIsland is a template instantiated in main's unit.
# README.md says what is in it.
idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES adc_window espp_adc filters hmi_rtps_spec logger stick task)
