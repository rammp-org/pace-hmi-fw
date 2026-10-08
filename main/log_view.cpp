#include "log_view.hpp"

// The LogView's one instance and what it reads, which is main's: the log capture, and PSRAM
// for the text.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "esp_heap_caps.h"

#include "hmi_ui/log_view.hpp"
#include "log_capture.hpp"

namespace {

static_assert(static_cast<int>(LogLevel::Plain) == static_cast<int>(hmi::ui::LogLineLevel::PLAIN));
static_assert(static_cast<int>(LogLevel::Warning) ==
              static_cast<int>(hmi::ui::LogLineLevel::WARNING));
static_assert(static_cast<int>(LogLevel::Error) == static_cast<int>(hmi::ui::LogLineLevel::ERROR));

// The capture's lines, handed to the view's visitor as its own level type.
uint32_t visit(const hmi::ui::LogView::LineVisitor &visitor) {
  return log_capture_visit([&visitor](LogLevel level, std::string_view line) {
    visitor(static_cast<hmi::ui::LogLineLevel>(level), line);
  });
}

// The text the log label draws, in PSRAM: LVGL's own allocator is a fixed
// 64 KB pool shared by the whole UI, and 500 lines of log can be larger than
// that on their own. Never freed: the screen lives as long as the firmware.
char *alloc_text(size_t size) {
  return static_cast<char *>(heap_caps_calloc(size, 1, MALLOC_CAP_SPIRAM));
}

// The one LogView, over main's log capture.
constinit hmi::ui::LogView view{{
    .lines = kLogCaptureLines,
    .line_len = kLogCaptureLineLen,
    .count = log_capture_count,
    .visit = visit,
    .alloc_text = alloc_text,
}};

} // namespace

void log_view_init() { view.init(); }

lv_group_t *log_view_group() { return view.group(); }

void log_view_set_escape(void (*down_past_end)()) { view.set_escape(down_past_end); }

void log_view_on_load() { view.on_load(); }
