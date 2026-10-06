#pragma once

/**
 * @file about_ui.hpp
 * @brief The AboutScreen (Settings > About in the burger menu): which firmware
 *        this is, whether it is a published release, and which board.
 *
 *   mark     green check: this image's SHA-256 is the digest of a GitHub
 *            release or pre-release (fw_info.hpp says how that is known);
 *            red cross: anything else, with the reason beside it
 *   firmware version (git describe at build time), commit, build date
 *   SHA-256  of the image in flash, as `sha256sum rammp-hmi-p4.bin` prints it
 *   device   the chip's MAC (also the USB serial number), link, IP, hostname
 *
 * Nothing on the screen can be selected; the stick only reaches the burger key.
 */

/// Once, after ui_init. Starts the timer that keeps the screen current while it is up.
void about_ui_init();

/// The AboutScreen came up: fill it now rather than on the next tick.
void about_ui_on_load();
