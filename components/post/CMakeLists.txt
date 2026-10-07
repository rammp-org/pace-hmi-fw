# post: the quick POST evaluator. Boot facts in, a verdict per check and an overall state out.
# Pure C++: no ESP-IDF, FreeRTOS or LVGL includes. Safety-relevant (CS-SAF-01): the POST gates
# motion once hazard-fixes C3 wires it in. Host tests: test/ (tests/manifest.d/post.yaml).
idf_component_register(
  SRCS "src/post.cpp"
  INCLUDE_DIRS "include")

fw_component_options(${COMPONENT_LIB})

# Safety code: a switch over an enum names every enumerator, and its default goes to the
# safe state (CS-TYP-06).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch-enum -Werror=switch-enum)
