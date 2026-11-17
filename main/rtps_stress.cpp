#include "rtps_stress.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "hmi_estop.hpp"
#include "logger.hpp"
#include "rtps_comms.hpp"
#include "rtps_participant.hpp"
#include "task.hpp"

using namespace std::chrono_literals;

namespace {

// ROS 2 names, so `ros2 topic pub /rammp/stress/to_hmi std_msgs/msg/UInt8MultiArray` works.
constexpr char kTopic[] = "rt/rammp/stress/to_hmi";
constexpr char kType[] = "std_msgs::msg::dds_::UInt8MultiArray_";

// The sample, XCDR1 little-endian: 4-byte encapsulation, then layout.dim (a sequence,
// length 0 from us), layout.data_offset, data's length, data. data starts with
// StressHeader; the rest is padding to the size under test.
constexpr size_t kDataLengthAt = 4 + 4 + 4; // encapsulation, dim length, data_offset
constexpr size_t kDataAt = kDataLengthAt + 4;
struct StressHeader {
  uint32_t sender; // the stand-in's participant id
  uint32_t seq;    // per sender, from 1
};

constexpr size_t kMaxSenders = 128; // participant ids are < 120 (RTPS port range)
constexpr auto kPeriod = 1s;
constexpr int kSendersEvery = 5; // STRESS_SENDERS once per this many STRESS lines

struct Sender {
  uint32_t rx = 0;   // samples, cumulative
  uint32_t lost = 0; // gaps in seq, cumulative
  uint32_t last_seq = 0;
};

espp::Logger logger({.tag = "rtps_stress", .level = espp::Logger::Verbosity::INFO});

espp::RtpsParticipant *participant = nullptr;
std::mutex senders_mutex;
std::array<Sender, kMaxSenders> senders;
std::atomic<uint32_t> rx_total{0};
std::atomic<uint32_t> rx_bytes{0};
std::atomic<uint32_t> rx_bad{0}; // too short, or a sender id out of range
std::unique_ptr<espp::Task> stats_task;

uint32_t read_u32(std::span<const uint8_t> p, size_t at) {
  uint32_t v;
  std::memcpy(&v, p.data() + at, sizeof v);
  return v;
}

void on_sample(std::span<const uint8_t> cdr) {
  rx_total.fetch_add(1, std::memory_order_relaxed);
  rx_bytes.fetch_add(cdr.size(), std::memory_order_relaxed);
  if (cdr.size() < kDataAt + sizeof(StressHeader) || read_u32(cdr, 4) != 0 ||
      read_u32(cdr, kDataLengthAt) < sizeof(StressHeader)) {
    rx_bad.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t id = read_u32(cdr, kDataAt);
  const uint32_t seq = read_u32(cdr, kDataAt + 4);
  if (id >= kMaxSenders) {
    rx_bad.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  std::lock_guard lock(senders_mutex);
  Sender &s = senders[id];
  if (s.last_seq != 0 && seq > s.last_seq + 1) {
    s.lost += seq - s.last_seq - 1;
  }
  if (seq < s.last_seq) { // the stand-in restarted: a new stream
    s.last_seq = 0;
  }
  s.last_seq = seq;
  s.rx++;
}

// One line a second, cumulative counters: the PC takes the differences.
bool stats_tick(std::mutex &m, std::condition_variable &cv) {
  static int tick = 0;
  const auto &p = *participant;
  const RtpsMcbStats mib = rtps_comms_mcb_stats();
  const int64_t mib_age_ms = mib.last_us ? (esp_timer_get_time() - mib.last_us) / 1000 : -1;
  size_t active = 0;
  {
    std::lock_guard lock(senders_mutex);
    for (const auto &s : senders) {
      active += s.rx != 0;
    }
  }
  logger.info("STRESS t={} heap={} heap_min={} psram={} m_stress={} m_joy={} m_mib={} "
              "m_estop_w={} m_estop_r={} rx={} rx_bytes={} rx_bad={} senders={} "
              "mib_rx={} mib_lost={} mib_gap_ms={} mib_age_ms={}",
              esp_timer_get_time() / 1000, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
              heap_caps_get_free_size(MALLOC_CAP_SPIRAM), p.matched_writers(kTopic),
              p.matched_readers(rammp::kJoystickXYTwist.name),
              p.matched_writers(rammp::kMibStatus.name), p.matched_writers(rammp::kEStop.name),
              p.matched_readers(rammp::kEStop.name), rx_total.load(), rx_bytes.load(),
              rx_bad.load(), active, mib.samples, mib.lost, mib.max_gap_us / 1000, mib_age_ms);
  if (++tick % kSendersEvery == 0) {
    std::string line;
    {
      std::lock_guard lock(senders_mutex);
      for (size_t id = 0; id < senders.size(); id++) {
        if (senders[id].rx != 0) {
          line += fmt::format(" {}:{}/{}", id, senders[id].rx, senders[id].lost);
        }
      }
    }
    logger.info("STRESS_SENDERS{}", line);
  }
  std::unique_lock lock(m);
  cv.wait_for(lock, kPeriod);
  return false;
}

} // namespace

bool rtps_stress_attach(espp::RtpsParticipant &p) {
  participant = &p;
  if (!p.add_reader({.topic = kTopic, .type_name = kType, .on_sample = on_sample})) {
    logger.error("Could not add the stress reader (the stress overlay raises the pools)");
    return false;
  }
  stats_task = std::make_unique<espp::Task>(espp::Task::Config{
      .callback = stats_tick,
      .task_config = {.name = "rtps_stress", .stack_size_bytes = 6 * 1024, .priority = 3}});
  stats_task->start();
  logger.info("Stress reader on {} up; STRESS lines every second", kTopic);
  return true;
}
