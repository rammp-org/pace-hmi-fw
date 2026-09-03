/**
 * The navigation and data layer, ported from main.cpp.
 *
 * Everything in main.cpp that touches the UI rather than the hardware lives
 * here: the lv_subject_t data model, the widget bindings and observers, the
 * hold-gesture engine behind "push up and hold to enter", the pager lock, and
 * the seat-screen button grids. It is a port, not a shared build -- see
 * sim/README.md on keeping it in step with main.cpp.
 */
#ifndef SIM_NAV_H
#define SIM_NAV_H

#include "lvgl.h"
#include "rammp_rtps_spec.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Plain-C mirror of RtpsLinkState in main/rtps_comms.hpp, which is a C++ enum
 * class in a header this C build cannot include. Same order, worst to best --
 * the TopBar indicator colours are keyed off it.
 */
typedef enum {
    SIM_LINK_ETH_FAILED = 0, /**< W5500 bring-up failed at boot */
    SIM_LINK_DOWN,           /**< no Ethernet link */
    SIM_LINK_NO_IP,          /**< link up, no DHCP lease */
    SIM_LINK_NO_PEER,        /**< have an IP, no MCB status inside the timeout */
    SIM_LINK_CONNECTED,      /**< MCB status arriving */
} sim_link_state_t;

/** Builds the data model and wires it to the screens ui_init() created. */
void sim_nav_init(void);

/** Called from the 33 ms input timer, after the stick has been sampled. */
void sim_nav_on_stick_sample(void);

/** Same entry point the firmware's RTPS receive task uses. */
void sim_nav_on_mcb_status(const rammp_mcb_status_t * status);

/** Fakes the link supervisor rather than watching a real timeout. */
void sim_nav_set_link_state(sim_link_state_t link_state);

/** Drops back to the boot screen and relocks, for the sim's reset hotkey. */
void sim_nav_reset(void);

/* ---------------------------------------------------------------------------
 * Hooks for the bench's button-mapping mockup (see sim_bench.h).
 *
 * NOTHING in the firmware calls these. They exist so a proposed button
 * function can be felt on screen before anyone commits it to main.cpp and the
 * wire spec. Each one does something the UI can already do by other means, so
 * none of them invents behaviour the board could not have; what is unproven is
 * whether a *button* should trigger it.
 * ------------------------------------------------------------------------ */

/** Back to MainScreenFlex, as the seat and drive exit gestures do. */
void sim_nav_go_home(void);

/** Step the DriveScreen's HOLO / Normal / Auto selection. */
void sim_nav_next_drive_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_NAV_H */
