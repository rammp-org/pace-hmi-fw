#pragma once
// Canonical text for an answer, so goldens are plain strings, and a digest for long ones.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace ota_test {

struct ReleaseView {
  std::string_view tag;
  bool prerelease;
  std::string_view published, notes, url;
  std::size_t size;
  std::string_view sha256;
};

/// Printable ASCII as is; '\n' as "\n", '\\' as "\\", anything else as "\xHH".
inline std::string escaped(std::string_view s) {
  std::string out;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '\n') {
      out += "\\n";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c >= 0x20 && c < 0x7F) {
      out += ch;
    } else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\x%02X", c);
      out += buf;
    }
  }
  return out;
}

inline std::string describe_releases(const std::vector<ReleaseView> &rs, std::string_view error) {
  std::string out = "error=" + std::string(error) + " n=" + std::to_string(rs.size());
  for (const ReleaseView &r : rs) {
    out += "\n[tag=" + escaped(r.tag) + " pre=" + (r.prerelease ? "1" : "0") +
           " pub=" + escaped(r.published) + " url=" + escaped(r.url) +
           " size=" + std::to_string(r.size) + " sha=" + escaped(r.sha256) +
           " notes=" + escaped(r.notes) + "]";
  }
  return out;
}

inline std::string describe_fw_record(std::string_view tag, bool prerelease,
                                      std::string_view checked) {
  return "tag=" + escaped(tag) + " pre=" + (prerelease ? "1" : "0") +
         " checked=" + escaped(checked);
}

/// FNV-1a, 64 bits.
inline std::uint64_t fnv1a(std::string_view s) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 0x100000001b3ULL;
  }
  return h;
}

/// The text itself when short, else its length and FNV-1a: what a golden holds.
inline std::string digest(std::string_view text) {
  constexpr std::size_t kInlineMax = 700;
  if (text.size() <= kInlineMax) {
    return std::string(text);
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), "len=%zu fnv1a=%016llx", text.size(),
                static_cast<unsigned long long>(fnv1a(text)));
  return buf;
}

} // namespace ota_test
