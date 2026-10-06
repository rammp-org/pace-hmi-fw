# fw_core: the shared channel and ownership helpers (CS-OWN-10).
# The host L1 build (test/Makefile) compiles src/port_host.cpp instead of src/port_freertos.cpp.
idf_component_register(
  SRCS "src/check.cpp"
       "src/error.cpp"
       "src/queue.cpp"
       "src/thread_checker.cpp"
       "src/port_freertos.cpp"
  INCLUDE_DIRS "include"
  REQUIRES freertos
  PRIV_REQUIRES logger)

if(COMMAND fw_component_options)
  fw_component_options(${COMPONENT_LIB})
endif()
