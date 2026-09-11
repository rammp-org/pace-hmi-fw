idf_component_register(SRC_DIRS "." "ui" "ui/screens" "ui/components" "ui/fonts" "ui/images"
                       INCLUDE_DIRS "." "ui"
                       PRIV_REQUIRES adc button drv2605 file_system filters input_drivers joystick lvgl m5stack-tab5 esp_driver_ppa
                                     rtps cdr w5500 esp_eth esp_netif esp_event lwip esp_app_format
                       EMBED_TXTFILES click.wav)