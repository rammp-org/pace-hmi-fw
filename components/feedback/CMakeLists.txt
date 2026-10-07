# feedback: plays the user's haptic and sound cues (CS-LAY-02). README.md says what is in it.
idf_component_register(SRCS "src/da7280_bench.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES base_peripheral i2c logger)

if(COMMAND fw_component_options)
  fw_component_options(${COMPONENT_LIB})
endif()
