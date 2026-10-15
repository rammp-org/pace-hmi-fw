# post_facts: the quick POST's facts from ESP-IDF and FreeRTOS (the reset reason, the running
# image's OTA state, heap and stack headroom, whether the stick task exists, the ms clock), for
# hmi_ui's PostPort (hazard-c3-spec.md §2.2, REQ-POST-19), with POST-050's static_asserts that
# post/facts.hpp mirrors the IDF enums. Moved from main.cpp's kPostPort unchanged (CS-LAY-01).
# Safety-relevant (CS-SAF-01): the facts gate motion through the POST.
idf_component_register(SRCS "src/post_facts.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES post
                       PRIV_REQUIRES app_update
                                     esp_partition
                                     esp_timer)

fw_component_options(${COMPONENT_LIB})

# Safety code: a switch over an enum names every enumerator, and its default goes to the
# safe state (CS-TYP-06).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch-enum -Werror=switch-enum)
