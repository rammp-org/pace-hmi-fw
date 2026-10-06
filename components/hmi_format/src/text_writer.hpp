#pragma once
// Private to hmi_format: appends text to a caller's buffer the way snprintf fills one, so the
// formatters need no printf (no varargs, no LVGL, the same bytes on host and target).

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>

namespace hmi::format::detail {

/// @brief Fills a buffer as snprintf does: what fits of size - 1 chars, always terminated.
/// A zero-size buffer is never written. Text past the end is dropped, not wrapped.
class TextWriter {
public:
  /// @param out the buffer; terminated at once, so it holds a valid string from the start
  explicit TextWriter(std::span<char> out) noexcept
      : out_(out) {
    terminate();
  }

  /// @brief Appends a string.
  /// @param text the characters to append
  void put_text(std::string_view text) noexcept {
    for (const char c : text) {
      put_char(c);
    }
  }

  /// @brief Appends one character.
  /// @param c the character
  void put_char(char c) noexcept {
    if (len_ + 1U < out_.size()) {
      out_[len_] = c;
      ++len_;
      terminate();
    }
  }

  /// @brief Appends an integer in decimal, with '-' when negative (printf's "%d").
  /// @param value the integer
  void put_int(int32_t value) noexcept {
    std::array<char, INT_TEXT_SIZE> digits{};
    const auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec == std::errc{}) { // cannot fail: INT_TEXT_SIZE holds "-2147483648"
      put_text(std::string_view(digits.data(), static_cast<std::size_t>(end - digits.data())));
    }
  }

  /// @brief Appends an unsigned integer in decimal (fmt's "{}" of a size_t).
  /// @param value the integer
  void put_unsigned(uint64_t value) noexcept {
    std::array<char, UNSIGNED_TEXT_SIZE> digits{};
    const auto [end, ec] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec == std::errc{}) { // cannot fail: UNSIGNED_TEXT_SIZE holds 2^64 - 1
      put_text(std::string_view(digits.data(), static_cast<std::size_t>(end - digits.data())));
    }
  }

private:
  static constexpr std::size_t INT_TEXT_SIZE = 12;      // "-2147483648"
  static constexpr std::size_t UNSIGNED_TEXT_SIZE = 20; // "18446744073709551615"

  void terminate() noexcept {
    if (!out_.empty()) {
      out_[len_] = '\0';
    }
  }

  std::span<char> out_;
  std::size_t len_ = 0;
};

} // namespace hmi::format::detail
