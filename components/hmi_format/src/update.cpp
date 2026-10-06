// Moved from main/update_ui.cpp (kMonths :46 and day :50, megabytes :64, the list status count
// :156, run_refresh's percentage :268 and progress text :271-272) without a change in output
// (REQ-FMT-11, REQ-FMT-13).

#include "hmi_format/update.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <system_error>

#include "text_writer.hpp"

namespace hmi::format {

namespace {

constexpr std::array<std::string_view, 12> MONTHS{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
constexpr double BYTES_PER_MEGABYTE = 1e6;
constexpr std::size_t PERCENT = 100;

// Reads a date the way sscanf reads it: a character past the view, or a '\0' in it, ends it.
class DateReader {
public:
  explicit DateReader(std::string_view text) noexcept
      : text_(text) {}

  // "%d": white space, an optional sign, then at least one digit. A value past int32 saturates.
  bool read_int(int32_t &value) noexcept {
    while (is_space(peek())) {
      ++at_;
    }
    const bool negative = peek() == '-';
    if (negative || peek() == '+') {
      ++at_;
    }
    if (!is_digit(peek())) {
      return false;
    }
    int64_t magnitude = 0;
    for (; is_digit(peek()); ++at_) {
      if (magnitude <= MAGNITUDE_MAX) {
        magnitude = magnitude * 10 + (peek() - '0');
      }
    }
    value = saturate(negative ? -magnitude : magnitude);
    return true;
  }

  // A character of the format other than white space or a conversion: it must come next.
  bool read_literal(char c) noexcept {
    if (peek() != c) {
      return false;
    }
    ++at_;
    return true;
  }

private:
  // Past this, one more digit cannot bring the value back inside int32.
  static constexpr int64_t MAGNITUDE_MAX = int64_t{std::numeric_limits<int32_t>::max()} + 1;

  [[nodiscard]] char peek() const noexcept { return at_ < text_.size() ? text_[at_] : '\0'; }
  static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
  // isspace in the C locale
  static bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
  }
  static int32_t saturate(int64_t v) noexcept {
    if (v > std::numeric_limits<int32_t>::max()) {
      return std::numeric_limits<int32_t>::max();
    }
    if (v < std::numeric_limits<int32_t>::min()) {
      return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(v);
  }

  std::string_view text_;
  std::size_t at_ = 0;
};

// fmt's "{:.1f}" of bytes / 1e6: std::to_chars rounds the double exactly, ties to even.
void put_megabytes(detail::TextWriter &text, std::size_t bytes) noexcept {
  std::array<char, MEGABYTES_TEXT_SIZE> digits{};
  const double megabytes = static_cast<double>(bytes) / BYTES_PER_MEGABYTE;
  const auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), megabytes,
                                       std::chars_format::fixed, 1);
  if (ec == std::errc{}) { // cannot fail: 2^64 bytes is 16 characters
    text.put_text(std::string_view(digits.data(), static_cast<std::size_t>(end - digits.data())));
  }
  text.put_text(" MB");
}

} // namespace

bool day_text(std::string_view iso, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  DateReader reader(iso);
  int32_t y = 0;
  int32_t m = 0;
  int32_t d = 0;
  if (!reader.read_int(y) || !reader.read_literal('-') || !reader.read_int(m) ||
      !reader.read_literal('-') || !reader.read_int(d) || m < 1 ||
      m > static_cast<int32_t>(MONTHS.size())) {
    return false;
  }
  text.put_int(d);
  text.put_char(' ');
  text.put_text(MONTHS[static_cast<std::size_t>(m - 1)]);
  text.put_char(' ');
  text.put_int(y);
  return true;
}

void megabytes_text(std::size_t bytes, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  put_megabytes(text, bytes);
}

void release_count_text(std::size_t count, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  text.put_unsigned(count);
  text.put_text(" releases. Pick one to install it.");
}

int32_t progress_pct(std::size_t done, std::size_t total) noexcept {
  return total > 0 ? static_cast<int32_t>(done * PERCENT / total) : 0;
}

void progress_text(std::size_t done, std::size_t total, int32_t pct, std::span<char> out) noexcept {
  detail::TextWriter text(out);
  put_megabytes(text, done);
  text.put_text(" of ");
  put_megabytes(text, total);
  text.put_text("  (");
  text.put_int(pct);
  text.put_text("%)");
}

} // namespace hmi::format
