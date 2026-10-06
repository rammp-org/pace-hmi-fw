# Which components are first-party in this repo (CODING_SPEC CS-LNG-02).
#
# First-party: `main`, and every component whose directory is under components/,
# except the ones below. Everything else (ESP-IDF, espp, managed_components,
# external/rammp-rtps, the vendored/generated ones) gets SYSTEM headers through
# fw_mark_third_party_system(), so its warnings never fail first-party builds.
#
# Derived from the directory, not a hand-kept list, so a new component under
# components/ is first-party the moment it exists. New components still call
# fw_component_options(${COMPONENT_LIB}) themselves.
#
#   ui            SquareLine export, generated (CS-LAY-04)
#   m5stack-tab5  vendored Tab5 BSP (CS-LAY-05)
#   joystick      vendored from espp (CS-LAY-05)
set(HMI_NOT_FIRST_PARTY ui m5stack-tab5 joystick)

# hmi_is_first_party(<out-var> <component-dir> <source-dir>)
# Pure (no IDF calls), so `cmake -P` can test it.
function(hmi_is_first_party out dir source_dir)
  file(REAL_PATH "${dir}" dir_real)
  file(REAL_PATH "${source_dir}" src_real)
  set(result FALSE)
  if(dir_real STREQUAL "${src_real}/main")
    set(result TRUE)
  else()
    get_filename_component(parent "${dir_real}" DIRECTORY)
    get_filename_component(leaf "${dir_real}" NAME)
    if(parent STREQUAL "${src_real}/components" AND NOT leaf IN_LIST HMI_NOT_FIRST_PARTY)
      set(result TRUE)
    endif()
  endif()
  set(${out} ${result} PARENT_SCOPE)
endfunction()

# hmi_mark_third_party_system(): fw_mark_third_party_system() over every build
# component that hmi_is_first_party() rejects. Call after project().
function(hmi_mark_third_party_system)
  idf_build_get_property(components BUILD_COMPONENTS)
  set(first_party "")
  foreach(component IN LISTS components)
    idf_component_get_property(dir ${component} COMPONENT_DIR)
    hmi_is_first_party(is_first "${dir}" "${CMAKE_SOURCE_DIR}")
    if(is_first)
      list(APPEND first_party ${component})
    endif()
  endforeach()
  message(STATUS "fw-standards first-party components: ${first_party}")
  fw_mark_third_party_system(${first_party})
endfunction()
