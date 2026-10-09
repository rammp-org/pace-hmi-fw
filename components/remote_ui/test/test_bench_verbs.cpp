// Host L1 tests for components/remote_ui's bench verbs (bench_verbs.hpp; AGENT_BRIEF host L1
// contract, TS-UNIT-02). spec-deviation(TS-UNIT-01): host-native g++ in WSL, not the IDF linux
// target (owner, 2026-10-06).
//
// Expected values are written from the specs (hazard-c1-spec.md §6, hazard-c3-spec.md
// REQ-RUI-06, hazard-c4-spec.md REQ-RUI-07, hazard-c2-spec.md REQ-RUI-08) and from the STATE
// key table in bench_verbs.hpp, never from the code's output.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bench_verbs.hpp"
#include "test_case.hpp"

namespace {

namespace bv = hmi::bench_verbs;

bv::Request request_of(std::string_view line) {
  const bv::Parsed parsed = bv::parse(line);
  TEST_ASSERT_TRUE_MESSAGE(parsed.bench_verb, std::string(line).c_str());
  TEST_ASSERT_TRUE_MESSAGE(parsed.request.has_value(), std::string(line).c_str());
  return *parsed.request;
}

void expect_usage(std::string_view line) {
  const bv::Parsed parsed = bv::parse(line);
  TEST_ASSERT_TRUE_MESSAGE(parsed.bench_verb, std::string(line).c_str());
  TEST_ASSERT_FALSE_MESSAGE(parsed.request.has_value(), std::string(line).c_str());
  TEST_ASSERT_TRUE_MESSAGE(parsed.usage.starts_with("usage: "), std::string(line).c_str());
}

bv::Hooks all_hooks() {
  bv::Hooks h;
  h.phase = [] { return std::string("DRIVING"); };
  h.notice = [] { return std::string("CENTRE_FIRST"); };
  h.banner = [] { return std::string("REFUSED_POST"); };
  h.hold_reason = [] { return std::string("CENTRE_FIRST"); };
  h.calibrating = [] { return false; };
  h.menu_open = [] { return true; };
  h.cal = [] {
    return bv::CalRecord{bv::CalAxis{11, 1507, 2971}, bv::CalAxis{6, 1510, 2962},
                         bv::CalAxis{10, 1477, 2960}};
  };
  h.post = [] { return std::string("PASS"); };
  h.post_check = [] { return std::optional<std::string>{}; };
  h.indicator = [] { return std::string("NONE"); };
  h.indicator_text = [] { return std::string(); };
  h.reset_reason = [] { return std::string("SW"); };
  h.stick = [] {
    bv::StickBlock s;
    s.state = "OK";
    s.health = "OK";
    s.reason = "NONE";
    s.reason_axis = "";
    s.fault_reason = "NONE";
    s.fault_axis = "";
    s.suspect_onsets = 2;
    s.faults = 0;
    s.bad = {1, 0, 0, 3, 0, 0};
    s.xy_age_max_ms = 131;
    s.max_mv = {2975, 2964, 2961};
    s.joy_key = 0;
    return s;
  };
  return h;
}

} // namespace

TEST_CASE("RUI-001 every bench verb of the specs parses to its request",
          "[remote_ui][REQ-RUI-05][REQ-RUI-06][REQ-RUI-07]") {
  TEST_ASSERT_TRUE(request_of("STATE").verb == bv::Verb::STATE);
  struct PostCase {
    const char *line;
    bv::PostGateName want;
  };
  for (const PostCase c : {PostCase{"PERMIT POST not_run", bv::PostGateName::NOT_RUN},
                           PostCase{"PERMIT POST pending", bv::PostGateName::PENDING},
                           PostCase{"PERMIT POST pass", bv::PostGateName::PASS},
                           PostCase{"PERMIT POST fail", bv::PostGateName::FAIL}}) {
    const bv::Request r = request_of(c.line);
    TEST_ASSERT_TRUE_MESSAGE(r.verb == bv::Verb::PERMIT_POST, c.line);
    TEST_ASSERT_TRUE_MESSAGE(r.post == c.want, c.line);
  }
  struct StickCase {
    const char *line;
    bv::StickHealthName want;
  };
  for (const StickCase c :
       {StickCase{"PERMIT STICK not_monitored", bv::StickHealthName::NOT_MONITORED},
        StickCase{"PERMIT STICK ok", bv::StickHealthName::OK},
        StickCase{"PERMIT STICK fault", bv::StickHealthName::FAULT}}) {
    const bv::Request r = request_of(c.line);
    TEST_ASSERT_TRUE_MESSAGE(r.verb == bv::Verb::PERMIT_STICK, c.line);
    TEST_ASSERT_TRUE_MESSAGE(r.stick == c.want, c.line);
  }
  TEST_ASSERT_TRUE(request_of("CAL UNSAVED").verb == bv::Verb::CAL_UNSAVED);
  TEST_ASSERT_TRUE(request_of("POST RERUN").verb == bv::Verb::POST_RERUN);
  TEST_ASSERT_TRUE(request_of("CRASH").verb == bv::Verb::CRASH);
  struct StallCase {
    const char *line;
    bv::StallTarget target;
    std::uint32_t ms;
  };
  for (const StallCase c : {StallCase{"STALL UI 300", bv::StallTarget::UI, 300},
                            StallCase{"STALL UI 3000", bv::StallTarget::UI, 3000},
                            StallCase{"STALL ADC 3000", bv::StallTarget::ADC, 3000},
                            StallCase{"STALL CADC 1000", bv::StallTarget::CADC, 1000},
                            StallCase{"STALL UI 1", bv::StallTarget::UI, 1},
                            StallCase{"STALL ADC 10000", bv::StallTarget::ADC, 10000},
                            StallCase{"  STALL   UI   300  ", bv::StallTarget::UI, 300}}) {
    const bv::Request r = request_of(c.line);
    TEST_ASSERT_TRUE_MESSAGE(r.verb == bv::Verb::STALL, c.line);
    TEST_ASSERT_TRUE_MESSAGE(r.stall == c.target, c.line);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.ms, r.stall_ms, c.line);
  }
}

TEST_CASE("RUI-002 a bench verb with a wrong, missing or extra argument gets its usage",
          "[remote_ui][REQ-RUI-05][REQ-RUI-06][REQ-RUI-07]") {
  for (const char *line : {"STATE now",
                           "PERMIT",
                           "PERMIT POST",
                           "PERMIT POST PASS",
                           "PERMIT POST passed",
                           "PERMIT POST pass now",
                           "PERMIT STICK check",
                           "PERMIT STICK OK",
                           "PERMIT GATE pass",
                           "CAL",
                           "CAL SAVED",
                           "CAL UNSAVED now",
                           "POST",
                           "POST rerun",
                           "POST RERUN 1",
                           "CRASH 1",
                           "STALL",
                           "STALL UI",
                           "STALL UI 0",
                           "STALL UI 10001",
                           "STALL UI 99999",
                           "STALL UI 123456",
                           "STALL UI -1",
                           "STALL UI 3e3",
                           "STALL UI 300ms",
                           "STALL ui 300",
                           "STALL LVGL 300",
                           "STALL UI 300 1"}) {
    expect_usage(line);
  }
}

TEST_CASE("RUI-003 a line that is not a bench verb is left to the remote UI's own verbs",
          "[remote_ui][REQ-RUI-05]") {
  for (const char *line :
       {"", "   ", "PING", "SCREEN", "STICK 1507 1510 1477 0 1", "TAP 1 2", "BTN 1", "TASKS",
        "SHOT", "STATEX", "state", "Permit POST pass", "CRASHED", "STALLS UI 300"}) {
    const bv::Parsed parsed = bv::parse(line);
    TEST_ASSERT_FALSE_MESSAGE(parsed.bench_verb, line);
    TEST_ASSERT_FALSE_MESSAGE(parsed.request.has_value(), line);
  }
}

TEST_CASE("RUI-004 STATE with no hook wired reports the screen and null for everything else",
          "[remote_ui][REQ-RUI-05]") {
  const std::string got =
      bv::answer(request_of("STATE"), bv::Hooks{}, std::optional<std::string>{"LockedScreen"});
  TEST_ASSERT_EQUAL_STRING(
      "OK "
      "{\"screen\":\"LockedScreen\",\"phase\":null,\"notice\":null,\"banner\":null,\"hold_reason\":"
      "null,"
      "\"calibrating\":null,\"menu_open\":null,\"cal\":null,\"post\":null,\"post_check\":null,"
      "\"indicator\":null,\"indicator_text\":null,\"reset_reason\":null,\"stick\":null}",
      got.c_str());
}

TEST_CASE("RUI-005 STATE with every hook wired is one JSON line in the documented key order",
          "[remote_ui][REQ-RUI-05][REQ-RUI-06]") {
  const std::string got =
      bv::answer(request_of("STATE"), all_hooks(), std::optional<std::string>{"DriveScreen"});
  TEST_ASSERT_EQUAL_STRING(
      "OK "
      "{\"screen\":\"DriveScreen\",\"phase\":\"DRIVING\",\"notice\":\"CENTRE_FIRST\",\"banner\":"
      "\"REFUSED_POST\","
      "\"hold_reason\":\"CENTRE_FIRST\",\"calibrating\":false,\"menu_open\":true,"
      "\"cal\":{\"h\":[11,1507,2971],\"v\":[6,1510,2962],\"twist\":[10,1477,2960]},"
      "\"post\":\"PASS\",\"post_check\":null,\"indicator\":\"NONE\",\"indicator_text\":\"\","
      "\"reset_reason\":\"SW\",\"stick\":{\"state\":\"OK\",\"health\":\"OK\",\"reason\":\"NONE\","
      "\"reason_axis\":\"\",\"fault_reason\":\"NONE\",\"fault_axis\":\"\",\"suspect_onsets\":2,"
      "\"faults\":0,"
      "\"bad\":{\"MISSING\":1,\"NAN\":0,\"NEGATIVE\":0,\"HIGH\":3,\"STALE\":0,"
      "\"TWIST_PARTIAL\":0},\"xy_age_max_ms\":131,\"max_mv\":[2975,2964,2961],\"joy_key\":0}}",
      got.c_str());
}

TEST_CASE("RUI-006 STATE escapes quotes, backslashes and control characters in its strings",
          "[remote_ui][REQ-RUI-06]") {
  bv::Hooks h;
  h.post_check = [] { return std::optional<std::string>{"joy.y_cal_off"}; };
  h.indicator_text = [] { return std::string("Restarted after a fault: \"PANIC\"\\\n\t"); };
  h.calibrating = [] { return true; };
  const std::string got =
      bv::answer(request_of("STATE"), h, std::optional<std::string>{"a\"b\\c\x01"});
  TEST_ASSERT_EQUAL_STRING(
      "OK {\"screen\":\"a\\\"b\\\\c\\u0001\",\"phase\":null,\"notice\":null,\"banner\":null,"
      "\"hold_reason\":null,\"calibrating\":true,\"menu_open\":null,\"cal\":null,\"post\":null,"
      "\"post_check\":\"joy.y_cal_off\",\"indicator\":null,"
      "\"indicator_text\":\"Restarted after a fault: \\\"PANIC\\\"\\\\\\u000a\\u0009\","
      "\"reset_reason\":null,\"stick\":null}",
      got.c_str());
}

TEST_CASE("RUI-007 a verb whose hook is not wired says so; a wired hook gets the parsed value",
          "[remote_ui][REQ-RUI-05][REQ-RUI-06][REQ-RUI-07]") {
  const bv::Hooks none;
  for (const char *line :
       {"PERMIT POST pass", "PERMIT STICK fault", "CAL UNSAVED", "POST RERUN", "STALL UI 300"}) {
    const std::string got = bv::answer(request_of(line), none, std::nullopt);
    TEST_ASSERT_TRUE_MESSAGE(got.starts_with("ERR "), got.c_str());
    TEST_ASSERT_TRUE_MESSAGE(got.find(" not wired in this firmware (") != std::string::npos,
                             got.c_str());
  }

  std::vector<std::string> seen;
  bv::Hooks h;
  h.permit_post = [&seen](bv::PostGateName v) {
    seen.push_back("post" + std::to_string(static_cast<int>(v)));
    return true;
  };
  h.permit_stick = [&seen](bv::StickHealthName v) {
    seen.push_back("stick" + std::to_string(static_cast<int>(v)));
    return false;
  };
  h.cal_unsaved = [&seen] {
    seen.emplace_back("cal");
    return true;
  };
  h.post_rerun = [&seen] {
    seen.emplace_back("rerun");
    return true;
  };
  h.stall = [&seen](bv::StallTarget t, std::uint32_t ms) {
    seen.push_back("stall" + std::to_string(static_cast<int>(t)) + ":" + std::to_string(ms));
    return true;
  };
  TEST_ASSERT_EQUAL_STRING("OK", bv::answer(request_of("PERMIT POST pending"), h, {}).c_str());
  TEST_ASSERT_EQUAL_STRING("ERR PERMIT STICK refused",
                           bv::answer(request_of("PERMIT STICK fault"), h, {}).c_str());
  TEST_ASSERT_EQUAL_STRING("OK", bv::answer(request_of("CAL UNSAVED"), h, {}).c_str());
  TEST_ASSERT_EQUAL_STRING("OK", bv::answer(request_of("POST RERUN"), h, {}).c_str());
  TEST_ASSERT_EQUAL_STRING("OK", bv::answer(request_of("STALL CADC 1000"), h, {}).c_str());
  TEST_ASSERT_EQUAL_STRING("OK", bv::answer(request_of("STALL ADC 3000"), h, {}).c_str());
  const std::vector<std::string> want{"post1", "stick2",      "cal",
                                      "rerun", "stall2:1000", "stall1:3000"};
  TEST_ASSERT_EQUAL_size_t(want.size(), seen.size());
  for (std::size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(want[i].c_str(), seen[i].c_str());
  }
}

TEST_CASE("RUI-008 CRASH is never answered by the hooks: the remote UI replies and aborts",
          "[remote_ui][REQ-RUI-06]") {
  const std::string got = bv::answer(request_of("CRASH"), all_hooks(), std::nullopt);
  TEST_ASSERT_TRUE_MESSAGE(got.starts_with("ERR "), got.c_str());
}
