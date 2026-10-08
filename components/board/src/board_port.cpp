#include "board/board_port.hpp"

#include "m5stack-tab5.hpp"

namespace hmi::board {

void backlight(float percent) { espp::M5StackTab5::get().brightness(percent); }

void present(const uint8_t *frame) { (void)espp::M5StackTab5::get().present_frame(frame); }

} // namespace hmi::board
