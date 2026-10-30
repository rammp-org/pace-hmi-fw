/*
 * actions_spec.h - the slots on the SkunkWorksScreen.
 *
 * - One X line per button, drawn in table order: a title, a subtitle, and
 *   what it needs, greyed out otherwise: 0 nothing, 1 the MCB (link up and its
 *   state OK), 2 the MIB idle.
 * - What a button DOES is main.cpp's kActionRun table, in the same order: a
 *   local function, or a request to the MCB over RTPS.
 * - HMI-only: nothing here crosses the wire (see messages/joystick_message.hpp for that).
 */

#ifndef ACTIONS_SPEC_H
#define ACTIONS_SPEC_H

/* X(NAME, title, subtitle, needs) */
#define ACTIONS_TABLE(X)                                                                           \
  X(HAPTIC_TEST, "Haptic test", "Buzz the vibration motor", 0)                                     \
  X(SELF_TEST, "Self test", "Check the HMI, results on screen", 0)                                 \
  X(SEAT_UP, "Seat up", "Ask the MCB to raise M1 one step", 1)                                     \
  X(PUSH_ASSIST, "Push assist", "An assistant drives from the handle", 2)                          \
  X(FPS_COUNTER, "FPS counter", "Show or hide the render overlay", 0)                              \
  X(RESTART_HMI, "Restart HMI", "Reboot this display", 0)

/* ACTION_HAPTIC_TEST, ..., ACTION_COUNT */
enum {
#define ACTIONS_ENUM(name_, title_, subtitle_, mcb_) ACTION_##name_,
  ACTIONS_TABLE(ACTIONS_ENUM)
#undef ACTIONS_ENUM
      ACTION_COUNT
};

#endif /* ACTIONS_SPEC_H */
