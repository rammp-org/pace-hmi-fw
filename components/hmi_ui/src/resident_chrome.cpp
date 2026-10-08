#include "hmi_ui/resident_chrome.hpp"

#include "ui.h"

namespace hmi::ui {

void for_each_resident_chrome(void (*bind)(const ScreenChrome &chrome)) {
  const ScreenChrome kChrome[] = {
      // DRIVE goes "home", which while locked IS this screen; on it, the cell
      // only closes the menu. The unlock hold, or the menu's Drive row, is the way in.
      {ui_TopBar1, ui_DriveBand1, ui_MenuKey1, ui_MenuOverlay1, true},
      // DriveScreen too: DRIVE is how you back out of the menu without
      // picking a row, and it has to do that on the screen it goes back to.
      {ui_TopBar2, ui_DriveBand2, ui_MenuKey2, ui_MenuOverlay2, true},
      {ui_TopBar3, ui_DriveBand4, ui_MenuKey3, ui_MenuOverlay3, true},     // JoystickScreen
      {ui_TopBar4, ui_DriveBand3, ui_MenuKey4, ui_MenuOverlay4, true},     // SeatScreen
      {ui_TopBar5, ui_DriveBand11, ui_MenuKey11, ui_MenuOverlay11, true},  // BenchGateScreen
      {ui_TopBar6, ui_DriveBand5, ui_MenuKey6, ui_MenuOverlay6, true},     // LogScreen
      {ui_TopBar11, ui_DriveBand10, ui_MenuKey10, ui_MenuOverlay10, true}, // UpdateScreen
      {ui_TopBar12, ui_DriveBand12, ui_MenuKey12, ui_MenuOverlay12, true}, // InternetScreen
      {ui_TopBar13, ui_DriveBand13, ui_MenuKey13, ui_MenuOverlay13, true}, // AboutScreen
  };
  for (const ScreenChrome &c : kChrome) {
    bind(c);
  }
}

} // namespace hmi::ui
