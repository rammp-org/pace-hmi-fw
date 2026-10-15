// SEAT-011: the seat path's call-site guard (hazard-seat-spec.md §5.1, REQ-UI-28, REQ-UI-30).
// Reads the firmware sources as text (components/ and main/, test folders and the generated
// export left out, comments stripped): seat_raw is called only in components/seat (its
// definition in hmi_rtps_spec.hpp aside), the LinkPort's publish_seat only inside
// UiApp::seat_request, and rtps_comms_publish_seat is named only where it is declared, defined
// and handed to the LinkPort. spec-deviation(TS-UNIT-01): host-native g++ in WSL.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "test_case.hpp"

#ifndef SEAT_REPO_ROOT
#error "SEAT_REPO_ROOT: the Makefile passes the worktree's path"
#endif

namespace {

namespace fs = std::filesystem;

struct Source {
  std::string rel;  // repo-relative, '/' separators
  std::string code; // comments replaced by spaces
};

// Line and block comments blanked (string literals are not parsed: none of the names
// searched for appears in one).
std::string strip_comments(const std::string &in) {
  std::string out = in;
  for (std::size_t i = 0; i + 1 < out.size(); ++i) {
    if (out[i] == '/' && out[i + 1] == '/') {
      while (i < out.size() && out[i] != '\n') {
        out[i++] = ' ';
      }
    } else if (out[i] == '/' && out[i + 1] == '*') {
      while (i + 1 < out.size() && !(out[i] == '*' && out[i + 1] == '/')) {
        out[i] = out[i] == '\n' ? '\n' : ' ';
        ++i;
      }
      if (i + 1 < out.size()) {
        out[i] = ' ';
        out[i + 1] = ' ';
      }
    }
  }
  return out;
}

bool skipped(const std::string &rel) {
  return rel.starts_with("components/ui/") || rel.find("/test/") != std::string::npos ||
         rel.find("/tests/") != std::string::npos;
}

std::vector<Source> sources() {
  std::vector<Source> out;
  const fs::path root{SEAT_REPO_ROOT};
  for (const char *top : {"components", "main"}) {
    for (const auto &entry : fs::recursive_directory_iterator(root / top)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const std::string ext = entry.path().extension().string();
      if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".c" && ext != ".inc") {
        continue;
      }
      const std::string rel = fs::relative(entry.path(), root).generic_string();
      if (skipped(rel)) {
        continue;
      }
      std::ifstream file(entry.path(), std::ios::binary);
      std::ostringstream text;
      text << file.rdbuf();
      out.push_back({rel, strip_comments(text.str())});
    }
  }
  return out;
}

std::size_t count(const std::string &code, const std::regex &re) {
  return static_cast<std::size_t>(
      std::distance(std::sregex_iterator(code.begin(), code.end(), re), std::sregex_iterator()));
}

// The body of `void UiApp::seat_request(`: from its first '{' to the matching '}'.
std::string seat_request_body(const std::string &code) {
  const std::size_t head = code.find("void UiApp::seat_request(");
  if (head == std::string::npos) {
    return {};
  }
  const std::size_t open = code.find('{', head);
  int depth = 0;
  for (std::size_t i = open; i < code.size(); ++i) {
    depth += code[i] == '{' ? 1 : (code[i] == '}' ? -1 : 0);
    if (depth == 0) {
      return code.substr(open, i - open + 1);
    }
  }
  return {};
}

} // namespace

TEST_CASE("SEAT-011 seat_raw is called only in components/seat, the LinkPort's publish_seat only "
          "in UiApp::seat_request",
          "[seat][REQ-UI-28][REQ-UI-30][safety]") {
  const std::regex raw_call{R"(\bseat_raw\s*\()"};
  const std::regex port_call{R"((->|\.)\s*publish_seat\s*\()"};
  const std::regex impl_name{R"(\brtps_comms_publish_seat\b)"};
  const std::vector<Source> all = sources();
  TEST_ASSERT_TRUE_MESSAGE(all.size() > 50, "the firmware sources were not found");
  std::size_t raw_in_seat = 0;
  std::size_t port_calls = 0;
  for (const Source &s : all) {
    const std::size_t raw = count(s.code, raw_call);
    if (s.rel.starts_with("components/seat/")) {
      raw_in_seat += raw;
    } else if (s.rel == "components/hmi_rtps_spec/include/hmi_rtps_spec.hpp") {
      TEST_ASSERT_EQUAL_size_t_MESSAGE(1, raw, "hmi_rtps_spec.hpp: only seat_raw's definition");
    } else {
      TEST_ASSERT_EQUAL_size_t_MESSAGE(0, raw, ("seat_raw called in " + s.rel).c_str());
    }
    const std::size_t port = count(s.code, port_call);
    port_calls += port;
    if (port != 0) {
      TEST_ASSERT_EQUAL_STRING_MESSAGE("components/hmi_ui/src/ui_app.cpp", s.rel.c_str(),
                                       "publish_seat called outside ui_app.cpp");
      TEST_ASSERT_EQUAL_size_t_MESSAGE(1, count(seat_request_body(s.code), port_call),
                                       "publish_seat outside UiApp::seat_request");
    }
    if (count(s.code, impl_name) != 0) {
      TEST_ASSERT_TRUE_MESSAGE(s.rel == "components/net_link/src/rtps_comms.cpp" ||
                                   s.rel == "components/net_link/include/rtps_comms.hpp" ||
                                   s.rel == "main/main.cpp" || s.rel == "main/bench_hooks.cpp",
                               ("rtps_comms_publish_seat named in " + s.rel).c_str());
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(raw_in_seat >= 1, "seat_reading no longer calls seat_raw");
  TEST_ASSERT_EQUAL_size_t_MESSAGE(1, port_calls, "exactly one publish_seat call");
}
