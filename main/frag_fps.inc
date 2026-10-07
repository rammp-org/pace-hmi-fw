// split_main.py fragment of main.cpp (frag_fps: original lines 78-107); one TU, see main.cpp.
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
// ---------------------------------------------------------------------------
static constexpr bool kFpsInstrument = CONFIG_HMI_DEBUG_FPS_AS_INT != 0;
static constexpr bool kFpsStress = CONFIG_HMI_DEBUG_FPS_STRESS_AS_INT != 0;
static std::atomic<uint32_t> fps_frames{0};
static std::atomic<uint64_t> fps_render_us_total{0};
static std::atomic<uint32_t> fps_render_us_max{0};
static int64_t fps_render_start_us = 0;

static void fps_render_start_cb(lv_event_t *) { fps_render_start_us = esp_timer_get_time(); }

static void fps_render_ready_cb(lv_event_t *) {
  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - fps_render_start_us);
  fps_frames.fetch_add(1, std::memory_order_relaxed);
  fps_render_us_total.fetch_add(us, std::memory_order_relaxed);
  uint32_t prev = fps_render_us_max.load(std::memory_order_relaxed);
  while (us > prev && !fps_render_us_max.compare_exchange_weak(prev, us)) {
  }
}
