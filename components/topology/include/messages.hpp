#pragma once

/// @file messages.hpp
/// @brief Every message type the topology's CHANNELS table names (CS-OWN-05, CS-OWN-12).
/// @details DRAFT for review. The fields are a sketch: each adapter or island defines them for
///          real when it is written. What is fixed here is the shape every message keeps:
///          - a plain struct, trivially copyable, at most 128 bytes, no pointers or handles;
///          - marked `static constexpr bool IS_MESSAGE = true;` (fw_core's Message concept);
///          - `static constexpr std::string_view NAME`, equal to its CHANNELS row's `message`
///            column, so `Channel<Ch::X, M>` can check the pairing (topology.hpp).
///          This header does not include fw_core: fw_core's config.hpp pulls in sdkconfig.h on
///          the target, and topology.hpp stays free of ESP-IDF includes. The Message concept is
///          checked where storage is created (topology_espp.hpp instantiates fw_core's
///          channels) and by static_asserts in the host test (test/test_topology.cpp).
///          Wire strings become fixed char arrays in the adapter (plan §2.2 open points, H8).

#include <cstdint>
#include <string_view>

namespace hmi::topo {

/// @brief The longest text field in a message, including the terminating NUL.
inline constexpr std::uint32_t MSG_TEXT_SIZE = 32;

/// @brief The MCB's status, decoded and validated by rtps_rx (MibStatus).
struct McbStatusMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "McbStatusMsg";
  std::uint32_t sequence;         ///< Counts every sample; only goes up.
  std::uint8_t drive_state;       ///< Validated enum value (H8).
  std::uint8_t fault;             ///< Validated enum value.
  std::int16_t speed_cm_s;        ///< Validated, in range.
  char mode[MSG_TEXT_SIZE];       ///< Wire string copied into a fixed field.
  char state_text[MSG_TEXT_SIZE]; ///< Wire string copied into a fixed field.
  char seat_text[MSG_TEXT_SIZE];  ///< Wire string copied into a fixed field.
};

/// @brief The MCB's diagnostics (Diagnostics), for the diagnostics screen.
struct DiagMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "DiagMsg";
  std::uint32_t sequence;    ///< Counts every sample.
  std::uint16_t battery_mv;  ///< Pack voltage.
  std::int16_t motor_temp_c; ///< Hottest motor.
  char text[MSG_TEXT_SIZE];  ///< Free text, truncated.
};

/// @brief A command from the MCB to this HMI (start a self test, show a message).
struct HmiCommandMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "HmiCommandMsg";
  std::uint8_t command; ///< Validated enum value; an unknown one is dropped by rtps_rx.
  std::uint32_t arg;    ///< Command argument.
};

/// @brief A screen brightness the MCB asks for.
struct BrightnessMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "BrightnessMsg";
  std::uint8_t percent; ///< 0..100.
};

/// @brief The link state the net island classifies (up, down, which interface, peer age).
struct LinkMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "LinkMsg";
  std::uint8_t state;        ///< Down, Wi-Fi, Ethernet.
  bool peer_seen;            ///< An MCB sample arrived within the staleness limit.
  std::uint32_t peer_age_ms; ///< Since the last MCB sample.
  std::uint32_t transitions; ///< Counts every state change; only goes up (CS-OWN-04).
};

/// @brief What the user asked the drive session to do (CS-OWN-04: must not be lost).
struct DriveIntentMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "DriveIntentMsg";
  std::uint8_t intent;  ///< Enable, disable, profile change.
  std::uint8_t profile; ///< For a profile change.
};

/// @brief A seat move the user asked for.
struct SeatRequestMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "SeatRequestMsg";
  std::uint8_t axis; ///< Seat axis.
  std::int8_t step;  ///< -1, 0 (stop), +1.
};

/// @brief What the UI shows, for the control island's gate (screen, menu, overlay, heartbeat).
struct UiContextMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "UiContextMsg";
  std::uint8_t screen;     ///< The active screen.
  bool menu_open;          ///< A menu covers the screen.
  bool overlay;            ///< An overlay (self test, refusal) covers the screen.
  std::uint32_t heartbeat; ///< Counts UI frames; a stalled UI stops it (H4).
};

/// @brief The stick settings the user chose (dead band, sensitivity, inversion).
struct StickSettingsMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "StickSettingsMsg";
  std::uint16_t deadband_permille;    ///< Dead band.
  std::uint16_t sensitivity_permille; ///< Scale.
  bool invert_y;                      ///< Inversion.
};

/// @brief The drive session's state, for the UI (locked, driving, why it stopped).
struct DriveViewMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "DriveViewMsg";
  std::uint8_t state;        ///< Session state.
  std::uint8_t reason;       ///< Why it stopped or refused.
  std::uint8_t profile;      ///< Active profile.
  std::uint32_t transitions; ///< Counts every state change; only goes up (CS-OWN-04).
};

/// @brief The stick's scaled values, for the UI's bars.
struct StickViewMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "StickViewMsg";
  std::int16_t x_permille;     ///< -1000..1000.
  std::int16_t y_permille;     ///< -1000..1000.
  std::int16_t twist_permille; ///< -1000..1000.
  bool valid;                  ///< false: the stick failed its plausibility check.
};

/// @brief A navigation key the stick produced while the UI is in a menu.
struct NavKeyMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "NavKeyMsg";
  std::uint8_t key; ///< Up, down, left, right, enter.
};

/// @brief A stick-button edge, for the UI (hold timing, counters).
struct ButtonEdgeMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "ButtonEdgeMsg";
  bool pressed;        ///< The new level.
  std::uint32_t at_ms; ///< When, on the steady clock.
};

/// @brief A side-button or touch event.
struct UiInputMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "UiInputMsg";
  std::uint8_t source; ///< Side button or touch.
  std::uint8_t event;  ///< Press, release, point.
  std::int16_t x;      ///< Touch point.
  std::int16_t y;      ///< Touch point.
};

/// @brief One remote-UI request line (bench only), truncated to fit.
struct RemoteUiReqMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "RemoteUiReqMsg";
  std::uint16_t id; ///< Pairs a reply with its request.
  char line[96];    ///< The request text.
};

/// @brief One remote-UI reply chunk (bench only).
struct RemoteUiRepMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "RemoteUiRepMsg";
  std::uint16_t id; ///< The request it answers.
  bool last;        ///< The last chunk of this reply.
  char chunk[96];   ///< Reply text.
};

/// @brief A request to run a self test.
struct SelfTestReqMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "SelfTestReqMsg";
  std::uint8_t kind; ///< POST, extended.
};

/// @brief One self-test row's result, or the end of a run.
struct SelfTestEventMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "SelfTestEventMsg";
  std::uint16_t row;   ///< Check id.
  std::uint8_t result; ///< Pass, fail, unmeasurable, done.
  std::int32_t value;  ///< Measured value.
};

/// @brief The POST verdict, shared with the control island as one atomic value.
enum class PostVerdict : std::uint8_t {
  NOT_RUN, ///< POST has not finished: no motion (CS-SAF-03).
  PASS,    ///< POST passed.
  FAIL,    ///< POST failed: no motion, and the user is told.
};

/// @brief OTA progress or a result.
struct OtaEventMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "OtaEventMsg";
  std::uint8_t event;       ///< List ready, progress, done, failed.
  std::uint8_t percent;     ///< Progress.
  char text[MSG_TEXT_SIZE]; ///< A release tag or an error.
};

/// @brief A Wi-Fi scan result or join outcome.
struct WifiEventMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "WifiEventMsg";
  std::uint8_t event;       ///< Network found, scan done, joined, failed.
  std::int8_t rssi;         ///< dBm.
  char ssid[MSG_TEXT_SIZE]; ///< SSID, truncated.
};

/// @brief A chunk of log output for the UI's log ring.
struct LogChunkMsg {
  static constexpr bool IS_MESSAGE = true;
  static constexpr std::string_view NAME = "LogChunkMsg";
  std::uint8_t length; ///< Bytes used in text.
  char text[120];      ///< Raw log bytes.
};

/// @brief The name a CHANNELS row uses for a message type (its `message` column).
/// @details A struct gives its NAME; types that cannot carry a member (bool, enums)
///          specialise this.
template <class T> struct MessageName {
  static constexpr std::string_view VALUE = T::NAME; ///< The name.
};
/// @brief bool, for the atomic STICK_BUTTON channel.
template <> struct MessageName<bool> {
  static constexpr std::string_view VALUE = "bool"; ///< The name.
};
/// @brief PostVerdict, for the atomic POST_RESULT channel.
template <> struct MessageName<PostVerdict> {
  static constexpr std::string_view VALUE = "PostVerdict"; ///< The name.
};

} // namespace hmi::topo
