// split_main.py fragment of main.cpp (frag_fps: original lines 78-107); one TU, see main.cpp.
#include "hmi_ui/fps_meter.hpp"
#include "hmi_ui/resident_chrome.hpp"
#include "hmi_ui/rtps_ui_bridge.hpp"
#include "hmi_ui/ui_build.hpp"
#include "hmi_ui/ui_island.hpp"
// ---------------------------------------------------------------------------
// Frame-rate instrumentation: a debug feature, off unless CONFIG_HMI_DEBUG_FPS
// (CS-LAY-09), and never in sdkconfig.defaults, which ships.
//
// Reports over serial rather than the LVGL perf overlay, so throughput can be
// measured without eyes on the panel. RENDER_START/RENDER_READY fire only when
// LVGL actually rasterizes, so these numbers are real frame cost -- REFR_*
// would tick even on an idle screen and read as a meaningless "infinite fps".
//
// kFpsStress forces a full-screen invalidation every LVGL cycle. Without it a
// static SquareLine screen invalidates nothing and renders nothing, which
// measures the redraw path not at all. With it we get sustained worst case
// (CONFIG_HMI_DEBUG_FPS_STRESS).
//
// The once-a-second report goes through its own espp::Logger at debug level,
// tag "fps" (CS-LOG-01/03): developer detail, and only in this debug build.
// The counters are hmi::ui::FpsMeter's members.
// ---------------------------------------------------------------------------
static constexpr bool kFpsInstrument = CONFIG_HMI_DEBUG_FPS_AS_INT != 0;
static constexpr bool kFpsStress = CONFIG_HMI_DEBUG_FPS_STRESS_AS_INT != 0;

// The one meter. Attached and reported only when kFpsInstrument.
static constinit hmi::ui::FpsMeter fps_meter;
