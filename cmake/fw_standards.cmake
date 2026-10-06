# Copied from fw-standards (github.com/Alaining/fw-standards) at commit a132375:
# cmake/fw_standards.cmake, unchanged below this header. Update by re-copying, never by editing.

# fw-standards build helpers (CODING_SPEC CS-LNG-02).
# In the project's top-level CMakeLists.txt, after project():
#   include(${CMAKE_CURRENT_LIST_DIR}/docs/standards/cmake/fw_standards.cmake)
#   fw_mark_third_party_system(<every first-party component>)
# In each first-party component's CMakeLists.txt, after idf_component_register():
#   fw_component_options(${COMPONENT_LIB})

# C++23 and the warning set, as errors. IDF downgrades some warnings globally
# (-Wno-error=unused-variable, ...); the explicit -Werror=... flags restore them.
function(fw_component_options target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23 -Wnon-virtual-dtor -Woverloaded-virtual>
    -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Wsign-compare
    -Wnull-dereference -Wimplicit-fallthrough -Wunused-parameter
    -Werror -Werror=unused-variable -Werror=unused-but-set-variable
    -Werror=unused-function -Werror=extra)
endfunction()

# Treat every component not listed as third-party: its headers become system headers,
# so their warnings (espp, LVGL, FreeRTOS, ESP-IDF) do not fail first-party builds.
# Pilot item: confirm with a real build that FreeRTOS macro expansions are covered.
function(fw_mark_third_party_system)
  idf_build_get_property(components BUILD_COMPONENTS)
  foreach(component IN LISTS components)
    if(NOT component IN_LIST ARGN)
      idf_component_get_property(lib ${component} COMPONENT_LIB)
      if(TARGET ${lib})
        set_target_properties(${lib} PROPERTIES SYSTEM TRUE)
      endif()
    endif()
  endforeach()
endfunction()
