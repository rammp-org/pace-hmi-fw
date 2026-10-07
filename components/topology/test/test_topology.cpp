// Host L1 tests for components/topology (AGENT_BRIEF host L1 contract, TS-UNIT-02).
// spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux target (owner,
// 2026-10-06).
//
// The tables are checked at compile time by topology.hpp's static_asserts; these cases check
// the same invariants at run time (so a regression in validate() itself shows), drive check()
// through every rule on mutated copies of the real tables, and exercise the Topology's storage,
// handles and task settings on fw_core's host port.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "fw_core/message.hpp"
#include "test_case.hpp"
#include "topology_espp.hpp"

namespace {

using namespace hmi::topo;
namespace fw = hmi::fw;

// --- compile-time: every channel's type is allowed in its channel (CS-OWN-05) ----------------

template <class XCh> constexpr bool message_ok() {
  using M = typename XCh::message_type;
  if constexpr (XCh::ROW.kind == Kind::ATOMIC) {
    return std::is_trivially_copyable_v<M> && std::atomic<M>::is_always_lock_free;
  } else {
    return fw::Message<M>;
  }
}
template <class... Chs> constexpr bool all_messages_ok(ChannelList<Chs...> /*list*/) {
  return (message_ok<Chs>() && ...);
}
static_assert(all_messages_ok(AllChannels{}),
              "every channel's message satisfies fw::Message (atomics: lock-free)");

// --- helpers -----------------------------------------------------------------------------

/// Mutable copies of the real tables, for check() on a broken variant.
struct Copy {
  std::array<TaskRow, TASKS.size()> tasks = TASKS;
  std::array<ForeignTaskRow, FOREIGN_TASKS.size()> foreign = FOREIGN_TASKS;
  std::array<ComponentRow, COMPONENTS.size()> components = COMPONENTS;
  std::array<ChannelRow, CHANNELS.size()> channels = CHANNELS;

  [[nodiscard]] Tables tables() const { return Tables{tasks, foreign, components, channels}; }
};

[[nodiscard]] bool declared(Task id) {
  if (find_task(id) != nullptr) {
    return true;
  }
  return std::any_of(FOREIGN_TASKS.begin(), FOREIGN_TASKS.end(), [id](const ForeignTaskRow &row) {
    return row.hosts.has_value() && *row.hosts == id;
  });
}

[[nodiscard]] bool safety(Task id) {
  const TaskRow *row = find_task(id);
  if (row != nullptr) {
    return row->safety;
  }
  const auto *foreign =
      std::find_if(FOREIGN_TASKS.begin(), FOREIGN_TASKS.end(),
                   [id](const ForeignTaskRow &r) { return r.hosts.has_value() && *r.hosts == id; });
  return foreign != FOREIGN_TASKS.end() && foreign->safety;
}

/// An island or adapter for the handle tests: the token comes from its own member function.
template <Task ID> class Island {
public:
  static constexpr Task TASK = ID;
  template <class Fn> void run(Fn &&fn) {
    const fw::Context<Island> ctx{fw::Passkey<Island>{}};
    fn(ctx);
  }
};

} // namespace

TEST_CASE("TOP-001 the firmware tables pass every validate() rule at run time", "[topology]") {
  const Verdict v = check(Tables{TASKS, FOREIGN_TASKS, COMPONENTS, CHANNELS});
  TEST_ASSERT_EQUAL_STRING("OK", std::string(to_string(v.rule)).c_str());
  TEST_ASSERT_TRUE(validate(TASKS, FOREIGN_TASKS, COMPONENTS, CHANNELS));
}

TEST_CASE("TOP-002 every Task id has exactly one row and every TASKS row has a role",
          "[topology]") {
  for (std::size_t i = 0; i < static_cast<std::size_t>(Task::COUNT_); ++i) {
    const auto id = static_cast<Task>(i);
    const auto foreign_rows =
        std::count_if(FOREIGN_TASKS.begin(), FOREIGN_TASKS.end(), [id](const ForeignTaskRow &row) {
          return row.hosts.has_value() && *row.hosts == id;
        });
    const std::size_t rows =
        (find_task(id) != nullptr ? 1U : 0U) + static_cast<std::size_t>(foreign_rows);
    TEST_ASSERT_EQUAL_UINT(1U, rows);
  }
  for (const TaskRow &row : TASKS) {
    TEST_ASSERT_TRUE(row.role == Role::ISLAND || row.role == Role::ADAPTER);
    TEST_ASSERT_TRUE(row.stack > 0U);
    TEST_ASSERT_TRUE(row.core >= -1 && row.core <= 1);
  }
}

TEST_CASE("TOP-003 task names are unique and fit FreeRTOS's 15 characters", "[topology]") {
  for (const TaskRow &row : TASKS) {
    TEST_ASSERT_TRUE(!row.name.empty() && row.name.size() <= MAX_TASK_NAME);
    const auto same_name = [&row](const auto &other) { return other.name == row.name; };
    const std::size_t same =
        static_cast<std::size_t>(std::count_if(TASKS.begin(), TASKS.end(), same_name)) +
        static_cast<std::size_t>(
            std::count_if(FOREIGN_TASKS.begin(), FOREIGN_TASKS.end(), same_name));
    TEST_ASSERT_EQUAL_UINT(1U, same);
  }
  for (const ForeignTaskRow &row : FOREIGN_TASKS) {
    TEST_ASSERT_TRUE(!row.name.empty() && row.name.size() <= MAX_TASK_NAME);
  }
}

TEST_CASE("TOP-004 every channel joins two different declared tasks and has one row",
          "[topology]") {
  for (std::size_t i = 0; i < CHANNELS.size(); ++i) {
    const ChannelRow &row = CHANNELS[i];
    TEST_ASSERT_TRUE(declared(row.producer));
    TEST_ASSERT_TRUE(declared(row.consumer));
    TEST_ASSERT_TRUE(row.producer != row.consumer);
    TEST_ASSERT_EQUAL_UINT(i, channel_index(row.id));
    TEST_ASSERT_EQUAL_UINT(i, static_cast<std::size_t>(row.id));
  }
}

TEST_CASE("TOP-005 no queue into a safety task drops, and no queue touching one blocks",
          "[topology]") {
  for (const ChannelRow &row : CHANNELS) {
    if (row.kind != Kind::QUEUE) {
      continue;
    }
    if (safety(row.consumer)) {
      TEST_ASSERT_TRUE(row.full != Full::DROP_NEWEST_COUNT);
    }
    if (safety(row.consumer) || safety(row.producer)) {
      TEST_ASSERT_TRUE(row.full != Full::BLOCK_TIMEOUT);
    }
  }
}

TEST_CASE("TOP-006 mailboxes and atomics overwrite; queues have depth 1..64 and never overwrite",
          "[topology]") {
  for (const ChannelRow &row : CHANNELS) {
    if (row.kind == Kind::MAILBOX || row.kind == Kind::ATOMIC) {
      TEST_ASSERT_TRUE(row.full == Full::OVERWRITE);
    } else if (row.kind == Kind::QUEUE) {
      TEST_ASSERT_TRUE(row.full != Full::OVERWRITE);
      TEST_ASSERT_TRUE(row.rate_or_depth >= 1U && row.rate_or_depth <= MAX_QUEUE_DEPTH);
    }
  }
}

TEST_CASE("TOP-007 every component runs on a declared task and has one row", "[topology]") {
  for (const ComponentRow &row : COMPONENTS) {
    TEST_ASSERT_TRUE(declared(row.task));
    const auto same =
        std::count_if(COMPONENTS.begin(), COMPONENTS.end(),
                      [&row](const ComponentRow &other) { return other.name == row.name; });
    TEST_ASSERT_EQUAL_UINT(1U, static_cast<std::size_t>(same));
  }
}

namespace {
struct Mutation {
  const char *what;
  void (*apply)(Copy &c);
  Rule rule;
  std::size_t row;
};

const std::size_t UI = task_index(Task::UI);
const std::size_t HMI_COMMAND = channel_index(Ch::HMI_COMMAND);
const std::size_t DRIVE_INTENT = channel_index(Ch::DRIVE_INTENT);
const std::size_t REMOTE_REQ = channel_index(Ch::REMOTE_UI_REQ);

// One broken variant of the real tables per rule, with the row check() must name.
const std::array MUTATIONS{
    Mutation{"two TASKS rows share an id", [](Copy &c) { c.tasks[1].id = c.tasks[0].id; },
             Rule::TASK_DUPLICATE_ID, 0},
    Mutation{"a foreign row hosts a TASKS id", [](Copy &c) { c.foreign[2].hosts = Task::UI; },
             Rule::TASK_DUPLICATE_ID, UI},
    Mutation{"two TASKS rows share a name", [](Copy &c) { c.tasks[2].name = c.tasks[0].name; },
             Rule::TASK_DUPLICATE_NAME, 0},
    Mutation{"two FOREIGN_TASKS rows share a name",
             [](Copy &c) { c.foreign[3].name = c.foreign[2].name; }, Rule::TASK_DUPLICATE_NAME, 2},
    Mutation{"a task name is longer than 15",
             [](Copy &c) { c.tasks[3].name = "Data Display Task"; }, Rule::TASK_NAME_LENGTH, 3},
    Mutation{"a foreign task name is empty", [](Copy &c) { c.foreign[2].name = ""; },
             Rule::TASK_NAME_LENGTH, 2},
    Mutation{"a task has no role", [](Copy &c) { c.tasks[4].role = Role::NONE; },
             Rule::TASK_WITHOUT_ROLE, 4},
    Mutation{"two channels share an id", [](Copy &c) { c.channels[5].id = c.channels[4].id; },
             Rule::CHANNEL_DUPLICATE_ID, 4},
    Mutation{"two channels share a name", [](Copy &c) { c.channels[6].name = c.channels[2].name; },
             Rule::CHANNEL_DUPLICATE_ID, 2},
    Mutation{"a producer is not declared", [](Copy &c) { c.channels[0].producer = Task::COUNT_; },
             Rule::CHANNEL_UNKNOWN_TASK, 0},
    Mutation{"a consumer is not declared", [](Copy &c) { c.channels[1].consumer = Task::COUNT_; },
             Rule::CHANNEL_UNKNOWN_TASK, 1},
    Mutation{"a channel's producer is its consumer",
             [](Copy &c) { c.channels[DRIVE_INTENT].consumer = Task::UI; }, Rule::CHANNEL_SAME_TASK,
             DRIVE_INTENT},
    Mutation{"a dropping queue feeds a safety task",
             [](Copy &c) { c.channels[DRIVE_INTENT].full = Full::DROP_NEWEST_COUNT; },
             Rule::CHANNEL_DROP_INTO_SAFETY, DRIVE_INTENT},
    Mutation{"a blocking queue starts at a safety task",
             [](Copy &c) { c.channels[REMOTE_REQ].producer = Task::CONTROL; },
             Rule::CHANNEL_BLOCK_ON_SAFETY, REMOTE_REQ},
    Mutation{"a blocking queue ends at a safety task",
             [](Copy &c) { c.channels[REMOTE_REQ].consumer = Task::CONTROL; },
             Rule::CHANNEL_BLOCK_ON_SAFETY, REMOTE_REQ},
    Mutation{"a mailbox raises a fault", [](Copy &c) { c.channels[0].full = Full::RAISE_FAULT; },
             Rule::MAILBOX_NOT_OVERWRITE, 0},
    Mutation{"a queue overwrites", [](Copy &c) { c.channels[HMI_COMMAND].full = Full::OVERWRITE; },
             Rule::QUEUE_OVERWRITE, HMI_COMMAND},
    Mutation{"a queue has depth 0", [](Copy &c) { c.channels[HMI_COMMAND].rate_or_depth = 0; },
             Rule::QUEUE_DEPTH, HMI_COMMAND},
    Mutation{"a queue is deeper than 64",
             [](Copy &c) { c.channels[HMI_COMMAND].rate_or_depth = MAX_QUEUE_DEPTH + 1U; },
             Rule::QUEUE_DEPTH, HMI_COMMAND},
    Mutation{"a component's task is not declared",
             [](Copy &c) { c.components[0].task = Task::COUNT_; }, Rule::COMPONENT_UNKNOWN_TASK, 0},
    Mutation{"two components share a name",
             [](Copy &c) { c.components[1].name = c.components[0].name; },
             Rule::COMPONENT_DUPLICATE_NAME, 0},
};
} // namespace

TEST_CASE("TOP-008 check() names each broken rule and its row on a mutated copy", "[topology]") {
  for (const Mutation &m : MUTATIONS) {
    Copy c;
    m.apply(c);
    const Verdict v = check(c.tables());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(std::string(to_string(m.rule)).c_str(),
                                     std::string(to_string(v.rule)).c_str(), m.what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(m.row, v.row, m.what);
    TEST_ASSERT_FALSE_MESSAGE(validate(c.tasks, c.foreign, c.components, c.channels), m.what);
  }
}

TEST_CASE("TOP-009 every rule has a name and a mutation that breaks it", "[topology]") {
  for (std::size_t i = 1; i <= static_cast<std::size_t>(Rule::COMPONENT_DUPLICATE_NAME); ++i) {
    const auto rule = static_cast<Rule>(i);
    TEST_ASSERT_TRUE(to_string(rule) != "UNKNOWN");
    bool covered = false;
    for (const Mutation &m : MUTATIONS) {
      covered = covered || m.rule == rule;
    }
    TEST_ASSERT_TRUE_MESSAGE(covered, std::string(to_string(rule)).c_str());
  }
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", std::string(to_string(static_cast<Rule>(200))).c_str());
}

TEST_CASE("TOP-010 a mailbox from the Topology carries the producer's value to the consumer",
          "[topology]") {
  Topology topo;
  Island<Task::CONTROL> control;
  Island<Task::UI> ui;
  control.run([&](const auto &ctx) {
    topo.writer<DriveViewCh>(ctx).write(DriveViewMsg{2, 1, 3, 7});
  });
  ui.run([&](const auto &ctx) {
    const auto reader = topo.reader<DriveViewCh>(ctx);
    DriveViewMsg got{};
    TEST_ASSERT_TRUE(reader.read(got) == fw::ReadStatus::CHANGED);
    TEST_ASSERT_EQUAL_UINT8(2, got.state);
    TEST_ASSERT_EQUAL_UINT32(7, got.transitions);
    TEST_ASSERT_TRUE(reader.read(got) == fw::ReadStatus::UNCHANGED);
  });
}

TEST_CASE("TOP-011 a queue from the Topology keeps its row's depth and full policy", "[topology]") {
  using IntentQueue = Storage<DriveIntentCh>::type;
  static_assert(IntentQueue::CAPACITY == DRIVE_INTENT_DEPTH);
  static_assert(IntentQueue::FULL_POLICY == fw::FullPolicy::RAISE_FAULT);
  static_assert(Storage<HmiCommandCh>::type::FULL_POLICY == fw::FullPolicy::DROP_NEWEST_COUNT);
  static_assert(Storage<RemoteUiReqCh>::type::FULL_POLICY == fw::FullPolicy::BLOCK_TIMEOUT);
  TEST_ASSERT_TRUE(Storage<RemoteUiReqCh>::config().send_timeout ==
                   std::chrono::milliseconds{BLOCK_SEND_TIMEOUT_MS});
  TEST_ASSERT_TRUE(Storage<DriveIntentCh>::config().send_timeout == std::chrono::milliseconds{0});
  TEST_ASSERT_EQUAL_STRING("DRIVE_INTENT",
                           std::string(Storage<DriveIntentCh>::config().name).c_str());

  Topology topo;
  Island<Task::UI> ui;
  Island<Task::CONTROL> control;
  ui.run([&](const auto &ctx) {
    const auto sender = topo.writer<DriveIntentCh>(ctx);
    std::error_code ec;
    for (std::uint32_t i = 0; i < DRIVE_INTENT_DEPTH; ++i) {
      TEST_ASSERT_TRUE(sender.send(DriveIntentMsg{static_cast<std::uint8_t>(i), 0}, ec));
    }
    TEST_ASSERT_FALSE(sender.send(DriveIntentMsg{99, 0}, ec));
    TEST_ASSERT_TRUE(static_cast<bool>(ec));
  });
  control.run([&](const auto &ctx) {
    const auto receiver = topo.reader<DriveIntentCh>(ctx);
    TEST_ASSERT_TRUE(receiver.faulted());
    std::uint32_t next = 0;
    const std::size_t n = receiver.drain([&](const DriveIntentMsg &m) {
      TEST_ASSERT_EQUAL_UINT8(next, m.intent);
      ++next;
    });
    TEST_ASSERT_EQUAL_UINT(DRIVE_INTENT_DEPTH, n);
  });
}

TEST_CASE("TOP-012 an atomic from the Topology carries the stick button and the POST verdict",
          "[topology]") {
  Topology topo;
  Island<Task::STICK_BUTTON> button;
  Island<Task::SELFTEST> selftest;
  Island<Task::CONTROL> control;
  button.run([&](const auto &ctx) { topo.writer<StickButtonCh>(ctx).write(true); });
  selftest.run([&](const auto &ctx) { topo.writer<PostResultCh>(ctx).write(PostVerdict::PASS); });
  control.run([&](const auto &ctx) {
    TEST_ASSERT_TRUE(topo.reader<StickButtonCh>(ctx).read());
    TEST_ASSERT_TRUE(topo.reader<PostResultCh>(ctx).read() == PostVerdict::PASS);
  });
}

TEST_CASE("TOP-013 task_config() returns the TASKS row's name, stack, priority and core",
          "[topology]") {
  const auto control = Topology::task_config(Task::CONTROL);
  TEST_ASSERT_EQUAL_STRING("control", control.name.c_str());
  TEST_ASSERT_EQUAL_UINT(6144U, control.stack_size_bytes);
  TEST_ASSERT_EQUAL_UINT(22U, control.priority);
  TEST_ASSERT_EQUAL_INT(0, control.core_id);
  const auto ui = Topology::task_config(Task::UI);
  TEST_ASSERT_EQUAL_STRING("ui", ui.name.c_str());
  TEST_ASSERT_EQUAL_INT(1, ui.core_id);
  const auto button = Topology::task_config(Task::STICK_BUTTON);
  TEST_ASSERT_EQUAL_STRING("Button", button.name.c_str());
  TEST_ASSERT_EQUAL_INT(-1, button.core_id);
}

TEST_CASE("TOP-014 the control island outranks the UI island", "[topology]") {
  // Plan §2.2: control above ui, so an LVGL stall cannot starve the stick (H4, H11).
  const TaskRow *control = find_task(Task::CONTROL);
  const TaskRow *ui = find_task(Task::UI);
  TEST_ASSERT_NOT_NULL(control);
  TEST_ASSERT_NOT_NULL(ui);
  TEST_ASSERT_TRUE(control->prio > ui->prio);
  TEST_ASSERT_TRUE(control->safety);
  TEST_ASSERT_NULL(find_task(Task::RTPS_RX));
}
