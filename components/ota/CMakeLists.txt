# ota: the firmware image: its identity (SHA-256, release match), the GitHub release list, the
# download and install of a release, and the rollback confirm. README.md has the concern and
# requirements; the pure parsing is components/ota_parse.
idf_component_register(SRCS "src/fw_info.cpp"
                            "src/github_ota.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update
                                     bootloader_support
                                     espressif__cjson
                                     esp_app_format
                                     esp_http_client
                                     esp_partition
                                     esp_timer
                                     format
                                     logger
                                     mbedtls
                                     ota_parse
                                     pthread
                                     spi_flash
                                     storage)

fw_component_options(${COMPONENT_LIB})
