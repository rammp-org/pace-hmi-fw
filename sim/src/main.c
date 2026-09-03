/**
 * Desktop simulator entry point.
 *
 * Stands in for app_main() in main/main.cpp: bring up a display, bring up an
 * input device, call ui_init(), then pump LVGL forever. Everything between
 * those steps that talks to a Tab5 -- the DSI panel, the ADCs, the GPIO
 * button, haptics, audio, the W5500 and the RTPS participant -- is either
 * replaced (display, input, MCB) or simply absent.
 */

#include "lvgl.h"
#include "ui.h"

#include "sim_bench.h"
#include "sim_input.h"
#include "sim_mcb.h"
#include "sim_nav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

/* The Tab5's DSI panel, and what main.cpp hands LVGL after setting
 * LV_DISPLAY_ROTATION_0. Portrait. */
#define PANEL_WIDTH  720
#define PANEL_HEIGHT 1280

/* 1280 logical pixels tall does not fit on most laptop screens, so the window
 * defaults to half size. The framebuffer is unscaled -- only the blit to the
 * window is scaled, so layout and text metrics are the panel's, not the
 * window's. */
#define DEFAULT_ZOOM 50

static void print_keymap(void)
{
    printf(
        "\n"
        "  RAMMP HMI simulator\n"
        "  ---------------------------------------------------------------\n"
        "  The bench window is the chair's control surface: a D-pad and two\n"
        "  buttons, and nothing else. Everything under 'Fake MCB' is keyboard\n"
        "  only, because none of it is a control the chair has.\n"
        "\n"
        "  The dropdown under each button is a MOCKUP for trying out what a\n"
        "  button might do. Only 'Joystick button' is in the firmware; the rest\n"
        "  are proposals and say so when pressed.\n"
        "\n"
        "  Stick\n"
        "    bench D-pad      hold a direction (same as holding an arrow key)\n"
        "    arrows / WASD    push the stick (hold to fill a gesture arc)\n"
        "    Q / E            twist left / right\n"
        "    Space            joystick button\n"
        "    mouse            the Tab5's touchscreen\n"
        "\n"
        "  Fake MCB (keyboard only)\n"
        "    1 / 2            drive status INACTIVE / ACTIVE\n"
        "    3 / 4            system state OK / ERROR\n"
        "    - / =            speed down / up (0.0 - 9.9)\n"
        "    L                cycle the RTPS link state\n"
        "    C                start/stop the hands-free state cycle\n"
        "\n"
        "  Sim\n"
        "    T                toggle day/night theme\n"
        "    R                reset to the boot screen\n"
        "    F1               reprint this map\n"
        "    Esc              quit\n"
        "  ---------------------------------------------------------------\n"
        "\n"
        "  Navigation is joystick-only, same as the board: push up and hold on\n"
        "  a page to enter it, then hold the button (or pull, on the seat\n"
        "  screen) to come back. The prompt at the bottom of each screen says\n"
        "  which.\n\n");
}

/**
 * Parks the bench beside the panel, so the two read as one instrument rather
 * than two windows you have to arrange by hand.
 *
 * Underneath is the natural place, but the panel is 1280 px tall: at --zoom
 * 100 on a 1080p-ish desktop it already fills the work area, and a bench
 * placed under it lands off the bottom of the screen where it is invisible and
 * unclickable. So fall back to the right of the panel, and clamp into the work
 * area either way -- a control surface you cannot reach is worse than an
 * untidy layout.
 */
static void place_bench_window(HWND panel, HWND bench)
{
    RECT panel_rect;
    RECT bench_rect;
    RECT work;

    if(panel == NULL || bench == NULL) return;
    if(!GetWindowRect(panel, &panel_rect)) return;
    if(!GetWindowRect(bench, &bench_rect)) return;
    if(!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return;

    const int width = bench_rect.right - bench_rect.left;
    const int height = bench_rect.bottom - bench_rect.top;

    int x = panel_rect.left;
    int y = panel_rect.bottom;

    if(y + height > work.bottom) {
        x = panel_rect.right;
        y = panel_rect.top;
    }

    if(x + width > work.right) x = work.right - width;
    if(y + height > work.bottom) y = work.bottom - height;
    if(x < work.left) x = work.left;
    if(y < work.top) y = work.top;

    SetWindowPos(bench, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int main(int argc, char ** argv)
{
    int zoom = DEFAULT_ZOOM;
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            zoom = atoi(argv[++i]);
        }
        else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: pace-hmi-sim [--zoom PERCENT]\n");
            print_keymap();
            return 0;
        }
    }
    if(zoom < 10) zoom = 10;
    if(zoom > 200) zoom = 200;

    /* The gesture and MCB narration is the console half of this tool, and it
     * is worthless if it arrives in 4 KB lumps after the fact. Straight to a
     * terminal stdout is line-buffered anyway, but redirected to a file or a
     * pipe it goes fully buffered, and MSVC's CRT quietly treats _IOLBF as
     * _IOFBF -- so unbuffered is the only setting that actually holds. The
     * volume is a handful of lines a minute; there is nothing to gain by
     * buffering it. */
    setvbuf(stdout, NULL, _IONBF, 0);

    lv_init();

    /* Two windows, two lv_displays. The panel one is exactly the Tab5's
     * 720x1280 and holds nothing but the firmware's own screens; the bench is
     * a separate window beside it. See sim_bench.h for why this cannot be
     * one wider window. */
    lv_display_t * display = lv_windows_create_display(
                                 L"RAMMP HMI simulator", PANEL_WIDTH, PANEL_HEIGHT, zoom,
                                 /*allow_dpi_override=*/false,
                                 /*simulator_mode=*/true);
    if(display == NULL) {
        fprintf(stderr, "[sim] could not create the display window\n");
        return 1;
    }

    lv_display_t * bench = lv_windows_create_display(
                               L"RAMMP HMI bench", SIM_BENCH_WIDTH, SIM_BENCH_HEIGHT, zoom,
                               /*allow_dpi_override=*/false,
                               /*simulator_mode=*/true);
    if(bench == NULL) {
        fprintf(stderr, "[sim] could not create the bench window\n");
        return 1;
    }

    /* Creating the bench second leaves it as LVGL's default display, which
     * would send ui_init() and every lv_screen_active() in sim_nav.c to the
     * wrong one. The panel is the default from here on; sim_bench_init() is
     * handed its display explicitly. */
    lv_display_set_default(display);

    /* The Tab5 has a GT911 touchscreen, so the mouse is not a sim-only
     * affordance -- it is the same input path a finger takes on the bench. */
    lv_windows_acquire_pointer_indev(display);
    lv_windows_acquire_pointer_indev(bench);

    sim_input_init(display);

    /* Same call, same place in the sequence as main.cpp: build all five
     * screens eagerly and load ui_BootScreen. */
    ui_init();

    sim_nav_init();
    sim_mcb_init();

    sim_bench_init(bench);

    print_keymap();

    HWND hwnd = lv_windows_get_display_window_handle(display);
    HWND bench_hwnd = lv_windows_get_display_window_handle(bench);
    place_bench_window(hwnd, bench_hwnd);

    /* Closing either window ends the run: leaving one orphaned on screen with
     * no way to drive it is worse than just exiting. */
    while(IsWindow(hwnd) && IsWindow(bench_hwnd)) {
        if(sim_input_key_edge(VK_ESCAPE)) break;
        if(sim_input_key_edge(VK_F1)) print_keymap();

        uint32_t next = lv_timer_handler();
        if(next == LV_NO_TIMER_READY) next = LV_DEF_REFR_PERIOD;
        Sleep(next);
    }

    return 0;
}
