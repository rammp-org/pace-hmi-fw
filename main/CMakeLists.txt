idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES
                         adc
                         button
                         drv2605
                         file_system
                         filters
                         hmi_format
                         input_drivers
                         joystick
                         lvgl
                         m5stack-tab5
                         ui
                         esp_driver_ppa
                         rtps
                         cdr
                         w5500
                         esp_eth
                         esp_netif
                         esp_event
                         lwip
                         esp_app_format
                         esp_wifi
                         esp_hosted
                         app_update
                         bootloader_support
                         esp_partition
                         mbedtls
                         esp_http_client
                         rammp_rtps_messages
                         fw_core
                       EMBED_TXTFILES click.wav)

# The commit the About screen shows beside the version. Read when CMake
# configures, like the version itself (PROJECT_VER, from git describe), and
# ESP-IDF reconfigures when HEAD moves, so the two stay in step.
execute_process(COMMAND git rev-parse --short=7 HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE hmi_git_commit
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(NOT hmi_git_commit)
  set(hmi_git_commit "unknown")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE HMI_GIT_COMMIT="${hmi_git_commit}")

# lv_mem_psram.c is LVGL's allocator, so only LVGL calls into it - and libmain is
# linked before liblvgl, so the linker would pass it over before anything asked
# for it. -u makes lv_malloc_core wanted from the start, which pulls the object
# (and the rest of the allocator with it) in.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")