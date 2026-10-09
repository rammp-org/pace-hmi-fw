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
#
# components/espp_adc (vendored espp adc, CS-LAY-05) is first-party on purpose: our hook lines
# in its continuous_adc.hpp get the full warning set, and its upstream headers build clean
# under it (checked 2026-10-08; owner: only upstream files may be exempt, never our code).
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

# hmi_toolchain_headers(<out-var> <compiler> <flags-file>)
# Every header (up to one folder deep, e.g. sys/lock.h) in the compiler's built-in
# include search list, as seen with IDF's own toolchain flags. Pure, so `cmake -P`
# can test it. <flags-file> may be empty or missing.
function(hmi_toolchain_headers out compiler flags_file)
  set(probe "${CMAKE_CURRENT_BINARY_DIR}/hmi_include_probe.c")
  file(WRITE "${probe}" "")
  set(flags "")
  if(flags_file AND EXISTS "${flags_file}")
    set(flags "@${flags_file}")
  endif()
  execute_process(COMMAND "${compiler}" ${flags} -E -v -x c "${probe}"
                  OUTPUT_QUIET ERROR_VARIABLE log RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0 OR NOT log MATCHES "End of search list")
    set(${out} "PROBE-FAILED" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCH "#include <[.][.][.]> search starts here:(.*)End of search list" _ "${log}")
  string(REGEX REPLACE "[\r\n]+" ";" dirs "${CMAKE_MATCH_1}")
  set(headers "")
  foreach(dir IN LISTS dirs)
    string(STRIP "${dir}" dir)
    if(dir STREQUAL "" OR NOT IS_DIRECTORY "${dir}")
      continue()
    endif()
    file(GLOB top RELATIVE "${dir}" "${dir}/*.h")
    file(GLOB sub RELATIVE "${dir}" "${dir}/*/*.h")
    list(APPEND headers ${top} ${sub})
  endforeach()
  list(REMOVE_DUPLICATES headers)
  set(${out} "${headers}" PARENT_SCOPE)
endfunction()

# hmi_shadows_toolchain(<out-var> <include-dirs> <toolchain-headers>)
# TRUE when any of <include-dirs> holds a header with the same path as a
# toolchain header (esp_libc's platform_include/sys/lock.h overriding picolibc's).
function(hmi_shadows_toolchain out include_dirs headers)
  set(subdirs "")
  foreach(h IN LISTS headers)
    if(h MATCHES "^([^/]+)/")
      list(APPEND subdirs "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES subdirs)
  set(result FALSE)
  foreach(inc IN LISTS include_dirs)
    if(inc MATCHES "[$]<" OR NOT IS_DIRECTORY "${inc}")
      continue()
    endif()
    file(GLOB found RELATIVE "${inc}" "${inc}/*.h")
    foreach(sd IN LISTS subdirs)
      if(IS_DIRECTORY "${inc}/${sd}")
        file(GLOB more RELATIVE "${inc}" "${inc}/${sd}/*.h")
        list(APPEND found ${more})
      endif()
    endforeach()
    foreach(h IN LISTS found)
      if(h IN_LIST headers)
        set(result TRUE)
        break()
      endif()
    endforeach()
    if(result)
      break()
    endif()
  endforeach()
  set(${out} ${result} PARENT_SCOPE)
endfunction()

# hmi_mark_third_party_system(): fw_mark_third_party_system() over every build
# component that is neither first-party nor a toolchain-header override. Call
# after project().
#
# Why the overrides stay non-SYSTEM: IDF compiles with -specs=picolibc.specs,
# which injects picolibc's include dir as an -isystem AHEAD of every -isystem on
# the command line. esp_libc's platform_include (sys/lock.h, pthread.h, ...)
# only wins while it is an -I; as an -isystem it lost and the first CI build
# failed in log_timestamp_common.c (unknown type _lock_t).
function(hmi_mark_third_party_system)
  idf_build_get_property(components BUILD_COMPONENTS)
  set(toolchain_dir "${IDF_TOOLCHAIN_BUILD_DIR}")
  if(NOT toolchain_dir)
    set(toolchain_dir "${CMAKE_BINARY_DIR}/toolchain")
  endif()
  hmi_toolchain_headers(toolchain_headers "${CMAKE_C_COMPILER}" "${toolchain_dir}/cflags")
  if(toolchain_headers STREQUAL "PROBE-FAILED")
    # Without the toolchain's header list, SYSTEM could hide a libc override and
    # break or change the build: mark nothing, say so, and build as before.
    message(WARNING "fw-standards: could not list ${CMAKE_C_COMPILER}'s include dirs; "
                    "no component marked SYSTEM")
    return()
  endif()
  set(first_party "")
  set(overrides "")
  foreach(component IN LISTS components)
    idf_component_get_property(dir ${component} COMPONENT_DIR)
    idf_component_get_property(lib ${component} COMPONENT_LIB)
    hmi_is_first_party(is_first "${dir}" "${CMAKE_SOURCE_DIR}")
    if(is_first)
      list(APPEND first_party ${component})
    elseif(TARGET ${lib})
      get_target_property(incs ${lib} INTERFACE_INCLUDE_DIRECTORIES)
      if(incs)
        hmi_shadows_toolchain(shadows "${incs}" "${toolchain_headers}")
        if(shadows)
          list(APPEND overrides ${component})
        endif()
      endif()
    endif()
  endforeach()
  message(STATUS "fw-standards first-party components: ${first_party}")
  message(STATUS "fw-standards non-SYSTEM (override toolchain headers): ${overrides}")
  fw_mark_third_party_system(${first_party} ${overrides})
endfunction()

# hmi_check_fw_options(): fail the configure when a first-party component's library
# did not get fw_component_options() (CS-LNG-02). The call used to be skipped silently
# (the function was defined after project()), so this makes a missing or ineffective call
# loud. `main` is exempt by the profile's CS-LNG-02 deviation; header-only (INTERFACE)
# components compile nothing of their own. Call after project().
function(hmi_check_fw_options)
  idf_build_get_property(components BUILD_COMPONENTS)
  set(missing "")
  foreach(component IN LISTS components)
    idf_component_get_property(dir ${component} COMPONENT_DIR)
    idf_component_get_property(lib ${component} COMPONENT_LIB)
    hmi_is_first_party(is_first "${dir}" "${CMAKE_SOURCE_DIR}")
    if(NOT is_first OR component STREQUAL "main" OR NOT TARGET ${lib})
      continue()
    endif()
    get_target_property(type ${lib} TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY")
      continue()
    endif()
    get_target_property(options ${lib} COMPILE_OPTIONS)
    if(NOT options OR NOT "-Wconversion" IN_LIST options)
      list(APPEND missing ${component})
    endif()
  endforeach()
  if(missing)
    message(FATAL_ERROR "fw-standards: first-party components without fw_component_options() "
                        "(CS-LNG-02): ${missing}. Call fw_component_options(\${COMPONENT_LIB}) "
                        "after idf_component_register().")
  endif()
endfunction()
