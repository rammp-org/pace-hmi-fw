# storage: files on the `storage` partition (LittleFS at /storage): paths, atomic writes, and
# the one-time migration from the pre-OTA layout. README.md has the concern and requirements.
idf_component_register(SRCS "src/storage.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_partition
                                     file_system
                                     joltwallet__littlefs
                                     logger)

fw_component_options(${COMPONENT_LIB})
