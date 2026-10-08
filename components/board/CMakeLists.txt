# board: the HMI's bring-up of the Tab5 on its BSP, and the adapters the BSP's own tasks call
# (CS-LAY-02). README.md says what is in it.
idf_component_register(SRCS "src/adapters.cpp"
                            "src/board.cpp"
                            "src/board_port.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES housekeeping logger m5stack-tab5
                       PRIV_REQUIRES hmi_format)

fw_component_options(${COMPONENT_LIB})
