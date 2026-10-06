// The joystick calibration record: see cal_record.hpp. Moved from main/joystick_cal.cpp
// (describe, plausible, next_word, read_file, write_file) without a change in behaviour.

#include "cal_record.hpp"

#include <algorithm>
#include <limits>

#include "format.hpp"

namespace hmi::cal {

namespace {

class DecodeCategory final : public std::error_category {
public:
  const char *name() const noexcept override { return "joystick_cal"; }
  std::string message(int value) const override {
    switch (static_cast<DecodeError>(value)) {
    case DecodeError::NOT_VERSION_1:
      return fmt::format("not a version {} calibration file", kFileVersion);
    case DecodeError::NO_HORIZONTAL:
      return fmt::format("expected a '{} min center max' line", kAxisNames[0]);
    case DecodeError::NO_VERTICAL:
      return fmt::format("expected a '{} min center max' line", kAxisNames[1]);
    case DecodeError::NO_TWIST:
      return fmt::format("expected a '{} min center max' line", kAxisNames[2]);
    }
    return "unknown joystick_cal error";
  }
};

constexpr std::array<DecodeError, kAxisCount> kNoAxisLine{
    DecodeError::NO_HORIZONTAL, DecodeError::NO_VERTICAL, DecodeError::NO_TWIST};

// The next word that is not part of a # comment.
bool next_word(std::istream &in, std::string &word) {
  while (in >> word) {
    if (word[0] != '#') {
      return true;
    }
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return false;
}

} // namespace

const std::error_category &decode_category() noexcept {
  static const DecodeCategory category;
  return category;
}

std::error_code make_error_code(DecodeError e) noexcept {
  return {static_cast<int>(e), decode_category()};
}

std::string describe(const Record &record) {
  std::string out;
  for (std::size_t i = 0; i < kAxisCount; ++i) {
    out += fmt::format("{}{} {:.0f}/{:.0f}/{:.0f}", i ? ", " : "", kAxisNames[i], record[i].min_mv,
                       record[i].center_mv, record[i].max_mv);
  }
  return out + " mV";
}

bool plausible(const Record &record) {
  return std::all_of(record.begin(), record.end(), [](const AxisCal &a) {
    return a.center_mv - a.min_mv >= kFullTravelMv && a.max_mv - a.center_mv >= kFullTravelMv;
  });
}

std::string encode(const Record &record) {
  std::string text = fmt::format("# joystick calibration, raw ADC mV: min center max\n"
                                 "version {}\n",
                                 kFileVersion);
  for (std::size_t i = 0; i < kAxisCount; ++i) {
    text += fmt::format("{} {:.1f} {:.1f} {:.1f}\n", kAxisNames[i], record[i].min_mv,
                        record[i].center_mv, record[i].max_mv);
  }
  return text;
}

bool decode(std::istream &in, Record &out, std::error_code &ec) {
  std::string word;
  int version = 0;
  if (!next_word(in, word) || word != "version" || !(in >> version) || version != kFileVersion) {
    ec = DecodeError::NOT_VERSION_1;
    return false;
  }
  Record record{};
  for (std::size_t i = 0; i < kAxisCount; ++i) {
    if (!next_word(in, word) || word != kAxisNames[i] ||
        !(in >> record[i].min_mv >> record[i].center_mv >> record[i].max_mv)) {
      ec = kNoAxisLine[i];
      return false;
    }
  }
  out = record;
  ec.clear();
  return true;
}

} // namespace hmi::cal
