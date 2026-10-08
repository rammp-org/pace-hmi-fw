# hmi::stick: the stick pipeline (raw mV -> key and motion command). Pure logic: no LVGL,
# no ESP-IDF drivers. Host tests: components/stick/test (tests/manifest.d/stick.yaml).
idf_component_register(
  SRCS "src/stick_pipeline.cpp"
  INCLUDE_DIRS "include"
  REQUIRES fw_core
           joystick)

fw_component_options(${COMPONENT_LIB})

# Safety code: a switch over an enum names every enumerator, and its default goes to the
# safe state (CS-TYP-06).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch-enum -Werror=switch-enum)
