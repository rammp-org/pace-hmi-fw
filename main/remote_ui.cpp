#include "remote_ui.hpp"

#if CONFIG_HMI_REMOTE_UI

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "esp_log.h"
#include "esp_pthread.h"
#include "lvgl.h"
#include "lwip/sockets.h"
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

lv_indev_t *pointer = nullptr;

void pointer_read(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = touch_x.load(std::memory_order_relaxed);
  data->point.y = touch_y.load(std::memory_order_relaxed);
  data->state =
      touch_down.load(std::memory_order_relaxed) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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
/// The lock is held for the whole send. Letting go between rows would let the
/// LVGL task redraw into the buffer mid-transfer and tear the capture.
bool send_shot(int sock, int step) {
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

  const int32_t out_w = buf->header.w / step;
  const int32_t out_h = buf->header.h / step;
  bool ok = send_line(sock, "FRAME " + std::to_string(out_w) + " " + std::to_string(out_h) + " " +
                                std::to_string(static_cast<size_t>(out_w) * out_h * 2));
  std::vector<uint16_t> row(static_cast<size_t>(out_w));
  for (int32_t y = 0; ok && y < out_h; y++) {
    const auto *src = reinterpret_cast<const uint16_t *>(buf->data + static_cast<size_t>(y) * step *
                                                                         buf->header.stride);
    if (step == 1) {
      ok = send_all(sock, src, static_cast<size_t>(out_w) * 2);
      continue;
    }
    for (int32_t x = 0; x < out_w; x++) {
      row[static_cast<size_t>(x)] = src[x * step];
    }
    ok = send_all(sock, row.data(), row.size() * 2);
  }
  return ok;
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

bool handle(int sock, const std::string &line) {
  const std::vector<std::string> words = split(line);
  if (words.empty()) {
    return true;
  }
  const std::string &verb = words[0];

  if (verb == "PING") {
    return send_line(sock, "OK");
  }
  if (verb == "SHOT") {
    const int step = arg(words, 1, 1) == 2 ? 2 : 1;
    return send_shot(sock, step);
  }
  if (verb == "SCREEN") {
    std::lock_guard<std::recursive_mutex> lock(*cfg.lvgl_mutex);
    const char *name = cfg.screen_name ? cfg.screen_name() : "?";
    return send_line(sock, std::string("OK ") + (name != nullptr ? name : "?"));
  }
  if (verb == "TAP") {
    touch_to(arg(words, 1), arg(words, 2), true);
    std::this_thread::sleep_for(std::chrono::milliseconds(kTapMs));
    touch_down.store(false, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(kTapMs));
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
