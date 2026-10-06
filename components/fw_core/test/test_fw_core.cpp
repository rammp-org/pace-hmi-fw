// L1 tests for fw_core (TS-UNIT-05), host-native in WSL.
// std::thread and sleep_for appear here only: they stand in for other tasks and for a slow
// receiver. Firmware code uses espp::Task (CS-CON-01).

#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include "fw_core/fw_core.hpp"

#include "unity.h"

namespace fw = hmi::fw;
using namespace std::chrono_literals;

namespace {

// ---------------------------------------------------------------------------------------------
// Test messages and helpers

struct SpeedMsg {
  static constexpr bool IS_MESSAGE = true;
  int speed_mm_s;
  unsigned seq;
};

struct EventMsg {
  static constexpr bool IS_MESSAGE = true;
  int id;
};

enum class Verdict : unsigned char { UNKNOWN, PASS, FAIL };

// What the injected failure handler saw. Written on the failing thread, read after join().
struct FailureRecord {
  int count{0};
  std::string function;
  std::string file;
  unsigned line{0};
  std::string caller;
  std::string owner;
  bool in_isr{false};
};
FailureRecord g_failure; // test-only recorder for the injected handler

void record_failure(const fw::OwnershipFailure &failure) {
  ++g_failure.count;
  g_failure.function = failure.where.function_name();
  g_failure.file = failure.where.file_name();
  g_failure.line = failure.where.line();
  g_failure.caller = std::string{failure.caller_task};
  g_failure.owner = std::string{failure.owner_task};
  g_failure.in_isr = failure.in_isr;
}

constexpr fw::ThreadChecker::Config RECORDING{.on_failure = &record_failure};

// Runs fn on a new thread with the given name (a name starting "isr" counts as an ISR).
template <class Fn> void run_on_thread(const char *name, Fn &&fn) {
  std::thread worker([&] {
    pthread_setname_np(pthread_self(), name);
    fn();
  });
  worker.join();
}

// Captures what is written to stderr between start() and stop().
class StderrCapture {
public:
  StderrCapture() {
    std::fflush(stderr);
    saved_ = dup(STDERR_FILENO);
    file_ = std::tmpfile();
    dup2(fileno(file_), STDERR_FILENO);
  }
  std::string stop() {
    std::fflush(stderr);
    dup2(saved_, STDERR_FILENO);
    close(saved_);
    std::rewind(file_);
    std::string text;
    std::array<char, 512> chunk{};
    size_t n = 0;
    while ((n = std::fread(chunk.data(), 1, chunk.size(), file_)) > 0) {
      text.append(chunk.data(), n);
    }
    std::fclose(file_);
    return text;
  }

private:
  int saved_{-1};
  std::FILE *file_{nullptr};
};

bool contains(const std::string &text, std::string_view part) {
  return text.find(part) != std::string::npos;
}

// The island that owns the tests' context tokens.
class TestIsland {
public:
  template <class Fn> static void with_context(Fn &&fn) {
    const fw::Context<TestIsland> ctx{fw::Passkey<TestIsland>{}};
    fn(ctx);
  }
};

// ---------------------------------------------------------------------------------------------
// Mailbox

void fwc_001() {
  fw::Mailbox<SpeedMsg> mailbox({.initial = {0, 0}, .reader_checker = RECORDING});
  mailbox.write({100, 1});
  mailbox.write({200, 2});
  mailbox.write({300, 3});
  SpeedMsg out{};
  TEST_ASSERT_EQUAL(fw::ReadStatus::CHANGED, mailbox.read(out));
  TEST_ASSERT_EQUAL_INT(300, out.speed_mm_s);
  TEST_ASSERT_EQUAL_UINT(3, out.seq);
}

void fwc_002() {
  fw::Mailbox<SpeedMsg> mailbox({.initial = {0, 0}, .reader_checker = RECORDING});
  SpeedMsg out{};
  mailbox.write({10, 1});
  TEST_ASSERT_EQUAL(fw::ReadStatus::CHANGED, mailbox.read(out));
  TEST_ASSERT_EQUAL(fw::ReadStatus::UNCHANGED, mailbox.read(out));
  TEST_ASSERT_EQUAL_INT(10, out.speed_mm_s);
  mailbox.write({20, 2});
  mailbox.write({30, 3});
  TEST_ASSERT_EQUAL(fw::ReadStatus::CHANGED, mailbox.read(out));
  TEST_ASSERT_EQUAL_INT(30, out.speed_mm_s);
  TEST_ASSERT_EQUAL(fw::ReadStatus::UNCHANGED, mailbox.read(out));
  TEST_ASSERT_EQUAL_INT(30, out.speed_mm_s);
}

void fwc_003() {
  fw::Mailbox<SpeedMsg> mailbox({.initial = {7, 0}, .reader_checker = RECORDING});
  SpeedMsg out{};
  TEST_ASSERT_EQUAL(fw::ReadStatus::UNCHANGED, mailbox.read(out));
  TEST_ASSERT_EQUAL_INT(7, out.speed_mm_s);
}

void fwc_004() {
  // No reader ever drains the mailbox: 10,000 writes from a task and from an "ISR" must all
  // return at once. A blocking write would hang here (and the runner's timeout would fail it).
  constexpr unsigned WRITES = 10000;
  fw::Mailbox<SpeedMsg> mailbox({.initial = {0, 0}, .reader_checker = RECORDING});
  auto writer = mailbox.writer();
  const auto start = std::chrono::steady_clock::now();
  for (unsigned i = 1; i <= WRITES; ++i) {
    writer.write({static_cast<int>(i), i});
  }
  bool woken = true;
  run_on_thread("isr_sim", [&] { writer.write_from_isr({-1, WRITES + 1}, woken); });
  const auto elapsed = std::chrono::steady_clock::now() - start;
  TEST_ASSERT_TRUE(elapsed < 2s);
  TEST_ASSERT_FALSE(woken);
  SpeedMsg out{};
  auto reader = mailbox.reader();
  TEST_ASSERT_EQUAL(fw::ReadStatus::CHANGED, reader.read(out));
  TEST_ASSERT_EQUAL_INT(-1, out.speed_mm_s);
  TEST_ASSERT_EQUAL_UINT(WRITES + 1, out.seq);
}

void fwc_005() {
  g_failure = {};
  fw::Mailbox<SpeedMsg> mailbox({.initial = {1, 0}, .reader_checker = RECORDING});
  SpeedMsg out{};
  TEST_ASSERT_EQUAL(fw::ReadStatus::UNCHANGED, mailbox.read(out)); // binds the reader
  mailbox.write({2, 1});
  SpeedMsg other{-5, 99};
  fw::ReadStatus status = fw::ReadStatus::CHANGED;
  run_on_thread("intruder", [&] { status = mailbox.read(other); });
  TEST_ASSERT_EQUAL(fw::ReadStatus::WRONG_TASK, status);
  TEST_ASSERT_EQUAL_INT(-5, other.speed_mm_s);
  TEST_ASSERT_EQUAL_INT(1, g_failure.count);
  // The intruder did not consume the write: the owner still sees it once.
  TEST_ASSERT_EQUAL(fw::ReadStatus::CHANGED, mailbox.read(out));
  TEST_ASSERT_EQUAL_INT(2, out.speed_mm_s);
}

// ---------------------------------------------------------------------------------------------
// Queue

using DropQueue = fw::Queue<EventMsg, 4, fw::FullPolicy::DROP_NEWEST_COUNT>;
using BlockQueue = fw::Queue<EventMsg, 2, fw::FullPolicy::BLOCK_TIMEOUT>;
using FaultQueue = fw::Queue<EventMsg, 2, fw::FullPolicy::RAISE_FAULT>;

void fwc_006() {
  DropQueue queue({.name = "order", .receiver_checker = RECORDING});
  std::error_code ec;
  for (int i = 1; i <= 4; ++i) {
    TEST_ASSERT_TRUE(queue.send({i}, ec));
  }
  TEST_ASSERT_FALSE(static_cast<bool>(ec));
  TEST_ASSERT_EQUAL_size_t(4, queue.size());
  for (int i = 1; i <= 4; ++i) {
    EventMsg out{};
    TEST_ASSERT_TRUE(queue.receive(out));
    TEST_ASSERT_EQUAL_INT(i, out.id);
  }
  EventMsg out{-1};
  TEST_ASSERT_FALSE(queue.receive(out));
  TEST_ASSERT_EQUAL_INT(-1, out.id);
}

void fwc_007() {
  DropQueue queue({.name = "drop", .receiver_checker = RECORDING});
  std::error_code ec;
  for (int i = 1; i <= 4; ++i) {
    TEST_ASSERT_TRUE(queue.send({i}, ec));
  }
  const auto start = std::chrono::steady_clock::now();
  TEST_ASSERT_FALSE(queue.send({5}, ec));
  TEST_ASSERT_FALSE(queue.send({6}, ec));
  TEST_ASSERT_TRUE(std::chrono::steady_clock::now() - start < 500ms); // dropping never waits
  TEST_ASSERT_TRUE(ec == fw::Error::QUEUE_FULL);
  TEST_ASSERT_EQUAL_UINT32(2, queue.dropped());
  TEST_ASSERT_FALSE(queue.faulted());
  std::array<int, 4> seen{};
  std::size_t n = 0;
  TEST_ASSERT_EQUAL_size_t(4, queue.drain([&](const EventMsg &m) { seen.at(n++) = m.id; }));
  TEST_ASSERT_EQUAL_INT(1, seen[0]);
  TEST_ASSERT_EQUAL_INT(4, seen[3]); // the newest (5, 6) were dropped, the oldest kept
}

void fwc_008() {
  BlockQueue queue({.name = "block", .send_timeout = 50ms, .receiver_checker = RECORDING});
  std::error_code ec;
  TEST_ASSERT_TRUE(queue.send({1}, ec));
  TEST_ASSERT_TRUE(queue.send({2}, ec));
  const auto start = std::chrono::steady_clock::now();
  TEST_ASSERT_FALSE(queue.send({3}, ec));
  const auto waited = std::chrono::steady_clock::now() - start;
  TEST_ASSERT_TRUE(waited >= 50ms);
  TEST_ASSERT_TRUE(waited < 2s);
  TEST_ASSERT_TRUE(ec == fw::Error::SEND_TIMEOUT);
  TEST_ASSERT_EQUAL_UINT32(0, queue.dropped());
  TEST_ASSERT_FALSE(queue.faulted());
  TEST_ASSERT_EQUAL_size_t(2, queue.size());
}

void fwc_009() {
  BlockQueue queue({.name = "block", .send_timeout = 2000ms, .receiver_checker = RECORDING});
  std::error_code ec;
  TEST_ASSERT_TRUE(queue.send({1}, ec));
  TEST_ASSERT_TRUE(queue.send({2}, ec));
  bool sent = false;
  std::thread producer([&] {
    pthread_setname_np(pthread_self(), "producer");
    std::error_code producer_ec;
    sent = queue.send({3}, producer_ec);
  });
  std::this_thread::sleep_for(20ms); // let the producer block on the full queue
  EventMsg out{};
  TEST_ASSERT_TRUE(queue.receive(out));
  producer.join();
  TEST_ASSERT_TRUE(sent);
  TEST_ASSERT_TRUE(queue.receive(out));
  TEST_ASSERT_EQUAL_INT(2, out.id);
  TEST_ASSERT_TRUE(queue.receive(out, 100ms));
  TEST_ASSERT_EQUAL_INT(3, out.id);
}

void fwc_010() {
  FaultQueue queue({.name = "intents", .receiver_checker = RECORDING});
  std::error_code ec;
  TEST_ASSERT_TRUE(queue.send({1}, ec));
  TEST_ASSERT_TRUE(queue.send({2}, ec));
  StderrCapture capture;
  TEST_ASSERT_FALSE(queue.send({3}, ec));
  TEST_ASSERT_FALSE(queue.send({4}, ec));
  const std::string log = capture.stop();
  TEST_ASSERT_TRUE(ec == fw::Error::QUEUE_FAULT);
  TEST_ASSERT_TRUE(queue.faulted());
  TEST_ASSERT_EQUAL_UINT32(0, queue.dropped());
  TEST_ASSERT_TRUE(contains(log, "queue 'intents' overflowed"));
  TEST_ASSERT_EQUAL_size_t(log.find("overflowed"), log.rfind("overflowed")); // logged once
  auto receiver = queue.receiver();
  TEST_ASSERT_TRUE(receiver.faulted());
  receiver.clear_fault();
  TEST_ASSERT_FALSE(queue.faulted());
  EventMsg out{};
  TEST_ASSERT_TRUE(receiver.receive(out));
  TEST_ASSERT_EQUAL_INT(1, out.id); // order kept, nothing overwritten
}

void fwc_011() {
  DropQueue drop({.name = "isr_drop", .receiver_checker = RECORDING});
  BlockQueue block({.name = "isr_block", .send_timeout = 5000ms, .receiver_checker = RECORDING});
  FaultQueue fault({.name = "isr_fault", .receiver_checker = RECORDING});
  auto drop_tx = drop.sender();
  auto block_tx = block.sender();
  auto fault_tx = fault.sender();
  int queued = 0;
  bool woken = true;
  const auto start = std::chrono::steady_clock::now();
  run_on_thread("isr_sim", [&] {
    for (int i = 0; i < 6; ++i) {
      queued += drop_tx.send_from_isr({i}, woken) ? 1 : 0;
      queued += block_tx.send_from_isr({i}, woken) ? 1 : 0;
      queued += fault_tx.send_from_isr({i}, woken) ? 1 : 0;
    }
  });
  TEST_ASSERT_TRUE(std::chrono::steady_clock::now() - start < 1s); // BLOCK_TIMEOUT did not wait
  TEST_ASSERT_EQUAL_INT(4 + 2 + 2, queued);
  TEST_ASSERT_FALSE(woken);
  TEST_ASSERT_EQUAL_UINT32(2, drop.dropped());
  TEST_ASSERT_EQUAL_UINT32(0, block.dropped());
  TEST_ASSERT_TRUE(fault.faulted());
  TEST_ASSERT_FALSE(block.faulted());
  std::error_code ec;
  TEST_ASSERT_TRUE(drop_tx.send({9}, ec) == false && ec == fw::Error::QUEUE_FULL);
}

void fwc_012() {
  fw::Queue<EventMsg, 3, fw::FullPolicy::DROP_NEWEST_COUNT> queue(
      {.name = "drain", .receiver_checker = RECORDING});
  std::error_code ec;
  TEST_ASSERT_TRUE(queue.send({1}, ec));
  TEST_ASSERT_TRUE(queue.send({2}, ec));
  auto receiver = queue.receiver();
  std::array<int, 3> seen{};
  std::size_t n = 0;
  TEST_ASSERT_EQUAL_size_t(2, receiver.drain([&](const EventMsg &m) { seen.at(n++) = m.id; }));
  TEST_ASSERT_EQUAL_INT(1, seen[0]);
  TEST_ASSERT_EQUAL_INT(2, seen[1]);
  TEST_ASSERT_EQUAL_size_t(0, receiver.drain([&](const EventMsg &) { ++n; }));
  TEST_ASSERT_EQUAL_size_t(2, n);
  TEST_ASSERT_EQUAL_UINT32(0, receiver.dropped());
}

void fwc_013() {
  g_failure = {};
  DropQueue queue({.name = "owned_rx", .receiver_checker = RECORDING});
  EventMsg out{-1};
  TEST_ASSERT_FALSE(queue.receive(out)); // empty; binds the receiver to this thread
  std::error_code ec;
  TEST_ASSERT_TRUE(queue.send({1}, ec));
  bool received = true;
  std::size_t drained = 99;
  EventMsg stolen{-7};
  run_on_thread("intruder", [&] {
    received = queue.receive(stolen);
    drained = queue.drain([](const EventMsg &) {});
  });
  TEST_ASSERT_FALSE(received);
  TEST_ASSERT_EQUAL_size_t(0, drained);
  TEST_ASSERT_EQUAL_INT(-7, stolen.id);
  TEST_ASSERT_EQUAL_INT(2, g_failure.count);
  TEST_ASSERT_TRUE(queue.receive(out));
  TEST_ASSERT_EQUAL_INT(1, out.id);
}

// ---------------------------------------------------------------------------------------------
// Atomic value

void fwc_014() {
  static_assert(std::atomic<Verdict>::is_always_lock_free);
  fw::AtomicValue<Verdict> verdict({.initial = Verdict::UNKNOWN});
  auto writer = verdict.writer();
  auto reader = verdict.reader();
  TEST_ASSERT_EQUAL(Verdict::UNKNOWN, reader.read());
  run_on_thread("selftest", [&] { writer.write(Verdict::PASS); });
  TEST_ASSERT_EQUAL(Verdict::PASS, reader.read());
  run_on_thread("isr_sim", [&] { writer.write(Verdict::FAIL); });
  TEST_ASSERT_EQUAL(Verdict::FAIL, verdict.read());
  verdict.write(Verdict::PASS);
  TEST_ASSERT_EQUAL(Verdict::PASS, reader.read());
}

// ---------------------------------------------------------------------------------------------
// ThreadChecker

void fwc_015() {
  g_failure = {};
  fw::ThreadChecker checker(RECORDING);
  TEST_ASSERT_FALSE(checker.is_bound());
  for (int i = 0; i < 100; ++i) {
    TEST_ASSERT_TRUE(checker.check());
  }
  TEST_ASSERT_TRUE(checker.is_bound());
  TEST_ASSERT_EQUAL_INT(0, g_failure.count);
}

void fwc_016() {
  g_failure = {};
  pthread_setname_np(pthread_self(), "owner_task");
  fw::ThreadChecker checker(RECORDING);
  TEST_ASSERT_TRUE(checker.check());
  bool result = true;
  unsigned expected_line = 0;
  run_on_thread("other_task", [&] {
    expected_line = std::source_location::current().line() + 1;
    result = checker.check();
  });
  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_EQUAL_INT(1, g_failure.count);
  TEST_ASSERT_EQUAL_STRING("other_task", g_failure.caller.c_str());
  TEST_ASSERT_EQUAL_STRING("owner_task", g_failure.owner.c_str());
  TEST_ASSERT_FALSE(g_failure.in_isr);
  TEST_ASSERT_EQUAL_UINT(expected_line, g_failure.line);
  TEST_ASSERT_TRUE(contains(g_failure.file, "test_fw_core.cpp"));
  TEST_ASSERT_TRUE(contains(g_failure.function, "fwc_016"));
  TEST_ASSERT_TRUE(checker.check()); // the owner still passes
  pthread_setname_np(pthread_self(), "test_fw_core");
}

void fwc_017() {
  g_failure = {};
  fw::ThreadChecker checker(RECORDING);
  bool result = true;
  // From an ISR: fails even before any task owns the checker, and does not bind it.
  run_on_thread("isr_sim", [&] { result = checker.check(); });
  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_FALSE(checker.is_bound());
  TEST_ASSERT_EQUAL_INT(1, g_failure.count);
  TEST_ASSERT_TRUE(g_failure.in_isr);
  TEST_ASSERT_EQUAL_STRING("ISR", g_failure.caller.c_str());
  TEST_ASSERT_EQUAL_STRING("?", g_failure.owner.c_str());
  // From an ISR on a bound checker: fails too.
  TEST_ASSERT_TRUE(checker.check());
  run_on_thread("isr_sim", [&] { result = checker.check(); });
  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_EQUAL_INT(2, g_failure.count);
}

void fwc_018() {
  g_failure = {};
  fw::ThreadChecker checker(RECORDING);
  bool bound = false;
  run_on_thread("island", [&] { bound = checker.bind_to_current_task(); });
  TEST_ASSERT_TRUE(bound);
  TEST_ASSERT_TRUE(checker.is_bound());
  TEST_ASSERT_FALSE(checker.bind_to_current_task()); // this thread is not the island
  TEST_ASSERT_FALSE(checker.check());
  TEST_ASSERT_EQUAL_INT(2, g_failure.count);
  TEST_ASSERT_EQUAL_STRING("island", g_failure.owner.c_str());
}

void fwc_019() {
  // Test builds abort on a failed ownership check (OWNERSHIP_CHECKS_ABORT, set by the Makefile).
  static_assert(fw::OWNERSHIP_CHECKS_ABORT, "the L1 build must use the test-build behaviour");
  std::fflush(stdout);
  std::fflush(stderr);
  const pid_t child = fork();
  TEST_ASSERT_TRUE(child >= 0);
  if (child == 0) {
    fw::ThreadChecker checker; // default handler: log, then abort
    pthread_setname_np(pthread_self(), "isr_child");
    const bool passed = checker.check();
    _exit(passed ? 2 : 3); // reached only if the handler did not abort
  }
  int status = 0;
  TEST_ASSERT_EQUAL_INT(child, waitpid(child, &status, 0));
  TEST_ASSERT_TRUE(WIFSIGNALED(status));
  TEST_ASSERT_EQUAL_INT(SIGABRT, WTERMSIG(status));
}

void fwc_020() {
  // Release builds log and return false (the release instantiation of the default handler).
  fw::ThreadChecker checker({.on_failure = &fw::default_failure_handler<false>});
  TEST_ASSERT_TRUE(checker.check());
  bool result = true;
  StderrCapture capture;
  run_on_thread("isr_sim", [&] { result = checker.check(); });
  const std::string log = capture.stop();
  TEST_ASSERT_FALSE(result);
  TEST_ASSERT_TRUE(contains(log, "ownership check failed in"));
  TEST_ASSERT_TRUE(contains(log, "test_fw_core.cpp:"));
  TEST_ASSERT_TRUE(contains(log, "caller task 'ISR' (ISR)"));
  TEST_ASSERT_TRUE(contains(log, "owner task 'test_fw_core'"));
  // A null handler falls back to the default one.
  fw::ThreadChecker fallback({.on_failure = nullptr});
  TEST_ASSERT_TRUE(fallback.check());
}

// ---------------------------------------------------------------------------------------------
// Owned<T>

struct Counter {
  explicit Counter(int start)
      : value(start) {}
  int value;
};

void fwc_021() {
  g_failure = {};
  fw::Owned<Counter> counter({.checker = RECORDING}, 40);
  const fw::Owned<Counter> &view = counter;
  int seen = 0;
  TestIsland::with_context([&](const fw::ContextBase &ctx) {
    TEST_ASSERT_TRUE(counter.access(ctx, [](Counter &c) { c.value += 2; }));
    TEST_ASSERT_TRUE(view.access(ctx, [&](const Counter &c) { seen = c.value; }));
  });
  TEST_ASSERT_EQUAL_INT(42, seen);
  bool ran = false;
  bool mutable_ok = true;
  bool const_ok = true;
  run_on_thread("intruder", [&] {
    TestIsland::with_context([&](const fw::ContextBase &ctx) {
      mutable_ok = counter.access(ctx, [&](Counter &) { ran = true; });
      const_ok = view.access(ctx, [&](const Counter &) { ran = true; });
    });
  });
  TEST_ASSERT_FALSE(mutable_ok);
  TEST_ASSERT_FALSE(const_ok);
  TEST_ASSERT_FALSE(ran);
  TEST_ASSERT_EQUAL_INT(2, g_failure.count);
  TEST_ASSERT_EQUAL_STRING("intruder", g_failure.caller.c_str());
}

// ---------------------------------------------------------------------------------------------
// check()

void fwc_022() {
  StderrCapture capture;
  const bool passed = fw::check(true, "never logged");
  const unsigned line = std::source_location::current().line() + 1;
  const bool failed = fw::check(1 > 2, "one above two");
  const bool failed_quietly = fw::check(false);
  const std::string log = capture.stop();
  TEST_ASSERT_TRUE(passed);
  TEST_ASSERT_FALSE(failed);
  TEST_ASSERT_FALSE(failed_quietly);
  TEST_ASSERT_TRUE(contains(log, "check failed: one above two in"));
  TEST_ASSERT_TRUE(contains(log, "test_fw_core.cpp:" + std::to_string(line)));
  TEST_ASSERT_TRUE(contains(log, "check failed: (no description)"));
  TEST_ASSERT_FALSE(contains(log, "never logged"));
}

// ---------------------------------------------------------------------------------------------
// Errors, names, compile-time rules, host port

void fwc_023() {
  const std::error_code ec = fw::Error::QUEUE_FULL;
  TEST_ASSERT_EQUAL_STRING("fw_core", ec.category().name());
  TEST_ASSERT_EQUAL_STRING("QUEUE_FULL", ec.message().c_str());
  TEST_ASSERT_TRUE(fw::to_string(fw::Error::SEND_TIMEOUT) == "SEND_TIMEOUT");
  TEST_ASSERT_TRUE(fw::to_string(fw::Error::QUEUE_FAULT) == "QUEUE_FAULT");
  TEST_ASSERT_TRUE(fw::to_string(static_cast<fw::Error>(0)) == "UNKNOWN");
  TEST_ASSERT_TRUE(fw::to_string(fw::ReadStatus::UNCHANGED) == "UNCHANGED");
  TEST_ASSERT_TRUE(fw::to_string(fw::ReadStatus::CHANGED) == "CHANGED");
  TEST_ASSERT_TRUE(fw::to_string(fw::ReadStatus::WRONG_TASK) == "WRONG_TASK");
  TEST_ASSERT_TRUE(fw::to_string(static_cast<fw::ReadStatus>(99)) == "UNKNOWN");
  TEST_ASSERT_TRUE(fw::to_string(fw::FullPolicy::DROP_NEWEST_COUNT) == "DROP_NEWEST_COUNT");
  TEST_ASSERT_TRUE(fw::to_string(fw::FullPolicy::BLOCK_TIMEOUT) == "BLOCK_TIMEOUT");
  TEST_ASSERT_TRUE(fw::to_string(fw::FullPolicy::RAISE_FAULT) == "RAISE_FAULT");
  TEST_ASSERT_TRUE(fw::to_string(static_cast<fw::FullPolicy>(99)) == "UNKNOWN");
}

struct Unmarked {
  int x;
};
struct Big {
  static constexpr bool IS_MESSAGE = true;
  std::array<char, 129> bytes;
};
struct Holder {
  static constexpr bool IS_MESSAGE = true;
  std::string text;
};

void fwc_024() {
  // The rules the must-not-compile tests prove from the other side (TS-UNIT-06).
  static_assert(fw::Message<SpeedMsg>);
  static_assert(!fw::Message<Unmarked>);
  static_assert(!fw::Message<Big>);
  static_assert(!fw::Message<Holder>);
  using Token = fw::Context<TestIsland>;
  static_assert(!std::is_default_constructible_v<Token>);
  static_assert(!std::is_copy_constructible_v<Token>);
  static_assert(!std::is_move_constructible_v<Token>);
  static_assert(!std::is_copy_assignable_v<Token>);
  static_assert(!std::is_default_constructible_v<fw::Passkey<TestIsland>>);
  static_assert(std::is_constructible_v<Token, fw::Passkey<TestIsland>>);
  TEST_PASS();
}

void fwc_025() {
  // Unity's assertions must run on the test's own thread, so the worker only records.
  std::array<char, 4> small{'x', 'x', 'x', 'x'};
  std::array<char, 1> none{'x'};
  bool isr = true;
  hmi::fw::port::TaskId id = hmi::fw::port::NO_TASK;
  run_on_thread("long_task_name", [&] {
    hmi::fw::port::current_task_name(small);
    hmi::fw::port::current_task_name(std::span<char>{none.data(), 0});
    isr = hmi::fw::port::in_isr();
    id = hmi::fw::port::current_task();
  });
  TEST_ASSERT_EQUAL_STRING("lon", small.data());
  TEST_ASSERT_EQUAL_CHAR('x', none[0]);
  TEST_ASSERT_FALSE(isr);
  TEST_ASSERT_TRUE(id != hmi::fw::port::NO_TASK);
  TEST_ASSERT_TRUE(id != hmi::fw::port::current_task());
}

} // namespace

// ---------------------------------------------------------------------------------------------

void setUp() {}
void tearDown() {}

int main() {
  pthread_setname_np(pthread_self(), "test_fw_core");
  UNITY_BEGIN();
  struct Case {
    void (*fn)();
    const char *name;
  };
  static constexpr std::array CASES{
      Case{fwc_001, "FWC-001 a mailbox returns the newest value"},
      Case{fwc_002, "FWC-002 a mailbox reports changed once per write"},
      Case{fwc_003, "FWC-003 a mailbox read before any write returns the initial value, unchanged"},
      Case{fwc_004, "FWC-004 a mailbox writer never blocks, from a task or an ISR"},
      Case{fwc_005, "FWC-005 a mailbox read off the reader's task fails and consumes nothing"},
      Case{fwc_006, "FWC-006 a queue keeps order"},
      Case{fwc_007, "FWC-007 a DROP_NEWEST_COUNT queue drops the newest message and counts it"},
      Case{fwc_008, "FWC-008 a BLOCK_TIMEOUT queue returns SEND_TIMEOUT after its timeout"},
      Case{fwc_009, "FWC-009 a BLOCK_TIMEOUT send completes when the receiver makes room"},
      Case{fwc_010, "FWC-010 a RAISE_FAULT queue sets its fault flag, logs once and keeps order"},
      Case{fwc_011, "FWC-011 send_from_isr never waits and applies each full policy"},
      Case{fwc_012, "FWC-012 drain hands over the waiting messages in order"},
      Case{fwc_013, "FWC-013 a queue receive off the receiver's task fails and consumes nothing"},
      Case{fwc_014, "FWC-014 an atomic value returns the last write through narrow handles"},
      Case{fwc_015, "FWC-015 a ThreadChecker passes on its owner"},
      Case{fwc_016,
           "FWC-016 a ThreadChecker fails on another task, naming both tasks and the call"},
      Case{fwc_017, "FWC-017 a ThreadChecker fails in an ISR"},
      Case{fwc_018, "FWC-018 a ThreadChecker bound explicitly fails on every other task"},
      Case{fwc_019, "FWC-019 a failed ownership check aborts in a test build"},
      Case{fwc_020, "FWC-020 a failed ownership check logs and returns false in a release build"},
      Case{fwc_021, "FWC-021 Owned runs the access on its owner only"},
      Case{fwc_022, "FWC-022 check returns false and logs what failed and where"},
      Case{fwc_023, "FWC-023 errors and enums have names for logs"},
      Case{fwc_024, "FWC-024 messages and context tokens obey their compile-time rules"},
      Case{fwc_025, "FWC-025 the host port names tasks and cuts long names to fit"},
  };
  for (const Case &c : CASES) {
    UnityDefaultTestRun(c.fn, c.name, __LINE__);
  }
  return UNITY_END();
}
