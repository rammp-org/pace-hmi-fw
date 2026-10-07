#pragma once

/// @file stick_inject_ui.hpp
/// @brief Bench stick injection in the firmware: the channel from the remote UI to the ADC
///        task, the ADC task's hook, and the on-screen "STICK INJECTED" marker.
/// @details BENCH ONLY: CONFIG_HMI_BENCH_STICK_INJECT (depends on CONFIG_HMI_REMOTE_UI, off by
///          default, never in sdkconfig.defaults; hazard-fixes.md §3 B1). The logic (parse,
///          expiry, fail_mask) is hmi::stick's bench_inject.hpp, host-tested (STK-040..049).
///
///          Who touches what (CS-OWN; one writer and one reader per channel):
///
///          | Member      | Kind                          | Written by           | Read by    |
///          | ----------- | ----------------------------- | -------------------- | ---------- |
///          | `mailbox_`  | fw::Mailbox<StickInjectMsg>   | remote_ui task       | ADC task   |
///          | `injector_` | ADC task state                | ADC task             | ADC task   |
///          | `active_`   | fw::AtomicValue<bool>         | ADC task             | LVGL task  |
///          | `marker_`   | the label, LVGL task state    | LVGL task            | LVGL task  |
///
///          app_main holds a `StickInjectSlot`: this class when the option is on, else
///          NoStickInject, an empty type whose functions are never defined. Every use sits in an
///          `if constexpr (BENCH_STICK_INJECT)` arm, so a release build compiles and links none
///          of it (CS-LAY-09, CS-TYP-05).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include "fw_core/atomic_value.hpp"
#include "fw_core/mailbox.hpp"
#include "lvgl.h"
#include "sdkconfig.h"
#include "stick/bench_inject.hpp"

/// @brief CONFIG_HMI_BENCH_STICK_INJECT as a constexpr (CS-TYP-05; the hidden _AS_INT is always
///        defined, so no #ifdef is needed).
inline constexpr bool BENCH_STICK_INJECT = CONFIG_HMI_BENCH_STICK_INJECT_AS_INT != 0;

/// @brief The one-slot channel the STICK verb writes and the ADC task drains.
using StickInjectMailbox = hmi::fw::Mailbox<hmi::stick::StickInjectMsg>;
/// @brief Its write end, the remote UI's only access to the injection.
using StickInjectWriter = hmi::fw::Writer<StickInjectMailbox>;

/// @brief Gives the remote UI's STICK verb its write end (the mailbox's one writer). Defined in
///        remote_ui.cpp, only when CONFIG_HMI_REMOTE_UI is on. Call once from app_main, inside
///        `if constexpr (BENCH_STICK_INJECT)` and BEFORE remote_ui_start, so the server task
///        starts with it set.
/// @param writer The write end, from StickInjectBench::writer().
void remote_ui_attach_stick_inject(StickInjectWriter writer);

/// @brief The bench stick injection: channel, ADC hook and marker. See the file comment.
class StickInjectBench {
public:
  StickInjectBench() = default;
  StickInjectBench(const StickInjectBench &) = delete;
  StickInjectBench &operator=(const StickInjectBench &) = delete;
  StickInjectBench(StickInjectBench &&) = delete;
  StickInjectBench &operator=(StickInjectBench &&) = delete;
  ~StickInjectBench() = default;

  /// @brief The STICK verb's write end, for the remote UI (the one writer). app_main, once.
  /// @return A handle that can only write.
  [[nodiscard]] StickInjectWriter writer() noexcept { return hmi::fw::writer(mailbox_); }

  /// @brief ADC task only (the mailbox's one reader): drains the mailbox and returns the reads
  ///        to pass to StickPipeline::cycle: the injected ones while an injection holds (300 ms
  ///        after the last STICK), else @p real unchanged. Never blocks.
  /// @param real This cycle's ADC reads.
  /// @return The reads the stick pipeline gets.
  [[nodiscard]] hmi::stick::RawReadsMv apply(const hmi::stick::RawReadsMv &real) noexcept {
    const auto now = std::chrono::steady_clock::now();
    hmi::stick::StickInjectMsg msg{};
    switch (mailbox_.read(msg)) {
    case hmi::fw::ReadStatus::CHANGED:
      injector_.note(msg, now);
      break;
    case hmi::fw::ReadStatus::UNCHANGED:
      break;
    case hmi::fw::ReadStatus::WRONG_TASK:
      // Not the ADC task (the read end reported it): touch nothing it owns; the real stick.
      return real;
    }
    active_.write(injector_.active(now));
    return injector_.apply(real, now);
  }

  /// @brief Builds the hidden marker and starts its poll timer. Once, from app_main during
  ///        start-up, under lvgl_mutex (as remote_ui_start builds its input device); after
  ///        that only the LVGL task touches either. Allocation can fail only here, at
  ///        start-up, where aborting is allowed (CS-ERR-04).
  void start_marker() noexcept {
    marker_ = lv_label_create(lv_layer_top());
    if (marker_ == nullptr ||
        lv_timer_create(&StickInjectBench::marker_poll, MARKER_POLL_MS, this) == nullptr) {
      std::abort(); // a bench build that cannot show an injection must not run one
    }
    lv_label_set_text_static(marker_, "STICK INJECTED");
    lv_obj_set_style_bg_color(marker_, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(marker_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(marker_, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(marker_, &lv_font_montserrat_34, LV_PART_MAIN);
    lv_obj_set_style_pad_all(marker_, 12, LV_PART_MAIN);
    lv_obj_align(marker_, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_add_flag(marker_, LV_OBJ_FLAG_HIDDEN);
  }

private:
  static constexpr std::uint32_t MARKER_POLL_MS = 100;

  /// LVGL task (an lv_timer): shows the marker while the ADC task reports an injection, on top
  /// of anything else on the top layer (the self-test overlay).
  static void marker_poll(lv_timer_t *timer) {
    auto *self = static_cast<StickInjectBench *>(lv_timer_get_user_data(timer));
    const bool on = self->active_.read();
    if (on == !lv_obj_has_flag(self->marker_, LV_OBJ_FLAG_HIDDEN)) {
      return;
    }
    if (on) {
      lv_obj_remove_flag(self->marker_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(self->marker_);
    } else {
      lv_obj_add_flag(self->marker_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  StickInjectMailbox mailbox_{StickInjectMailbox::Config{}};
  hmi::stick::StickInjector injector_;
  hmi::fw::AtomicValue<bool> active_{{.initial = false}};
  lv_obj_t *marker_ = nullptr;
};

/// @brief What app_main holds when CONFIG_HMI_BENCH_STICK_INJECT is off: nothing.
/// @details Its functions are declared and never defined. Every call is in a discarded
///          `if constexpr (BENCH_STICK_INJECT)` arm, so nothing needs them; a call outside such
///          an arm fails to link instead of shipping.
struct NoStickInject {
  [[nodiscard]] StickInjectWriter writer() noexcept;
  [[nodiscard]] hmi::stick::RawReadsMv apply(const hmi::stick::RawReadsMv &real) noexcept;
  void start_marker() noexcept;
};

/// @brief app_main's bench stick injection: StickInjectBench in a bench build, else empty.
using StickInjectSlot = std::conditional_t<BENCH_STICK_INJECT, StickInjectBench, NoStickInject>;
