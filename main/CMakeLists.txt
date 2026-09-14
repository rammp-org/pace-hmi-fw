idf_component_register(SRC_DIRS "." "ui" "ui/screens" "ui/components" "ui/fonts" "ui/images"
                       INCLUDE_DIRS "." "ui"
                       PRIV_REQUIRES adc button drv2605 file_system filters input_drivers joystick lvgl m5stack-tab5 esp_driver_ppa
                                     rtps cdr w5500 esp_eth esp_netif esp_event lwip esp_app_format
                       EMBED_TXTFILES click.wav)

# lv_mem_psram.c is LVGL's allocator, so only LVGL calls into it - and libmain is
# linked before liblvgl, so the linker would pass it over before anything asked
# for it. -u makes lv_malloc_core wanted from the start, which pulls the object
# (and the rest of the allocator with it) in.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")