// funopen() is a BSD extension in picolibc's stdio.h
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include "log_capture.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "esp_heap_caps.h"

namespace {

struct Line {
  LogLevel level;
  uint8_t len;
  char text[kLogCaptureLineLen];
};
static_assert(kLogCaptureLineLen <= UINT8_MAX, "Line::len is a uint8_t");

Line *ring = nullptr; // kLogCaptureLines of them, in PSRAM
uint32_t count = 0;   // lines committed since boot; the newest is ring[(count - 1) % size]
std::mutex ring_mutex;
FILE *console = nullptr; // the original stdout: the serial console

// The line being assembled: a write can end mid-line, and a line can arrive in
// several writes.
Line partial{};
bool truncated = false;

// ANSI escape parsing. The espp loggers colour every line, and the colour is
// the only level marker their lines carry in a form worth trusting.
enum class Esc : uint8_t { None, Start, Csi };
Esc esc = Esc::None;
int esc_param = 0;

void mark_level(LogLevel level) { partial.level = std::max(partial.level, level); }

void put(char c) {
  if (partial.len < kLogCaptureLineLen) {
    partial.text[partial.len++] = c;
  } else {
    truncated = true;
  }
}

void commit() {
  if (truncated) {
    std::memcpy(partial.text + kLogCaptureLineLen - 3, "...", 3);
  }
  // IDF's own log lines are not coloured here, only lettered: "E (123) tag: ..."
  if (partial.len >= 3 && partial.text[1] == ' ' && partial.text[2] == '(') {
    if (partial.text[0] == 'E') {
      mark_level(LogLevel::Error);
    } else if (partial.text[0] == 'W') {
      mark_level(LogLevel::Warning);
    }
  }
  ring[count % kLogCaptureLines] = partial;
  count++;
  partial = Line{};
  truncated = false;
}

void feed(char c) {
  const auto byte = static_cast<unsigned char>(c);
  if (esc == Esc::Start) {
    esc = c == '[' ? Esc::Csi : Esc::None;
    esc_param = 0;
    return;
  }
  if (esc == Esc::Csi) {
    if (c >= '0' && c <= '9') {
      esc_param = esc_param * 10 + (c - '0');
      return;
    }
    // A parameter ended. Red and yellow foreground mark the line: the espp
    // loggers print errors in 31 and warnings in 33.
    if (esc_param == 31) {
      mark_level(LogLevel::Error);
    } else if (esc_param == 33) {
      mark_level(LogLevel::Warning);
    }
    esc_param = 0;
    if (c != ';') {
      esc = Esc::None; // any final byte ends the sequence
    }
    return;
  }
  if (byte == 0x1b) {
    esc = Esc::Start;
  } else if (c == '\n') {
    commit();
  } else if (c == '\t') {
    put(' ');
  } else if (byte >= 0x80) {
    // Montserrat carries ASCII only. A UTF-8 lead byte becomes '-' (the log's
    // usual non-ASCII is an em dash) and its continuation bytes vanish.
    if (byte >= 0xC0) {
      put('-');
    }
  } else if (byte >= 0x20 && byte != 0x7f) {
    put(c);
  }
  // '\r' and the other control characters are dropped
}

// The classic BSD funopen() write signature, which is the declaration the
// build sees (not the _ssize_t/size_t variant further down picolibc's stdio.h).
int tee_write(void *, const char *buf, int n) {
  if (n <= 0) {
    return 0;
  }
  // The console first, unchanged, and outside the ring's lock: a slow UART
  // must never hold up the LogScreen reading the ring, nor the reverse.
  std::fwrite(buf, 1, static_cast<size_t>(n), console);
  std::fflush(console);
  std::lock_guard<std::mutex> lock(ring_mutex);
  for (int i = 0; i < n; ++i) {
    feed(buf[i]);
  }
  return n;
}

} // namespace

void log_capture_start() {
  if (ring != nullptr) {
    return;
  }
  ring = static_cast<Line *>(heap_caps_calloc(kLogCaptureLines, sizeof(Line), MALLOC_CAP_SPIRAM));
  if (ring == nullptr) {
    return;
  }
  FILE *tee = funopen(nullptr, nullptr, tee_write, nullptr, nullptr);
  if (tee == nullptr) {
    heap_caps_free(ring);
    ring = nullptr;
    return;
  }
  // line-buffered, like the console it stands in front of
  setvbuf(tee, nullptr, _IOLBF, 256);
  std::fflush(stdout);
  console = stdout;
  // In IDF's picolibc these are plain globals shared by every task
  // (esp_libc/src/picolibc/picolibc_init.c), so this reaches tasks that are
  // already running as well as the ones started later.
  stdout = tee;
  stderr = tee;
}

bool log_capture_active() { return ring != nullptr && console != nullptr && stdout != console; }

uint32_t log_capture_count() {
  std::lock_guard<std::mutex> lock(ring_mutex);
  return count;
}

void log_capture_visit(const std::function<void(LogLevel, std::string_view)> &visit) {
  std::lock_guard<std::mutex> lock(ring_mutex);
  if (ring == nullptr) {
    return;
  }
  const uint32_t kept = std::min<uint32_t>(count, kLogCaptureLines);
  for (uint32_t i = count - kept; i < count; ++i) {
    const Line &line = ring[i % kLogCaptureLines];
    visit(line.level, std::string_view(line.text, line.len));
  }
}
