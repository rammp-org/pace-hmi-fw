#pragma once

/// @file topology_espp.hpp
/// @brief Topology: creates every channel's storage and hands out narrow handles and task
///        settings (CS-OWN-12, CS-OWN-06, CS-CON-02). DRAFT for review.
/// @details Built on fw_core's Mailbox, Queue and AtomicValue (FreeRTOS on the target, the host
///          shim in the host test) and espp's Task::BaseConfig (host-capable).
///          - Storage: one Slot per channel. A Slot is built only with a Passkey that only the
///            Topology can make, so no other code creates a Slot.
///            Limit: fw_core's channel constructors are public, so nothing yet stops code from
///            building a raw fw::Mailbox outside the Topology (review, and the L0 placement
///            grep, cover that until fw_core takes a passkey).
///          - Handles: `topo.writer<XCh>(ctx)` and `topo.reader<XCh>(ctx)`. ctx is the
///            caller's context token, fw::Context<Island>; Island::TASK must be the row's
///            producer (writer) or consumer (reader), or the call does not compile.
///          - Task settings: `topo.task_config(Task::X)`, from the TASKS row only (CS-CON-02).
///            An id with no TASKS row (an adapter on a foreign task) does not compile.

#include <chrono>
#include <cstddef>
#include <string>
#include <type_traits>

#include "fw_core/fw_core.hpp"
#include "task.hpp" // espp::Task::BaseConfig

#include "topology.hpp"

namespace hmi::topo {

static_assert(MAX_QUEUE_DEPTH == fw::MAX_QUEUE_DEPTH,
              "topology: MAX_QUEUE_DEPTH must equal fw_core's (rates_constants.hpp)");

/// @brief fw_core's full policy for a queue row's Full.
/// @param full The row's policy (never OVERWRITE for a queue: validate() rejects that).
/// @return The fw_core policy.
[[nodiscard]] consteval fw::FullPolicy to_fw(Full full) noexcept {
  switch (full) {
  case Full::BLOCK_TIMEOUT:
    return fw::FullPolicy::BLOCK_TIMEOUT;
  case Full::RAISE_FAULT:
    return fw::FullPolicy::RAISE_FAULT;
  case Full::DROP_NEWEST_COUNT:
  case Full::OVERWRITE:
    break;
  }
  return fw::FullPolicy::DROP_NEWEST_COUNT;
}

/// @brief The fw_core storage of a channel kind, and its configuration from the row.
template <class XCh, Kind KIND = XCh::ROW.kind> struct Storage;

/// @brief A mailbox: fw::Mailbox<M>, starting at M{}.
template <class XCh> struct Storage<XCh, Kind::MAILBOX> {
  using type = fw::Mailbox<typename XCh::message_type>; ///< The storage.
  /// @brief Its configuration.
  [[nodiscard]] static typename type::Config config() noexcept { return {}; }
};

/// @brief A queue: fw::Queue<M, depth, policy>, named after its row.
template <class XCh> struct Storage<XCh, Kind::QUEUE> {
  using type = fw::Queue<typename XCh::message_type, XCh::ROW.rate_or_depth,
                         to_fw(XCh::ROW.full)>; ///< The storage.
  /// @brief Its configuration.
  [[nodiscard]] static typename type::Config config() noexcept {
    typename type::Config c{};
    c.name = XCh::ROW.name;
    if constexpr (XCh::ROW.full == Full::BLOCK_TIMEOUT) {
      c.send_timeout = std::chrono::milliseconds{BLOCK_SEND_TIMEOUT_MS};
    }
    return c;
  }
};

/// @brief An atomic: fw::AtomicValue<M>, starting at M{}.
template <class XCh> struct Storage<XCh, Kind::ATOMIC> {
  using type = fw::AtomicValue<typename XCh::message_type>; ///< The storage.
  /// @brief Its configuration.
  [[nodiscard]] static typename type::Config config() noexcept { return {}; }
};

/// @brief One channel's storage. Only @p Topo can build one (it alone makes the key).
template <class Topo, class XCh> class Slot {
public:
  /// @brief Creates the storage from the row.
  /// @param key The Topology's key.
  explicit Slot([[maybe_unused]] fw::Passkey<Topo> key) noexcept
      : storage_(Storage<XCh>::config()) {}

  /// @brief The storage, for the Topology's handle getters.
  /// @param key The Topology's key.
  /// @return The storage.
  [[nodiscard]] typename Storage<XCh>::type &get([[maybe_unused]] fw::Passkey<Topo> key) noexcept {
    return storage_;
  }

private:
  typename Storage<XCh>::type storage_;
};

namespace broken_rule {
/// @brief task_config() was asked for a task with no TASKS row (an adapter on a foreign task
///        has no settings of its own). Not constexpr: reaching it is a compile error.
inline void task_config_without_task_row() noexcept {}
} // namespace broken_rule

/// @brief A task id that has a TASKS row, checked at compile time.
/// @details The constructor is consteval, so an id without a row, or an id that is not a
///          constant, does not compile. It is explicit: callers write
///          `task_config(TaskId{Task::X})` (README, REQ-TOP-07).
class TaskId {
public:
  /// @brief Looks up @p id's TASKS row; a compile error if it has none.
  /// @param id The task.
  consteval explicit TaskId(Task id) noexcept
      : index_(task_index(id)) {
    if (index_ >= TASKS.size()) {
      broken_rule::task_config_without_task_row();
    }
  }

  /// @brief The row.
  /// @return The TASKS row.
  [[nodiscard]] constexpr const TaskRow &row() const noexcept { return TASKS[index_]; }

private:
  std::size_t index_; // An index, not a pointer: GCC 13 with -fsanitize cannot compare a
                      // pointer to a static object with nullptr in a constant expression.
};

/// @brief Owns every channel in @p List and hands out its ends.
template <class List> class BasicTopology;

/// @brief Owns every channel in Chs... and hands out its ends.
/// @tparam Chs The channel types (Channel<Ch::X, M>).
template <class... Chs>
class BasicTopology<ChannelList<Chs...>>
    : private Slot<BasicTopology<ChannelList<Chs...>>, Chs>... {
  using Self = BasicTopology<ChannelList<Chs...>>;
  template <class XCh> using SlotOf = Slot<Self, XCh>;

  template <class XCh> static constexpr bool HAS = (std::is_same_v<XCh, Chs> || ...);

  template <class XCh> [[nodiscard]] typename Storage<XCh>::type &storage() noexcept {
    return static_cast<SlotOf<XCh> &>(*this).get(fw::Passkey<Self>{});
  }

public:
  /// @brief Creates every channel's storage. Built once, at start-up, before any task.
  BasicTopology() noexcept
      : SlotOf<Chs>(fw::Passkey<Self>{})... {}

  BasicTopology(const BasicTopology &) = delete;
  BasicTopology &operator=(const BasicTopology &) = delete;
  BasicTopology(BasicTopology &&) = delete;
  BasicTopology &operator=(BasicTopology &&) = delete;
  ~BasicTopology() = default;

  /// @brief The write end of @p XCh, for its producer only (CS-OWN-06).
  /// @tparam XCh The channel.
  /// @tparam Island The caller's island or adapter class; Island::TASK must be the producer.
  /// @param ctx The caller's context token (CS-OWN-07).
  /// @return fw::Writer (mailbox, atomic) or fw::Sender (queue).
  template <class XCh, class Island>
  [[nodiscard]] auto writer([[maybe_unused]] const fw::Context<Island> &ctx) noexcept {
    static_assert(HAS<XCh>, "topology: this Topology holds no storage for the channel");
    static_assert(Island::TASK == XCh::ROW.producer,
                  "topology: only the channel's producer task gets its write end (CS-OWN-06)");
    if constexpr (XCh::ROW.kind == Kind::QUEUE) {
      return fw::sender(storage<XCh>());
    } else {
      return fw::writer(storage<XCh>());
    }
  }

  /// @brief The read end of @p XCh, for its consumer only (CS-OWN-06).
  /// @tparam XCh The channel.
  /// @tparam Island The caller's island or adapter class; Island::TASK must be the consumer.
  /// @param ctx The caller's context token (CS-OWN-07).
  /// @return fw::Reader (mailbox, atomic) or fw::Receiver (queue).
  template <class XCh, class Island>
  [[nodiscard]] auto reader([[maybe_unused]] const fw::Context<Island> &ctx) noexcept {
    static_assert(HAS<XCh>, "topology: this Topology holds no storage for the channel");
    static_assert(Island::TASK == XCh::ROW.consumer,
                  "topology: only the channel's consumer task gets its read end (CS-OWN-06)");
    if constexpr (XCh::ROW.kind == Kind::QUEUE) {
      return fw::receiver(storage<XCh>());
    } else {
      return fw::reader(storage<XCh>());
    }
  }

  /// @brief A task's settings, from its TASKS row only (CS-CON-02).
  /// @param id The task, as `TaskId{Task::X}`; it must have a TASKS row (checked at compile
  ///           time).
  /// @return The espp task settings.
  [[nodiscard]] static espp::Task::BaseConfig task_config(TaskId id) {
    const TaskRow &row = id.row();
    espp::Task::BaseConfig config{};
    config.name = std::string(row.name);
    config.stack_size_bytes = row.stack;
    config.priority = row.prio;
    config.core_id = row.core;
    return config;
  }
};

/// @brief The firmware's topology: storage for every CHANNELS row.
using Topology = BasicTopology<AllChannels>;

} // namespace hmi::topo
