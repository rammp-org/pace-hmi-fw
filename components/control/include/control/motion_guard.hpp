#pragma once
// The motion guard (hazard fix C4, docs/plans/hazard-c4-spec.md): the ADC task's own check that
// the UI task, the link and the MCB are alive and that the MCB reports ENABLED. Its verdict is
// condition 2 of C1's output permit (C4 §2.1): a verdict other than OK sends the literal 0.
// C4 adds no gate and no re-arm of its own; C1's neutral latch re-arms (REQ-CTL-14).
//
// Pure C++: no ESP-IDF, no espp, no allocation, no logging, no lock, no indirect call
// (REQ-CTL-11). Time is a parameter: the ADC-side clock, uint32 ms, that main injects (C4 §3.1,
// hazard-fixes.md §10 item 22), so the host tests run it on fake time.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "hmi_rtps_spec.hpp"

namespace hmi::control {

/// UI heartbeat: stale at this age or older, or when never written (C4-a, D4 approved).
inline constexpr std::uint32_t UI_HEARTBEAT_MAX_AGE_MS = 200;
/// Newest MibStatus: stale at this age or older, or when none arrived (C4-c, D4 approved).
inline constexpr std::uint32_t MIB_STATUS_MAX_AGE_MS = 2000;
static_assert(MIB_STATUS_MAX_AGE_MS == rammp::kMibStatusTimeout.count(),
              "the guard's MibStatus age is the HMI's MibStatus timeout (C4 §2.3)");

/// The MibStatus systemState byte that allows motion (C4-d). Any other byte forces 0.
inline constexpr std::uint8_t MCB_STATE_ENABLED =
    static_cast<std::uint8_t>(MIB::MibSystemState::ENABLED);

/// The guard's verdict. Several conditions can hold at once; the verdict is the first in this
/// order (C4 §2.2: e, a, b, c, d). The order only picks the code: every non-OK forces 0.
enum class GuardReason : std::uint8_t {
  OK,              ///< every condition fresh: the permit's condition 2 holds
  WDT_MISSING,     ///< C4-e: the ADC or the UI task failed to subscribe to the TWDT
  UI_STALE,        ///< C4-a: UI heartbeat age >= 200 ms, or never written
  LINK_DOWN,       ///< C4-b: net failed, no link, or no IP
  MIB_STALE,       ///< C4-c: newest MibStatus age >= 2000 ms, or none since boot
  MCB_NOT_ENABLED, ///< C4-d: newest MibStatus systemState is not ENABLED
};
inline constexpr std::size_t kGuardReasonCount = 6;

/// A stamp's value before its writer's first store. The guard starts with every source "seen
/// stale at this stamp", so a source never written stays stale (REQ-CTL-03, REQ-CTL-04). A
/// writer whose first store happens to be this value (clock exactly 0, or a wrap) is seen
/// stale until its next store: conservative, never the other way.
inline constexpr std::uint32_t kStampInitial = 0;

/// Age of @p stamp_ms at @p now_ms, in whole ms modulo 2^32 (REQ-CTL-07). A stamp ahead of the
/// clock gives an age near 2^32, so it is stale.
[[nodiscard]] constexpr std::uint32_t age_ms(std::uint32_t now_ms, std::uint32_t stamp_ms) {
  return now_ms - stamp_ms; // unsigned: wraps modulo 2^32 by definition
}

/// One source's timestamp shared between tasks: 32-bit ms of the ADC-side clock, lock-free
/// (C4 §3.3: a 64-bit atomic is not lock-free on the P4). One writer, any readers.
class StampCell {
public:
  StampCell() = default;
  StampCell(const StampCell &) = delete;
  StampCell &operator=(const StampCell &) = delete;
  StampCell(StampCell &&) = delete;
  StampCell &operator=(StampCell &&) = delete;
  ~StampCell() = default;

  /// The writer: stamps @p now_ms (release).
  void store(std::uint32_t now_ms) noexcept { ms_.store(now_ms, std::memory_order_release); }
  /// A reader: the newest stamp (acquire).
  [[nodiscard]] std::uint32_t load() const noexcept { return ms_.load(std::memory_order_acquire); }

private:
  std::atomic<std::uint32_t> ms_{kStampInitial};
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "CS-OWN-03");
};

/// What the other tasks write for the guard (C4 §3.1). One writer per member.
struct GuardSources {
  StampCell ui_heartbeat;                 ///< lv_task, after each cycle()
  std::atomic<bool> ui_wdt_ok{false};     ///< lv_task, once, after subscribing to the TWDT
  std::atomic<std::uint8_t> mcb_state{0}; ///< rtps_worker, each MibStatus, before the stamp
  StampCell mcb_rx;                       ///< rtps_worker, each MibStatus, after the state

  static_assert(std::atomic<bool>::is_always_lock_free, "CS-OWN-03");
  static_assert(std::atomic<std::uint8_t>::is_always_lock_free, "CS-OWN-03");

  /// The UI task, after every completed cycle (REQ-UI-24).
  void note_ui_cycle(std::uint32_t now_ms) noexcept { ui_heartbeat.store(now_ms); }
  /// The UI task, once, with the TWDT subscription's result.
  void note_ui_wdt(bool subscribed) noexcept {
    ui_wdt_ok.store(subscribed, std::memory_order_release);
  }
  /// The RTPS receive task, for each MibStatus, before any handler or lock (REQ-CTL-04): the
  /// state first (relaxed), then the stamp (release), so a reader that sees stamp N sees a
  /// state from message N or newer (C4 §3.2 rule 3).
  void note_mib_status(std::uint8_t system_state, std::uint32_t now_ms) noexcept {
    mcb_state.store(system_state, std::memory_order_relaxed);
    mcb_rx.store(now_ms);
  }
};

/// The link's existing flags (rtps_comms) and the UI's gate, read by the ADC task.
struct GuardFlags {
  const std::atomic<bool> &net_failed;
  const std::atomic<bool> &link_up;
  const std::atomic<bool> &got_ip;
  const std::atomic<bool> &stick_drives; ///< only for the ui_stalls_drive counter
};

/// One cycle's inputs, as the ADC task loaded them.
struct GuardInputs {
  bool adc_wdt_ok;               ///< the ADC task's own subscription result
  bool ui_wdt_ok;                ///< the UI task's
  std::uint32_t ui_heartbeat_ms; ///< stamp of the UI's last completed cycle
  bool net_failed;               ///< the link's hardware failed to start
  bool link_up;                  ///< carrier / association
  bool got_ip;                   ///< an IP address
  std::uint32_t mcb_rx_ms;       ///< stamp of the newest MibStatus
  std::uint8_t mcb_state;        ///< its systemState byte
  bool stick_drives;             ///< the UI's gate (counter only, never the verdict)
};

/// Loads one cycle's inputs in C4 §3.2's order: the stamps first (acquire), then the MCB state,
/// then the clock, so no stamp is ahead of the returned @p now_ms.
/// @tparam Clock A callable returning the ADC-side clock in uint32 ms (main's, or a fake).
template <typename Clock>
[[nodiscard]] GuardInputs load_guard_inputs(const GuardSources &sources, const GuardFlags &flags,
                                            bool adc_wdt_ok, Clock &clock, std::uint32_t &now_ms) {
  GuardInputs in{};
  in.ui_heartbeat_ms = sources.ui_heartbeat.load();
  in.mcb_rx_ms = sources.mcb_rx.load();
  in.mcb_state = sources.mcb_state.load(std::memory_order_relaxed);
  in.adc_wdt_ok = adc_wdt_ok;
  in.ui_wdt_ok = sources.ui_wdt_ok.load(std::memory_order_acquire);
  in.net_failed = flags.net_failed.load(std::memory_order_acquire);
  in.link_up = flags.link_up.load(std::memory_order_acquire);
  in.got_ip = flags.got_ip.load(std::memory_order_acquire);
  in.stick_drives = flags.stick_drives.load(std::memory_order_acquire);
  now_ms = clock();
  return in;
}

/// One stamped source with its stale latch (REQ-CTL-07): fresh means its age is below the
/// limit; once seen stale it stays stale until its stamp changes, so a stamp 2^32 ms old cannot
/// look fresh again. Starts seen stale at kStampInitial (never written = stale).
class StampWatch {
public:
  /// Whether the source is stale at @p now_ms; advances the latch.
  [[nodiscard]] constexpr bool stale(std::uint32_t stamp_ms, std::uint32_t now_ms,
                                     std::uint32_t max_age_ms) noexcept {
    if (stamp_ms != seen_ms_) {
      seen_ms_ = stamp_ms;
      latched_ = false;
      written_ = true;
    }
    if (age_ms(now_ms, stamp_ms) >= max_age_ms) {
      latched_ = true;
    }
    return latched_;
  }
  /// Whether the stamp has changed at least once since start (the source was written).
  [[nodiscard]] constexpr bool written() const noexcept { return written_; }

private:
  std::uint32_t seen_ms_ = kStampInitial;
  bool latched_ = true;
  bool written_ = false;
};

/// The guard's observability, written by the ADC task only (C4 §3.1), read by the UI and the
/// self test. Every member is a lock-free atomic, stored relaxed.
struct GuardTelemetry {
  /// This cycle's verdict; not OK before the first cycle.
  std::atomic<std::uint8_t> reason{static_cast<std::uint8_t>(GuardReason::WDT_MISSING)};
  std::array<std::atomic<std::uint32_t>, kGuardReasonCount> trips{}; ///< onsets per reason
  std::atomic<std::uint32_t> ui_age_max_ms{0};                       ///< largest heartbeat age
  std::atomic<std::uint32_t> ui_stalls_drive{0}; ///< UI_STALE onsets while driving
};

/// The guard. One instance, owned by the ADC task; evaluate() once per cycle (REQ-CTL-01).
///
/// Counters (REQ-CTL-10): `trips(r)` counts each onset of condition r (it holds this cycle and
/// did not the cycle before), per condition, whichever reason is reported; before the first
/// evaluation every condition counts as holding, so a source that is stale from boot is not a
/// trip until it has been fresh once. `ui_stalls_drive` counts the UI_STALE onsets that happen
/// while `stick_drives` is true. `ui_age_max_ms` is the largest heartbeat age seen since the
/// heartbeat was first written. All only go up.
class MotionGuard {
public:
  /// The verdict for this cycle; advances the latches and the counters.
  [[nodiscard]] constexpr GuardReason evaluate(const GuardInputs &in,
                                               std::uint32_t now_ms) noexcept {
    const bool wdt_missing = !(in.adc_wdt_ok && in.ui_wdt_ok);
    const bool ui_stale = ui_.stale(in.ui_heartbeat_ms, now_ms, UI_HEARTBEAT_MAX_AGE_MS);
    const bool link_down = in.net_failed || !in.link_up || !in.got_ip;
    const bool mib_stale = mib_.stale(in.mcb_rx_ms, now_ms, MIB_STATUS_MAX_AGE_MS);
    const bool mcb_not_enabled = in.mcb_state != MCB_STATE_ENABLED;
    if (ui_.written()) {
      const std::uint32_t age = age_ms(now_ms, in.ui_heartbeat_ms);
      ui_age_max_ms_ = age > ui_age_max_ms_ ? age : ui_age_max_ms_;
    }
    if (ui_stale && !was_holding(GuardReason::UI_STALE) && in.stick_drives) {
      ++ui_stalls_drive_;
    }
    note(GuardReason::WDT_MISSING, wdt_missing);
    note(GuardReason::UI_STALE, ui_stale);
    note(GuardReason::LINK_DOWN, link_down);
    note(GuardReason::MIB_STALE, mib_stale);
    note(GuardReason::MCB_NOT_ENABLED, mcb_not_enabled);
    reason_ = wdt_missing       ? GuardReason::WDT_MISSING
              : ui_stale        ? GuardReason::UI_STALE
              : link_down       ? GuardReason::LINK_DOWN
              : mib_stale       ? GuardReason::MIB_STALE
              : mcb_not_enabled ? GuardReason::MCB_NOT_ENABLED
                                : GuardReason::OK;
    return reason_;
  }

  /// The last verdict; WDT_MISSING (not OK) before the first evaluation.
  [[nodiscard]] constexpr GuardReason reason() const noexcept { return reason_; }
  /// Onsets of condition @p r since start.
  [[nodiscard]] constexpr std::uint32_t trips(GuardReason r) const noexcept {
    return trips_[index(r)];
  }
  [[nodiscard]] constexpr std::uint32_t ui_age_max_ms() const noexcept { return ui_age_max_ms_; }
  [[nodiscard]] constexpr std::uint32_t ui_stalls_drive() const noexcept {
    return ui_stalls_drive_;
  }

  /// Stores the observability into @p out (relaxed), on the ADC task, after evaluate().
  void publish(GuardTelemetry &out) const noexcept {
    out.reason.store(static_cast<std::uint8_t>(reason_), std::memory_order_relaxed);
    for (std::size_t i = 0; i < kGuardReasonCount; ++i) {
      out.trips[i].store(trips_[i], std::memory_order_relaxed);
    }
    out.ui_age_max_ms.store(ui_age_max_ms_, std::memory_order_relaxed);
    out.ui_stalls_drive.store(ui_stalls_drive_, std::memory_order_relaxed);
  }

private:
  static constexpr std::size_t index(GuardReason r) noexcept { return static_cast<std::size_t>(r); }
  static constexpr std::uint8_t bit(GuardReason r) noexcept {
    return static_cast<std::uint8_t>(1U << index(r));
  }
  [[nodiscard]] constexpr bool was_holding(GuardReason r) const noexcept {
    return (holding_ & bit(r)) != 0;
  }
  constexpr void note(GuardReason r, bool holds) noexcept {
    if (holds && !was_holding(r)) {
      ++trips_[index(r)];
    }
    holding_ = holds ? static_cast<std::uint8_t>(holding_ | bit(r))
                     : static_cast<std::uint8_t>(holding_ & ~bit(r));
  }

  StampWatch ui_;
  StampWatch mib_;
  GuardReason reason_ = GuardReason::WDT_MISSING; // never OK before the first evaluation
  std::uint8_t holding_ = 0xFF; // before the first evaluation: everything holds (no onset)
  std::array<std::uint32_t, kGuardReasonCount> trips_{};
  std::uint32_t ui_age_max_ms_ = 0;
  std::uint32_t ui_stalls_drive_ = 0;
};

} // namespace hmi::control
