#pragma once
// The ErrorBanners that say why driving (or the seat) is not permitted: a refused request on the
// Locked screen and the screens a menu refusal lands on, and a drive cut short on the Drive and
// Seat screens.

#include <array>
#include <cstddef>
#include <cstdint>

#include "lvgl.h"

#include "messages/mib_message.hpp"

#include "hmi_ui/link_state.hpp"
#include "hmi_ui/refused.hpp"
#include "hmi_ui/shared_subjects.hpp"

namespace hmi::ui {

/// A banner's body and footer.
struct BannerLines {
  const char *body;
  const char *footer;
};

/// A banner's three lines.
struct BannerText {
  const char *title;
  const char *body;
  const char *footer;
};

/// The banners' words: the shared spec's (main fills this from hmi_rtps_spec.hpp), so the
/// MCB's logs and these banners use the same words.
struct RefusalTexts {
  BannerLines eth_failed, wifi_failed, link_down, wifi_down, no_ip, no_peer; ///< the link
  const char *mcb_no_text_fmt; ///< printf: %u the MIB state's value, %s its name
  const char *(*state_name)(MIB::MibSystemState state);
  BannerText drive_stopped, drive_not_granted, exit_refused; ///< the MIB's own refusals
  const char *link_refused_title, *mcb_refused_title;        ///< a refused push
  const char *seat_link_refused_title, *seat_mcb_refused_title;
  const char *drive_lost_link_title, *drive_lost_mcb_title;
  const char *link_lost_title, *mcb_fault_title; ///< a drive cut short
};

/// One instance for every refusal banner. `refused` records only THAT a request was refused;
/// the banners work out WHY from the link and state subjects whenever any of them changes, so
/// they always name the current cause. All words come from the shared spec
/// (hmi_rtps_spec.hpp, through rammp's texts).
class RefusalView {
public:
  struct Config {
    const SharedSubjects *shared;             ///< `rtps_link`, `mib_state` and `locked` are read
    const RefusalTexts *texts;                ///< the words
    lv_subject_t *refused;                    ///< int: Refused; banners up unless REFUSED_NONE
    bool (*wifi)();                           ///< the link is Wi-Fi (rtps_comms_net_link)
    bool (*mcb_ready)();                      ///< link CONNECTED and the MIB IDLE or ENABLED
    void (*play_refusal)(bool warning);       ///< the refusal sound (warning = louder)
    void (*keep_overlay_fill)(lv_obj_t *obj); ///< main's overdraw exemption
    bool (*button_held)();                    ///< the stick button, now (joy_button_held)
    int64_t (*now_us)();                      ///< esp_timer_get_time
    bool (*menu_open)();                      ///< the burger menu is open
    /// The drive session's ENTRY_PUSH input; true when its row acted (a refusal was raised).
    bool (*entry_push)();
    uint32_t grace_ms; ///< a press shorter than this is a tap, not a push (kBarGraceMs)
  };

  constexpr explicit RefusalView(const Config &config) noexcept
      : config_(config) {}

  /// The two text buffers' sizes: the shared spec's kErrorTextLen and kErrorFooterLen (main
  /// static_asserts them).
  static constexpr size_t ERROR_TEXT_SIZE = 64;
  static constexpr size_t ERROR_FOOTER_SIZE = 32;

  /// @brief Initialises the MIB's error text and footer subjects, empty. app_main, before any
  ///        banner binds (V7).
  void init_error_texts();
  /// string: the MIB's error_message ("" = none). Written by main's MibStatus handler under
  /// lvgl_mutex.
  [[nodiscard]] lv_subject_t *error_text() { return &error_text_; }
  /// string: the MIB's error_footer ("" = none), written the same way.
  [[nodiscard]] lv_subject_t *error_footer() { return &error_footer_; }

  /// @brief Binds a banner that names a refused request (Locked screen and the menu's
  ///        landing screens): on `refused` and every cause subject. Null skips the binding.
  /// app_main (after `refused` and the cause subjects are initialised, V7).
  void bind_refused_panel(lv_obj_t *panel);
  /// @brief Binds a banner that says the drive was cut short (Drive and Seat screens), or that
  ///        an exit was refused. Null skips the binding.
  /// app_main (same order rule).
  void bind_lost_panel(lv_obj_t *panel);
  /// @brief The error banners of the screens ui_init builds: the link-lost panel on Drive and
  ///        Seat, the refused-entry panel on Locked and on every screen whose banner has no
  ///        other job (Log, Joystick, BenchGate, Update, Internet, About).
  /// app_main, after the entry-refused subject is initialised, before lv_task starts.
  void bind_resident_banners();
  /// @brief Fills a banner with why driving is not permitted right now; the titles are the
  ///        caller's (a refused push and a drive cut short read differently).
  /// UI task, lvgl_mutex held.
  void fill_drive_blocked(lv_obj_t *panel, const char *link_title, const char *mcb_title);
  /// @brief Binds `cb` on `panel` (object-bound, with `user_data`) to every subject the cause
  ///        depends on: link, MIB state, error text and footer. Also for other views' banners
  ///        that word the same cause.
  /// app_main or a screen's *_ensure (UI task).
  void bind_to_cause(lv_obj_t *panel, lv_observer_cb_t cb, void *user_data);
  /// @brief Shows or hides a banner; a banner rising on the screen in front sounds the refusal.
  /// UI task, lvgl_mutex held.
  void show(lv_obj_t *panel, bool up) const;
  /// @brief Makes the dwell timer, paused: each refusal re-arms it (period, reset, resume), so
  ///        a second push restarts the countdown rather than stacking a timer.
  /// app_main, before lv_task starts.
  void start_timer(uint32_t period_ms);
  /// The dwell timer (null until start_timer): main's entry_refused_show re-arms it.
  [[nodiscard]] lv_timer_t *timer() const { return timer_; }
  /// @brief The dwell ran out (or the cause cleared): pause the timer, `refused` := NONE.
  /// UI task, lvgl_mutex held.
  void clear() const;
  /// @brief One hold poll's refusal check: a push held past the grace on the Locked screen goes
  ///        to the drive session (ENTRY_PUSH); a refusal whose cause cleared is cleared.
  /// UI task (main's hold poll timer), lvgl_mutex held.
  void poll();

private:
  [[nodiscard]] BannerLines link_text(LinkState link) const;
  void fill_mib_reason(lv_obj_t *panel, const char *title, const char *fallback_body,
                       const char *fallback_footer);
  static void refused_panel_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void lost_panel_observer(lv_observer_t *observer, lv_subject_t *subject);
  static void timer_cb(lv_timer_t *timer);

  Config config_;
  lv_subject_t error_text_{};
  lv_subject_t error_footer_{};
  std::array<char, ERROR_TEXT_SIZE> error_text_buf_{};
  std::array<char, ERROR_TEXT_SIZE> error_text_prev_buf_{};
  std::array<char, ERROR_FOOTER_SIZE> error_footer_buf_{};
  std::array<char, ERROR_FOOTER_SIZE> error_footer_prev_buf_{};
  lv_timer_t *timer_ = nullptr;     ///< the dwell timer
  int64_t pressed_at_us_ = 0;       ///< when the current press began; 0 = not pressed
  bool refused_this_press_ = false; ///< this press already raised its refusal
};

} // namespace hmi::ui
