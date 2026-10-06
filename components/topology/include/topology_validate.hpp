#pragma once

/// @file topology_validate.hpp
/// @brief validate(): the compile-time check of the topology tables (CS-OWN-12). DRAFT.
/// @details `static_assert(validate(TASKS, FOREIGN_TASKS, COMPONENTS, CHANNELS))` in
///          topology.hpp. check() finds the first broken rule and its row; validate() turns
///          it into a compile error that names the rule. It does that by calling a function in
///          namespace `broken_rule` that is deliberately not constexpr: inside a constant
///          expression that call is an error, and the compiler prints the function's name,
///          e.g. "call to non-'constexpr' function 'void hmi::topo::broken_rule::
///          channel_drop_into_safety()'". No exceptions are needed (CS-LNG-03).
///          At run time (the host test) both functions are ordinary: check() returns the
///          verdict, validate() returns false.
///          Limit (CS-OWN-12): this proves what is declared, not what runs. The task census
///          (TS-DET-09) and the ThreadCheckers (CS-OWN-11) cover what runs.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "rates_constants.hpp"
#include "topology_types.hpp"

namespace hmi::topo {

/// @brief The longest task name FreeRTOS keeps: configMAX_TASK_NAME_LEN (16 in ESP-IDF's
///        default sdkconfig) minus the NUL. A longer name is truncated, and the census
///        (TS-DET-09) would not find it. doc: ESP-IDF Kconfig FREERTOS_MAX_TASK_NAME_LEN.
inline constexpr std::size_t MAX_TASK_NAME = 15;

/// @brief The rules validate() checks. Each has a function in namespace broken_rule.
enum class Rule : std::uint8_t {
  OK,                       ///< Every rule holds.
  TASK_DUPLICATE_ID,        ///< A Task id has two rows (TASKS, or a foreign row's hosts).
  TASK_DUPLICATE_NAME,      ///< Two TASKS / FOREIGN_TASKS rows share a name.
  TASK_NAME_LENGTH,         ///< A task name is empty or longer than MAX_TASK_NAME.
  TASK_WITHOUT_ROLE,        ///< A TASKS row is neither an island nor an adapter.
  CHANNEL_DUPLICATE_ID,     ///< Two CHANNELS rows share an id or a name.
  CHANNEL_UNKNOWN_TASK,     ///< A producer or consumer is in neither TASKS nor a hosts.
  CHANNEL_SAME_TASK,        ///< A channel's producer is its consumer.
  CHANNEL_DROP_INTO_SAFETY, ///< A DROP_NEWEST_COUNT queue feeds a safety task (CS-OWN-04).
  CHANNEL_BLOCK_ON_SAFETY,  ///< A BLOCK_TIMEOUT queue touches a safety task (CS-OWN-04).
  MAILBOX_NOT_OVERWRITE,    ///< A mailbox or atomic declares a policy other than OVERWRITE.
  QUEUE_OVERWRITE,          ///< A queue declares OVERWRITE: a queue never merges.
  QUEUE_DEPTH,              ///< A queue's depth is 0 or above MAX_QUEUE_DEPTH.
  COMPONENT_UNKNOWN_TASK,   ///< A component runs on a task that does not exist.
  COMPONENT_DUPLICATE_NAME, ///< Two COMPONENTS rows share a name.
};

/// @brief The rule's name, for the host test's messages.
/// @param rule The rule.
/// @return Its UPPER_CASE name.
[[nodiscard]] constexpr std::string_view to_string(Rule rule) noexcept {
  switch (rule) {
  case Rule::OK:
    return "OK";
  case Rule::TASK_DUPLICATE_ID:
    return "TASK_DUPLICATE_ID";
  case Rule::TASK_DUPLICATE_NAME:
    return "TASK_DUPLICATE_NAME";
  case Rule::TASK_NAME_LENGTH:
    return "TASK_NAME_LENGTH";
  case Rule::TASK_WITHOUT_ROLE:
    return "TASK_WITHOUT_ROLE";
  case Rule::CHANNEL_DUPLICATE_ID:
    return "CHANNEL_DUPLICATE_ID";
  case Rule::CHANNEL_UNKNOWN_TASK:
    return "CHANNEL_UNKNOWN_TASK";
  case Rule::CHANNEL_SAME_TASK:
    return "CHANNEL_SAME_TASK";
  case Rule::CHANNEL_DROP_INTO_SAFETY:
    return "CHANNEL_DROP_INTO_SAFETY";
  case Rule::CHANNEL_BLOCK_ON_SAFETY:
    return "CHANNEL_BLOCK_ON_SAFETY";
  case Rule::MAILBOX_NOT_OVERWRITE:
    return "MAILBOX_NOT_OVERWRITE";
  case Rule::QUEUE_OVERWRITE:
    return "QUEUE_OVERWRITE";
  case Rule::QUEUE_DEPTH:
    return "QUEUE_DEPTH";
  case Rule::COMPONENT_UNKNOWN_TASK:
    return "COMPONENT_UNKNOWN_TASK";
  case Rule::COMPONENT_DUPLICATE_NAME:
    return "COMPONENT_DUPLICATE_NAME";
  }
  return "UNKNOWN";
}

/// @brief What check() found: the first broken rule and the row it found it in.
struct Verdict {
  Rule rule;       ///< Rule::OK when every rule holds.
  std::size_t row; ///< The offending row's index in its table (0 when OK).
};

/// @brief The tables validate() reads, as views.
struct Tables {
  std::span<const TaskRow> tasks;           ///< TASKS.
  std::span<const ForeignTaskRow> foreign;  ///< FOREIGN_TASKS.
  std::span<const ComponentRow> components; ///< COMPONENTS.
  std::span<const ChannelRow> channels;     ///< CHANNELS.
};

namespace detail {

/// @brief How many rows, in TASKS and in foreign hosts, declare @p id.
[[nodiscard]] constexpr std::size_t task_rows(const Tables &t, Task id) noexcept {
  std::size_t n = 0;
  for (const TaskRow &row : t.tasks) {
    n += row.id == id ? 1U : 0U;
  }
  for (const ForeignTaskRow &row : t.foreign) {
    n += (row.hosts.has_value() && *row.hosts == id) ? 1U : 0U;
  }
  return n;
}

/// @brief Whether @p id is a safety task (TASKS) or a safety adapter on a foreign task.
[[nodiscard]] constexpr bool is_safety(const Tables &t, Task id) noexcept {
  for (const TaskRow &row : t.tasks) {
    if (row.id == id) {
      return row.safety;
    }
  }
  for (const ForeignTaskRow &row : t.foreign) {
    if (row.hosts.has_value() && *row.hosts == id) {
      return row.safety;
    }
  }
  return false;
}

/// @brief How many TASKS and FOREIGN_TASKS rows are named @p name.
[[nodiscard]] constexpr std::size_t name_rows(const Tables &t, std::string_view name) noexcept {
  std::size_t n = 0;
  for (const TaskRow &row : t.tasks) {
    n += row.name == name ? 1U : 0U;
  }
  for (const ForeignTaskRow &row : t.foreign) {
    n += row.name == name ? 1U : 0U;
  }
  return n;
}

[[nodiscard]] constexpr bool name_length_ok(std::string_view name) noexcept {
  return !name.empty() && name.size() <= MAX_TASK_NAME;
}

/// @brief The task rules, row by row.
[[nodiscard]] constexpr Verdict check_tasks(const Tables &t) noexcept {
  for (std::size_t i = 0; i < t.tasks.size(); ++i) {
    const TaskRow &row = t.tasks[i];
    if (task_rows(t, row.id) != 1U) {
      return {Rule::TASK_DUPLICATE_ID, i};
    }
    if (name_rows(t, row.name) != 1U) {
      return {Rule::TASK_DUPLICATE_NAME, i};
    }
    if (!name_length_ok(row.name)) {
      return {Rule::TASK_NAME_LENGTH, i};
    }
    if (row.role != Role::ISLAND && row.role != Role::ADAPTER) {
      return {Rule::TASK_WITHOUT_ROLE, i};
    }
  }
  for (std::size_t i = 0; i < t.foreign.size(); ++i) {
    const ForeignTaskRow &row = t.foreign[i];
    if (row.hosts.has_value() && task_rows(t, *row.hosts) != 1U) {
      return {Rule::TASK_DUPLICATE_ID, i};
    }
    if (name_rows(t, row.name) != 1U) {
      return {Rule::TASK_DUPLICATE_NAME, i};
    }
    if (!name_length_ok(row.name)) {
      return {Rule::TASK_NAME_LENGTH, i};
    }
  }
  return {Rule::OK, 0};
}

/// @brief How many CHANNELS rows have @p row's id, and how many its name.
[[nodiscard]] constexpr bool channel_unique(const Tables &t, const ChannelRow &row) noexcept {
  std::size_t ids = 0;
  std::size_t names = 0;
  for (const ChannelRow &other : t.channels) {
    ids += other.id == row.id ? 1U : 0U;
    names += other.name == row.name ? 1U : 0U;
  }
  return ids == 1U && names == 1U;
}

/// @brief The rules on one channel's kind, policy and depth.
[[nodiscard]] constexpr Rule check_kind(const Tables &t, const ChannelRow &row) noexcept {
  const bool state = row.kind == Kind::MAILBOX || row.kind == Kind::ATOMIC;
  if (state && row.full != Full::OVERWRITE) {
    return Rule::MAILBOX_NOT_OVERWRITE;
  }
  if (row.kind != Kind::QUEUE) {
    return Rule::OK;
  }
  if (row.full == Full::OVERWRITE) {
    return Rule::QUEUE_OVERWRITE;
  }
  if (row.rate_or_depth == 0U || row.rate_or_depth > MAX_QUEUE_DEPTH) {
    return Rule::QUEUE_DEPTH;
  }
  if (row.full == Full::DROP_NEWEST_COUNT && is_safety(t, row.consumer)) {
    return Rule::CHANNEL_DROP_INTO_SAFETY;
  }
  if (row.full == Full::BLOCK_TIMEOUT &&
      (is_safety(t, row.producer) || is_safety(t, row.consumer))) {
    return Rule::CHANNEL_BLOCK_ON_SAFETY;
  }
  return Rule::OK;
}

/// @brief The channel rules, row by row.
[[nodiscard]] constexpr Verdict check_channels(const Tables &t) noexcept {
  for (std::size_t i = 0; i < t.channels.size(); ++i) {
    const ChannelRow &row = t.channels[i];
    if (!channel_unique(t, row)) {
      return {Rule::CHANNEL_DUPLICATE_ID, i};
    }
    if (task_rows(t, row.producer) == 0U || task_rows(t, row.consumer) == 0U) {
      return {Rule::CHANNEL_UNKNOWN_TASK, i};
    }
    if (row.producer == row.consumer) {
      return {Rule::CHANNEL_SAME_TASK, i};
    }
    const Rule kind = check_kind(t, row);
    if (kind != Rule::OK) {
      return {kind, i};
    }
  }
  return {Rule::OK, 0};
}

/// @brief The component rules, row by row.
[[nodiscard]] constexpr Verdict check_components(const Tables &t) noexcept {
  for (std::size_t i = 0; i < t.components.size(); ++i) {
    const ComponentRow &row = t.components[i];
    if (task_rows(t, row.task) == 0U) {
      return {Rule::COMPONENT_UNKNOWN_TASK, i};
    }
    std::size_t names = 0;
    for (const ComponentRow &other : t.components) {
      names += other.name == row.name ? 1U : 0U;
    }
    if (names != 1U) {
      return {Rule::COMPONENT_DUPLICATE_NAME, i};
    }
  }
  return {Rule::OK, 0};
}

} // namespace detail

/// @brief Finds the first broken rule: tasks first, then channels, then components.
/// @param t The tables.
/// @return The rule and the row; Rule::OK when every rule holds.
[[nodiscard]] constexpr Verdict check(const Tables &t) noexcept {
  Verdict v = detail::check_tasks(t);
  if (v.rule == Rule::OK) {
    v = detail::check_channels(t);
  }
  if (v.rule == Rule::OK) {
    v = detail::check_components(t);
  }
  return v;
}

/// @brief One function per rule. Not constexpr on purpose: reaching one while validate() runs
///        in a static_assert is a compile error that prints the rule's name. At run time
///        they do nothing.
namespace broken_rule {
// clang-format off
/// @brief A Task id has two rows: in TASKS, or in TASKS and a FOREIGN_TASKS `hosts`.
inline void task_duplicate_id() noexcept {}
/// @brief Two TASKS / FOREIGN_TASKS rows share a name; the census matches by name (TS-DET-09).
inline void task_duplicate_name() noexcept {}
/// @brief A task name is empty or longer than MAX_TASK_NAME (FreeRTOS truncates it).
inline void task_name_length() noexcept {}
/// @brief A TASKS row has no role: every task is an island or an adapter (CS-OWN-01/02).
inline void task_without_role() noexcept {}
/// @brief Two CHANNELS rows share an id or a name.
inline void channel_duplicate_id() noexcept {}
/// @brief A channel names a producer or consumer that is in neither TASKS nor a `hosts`.
inline void channel_unknown_task() noexcept {}
/// @brief A channel's producer is its consumer: data inside one task needs no channel.
inline void channel_same_task() noexcept {}
/// @brief A DROP_NEWEST_COUNT queue feeds a safety task (CS-OWN-04): use RAISE_FAULT.
inline void channel_drop_into_safety() noexcept {}
/// @brief A BLOCK_TIMEOUT queue has a safety task at either end (CS-OWN-04).
inline void channel_block_on_safety_path() noexcept {}
/// @brief A mailbox or atomic declares a full policy other than OVERWRITE (CS-OWN-03).
inline void mailbox_not_overwrite() noexcept {}
/// @brief A queue declares OVERWRITE: a queue never merges events (CS-OWN-03).
inline void queue_overwrite() noexcept {}
/// @brief A queue's depth is 0 or above MAX_QUEUE_DEPTH (CS-OWN-04, CS-FLW-02).
inline void queue_depth_out_of_range() noexcept {}
/// @brief A component runs on a task that is in neither TASKS nor a `hosts`.
inline void component_unknown_task() noexcept {}
/// @brief Two COMPONENTS rows share a name.
inline void component_duplicate_name() noexcept {}
// clang-format on
} // namespace broken_rule

namespace detail {
/// @brief Calls the broken rule's function (a compile error inside a constant expression).
constexpr void report(Rule rule) noexcept {
  switch (rule) {
  case Rule::OK:
    return;
  case Rule::TASK_DUPLICATE_ID:
    return broken_rule::task_duplicate_id();
  case Rule::TASK_DUPLICATE_NAME:
    return broken_rule::task_duplicate_name();
  case Rule::TASK_NAME_LENGTH:
    return broken_rule::task_name_length();
  case Rule::TASK_WITHOUT_ROLE:
    return broken_rule::task_without_role();
  case Rule::CHANNEL_DUPLICATE_ID:
    return broken_rule::channel_duplicate_id();
  case Rule::CHANNEL_UNKNOWN_TASK:
    return broken_rule::channel_unknown_task();
  case Rule::CHANNEL_SAME_TASK:
    return broken_rule::channel_same_task();
  case Rule::CHANNEL_DROP_INTO_SAFETY:
    return broken_rule::channel_drop_into_safety();
  case Rule::CHANNEL_BLOCK_ON_SAFETY:
    return broken_rule::channel_block_on_safety_path();
  case Rule::MAILBOX_NOT_OVERWRITE:
    return broken_rule::mailbox_not_overwrite();
  case Rule::QUEUE_OVERWRITE:
    return broken_rule::queue_overwrite();
  case Rule::QUEUE_DEPTH:
    return broken_rule::queue_depth_out_of_range();
  case Rule::COMPONENT_UNKNOWN_TASK:
    return broken_rule::component_unknown_task();
  case Rule::COMPONENT_DUPLICATE_NAME:
    return broken_rule::component_duplicate_name();
  }
}
} // namespace detail

/// @brief Checks the tables; use it as `static_assert(validate(...))`.
/// @details In a static_assert, a broken rule is a compile error naming
///          `broken_rule::<rule>`. At run time it returns false instead.
/// @param tasks TASKS.
/// @param foreign FOREIGN_TASKS.
/// @param components COMPONENTS.
/// @param channels CHANNELS.
/// @return true when every rule holds.
[[nodiscard]] constexpr bool validate(std::span<const TaskRow> tasks,
                                      std::span<const ForeignTaskRow> foreign,
                                      std::span<const ComponentRow> components,
                                      std::span<const ChannelRow> channels) noexcept {
  const Verdict v = check(Tables{tasks, foreign, components, channels});
  detail::report(v.rule);
  return v.rule == Rule::OK;
}

} // namespace hmi::topo
