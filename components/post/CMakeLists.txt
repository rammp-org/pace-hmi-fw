# post: the quick POST: the evaluator (boot facts in, a verdict per check and an overall state
# out), the ADC task's rest-window accumulator, the POST runner and the persistent indicator's
# state. Pure C++: no ESP-IDF, FreeRTOS or LVGL includes (stick only for the POST gate's type).
# Safety-relevant (CS-SAF-01): the POST gates motion (hazard-fixes C3). Host tests: test/
# (tests/manifest.d/post.yaml).
idf_component_register(
  SRCS "src/post.cpp"
       "src/runner.cpp"
       "src/indicator.cpp"
  INCLUDE_DIRS "include"
  REQUIRES stick)

fw_component_options(${COMPONENT_LIB})

# Safety code: a switch over an enum names every enumerator, and its default goes to the
# safe state (CS-TYP-06).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch-enum -Werror=switch-enum)
