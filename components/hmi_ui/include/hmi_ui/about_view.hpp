#pragma once
// The AboutScreen (Settings > About in the burger menu).

#include <array>
#include <cstdint>
#include <string>

#include "lvgl.h"

#include "drive_ui/link_state.hpp"
#include "hmi_ui/firmware_info.hpp"

namespace hmi::ui {

/// Which firmware this is, whether it is a published release, and which board:
///   mark     green check: this image's SHA-256 is the digest of a GitHub release or
///            pre-release; red cross: anything else, with the reason beside it
///   firmware version, commit, build date
///   SHA-256  of the image in flash, as `sha256sum rammp-hmi-p4.bin` prints it
///   device   the chip's MAC (also the USB serial number), link, IP, hostname
/// Nothing on the screen can be selected; the stick only reaches the burger key. Everything
/// it shows comes from main through the Config (the hash, the network, the board).
class AboutView {
public:
  /// How often the screen is refreshed while it is up.
  static constexpr uint32_t REFRESH_MS = 1000;

  struct Config {
    FirmwareInfo (*firmware)();               ///< what main knows of the image; any task's copy
    FirmwareIdentity (*identity)();           ///< the image's description and commit
    bool (*mac)(std::array<uint8_t, 6> &mac); ///< the base MAC; false if it cannot be read
    const char *(*link_name)();               ///< the link RTPS runs over: "Ethernet" / "WiFi"
    LinkState (*link_state)();                ///< what that link is doing
    std::string (*ip)();                      ///< its address; empty without one
    const char *(*hostname)();                ///< the board's hostname
  };

  constexpr explicit AboutView(const Config &config) noexcept
      : config_(config) {}

  /// @brief Fills what never changes and starts the timer that keeps the screen current
  ///        while it is up.
  /// app_main, once, after ui_init and before lv_task starts.
  void init();
  /// @brief The screen came up: fill it now rather than on the next tick.
  /// UI task.
  void on_load();

private:
  /// What the mark shows. Kept so the themeable colour is only set on a change: each call
  /// registers the label with the theme again.
  enum class Mark : uint8_t { NONE, RELEASE, NOT_RELEASE };

  void set_mark(Mark mark);
  void show_verdict(const FirmwareInfo &info);
  static void show_sha(const FirmwareInfo &info);
  static const char *link_words(LinkState state);
  void refresh();
  void fill_static();
  static void refresh_cb(lv_timer_t *timer);

  Config config_;
  Mark shown_mark_ = Mark::NONE;
};

} // namespace hmi::ui
