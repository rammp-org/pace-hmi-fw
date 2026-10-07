# ota_parse: pure parsers for what a firmware update reads from outside (GitHub's release list,
# the image's first block, fwinfo.txt, the confirms-its-boot marker). No ESP-IDF, no HTTP;
# host-tested in test/.
idf_component_register(SRCS "src/fw_record.cpp"
                            "src/image_header.cpp"
                            "src/marker_search.cpp"
                            "src/release_list.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES espressif__cjson)

if(COMMAND fw_component_options)
  fw_component_options(${COMPONENT_LIB})
endif()
