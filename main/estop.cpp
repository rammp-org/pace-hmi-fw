#include "estop.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <sys/time.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "cdr.hpp"
#include "estop_codec.hpp"
#include "hmi_rtps_spec.hpp"
#include "rtps_comms.hpp"

// Everything below is the e-stop island: one task owns the state, the ISR and the
// RTPS receive callback only count and queue. No heap after estop_start(), logging
// included -- the UI task logs from estop_view() instead.

namespace {

using std::chrono::duration_cast;
using std::chrono::microseconds;

constexpr int64_t us_of(std::chrono::milliseconds ms) {
  return duration_cast<microseconds>(ms).count();
}
constexpr int64_t kPeriodUs = us_of(rammp::kEStopPolicy.period);
constexpr int64_t kTimeoutUs = us_of(rammp::kEStopPolicy.timeout);

// The setting's values, in EStopPin order.
constexpr std::array<gpio_num_t, 2> kPins{GPIO_NUM_2, GPIO_NUM_47};
// EStop.reason while engaged, per pin. Short enough for std::string's in-place
// buffer, so assigning one never allocates.
constexpr std::array<const char *, 2> kReasons{"switch gpio2", "switch gpio47"};
static_assert(sizeof("switch gpio47") <= 16, "EStop.reason must fit std::string's SSO buffer");

// Above the LVGL task (20) and lwIP (18): it runs for microseconds a period, and a
// press must not wait behind a frame. Core 0, away from LVGL's core 1.
constexpr UBaseType_t kTaskPriority = 21;
constexpr BaseType_t kTaskCore = 0;
constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kRxQueueDepth = 8;
// Settling time after the pull-up goes on, before the edge interrupt is enabled: so
// the pin's own rise is not taken for a press.
constexpr TickType_t kSettleTicks = pdMS_TO_TICKS(2);

enum class Mode : uint8_t {
  NOT_ENGAGED, // sending state=NOT_ENGAGED: the heartbeat
  ENGAGED,     // sending state=ENGAGED until a clear attempt from the MIB is accepted
};

// A received EStop, as the task needs it, with when it arrived (steady clock).
struct Rx {
  int64_t rx_us;
  estop_codec::Decoded msg;
};

// ---- Shared with the ISR and the RTPS task: atomics and kernel objects only ----

std::atomic<uint32_t> press_edges{0};     // ISR: +1 per active edge
std::atomic<uint8_t> wanted_config{0xFF}; // estop_configure: pin | contact << 1
std::atomic<bool> active{true};           // estop_active(): engaged until the task says not
std::atomic<uint32_t> rx_dropped{0};      // RTPS task: undecodable, or the queue was full
TaskHandle_t task = nullptr;

StaticTask_t task_tcb;
StackType_t task_stack[kStackBytes]; // ESP-IDF counts stack in bytes
StaticQueue_t rx_queue_buf;
uint8_t rx_queue_storage[kRxQueueDepth * sizeof(Rx)];
QueueHandle_t rx_queue = nullptr;
StaticQueue_t view_queue_buf; // a mailbox: overwritten by the task, peeked by readers
uint8_t view_queue_storage[sizeof(EStopView)];
QueueHandle_t view_queue = nullptr;

// ---- Owned by the task alone ----

struct Island {
  Mode mode = Mode::NOT_ENGAGED;
  uint32_t pub_seq = 0;         // this HMI's message number: +1 per e-stop message sent
  uint32_t engaged_pub_seq = 0; // pub_seq of the first ENGAGED after the latest press
  bool press_unsent = false;    // a press whose first ENGAGED has not gone out yet
  uint32_t presses_seen = 0;    // press_edges, as last handled
  int64_t next_send_us = 0;
  int64_t mib_last_us = 0; // last EStop from the MIB; 0 = never
  bool mib_engaged = false;
  uint8_t config = 0xFF; // armed pin | contact << 1; 0xFF = none
  gpio_num_t pin = GPIO_NUM_NC;
  EStopContact contact = EStopContact::NO;
  EStopView view;
};
Island island;

rammp::EStop tx_msg;               // built once in estop_start(); only POD fields change
std::array<uint8_t, 128> tx_buf{}; // the largest HMI EStop is well under this

void IRAM_ATTR on_edge(void *) {
  press_edges.fetch_add(1, std::memory_order_relaxed);
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(task, &woken);
  portYIELD_FROM_ISR(woken);
}

constexpr uint8_t config_of(EStopPin pin, EStopContact contact) {
  return static_cast<uint8_t>(static_cast<uint8_t>(pin) | (static_cast<uint8_t>(contact) << 1));
}

bool switch_pressed(const Island &s) {
  if (s.pin == GPIO_NUM_NC) {
    return false;
  }
  const int level = gpio_get_level(s.pin);
  return s.contact == EStopContact::NO ? level == 0 : level == 1;
}

void disarm(Island &s) {
  if (s.pin == GPIO_NUM_NC) {
    return;
  }
  // Best effort: a pin that will not let go still reads as not armed below.
  (void)gpio_intr_disable(s.pin);
  (void)gpio_isr_handler_remove(s.pin);
  s.pin = GPIO_NUM_NC;
  s.view.armed = false;
}

// Input with the pull-up, interrupt on the pressing edge only: falling for NO,
// rising for NC. Only the edge is counted, not the level, so a press released
// before the task runs is still a press.
bool arm(Island &s, uint8_t config) {
  disarm(s);
  // Shown as the configured one even if arming fails, so the banner names what to check.
  s.view.pin = static_cast<EStopPin>(config & 0x1u);
  s.view.contact = static_cast<EStopContact>((config >> 1) & 0x1u);
  const auto pin_index = static_cast<size_t>(config & 0x1u);
  const auto contact = static_cast<EStopContact>((config >> 1) & 0x1u);
  const gpio_num_t pin = kPins[pin_index];
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&io) != ESP_OK) {
    return false;
  }
  vTaskDelay(kSettleTicks);
  const gpio_int_type_t edge = contact == EStopContact::NO ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
  if (gpio_set_intr_type(pin, edge) != ESP_OK ||
      gpio_isr_handler_add(pin, on_edge, nullptr) != ESP_OK) {
    return false;
  }
  if (gpio_intr_enable(pin) != ESP_OK) {
    (void)gpio_isr_handler_remove(pin);
    return false;
  }
  s.pin = pin;
  s.contact = contact;
  s.view.armed = true;
  return true;
}

void press(Island &s) {
  s.mode = Mode::ENGAGED;
  s.press_unsent = true; // a new engaged_pub_seq, so a clear attempt already in flight is refused
}

rammp::Time stamp_now() {
  timeval tv{};
  // Before the MIB has set the clock it reads 1970: send the all-zero "unstamped".
  constexpr int64_t kClockSetAfter = 1'600'000'000; // 2020-09-13
  if (gettimeofday(&tv, nullptr) != 0 || tv.tv_sec < kClockSetAfter) {
    return {};
  }
  return {static_cast<int32_t>(tv.tv_sec), static_cast<uint32_t>(tv.tv_usec) * 1000u};
}

void send(Island &s, int64_t now) {
  const bool engaged = s.mode == Mode::ENGAGED;
  s.pub_seq++;
  if (engaged && s.press_unsent) {
    s.engaged_pub_seq = s.pub_seq;
    s.press_unsent = false;
  }
  tx_msg.header.stamp = stamp_now();
  tx_msg.state = engaged ? rammp::EStopState::ENGAGED : rammp::EStopState::NOT_ENGAGED;
  tx_msg.reason = engaged ? kReasons[static_cast<size_t>(s.view.pin)] : "";
  tx_msg.pub_seq = s.pub_seq;
  s.next_send_us = now + kPeriodUs;
  s.view.pub_seq = s.pub_seq;
  const auto size =
      cdr::serialize_into<cdr::xcdr1>(tx_msg, std::as_writable_bytes(std::span(tx_buf)));
  if (!size) {
    s.view.tx_errors++;
    return;
  }
  // Not up yet (no IP, no participant) is not an error: the next period resends.
  if (!rtps_comms_publish_estop(std::span<const uint8_t>(tx_buf.data(), *size)) &&
      rtps_comms_estop_ready()) {
    s.view.tx_errors++;
  }
}

// A clear attempt for this HMI, from the MIB. Accepted only if it covers the latest
// press (clear_pub_seq >= engaged_pub_seq), is fresh, and the switch is not still
// held -- otherwise ENGAGED keeps going out and the MIB has to try again.
void on_clear_attempt(Island &s, const Rx &rx, int64_t now) {
  if (s.mode != Mode::ENGAGED) {
    return; // the MIB resends its attempt until it sees our NOT_ENGAGED: nothing to do
  }
  const bool fresh = now - rx.rx_us <= kPeriodUs;
  const bool covers =
      !s.press_unsent && rammp::estop_seq_at_least(rx.msg.clear_pub_seq, s.engaged_pub_seq);
  if (!fresh || !covers || switch_pressed(s)) {
    s.view.clear_attempts_refused++;
    return;
  }
  s.mode = Mode::NOT_ENGAGED;
  s.view.clear_attempts_accepted++;
  send(s, now); // NOT_ENGAGED at once: it is what lets the MIB release the stop
}

void on_rx(Island &s, const Rx &rx, int64_t now) {
  if (rx.msg.from_self || !rx.msg.from_clearer) {
    return; // our own sample, or another node's: the MIB's latch speaks for them
  }
  s.mib_last_us = rx.rx_us;
  s.mib_engaged = rx.msg.engaged;
  if (rx.msg.engaged) {
    estop_codec::copy_text(s.view.remote_source, rx.msg.source);
    estop_codec::copy_text(s.view.remote_reason, rx.msg.reason);
  } else if (rx.msg.clear_for_self) {
    on_clear_attempt(s, rx, now);
  }
}

void publish_view(Island &s, int64_t now) {
  s.view.mib_fresh = s.mib_last_us != 0 && now - s.mib_last_us <= kTimeoutUs;
  s.view.local = s.mode == Mode::ENGAGED;
  s.view.remote = s.view.mib_fresh && s.mib_engaged;
  s.view.presses = s.presses_seen;
  s.view.rx_errors = rx_dropped.load(std::memory_order_relaxed);
  // cppcheck-suppress knownConditionTrueFalse ; kEStopNeedsMib is a build-time switch
  const bool link_missing = rammp::kEStopNeedsMib && !s.view.mib_fresh;
  active.store(s.view.local || s.view.remote || !s.view.armed || link_missing);
  (void)xQueueOverwrite(view_queue, &s.view);
}

void task_main(void *) {
  Island &s = island;
  for (;;) {
    const int64_t before = esp_timer_get_time();
    const int64_t wait_us = s.next_send_us > before ? s.next_send_us - before : 0;
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_us / 1000) + 1);
    const int64_t now = esp_timer_get_time();
    bool send_now = false;

    // A new pin or contact from Settings, or arming failed last time (retried every
    // period; until it works estop_active() holds). A switch already held on the
    // new pin is a press: the edge interrupt alone would never see it.
    if (const uint8_t want = wanted_config.load(); want != s.config || !s.view.armed) {
      s.config = want;
      if (arm(s, want) && switch_pressed(s)) {
        press(s);
        send_now = true;
      }
    }

    if (const uint32_t edges = press_edges.load(); edges != s.presses_seen) {
      s.presses_seen = edges;
      press(s);
      send_now = true; // engaging does not wait for the period
    }

    Rx rx{};
    for (UBaseType_t i = 0; i < kRxQueueDepth && xQueueReceive(rx_queue, &rx, 0) == pdTRUE; i++) {
      on_rx(s, rx, now);
    }

    if (send_now || now >= s.next_send_us) {
      send(s, now);
    }
    publish_view(s, now);
  }
}

} // namespace

bool estop_start(EStopPin pin, EStopContact contact) {
  if (task != nullptr) {
    return true;
  }
  // The GPIO ISR service is not installed here: the GPIO48 button installs it first,
  // and a second install only logs an error. Without it gpio_isr_handler_add fails,
  // the task retries every period, and estop_active() holds until it works.
  rx_queue = xQueueCreateStatic(kRxQueueDepth, sizeof(Rx), rx_queue_storage, &rx_queue_buf);
  view_queue = xQueueCreateStatic(1, sizeof(EStopView), view_queue_storage, &view_queue_buf);
  if (rx_queue == nullptr || view_queue == nullptr) {
    return false;
  }
  // Every string the HMI sends, set once here: from now on only the POD fields change
  // and the reason swaps between strings that fit the in-place buffer.
  tx_msg.header.frame_id = "";
  tx_msg.source = rammp::kEStopSelf;
  tx_msg.reason = kReasons[1];
  tx_msg.clear_target = "";
  tx_msg.clear_pub_seq = 0;
  island.view.pin = pin;
  island.view.contact = contact;
  (void)xQueueOverwrite(view_queue, &island.view);
  wanted_config.store(config_of(pin, contact));
  task = xTaskCreateStaticPinnedToCore(task_main, "estop", kStackBytes, nullptr, kTaskPriority,
                                       task_stack, &task_tcb, kTaskCore);
  return task != nullptr;
}

void estop_configure(EStopPin pin, EStopContact contact) {
  wanted_config.store(config_of(pin, contact));
  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
}

bool estop_active() { return active.load(); }

EStopView estop_view() {
  EStopView view;
  if (view_queue == nullptr || xQueuePeek(view_queue, &view, 0) != pdTRUE) {
    view.local = true; // not started: say engaged, as estop_active() does
  }
  return view;
}

void estop_on_sample(std::span<const uint8_t> cdr_payload) {
  Rx rx{};
  rx.rx_us = esp_timer_get_time(); // receive time on the steady clock: what freshness means
  if (!estop_codec::decode(cdr_payload, rammp::kEStopSelf, rammp::kEStopClearer, rx.msg)) {
    rx_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (rx.msg.from_self) {
    return; // our own sample, looped back
  }
  if (rx_queue == nullptr || task == nullptr || xQueueSend(rx_queue, &rx, 0) != pdTRUE) {
    rx_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  xTaskNotifyGive(task);
}
