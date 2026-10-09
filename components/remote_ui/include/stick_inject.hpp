#pragma once

/// @file stick_inject.hpp
/// @brief Bench stick injection in the firmware: the ADC task's stick, with a channel from the
///        remote UI in front of its raw reads.
/// @details BENCH ONLY: CONFIG_HMI_BENCH_STICK_INJECT (depends on CONFIG_HMI_REMOTE_UI, off by
///          default, never in sdkconfig.defaults; hazard-fixes.md §3 B1). The logic (parse,
///          expiry, fail_mask) is hmi::stick's bench_inject.hpp, host-tested (STK-040..049).
///
///          the stick that main's `StickIsland<StickSlot, ...>` owns is hmi::stick::StickPipeline
///          itself in every other build: the type is chosen by `std::conditional_t` on the
///          constexpr, so a release build compiles none of this (CS-LAY-09, CS-TYP-05) and app_main
///          is unchanged (the L0 ratchet holds main.cpp's lines and statics).
///
///          In a bench inject build it is StickInjectBench, a StickPipeline whose reads() swaps
///          the ADC task's RawReadsMv for the injected ones (StickInjector::apply); the island
///          runs the real pipeline on them: calibration, mapping, keys, gate, XYTwist.
///
///          Who touches what (CS-OWN; one writer and one reader per channel):
///
///          | Member      | Kind                        | Written by | Read by     |
///          | ----------- | --------------------------- | ---------- | ----------- |
///          | `mailbox_`  | fw::Mailbox<StickInjectMsg> | remote_ui  | ADC task    |
///          | `injector_` | ADC task state              | ADC task   | ADC task    |
///          | `active_`   | fw::AtomicValue<bool>       | ADC task   | LVGL marker |
///
///          The remote UI gets the write end and the marker's read end once, from the
///          constructor (remote_ui_attach_stick_inject), which runs on app_main before
///          remote_ui_start creates the server task. The "STICK INJECTED" marker is the remote
///          UI's (remote_ui.cpp), drawn on the LVGL task.

#include <chrono>
#include <type_traits>

#include "fw_core/atomic_value.hpp"
#include "fw_core/mailbox.hpp"
#include "sdkconfig.h"
#include "stick/bench_inject.hpp"
#include "stick/stick_pipeline.hpp"

/// @brief CONFIG_HMI_BENCH_STICK_INJECT as a constexpr (CS-TYP-05; the hidden _AS_INT is always
///        defined, so no #ifdef is needed).
inline constexpr bool BENCH_STICK_INJECT = CONFIG_HMI_BENCH_STICK_INJECT_AS_INT != 0;

/// @brief The one-slot channel the STICK verb writes and the ADC task drains.
using StickInjectMailbox = hmi::fw::Mailbox<hmi::stick::StickInjectMsg>;
/// @brief Its write end, the remote UI's only way into the injection.
using StickInjectWriter = hmi::fw::Writer<StickInjectMailbox>;
/// @brief Whether an injection holds, as the ADC task last saw it.
using StickInjectActive = hmi::fw::AtomicValue<bool>;
/// @brief Its read end, for the remote UI's marker (LVGL task).
using StickInjectActiveReader = hmi::fw::Reader<StickInjectActive>;

/// @brief Hands the remote UI the STICK verb's write end and the marker's read end. Defined in
///        remote_ui.cpp, only when CONFIG_HMI_REMOTE_UI is on. Called once, by StickInjectBench's
///        constructor on app_main, before remote_ui_start creates the server task.
/// @param writer The mailbox's one writer.
/// @param active The injection-active flag's read end.
void remote_ui_attach_stick_inject(StickInjectWriter writer, StickInjectActiveReader active);

/// @brief The ADC task's stick with the bench injection in front of its reads (reads()). See
///        the file comment.
class StickInjectBench : public hmi::stick::StickPipeline {
public:
  /// @brief The stick pipeline on @p config, and the channel ends handed to the remote UI.
  /// @param config The stick pipeline's configuration.
  explicit StickInjectBench(const Config &config)
      : StickPipeline(config) {
    remote_ui_attach_stick_inject(hmi::fw::writer(mailbox_), hmi::fw::reader(active_));
  }
  StickInjectBench(const StickInjectBench &) = delete;
  StickInjectBench &operator=(const StickInjectBench &) = delete;
  StickInjectBench(StickInjectBench &&) = delete;
  StickInjectBench &operator=(StickInjectBench &&) = delete;
  ~StickInjectBench() = default;

  /// @brief The reads this cycle uses (ADC task only, once per cycle, before the cycle): the
  ///        injected ones while an injection holds (until 300 ms after the last STICK), else
  ///        @p real. The island passes them to StickPipeline::cycle and to its Io.
  ///        Drains the mailbox (its one reader), never blocking.
  /// @param real This cycle's ADC reads.
  /// @return The reads for the pipeline.
  [[nodiscard]] hmi::stick::RawReadsMv reads(const hmi::stick::RawReadsMv &real) noexcept {
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

private:
  StickInjectMailbox mailbox_{StickInjectMailbox::Config{}};
  hmi::stick::StickInjector injector_;
  StickInjectActive active_{{.initial = false}};
};

/// @brief main's stick (the StickIsland's): StickInjectBench in a bench inject build, else
/// StickPipeline.
using StickSlot =
    std::conditional_t<BENCH_STICK_INJECT, StickInjectBench, hmi::stick::StickPipeline>;
