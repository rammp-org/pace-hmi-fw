#pragma once

/**
 * @file ota_update.hpp
 * @brief Firmware update over Ethernet, and the rollback that makes it safe.
 *
 * The protocol is "Firmware update over Ethernet" in hmi_rtps_spec.hpp; the
 * host side is scripts/rtps_ota.py. RTPS says who the device is and carries
 * the authenticated START; the image itself comes over a TCP connection the
 * device opens for that one update, framed by espp::OtaService.
 */

#include <string>

#include "hmi_rtps_spec.hpp"

/// Top of app_main. On the first boot of an update, arms the rollback: the
/// image goes back to the previous one unless ota_boot_confirm() runs within
/// rammp::kOtaConfirmTimeout.
void ota_boot_check();

/// Once Ethernet and RTPS are up. Confirms a freshly updated image (if the boot
/// is one) so the bootloader keeps it.
void ota_boot_confirm();

/// A command from the RTPS task. Ignored unless addressed to this device with
/// its current nonce.
void ota_handle_command(const rammp::OtaCommand &command);

/// Everything in OtaDeviceInfo but `seq` and `ip`, which the publisher owns.
rammp::OtaDeviceInfo ota_device_info();
