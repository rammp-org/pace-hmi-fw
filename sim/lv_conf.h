/**
 * lv_conf.h for the desktop simulator.
 *
 * Only the options that differ from LVGL's built-in defaults are set here;
 * lv_conf_internal.h fills in the rest. Every value below is deliberately
 * matched to the firmware's own LVGL config in ../sdkconfig.defaults -- if the
 * two drift, the sim stops being evidence about what the board will do. The
 * comments name the sdkconfig key each one mirrors.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* SquareLine's export hard-asserts this: every ui_*.c has an #error if it is
 * not 16. Matches the Tab5's RGB565 DSI panel. */
#define LV_COLOR_DEPTH 16

/* CONFIG_LV_DEF_REFR_PERIOD=16 -- 60 fps, same as the panel. */
#define LV_DEF_REFR_PERIOD 16

/* The firmware forces LV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB (CMakeLists.txt) so
 * draw buffers land in internal RAM rather than PSRAM. Same choice here. */
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB

/* Required by the Win32 backend, which runs its window and message loop on a
 * thread of its own and needs LVGL's lock to hand frames across. The firmware
 * uses LV_OS_FREERTOS for the same reason; only the OS differs. Application
 * code still lives on one thread: main() pumps lv_timer_handler() and every
 * sim timer fires from there. */
#define LV_USE_OS LV_OS_WINDOWS

/* ------------------------------------------------------------------ fonts */
/* CONFIG_LV_FONT_MONTSERRAT_{24,30,32,34,48}. 14 is LV_FONT_DEFAULT. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 1
#define LV_FONT_MONTSERRAT_48 1

/* CONFIG_LV_FONT_FMT_TXT_LARGE=y -- ui_font_IBMPlexSansMedium264 and the
 * Digital faces overflow the 16-bit glyph offsets without it. */
#define LV_FONT_FMT_TXT_LARGE 1

/* -------------------------------------------------------------- graphics */
/* CONFIG_LV_USE_VECTOR_GRAPHIC / THORVG / SVG. The boot screen's RAMMP
 * wordmark (ui_img_rammp_type_white_svg) is stored as SVG text and decoded at
 * runtime; without these it renders as nothing. */
#define LV_USE_VECTOR_GRAPHIC  1
#define LV_USE_THORVG_INTERNAL 1
#define LV_USE_SVG             1

/* Neither of these appears in sdkconfig.defaults, and neither can be dropped.
 * LVGL's Kconfig chains them (LV_USE_VECTOR_GRAPHIC selects LV_USE_MATRIX,
 * which selects LV_USE_FLOAT), so the ESP-IDF build gets both for free. The
 * plain lv_conf.h path has no "select", so the chain has to be spelled out:
 * without MATRIX, lv_matrix_t stays an incomplete type and every vector/SVG
 * header fails to compile; without FLOAT, lv_matrix.h #errors outright. */
#define LV_USE_MATRIX 1
#define LV_USE_FLOAT  1

/* ThorVG rasterises on LVGL's draw thread and LVGL refuses to build it on the
 * default 8 KB stack. sdkconfig.defaults works around the same limit on the
 * P4 by running a single draw unit so ThorVG rasterises inline on the 32 KB
 * LVGL task stack; a desktop thread can simply be given room. */
#define LV_DRAW_THREAD_STACK_SIZE (64 * 1024)

/* --------------------------------------------------------------- tooling */
/* CONFIG_LV_USE_SYSMON / PERF_MONITOR / PERF_MONITOR_ALIGN_TOP_RIGHT. Keeps
 * the FPS button on the settings page working, though the number it shows is
 * a desktop CPU's, not the P4's. */
#define LV_USE_SYSMON       1
#define LV_USE_PERF_MONITOR 1
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_TOP_RIGHT

/* lv_subject_t / lv_obj_bind_* -- the whole data layer in main.cpp and
 * sim_nav.c is built on these. On by default; pinned so a future LVGL bump
 * flipping the default cannot silently gut the bindings. */
#define LV_USE_OBSERVER 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* ------------------------------------------------------------- backend --- */
/* Native Win32 backend: no SDL, no vcpkg, nothing to install. Supports
 * LV_COLOR_DEPTH 16 directly (BI_BITFIELDS RGB565 DIB section). */
#define LV_USE_WINDOWS 1

#endif /*LV_CONF_H*/
