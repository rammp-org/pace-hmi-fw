# settings: the user settings that survive a reboot (the spec table, /storage/settings.txt, the
# getters and setters). README.md has the concern and requirements.
idf_component_register(SRCS "src/settings.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES logger
                                     storage)

fw_component_options(${COMPONENT_LIB})
