#include "remote_ui.hpp"

#if CONFIG_HMI_REMOTE_UI

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_private/freertos_idf_additions_priv.h"
#include "esp_pthread.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/freertos_debug.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lwip/sockets.h"
#include "riscv/rvruntime-frames.h"
#include "soc/soc_caps.h"
#include "ui.h"

namespace {

constexpr const char *kTag = "remote_ui";

// A tap has to outlast one LVGL read (the indev polls every 30 ms by default)
// at both ends, or the press and the release land in the same read and nothing
// sees a click.
constexpr int kTapMs = 80;
constexpr int kSwipeStepMs = 20;
// The server renders: SHOT calls lv_refr_now, which runs the whole draw
// pipeline for the active screen on THIS task's stack. The LVGL task gets
// 16 KB for the same job; the pthread default (a few KB) overflowed silently
// on the settings and diagnostics screens, whose rows nest deepest, and the
// board reset with nothing on the console.
constexpr size_t kStackBytes = 16 * 1024;

RemoteUiConfig cfg;

// Written by the server task, read by the LVGL task through the indev. Plain
// atomics rather than a lock: the read callback runs inside lv_timer_handler
// and must not block on a socket.
std::atomic<int32_t> touch_x{0};
std::atomic<int32_t> touch_y{0};
std::atomic<bool> touch_down{false};
// Bumped on every read, so a tap can wait until LVGL has actually seen its
// press and then its release.
std::atomic<uint32_t> pointer_reads{0};

lv_indev_t *pointer = nullptr;

void pointer_read(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = touch_x.load(std::memory_order_relaxed);
  data->point.y = touch_y.load(std::memory_order_relaxed);
  data->state =
      touch_down.load(std::memory_order_relaxed) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  pointer_reads.fetch_add(1);
}

// Holds the pointer as it is until LVGL has read it twice more (so once whole
// after the change), or a second has gone: a fixed 80 ms was shorter than a
// full redraw, and a press the LVGL task never polled was a tap that vanished.
void hold_until_read() {
  const uint32_t from = pointer_reads.load();
  for (int waited = 0; waited < 1000 && pointer_reads.load() - from < 2; waited += 5) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

bool send_all(int sock, const void *data, size_t len) {
  const auto *p = static_cast<const uint8_t *>(data);
  while (len > 0) {
    const int sent = ::send(sock, reinterpret_cast<const char *>(p), len, 0);
    if (sent <= 0) {
      return false;
    }
    p += sent;
    len -= static_cast<size_t>(sent);
  }
  return true;
}

bool send_line(int sock, const std::string &line) {
  const std::string out = line + "\n";
  return send_all(sock, out.data(), out.size());
}

/// The frame the panel is showing, as RGB565, optionally decimated by `step`
/// on both axes.
///
/// Read straight out of the display buffer rather than through
/// lv_snapshot_take: this build renders in DIRECT mode into the DSI panel's own
/// frame buffers, so the frame already exists and a snapshot would be a second
/// 1.8 MB of it plus a full re-render. It also needs no LV_USE_SNAPSHOT, which
/// would otherwise be compiled into every build for a debug channel.
///
/// Refreshed twice first. DIRECT mode flips between two buffers and LVGL only
/// redraws the invalid areas of each, so one refresh leaves the buffer that is
/// now active holding the PREVIOUS frame in whatever changed. Two full
/// invalidations put the same complete frame in both, whichever one comes back
/// as active. It costs ~170 ms; this is a debug channel, and a frame that is
/// right matters more than a frame that is quick.
///
/// The frame is copied out under the LVGL lock and sent after it is let go.
/// Sending under the lock stalled the LVGL task for the ~2.6 s the socket
/// takes over 1.8 MB, and a stick-button hold started straight after that
/// stall was lost -- a debug channel must not change what it is observing.
/// The copy is one PSRAM buffer, allocated on the first SHOT and kept.
bool send_shot(int sock, int step) {
  static uint16_t *copy = nullptr;
  int32_t out_w = 0;
  int32_t out_h = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    lv_display_t *disp = lv_display_get_default();
    for (int i = 0; i < 2; i++) {
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(disp);
    }
    const lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    if (buf == nullptr || buf->data == nullptr) {
      return send_line(sock, "ERR no display buffer");
    }
    if (buf->header.cf != LV_COLOR_FORMAT_RGB565) {
      return send_line(sock, "ERR display is not RGB565 (cf " +
                                 std::to_string(static_cast<int>(buf->header.cf)) + ")");
    }
    out_w = buf->header.w / step;
    out_h = buf->header.h / step;
    static size_t copy_px = 0;
    const size_t px = static_cast<size_t>(out_w) * out_h;
    if (px > copy_px) {
      heap_caps_free(copy);
      copy = static_cast<uint16_t *>(heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM));
      copy_px = copy != nullptr ? px : 0;
    }
    if (copy == nullptr) {
      return send_line(sock, "ERR no memory for the capture");
    }
    for (int32_t y = 0; y < out_h; y++) {
      const auto *src = reinterpret_cast<const uint16_t *>(
          buf->data + static_cast<size_t>(y) * step * buf->header.stride);
      uint16_t *dst = copy + static_cast<size_t>(y) * out_w;
      if (step == 1) {
        memcpy(dst, src, static_cast<size_t>(out_w) * 2);
        continue;
      }
      for (int32_t x = 0; x < out_w; x++) {
        dst[x] = src[x * step];
      }
    }
  }
  const size_t bytes = static_cast<size_t>(out_w) * out_h * 2;
  return send_line(sock, "FRAME " + std::to_string(out_w) + " " + std::to_string(out_h) + " " +
                             std::to_string(bytes)) &&
         send_all(sock, copy, bytes);
}

void touch_to(int32_t x, int32_t y, bool down) {
  touch_x.store(x, std::memory_order_relaxed);
  touch_y.store(y, std::memory_order_relaxed);
  touch_down.store(down, std::memory_order_relaxed);
}

uint32_t key_from_name(const std::string &name) {
  if (name == "UP") {
    return LV_KEY_UP;
  }
  if (name == "DOWN") {
    return LV_KEY_DOWN;
  }
  if (name == "LEFT") {
    return LV_KEY_LEFT;
  }
  if (name == "RIGHT") {
    return LV_KEY_RIGHT;
  }
  return 0; // NONE, and anything unknown: let the stick go
}

std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && line[i] == ' ') {
      i++;
    }
    const size_t start = i;
    while (i < line.size() && line[i] != ' ') {
      i++;
    }
    if (i > start) {
      out.emplace_back(line, start, i - start);
    }
  }
  return out;
}

int arg(const std::vector<std::string> &words, size_t index, int fallback = 0) {
  if (index >= words.size()) {
    return fallback;
  }
  return std::atoi(words[index].c_str());
}

// TASKS (G10, docs/plans/app-main-shrink.md V11): every task's name, priority,
// core and stack, for tools/guards/task_dump.py.
//
// The walk holds the kernel lock, so it is consistent across BOTH cores. On this
// dual-core IDF FreeRTOS (not CONFIG_FREERTOS_SMP), vTaskSuspendAll only stops the
// calling core's scheduler (FreeRTOS-Kernel/tasks.c:2499-2524:
// ++uxSchedulerSuspended[portGET_CORE_ID()], the lock released again).
// uxTaskGetSnapshotAll/xTaskGetNext take no lock
// (esp_additions/freertos_tasks_c_additions.h:976, :1061). Under the old
// vTaskSuspendAll, the other core moved tasks between the lists mid-walk: one bench
// dump in 9 listed a task twice, and a task deleted there could leave a dangling
// TCB. Every task list change takes xKernelLock. prvTakeKernelLock() is IDF's
// wrapper to take it from outside tasks.c (esp_private/freertos_idf_additions_priv.h:
// 140-160; it is taskENTER_CRITICAL(&xKernelLock), freertos_tasks_c_additions.h:42-46;
// event_groups.c:569-572 uses it the same way). It is declared only for dual-core
// non-SMP builds, so another kernel configuration fails to compile here.
// uxTaskGetSystemState (CONFIG_FREERTOS_USE_TRACE_FACILITY) takes the same lock,
// but it hands back the name as a pointer into the TCB and no pxEndOfStack, so the
// rest would be read after the lock was dropped. Inside the lock: only copies and
// non-blocking reads. uxTaskPriorityGet and xTaskGetCoreID take xKernelLock again,
// which IDF spinlocks allow on the owning core (esp_hw_support/include/spinlock.h:
// 103-112). The high-water scans make it the longest part; "locked_us" reports it.
constexpr UBaseType_t kMaxTasks = 64; // the board runs about 30

struct TaskRow {
  std::array<char, configMAX_TASK_NAME_LEN> name{};
  UBaseType_t priority = 0;
  BaseType_t core = 0;
  uint32_t stack_bytes = 0;
  uint32_t stack_free = 0;
  bool coproc_pinned = false; // has used the FPU or PIE: the port pinned it to `core`
};

// The coprocessor save area ESP-IDF 6.0's RISC-V port keeps at the top of every
// task's stack, set up at creation (FreeRTOS-Kernel/portable/riscv/port.c:276
// pxRetrieveCoprocSaveAreaFromStackPointer, :426). Its fields mean:
// - sa_allocator/sa_tcbstack: set by the first pxPortGetCoprocArea call for the
//   task (port.c:793-799), which is the HWLP check on every switch-in
//   (portasm.S:814 calls :195-207). So they are set for every task that has ever run, and
//   say nothing about coprocessor use. sa_tcbstack is the stack start the task
//   was created with.
// - the saved contexts: when a coprocessor context must be saved, the port
//   carves it from the BOTTOM of the stack and moves the TCB's pxStack up past
//   it (port.c:800-815; 132 B for the FPU, RvFPUSaveArea), so
//   xTaskGetStackStart reads 132+ B short of the created size.
// - sa_enable: bit FPU_COPROC_IDX or PIE_COPROC_IDX is set by the trap on the
//   task's first use of that coprocessor (portasm.S:111-114). The same trap pins
//   the task to the core it is running on (portasm.S:99 vPortTaskPinToCore).
//   Only HWLP's bit is ever cleared (portasm.S:224), and HWLP does not pin.
static_assert(SOC_CPU_COPROC_NUM > 0, "this port keeps a coprocessor save area");
const RvCoprocSaveArea &coproc_area(const TaskSnapshot_t &snap) {
  const auto top = reinterpret_cast<uintptr_t>(snap.pxEndOfStack);
  // the port's own arithmetic: STACKPTR_ALIGN_DOWN(16, top - sizeof(area))
  return *reinterpret_cast<const RvCoprocSaveArea *>((top - sizeof(RvCoprocSaveArea)) &
                                                     ~uintptr_t{15});
}

struct TaskTable {
  std::array<TaskSnapshot_t, kMaxTasks> snapshots{};
  std::array<TaskRow, kMaxTasks> rows{};
  UBaseType_t count = 0;
  uint32_t dup_handles = 0; // a TCB seen twice: the walk was not consistent
  int64_t locked_us = 0;    // how long the kernel lock was held
};

void take_tasks(TaskTable &table) {
  const int64_t t0 = esp_timer_get_time();
  prvTakeKernelLock();
  const UBaseType_t n = uxTaskGetSnapshotAll(table.snapshots.data(), kMaxTasks, nullptr);
  for (UBaseType_t i = 0; i < n; ++i) {
    const TaskSnapshot_t &snap = table.snapshots[i];
    auto task = static_cast<TaskHandle_t>(snap.pxTCB);
    if (task == nullptr) {
      continue; // a corrupt list entry: the iterator hands back no handle
    }
    // belt and braces: under the lock a TCB cannot appear twice; count it if it does
    const auto first = table.snapshots.begin();
    if (std::any_of(first, first + i,
                    [&](const TaskSnapshot_t &s) { return s.pxTCB == snap.pxTCB; })) {
      ++table.dup_handles;
      continue;
    }
    TaskRow &row = table.rows[table.count++];
    std::strncpy(row.name.data(), pcTaskGetName(task), row.name.size() - 1);
    row.priority = uxTaskPriorityGet(task);
    row.core = xTaskGetCoreID(task);
    const RvCoprocSaveArea &sa = coproc_area(snap);
    constexpr uint32_t kPinningCoprocs = (1U << FPU_COPROC_IDX) | (1U << PIE_COPROC_IDX);
    row.coproc_pinned = (static_cast<uint32_t>(sa.sa_enable) & kPinningCoprocs) != 0;
    // from the start the task was created with (see coproc_area) to
    // pxEndOfStack, the last slot aligned down to 16 B (tasks.c:1054-1064): the
    // created size, less 0-15 B of that alignment
    const auto *start = sa.sa_allocator != 0 ? static_cast<const StackType_t *>(sa.sa_tcbstack)
                                             : xTaskGetStackStart(task);
    row.stack_bytes = static_cast<uint32_t>((snap.pxEndOfStack - start + 1) *
                                            static_cast<std::ptrdiff_t>(sizeof(StackType_t)));
    // ESP-IDF reports the high-water mark in bytes, not words
    row.stack_free = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(task));
  }
  prvReleaseKernelLock();
  table.locked_us = esp_timer_get_time() - t0;
}

// One line: OK {"tasks":[{"name":..,"prio":..,"core":..,"stack_bytes":..,
// "stack_free":..,"coproc_pinned":..},...],"complete":true,"dup_handles":0,
// "locked_us":..}; core -1 = not pinned.
bool send_tasks(int sock) {
  auto table = std::make_unique<TaskTable>(); // ~2.5 KB: off this task's stack
  take_tasks(*table);
  std::string out = "OK {\"tasks\":[";
  for (UBaseType_t i = 0; i < table->count; ++i) {
    const TaskRow &row = table->rows[i];
    std::string name(row.name.data());
    // IDF's names and ours: keep the line valid JSON whatever they hold
    std::replace_if(
        name.begin(), name.end(), [](char c) { return c == '"' || c == '\\' || c < ' '; }, '?');
    out += std::string(i > 0 ? "," : "") + "{\"name\":\"" + name +
           "\",\"prio\":" + std::to_string(row.priority) +
           ",\"core\":" + std::to_string(row.core == tskNO_AFFINITY ? -1 : row.core) +
           ",\"stack_bytes\":" + std::to_string(row.stack_bytes) +
           ",\"stack_free\":" + std::to_string(row.stack_free) +
           ",\"coproc_pinned\":" + (row.coproc_pinned ? "true" : "false") + "}";
  }
  out += std::string("],\"complete\":") + (table->count < kMaxTasks ? "true" : "false") +
         ",\"dup_handles\":" + std::to_string(table->dup_handles) +
         ",\"locked_us\":" + std::to_string(table->locked_us) + "}";
  return send_line(sock, out);
}

bool handle(int sock, const std::string &line) {
  const std::vector<std::string> words = split(line);
  if (words.empty()) {
    return true;
  }
  const std::string &verb = words[0];

  if (verb == "PING") {
    return send_line(sock, "OK");
  }
  if (verb == "TASKS") {
    return send_tasks(sock);
  }
  if (verb == "SHOT") {
    const int step = arg(words, 1, 1) == 2 ? 2 : 1;
    return send_shot(sock, step);
  }
  if (verb == "FOCUS") {
    // Where input stands: the object the keypad (joystick) has focused, and
    // each pointer's state. For chasing "the cursor vanished" without a
    // debugger: x y w h are screen coordinates, state is LVGL's bit set.
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    std::string out = "OK";
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr;
         indev = lv_indev_get_next(indev)) {
      if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
        lv_group_t *g = lv_indev_get_group(indev);
        lv_obj_t *f = g != nullptr ? lv_group_get_focused(g) : nullptr;
        if (f == nullptr) {
          out += " keypad:none";
        } else {
          lv_area_t a;
          lv_obj_get_coords(f, &a);
          out += " keypad:" + std::to_string(a.x1) + "," + std::to_string(a.y1) + "," +
                 std::to_string(lv_area_get_width(&a)) + "x" +
                 std::to_string(lv_area_get_height(&a)) +
                 ",state=" + std::to_string(lv_obj_get_state(f)) +
                 ",groupsize=" + std::to_string(lv_group_get_obj_count(g));
        }
      } else if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        out += std::string(" pointer:") +
               (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED ? "down" : "up") + "@" +
               std::to_string(pt.x) + "," + std::to_string(pt.y);
      }
    }
    return send_line(sock, out);
  }
  if (verb == "SCREEN") {
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    const char *name = cfg.screen_name ? cfg.screen_name() : "?";
    return send_line(sock, std::string("OK ") + (name != nullptr ? name : "?"));
  }
  if (verb == "TAP") {
    touch_to(arg(words, 1), arg(words, 2), true);
    std::this_thread::sleep_for(std::chrono::milliseconds(kTapMs));
    hold_until_read();
    touch_down.store(false, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(kTapMs));
    hold_until_read();
    return send_line(sock, "OK");
  }
  if (verb == "PRESS") {
    touch_to(arg(words, 1), arg(words, 2), true);
    return send_line(sock, "OK");
  }
  if (verb == "RELEASE") {
    touch_down.store(false, std::memory_order_relaxed);
    return send_line(sock, "OK");
  }
  if (verb == "SWIPE") {
    const int32_t x0 = arg(words, 1);
    const int32_t y0 = arg(words, 2);
    const int32_t x1 = arg(words, 3);
    const int32_t y1 = arg(words, 4);
    const int ms = std::max(arg(words, 5, 300), kSwipeStepMs);
    const int steps = ms / kSwipeStepMs;
    touch_to(x0, y0, true);
    for (int i = 1; i <= steps; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kSwipeStepMs));
      touch_to(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, true);
    }
    touch_down.store(false, std::memory_order_relaxed);
    return send_line(sock, "OK");
  }
  if (verb == "KEY") {
    const std::string name = words.size() > 1 ? words[1] : "NONE";
    if (name == "ENTER") {
      if (cfg.press_select) {
        cfg.press_select();
      }
      return send_line(sock, "OK");
    }
    if (cfg.set_key) {
      cfg.set_key(key_from_name(name));
    }
    return send_line(sock, "OK");
  }
  if (verb == "BTN") {
    if (cfg.set_button) {
      cfg.set_button(arg(words, 1) != 0);
    }
    return send_line(sock, "OK");
  }
  if (verb == "THEME") {
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    ui_theme_set(static_cast<uint8_t>(arg(words, 1) != 0 ? UI_THEME_DAY : UI_THEME_DEFAULT));
    return send_line(sock, "OK");
  }
  return send_line(sock, "ERR unknown command: " + verb);
}

void serve(int client) {
  std::string pending;
  char chunk[256];
  while (true) {
    const int got = ::recv(client, chunk, sizeof(chunk), 0);
    if (got <= 0) {
      break;
    }
    pending.append(chunk, static_cast<size_t>(got));
    size_t nl = pending.find('\n');
    while (nl != std::string::npos) {
      std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!handle(client, line)) {
        return;
      }
      nl = pending.find('\n');
    }
  }
}

void server_task() {
  const int listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener < 0) {
    ESP_LOGE(kTag, "socket() failed: %d", errno);
    return;
  }
  int one = 1;
  ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(kRemoteUiPort);
  if (::bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
      ::listen(listener, 1) != 0) {
    ESP_LOGE(kTag, "bind/listen on %u failed: %d", kRemoteUiPort, errno);
    ::close(listener);
    return;
  }
  ESP_LOGW(kTag, "remote UI listening on %u -- DEBUG ONLY, it can press anything on screen",
           kRemoteUiPort);

  while (true) {
    const int client = ::accept(listener, nullptr, nullptr);
    if (client < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }
    // Nagle would sit on the one-line answers waiting for more to send, which
    // turns every command into a 40 ms round trip.
    ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    ESP_LOGI(kTag, "client connected");
    serve(client);
    ESP_LOGI(kTag, "client gone");
    // Whatever the client was holding down, it is not holding it now.
    touch_down.store(false, std::memory_order_relaxed);
    if (cfg.set_key) {
      cfg.set_key(0);
    }
    if (cfg.set_button) {
      cfg.set_button(false);
    }
    ::close(client);
  }
}

} // namespace

void remote_ui_start(const RemoteUiConfig &config) {
  cfg = config;
  {
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    // A second POINTER indev beside the GT911's, so real touch keeps working:
    // LVGL polls every indev and the one that reports a press wins.
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, pointer_read);
  }
  esp_pthread_cfg_t thread_cfg = esp_pthread_get_default_config();
  thread_cfg.stack_size = kStackBytes;
  thread_cfg.thread_name = "remote_ui";
  esp_pthread_set_cfg(&thread_cfg);
  std::thread(server_task).detach();
  // Back to the default, so threads started after this one are not all given
  // a renderer's stack.
  const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
  esp_pthread_set_cfg(&defaults);
}

#else // CONFIG_HMI_REMOTE_UI

void remote_ui_start(const RemoteUiConfig &) {}

#endif
