#pragma once

#include "format.hpp"

// for allowing easy serialization/printing of the
// Trigger class
template <> struct fmt::formatter<espp::Joystick> {
  // Presentation format: 'v' - value, 'r' - raw, 'b' - both.
  char presentation = 'v';

  // Parses format specifications of the form ['v' | 'r' | 'b'].
  template <typename ParseContext> constexpr auto parse(ParseContext &ctx) {
    // Parse the presentation format and store it in the formatter:
    auto it = ctx.begin(), end = ctx.end();
    if (it != end && (*it == 'v' || *it == 'r' || *it == 'b'))
      presentation = *it++;

    // TODO: Check if reached the end of the range:
    // if (it != end && *it != '}') throw format_error("invalid format");

    // Return an iterator past the end of the parsed range:
    return it;
  }

  template <typename FormatContext> auto format(espp::Joystick const &j, FormatContext &ctx) const {
    // the z axis is only shown when the joystick was configured with one, so
    // that 2-axis joysticks format exactly as they always have
    switch (presentation) {
    case 'v':
      return j.has_z_ ? fmt::format_to(ctx.out(), "{}, {}", j.position_, j.z_)
                      : fmt::format_to(ctx.out(), "{}", j.position_);
    case 'r':
      return j.has_z_ ? fmt::format_to(ctx.out(), "{}, {}", j.raw_, j.raw_z_)
                      : fmt::format_to(ctx.out(), "{}", j.raw_);
    case 'b':
      return j.has_z_ ? fmt::format_to(ctx.out(), "({}, {} -> {}, {})", j.raw_, j.raw_z_,
                                       j.position_, j.z_)
                      : fmt::format_to(ctx.out(), "({} -> {})", j.raw_, j.position_);
    default:
      // shouldn't get here!
      return fmt::format_to(ctx.out(), "{}", j.position_);
    }
  }
};
