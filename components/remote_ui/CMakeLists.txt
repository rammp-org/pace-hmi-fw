# remote_ui: the bench debug channel (CONFIG_HMI_REMOTE_UI, off in release builds): a TCP server
# that takes screenshots and injects input, and the bench stick injection's write end.
# README.md has the concern and requirements.
idf_component_register(SRCS "src/remote_ui.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES fw_core
                                stick
                       PRIV_REQUIRES esp_timer
                                     freertos
                                     log
                                     lvgl
                                     lwip
                                     pthread
                                     ui)

fw_component_options(${COMPONENT_LIB})
