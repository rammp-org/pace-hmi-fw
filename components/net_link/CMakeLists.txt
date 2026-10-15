# net_link: the network link (W5500 SPI Ethernet, or Wi-Fi through the Tab5's ESP32-C6) and the
# HMI's one RTPS participant, every publisher and subscriber (rtps_comms.hpp). Moved from
# main/rtps_comms.* unchanged (CS-LAY-01). README.md has the concern and requirements.
idf_component_register(SRCS "src/rtps_comms.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES hmi_rtps_spec
                       PRIV_REQUIRES cdr
                                     esp_driver_gpio
                                     esp_driver_spi
                                     esp_eth
                                     esp_event
                                     esp_hosted
                                     esp_netif
                                     esp_timer
                                     esp_wifi
                                     logger
                                     lwip
                                     rtps
                                     storage
                                     task
                                     w5500)

fw_component_options(${COMPONENT_LIB})
