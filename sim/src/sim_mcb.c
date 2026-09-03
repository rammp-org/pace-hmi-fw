#include "sim_mcb.h"

#include "sim_nav.h"

#include "sim_input.h"

#include "rammp_rtps_spec.h"

#include <stdio.h>
#include <string.h>

#include <windows.h>

/* The spec's timing contract: republish every RAMMP_MCB_STATUS_PERIOD_MS even
 * when nothing changed, because the HMI treats silence as a lost link. Holding
 * to it here means the sim exercises the staleness path for free -- stop this
 * timer and the labels grey out after RAMMP_MCB_STATUS_TIMEOUT_MS, exactly as
 * they would with the MCB unplugged. */
#define MCB_PUBLISH_PERIOD_MS RAMMP_MCB_STATUS_PERIOD_MS

/* Dwell for the hands-free walk, matching the `dwell` default in
 * scripts/rtps_mcb_sim.py --cycle. */
#define MCB_CYCLE_DWELL_MS 2000

typedef struct {
    rammp_mcb_status_t status;
    sim_link_state_t   link;
    bool               cycling;
    uint32_t           cycle_elapsed_ms;
    uint8_t            cycle_step;
} sim_mcb_t;

static sim_mcb_t m;

static const char * sim_mcb_link_name(sim_link_state_t link)
{
    switch(link) {
        case SIM_LINK_ETH_FAILED: return "ETH FAILED";
        case SIM_LINK_DOWN:       return "LINK DOWN";
        case SIM_LINK_NO_IP:      return "NO IP";
        case SIM_LINK_NO_PEER:    return "NO PEER";
        case SIM_LINK_CONNECTED:  return "CONNECTED";
        default:                  return "?";
    }
}

static void publish(void)
{
    m.status.seq++; /* free-running, wraps; same as a real publisher */
    sim_nav_on_mcb_status(&m.status);
}

/* ---------------------------------------------------------- transitions --- */

static void sim_mcb_set_drive_status(uint8_t drive_status)
{
    m.status.drive_status = drive_status;
    printf("[mcb] drive=%s\n", rammp_drive_status_name(drive_status));
}

static void sim_mcb_set_state(uint8_t system_state)
{
    m.status.system_state = system_state;
    /* The banner text is the MCB's to word, so the fake one has to supply it;
     * an empty error_text with system_state != OK would raise a blank panel. */
    if(system_state == RAMMP_STATE_OK) {
        m.status.error_text[0] = '\0';
        m.status.error_footer[0] = '\0';
    }
    else {
        snprintf(m.status.error_text, sizeof(m.status.error_text), "MOTOR FAULT");
        snprintf(m.status.error_footer, sizeof(m.status.error_footer), "Service required");
    }
    printf("[mcb] state=%s\n", rammp_state_name(system_state));
}

static void sim_mcb_bump_speed(int delta_tenths)
{
    int next = (int)m.status.speed_tenths + delta_tenths;
    if(next < 0) next = 0;
    if(next > RAMMP_SPEED_MAX_TENTHS) next = RAMMP_SPEED_MAX_TENTHS;
    m.status.speed_tenths = (uint8_t)next;
}

static void sim_mcb_cycle_link(void)
{
    m.link = (sim_link_state_t)((m.link + 1) % (SIM_LINK_CONNECTED + 1));
    sim_nav_set_link_state(m.link);
    printf("[mcb] link=%s\n", sim_mcb_link_name(m.link));
}

static void sim_mcb_toggle_cycle(void)
{
    m.cycling = !m.cycling;
    m.cycle_elapsed_ms = 0;
    printf("[mcb] cycle %s\n", m.cycling ? "started" : "stopped");
}

/* --------------------------------------------------------------- cycle --- */

/** The four state combinations rtps_mcb_sim.py --cycle walks. */
static void cycle_step(void)
{
    static const struct {
        uint8_t drive_status;
        uint8_t system_state;
    } steps[] = {
        {RAMMP_DRIVE_STATUS_INACTIVE, RAMMP_STATE_OK},
        {RAMMP_DRIVE_STATUS_ACTIVE, RAMMP_STATE_OK},
        {RAMMP_DRIVE_STATUS_ACTIVE, RAMMP_STATE_ERROR},
        {RAMMP_DRIVE_STATUS_INACTIVE, RAMMP_STATE_ERROR},
    };
    const uint8_t count = (uint8_t)(sizeof(steps) / sizeof(steps[0]));

    m.cycle_step = (uint8_t)((m.cycle_step + 1) % count);
    sim_mcb_set_drive_status(steps[m.cycle_step].drive_status);
    sim_mcb_set_state(steps[m.cycle_step].system_state);
}

static void sim_mcb_tick_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);

    /* Keyboard only: none of this is a control the chair has, so none of it
     * is on the bench window. */
    if(sim_input_key_edge('1')) sim_mcb_set_drive_status(RAMMP_DRIVE_STATUS_INACTIVE);
    if(sim_input_key_edge('2')) sim_mcb_set_drive_status(RAMMP_DRIVE_STATUS_ACTIVE);
    if(sim_input_key_edge('3')) sim_mcb_set_state(RAMMP_STATE_OK);
    if(sim_input_key_edge('4')) sim_mcb_set_state(RAMMP_STATE_ERROR);
    if(sim_input_key_edge(VK_OEM_MINUS)) sim_mcb_bump_speed(-1);
    if(sim_input_key_edge(VK_OEM_PLUS)) sim_mcb_bump_speed(1);
    if(sim_input_key_edge('L')) sim_mcb_cycle_link();
    if(sim_input_key_edge('C')) sim_mcb_toggle_cycle();

    if(m.cycling) {
        m.cycle_elapsed_ms += MCB_PUBLISH_PERIOD_MS;
        if(m.cycle_elapsed_ms >= MCB_CYCLE_DWELL_MS) {
            m.cycle_elapsed_ms = 0;
            cycle_step();
        }
    }

    publish();
}

void sim_mcb_init(void)
{
    memset(&m, 0, sizeof(m));
    m.status.drive_status = RAMMP_DRIVE_STATUS_INACTIVE;
    m.status.system_state = RAMMP_STATE_OK;
    m.status.speed_tenths = 0;
    m.link = SIM_LINK_CONNECTED;

    sim_nav_set_link_state(m.link);
    publish();

    lv_timer_create(sim_mcb_tick_cb, MCB_PUBLISH_PERIOD_MS, NULL);
}
