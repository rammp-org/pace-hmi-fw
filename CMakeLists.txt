# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.20)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# add the component directories that we want to use
set(EXTRA_COMPONENT_DIRS

  # NOTE: if you add any custom components, you should add them to a
  #       `components/` folder within this repository and update the line below
  #       to point to it.
  #
  # "components/"

  # NOTE: if we didn't use the idf-component-manager to get the espp
  #       dependencies, we could instead have ESPP be a submodule within our
  #       'components' folder, which would mean we'd need to add it to the DIRS:
  #
  # "components/espp/components/"
  #
  # NOTE: alternatively, we could install espp system-wide, so we could just
  #       point it to wherever we have espp installed.
  #
  # "/path/to/espp/components/"
)

set(
  COMPONENTS
  # NOTE: you can add additional esp-idf and espp components you want to use to
  #       the line below.
  #
  # Alternatively, If they exist in the component registry, then you can ignore
  # adding them here, and instead use call
  #
  # `idf.py add-dependency "<namespace>/<component>^<version>"`
  #
  # e.g.
  #
  # `idf.py add-dependency "espp/task>=1.0"`
  "main esptool_py"
  CACHE STRING
  "List of components to include"
  )

# TODO: update this with your project's name
# ThorVG renders vectors into an ARGB8888 scratch buffer the size of the whole
# layer whenever the layer isn't already ARGB/XRGB8888 (see lv_draw_sw_vector.c).
# At LV_COLOR_DEPTH=16 on this 1280x720 panel that's a 3.5 MB request, and LVGL's
# builtin allocator only has LV_MEM_SIZE (64 KB), so it returned NULL and
# LV_ASSERT_NULL spun in LV_ASSERT_HANDLER's `while(1)` until the task WDT fired.
# LV_USE_STDLIB_MALLOC isn't exposed through LVGL's Kconfig and CONFIG_LV_CONF_SKIP
# is set, so it has to come from the command line. Global (not per-component) so
# every translation unit sees the same allocator.
idf_build_set_property(COMPILE_DEFINITIONS "LV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB" APPEND)

project(rammp-hmi-p4)

set(CMAKE_CXX_STANDARD 20)

# picolibc's pthread.h uses `__noreturn` in trailing position (`void f(void)
# __noreturn;`). In C++ that macro expands to `[[noreturn]]`, which then
# appertains to the type rather than the declaration, so GCC raises
# -Wattributes; IDF v6.0 makes it fatal via its build-wide -Werror. Hit by the
# Tab5 BSP's camera.cpp, which includes <pthread.h> after <driver/ppa.h> has
# pulled in hal/assert.h. Toolchain header issue, not a BSP one.
idf_component_get_property(tab5_lib m5stack-tab5 COMPONENT_LIB)
target_compile_options(${tab5_lib} PRIVATE -Wno-error=attributes)

# ThorVG's base64 decoder guards with `encoded[3] < 0` on a `const char*`.
# `char` is unsigned on RISC-V, so that test is always false and GCC raises
# -Wtype-limits, fatal under IDF v6.0's build-wide -Werror. Upstream ThorVG
# issue, pulled in only because LV_USE_THORVG is on for SVG rendering.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-error=type-limits)

# Refresh precompiled/ after every build, so flash_precompiled.ps1 always has the
# image this tree just produced. The folder is gitignored -- it is build output, not
# source; the copy anyone without a toolchain flashes comes from a release asset.
#
# Ordering is by target, not by output file: IDF's app binary is produced by a
# custom command whose declared output is a `.bin_timestamp` sentinel rather than
# the .bin itself (components/esptool_py/project_include.cmake), so ninja has no
# rule making `rammp-hmi-p4.bin` and a file-level DEPENDS on it fails a clean
# build outright. `bootloader` is an ExternalProject and nothing else orders it
# against the app, so it has to be named here too. Only `all` pulls this target
# in, so `idf.py app` stays as cheap as it was.
idf_build_get_property(python PYTHON)
add_custom_target(precompiled ALL
  COMMAND ${python} "${CMAKE_CURRENT_LIST_DIR}/scripts/export_precompiled.py"
          --build-dir "${CMAKE_BINARY_DIR}"
          --out-dir "${CMAKE_CURRENT_LIST_DIR}/precompiled"
  VERBATIM USES_TERMINAL)
add_dependencies(precompiled app bootloader partition_table_bin)
