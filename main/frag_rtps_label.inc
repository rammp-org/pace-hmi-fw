// split_main.py fragment of main.cpp (frag_rtps_label: original lines 519-566); one TU, see main.cpp.
#include "hmi_ui/rtps_label_view.hpp"

// The one instance, for every TopBar; bound from app_main and the screens built on demand.
static constinit hmi::ui::RtpsLabelView rtps_label_view{{.shared = &ui_shared_subjects}};
static void bind_rtps_label(lv_obj_t *bar) { rtps_label_view.bind(bar); }

/////////////////////////////////////////////////////////////////////////////
// DriveScreen: speed readout and the error banner
/////////////////////////////////////////////////////////////////////////////

