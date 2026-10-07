#include "log_capture.hpp"

#include <algorithm>
#include <array>
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

// The stream stdout and stderr point at. A plain picolibc stream: stdio hands
// it one character at a time through put() and keeps no buffer of its own.
//
// It used to be a funopen() stream, whose 128-byte buffer is the last 128 bytes
// of its 208-byte heap block. picolibc's fwrite can leave that buffer exactly
// full without flushing it, and its __bufio_put stores the next character
// before it checks for room: one byte past the buffer, into the size word of
// the next heap block (the stream's own lock). An espp line (one fwrite) that
// filled the buffer to the byte, then an IDF line (vprintf), wrote its 'E'
// there; the next heap walk (spi_flash's bounce buffer, at fw_info's first
// read) took 0x45 for a free block of 0x44 and faulted (final-a9a040f, boot
// panic of 2026-10-06). Here the only buffer is `line`, and put() never writes
// past it.
int tee_put(char c, FILE *);
int tee_flush(FILE *);

constexpr size_t kTeeLineBytes = 256; // a longer line goes out in pieces

struct Tee {
  FILE file;
  FILE *console; // the original stdout: the serial console
  std::array<char, kTeeLineBytes> line;
  size_t used;
};
// picolibc's FDEV_SETUP_STREAM, spelled out: in C++ every member needs its
// initializer (-Wmissing-field-initializers). The lock starts empty and stdio
// creates it on first use (__flockfile_init).
Tee tee{.file = {.unget = 0,
                 .flags = _FDEV_SETUP_WRITE,
                 .put = tee_put,
                 .get = nullptr,
                 .flush = tee_flush,
                 .lock = {}},
        .console = nullptr,
        .line = {},
        .used = 0};

// The line so far to the console, unchanged, then into the ring. A print made
// from inside this one (same task: the stream's lock is recursive) can cut the
// line short; it cannot write outside `line`.
void write_line() {
  const size_t n = std::min(tee.used, tee.line.size());
  if (n == 0) {
    return;
  }
  // The console first, and outside the ring's lock: a slow UART must never
  // hold up the LogScreen reading the ring, nor the reverse.
  std::fwrite(tee.line.data(), 1, n, tee.console);
  std::fflush(tee.console);
  {
    std::lock_guard<std::mutex> lock(ring_mutex);
    for (size_t i = 0; i < n; ++i) {
      feed(tee.line[i]);
    }
  }
  tee.used = 0;
}

// Every character any task prints. stdio calls it under the stream's lock
// (picolibc __STDIO_LOCKING), so one task at a time.
int tee_put(char c, FILE *) {
  const size_t used = tee.used;
  if (used < tee.line.size()) {
    tee.line[used] = c;
    tee.used = used + 1;
  }
  if (c == '\n' || tee.used >= tee.line.size()) {
    write_line();
  }
  return static_cast<unsigned char>(c);
}

int tee_flush(FILE *) {
  write_line();
  return 0;
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
  std::fflush(stdout);
  tee.console = stdout;
  // In IDF's picolibc these are plain globals shared by every task
  // (esp_libc/src/picolibc/picolibc_init.c), so this reaches tasks that are
  // already running as well as the ones started later.
  stdout = &tee.file;
  stderr = &tee.file;
}

bool log_capture_active() {
  return ring != nullptr && tee.console != nullptr && stdout == &tee.file;
}

uint32_t log_capture_count() {
  std::lock_guard<std::mutex> lock(ring_mutex);
  return count;
}

uint32_t log_capture_visit(const std::function<void(LogLevel, std::string_view)> &visit) {
  std::lock_guard<std::mutex> lock(ring_mutex);
  if (ring == nullptr) {
    return 0;
  }
  const uint32_t kept = std::min<uint32_t>(count, kLogCaptureLines);
  for (uint32_t i = count - kept; i < count; ++i) {
    const Line &line = ring[i % kLogCaptureLines];
    visit(line.level, std::string_view(line.text, line.len));
  }
  return count;
}
