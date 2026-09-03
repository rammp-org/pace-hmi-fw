# Desktop simulator for the RAMMP HMI UI.
#
# Compiles the SquareLine export in main/ui/ verbatim -- the same sources the
# firmware builds -- against upstream LVGL 9.5 and LVGL's native Win32 backend,
# so the screens can be driven with a keyboard instead of a joystick and a Tab5.
#
# Nothing here is part of the ESP-IDF build; `idf.py build` never looks at this
# directory. See sim/README.md for the key map and the fidelity caveats.

cmake_minimum_required(VERSION 3.20)
project(pace-hmi-sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(FW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(UI_DIR ${FW_ROOT}/main/ui)

# ---------------------------------------------------------------- LVGL -----
# Pinned to the same minor the firmware pulls in via idf_component.yml
# (lvgl/lvgl ^9.5.0). Bump both together or the sim stops being evidence.
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.5.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl)

# LVGL's desktop CMake enables ASM and globs src/*.S, which picks up the Arm
# Helium (MVE) blend kernels meant for Cortex-M55/M85. On x86 the assembler
# tries to parse their C headers and dies. ESP-IDF's own lvgl build never hits
# this because esp.cmake lists sources explicitly instead of globbing.
get_target_property(LVGL_SOURCES lvgl SOURCES)
list(FILTER LVGL_SOURCES EXCLUDE REGEX "\\.S$")
set_target_properties(lvgl PROPERTIES SOURCES "${LVGL_SOURCES}")

# Bundled ThorVG uses fopen/strcpy freely. Not our code and not our call, so
# silence the CRT deprecation noise rather than let it bury real warnings.
if(MSVC)
  target_compile_definitions(lvgl PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(TARGET lvgl_thorvg)
    target_compile_definitions(lvgl_thorvg PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endif()

# ------------------------------------------------------- UI (unmodified) ----
# Every .c/.cpp under main/ui, screens, components, fonts and images. Globbed
# because import_ui.ps1 rewrites this tree wholesale on every SquareLine
# export -- an explicit list would go stale the first time a screen is added.
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
  ${UI_DIR}/*.c
  ${UI_DIR}/*.cpp
)

add_library(hmi_ui STATIC ${UI_SOURCES})
target_include_directories(hmi_ui PUBLIC ${UI_DIR})
target_link_libraries(hmi_ui PUBLIC lvgl)

# ------------------------------------------------------------- the sim -----
# Console subsystem on purpose: the sim logs gesture and MCB-state transitions
# to stdout, which is half the value of running it.
add_executable(pace-hmi-sim
  src/main.c
  src/sim_input.c
  src/sim_mcb.c
  src/sim_nav.c
  src/sim_bench.c
  src/sim_dpad.c
)
target_include_directories(pace-hmi-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${FW_ROOT}/main)
target_link_libraries(pace-hmi-sim PRIVATE hmi_ui lvgl)

if(MSVC)
  # SquareLine's generated tables are large; the fonts alone blow the default
  # section limit on the image/font translation units.
  target_compile_options(hmi_ui PRIVATE /bigobj /wd4244 /wd4267)
  target_compile_options(pace-hmi-sim PRIVATE /bigobj)
endif()
