// split_main.py fragment of main.cpp (frag_display_flip: original lines 4237-4418); one TU, see main.cpp.
#include "hmi_ui/display_flip.hpp"
// DIRECT rendering and the screen flip are hmi::ui::DisplayFlip (components/hmi_ui).
// logger_nav is the nav code's; it stays here so the static-init order is unchanged.
static espp::Logger logger_nav({.tag = "nav", .level = espp::Logger::Verbosity::INFO});
static espp::Logger logger_flip({.tag = "flip", .level = espp::Logger::Verbosity::INFO});

// The board's swap, for the flush: present_frame waits for vsync. Its result
// was never used; a missed swap shows as one late frame.
static void present_on_panel(const uint8_t *frame) {
  (void)espp::M5StackTab5::get().present_frame(frame);
}

// The one DisplayFlip. app_main hands it the panel buffers and the touch input.
static constinit hmi::ui::DisplayFlip display_flip{
    {.log = &logger_flip, .present = present_on_panel, .refuse = refusal_feedback}};

// Settings "Flip screen" (frag_settings_ui's observer, and the boot-time apply).
static void set_display_flipped(bool on) { display_flip.set_flipped(on); }
