#pragma once
// The refusal banners' words, from the HMI's RTPS spec (components/hmi_rtps_spec), so the MCB's
// logs and these banners use the same words (moved from main.cpp's kRefusalTexts).

#include "hmi_rtps_spec.hpp"

#include "hmi_ui/refusal_view.hpp"

namespace hmi::ui {

static_assert(sizeof(rammp::kHmiEthFailedText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiLinkDownText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiWifiFailedText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiWifiDownText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiNoIpText) <= rammp::kErrorTextLen &&
                  sizeof(rammp::kHmiNoPeerText) <= rammp::kErrorTextLen,
              "refusal body outgrows the banner it shares with MCB faults");
static_assert(sizeof(rammp::kHmiEthFailedFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiLinkDownFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiWifiFailedFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiWifiDownFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiNoIpFooter) <= rammp::kErrorFooterLen &&
                  sizeof(rammp::kHmiNoPeerFooter) <= rammp::kErrorFooterLen,
              "refusal footer outgrows the banner it shares with MCB faults");

// The MIB's texts' buffers are the shared spec's.
static_assert(RefusalView::ERROR_TEXT_SIZE == rammp::kErrorTextLen &&
                  RefusalView::ERROR_FOOTER_SIZE == rammp::kErrorFooterLen,
              "RefusalView's error text buffers are the shared spec's");

/// The banners' words, from the shared spec.
inline constexpr RefusalTexts REFUSAL_TEXTS{
    .eth_failed = {rammp::kHmiEthFailedText, rammp::kHmiEthFailedFooter},
    .wifi_failed = {rammp::kHmiWifiFailedText, rammp::kHmiWifiFailedFooter},
    .link_down = {rammp::kHmiLinkDownText, rammp::kHmiLinkDownFooter},
    .wifi_down = {rammp::kHmiWifiDownText, rammp::kHmiWifiDownFooter},
    .no_ip = {rammp::kHmiNoIpText, rammp::kHmiNoIpFooter},
    .no_peer = {rammp::kHmiNoPeerText, rammp::kHmiNoPeerFooter},
    .mcb_no_text_fmt = rammp::kHmiMcbNoTextFmt,
    .state_name = [](MIB::MibSystemState state) { return rammp::to_string(state); },
    .drive_stopped = {rammp::kHmiDriveStoppedTitle, rammp::kHmiDriveStoppedText,
                      rammp::kHmiDriveStoppedFooter},
    .drive_not_granted = {rammp::kHmiDriveNotGrantedTitle, rammp::kHmiDriveNotGrantedText,
                          rammp::kHmiDriveNotGrantedFooter},
    .exit_refused = {rammp::kHmiExitRefusedTitle, rammp::kHmiExitRefusedText,
                     rammp::kHmiExitRefusedFooter},
    .link_refused_title = rammp::kHmiLinkRefusedTitle,
    .mcb_refused_title = rammp::kHmiMcbRefusedTitle,
    .seat_link_refused_title = rammp::kHmiSeatLinkRefusedTitle,
    .seat_mcb_refused_title = rammp::kHmiSeatMcbRefusedTitle,
    .drive_lost_link_title = rammp::kHmiDriveLostLinkTitle,
    .drive_lost_mcb_title = rammp::kHmiDriveLostMcbTitle,
    .link_lost_title = rammp::kHmiLinkLostTitle,
    .mcb_fault_title = rammp::kHmiMcbFaultTitle,
};

} // namespace hmi::ui
