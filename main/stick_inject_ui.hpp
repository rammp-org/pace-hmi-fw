#pragma once

/// @file stick_inject_ui.hpp
/// @brief Bench stick injection in the firmware: the channel from the remote UI to the ADC
///        task, and the ADC task's hook.
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
///
///          app_main holds a `StickInjectSlot`: this class when the option is on, else
///          NoStickInject, an empty type whose functions are never defined. Every use sits in an
///          `if constexpr (BENCH_STICK_INJECT)` arm, so a release build compiles and links none
///          of it (CS-LAY-09, CS-TYP-05).

#include <chrono>
#include <type_traits>

#include "fw_core/mailbox.hpp"
#include "sdkconfig.h"
#include "stick/bench_inject.hpp"

/// @brief CONFIG_HMI_BENCH_STICK_INJECT as a constexpr (CS-TYP-05; the hidden _AS_INT is always
///        defined, so no #ifdef is needed).
inline constexpr bool BENCH_STICK_INJECT = CONFIG_HMI_BENCH_STICK_INJECT_AS_INT != 0;

/// @brief The one-slot channel the STICK verb writes and the ADC task drains.
using StickInjectMailbox = hmi::fw::Mailbox<hmi::stick::StickInjectMsg>;
/// @brief Its write end, the remote UI's only access to the injection.
using StickInjectWriter = hmi::fw::Writer<StickInjectMailbox>;

/// @brief The bench stick injection: channel and ADC hook. See the file comment.
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
    return injector_.apply(real, now);
  }

private:
  StickInjectMailbox mailbox_{StickInjectMailbox::Config{}};
  hmi::stick::StickInjector injector_;
};

/// @brief What app_main holds when CONFIG_HMI_BENCH_STICK_INJECT is off: nothing.
/// @details Its functions are declared and never defined. Every call is in a discarded
///          `if constexpr (BENCH_STICK_INJECT)` arm, so nothing needs them; a call outside such
///          an arm fails to link instead of shipping.
struct NoStickInject {
  [[nodiscard]] StickInjectWriter writer() noexcept;
  [[nodiscard]] hmi::stick::RawReadsMv apply(const hmi::stick::RawReadsMv &real) noexcept;
};

/// @brief app_main's bench stick injection: StickInjectBench in a bench build, else empty.
using StickInjectSlot = std::conditional_t<BENCH_STICK_INJECT, StickInjectBench, NoStickInject>;
