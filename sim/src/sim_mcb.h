/**
 * The Main Control Board, faked.
 *
 * On the bench this job belongs to scripts/rtps_mcb_gui.py, which plays the
 * MCB over the wire. The sim has no wire, so this feeds the same
 * rammp_mcb_status_t through the same entry point the RTPS receive task uses
 * (sim_nav_on_mcb_status), at the same RAMMP_MCB_STATUS_PERIOD_MS cadence the
 * spec requires of a real publisher.
 *
 * It is driven entirely from the keyboard (press F1 for the map). None of it
 * appears on the bench window, because none of it is something a hand reaches
 * for on the chair: the bench is the chair's control surface, not a test
 * console.
 */
#ifndef SIM_MCB_H
#define SIM_MCB_H

#ifdef __cplusplus
extern "C" {
#endif

/** Starts the status publisher and binds the hotkeys. */
void sim_mcb_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_MCB_H */
