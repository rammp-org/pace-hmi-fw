idf_component_register(SRC_DIRS "." "ui" "ui/screens" "ui/components"
                       INCLUDE_DIRS "." "ui"
                       PRIV_REQUIRES adc filters lvgl m5stack-tab5 esp_driver_ppa
                       EMBED_TXTFILES click.wav)