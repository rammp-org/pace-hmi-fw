# hmi::stick: the stick pipeline (raw mV -> key and motion command). Pure logic: no LVGL,
# no ESP-IDF drivers. Host tests: components/stick/test (tests/manifest.d/stick.yaml).
idf_component_register(
  SRCS "src/stick_pipeline.cpp"
  INCLUDE_DIRS "include"
  REQUIRES joystick)

if(COMMAND fw_component_options)
  fw_component_options(${COMPONENT_LIB})
endif()
