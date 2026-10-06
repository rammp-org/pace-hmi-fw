#pragma once
// The FIRMWARE UPDATE screen's texts: a release's date and size, the list status, and the
// install's percentage and progress. Plain C++, no LVGL (CS-UI-03).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hmi::format {

/// day_text's buffer: "-2147483648 Sep -2147483648" plus the terminator.
inline constexpr std::size_t DAY_TEXT_SIZE = 28;
/// megabytes_text's buffer: a 64-bit byte count ("18446744073709.6 MB") plus the terminator.
inline constexpr std::size_t MEGABYTES_TEXT_SIZE = 24;
/// release_count_text's buffer: a 64-bit count and the sentence, plus the terminator.
inline constexpr std::size_t RELEASE_COUNT_TEXT_SIZE = 64;
/// progress_text's buffer: two MEGABYTES texts, "-2147483648%" and the words, plus the terminator.
inline constexpr std::size_t PROGRESS_TEXT_SIZE = 64;

/// @brief A release's date as the screen shows it: "2026-09-30" reads "30 Sep 2026".
/// The date is read as sscanf's "%d-%d-%d" reads it: each number may have leading white space
/// and a sign, the '-' between them must follow at once, and anything after the third number
/// is ignored. The day is not checked; the month must be 1..12.
/// @param iso the date, "YYYY-MM-DD" (GitHub's published_at cut to 10 characters); it ends at
/// its first '\0', as a C string would
/// @param out the buffer, DAY_TEXT_SIZE fits the text; always terminated (unless empty)
/// @return false, with out empty, when it is not a date: the caller shows `iso` itself
/// @pre each number fits an int32 (one that does not saturates to INT32_MIN or INT32_MAX, as
/// the target's newlib strtol on a 32-bit long does; the host's glibc differs)
[[nodiscard]] bool day_text(std::string_view iso, std::span<char> out) noexcept;

/// @brief A byte count in megabytes to one decimal, "1.2 MB": bytes / 1e6 as a double, rounded
/// to nearest with ties to even (fmt's "{:.1f}"; 250000 reads "0.2 MB").
/// @param bytes the count
/// @param out the buffer, MEGABYTES_TEXT_SIZE fits the text; always terminated (unless empty)
void megabytes_text(std::size_t bytes, std::span<char> out) noexcept;

/// @brief The list status once releases came back: "N releases. Pick one to install it."
/// @param count how many releases there are
/// @param out the buffer, RELEASE_COUNT_TEXT_SIZE fits the text; always terminated (unless empty)
void release_count_text(std::size_t count, std::span<char> out) noexcept;

/// @brief The install's percentage: done * 100 / total in size_t (so it wraps where size_t
/// does), 0 while total is 0.
/// @param done bytes written
/// @param total bytes expected
/// @return the percentage; more than 100 when done passes total
[[nodiscard]] int32_t progress_pct(std::size_t done, std::size_t total) noexcept;

/// @brief The install's progress label while total > 0: "1.0 MB of 2.1 MB  (50%)".
/// @param done bytes written
/// @param total bytes expected
/// @param pct the percentage shown (the caller shows 100 once the install is done)
/// @param out the buffer, PROGRESS_TEXT_SIZE fits the text; always terminated (unless empty)
void progress_text(std::size_t done, std::size_t total, int32_t pct, std::span<char> out) noexcept;

} // namespace hmi::format
