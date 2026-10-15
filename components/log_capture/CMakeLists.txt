# log_capture: stdout and stderr teed into a ring of lines in PSRAM, for the LogScreen. Moved
# from main/log_capture.* unchanged (CS-LAY-01). README.md has the concern and requirements.
idf_component_register(SRCS "src/log_capture.cpp"
                       INCLUDE_DIRS "include")

fw_component_options(${COMPONENT_LIB})
